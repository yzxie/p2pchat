#include <iostream>
#include "Client.cpp"

int main() {
	Client client;
	std::string userName;
	std::cout << "请输入您的用户名: ";
	std::cin >> userName;
	if (client.loginServer(userName)) {
			client.Online();
	}
	return 0;
}