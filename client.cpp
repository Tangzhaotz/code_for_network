#define _GNU_SOURCE 1
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<stdlib.h>
#include<poll.h>
#include<fcntl.h>


#define BUF_SIZE 128

int main(int argc,char *argv[])
{
    if(argc<=2)
    {
        printf("usage: %s ip_address port_number\n",basename(argv[0]));
        return 1;
    }

    const char *ip =argv[1];  //ip地址
    int port = atoi(argv[2]);  //端口号

    struct sockaddr_in server_address;  //定义服务器的地址信息
    bzero(&server_address,sizeof(server_address));  //初始化
    server_address.sin_family=AF_INET;  //定义地址簇
    inet_pton(AF_INET,ip,&server_address.sin_addr);  //ip地址转换
    server_address.sin_port=htons(port);  //端口设置

    //创建套接字
    int sockfd =socket(PF_INET,SOCK_STREAM,0);
    assert(sockfd>=0);

   //因为是客户端套接字，所以直接连接
   if(connect(sockfd,(struct sockaddr*)&server_address,sizeof(server_address))<0)
   {
       printf("connect failed\n");  //连接失败
       close(sockfd);
       return 1;
   }

   struct pollfd fds[2];  //定义io复用的poll
   /*
   struct pollfd {
	int fd;
	short events;
	short revents;
};
   */

   //注册文件描述符0（标准输入）和文件描述符sockfd上的可读事件
   fds[0].fd=0;  //委托内核需要操作的文件描述符
   fds[0].events=POLLIN;  //委托内核检测的什么事件
   fds[0].revents=0;  //内核实际检测到的事件

   fds[1].fd=sockfd;
   fds[1].events=POLLIN | POLLRDHUP;
   fds[1].revents=0;

   char read_buf[BUF_SIZE];
   int pipefd[2];
   int ret = pipe(pipefd);  //创建管道
   assert(ret!=-1);

   while(1)
   {
       ret =poll(fds,2,-1);  //第三个参数为-1表示永远被阻塞
       if(ret <0)
       {
           printf("poll failure\n");
           break;
       }

       if(fds[1].revents&POLLRDHUP)
       {
           printf("server close the connection\n");
           break;
       }
       else if(fds[1].revents & POLLIN)
       {
           memset(read_buf,'\0',BUF_SIZE);
           recv(fds[1].fd,read_buf,BUF_SIZE-1,0);  //接收数据
           printf("%s\n",read_buf);  //输出读取到的数据
       }

       if(fds[0].revents & POLLIN)
       {
           //使用splice将用户输入的数据直接写到sockfd上（零拷贝）
           ret = splice(0,NULL,pipefd[1],NULL,32768,SPLICE_F_MORE | SPLICE_F_MOVE);
           ret = splice(pipefd[0],NULL,sockfd,NULL,32768,SPLICE_F_MORE | SPLICE_F_MOVE);
       }
   }

    close(sockfd);

    return 0;

}
