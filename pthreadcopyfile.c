/*
author:shayne
date:2016-3-11
function:实现内存映射多线程大文件复制
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

#define  BLOCKSIZE  (1024*1024*2)//每个文件块大小2M
//定义文件块结构信息,发送给每个线程使用
typedef struct fileblock
{
	unsigned char* srcfileaddres;
	unsigned char* destfileaddres;
	unsigned int startfilepos;
	unsigned int blocksize;
}fileblock;

//源文件内存映射的全局指针
unsigned char * g_srcfilestartp=NULL;
//目标文件内存映射的全局指针
unsigned char * g_destfilestartp=NULL;

//线程互斥锁
pthread_mutex_t g_mutex=PTHREAD_MUTEX_INITIALIZER;

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
function:线程执行函数(实现分块复制)
parameter:
          void* arg 主线程传递块结构
*/
void*  pthread_copy_work(void* arg)
{
   fileblock * blockstruct=(fileblock*)arg;
   pthread_mutex_lock(&g_mutex);
   memcpy((void*)&g_destfilestartp[blockstruct->startfilepos],(void*)&g_srcfilestartp[blockstruct->startfilepos],blockstruct->blocksize);
   pthread_mutex_unlock(&g_mutex);
   return NULL;
}  

/*
function:根据文件分块数,创建线程
parameter:
          unsigned int fd  源文件描述符
*/
void  init_copy_pthread(unsigned int fd)
{
   unsigned int blockcnt=get_file_block_cnt(fd);
   fileblock *fileblockarray=get_file_block(fd);
   pthread_t *pthreads=NULL;
   pthreads=(pthread_t*)malloc(blockcnt*sizeof(pthread_t));
   if(pthreads==NULL)
   {
   	  output_sys_errmsg("init_copy_pthread malloc:");
   	  exit(-1);
   }
   int i;
   int ret;
   for(i=0;i<blockcnt;i++)
   {
      ret=pthread_create(&pthreads[i],NULL,pthread_copy_work,(void*)&fileblockarray[i]);
      while(ret==-1)
      {
      	ret=pthread_create(&pthreads[i],NULL,pthread_copy_work,(void*)&fileblockarray[i]);
      }
   }   

   for(i=0;i<blockcnt;i++)
   {
   	  pthread_join(pthreads[i],NULL);
   }
}

int main(int argc, char const *argv[])
{
	int srcfd=open("maishu.3gp",O_RDWR);
	if(srcfd==-1)
	{
		output_sys_errmsg("main open srcfd:");
		exit(-1);
	}

	int destfd=open("maishu1.3gp",O_CREAT|O_EXCL|O_RDWR,0777);
    if(destfd==-1)
    {
    	output_sys_errmsg("main open destfd:");
    	exit(-1);
    }
    get_srcfile_map_addres(srcfd);
    get_destfile_map_addres(srcfd,destfd);
    init_copy_pthread(srcfd);
    set_srcfile_munmap(srcfd);
    set_destfile_munmap(srcfd);
	return 0;
}
