// #include <errno.h>
#include <string.h>
#include <unistd.h>
// #include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct s_client {
    int fd;
    int id;
    char *recv_buffer;
    char *send_buffer;
} t_client;

t_client clients[200];
int client_num = 0;
int max_fd;
int next_id = 0;

fd_set check_recv_fds, check_send_fds;
fd_set ready_recv_fds, ready_send_fds;

char buffer[4096];

void exit_if (int condition) {
    if (condition) {
        write(STDERR_FILENO, "Fatal error\n", 12);
        exit (1);
    }
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
	exit_if(newbuf == 0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

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
			exit_if(newbuf == 0);
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

void push_new_client ( int fd ) {
    t_client new_client;
    new_client.fd = fd;
    new_client.id = next_id++;
    new_client.recv_buffer = calloc(1, 1);
    new_client.send_buffer = calloc(1, 1);
    exit_if(new_client.recv_buffer == 0 || new_client.send_buffer == 0);
    FD_SET(fd, &check_recv_fds);
    max_fd = max_fd > fd ? max_fd : fd;
    clients[client_num++] = new_client;
}

void pop_client ( int fd ) {
    t_client *client = NULL;
    int idx = 0;
    for (idx = 0; idx < client_num; idx++) {
        if (clients[idx].fd == fd) {
            client = &(clients[idx]);
            break;
        }
    }
    free(client->recv_buffer);
    free(client->send_buffer);
    FD_CLR(fd, &check_recv_fds);
    FD_CLR(fd, &check_send_fds);
    FD_CLR(fd, &ready_send_fds);
    client_num -= 1;
    for (; idx < client_num; idx++) {
        clients[idx] = clients[idx + 1];
    }
}

void broadcast ( int fd, char * buffer ) {
    printf("%d: %s", fd, buffer); // TODO remove
    for (int idx = 0; idx < client_num; idx++) {
        if (clients[idx].fd == fd) continue;
        clients[idx].send_buffer = str_join(clients[idx].send_buffer, buffer);
        FD_SET(clients[idx].fd, &check_send_fds);
    }
}


int main ( int argc, char *argv[] ) {
    // Check for arguments
    if (argc != 2) {
        write(STDERR_FILENO, "Wrong number of arguments\n", 26);
        exit(1);
    }

    // Init server
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(server_fd < 0);
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(0x7F000001);
    server_addr.sin_port = htons(atoi(argv[1]));
    exit_if(bind(server_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) != 0);
    FD_ZERO(&check_recv_fds);
    FD_ZERO(&check_send_fds);
    FD_SET(server_fd, &check_recv_fds);
    max_fd = server_fd;
    struct timeval timeout;
    timeout.tv_usec = 500000; // 500ms;
    timeout.tv_sec = 0;

    // Start_server
    exit_if(listen(server_fd, 10) != 0);

    // Start loop
    while (1) {
        ready_recv_fds = check_recv_fds;
        ready_send_fds = check_send_fds;
        exit_if(select(max_fd + 1, &ready_recv_fds, &ready_send_fds, NULL, &timeout) < 0);

        for (int fd = 0; fd <= max_fd; fd++) {
            if (!FD_ISSET(fd, &ready_recv_fds)) continue;

            // Accept new client
            if (fd == server_fd) {
                int client_fd = accept(server_fd, NULL, NULL);
                sprintf(buffer, "server: client %d just arrived\n", next_id);
                broadcast(fd, buffer);
                push_new_client(client_fd);
                continue;
            }

            t_client *client = NULL;
            for (int idx = 0; idx < client_num; idx++) {
                if (clients[idx].fd == fd) {
                    client = &(clients[idx]);
                    break;
                }
            }
            ssize_t recv_len = recv(fd, buffer, 4096, 0);

            // Handle disconnected client
            if (recv_len <= 0) {
                if (strlen(client->send_buffer) != 0) {
		    client->send_buffer = str_join(client->send_buffer, "\n");
                    sprintf(buffer, "client %d: ", client->id);
                    broadcast(fd, buffer);
		    broadcast(client->send_buffer);
                }
                sprintf(buffer, "server: client %d just left\n", client->id);
                broadcast(fd, buffer);
                pop_client(fd);
                close(fd);
                continue;
            }

            // Handle message
            buffer[recv_len] = '\0';
            client->recv_buffer = str_join(client->recv_buffer, buffer);
            char* message = NULL;
            while (extract_message(&(client->recv_buffer), &message) != 0) {
                sprintf(buffer, "client %d: ", client->id);
                broadcast(fd, buffer);
		broadcast(fd, message);
                free(message);
            }
        }

        for (int fd = 0; fd <= max_fd; fd++) {
            if (!FD_ISSET(fd, &ready_send_fds)) continue;

            t_client *client = NULL;
            for (int idx = 0; idx < client_num; idx++) {
                if (clients[idx].fd == fd) {
                    client = &(clients[idx]);
                    break;
                }
            }
            ssize_t send_len = send(fd, client->send_buffer, strlen(client->send_buffer), 0);
            char *left = calloc(1, sizeof(*left) * (strlen(client->send_buffer + send_len) + 1));
            exit_if(left == NULL);
			strcpy(left, client->send_buffer + send_len);
            free(client->send_buffer);
            client->send_buffer = left;
            if (strlen(client->send_buffer) == 0) {
                FD_CLR(fd, &check_send_fds);
            }
        }
    }
    return 0;
}
