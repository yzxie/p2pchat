CC=g++

ALGCS = -Wall -g
BIN = server main

all:$(BIN)

server.o: server.cpp
	g++ -g -c server.cpp -lpthread
server: server.o
	g++ -g -o server server.o -lpthread

main.o: main.cpp
	g++ -g -c main.cpp -lpthread
main: main.o
	g++ -g -o main main.o -lpthread

clean:
	rm -f $(BIN) *.o
