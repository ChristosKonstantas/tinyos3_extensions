
#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_sched.h"
#include "util.h"
#include "kernel_cc.h"
#include "kernel_streams.h"


void initialize_PORT_MAP (){

for(int i=0;i<MAX_PORT+1; i++)
{   
  PORT_MAP[i]=(SCB*)xmalloc(sizeof(SCB));
  PORT_MAP[i]->lcb=(LCB*)xmalloc(sizeof(LCB));
  rlnode_init(& (PORT_MAP[i]->lcb->queue), NULL);
  PORT_MAP[i]->lcb->flag=0;
}

}


int socket_close(void* socket)
{
   SCB* the_socket= (SCB*) socket;
   free(the_socket);
  return -1;
}

int socket_write(void* socket, const char* buf, unsigned int size)
{ 
  SCB* the_socket= (SCB*) socket;

  if(the_socket==NULL) 
    return -1;

 int write_count=0;
 if(the_socket->peercb==NULL)
   return -1;

 write_count=pipe_write(the_socket->peercb->pipe->writer,buf,size);    
 return write_count;
}

int socket_read(void* socket, char *buf, unsigned int size)
{
  int read_count=0;
  SCB* the_socket= (SCB*) socket;/*  convert to SCB*/
  if(the_socket==NULL || the_socket->peercb==NULL)
   	return -1;
  read_count=pipe_read(the_socket->peercb->pipe->reader,buf,size);//call pipe_read for the receiver
  return read_count;
}

file_ops socket_ops = {
  .Open = NULL,
  .Read = socket_read,
  .Write = socket_write,
  .Close = socket_close
};


Fid_t sys_Socket(port_t port)
{

	if((port<0)||(port>MAX_PORT))
	 return NOFILE;

  Fid_t fid;
  FCB* fcb;

  if(! FCB_reserve(1, &fid, &fcb))
  {
    fid = NOFILE;
    return -1;
  } 

  SCB* socket=(SCB*)xmalloc(sizeof(SCB));
  fcb->streamobj =socket;
  fcb->streamfunc = &socket_ops;
    
    
  socket->sfcb=fcb;
  socket->refcount = 0;
  socket->type=UNBOUND;

  socket->port=port==NOPORT ? NOPORT : port;

   return fid;////return success code
}

int sys_Listen(Fid_t sock)
{      
  FCB* fcb = get_fcb(sock);
    
  if(fcb!=NULL && sock>=0) 
  {                       
    SCB* sobj = fcb->streamobj;
    SCB* new_socket =  sobj;
  
    if(new_socket->type==PEER )
    {        
      return -1;          
    }

    if(new_socket!=NULL)
    {     
      if(new_socket->port==NOPORT)
        return -1;
    
      if(PORT_MAP[new_socket->port]!=NULL)
        {
          SCB* temp=PORT_MAP[new_socket->port]; 
          if(temp->type==LISTENER )             
            return -1;          
        }
        PORT_MAP[new_socket->port]=new_socket;
        new_socket->type=LISTENER;
        new_socket->lcb=(LCB*)xmalloc(sizeof(LCB));
        new_socket->lcb->req = COND_INIT;

        rlnode_init(&(new_socket->lcb->queue), NULL);

        return 0;
        }
    }
    return -1; 
}

Fid_t sys_Accept(Fid_t lsock)
{
  FCB* fcb = get_fcb(lsock);

  if(fcb!=NULL && lsock<MAX_FILEID-1 && lsock>=0)
  {  
     void *sobj1 = fcb->streamobj;
     SCB* slisten=(SCB*) sobj1;

     if(slisten!=NULL && slisten->type==LISTENER)
     { 
        while(is_rlist_empty(&PORT_MAP[slisten->port]->lcb->queue))
          kernel_wait(&(PORT_MAP[slisten->port]->lcb->req),SCHED_PIPE);
          

        rlnode* request=rlist_pop_front(&(PORT_MAP[slisten->port]->lcb->queue));
        
         //PEERS

        SCB* peer1 = (SCB*) request->scb;
        peer1->type=PEER; //From UNB to PEER

        Fid_t newf=Socket(slisten->port);
        
        if (newf>=MAX_FILEID)
          return -1;   
                
        FCB* fcb1 = get_fcb(newf);

        void *sobj2 = fcb1->streamobj;

        SCB* peer2=(SCB*) sobj2;
        peer2->type=PEER; //From UNB to PEER

        Fid_t fid_pipe[2];
        FCB * fcb_pipe[2];

        if(!FCB_reserve(2, fid_pipe, fcb_pipe))
        {
          fid_pipe[0] = NOFILE;
          fid_pipe[1] = NOFILE;
          return -1;
        }

        //Make pipes

        peer1->peercb = (PEER_CB*)xmalloc(sizeof(PEER_CB));
        peer2->peercb = (PEER_CB*)xmalloc(sizeof(PEER_CB));

        peer1->peercb->pipe = (PIPE_CB*)xmalloc(sizeof(PIPE_CB));
        peer2->peercb->pipe = (PIPE_CB*)xmalloc(sizeof(PIPE_CB));

        peer1->peercb->pipe->writer=fcb_pipe[0];
        peer1->peercb->pipe->reader=fcb_pipe[1];

        peer2->peercb->pipe->writer=fcb_pipe[1];
        peer2->peercb->pipe->reader=fcb_pipe[0];

        if(PORT_MAP[slisten->port]->lcb->flag==1)
        {
          Initialize_Pipe(fcb_pipe[0],fcb_pipe[1]);
          Initialize_Pipe(fcb_pipe[1],fcb_pipe[0]);
        }

        return newf;
   }
  } 
  return -1;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
  if(port<0 || port>MAX_PORT || sock<0 || sock>MAX_FILEID-1)
    return -1;

  FCB* fcb=get_fcb(sock);
  void* sobj=fcb->streamobj;
  SCB* usocket=(SCB*) sobj;

  if(usocket!=UNBOUND)
    return -1;

  if(PORT_MAP[port]!=NULL)
    if(PORT_MAP[port]->type==LISTENER){
      rlnode_init(&usocket->socket_node,usocket);
      PORT_MAP[port]->lcb->flag=1;
      rlist_push_back(&PORT_MAP[port]->lcb->queue,&usocket->socket_node);
      Cond_Signal(&PORT_MAP[port]->lcb->req);
      return 0;
    }
  return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	    FCB* fcb = get_fcb(sock);
      void *sobj = fcb->streamobj;
      SCB* new_sock=(SCB*) sobj;
      if(new_sock!=NULL && new_sock->peercb!=NULL){
        switch(how){
            case 1:
                return pipe_reader_close(new_sock->peercb->pipe->reader) == 0 ? 0 : -1;
                break;
            case 2:
                return pipe_writer_close(new_sock->peercb->pipe->writer) == 0 ? 0 : -1;
                break;
            case 3:
                return pipe_reader_close(new_sock->peercb->pipe->reader) == 0 && pipe_writer_close(new_sock->peercb->pipe->writer) == 0 ? 0 : -1;
                 break;
            default: 
                fprintf(stderr, "Wrong case of shutdown_mode\n");}
       }
      
      return -1;
}

