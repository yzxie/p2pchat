#ifndef CLIENT_H
#define CLIENT_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <vector>
#include "Public.h"
#include "NetUser.cpp"

class Client {
	public:
		Client();
		bool loginServer(const std::string &clientName);
		void Online();
		void logoutServer();
	private:
		int connectfd;
		struct sockaddr_in serveraddr;
		socklen_t serveraddrlen;

		std::string clientName;
		std::vector<NetUser> onlineUsers;

		int udpfd;
		struct sockaddr_in localaddr;
		socklen_t localaddrlen;
		bool chatWithPeer(int peerindex);
		bool chatInGroup();
};

#endif