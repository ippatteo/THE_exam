#include <iostream>
#include <stdexcept>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <map>
#include <sstream>
#include <sys/select.h>  // Aggiunto per select()


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
			if(_sockfd == -1)
			{
				throw std::runtime_error("Socket creation failed");
			}

			memset(&_servaddr, 0, sizeof(_servaddr));
			_servaddr.sin_family = AF_INET;
			_servaddr.sin_addr.s_addr = 0x0100007F;
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
		//il vero pull messages
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
		
		// Getter per ottenere fd
		int getSocketFd() const
		{
			return _sockfd;
		}
};

//ggiunto db
class Server
{
private: 
	Socket _listeningSocket;
	std::map<std::string, std::string> &db; //database
public:
	//construct
	Server(int port, std::map<std::string, std::string>&database) :
		_listeningSocket(port), db(database)
		{

		}
	//add handler function (diverso da main)
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
				
				std::cout << "ready" << std::endl;

				  // Variabili per select()
        		fd_set readfds, writefds, active;
        		int fdMax = 0;
        		std::map<int, std::string> clientBuffers; // Buffer per messaggi parziali

				  // Inizializza set FD
       			 FD_ZERO(&active);
       			 int serverFd = _listeningSocket.getSocketFd();
        		FD_SET(serverFd, &active);
        		fdMax = serverFd;
				while(true)
				{
					//  select()
           			 readfds = writefds = active;
            		if (select(fdMax + 1, &readfds, &writefds, NULL, NULL) < 0)
             		   continue;
					
					// controlla ogni fd per vedere se è pronto
					for(int fd = 0; fd <= fdMax; fd++) 
					{
						if(!FD_ISSET(fd, &readfds)) continue; // Skip fd non pronti
						
						if(fd == serverFd)
						{
							// se fd pronto nuovo client in arrivo
							sockaddr_in clientAddr;
							try {
								int clientFd = _listeningSocket.accept(clientAddr);
								// nuovo client al set
								FD_SET(clientFd, &active);
								if(clientFd > fdMax) fdMax = clientFd;
								clientBuffers[clientFd] = "";
							}
							catch(const std::exception& e)
							{
								// ignora errori di accept
							}
						}
						else 
						{

							char buf[1024];
							int bytes = recv(fd, buf, 1000, 0);
							if(bytes <= 0) {
								// Client disconnesso
								FD_CLR(fd, &active);
								close(fd);
								clientBuffers.erase(fd);
							}
							else
							{
								buf[bytes] = '\0';
								clientBuffers[fd] += buf;
								
								// Processa messaggi completi (terminati con \n)
								size_t pos;
								while((pos = clientBuffers[fd].find('\n')) != std::string::npos) {
									std::string message = clientBuffers[fd].substr(0, pos);
									clientBuffers[fd].erase(0, pos + 1);
									if(!message.empty())
									{
										handlemessage(fd, message);
									}
								}
							}
						}
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