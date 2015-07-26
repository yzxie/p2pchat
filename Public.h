#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <netinet/in.h>
#include <arpa/inet.h>

enum Type { LOGIN = 0x01, LOGOUT = 0x02, LISTALLUSER = 0x03, USERLIST = 0x04, LOGIN_SUCCESS = 0x05, \
	LOGOUT_SUCCESS = 0x06, USERLOGIN = 0x07, USERLOGOUT = 0x08 };

typedef struct Message {
	int type;
	int len;
	char content[1024];
} Message;

void ERR_REPORT(const char *m) {
	do {
		perror(m);
		exit(EXIT_FAILURE);
	} while(0);
}

//readn, writen用于处理tcp的粘包问题
//方法1:读取和发送定长包
ssize_t readn(int fd, void *buf, size_t count) {
	size_t nleft = count;//还有多少没读
	char *bufp = (char*)buf;
	ssize_t nread;
	while (nleft > 0) {
		if ((nread = read(fd, bufp, nleft)) < 0) {
			if (errno == EINTR)//接收到信号
				continue;
			return -1;
		} else if (nread == 0)//读完
			return count - nleft;
		bufp += nread;
		nleft -= nread;
	}
	return count;//while循环成功退出,则读取了定长count个字节
}

//只有发送缓存区的大小大于要发送的数据,要发送的数据都会成功发送到
//发送缓存区,而不会阻塞
ssize_t writen(int fd, const void *buf, size_t count) {
	size_t nleft = count;//还有多少发送
	char *bufp = (char*)buf;
	ssize_t nwrite;
	while (nleft > 0) {
		if ((nwrite = write(fd, bufp, nleft)) < 0) {
			if (errno == EINTR)//接收到信号中断
				continue;
			return -1;
		} else if (nwrite == 0)//什么都没有发生过一样
			continue;
		bufp += nwrite;
		nleft -= nwrite;
	}
	return count;//while循环成功退出,则发送了定长count个字节
}

//线程池结构体
typedef struct idle_threads {
	pthread_mutex_t threads_lock; //互斥锁,用于指派用于监听的线程, 任何时候只有一个线程处于监听状态
	pthread_cond_t threads_accept; //用于等待成为监听线程
	int shutdown; //服务器是否关闭
	pthread_t *threads; //该线程池中的线程
	int max_pool_size; //该线程池的最大线程数目
	bool listen; //判断是否可以成为监听线程
	int listenfd; //tcp监听套接口, 被所有线程共享
	int udpfd; //udp套接口, 被所有线程共享

	struct sockaddr_in udpaddr; //服务器端udp地址
	socklen_t udpaddrlen;//服务器端udp地址长度

	struct sockaddr_in serveraddr;//服务器端tcp地址
	socklen_t serveraddr_len; //服务器端tcp地址长度

} idle_threads_pool;