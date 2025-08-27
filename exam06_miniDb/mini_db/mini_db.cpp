#include "mini_db.hpp"
#include <iostream>
#include <map>
#include <fstream>
#include <signal.h>
#include <cstdlib> // ADDED: per atoi()


std::string path;
std::map<std::string, std::string> db;


void handler(int signalNum){
	std::ofstream file(path);
	if(!file.is_open())
	{
		std::cout << "Invalid file path" << std::endl;
		exit(signalNum); 
	}
	for (auto const&[key, value] : db){
		file << key << " " << value << std::endl;
	}
	file.close();
	exit(signalNum);
}

void readDb(){
	std::ifstream file(path);
	if(!file.is_open())
		return;
	std::string key, value;
	while(file >> key >> value){
		db[key] = value;
	}
}

int main(int ac, char **av)
{
	if(ac != 3)
	{
		std::cout << "Wrong number of argument" << std::endl;
		return (1); 
	}
	signal(SIGINT, handler);
	path = av[2];
	readDb();
	Server server(atoi(av[1]), db);
	server.run();
	return 0;
}

