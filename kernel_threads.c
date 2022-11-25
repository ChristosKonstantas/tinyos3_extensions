#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_threads.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

#include "util.h"


Mutex kernel_mutex = MUTEX_INIT;

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  PTCB* cur_ptcb=xmalloc(sizeof(PTCB));

  cur_ptcb->thread=spawn_thread(CURPROC,start_new_thread); //Spawn a thread for our new ptcb node and then ./ Start Thread ./ Exit Thread

  cur_ptcb->main_task=task;
  cur_ptcb->argl=argl;      
  cur_ptcb->args=args;

  cur_ptcb->Jointhreads=COND_INIT;
  cur_ptcb->detached=0;
  cur_ptcb->exited=0;

  cur_ptcb->thread->owner_ptcb = cur_ptcb;

  cur_ptcb->ref_count=1;

  rlnode_init(&cur_ptcb->ptcb_node,cur_ptcb);
  rlist_push_back(&CURPROC->ptcbs,&cur_ptcb->ptcb_node);

  CURPROC->thread_counter++; //Need to sysinfo!

  wakeup(cur_ptcb->thread); //Send signal to the scheduler so that the thread becomes ready //-FAULT- Edo itan to lathos sto 1o meros
  
  Tid_t tid=(Tid_t)cur_ptcb;
 // fprintf(stderr, "CREATE BOBA logika\n");  
	return tid;

}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) CURTHREAD;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{   

  PTCB* ptcbg;
  ptcbg= (PTCB*) tid; //logika ginetai. ekei kolage kai evgaze seg fault

  rlnode* node;
  
  //fprintf(stderr, "Xmalloc\n");

  //exitval=xmalloc(sizeof(int));

 // node=rlist_find(&(CURTHREAD->owner_pcb->ptcbs),ptcbg,NULL);
  /*Mas gyrnage panta null*/

  node = ptcbs_search_method (&(CURTHREAD->owner_pcb->ptcbs),ptcbg,NULL);

  if(node == NULL) {
    //fprintf(stderr, "kanei ret -1\n");

    return -1 ;
  }
  

  PTCB* join_ptcb = node->ptcb;

  if (CURTHREAD == join_ptcb->thread || join_ptcb->exited == 1)
    return -1;

  join_ptcb->ref_count++;

  while (join_ptcb->exited!=1 && join_ptcb->detached!=1)
  {
  	kernel_wait(&join_ptcb->Jointhreads, SCHED_USER);
  }

  if (exitval==NULL)
  {
    join_ptcb->ref_count--;
    if(join_ptcb->ref_count==0)
    {
      rlist_remove(&join_ptcb->ptcb_node);
      free(join_ptcb);
      return 0;
    }
  }

  if (join_ptcb->detached == 1)
  {   
    *exitval=join_ptcb->exitval;
     join_ptcb->ref_count--;
    if(join_ptcb->ref_count==0)
    {
      rlist_remove(&join_ptcb->ptcb_node);
      free(join_ptcb);
    }
    return -1;
  }
  else
  {
    *exitval=join_ptcb->exitval;
    join_ptcb->ref_count--;
    if(join_ptcb->ref_count==0)
    {
      rlist_remove(&join_ptcb->ptcb_node);
      free(join_ptcb);
    }
    return 0;
  }
  
  return -1;
}



/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)

{
  TCB* the_tcb = (TCB*) tid;
  PTCB* ptcb_owner;
  ptcb_owner=the_tcb->owner_ptcb;

  
  rlnode* ptcbs = &CURPROC->ptcbs;
  rlnode* node=(rlnode*)xmalloc(sizeof(rlnode));
  rlnode* fail=(rlnode*)xmalloc(sizeof(rlnode));
  node=rlnode_init(node, NULL);
  fail=rlnode_init(fail, NULL);
  node=ptcbs_search_method(ptcbs,ptcb_owner,fail);

  if(node==fail || the_tcb->state==EXITED)
     return -1;

  ptcb_owner->detached=1;
  Cond_Broadcast(&ptcb_owner->Jointhreads);
	
  return 0; 
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  TCB* thread=CURTHREAD;

  PTCB* owner_ptcb=thread->owner_ptcb;

  rlnode* node;
  
  owner_ptcb->exited=1;
  owner_ptcb->exitval=exitval;
  
  Cond_Broadcast(&owner_ptcb->Jointhreads);

  if (owner_ptcb->detached==1)
  {
    rlist_remove(&thread->owner_ptcb->ptcb_node);
    free(owner_ptcb);
  }
  else
  {
    owner_ptcb->ref_count--;
    if(owner_ptcb->ref_count==0)
    {
      node = rlist_find(&(CURTHREAD->owner_pcb->ptcbs),owner_ptcb,NULL);
      rlist_remove(node);
      free(owner_ptcb);
    }
  }

  /* Bye-bye cruel world */
        fprintf(stderr, "BB CRUEL WORLD\n");

  kernel_sleep(EXITED, SCHED_USER);		
}



