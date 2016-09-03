/*
author:shayne
date:2016-3-10
function:实现线程池分块复制大文件
*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#define  BLOCKSIZE  (1024*1024*2) //文件每块为2m
#define  TASKSIZE 20  //计划每批执行的任务数
#define  PTHREADCNT 5 //计划线程个数
#define  BLOCKSIZE  (1024*1024*2)//每个文件块大小2M
//定义文件块结构信息,发送给每个线程使用
typedef struct fileblock
{
	unsigned char* srcfileaddres;
	unsigned char* destfileaddres;
	unsigned int startfilepos;
	unsigned int blocksize;
}fileblock;

//定义线程池任务队列节点
typedef struct tasknode
{
  fileblock *fpblock;
  struct tasknode  *next;
}tasknode;

//定义任务队列
typedef struct  taskqueue
{
    tasknode* head;
    tasknode* tail;
}taskqueue;

//定义线程池结构
typedef struct pthreadpool
{
	pthread_mutex_t * mutex;//互斥锁
	pthread_cond_t  *cond;//条件变量
	pthread_t *pthreads;//指向线程id集合指针   
    tasknode * head;//任务起始位置
	unsigned int taskcnt;//已经接收任务数量
	unsigned int isshutdown;//线程池是否可以销毁 0:不能销毁 1:销毁
	unsigned int tasktotalcnt;//任务总数量
}pthreadpool;


//主线程唤醒标志位
unsigned int  g_ismainwake=0; //0:堵塞  1:唤醒
//记录已经处理完的任务数
unsigned int g_hasdotaskcnt=0;
//记录计划执行任务批次数
unsigned int  g_totaltaskNO=0;
//记录已经创建线程数
unsigned int g_pthreadcnt=0;
//记录每批次分配的任务数
unsigned int g_taskcntper=0;
//定义一个全局线程池指针
pthreadpool *g_pool=NULL;
//定义指向任务队列的全局指针
taskqueue * g_taskqueuep=NULL;
//指向文件结构全局指针
fileblock * g_fileblockfp=NULL;

//源文件内存映射的全局指针
unsigned char * g_srcfilestartp=NULL;
//目标文件内存映射的全局指针
unsigned char * g_destfilestartp=NULL;


/*线程池任务队列代码*/
/*
function:初始化任务链表队列
parameter:
          taskqueue qtask  队列
*/
void init_queue(taskqueue* qtask)
{
	qtask->head=NULL;
	qtask->tail=NULL;
}

/*
function:进队操作
parameter:
          taskqueue qtask  队列
          fileblock *fblock  指向文件分块的指针
*/
void push_queue(taskqueue *qtask,fileblock *fblock)
{
	tasknode *newtasknode=(tasknode *)malloc(1*sizeof(tasknode));
	newtasknode->fpblock=fblock;
	newtasknode->next=NULL;
	if(qtask->head==NULL && qtask->tail==NULL)
	{
       qtask->head=newtasknode;
       qtask->tail=newtasknode;
	}
	else
	{
		qtask->tail->next=newtasknode;
		qtask->tail=newtasknode;
	}
}
/*
function:判断队列是否为空
parameter:
          taskqueue qtask  队列
return value:
             1:队列为空
             0:队列非空
*/
int  is_empty_queue(taskqueue* qtask)
{
	int isempty=0;
	if(qtask->head==NULL && qtask->tail==NULL)
		isempty=1;
	return isempty;
}
/*
function:出队列操作
parameter:
          taskqueue qtask  队列
return value:
            NOT NULL 返回从队列中提取一个任务节点
            NULL  从队列中提取任务节点失败
*/
tasknode*  pop_queue(taskqueue* qtask)
{
     tasknode  *returnnode=NULL;
     if(!is_empty_queue(qtask))
     {
     	returnnode=qtask->head;     	
     	qtask->head=qtask->head->next;
     	if(qtask->head==NULL)
     	{
     		qtask->tail=NULL;
     	}
     	returnnode->next=NULL;
     }
     return returnnode;
}

void free_queue_point(taskqueue* qtask)
{
	if(qtask!=NULL && qtask->head==NULL && qtask->tail==NULL)
	{
		free(qtask);
		qtask=NULL;
	}
}


/*文件处理代码*/
/*
function:输出系统返回错误信息
parameter:
          const char* errmsg  错误信息
*/
void  output_sys_errmsg(const char* errmsg)
{
	perror(errmsg);
}

/*
function:计算文件大小
parameter:
          unsigned int fd  源文件描述符
return value: 文件大小
*/
unsigned int  get_file_size(unsigned int fd)
{
	unsigned int filesize=lseek(fd,0,SEEK_END);
	lseek(fd,0,SEEK_SET);
	return filesize;
}
/*
function:依据文件大小计算文件分块数
parameter:
          unsigned int fd  源文件描述符
return value: 文件块数
*/
unsigned int  get_file_block_cnt(unsigned int fd)
{
	unsigned int fileblockcnt=0;
	unsigned int  filesize=get_file_size(fd);
	unsigned int fileremaindsize=filesize%BLOCKSIZE;
	fileblockcnt=filesize/BLOCKSIZE;
	if(fileremaindsize>0)
	{
       fileblockcnt=fileblockcnt+1;
	}
	return fileblockcnt;
}

/*
function:如果文件分块不是整数块,计算出剩余的字节数
parameter:
          unsigned int fd  源文件描述符
return value: 最后一块大小,文件最后一块剩余大小
*/
unsigned int   get_remainsize(unsigned int fd)
{
	unsigned int remainsize=0;
	unsigned int filesize=get_file_size(fd);
	if(filesize%BLOCKSIZE>0)
	{
		remainsize=filesize%BLOCKSIZE;
	}
	return remainsize;
}

/*
function:建立文件内存映射
parameter:
          unsigned int fd  源文件描述符
return value:返回内存映射地址值
*/
unsigned char *  get_srcfile_map_addres(unsigned int fd)
{
	unsigned int filesize=get_file_size(fd);
	//unsigned char * filestartp=NULL;
	g_srcfilestartp=(unsigned char*)mmap(NULL,filesize,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	if(g_srcfilestartp==NULL)
	{
		output_sys_errmsg("get_srcfile_map_addres mmap:");
		exit(-1);
	}
	return g_srcfilestartp;
} 

/*
function:建立目标文件内存映射
parameter:
          unsigned int srcfd  源文件描述符
          unsigned int destfd 目标文件描述符
return value:返回内存映射地址值
*/
unsigned char *  get_destfile_map_addres(unsigned int srcfd,unsigned int destfd)
{
	unsigned int filesize=get_file_size(srcfd);
	lseek(destfd,filesize,SEEK_SET);
	write(destfd," ",1);
	g_destfilestartp=(unsigned char*)mmap(NULL,filesize,PROT_READ|PROT_WRITE,MAP_SHARED,destfd,0);
	if(g_destfilestartp==NULL)
	{
		output_sys_errmsg("get_destfile_map_addres mmap:");
		exit(-1);
	}
	return g_destfilestartp;
}

/*
function:释放源文件内存映射区
parameter:
         int fd 映射对应的文件描述符
*/
void  set_srcfile_munmap(int fd)
{
	unsigned int filesize=get_file_size(fd);
    if(g_srcfilestartp!=NULL)
    {
        munmap(g_srcfilestartp,filesize);
    }
}

/*
function:释放源文件内存映射区
parameter:
         int fd 映射对应的文件描述符         
*/

void  set_destfile_munmap(int fd)
{
	unsigned int filesize=get_file_size(fd);
    if(g_destfilestartp!=NULL)
    {
        munmap(g_destfilestartp,filesize);
    }
}

/*
function:计算文件分块,并记录每块文件的信息
parameter:
          unsigned int fd  源文件描述符
return values:指向记录文件块信息结构数组指针
*/
fileblock*  get_file_block(unsigned int fd)
{
   unsigned int fileblockcnt=get_file_block_cnt(fd);
   unsigned int fileremaindsize=get_remainsize(fd);
   fileblock* fileblockarray=(fileblock*)malloc(fileblockcnt*sizeof(fileblock));
   if(fileblockarray==NULL)
   {
   	   output_sys_errmsg("get_file_block malloc:");
   	   exit(-1);
   }
   int i;
   for(i=0;i<fileblockcnt-1;i++)
   {
      fileblockarray[i].startfilepos=i*BLOCKSIZE;
      fileblockarray[i].blocksize=BLOCKSIZE;
   }

   fileblockarray[i].startfilepos=i*BLOCKSIZE;
   fileblockarray[i].blocksize=fileremaindsize>0?fileremaindsize:BLOCKSIZE;
   return fileblockarray;
}

/*
function:线程执行函数(实现分块复制)
parameter:
          void* arg 主线程传递块结构
*/
void*  pthread_copy_work(void* arg)
{
   fileblock * blockstruct=(fileblock*)arg; 
   memcpy((void*)&g_destfilestartp[blockstruct->startfilepos],(void*)&g_srcfilestartp[blockstruct->startfilepos],blockstruct->blocksize);
   return NULL;
}  

/*线程池的代码*/
/*
function:线程执行的任务
parameter:
*/
void* run(void * arg)
{
   pthread_mutex_lock(g_pool->mutex);
   g_pthreadcnt++;//记录成功创建的线程数
   pthread_mutex_unlock(g_pool->mutex);
	//不能让每个线程结束
   while(1)
   {
	  pthread_mutex_lock(g_pool->mutex);
	  //刚开无任务执行,堵塞线程
      if(g_pool->taskcnt==0 && g_pool->isshutdown==0)
	   {
		   pthread_cond_wait(g_pool->cond,g_pool->mutex);
	   }
	   //获取任务队列中任务节点
	   g_pool->head=pop_queue(g_taskqueuep);
	   if(g_pool->head!=NULL)
	   { 
	     pthread_copy_work((void*)g_pool->head->fpblock);
	     // if(g_pool->head->fpblock!=NULL)
	     // {
      //       free(g_pool->head->fpblock);
      //       g_pool->head->fpblock=NULL;
	     // }
		 free(g_pool->head);
		 g_pool->head=NULL;
	   g_pool->taskcnt--;	 
	   g_hasdotaskcnt++;//记录已经完成一个任务   
		 if(g_pool->taskcnt==0 && g_hasdotaskcnt==g_pool->tasktotalcnt)
		 {
		 	 g_ismainwake=1;
       if(g_fileblockfp!=NULL)
       {
          free(g_fileblockfp);
          g_fileblockfp=NULL;
       }
		 }            
     pthread_mutex_unlock(g_pool->mutex);
		 usleep(100);//防止总是让一个线程做
	   }

	   if(g_pool->isshutdown)
	   {
		   pthread_mutex_unlock(g_pool->mutex);
		   //printf("pthread id=%u\n",pthread_self());
		   pthread_exit(NULL);
	   }
   }
}

/*
function:初始化线程池
parameter:
          int initpthreadcnt  线程个数
*/
void init_pthread_pool(unsigned int initpthreadcnt,unsigned int tasktotalcnt)
{
   g_pool=(pthreadpool*)malloc(1*sizeof(pthreadpool));
   while(g_pool==NULL)
   {
     g_pool=(pthreadpool *)malloc(1*sizeof(pthreadpool));
   }
   g_pool->mutex=NULL;
   g_pool->mutex=(pthread_mutex_t*)malloc(1*sizeof(pthread_mutex_t));
   while(g_pool->mutex==NULL)
   {
      g_pool->mutex=(pthread_mutex_t*)malloc(1*sizeof(pthread_mutex_t));
   }
   g_pool->cond=NULL;
   g_pool->cond=(pthread_cond_t*)malloc(1*sizeof(pthread_cond_t));
   while(g_pool->cond==NULL)
   {
      g_pool->cond=(pthread_cond_t*)malloc(1*sizeof(pthread_cond_t));
   }
   pthread_mutex_init(g_pool->mutex,NULL);
   pthread_cond_init(g_pool->cond,NULL);

   g_pool->pthreads=(pthread_t *)malloc(sizeof(pthread_t)*initpthreadcnt);
   while(g_pool->pthreads==NULL)
   {
      g_pool->pthreads=(pthread_t *)malloc(sizeof(pthread_t)*initpthreadcnt);
   }
   //创建线程
   int i;
   int ret;
   for(i=0;i<initpthreadcnt;i++)
   {
     ret=pthread_create(&(g_pool->pthreads[i]),NULL,(void*)run,NULL);
	   while(ret==-1)
	   {
        ret=pthread_create(&(g_pool->pthreads[i]),NULL,(void*)run,NULL);
	   }
   }   
   g_pool->head=NULL;
   g_pool->taskcnt=0;
   g_pool->isshutdown=0;
   g_pool->tasktotalcnt=tasktotalcnt;
   while(g_pthreadcnt!=initpthreadcnt)
   {
   	   ;
   }
}
/*
function:创建任务队列
parameter:
          unsigned int fd 操作的源文件描述符值
*/
void init_task_list(unsigned int fd)
{
   g_taskqueuep=(taskqueue*)malloc(1*sizeof(taskqueue));
   if(g_taskqueuep==NULL)
   {
    	output_sys_errmsg("init_task_list:malloc:");
   	  exit(-1);
   }
   init_queue(g_taskqueuep);
   g_fileblockfp=NULL;
   g_fileblockfp=get_file_block(fd);  
   int i=0;
   for(i=0;i<g_pool->tasktotalcnt;i++)
   {
   	  push_queue(g_taskqueuep,&g_fileblockfp[i]);   	
   	  g_pool->taskcnt++;
	    pthread_cond_signal(g_pool->cond);
   } 	   
}

/*
function:所有任务已经执行完毕,清理释放操作
parameter:
          int initpthreadcnt  线程个数
*/
void clean_pthread_pool(int initpthreadcnt)
{
   if(g_pool->isshutdown)
	   return;
   g_pool->isshutdown=1;

   pthread_cond_broadcast(g_pool->cond);

   int i=0;
   for(i=0;i<initpthreadcnt;i++)
   {
	   pthread_join(g_pool->pthreads[i],NULL);
   }

   free(g_pool->pthreads);
   g_pool->pthreads=NULL;
   free(g_pool->mutex);
   g_pool->mutex=NULL;
   free(g_pool->cond);
   g_pool->cond=NULL;
   free(g_pool);
   g_pool=NULL;
}


int main(int argc, char *argv[])
{
	unsigned int initpthreadcnt=PTHREADCNT;
	int srcfd=open("wenchuandiz.flv",O_RDWR);
	if(srcfd==-1)
	{
		output_sys_errmsg("main open srcfd:");
		exit(-1);
	}
	int destfd=open("wenchuandiz1.flv",O_CREAT|O_EXCL|O_RDWR,0777);
  if(destfd==-1)
  {
    output_sys_errmsg("main open destfd:");
    exit(-1);
  }
  unsigned int fblockcnt=get_file_block_cnt(srcfd);
  get_srcfile_map_addres(srcfd);
  get_destfile_map_addres(srcfd,destfd);
	init_pthread_pool(initpthreadcnt,fblockcnt);
	init_task_list(srcfd);
	while(g_ismainwake==0)
		   ;	
	set_srcfile_munmap(srcfd);
  set_destfile_munmap(srcfd);  
	clean_pthread_pool(initpthreadcnt);	
  free_queue_point(g_taskqueuep);
	return 0;
}
