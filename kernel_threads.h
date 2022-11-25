#ifndef __KERNEL_THREAD_H
#define __KERNEL_THREAD_H

#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"
#include "util.h"

//The kernel_threads.h file

/*Here we have to declare process_thread_control_block, so we can make our processes multithreading.
Now for each process, we use an rl-node struct from our util file and in this we save a process-thread object.
*/

//Declaration of new block PTCB

typedef struct  process_thread_control_block
{
  PCB* parent;
  TCB* thread;     //Threads belong now to ptcbs */
  
  Task main_task;       // The main thread's function */
  int argl;             // The main thread's argument length */
  void* args;           // The main thread's argument string */
 
  int exitval;          // The exit value */  
  
  int detached; //(0 or 1)
  int exited; //If a thread is exited (0 or 1)
  
  CondVar Jointhreads;

  int ref_count;
  
  rlnode ptcb_node; //Node to add a ptcb //-FAULT- Veltistopoisi lathous sto 1o meros

} PTCB;

#endif

