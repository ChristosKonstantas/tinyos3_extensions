#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_sched.h"
#include "util.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

#define numOfFCBs 2

file_ops pipe_reader = {
  .Open = NULL,
  .Read = pipe_read,
  .Write = NULL,
  .Close = pipe_reader_close
};

file_ops pipe_writer = {
  .Open = NULL,
  .Read = NULL,
  .Write = pipe_write,
  .Close = pipe_writer_close
};

int sys_Pipe(pipe_t* pipe)
{
  Fid_t fid[2];
  FCB  *fcb[2];

   if(! FCB_reserve(numOfFCBs, fid, fcb))//this function reserves 2 fcbs and 2 fids
    {
	    fid[0] = NOFILE; // -1
	    fid[1] = NOFILE; // -1
	    return -1;//return error code
    } 

    pipe->read=fid[0]; //fid for reader
    pipe->write=fid[1];//fid for writer
    int retVal=Initialize_Pipe(fcb[0],fcb[1]);

   return retVal;
}

int Initialize_Pipe(FCB* fcb1,FCB* fcb2)
{
    FCB  *fcb[2];
    fcb[0]=fcb1;
    fcb[1]=fcb2;
    PIPE_CB* pipe_cb=(PIPE_CB*)xmalloc(sizeof(PIPE_CB));
    fcb[0]->streamobj =fcb[1]->streamobj= pipe_cb;
    fcb[0]->streamfunc = &pipe_reader;
    fcb[1]->streamfunc = &pipe_writer;
    pipe_cb->reader=fcb[0];
    pipe_cb->writer=fcb[1];
    pipe_cb->In_Cv=COND_INIT;
    pipe_cb->Out_Cv=COND_INIT;
    pipe_cb->w=0; //Write pointer on this ring buffer
    pipe_cb->r=0; //Read pointer - HEAD on this ring buffer
    
    for(int i=0; i<BUF_SIZE;i++){
       pipe_cb->buffer[i]=0;
    }
 
   return 0; //success retval
}

/* Usefull function to read a char from buffer */

char get_char(PIPE_CB* pipe)
{
  char retVal; 
  
  while ((pipe->w)==0)
  {
    kernel_wait(&pipe->Out_Cv,SCHED_PIPE);//Wait on Output-consumer cv
  }

  retVal=(pipe->buffer)[pipe->r];
  pipe->r=(pipe->r+1)%BUF_SIZE; //We use buffer as ring instaid of a simple array
  pipe->w--;

  Cond_Broadcast(&pipe->In_Cv);//Broadcast Input-producer cv

  return retVal;

}

/* Usefull function to write a char into buffer */


void put_char(char ch, PIPE_CB* pipe) 
{
    while((pipe->w)==BUF_SIZE)
    {
      kernel_wait(&pipe->In_Cv,SCHED_PIPE); // Buffer is full! Wait on Input Cv
    }
    
    (pipe->buffer)[(pipe->r+pipe->w)%BUF_SIZE]=ch;//We use buffer as ring instaid of a simple array
    pipe->w++;

    
    Cond_Broadcast(&pipe->Out_Cv);//Broadcast consumer cv
}

int pipe_write(void* pipe, const char* buf, unsigned int size)
{
  	PIPE_CB* pipe_cb = (PIPE_CB*) pipe;

  	 if(pipe_cb->writer==NULL || pipe_cb->reader==NULL) 
  	   return -1; //Fail!
  	
	 int i=0;
   int noOfWr=0;

   pipe_cb->r=0;

	 while(i < size)
     { 
       put_char(buf[i],pipe_cb);//Call put_char every single time
       noOfWr++;
       i++;
     }

	return noOfWr;
}

int pipe_read(void* pipe, char *buf, unsigned int size)///den prepei na pairnei san orisma to pipe_t pou exei mesa ta fids
{
        
  PIPE_CB* pipe_cb= (PIPE_CB*) pipe;

  char *temp=(char*)malloc(sizeof(char)*size);

  int i=-1;
  char ch; 
  int counter=size;//We count how much elements have we read
  int noOfRd=0;
  

   if(pipe_cb->reader==NULL)
    return -1; //Fail!  	

  if(pipe_cb->writer!=NULL)
  {
    do
    {
      i=i+1;
      ch=get_char(pipe_cb);//Call get_char every single time 
      temp[i]=ch;
      counter--;
      noOfRd++;
    }while(pipe_cb->w!=0 && counter>0);
          
    memcpy(buf,temp,strlen(temp));
    return noOfRd;
  }        
  else
    return 0; //EOF
}

int pipe_writer_close(void* pipe)
{
 	PIPE_CB* new_pipe= (PIPE_CB*) pipe;//an kai ta dio fid einai adeia
 	new_pipe->writer=NULL;
 	if(new_pipe->reader==NULL)
 		free(pipe);
   
     return 0;
}

int pipe_reader_close(void* pipe)
{

  	PIPE_CB* new_pipe= (PIPE_CB*) pipe;
  	new_pipe->reader=NULL;

  	if(new_pipe->writer==NULL) 
    	free(pipe);
    return 0;
}

