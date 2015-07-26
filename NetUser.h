#ifndef NETUSER_H
#define NETUSER_H

#include <string>

class NetUser {
	public:
		NetUser();
		NetUser(std::string userName, std::string udpip, unsigned short udport, int connectedfd = -1);
		bool operator==(const NetUser &);
		std::string getName() const;
		std::string getUDPIp() const;
		unsigned short getUDPPort() const;
		int getConnectedfd() const;
		void setConnectedfd(int connectedfd);
	private:
		std::string userName;
		int connectedfd;
		std::string udpip;
		unsigned short udpport;
};

#endif