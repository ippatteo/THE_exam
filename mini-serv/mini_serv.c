#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netinet/ip.h>

int count =0, max_fd=0;
int ids[65536];

char *msgs[65536];
char buf_write[42], buf_read[1001];

fd_set afds,rfds,wfds;

int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void fatal()
{
	write(2,"Fatal error\n",12);
	exit(1);
}

void	notify(int or, char *msg)
{
	for(int fd=0;fd<=max_fd;fd++)
	{
		if(FD_ISSET(fd,&wfds) && or!=fd)
			send(fd,msg,strlen(msg),0);
	}
}

void	regist(int fd)
{
	if(fd>max_fd)
		max_fd=fd;
	ids[fd]=count++;
	msgs[fd]=NULL;
	FD_SET(fd,&afds);
	sprintf(buf_write,"server: client %d just arrived\n",ids[fd]);
	notify(fd,buf_write);
}

void	remov(int fd)
{
        sprintf(buf_write,"server: client %d just left\n",ids[fd]);
        notify(fd,buf_write);
	free(msgs[fd]);
	FD_CLR(fd,&afds);
	close(fd);
}

void	messaggia(int fd)
{
	char *msg;
	while(extract_message(&(msgs[fd]),&msg))
	{
		sprintf(buf_write,"client %d: ",ids[fd]);
		notify(fd,buf_write);
		notify(fd,msg);	
		free(msg);
	}
}

int create_socket()
{
	max_fd=socket(AF_INET,SOCK_STREAM,0);
	if(max_fd<0)
		fatal();
	FD_SET(max_fd,&afds);
	return max_fd;
}

int main(int c, char **v)
{
	if(c!=2)
	{
		write(2,"Wrong number of arguments\n", 26);
		return 1;
	}
	FD_ZERO(&afds);
	int sockfd=create_socket();
	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(v[1]));
       if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
       fatal();
	if (listen(sockfd, SOMAXCONN) != 0)
	 fatal();

	while(1)
	{
		wfds=rfds=afds;
		if(select(max_fd+1,&rfds,&wfds,NULL,NULL)<0)
			fatal();
		for(int fd=0;fd<=max_fd;fd++)
        {
                if(!FD_ISSET(fd,&rfds))
			continue;
		if(fd==sockfd)
		{
			socklen_t len=sizeof(servaddr);
			int client_fd =accept(fd, (struct sockaddr *)&servaddr,&len);
			if(client_fd>=0)
			{
				regist(client_fd);
				break;
			}
		
		
		}
		else
		{
			int bytes=recv(fd,buf_read,1000,0);
			if(bytes<=0)
			{
				remov(fd);
				break;
			}
			buf_read[bytes]='\0';
			msgs[fd]=str_join(msgs[fd],buf_read);
			messaggia(fd);
		}

	}
	
	
	
	
	
	
	
	
	
	
	
	
	}
return 0;	



}
