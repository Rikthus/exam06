#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct s_client
{
	int				fd;
	int				id;
	char			*buf;
	struct s_client	*next;
}	t_client;

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

void	error(char *msg)
{
	write(2, msg, strlen(msg));
	exit(1);
}

void	free_close(t_client **cli_lst)
{
	t_client	*prev;
	t_client	*curr;

	curr = *cli_lst;
	while (curr)
	{
		prev = curr;
		curr = curr->next;
		close(prev->fd);
		free(prev->buf);
		free(prev);
	}
	error("Fatal error\n");
}

int	add_client(t_client **cli_lst, int fd, int id)
{
	t_client	*new;
	t_client	*tmp;

	new = malloc(sizeof(*new));
	if (!new)
		return (-1);

	new->fd = fd;
	new->id = id;
	new->buf = NULL;
	new->next = NULL;

	if (*cli_lst == NULL)
		*cli_lst = new;
	else
	{
		tmp = *cli_lst;
		while (tmp->next)
			tmp = tmp->next;
		tmp->next = new;
	}
	return (0);
}

void	remove_client(t_client **cli_lst, int id)
{
	t_client	*prev;
	t_client	*curr;

	if (!(*cli_lst))
		return;
	
	curr = *cli_lst;
	if (curr->id == id)
	{
		*cli_lst = (*cli_lst)->next;
		close(curr->fd);
		free(curr->buf);
		free(curr);
	}
	else
	{
		while (curr->next)
		{
			prev = curr;
			curr = curr->next;
			if (curr->id == id)
			{
				prev->next = curr->next;
				close(curr->fd);
				free(curr->buf);
				free(curr);
				return;
			}
		}
	}
}

int	send_to_all(fd_set write_fd, t_client *cli_lst, char *msg, int sender_id)
{
	while (cli_lst)
	{
		if (cli_lst->id != sender_id && FD_ISSET(cli_lst->fd, &write_fd))
		{
			if (send(cli_lst->fd, msg, strlen(msg), 0) < 0)
				return (-1);
		}
		cli_lst = cli_lst->next;
	}
	return (0);
}

int	main(int argc, char **argv)
{
	t_client			*cli_lst = NULL;
	int					id = 0;
	int					sockfd, clifd;
	unsigned int		len;
	struct sockaddr_in	servaddr, cliaddr;
	fd_set				master_fd, read_fd, write_fd;
	char				buffer[4097];

	int					optval = 1;

	if (argc != 2)
		error("Wrong number of arguments\n");
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
		error("Fatal error\n");
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(2130706433);
	servaddr.sin_port = htons(atoi(argv[1]));

	if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
		error("Fatal error\n");
	
	if (listen(sockfd, 4096) != 0)
		error("Fatal error\n");

	len = sizeof(cliaddr);
	FD_ZERO(&master_fd);
	FD_SET(sockfd, &master_fd);

	while (1)
	{
		read_fd = write_fd = master_fd;
		bzero(&buffer, 4097);

		if (select(FD_SETSIZE, &read_fd, &write_fd, 0, 0) < 0)
			continue;
		
		if (FD_ISSET(sockfd, &read_fd))
		{
			clifd = accept(sockfd, (struct sockaddr *)&cliaddr, &len);
			if (clifd < 0)
				free_close(&cli_lst);
			if (add_client(&cli_lst, clifd, id) == -1)
				free_close(&cli_lst);
			sprintf(buffer, "server: client %d just arrived\n", id);
			FD_SET(clifd, &master_fd);
			if (send_to_all(write_fd, cli_lst, buffer, id) == -1)
				free_close(&cli_lst);
			id++;
			continue;
		}

		for (t_client *curr_cli = cli_lst; curr_cli != NULL;)
		{
			if (FD_ISSET(curr_cli->fd, &read_fd))
			{
				bzero(&buffer, 4096);
				ssize_t	recv_ret = 0;

				recv_ret = recv(curr_cli->fd, buffer, 4096, 0);
				curr_cli->buf = str_join(curr_cli->buf, buffer);
				if (!curr_cli->buf)
					free_close(&cli_lst);
				if (recv_ret == 0)
				{
					t_client	*quit_cli;

					quit_cli = curr_cli;
					curr_cli = curr_cli->next;
					FD_CLR(quit_cli->fd, &master_fd);
					
					if (quit_cli->buf && strlen(quit_cli->buf) > 0)
					{
						sprintf(buffer, "client %d: %s", quit_cli->id, quit_cli->buf);
						if (send_to_all(write_fd, cli_lst, buffer, quit_cli->id) == -1)
							free_close(&cli_lst);
						bzero(&buffer, 4096);
					}
					sprintf(buffer, "server: client %d just left\n", quit_cli->id);
					if (send_to_all(write_fd, cli_lst, buffer, quit_cli->id) == -1)
						free_close(&cli_lst);
					remove_client(&cli_lst, quit_cli->id);
				}
				else
				{
					ssize_t	ret;
					char	*msg = NULL, *str = NULL;

					while ((ret = extract_message(&curr_cli->buf, &msg)) == 1)
					{
						str = malloc(sizeof(*str) * (100 + strlen(msg)));
						if (!str)
						{
							free(msg);
							free_close(&cli_lst);
						}
						sprintf(str, "client %d: %s", curr_cli->id, msg);
						free(msg);
						if (send_to_all(write_fd, cli_lst, str, curr_cli->id))
							free_close(&cli_lst);
						free(str);
					}
					if (ret == -1)
						free_close(&cli_lst);
					curr_cli = curr_cli->next;
				}
			}
			else
				curr_cli = curr_cli->next;
		}
	}
	return (0);
}
