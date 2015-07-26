#include <iostream>
#include "Server2.cpp"

int main() {
	const int max_pool_size = 1000;
	Server2::initServer2(max_pool_size);
	while(1);
	return 0;
}