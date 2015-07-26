#ifndef SERVER_H
#define SERVER_H

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <map>
#include <list>
#include <vector>
#include <string>

#include "NetUser.cpp"


class Server {
	public:
		~Server();
		static Server *getInstance();
		void startServer();
		void ServerOn();
		void serveClient(int conn);
		//void stopSever();
	private:
		Server();
		Server(const Server&);
		Server& operator=(const Server&);

		static bool hasInstance;
		static Server *instance;

		int listenfd;
		struct sockaddr_in serveraddr;
		socklen_t serveraddrlen;

		int epollfd;
		//
		std::list<NetUser> loginUsers2;
		
		std::vector<struct epoll_event> events;//保存可读事件
		struct epoll_event event;//设置监听套接口感兴趣的事件

		std::vector<int> conns; //保存已连接套接口

		void setUnBlock(int conn);
		void remindUserLogOut(const NetUser&);

		//用于为用户获得UDP地址
		int udpfd;
		struct sockaddr_in udpaddr;
		socklen_t udpaddrlen;

		std::map<std::string, NetUser> loginUsers1;
		//std::map<std::string, std::string> udpips;
		//std::map<std::string, unsigned short> udpports;
};	

#endif