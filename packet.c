
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "main.h"
#include "packet.h"
#include "net.h"
#include "host.h"
#define MAX 100

int sockfd, numbytes;
char buf[MAX];
struct addrinfo hints, *servinfo, *ps;
int rv;
char s[INET6_ADDRSTRLEN];

void packet_send(struct net_port *port, struct packet *p)
{
		char msg[PAYLOAD_MAX+4];
		int i;

		if (port->type == PIPE)
		{
				msg[0] = (char) p->src;
				msg[1] = (char) p->dst;
				msg[2] = (char) p->type;
				msg[3] = (char) p->length;

				for (i=0; i<p->length; i++)
				{
					msg[i+4] = p->payload[i];
				}
				write(port->pipe_send_fd, msg, p->length+4);
		}

		if(port->type == SOCKET)
		{
				printf("PACKET_SEND SOCKET\n");

				// FILL MESSAGE
				msg[0] = (char) p->src;
				msg[1] = (char) p->dst;
				msg[2] = (char) p->type;
				msg[3] = (char) p->length;
				for (i=0; i < p->length; i++)
				{
					msg[i+4] = p->payload[i];
				}

				// CONNECT TO SERVER
				memset(&hints, 0, sizeof hints);
				hints.ai_family = AF_UNSPEC;
				hints.ai_socktype = SOCK_STREAM;

				char sport2[port->domain2size];
				snprintf (sport2, sizeof(sport2), "%d", port->port2);
				printf("PACKET SENDING Port: %s\n", sport2);

				if ((rv = getaddrinfo(port->domain2, sport2, &hints, &servinfo)) != 0)
				{
					fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
					return;
				}

				// loop through all the results and connect to the first we can
				for(ps = servinfo; ps != NULL; ps = ps->ai_next)
				{
						if ((sockfd = socket(ps->ai_family, ps->ai_socktype, ps->ai_protocol)) == -1)
						{
							perror("client: socket");
							continue;
						}
						if (connect(sockfd, ps->ai_addr, ps->ai_addrlen) == -1)
						{
							close(sockfd);
							perror("client: connect");
							continue;
						}
						break;
				}
				if (ps == NULL)
				{
						fprintf(stderr, "client: failed to connect\n");
						return;
				}

				inet_ntop(ps->ai_family, get_in_addr((struct sockaddr *)ps->ai_addr), s, sizeof s);
				freeaddrinfo(servinfo); 		// all done with this structure

				// SEND MESSAGE ON SERVER
				if(send(sockfd, msg, MAX+4, 0) ==-1)

				// if (send(sockfd, "Hello, world!", 13, 0) == -1)
				{ perror("send"); }

				printf("			Sending message with packet.c\n");
				memset(msg, 0,MAX+4);
				// for(i = 0; i < MAX; i++){
				// 	printf("%c", msg[i]);
				// }

				close(sockfd);

		}
		return;
}

int packet_recv(struct net_port *port, struct packet *p)
{
		char msg[MAX+4];
		int n;
		int i;

		if (port->type == PIPE)
		{
			n = read(port->pipe_recv_fd, msg, MAX+4);
			if (n>0)
			{
				p->src = (char) msg[0];
				p->dst = (char) msg[1];
				p->type = (char) msg[2];
				p->length = (int) msg[3];
				for (i=0; i<p->length; i++)
				{
					p->payload[i] = msg[i+4];
				}
			}
		}

		return(n);
}
