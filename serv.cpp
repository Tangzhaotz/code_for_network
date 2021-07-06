#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <poll.h>

#define USER_LIMIT 5  //最大的用户数量限制
#define BUFFER_SIZE 64  //缓冲的大小
#define FD_LIMIT 65535  //文件描述符的限制

struct client_data  //客户数据的结构体
{
    sockaddr_in address;
    char* write_buf;
    char buf[ BUFFER_SIZE ];
};

int setnonblocking( int fd )  //设置非阻塞的函数
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    //设置ip地址和端口号
    const char* ip = argv[1];  
    int port = atoi( argv[2] );

    //初始化套接字的地址
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    //创建监听套接字
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    //绑定
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    //监听
    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    //创建客户数据的结构体存储客户的信息
    client_data* users = new client_data[FD_LIMIT];
    pollfd fds[USER_LIMIT+1];  //定义poll事件，每一个poll监听一个客户
    int user_counter = 0;  //开始的时候，客户的数量为0
    for( int i = 1; i <= USER_LIMIT; ++i )  //初始化poll事件的信息
    {
        fds[i].fd = -1;  //开始时，要检测的文件描述符全部设为-1
        fds[i].events = 0;  //委托内核检测的事件为0
    }
    fds[0].fd = listenfd;  //将第一个文件描述符设为监听描述符
    fds[0].events = POLLIN | POLLERR;  //委托检测的事件为读入和异常
    fds[0].revents = 0;  

    while( 1 )  //不断循环的监听是否有事件发生
    {
        ret = poll( fds, user_counter+1, -1 );  //监听
        if ( ret < 0 )
        {
            printf( "poll failure\n" );
            break;
        }
    
        for( int i = 0; i < user_counter+1; ++i )  //监听到事件的发生
        {
            //判断是否为监听描述符，是的话，说明监听到事件
            if( ( fds[i].fd == listenfd ) && ( fds[i].revents & POLLIN ) )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                //连接套接字
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                //请求的用户数量超出限制，给用户发送信息提示
                if( user_counter >= USER_LIMIT )  
                {
                    const char* info = "too many users\n";
                    printf( "%s", info );
                    send( connfd, info, strlen( info ), 0 );  //
                    close( connfd );
                    continue;
                }
                //没有超出限制的话，用户的数量加一个
                user_counter++;
                users[connfd].address = client_address;  //将用户数组与连接的客户信息绑定
                setnonblocking( connfd );
                //将相应的poll事件进行设置
                fds[user_counter].fd = connfd;
                fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_counter].revents = 0;
                printf( "comes a new user, now have %d users\n", user_counter );
            }
            else if( fds[i].revents & POLLERR )  //如果请求的是一个错误，打印出消息提示，并继续监听
            {
                printf( "get an error from %d\n", fds[i].fd );
                char errors[ 100 ];
                memset( errors, '\0', 100 );
                socklen_t length = sizeof( errors );
                if( getsockopt( fds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &length ) < 0 )
                {
                    printf( "get socket option failed\n" );
                }
                continue;
            }
            else if( fds[i].revents & POLLRDHUP )  //如果连接的客户端关闭，那么相应的服务端也关闭，
            //并将用户数减一
            {
                users[fds[i].fd] = users[fds[user_counter].fd];
                close( fds[i].fd );
                fds[i] = fds[user_counter];
                i--;
                user_counter--;
                printf( "a client left\n" );
            }
            else if( fds[i].revents & POLLIN )  //如果有新的客户端连接，则读取数据
            {
                int connfd = fds[i].fd;
                memset( users[connfd].buf, '\0', BUFFER_SIZE );
                ret = recv( connfd, users[connfd].buf, BUFFER_SIZE-1, 0 );
                printf( "get %d bytes of client data %s from %d\n", ret, users[connfd].buf, connfd );
                if( ret < 0 )
                {
                    if( errno != EAGAIN )
                    {
                        close( connfd );
                        users[fds[i].fd] = users[fds[user_counter].fd];
                        fds[i] = fds[user_counter];
                        i--;
                        user_counter--;
                    }
                }
                else if( ret == 0 )
                {
                    printf( "code should not come to here\n" );
                }
                else
                {
                    for( int j = 1; j <= user_counter; ++j )
                    {
                        if( fds[j].fd == connfd )
                        {
                            continue;
                        }
                        
                        fds[j].events |= ~POLLIN;
                        fds[j].events |= POLLOUT;
                        users[fds[j].fd].write_buf = users[connfd].buf;
                    }
                }
            }
            else if( fds[i].revents & POLLOUT )  //读出数据
            {
                int connfd = fds[i].fd;
                if( ! users[connfd].write_buf )
                {
                    continue;
                }
                ret = send( connfd, users[connfd].write_buf, strlen( users[connfd].write_buf ), 0 );
                users[connfd].write_buf = NULL;
                fds[i].events |= ~POLLOUT;
                fds[i].events |= POLLIN;
            }
        }
    }

    delete [] users;
    close( listenfd );
    return 0;
}
