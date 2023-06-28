#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>

typedef	struct	s_client {
	int					fd;
	int					id;
	char				*buf;
	struct	s_client	*next;
}	t_client;

static	int	add_client(t_client **cli_lst, int fd, int id)
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

static	void	remove_client(t_client **cli_lst, int id)
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

static	int	error(char *msg)
{
	write(2, msg, strlen(msg));
	exit(1);
	return (1);
}

static	int	free_close(t_client **cli_lst)
{
	t_client	*prev;
	t_client	*curr;

	curr = *cli_lst;
	while (curr)
	{
		prev = curr;
		curr = curr->next;
		close(prev->fd);
		free(prev);
	}
	return (error("Fatal error\n"));
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
		return (NULL);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	newbuf[len + strlen(add)] = '\0';
	return (newbuf);
}

static	int	send_to_all(fd_set write_fd, t_client *cli_lst, char *msg, int sender_id)
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
	unsigned	int		len;
	struct sockaddr_in	servaddr, cliaddr;
	fd_set				master_fd, write_fd, read_fd;
	char				buffer[4097];

	int					optval = 1;

	if (argc != 2)
		return (error("Wrong number of arguments\n"));

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
		return (error("Fatal error\n"));
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(2130706433);
	servaddr.sin_port = htons(atoi(argv[1]));

	if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
		return (error("Fatal error\n"));
	if (listen(sockfd, 4096) != 0)
		return (error("Fatal error\n"));

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
				return (free_close(&cli_lst));
			if (add_client(&cli_lst, clifd, id) == -1)
				return (free_close(&cli_lst));
			sprintf(buffer, "server: client %d just arrived\n", id);
			FD_SET(clifd, &master_fd);
			if (send_to_all(write_fd, cli_lst, buffer, id) == -1)
				return (free_close(&cli_lst));
			id++;
			continue;
		}
		else
		{
			for (t_client *tmp_cli = cli_lst; tmp_cli != NULL;)
			{
				if (FD_ISSET(tmp_cli->fd, &read_fd))
				{
					bzero(&buffer, 4096);
					ssize_t	recv_ret = 0;

					recv_ret = recv(tmp_cli->fd, buffer, 4096, 0);
					tmp_cli->buf = str_join(tmp_cli->buf, buffer);
					if (!tmp_cli->buf)
						return (free_close(&cli_lst));
					if (recv_ret == 0)
					{
						t_client	*rm = tmp_cli;
						tmp_cli = tmp_cli->next;
						FD_CLR(rm->fd, &master_fd);

						if (rm->buf && strlen(rm->buf) > 0)
						{
							sprintf(buffer, "client %d: %s", rm->id, rm->buf);
							if (send_to_all(write_fd, cli_lst, buffer, rm->id) == -1)
								return (free_close(&cli_lst));
							bzero(&buffer, 4096);							
						}
						sprintf(buffer, "server: client %d just left\n", rm->id);
						if (send_to_all(write_fd, cli_lst, buffer, rm->id) == -1)
							return (free_close(&cli_lst));
						remove_client(&cli_lst, rm->id);
					}
					else
					{
						ssize_t	ret = 0;
						char	*msg = NULL, *str = NULL;

						while ((ret = extract_message(&tmp_cli->buf, &msg)) == 1)
						{
							if (!(str = malloc(sizeof(*str) * (100 + strlen(msg)))))
							{
								free(msg);
								return (free_close(&cli_lst));
							}
							sprintf(str, "client %d: %s", tmp_cli->id, msg);
							if (send_to_all(write_fd, cli_lst, str, tmp_cli->id) == -1)
								return (free_close(&cli_lst));
							free(msg);
							free(str);
						}
						if (ret == -1)
							free_close(&cli_lst);
						tmp_cli = tmp_cli->next;
					}
				}
				else
					tmp_cli = tmp_cli->next;
			}
		}
	}
}
