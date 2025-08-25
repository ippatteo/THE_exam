#include <iostream>
#include <stdexcept>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <map>
#include <sstream>


//copy all the main.c in the subject
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
		//change pull message to retrive the message add int clientFd as param
		std::string pullMessage(int clientFd)
		{
			char buf[1024];
			int byte_read = recv(clientFd, buf, 1000, 0);
			if(byte_read <= 0)
				return std::string("");
			buf[byte_read] = '\0';
			std::string msg(buf);
			return msg;
		}
};

//ad ad db reference to constructor
class Server
{
private: 
	Socket _listeningSocket;
	std::map<std::string, std::string> &db;
public:
	Server(int port, std::map<std::string, std::string>&database) :
		_listeningSocket(port), db(database)
		{

		}
	//add handler function
	void handlemessage(int clientFd, std::string message){
		std::istringstream msg(message);
		std::string command, key, value;
		msg >> command >> key >> value;
		if(command == "POST" && !value.empty()){
			db[key] = value;
			send(clientFd, "0\n", 2, 0);
		}
		else if(command == "GET" && value.empty()){
			auto it = db.find(key);
			if(it != db.end()){
				std::string ret = "0 " + it->second + '\n';
				send(clientFd, ret.c_str(), ret.size(), 0);
			}
			else
				send(clientFd, "1\n", 2 ,0);

		}else if(command == "DELETE" && value.empty()){
			auto it = db.find(key);
			if(it != db.end()){
				db.erase(it);
				send(clientFd, "0\n", 2, 0);
			}
			else
				send(clientFd, "1\n", 2 ,0);
		}else{
			send(clientFd, "2\n", 2, 0);
		}
	}
	int run()
		{
			try
			{
				_listeningSocket.bindAndListen();
				//edit run function with this part
				std::cout << "ready" << std::endl;
				while(true){
					sockaddr_in clientAddr;
					try{
						int clientFd = _listeningSocket.accept(clientAddr);
						while(true){
							std::string message = _listeningSocket.pullMessage(clientFd);
							if(message.empty()){
								close(clientFd);
								break;
							}
							handlemessage(clientFd, message);
						}
					}
					catch(const std::exception& e){
						std::cerr << "Error adding a new client" << e.what() << std::endl;
						return 1;
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