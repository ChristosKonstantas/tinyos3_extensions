#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "kernel_threads.h"
#include "kernel_dev.h"


/* 
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];

unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->ptcbs, NULL);
  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
  pcb->child_exit = COND_INIT;

  rlnode_init(& pcb->ptcbs, NULL);
}

static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }
  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}

 


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}

/*
 *
 * Process creation
 *
 */

/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;
  
  exitval = call(argl,args);

  rlist_pop_front(&CURPROC->ptcbs);
  free(CURTHREAD->owner_ptcb);

  Exit(exitval);
}

void start_new_thread()
{
  int exitval;

  Task call =  CURTHREAD->owner_ptcb->main_task;
  int argl = CURTHREAD->owner_ptcb->argl;
  void* args = CURTHREAD->owner_ptcb->args;
  exitval = call(argl,args);

 // CURTHREAD->owner_ptcb->exitval=exitval;

  sys_ThreadExit(exitval);
}

/*
  System call to create a new process.
 */

Pid_t sys_Exec(Task call, int argl, void* args)
{ PCB *curproc, *newproc;
  
  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process) 
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }


  /* Set the main thread's function */
  newproc->main_task = call;


  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;

  /* 
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if(call != NULL) {

    PTCB *  main_ptcb =(PTCB*)xmalloc(sizeof(PTCB));

    newproc->main_thread = spawn_thread(newproc, start_main_thread);

    newproc->thread_counter=1;

    main_ptcb->thread=newproc->main_thread;
    main_ptcb->main_task=call;
    main_ptcb->argl=argl;      
    main_ptcb->args=args;
    
    main_ptcb->Jointhreads=COND_INIT;

    main_ptcb->thread->owner_ptcb = main_ptcb;

    main_ptcb->detached=1;
    main_ptcb->ref_count=1;


    main_ptcb->ptcb_node=*rlnode_init(&main_ptcb->ptcb_node,main_ptcb);

    rlist_push_back(& newproc->ptcbs,&main_ptcb->ptcb_node);

    wakeup(newproc->main_thread);
  }


finish:
  return get_pid(newproc);
}


/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}


Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    kernel_wait(& parent->child_exit, SCHED_USER);
  
  cleanup_zombie(child, status);
  
finish:
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  if(is_rlist_empty(& parent->children_list)) {
    cpid = NOPROC;
    goto finish;
  }

  while(is_rlist_empty(& parent->exited_list)) {
    kernel_wait(& parent->child_exit, SCHED_USER);
  }

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

finish:
  return cpid;
}


Pid_t sys_WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void sys_Exit(int exitval)
{
  /* Right here, we must check that we are not the boot task. If we are, 
     we must wait until all processes exit. */
  if(sys_GetPid()==1) {
    while(sys_WaitChild(NOPROC,NULL)!=NOPROC);
  }

  PCB *curproc = CURPROC;  /* cache for efficiency */

  /* Do all the other cleanup we want here, close files etc. */
  if(curproc->args) {
    free(curproc->args);
    curproc->args = NULL;
  }

  /* Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  }

  /* Reparent any children of the exiting process to the 
     initial task */
  PCB* initpcb = get_pcb(1);
  while(!is_rlist_empty(& curproc->children_list)) {
    rlnode* child = rlist_pop_front(& curproc->children_list);
    child->pcb->parent = initpcb;
    rlist_push_front(& initpcb->children_list, child);
  }

  /* Add exited children to the initial task's exited list 
     and signal the initial task */
  if(!is_rlist_empty(& curproc->exited_list)) {
    rlist_append(& initpcb->exited_list, &curproc->exited_list);
    kernel_broadcast(& initpcb->child_exit);
  }

  /* Put me into my parent's exited list */
  if(curproc->parent != NULL) {   /* Maybe this is init */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);
  }

  /* Disconnect my main_thread */
  curproc->main_thread = NULL;

  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;
  curproc->exitval = exitval;

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}

/* 
---------------------------------

OPEN INFO SYSTEM CALL

COMMENT: We use the default scheduller (Round Robin)

---------------------------------
*/

static file_ops cthulhu = {
.Open = NULL,
.Read = cthulhu_read,
.Write = NULL,
.Close = cthulhu_close
};

int cthulhu_close (void *pi)
{
  procinfo* pinfo = (procinfo*)pi;
  free(pinfo);
  return 1;

}

int cthulhu_read (void *pi, char* buf, unsigned int size)
{
  procinfo* pinfo = (procinfo*)pi;
  void* pi_args = PT[pinfo->read_count].args;
  int i;

  if(pinfo->read_count>=MAX_PROC)
    return 0;

  while(pinfo->read_count<MAX_PROC)
  {
    if (PT[pinfo->read_count].pstate != FREE)
    {
      pinfo->pid =(Pid_t) get_pid(&PT[pinfo->read_count]);
      pinfo->ppid =(Pid_t) get_pid(PT[pinfo->pid].parent);

      pinfo->alive=PT[pinfo->read_count].pstate==ALIVE ? 1 :  0;

      pinfo->thread_count=(unsigned long) PT[pinfo->read_count].thread_counter+1;
      pinfo->main_task=PT[pinfo->read_count].main_task;
      pinfo->argl=PT[pinfo->read_count].argl;

      if (PT[pinfo->read_count].args)
      {
        for (i=0; i<PROCINFO_MAX_ARGS_SIZE; i++)
        {
          pinfo->args[i] = *(char*)pi_args;

          pi_args++; //size of char = 1
        }
      } 
      memcpy(buf,(char*)pinfo,size); //READ
      size=sizeof(pinfo);
      pinfo->read_count++;

      return size;
    }
    pinfo->read_count++;  
  }
  return 0;
}

Fid_t sys_OpenInfo()
{
  procinfo* pinfo=(procinfo*)xmalloc(sizeof(procinfo));
  FCB* fcb;
  Fid_t fid;

  if(!FCB_reserve(1,&fid,&fcb))
   return -1;

  fcb->streamobj=pinfo;
  fcb->streamfunc=&cthulhu;
  pinfo->read_count=0;

  return fid;
}

