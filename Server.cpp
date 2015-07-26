#include "Server.h"
#include "Public.h"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>
#include <sstream>

bool Server::hasInstance = false;
Server* Server::instance = NULL;

Server::Server() {
	listenfd = -1;
	serveraddrlen = sizeof(serveraddr);
	events.resize(128);
}

Server::~Server() { 
	if (instance != NULL)
		delete instance;
	hasInstance = false;
}

Server* Server::getInstance() {
	if (instance == NULL) {
		instance = new Server();
	}
	hasInstance = true;
	return instance;
}


void Server::setUnBlock(int conn) {
	int ret;
	int flags = fcntl(conn, F_GETFL);
	if (flags == -1)
		ERR_REPORT("fcntl");
	flags |= O_NONBLOCK;
	ret = fcntl(conn, F_SETFL, flags);
	if (ret == -1)
		ERR_REPORT("fcntl");
}

void Server::remindUserLogOut(const NetUser &user) {
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
void Server::startServer() {
	//udp通信, 用于触发客户端的udp套接口生成本地地址
	if (((udpfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0))
		ERR_REPORT("udp socket");
	//设定服务器端UDP套接口地址
	udpaddrlen = sizeof(udpaddr);
	udpaddr.sin_family = AF_INET;
	udpaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	udpaddr.sin_port = htons(5555);
	if (bind(udpfd, (struct sockaddr*)&udpaddr, udpaddrlen) < 0) 
		ERR_REPORT("udp bind");

	//tcp通信
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		ERR_REPORT("socket");
	//设定服务器地址
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	serveraddr.sin_port = htons(6666);
	//设置地址可重复利用
	int on = 1;
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		ERR_REPORT("setsockopt");
	//绑定套接口到该地址
	if (bind(listenfd, (struct sockaddr*)&serveraddr, serveraddrlen) < 0)
		ERR_REPORT("bind");
	//监听客户端的连接
	if (listen(listenfd, SOMAXCONN) < 0)
		ERR_REPORT("listen");
}

void Server::ServerOn() {	
	//客户端地址
	struct sockaddr_in clientaddr;
	socklen_t clientaddrlen;
	//使用epoll函数来管理套接口描述符
	
	epollfd = epoll_create1(EPOLL_CLOEXEC);//创建一个epoll实例
	if (epollfd == -1)
		ERR_REPORT("epoll_create1");
	//设置监听套接口感兴趣的事件
	setUnBlock(listenfd);
	event.data.fd = listenfd;
	event.events = EPOLLIN | EPOLLET;//可读事件,边沿触发
	//epoll实例管理监听套接口
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event) < 0)
		ERR_REPORT("epoll_ctl");

	//设置upd套接口为感兴趣事件
	setUnBlock(udpfd);
	event.data.fd = udpfd;
	event.events = EPOLLIN | EPOLLET;//
	//epoll实例管理udp套接口
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, udpfd, &event) < 0)
		ERR_REPORT("epoll_ctl");

	//服务器监听新客户端来的连接,同时为已建立连接的套接字维持通信
	int ecount;//记录发生了事件的套接口的个数
	int conn;

	while (1) {
		
		ecount = epoll_wait(epollfd, &*events.begin(), (int)sizeof(events), -1);
		if (ecount == -1) {
			if (errno == EINTR)
				continue;
			ERR_REPORT("epoll_wait");
		}
		if (ecount == 0)
			continue;
		if (ecount >= events.size())
			events.resize(events.size() * 2);
		//处理各套接口的可读事件

		for (int i = 0; i < ecount; i++) {
			if (events[i].data.fd == udpfd) {//udp套接口发来消息
				struct sockaddr_in udpclientaddr;
				socklen_t udpclientaddr_len = sizeof(udpclientaddr);
				
				char udprecvbuf[1024];
				memset(udprecvbuf, 0, sizeof(udpclientaddr));

				int udp_ret = recvfrom(udpfd, udprecvbuf, sizeof(udprecvbuf), 0, (struct sockaddr*)&udpclientaddr, \
					&udpclientaddr_len);

				if (udp_ret == -1) {
					continue;
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
				//test
				printf("%s %d\n", inet_ntoa(udpclientaddr.sin_addr), ntohs(udpclientaddr.sin_port));
				if (sendto(udpfd, udprecvbuf, strlen(udprecvbuf), 0, (struct sockaddr*)&udpclientaddr, udpclientaddr_len) < 0) {
					perror("server sendto:");
				}
			} else if (events[i].data.fd == listenfd) {//监听到有新连接到来
				clientaddrlen = sizeof(clientaddr);
				conn = accept(listenfd, (struct sockaddr *)&clientaddr, &clientaddrlen);
				if (conn == -1)
					ERR_REPORT("accept");
				
				//设置该套接口为非阻塞模式, EPOLLET模式必须设置非阻塞模式
				setUnBlock(conn);
				//保存到conns, ips, ports中
				conns.push_back(conn);
				//添加到events中,由epollfd管理
				event.events = EPOLLIN | EPOLLET;
				event.data.fd = conn;
				if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn, &event) < 0)
					ERR_REPORT("epoll_ctl");
				//在loginUsers中记录
				//loginUsers.push_back(NetUser())
			} else if (events[i].events == EPOLLIN) {//已连接套接口收到客户端信息
				//可考虑使用线程来处理已连接套接口的可读事件
				//可使用线程池
				serveClient(events[i].data.fd);
			}
		}
	}
}

void Server::serveClient(int conn) {
	//读取数据
	Message revbuf;//接收缓存
	Message sendbuf;//发送缓存
	memset(&revbuf, 0, sizeof(revbuf));
	memset(&sendbuf, 0, sizeof(sendbuf));

	int ret = readn(conn, &revbuf.type, 4);//首先接收消息的类型信息
	if (ret == -1)
		ERR_REPORT("read1");
	if (ret == 0 || ret < 4) {
		printf("客户下线\n");

		//通知其他用户用户下线
		NetUser temp ("xx", "0", 0, conn);
		remindUserLogOut(temp);

		conns.erase(std::remove(conns.begin(), conns.end(), conn), conns.end());
		//epollfd不再管理检测该套接字
		event.events = EPOLLIN | EPOLLET;
		event.data.fd = conn;
		if (epoll_ctl(epollfd, EPOLL_CTL_DEL, conn, &event) < 0)
			ERR_REPORT("epoll_ctl");
		close(conn);
		return;
	}
	//接收消息的长度信息
	ret = readn(conn, &revbuf.len, 4);
	if (ret == -1)
		ERR_REPORT("read2");
	if (ret == 0 || ret < 4) {
		printf(" 客户下线\n");
		//通知其他用户用户下线
		NetUser temp ("xx", "0", 0, conn);
		remindUserLogOut(temp);

		conns.erase(std::remove(conns.begin(), conns.end(), conn), conns.end());
		//epollfd不再管理检测该套接字
		event.events = EPOLLIN | EPOLLET;
		event.data.fd = conn;
		if (epoll_ctl(epollfd, EPOLL_CTL_DEL, conn, &event) < 0)
			ERR_REPORT("epoll_ctl");

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
			strcpy(sendbuf.content," 成功退出图书交流社区!");
			sendbuf.len = htonl(strlen(sendbuf.content));

			writen(conn, &sendbuf, 8+strlen(sendbuf.content));
			
			//epollfd不再管理检测该套接字
			event.events = EPOLLIN | EPOLLET;
			event.data.fd = conn;
			
			if (epoll_ctl(epollfd, EPOLL_CTL_DEL, conn, &event) < 0)
				ERR_REPORT("epoll_ctl");
			//关闭与客户端的连接
			printf("客户下线\n");
	
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