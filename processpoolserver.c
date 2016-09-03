/*
File name:processpool
Author:shayne
Date:2016-3-2
Description:服务端的进程池实现服务于多个客户
*/

#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#define  MAX_OPEN_FILENO 1024    //打开文件最大数
#define  PROCESS_NAME   "tcpserverpool" //进程的名字,供控制只运行一个实例
#define  LISTENSOCKETNO  10 //监听队列最大套接字数
#define  LISTENPORT   6789  //监听端口值
#define  MAXCHILDNO 50  //子进程的最大数
#define  PARTCHILDNO 5  //每次创建的子进程数
#define  BUFSIZE 4096


//定义进程信息块(进程pid ,进程状态 0:停止 1:运行)
typedef struct problock
{
	pid_t myselfpid;
	int   myselfstatus;
}problock;

//定义指向匿名管道实现子进程与主进程通信
int*  g_childtomainfds;
//定义指向匿名管道实现主进程与子进程通信
int * g_maintochildfds;
//服务端监听套接字
int g_tcplistensock;

/*
设置成后台守护进程运行
*/
void  set_daemon()
{
	pid_t childpid;
	childpid=fork();
	if(childpid>0)
		exit(-1);
	setsid();
	childpid=fork();
	if(childpid>0)
		exit(-1);
	umask(0);
	int i;
	for(i=0;i<MAX_OPEN_FILENO;i++)
	{
        close(i);
	}
	chdir("/tmp");
}

/*
输出系统错误信息
*/
void  output_sys_error(char*  errortitle)
{
    perror(errortitle);
    //exit(-1);
}

/*
判断是否只存在一个进程实例运行
返回值: 0:文件打开错误
        -1:文件加锁失败
        1:只有一个进程实例运行
*/
int   only_one_process()
{
	int fd;
	fd=open(PROCESS_NAME,O_RDONLY);
	if(fd<0)
	{
		output_sys_error("isonlyoneprocess-open:");
		return 0;
	}
	if(flock(fd,LOCK_EX|LOCK_NB)==-1)
	{
		output_sys_error("isonlyoneprocess-flock:");
		return -1;
	}
	return 1;
}

/*
创建一个TCP套接字,初始化地址结构,绑定 监听
返回值:成功 返回一个监听套接字
       -1:发生错误
*/
int  init_tcp_socket()
{
	int listensock=-1;
	listensock=socket(AF_INET,SOCK_STREAM,0);
	if(listensock==-1)
	{
		output_sys_error("init_tcp_socket-socket:");
		return -1;
	}
	struct sockaddr_in seraddress;
	bzero(&seraddress,sizeof(seraddress));
	seraddress.sin_family=PF_INET;
	seraddress.sin_port=htons(LISTENPORT);
	//seraddress.sin_addr.s_addr=inet_ntoa("192.168.252.3")
	seraddress.sin_addr.s_addr=htonl(INADDR_ANY);
	if(bind(listensock,(struct sockaddr*)&seraddress,sizeof(seraddress))==-1)
	{
		output_sys_error("init_tcp_socket-bind:");
		return -1;
	}
	if(listen(listensock,LISTENSOCKETNO)==-1)
	{
		output_sys_error("init_tcp_socket-listen:");
		return -1;
	}
	return listensock;
}

/*
创建进程通信的匿名管道
*/
int*  create_pipe()
{
	int * fds=(int*)malloc(2*sizeof(int));
	while(fds==NULL)
	{
		 fds=(int*)malloc(2*sizeof(int));
	}
	int ret=pipe(fds);
	if(ret==-1)
	{
		output_sys_error("create_pipe-pipe:");
		return NULL;
	}
	return fds;
}

/*

*/
void childdowork(int listensock)
{
	int mylistensock=listensock;
	int clientsock;
	struct sockaddr_in clientaddr;
	int clientaddrlen=sizeof(clientaddr);
	char buf[BUFSIZE];
	int currentpid=getpid();
	int ret;
	char hashnewclient;
	problock  currentpro;
	currentpro.myselfpid=currentpid;
	currentpro.myselfstatus=1;
	//每个子进程不能结束
	while(1)
	{
       flock(mylistensock,LOCK_EX);
       clientsock=accept(mylistensock,(struct sockaddr*)&clientaddr,&clientaddrlen);
       while(clientsock==-1)
       {
       	  clientsock=accept(mylistensock,(struct sockaddr*)&clientaddr,&clientaddrlen);
       }
       flock(mylistensock,LOCK_UN);
       bzero(buf,sizeof(buf));
       //告诉请求客户已经请求成功
       strcpy(buf,"connect ok!");
       ret=send(clientsock,buf,strlen(buf),0);
       while(ret==-1)
       {
       	  ret=send(clientsock,buf,strlen(buf),0);
       }
       printf("I (%d) am working\n",currentpid);

       //子进程发送消息给主进程:我已经接了个客户正在服务       
       ret=write(g_childtomainfds[1],(void*)&currentpro,sizeof(currentpro));
       while(ret==-1)
       {
       	  ret=write(g_childtomainfds[1],(void*)&currentpro,sizeof(currentpro));
       }
       //别吵我,我开始服务我客户了
       while(1)
       {
          bzero(buf,sizeof(buf));
          recv(clientsock,buf,sizeof(buf)-1,0);
          if(strcmp(buf,"quit")==0)
          {
          	//客户主动提出结束服务
          	 close(clientsock);
          	 currentpro.myselfstatus=0;
          	 break;
          }
          else
          {
          	 printf("%d recv msg: %s\n",currentpid,buf);
          }           
       }
       //子进程发送消息告诉主进程,客户已经被我气跑了
       if(currentpro.myselfstatus==0)
       {
       	  ret=write(g_childtomainfds[1],(void*)&currentpro,sizeof(currentpro));
       	  while(1)
       	  {
       	  	 ret=write(g_childtomainfds[1],(void*)&currentpro,sizeof(currentpro));
       	  }

       	  //等待主进程新的召唤
       	  ret=read(g_maintochildfds[0],(void*)&hashnewclient,sizeof(hashnewclient));
       	  while(ret==-1)
       	  {
       	  	 ret=read(g_maintochildfds[0],(void*)&hashnewclient,sizeof(hashnewclient));
       	  } 	
       	  
       	  if(hashnewclient=='n')
       	  {
       	  	printf("game over\n");
       	  	exit(-1);
       	  }  
       	  if(hashnewclient=='y')
       	  {
       	  	continue;
       	  }
       }
	}
}

/*
一次性创建PARTCHILDNO进程
*/
void  create_childs(int listensock)
{
	int i;
    for(i=0;i<PARTCHILDNO;i++)
    {
    	pid_t childpid=fork();
    	while(childpid<0)
    	{
    		childpid=fork();
    	}
    	if(childpid==0)
    	{
        childdowork(listensock);
    	}
    	if(childpid>0)
    	{
    		printf("create %d child:%d\n",i+1,childpid);
    	}
    }
}
/*
主进程继续执行任务的安排与调度任务
*/
void  mainprocwork()
{
 int hasworkchildnum=0;
 int createchildok=PARTCHILDNO;
 problock saveproblock;
 int ret;
 char isexit='n';
 char iscontinue='y';
 while(1)
 {
     ret=read(g_childtomainfds[0],(void*)&saveproblock,sizeof(saveproblock));
     while(ret==-1)
     {
     	ret=read(g_childtomainfds[0],(void*)&saveproblock,sizeof(saveproblock));
     }
     if(saveproblock.myselfstatus==1)
     {
     	hasworkchildnum++;
     	if(hasworkchildnum==createchildok && createchildok<=MAXCHILDNO)
     	{
     		create_childs(g_tcplistensock);
     		createchildok=createchildok+PARTCHILDNO;
     	}
     }
     else if(saveproblock.myselfstatus==0)
     { 
        hasworkchildnum--; 
        createchildok--;    	
        if(createchildok>(hasworkchildnum+1) && hasworkchildnum==0)
          //如果已经超出,并通过应急增加后,当客户已经回到正常情况,删除多余进程
        {
           while(write(g_maintochildfds[1],(void*)&isexit,sizeof(isexit))<0)
            {
              write(g_maintochildfds[1],(void*)&isexit,sizeof(isexit));
            }
        }
        else
        {
            while(write(g_maintochildfds[1],&iscontinue,sizeof(iscontinue))<0)
            {
              write(g_maintochildfds[1],&iscontinue,sizeof(iscontinue));
            }
        }
      }
     
   }
}

int main(int argc, char const *argv[])
{
	g_tcplistensock=init_tcp_socket();
	g_childtomainfds=create_pipe();
	g_maintochildfds=create_pipe();
	create_childs(g_tcplistensock);
	mainprocwork();
	return 0;
}
