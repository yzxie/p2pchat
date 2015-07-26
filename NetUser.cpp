#include "NetUser.h"

NetUser::NetUser(): udpport(0), connectedfd(-1) {}
NetUser::NetUser(std::string userName, std::string udpip, unsigned short udpport, int connectedfd):
	userName(userName), udpip(udpip), udpport(udpport), connectedfd(connectedfd)  {}

bool NetUser::operator==(const NetUser &other) {
	return ((other.connectedfd == connectedfd) && (userName.compare(other.getName()) == 0));
}

std::string NetUser::getName() const {
	return userName;
}

std::string NetUser::getUDPIp() const {
	return udpip;
}

unsigned short NetUser::getUDPPort() const {
	return udpport;
}

int NetUser::getConnectedfd() const {
	return connectedfd;
}

void NetUser::setConnectedfd(int connectedfd) {
	this->connectedfd = connectedfd;
}
