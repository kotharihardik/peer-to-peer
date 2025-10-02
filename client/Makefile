all: client

client: client.cpp methods.cpp peer_server.cpp download.cpp global.cpp
	g++ -std=c++11 -pthread client.cpp methods.cpp download.cpp global.cpp peer_server.cpp\
	  -I/opt/homebrew/opt/openssl@3/include \
	  -L/opt/homebrew/opt/openssl@3/lib \
	  -lssl -lcrypto \
	  -o client

clean:
	rm -f client
