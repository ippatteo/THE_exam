#include "./studio.hpp"
#include <signal.h>
#include <fstream>
#include <map>

std::map<std::string, std::string> db;
std::string path;

void handler(int sigNum){
	std::ofstream file(path);
	if(!file)
		exit(sigNum);
	for(auto const&[key, value] : db){
		file << key << " " << value << std::endl;
	}
	exit(sigNum);
}

void getDb(){
	std::ifstream file(path);
	if(!file)
		return;
	std::string key, value;
	while(file >> key >> value){
		db[key] = value;
	}
}


int main(int ac, char **av)
{
	if(ac != 3){
		std::cout << "Wrong number of arguments" <<std::endl;
		return 1;
	}
	path = av[2];
	signal(SIGINT, handler);
	getDb();
	Server server(atoi(av[1]), db);
	server.run();
}
