#include "Server2.h"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>
#include <sstream>
#include <assert.h>

idle_threads_pool* Server2::threads_pool = NULL;

std::list<NetUser> Server2::loginUsers2;//用于某用户上线与下线时,通知其他用户
std::vector<int> Server2::conns; //保存已连接套接口
std::map<std::string, NetUser> Server2::loginUsers1; //用户上线时用于收集并保存用户信息

//初始化线程池,每个线程均在绑定的函数中监听客户端的请求,同时建立请求后服务客户端
void Server2::initServer2(int max_pool_size) {
	threads_pool = (idle_threads_pool*)malloc(sizeof(idle_threads_pool));//初始化线程池
	pthread_mutex_init(&threads_pool->threads_lock, NULL);//初始化用于指派监听线程的互斥锁
	pthread_cond_init(&threads_pool->threads_accept, NULL);//初始化用于等待成为监听线程的条件变量

	threads_pool->max_pool_size = max_pool_size;
	threads_pool->shutdown = 0;
	threads_pool->listenfd = -1;
	threads_pool->udpfd = -1;
	threads_pool->listen = true; //第一个线程为默认监听线程
	//tcp通信, 所有线程共享tcp监听套接口, 轮流监听
	if ((threads_pool->listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		ERR_REPORT("socket");
	//设定服务器地址
	threads_pool->serveraddr_len = sizeof(threads_pool->serveraddr);//为初始化会出现bind: invalid argument
	threads_pool->serveraddr.sin_family = AF_INET;
	threads_pool->serveraddr.sin_addr.s_addr = inet_addr("192.168.42.34");
	threads_pool->serveraddr.sin_port = htons(6666);
	//设置地址可重复利用
	int on = 1;
	if (setsockopt(threads_pool->listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		ERR_REPORT("setsockopt");
	//绑定套接口到该地址
	if (bind(threads_pool->listenfd, (struct sockaddr*)&threads_pool->serveraddr, threads_pool->serveraddr_len) < 0)
		ERR_REPORT("bind");
	//监听客户端的连接
	if (listen(threads_pool->listenfd, SOMAXCONN) < 0)
		ERR_REPORT("listen");

	//udp通信, 用于触发客户端的udp套接口生成本地地址
	if (((threads_pool->udpfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0))
		ERR_REPORT("udp socket");

	//设定服务器端UDP套接口地址
	threads_pool->udpaddrlen = sizeof(threads_pool->udpaddr);
	threads_pool->udpaddr.sin_family = AF_INET;
	threads_pool->udpaddr.sin_addr.s_addr = inet_addr("192.168.42.34");
	threads_pool->udpaddr.sin_port = htons(5555);

	if (bind(threads_pool->udpfd, (struct sockaddr*)&threads_pool->udpaddr, threads_pool->udpaddrlen) < 0) 
		ERR_REPORT("udp bind");
	//为线程指针进行内存分配
	threads_pool->threads = (pthread_t*)malloc(threads_pool->max_pool_size * sizeof(pthread_t));
	assert(threads_pool->threads);//检测是否分配内存成功
	//初始化线程池中的线程
	for(int i = 0; i < max_pool_size; i++) {
		pthread_create(&(threads_pool->threads[i]), NULL, &serveThreadRun, NULL);
	}
}
//线程所绑定的函数
void* Server2::serveThreadRun(void *arg) {
	int connectedfd = -1;//已连接套接字, 每个线程对应一个
	struct sockaddr_in clientaddr; //客户端tcp地址
	socklen_t clientaddr_len; //客户端tcp地址长度

	fd_set rset;//一个集合用于检测里面IO的可读事件, 此处为检测客户端发送过来的udp包
	FD_ZERO(&rset);

	while (1) {
		printf("thread %ld is waiting...\n", pthread_self());
		//以下为临界区, 实现线程的互斥访问, 线程轮流进入, 监听客户端的连接请求
		pthread_mutex_lock(&threads_pool->threads_lock);//加锁
		while (!threads_pool->listen) {//listen为条件变量, 由所有线程共享, 为true则该线程成为监听线程, 否则等待
				printf("thread %ld is waiting to be the listening thread...\n", pthread_self());
				//阻塞: 解锁->等待->获得信号->返回->重新锁住互斥锁thread_lock, 完成一个pv操作.调用之后, 该线程退出cpu进入等待队列
				//所以不会不停地输出
				pthread_cond_wait(&threads_pool->threads_accept, &threads_pool->threads_lock);
		}
		threads_pool->listen = false; //修改条件变量, 当前线程为监听线程, 使其他线程成为空闲线程
		printf("thread %ld is listening...\n", pthread_self());

		connectedfd = accept(threads_pool->listenfd, (struct sockaddr *)&clientaddr, (socklen_t *)&clientaddr_len);//
		if (connectedfd < 0) {
			threads_pool->listen = true; //设置条件变量, 通知其他线程可以去成为监听线程
			pthread_cond_signal(&threads_pool->threads_accept); //当前线程三次握手失败, 通知其他线程可以去监听客户端连接请求
			pthread_mutex_unlock(&threads_pool->threads_lock);	//当前线程失败退出, 释放用于监听的互斥锁
			continue;//将线程放回线程池, 重新成为空闲线程
		}
		else {//tcp三次握手建立完成	
			FD_SET(threads_pool->udpfd, &rset);//将udpfd交给rset集合检测, 当udp套接口有消息可读时(即客户端发送了udp到服务器时), select 返回.
			//监听线程在与客户端建立tcp连接成功后, 不马上成为工作线程, 而是要先收到客户端发送过来的udp包再释放锁和发送信号, 成为工作线程, 由其他线程继续监听tcp		
			int nselect = select(threads_pool->udpfd+1, &rset, NULL, NULL, NULL);//接收到客户端发送过来的udp包时返回
			if (nselect == -1) {
				threads_pool->listen = true; //设置条件变量, 通知其他线程可以去成为监听线程
				pthread_cond_signal(&threads_pool->threads_accept);
				pthread_mutex_unlock(&threads_pool->threads_lock);//释放监听锁
				close(connectedfd);

				if (errno != EINTR) {
					perror("select");
				}
				continue;//将线程放回线程池, 重新成为空闲线程
			} else if (nselect == 0) {//超时				
				threads_pool->listen = true; //设置条件变量, 通知其他线程可以去成为监听线程
				pthread_cond_signal(&threads_pool->threads_accept);
				pthread_mutex_unlock(&threads_pool->threads_lock);//释放监听锁 
				close(connectedfd);//关闭与新客户端的tcp连接
				continue; //将线程放回线程池, 重新成为空闲线程
			} else if (FD_ISSET(threads_pool->udpfd, &rset)) {//检测到客户端发送过来的udp包, 且客户端只向服务器端发送一次udp包, 用于使服务器端获得											  //客户端的udp地址,然后保存到服务器本地列表中
				struct sockaddr_in udpclientaddr;	//客户端udp地址
				socklen_t udpclientaddr_len = sizeof(udpclientaddr); //客户端udp地址长度
				
				char udprecvbuf[1024];
				memset(udprecvbuf, 0, sizeof(udpclientaddr));

				int udp_ret = recvfrom(threads_pool->udpfd, udprecvbuf, sizeof(udprecvbuf), 0, (struct sockaddr*)&udpclientaddr, \
					&udpclientaddr_len);
					
				if (udp_ret == -1) {
					perror("recvfrom");
					threads_pool->listen = true; //设置条件变量, 通知其他线程可以去成为监听线程
					pthread_cond_signal(&threads_pool->threads_accept);//接收出错, 发送信号, 通知其他线程去监听
					pthread_mutex_unlock(&threads_pool->threads_lock);//接收出错, 释放监听锁		
					close(connectedfd);
					continue;//将线程放回线程池, 重新成为空闲线程
				}
					
				//打印客户端地址
				char *c_udpip = inet_ntoa(udpclientaddr.sin_addr);
				unsigned short udpport = ntohs(udpclientaddr.sin_port);
				printf(" 新客户上线: ip = %s, port = %d\n", c_udpip, udpport);
				//获得客户端的udp套接口的ip地址, 并在创建用户时,保存起来用于之后的私聊, 即发送给用户用户列表,使
				//用户进行选择和哪个用户私聊, 其中发回的列表的每个条目包含了用户端的udp套接口的地址, 从而实现了点对点的聊天
				std::string userName(udprecvbuf, udprecvbuf+strlen(udprecvbuf));//用户名		
				std::string udpip(c_udpip, c_udpip+strlen(c_udpip));//udp ip
				//保存用户
				NetUser user(userName, udpip, udpport);
				loginUsers1[userName] = user;

				threads_pool->listen = true; //设置条件变量, 通知其他线程可以去成为监听线程
				pthread_cond_signal(&threads_pool->threads_accept); //当前线程成为工作线程, 通知其他线程可以去监听客户端连接请求
				pthread_mutex_unlock(&threads_pool->threads_lock);//当前线程成为工作线程,释放监听锁
			}
		}
		
		//当前线程变成工作者线程, 为客户端提供服务
		bool logout = false; //判断客户端是否退出, 由serveClient函数中确定
		while (!logout) {
			serveClient(connectedfd, logout);
		}
		//客户端退出
		//当前线程重新成为空闲线程重新等待获得互斥锁
	}
	pthread_mutex_unlock(&threads_pool->threads_lock); //线程退出时,释放锁
	pthread_exit(0); //退出线程
}

void Server2::serveClient(int conn, bool &logout) {
	//读取数据
	Message revbuf;//接收缓存
	Message sendbuf;//发送缓存
	memset(&revbuf, 0, sizeof(revbuf));
	memset(&sendbuf, 0, sizeof(sendbuf));

	int ret = readn(conn, &revbuf.type, 4);//首先接收消息的类型信息
	if (ret == -1)
		ERR_REPORT("read1");
	if (ret == 0 || ret < 4) {
		logout = true;
		printf(" 客户下线\n");

		//通知其他用户用户下线
		NetUser temp ("xx", "0", 0, conn);
		remindUserLogOut(temp);
		conns.erase(std::remove(conns.begin(), conns.end(), conn), conns.end());
		close(conn);
		return;
	}
	//接收消息的长度信息
	ret = readn(conn, &revbuf.len, 4);
	if (ret == -1) {
		ERR_REPORT("read2");
		logout = true;
	}
	if (ret == 0 || ret < 4) {
		logout = true;
		printf(" 客户下线\n");
		//通知其他用户用户下线
		NetUser temp ("xx", "0", 0, conn);
		remindUserLogOut(temp);

		conns.erase(std::remove(conns.begin(), conns.end(), conn), conns.end());
		close(conn);
		return;
	}
	//读取消息
	int n = ntohl(revbuf.len);
	int type = ntohl(revbuf.type);

	switch(type) {
		case LOGIN: {
			ret = readn(conn, revbuf.content, n);

			if (ret == -1)
				ERR_REPORT("read");

			std::string userName(revbuf.content, revbuf.content + strlen(revbuf.content));

			//服务器通知所有在线用户有用户登陆了
			for (std::list<NetUser>::iterator iter = loginUsers2.begin(); iter != loginUsers2.end(); iter++) {
				sendbuf.type = htonl(USERLOGIN);
				std::string content = userName + " " + loginUsers1[userName].getUDPIp() + " ";
				unsigned short udpport = loginUsers1[userName].getUDPPort();
				std::ostringstream os2;
				os2 << udpport;
				content += os2.str();

				strcpy(sendbuf.content, content.c_str());
				int n = strlen(sendbuf.content);
				sendbuf.len = htonl(n);
				writen(iter->getConnectedfd(), &sendbuf, 8 + n);
			}
			//设置已连接套接口,同时将该用户添加到loginUsers2中保存起来,用于发送上线 ,下线提示信息时使用
			loginUsers1[userName].setConnectedfd(conn);
			loginUsers2.push_back(loginUsers1[userName]);
			//给新客户端返回登陆成功信息.
			sendbuf.type = htonl(LOGIN_SUCCESS);
			std::string content = " \n1.查看所有在线用户\n2.私聊\n3.群聊\n4.退出系统";
			strcpy(sendbuf.content, content.c_str());
			sendbuf.len = htonl(strlen(sendbuf.content));

			writen(conn, &sendbuf, 8 + strlen(sendbuf.content));

			memset(&sendbuf, 0, sizeof(sendbuf));
			memset(&revbuf, 0, sizeof(revbuf));
			break;
		}
		case LOGOUT: {
			ret = readn(conn, revbuf.content, n);
			if (ret == -1)
				ERR_REPORT("read");

			NetUser temp(std::string(revbuf.content, revbuf.content+strlen(revbuf.content)), "0", 0, conn);

			remindUserLogOut(temp);
			
			conns.erase(std::remove(conns.begin(), conns.end(), conn), conns.end());
			//给新客户端返回退出信息.
			sendbuf.type = htonl(LOGOUT_SUCCESS);
			strcpy(sendbuf.content," 下线成功!");
			sendbuf.len = htonl(strlen(sendbuf.content));

			writen(conn, &sendbuf, 8+strlen(sendbuf.content));
			
			//关闭与客户端的连接
			printf(" 客户下线\n");
			logout = true;
			close(conn);

			memset(&sendbuf, 0, sizeof(sendbuf));
			memset(&revbuf, 0, sizeof(revbuf));
			break;
		}
		case LISTALLUSER: {
			ret = readn(conn, revbuf.content, n);//之前没加使得在客户端套接字的发送缓存中驻留有字节,使得list user一遍之后
															 //再list,服务器端在接收前4个字节的操作时,一直出错
															 //注意read在读取数据之后,会清空对等方的发送缓存
			memset(&revbuf, 0, sizeof(revbuf));
			//发送用户列表
			for (std::list<NetUser>::iterator iter = loginUsers2.begin(); iter != loginUsers2.end(); iter++) {
				sendbuf.type = htonl(USERLIST);
				//发送用户的姓名, ip地址, port端口号
				
				std::string content = iter->getName() + " " + iter->getUDPIp() + " ";
				std::ostringstream os;
				os << iter->getUDPPort();
				content += os.str();
				strcpy(sendbuf.content, content.c_str());
				sendbuf.len = htonl(strlen(sendbuf.content));

				writen(conn, &sendbuf, 8+strlen(sendbuf.content));

				memset(&sendbuf, 0, sizeof(sendbuf));
			}
			break;
		}
	}
}

void Server2::remindUserLogOut(const NetUser &user) {
	std::list<NetUser>::iterator iter = std::find(loginUsers2.begin(), loginUsers2.end(), user);
	if (iter != loginUsers2.end())
		loginUsers2.erase(iter);
	//loginUsers1.erase(user.getName());
	//通知所有其他用户该用户下线
	Message sendbuf;
	memset(&sendbuf, 0, sizeof(sendbuf));

	for (std::list<NetUser>::iterator iter = loginUsers2.begin(); iter != loginUsers2.end(); iter++) {
		sendbuf.type = htonl(USERLOGOUT);
		std::string content = " 用户下线: " + user.getName();
		strcpy(sendbuf.content, content.c_str());
		int n = strlen(sendbuf.content);
		sendbuf.len = htonl(n);
		writen(iter->getConnectedfd(), &sendbuf, 8 + n);
		memset(&sendbuf, 0, sizeof(sendbuf));
	}
}

