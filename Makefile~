CC=g++

ALGCS = -Wall -g
BIN = server

all:$(BIN)

%.o:%.cpp
	$(CC) $(ALGCS) $< -o $@ -lpthread

clean:
	rm -f $(BIN)