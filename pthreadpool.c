/*
author:shayne
date:2016-3-10
function:实现一个简单的线程池
*/
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#define  BLOCKSIZE  (1024*1024*2) //文件每块为2m
#define  TASKSIZE 20  //计划每批执行的任务数
#define  PTHREADCNT 5 //计划线程个数
//定义任务函数成员指针
typedef void (*worktask)(void* arg);
//定义任务节点
typedef struct tasknode
{
  worktask  fun;//指向线程执行的函数
  void * arg;//线程执行的函数参数
  struct tasknode  *next;
}tasknode;

//定义线程池结构
typedef struct pthreadpool
{
	pthread_mutex_t * mutex;//互斥锁
	pthread_cond_t  *cond;//条件变量
	pthread_t *pthreads;//指向线程id集合指针   
    tasknode * head;//任务起始位置
	int taskcnt;//任务数量
	int isshutdown;//线程池是否可以销毁 0:不能销毁 1:销毁
}pthreadpool;

//主线程唤醒标志位
int  g_ismainwake=0; //0:堵塞  1:唤醒

//记录已经处理完的任务数
int g_hasdotaskcnt=0;

//记录计划执行任务批次数
int  g_totaltaskNO=0;

//记录已经创建线程数量
int g_pthreadcnt=0;


//定义一个全局线程池指针
pthreadpool *g_pool=NULL;

//线程执行的任务
void run()
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
	   //获取任务链表中任务
	   if(g_pool->head!=NULL)
	   {
         tasknode *tmp=g_pool->head;
	     g_pool->head=g_pool->head->next;

	     (*(tmp->fun))(tmp->arg);//执行线程的函数
		 free(tmp);
		 tmp=NULL;
	     g_pool->taskcnt--;	 
	     g_hasdotaskcnt++;//记录已经完成一个任务   
		 if(g_pool->taskcnt==0&& g_hasdotaskcnt==TASKSIZE)
		 {
		 	g_ismainwake=1;
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

//初始化线程池
void initpthreadpool(int initpthreadcnt)
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
   //usleep(100);
   while(g_pthreadcnt!=PTHREADCNT)
   {
   	   ;
   }
}

//线程执行的任务
void runtask(void* arg)
{
	 int* argvalue=(int*)arg;
	 printf("%u\t running task %d\n",pthread_self(),*argvalue);
	 free(argvalue);
	 argvalue=NULL;
}

//模拟创建任务链表
void createtask(int taskcnt)
{
   tasknode *tmp=NULL;
   tasknode *curr=NULL;
   int i;
   int *argvalue=NULL;
   g_totaltaskNO=g_totaltaskNO+1;
   for(i=0;i<taskcnt;i++)
   {   	   
	   tmp=(tasknode*)malloc(1*sizeof(tasknode));
	   while(tmp==NULL)
	   {
          tmp=(tasknode*)malloc(1*sizeof(tasknode));
	   }
	   tmp->fun=runtask;
	   argvalue=(int*)malloc(1*sizeof(int));
	   *argvalue=i+1;
	   tmp->arg=argvalue;
	   argvalue=NULL;
	   tmp->next=NULL;

	   curr=g_pool->head;
	   if(curr==NULL)
	   {		 
		   g_pool->head=tmp;
	   }
	   else
	   {
           while(curr->next!=NULL)
		   {
			   curr=curr->next;
		   }
		   curr->next=tmp;
		   curr=tmp;
		   tmp=NULL;
	   }
	   g_pool->taskcnt++;
	   pthread_cond_signal(g_pool->cond);
   }
}

//所有任务已经执行完毕,清理释放操作
void cleanpthreadpool(int initpthreadcnt)
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
	char havetaskwork='y';
	int initpthreadcnt=PTHREADCNT;
	initpthreadpool(initpthreadcnt);
	while(havetaskwork=='y')
	{
		g_hasdotaskcnt=0;
		g_ismainwake=0;
		createtask(TASKSIZE);    
	    while(g_ismainwake==0)
		   ;
		printf("do you have other task works(y|n)?");
	    scanf("%c",&havetaskwork);
	    getchar();
	}	
	cleanpthreadpool(initpthreadcnt);	
	return 0;
}
