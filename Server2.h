#ifndef SERVER2_H
#define SERVER2_H

#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>
#include <list>
#include <vector>
#include <string>
#include <pthread.h>

#include "NetUser.cpp"
#include "Public.h"

class Server2 {
	public:
		static void initServer2(int max_pool_size);
		static void* serveThreadRun(void *arg); //static修饰,可以由c++成员函数指针到c函数指针的转换
		static void serveClient(int conn, bool &logout);
		//void stopSever();
	private:
		Server2();
		Server2(const Server2&);
		Server2& operator=(const Server2&);

		static idle_threads_pool *threads_pool;//线程池

		static std::list<NetUser> loginUsers2;//用于在某用户上线下线时, 发送通知给其他在线用户
		static std::vector<int> conns; //保存已连接套接口
		static std::map<std::string, NetUser> loginUsers1; //用于在用户上线时,收集用户的信息,并创建在线用户项.

		static void remindUserLogOut(const NetUser&);
};	

#endif