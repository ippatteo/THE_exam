#include <iostream>
#include <map>
#include <stdexcept>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>

class Socket
{
private:
	int _sockfd;
	struct sockaddr_in _servaddr;

public:
	Socket(int port) :
			_sockfd(socket(AF_INET, SOCK_STREAM, 0))
		{
			if(_sockfd == -1){
				throw std::runtime_error("Socket creation failed");
			}

			memset(&_servaddr, 0, sizeof(_servaddr));
			_servaddr.sin_family = AF_INET;
			_servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
			_servaddr.sin_port = htons(port);
		}

	~Socket()
		{
			if (_sockfd != -1)
			{
				close(_sockfd);
			}
		}
	void bindAndListen()
		{
			if(bind(_sockfd, (struct sockaddr *)&_servaddr, sizeof(_servaddr)) < 0)
			{
				throw std::runtime_error("Socket bind failed");
			}

			if (listen(_sockfd, 10) < 0)
			{
				throw std::runtime_error("Socket listen failed");
			}
		}
	int accept(struct sockaddr_in& clientAddr)
		{
			socklen_t clientLen = sizeof(clientAddr);
			int clientSocketFd = ::accept(_sockfd, (struct sockaddr*)&clientAddr, &clientLen);
			if(clientSocketFd < 0)
			{
				throw std::runtime_error("Failed to accept connection");
			}
			return clientSocketFd;
		}
		std::string pullMessage(int fd)
		{
			char buf[1024];
			int byte_read = recv(fd, buf, 1001, 0);
			if(byte_read <= 0)
				return std::string("");
			buf[byte_read] = '\0';
			std::string ret(buf);
			return ret;
		}
};

class Server
{
private: 
	Socket _listeningSocket;
	std::map<std::string, std::string>& db;
public:
	Server(int port, std::map<std::string, std::string>& database) :
		_listeningSocket(port), db(database)
		{

		}
	void handleMessage(int fd, std::string message){
		std::istringstream msg(message);
		std::string command, key, value;
		msg >> command >> key >> value;
		if(command == "POST" && !key.empty() && !value.empty()){
			db[key] = value;
			send(fd, "0\n", 2, 0);
		}else if(command == "GET" && !key.empty() && value.empty()){
			auto it = db.find(key);
			if(it != db.end()){
				std::string ret = "0 " + it->second + "\n";
				send(fd, ret.c_str(), ret.size(), 0);
				return;
			}
			send(fd, "1\n", 2, 0);
		}
		else if(command == "DELETE" && !key.empty() && value.empty()){
			auto it = db.find(key);
			if(it != db.end()){
				db.erase(it);
				send(fd, "0\n", 2, 0);
				return;
			}
			send(fd, "1\n", 2, 0);
		}
		else
			send(fd, "2\n", 2, 0);
	}
	int run()
		{
			try
			{
				_listeningSocket.bindAndListen();
				std::cout << "ready" << std::endl;
				sockaddr_in clientAddr;
				while(true){
					try{
						int clientFd = _listeningSocket.accept(clientAddr);
						while(true){
							std::string message = _listeningSocket.pullMessage(clientFd);
							if(message.empty()){
								break;
								close(clientFd);
							}
							handleMessage(clientFd, message);
						}
					
					}
					catch(std::exception& e){
						std::cerr << "Error connecting new client" << e.what() << std::endl;
					}
				}
				

				return 0; //Success
			}
			catch(const std::exception& e)
			{
				std::cerr << "Error during server run: " << e.what() << std::endl;
				return 1; // Return error code if server fails to start
			}
		}
};
