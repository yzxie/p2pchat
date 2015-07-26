#include "Client.h"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>

Client::Client() {
	serveraddrlen = sizeof(serveraddr);
}

bool Client::loginServer(const std::string &clientName) {	
	//tcp通信
	if ((connectfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return false;
	}

	//指定服务器地址
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr("192.168.42.34");
	serveraddr.sin_port = htons(6666);

	if (connect(connectfd, (const struct sockaddr*)&serveraddr, serveraddrlen) < 0) {
		perror("connect server failure");
		return false;
	}

	//改进: 先与服务器建立连接在来发送udp数据, 可以使得在服务器未启动时, 防止客户端发送udp数据而阻塞
	//创建一个udp套接口用于私聊, 本地地址为在第一次调用sendto向服务器端发送一个消息时,自动生成, 以后不向服务器端发送消息
	if (((udpfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)) {
		return false;
	}
	struct sockaddr_in udpserveraddr;
	socklen_t udpserveraddr_len = sizeof(udpserveraddr);
	udpserveraddr.sin_family = AF_INET;
	udpserveraddr.sin_addr.s_addr = inet_addr("192.168.42.34");
	udpserveraddr.sin_port = htons(5555);
	//发送用户名
	if (sendto(udpfd, clientName.c_str(), strlen(clientName.c_str()), 0, (struct sockaddr*)&udpserveraddr, udpserveraddr_len) < 0) {
		perror("send udp failure: ");
		return false;
	}

	//将用户名发送给服务器端, tcp通信
	Message sendMsg;
	
	sendMsg.type = htonl(LOGIN);

	strcpy(sendMsg.content, clientName.c_str());
	sendMsg.len = htonl(strlen(sendMsg.content));

	writen(connectfd, &sendMsg, 8+strlen(sendMsg.content));

	this->clientName = clientName;

	return true;
}

bool Client::chatWithPeer(int peerindex) {
	if (clientName.compare(onlineUsers[peerindex].getName()) == 0) {
		printf(" 不能发送消息给自己!\n");
		return false;
	}
	//需要通信的对等方的地址
	struct sockaddr_in peeraddr;
	socklen_t peeraddrlen;
	peeraddrlen = sizeof(peeraddr);
	peeraddr.sin_family = AF_INET;
	std::string ip = onlineUsers[peerindex].getUDPIp();

	peeraddr.sin_addr.s_addr = inet_addr(ip.c_str());
	peeraddr.sin_port = htons(onlineUsers[peerindex].getUDPPort());
	//输入并发送消息
	std::string message;
	std::cout << " 请输入您要发送的消息: ";
	std::cin.ignore();
	std::getline(std::cin, message);
	std::string content = clientName + " 发来私聊信息: " + message;
	if ((sendto(udpfd, content.c_str(), strlen(content.c_str()), 0, (struct sockaddr*)&peeraddr, peeraddrlen)) < 0) {
		perror("chat sendto");
		return false;
	}
	return true;
}

bool Client::chatInGroup() {
	//由用户自己直接发送消息给其他所有在线用户,使用的是udp通信
	//需要通信的对等方的地址
	struct sockaddr_in peeraddr;
	socklen_t peeraddrlen;
	peeraddrlen = sizeof(peeraddr);
	peeraddr.sin_family = AF_INET;
	
	//输入
	std::string message;
	std::cout << " 请输入您要发送的群聊消息: ";
	std::cin.ignore();
	std::getline(std::cin, message);
	
	std::string content = clientName + " 发来群聊信息: " + message;
	for (int i = 0; i < onlineUsers.size(); i++) {
		if (clientName.compare(onlineUsers[i].getName()) != 0) {			
			std::string ip = onlineUsers[i].getUDPIp();
			peeraddr.sin_addr.s_addr = inet_addr(ip.c_str());
			peeraddr.sin_port = htons(onlineUsers[i].getUDPPort());
			//发送消息
			if ((sendto(udpfd, content.c_str(), strlen(content.c_str()), 0, (struct sockaddr*)&peeraddr, peeraddrlen)) < 0) {
				perror("chat sendto");
				return false;
			}
		}
	}
	return true;
}

void Client::Online() {
	int stdinfd = fileno(stdin);
	int maxfd;
	if (connectfd > stdinfd)
		maxfd = connectfd;
	else
		maxfd = stdinfd;
	if (udpfd > maxfd)
		maxfd = udpfd;

	fd_set rset;//一个集合用于检测里面IO的可读事件
	FD_ZERO(&rset);

	Message sendbuf;//发送缓存区
	memset(&sendbuf, 0, sizeof(sendbuf));
	Message recvbuf; //接收缓存区
	memset(&recvbuf, 0, sizeof(recvbuf));

	bool on = true; //标记是否处于登陆状态

	while (on) {
		FD_SET(connectfd, &rset);//监听套接口放入集合中
		FD_SET(stdinfd, &rset);//输入描述符放入集合
		FD_SET(udpfd, &rset);//将UDP套接口放入集合,用于收发私聊信息
		timeval t = {0, 500000};
		int nselect = select(maxfd+1, &rset, NULL, NULL, &t);
		if (nselect == -1) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}
		if (nselect == 0) //超时
			continue;
		if (FD_ISSET(stdinfd, &rset)) {//检测到标准输入
			int choice;
			std::cin >> choice;
			
			switch(choice) {
				case 1: {//查看当前所有登陆用户列表
					sendbuf.type = htonl(LISTALLUSER);
					strcpy(sendbuf.content, "1");
					sendbuf.len = htonl(strlen(sendbuf.content));
					//发送读取服务器的所有登陆用户的请求信息
					writen(connectfd, &sendbuf, 8 + strlen(sendbuf.content));
					memset(&sendbuf, 0, sizeof(sendbuf));
					break;
				}
				case 2: {
					printf(" 在线用户:\n");
					for (int i = 0; i < onlineUsers.size(); i++) {
						if (clientName.compare(onlineUsers[i].getName()) == 0)
							std::cout << " " << i+1 << ": " << onlineUsers[i].getName() << "(自己)" << std::endl;
						else
							std::cout << " " <<i+1 << ": " << onlineUsers[i].getName() << std::endl;	
					}
					std::cout << " 请选择您需要私聊的用户序号或0放弃(序号或者0): ";
					int order;
					std::cin >> order;
					if (order >= 1 && order <= onlineUsers.size()) {
						if (chatWithPeer(order-1))
							printf(" 发送成功\n");
						else
							printf(" 发送失败\n");
					} else if (order == 0) {
						break;
					} else {
						std::cout << " 您选择的用户当前不存在或未登陆社区!" << std::endl;
					}
					break;
				}
				case 3: {//群聊
					if (chatInGroup())
						printf(" 发送群聊消息成功\n");
					else
						printf(" 发送群聊消息失败\n");
					break;
				}
				case 4: { //退出登陆
					sendbuf.type = htonl(LOGOUT);
					strcpy(sendbuf.content, clientName.c_str());
					sendbuf.len = htonl(strlen(sendbuf.content));
					//发送到服务器请求退出信息
					writen(connectfd, &sendbuf, 8 + strlen(sendbuf.content));
					memset(&sendbuf, 0, sizeof(sendbuf));
					break;
				}
				default: {
					std::cout << " 输入错误\n" << std::endl;
					std::cout << " 1. 查看社区当前在线用户" << std::endl;
					std::cout << " 2. 私聊" << std::endl;
					std::cout << " 3. 群聊" << std::endl;
					std::cout << " 4. 退出交流社区" << std::endl;
					break;
				}
			}
		}
		if (FD_ISSET(udpfd, &rset)) {//其他用户发来私聊或群聊信息
			
			char udp_recvbuf[1024];
			memset(udp_recvbuf, 0, sizeof(udp_recvbuf));

			int ret = recvfrom(udpfd, udp_recvbuf, sizeof(udp_recvbuf), 0, NULL, NULL);
			if (ret == -1 || ret == 0) {
				continue;
			}
			
			printf(" %s\n",  udp_recvbuf);
		}
		if (FD_ISSET(connectfd, &rset)) {//服务端发来消息
			
			//读取服务器发送过来的信息的类型
			int ret = readn(connectfd, &recvbuf.type, 4);
			if (ret == -1) {
				if (errno == EINTR)
					continue;
				perror("read type");
				break;
			}
			if (ret == 0) {
				std::cout << " 服务器关闭" << std::endl;
				close(connectfd);
				break;
			}
			if (ret < 4) {
				break;
			}

			//读取消息内容长度
			ret = readn(connectfd, &recvbuf.len, 4);
			if (ret == -1) {
				if (errno == EINTR)
					continue;
				perror("readn len");
				break;
			}
			if (ret == 0) {
				std::cout << " 服务器关闭" << std::endl;
				close(connectfd);
				break;
			}
			if (ret < 4) {
				perror("readn len");
				break;
			}

			//读取服务器发送过来的信息内容
			int n = ntohl(recvbuf.len);
			ret = readn(connectfd, recvbuf.content, n);

			if (ret == -1) {
				if (errno == EINTR)
					continue;
				perror("readn content");
				break;
			}

			int type = ntohl(recvbuf.type);//服务器发送过来的信息类型

			switch(type) {
				case LOGIN_SUCCESS: {
					//读取打印成功登陆信息
					printf("\n 登陆成功!");
					printf(" %s\n", recvbuf.content);
					break;
				}
				case USERLIST: {
					//保存已登陆用户的用户名, udpip, udp端口号信息, 以便通过UDP实现私聊
					char *space1 = strchr(recvbuf.content, ' ');
					std::string userName(recvbuf.content, space1);
					char *space2 = strrchr(recvbuf.content, ' ');
					std::string ip(++space1, space2);
					std::string port_str(++space2, recvbuf.content+strlen(recvbuf.content));
					std::istringstream os(port_str);
					unsigned short port;
					os >> port;
					NetUser temp(userName, ip, port);
					std::vector<NetUser>::iterator existed = std::find(onlineUsers.begin(), onlineUsers.end(), temp);
					if (existed == onlineUsers.end())
						onlineUsers.push_back(temp);
					if (userName.compare(clientName) == 0)
						std::cout << " 用户: " << userName << "(自己)" << std::endl;
					else
						std::cout << " 用户: " << userName << std::endl;
					break;
				}
				case LOGOUT_SUCCESS: {
					printf("%s\n", recvbuf.content);		
					shutdown(connectfd, SHUT_WR);
					on = false;
					break;
				}
				case USERLOGIN: {
					//解析出新用户的用户名 udpip地址, udp端口号
					char *space1 = strchr(recvbuf.content, ' ');
					std::string newUserName(recvbuf.content, space1);
					char *space2 = strrchr(recvbuf.content, ' ');
					std::string udpip(++space1, space2);
					std::string s_udpport(++space2, recvbuf.content+strlen(recvbuf.content));
					unsigned short udpport;
					std::istringstream os2(s_udpport);
					os2 >> udpport;
					std::cout << " 新用户上线: " << newUserName << std::endl;
					NetUser temp(newUserName, udpip, udpport);
					//std::vector<NetUser>::iterator existed = std::find(onlineUsers.begin(), onlineUsers.end(), temp);
					//如果当前该新用户之前未登陆, 将它添加到登陆列表中
					//if (existed == onlineUsers.end())
					onlineUsers.push_back(temp);
					break;
				}
				case USERLOGOUT: {
					char *space1 = strrchr(recvbuf.content, ' ');
					std::string logoutUserName(++space1, recvbuf.content+strlen(recvbuf.content));
					NetUser temp(logoutUserName, "0", 0);
					
					std::vector<NetUser>::iterator iter = std::find(onlineUsers.begin(), onlineUsers.end(), temp);
					if (iter != onlineUsers.end())
						onlineUsers.erase(iter);
					
					printf(" %s\n", recvbuf.content);
					break;
				}
				default: {
					printf(" 未知的错误信息\n");
					break;
				}
			}
			memset(&recvbuf, 0, sizeof(recvbuf));
		}
		
	}
}