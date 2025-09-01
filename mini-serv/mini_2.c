// mini_serv.c - solution for "mini_serv" subject (select-based local chat server)
//
// Allowed functions used: write, close, select, socket, accept, listen, send, recv, bind,
// strstr, malloc, realloc, free, calloc, bzero, atoi, sprintf, strlen, exit, strcpy, strcat, memset
//
// No #define as requested by the subject.

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

typedef struct s_client {
	int   fd;
	int   id;
	char *inbuf;   // accumulated incoming data not yet split into lines
	char *outbuf;  // pending outgoing data waiting to be sent
} t_client;

/* ---------- tiny helpers, no forbidden functions ---------- */

static void	fatal(void)
{
	write(2, "Fatal error\n", 12);
	exit(1);
}

static void wrong_args(void)
{
	write(2, "Wrong number of arguments\n", 26);
	exit(1);
}

static size_t	my_strlen(const char *s)
{
	size_t i = 0;
	if (!s) return 0;
	while (s[i]) i++;
	return i;
}

static void *	xmalloc(size_t n)
{
	void *p = malloc(n);
	if (!p) fatal();
	return p;
}

// Append "src" (C string) to *dst (C string), reallocating as needed.
// After call, *dst is a valid C string (null-terminated).
static void	append_cstr(char **dst, const char *src)
{
	size_t a = my_strlen(*dst);
	size_t b = my_strlen(src);
	char *newbuf = (char *)realloc(*dst, a + b + 1);
	if (!newbuf) fatal();
	*dst = newbuf;
	size_t i;
	for (i = 0; i < b; i++) newbuf[a + i] = src[i];
	newbuf[a + b] = '\0';
}

// Append exactly n bytes from src into *dst and keep it a C string. (src may not be null-terminated)
static void	append_nbytes(char **dst, const char *src, size_t n)
{
	size_t a = my_strlen(*dst);
	char *newbuf = (char *)realloc(*dst, a + n + 1);
	if (!newbuf) fatal();
	*dst = newbuf;
	size_t i;
	for (i = 0; i < n; i++) newbuf[a + i] = src[i];
	newbuf[a + n] = '\0';
}

// Extract one line (ending with '\n') from *buf if present.
// Returns a newly allocated C string that includes the trailing '\n'.
// Removes the extracted line from *buf.
// If no full line is present, returns NULL and leaves *buf unchanged.
static char *	extract_line(char **buf)
{
	if (!buf || !*buf) return NULL;
	char *s = *buf;
	size_t i = 0;
	while (s[i])
	{
		if (s[i] == '\n')
		{
			size_t linelen = i + 1; // include '\n'
			char *line = (char *)xmalloc(linelen + 1);
			size_t k;
			for (k = 0; k < linelen; k++) line[k] = s[k];
			line[linelen] = '\0';

			// shift remaining bytes left
			size_t restlen = my_strlen(s + linelen);
			for (k = 0; k < restlen; k++) s[k] = s[linelen + k];
			s[restlen] = '\0';

			return line;
		}
		i++;
	}
	return NULL;
}

// Byte swap helpers instead of htons/htonl (not in allowed list)
static unsigned short	ft_htons(unsigned short v)
{
	return (unsigned short)((v >> 8) | (v << 8));
}

static unsigned int	ft_htonl(unsigned int v)
{
	return ((v & 0x000000FFU) << 24) |
	       ((v & 0x0000FF00U) << 8)  |
	       ((v & 0x00FF0000U) >> 8)  |
	       ((v & 0xFF000000U) >> 24);
}

/* ---------- server state ---------- */

static t_client *clients[FD_SETSIZE];
static int       next_id = 0;
static int       max_fd  = 0;
static int       server_fd = -1;
static fd_set    all_reads, all_writes;

// Get client pointer by fd; returns NULL if not found
static t_client *get_client(int fd)
{
	if (fd < 0 || fd >= FD_SETSIZE) return NULL;
	return clients[fd];
}

static void	clear_client_slot(int fd)
{
	if (fd >=0 && fd < FD_SETSIZE) clients[fd] = NULL;
}

// Broadcast raw C string message "msg" to all clients except "except_fd" (use -1 for none)
static void	broadcast(int except_fd, const char *msg)
{
	int fd;
	for (fd = 0; fd <= max_fd; fd++)
	{
		t_client *c = get_client(fd);
		if (!c) continue;
		if (fd == except_fd) continue;
		append_cstr(&c->outbuf, msg);
	}
}

// Build "server: client %d just arrived/left\n"
static void	broadcast_server_event(int except_fd, int id, int arrived)
{
	char buf[64];
	if (arrived)
		sprintf(buf, "server: client %d just arrived\n", id);
	else
		sprintf(buf, "server: client %d just left\n", id);
	broadcast(except_fd, buf);
}

// For each complete line in src, send "client %d: <line>" to all except 'from_fd'
static void	distribute_lines_from(t_client *sender)
{
	while (1)
	{
		char *line = extract_line(&sender->inbuf);
		if (!line) break;

		char prefix[64];
		sprintf(prefix, "client %d: ", sender->id);

		int fd;
		for (fd = 0; fd <= max_fd; fd++)
		{
			t_client *c = get_client(fd);
			if (!c || c == sender) continue;

			append_cstr(&c->outbuf, prefix);
			append_cstr(&c->outbuf, line); // 'line' already ends with '\n'
		}
		free(line);
	}
}

static void	update_max_fd(void)
{
	int fd = FD_SETSIZE - 1;
	while (fd >= 0)
	{
		if (get_client(fd) || fd == server_fd)
		{
			max_fd = fd;
			return;
		}
		fd--;
	}
	max_fd = server_fd;
}

static void	remove_client(int fd)
{
	t_client *c = get_client(fd);
	if (!c) return;
	int id = c->id;

	// announce departure to everyone else
	broadcast_server_event(fd, id, 0);

	FD_CLR(fd, &all_reads);
	FD_CLR(fd, &all_writes);
	close(fd);

	if (c->inbuf) free(c->inbuf);
	if (c->outbuf) free(c->outbuf);
	free(c);
	clear_client_slot(fd);

	if (fd == max_fd) update_max_fd();
}

static void	add_client(int newfd)
{
	t_client *c = (t_client *)xmalloc(sizeof(t_client));
	c->fd = newfd;
	c->id = next_id++;
	c->inbuf = (char *)xmalloc(1);
	c->inbuf[0] = '\0';
	c->outbuf = (char *)xmalloc(1);
	c->outbuf[0] = '\0';

	clients[newfd] = c;

	FD_SET(newfd, &all_reads);
	FD_SET(newfd, &all_writes); // we only actually send when there's data pending

	if (newfd > max_fd) max_fd = newfd;

	// announce arrival to everyone else (NOT the newcomer)
	broadcast_server_event(newfd, c->id, 1);
}

/* ---------- main loop ---------- */

int	main(int argc, char **argv)
{
	int port;
	struct sockaddr_in servaddr;

	if (argc != 2)
		wrong_args();

	port = atoi(argv[1]);

	// create socket
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) fatal();

	// bind 127.0.0.1:port
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = ft_htons((unsigned short)port);
	servaddr.sin_addr.s_addr = ft_htonl(0x7F000001U); // 127.0.0.1
	if (bind(server_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
		fatal();

	if (listen(server_fd, 128) < 0)
		fatal();

	// init client table
	int i;
	for (i = 0; i < FD_SETSIZE; i++) clients[i] = NULL;

	FD_ZERO(&all_reads);
	FD_ZERO(&all_writes);
	FD_SET(server_fd, &all_reads);
	max_fd = server_fd;

	while (1)
	{
		fd_set rfds = all_reads;
		fd_set wfds = all_writes;

		// only set write bit if client has pending data
		int fd;
		FD_ZERO(&wfds);
		for (fd = 0; fd <= max_fd; fd++)
		{
			t_client *c = get_client(fd);
			if (c && my_strlen(c->outbuf) > 0)
				FD_SET(fd, &wfds);
		}

		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0)
			continue; // ignore and loop

		// new connection?
		if (FD_ISSET(server_fd, &rfds))
		{
			struct sockaddr_in cliaddr;
			socklen_t len = sizeof(cliaddr);
			int newfd = accept(server_fd, (struct sockaddr *)&cliaddr, &len);
			if (newfd >= 0)
				add_client(newfd);
			// If accept fails, we simply continue; fatal only before accepting starts
		}

		// readable clients
		for (fd = 0; fd <= max_fd; fd++)
		{
			if (fd == server_fd) continue;
			if (!FD_ISSET(fd, &rfds)) continue;

			t_client *c = get_client(fd);
			if (!c) continue;

			char buf[4096];
			int n = recv(fd, buf, sizeof(buf), 0);
			if (n <= 0)
			{
				remove_client(fd);
				continue;
			}
			// append to client's input buffer
			append_nbytes(&c->inbuf, buf, (size_t)n);
			// extract and distribute full lines
			distribute_lines_from(c);
		}

		// writable clients: flush pending outbuf
		for (fd = 0; fd <= max_fd; fd++)
		{
			if (!FD_ISSET(fd, &wfds)) continue;
			t_client *c = get_client(fd);
			if (!c) continue;

			size_t outlen = my_strlen(c->outbuf);
			if (outlen == 0) continue;

			int n = send(fd, c->outbuf, outlen, 0);
			if (n < 0)
			{
				// treat as disconnection
				remove_client(fd);
				continue;
			}
			else if ((size_t)n < outlen)
			{
				// shift remaining data left
				size_t k;
				for (k = 0; k < outlen - (size_t)n; k++)
					c->outbuf[k] = c->outbuf[n + k];
				c->outbuf[outlen - (size_t)n] = '\0';
			}
			else
			{
				// flushed everything
				c->outbuf[0] = '\0';
			}
		}
	}

	return 0;
}
