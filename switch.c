 /*
  * switch.c
 */

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
#include "net.h"
#include "man.h"
#include "host.h"
#include "packet.h"
#include "switch.h"
#define DEBUG
#define BACKLOG 10

#define MAX_FILE_BUFFER 1000
#define MAX_MSG_LENGTH 100
#define MAX_DIR_NAME 100
#define MAX_FILE_NAME 100
#define PKT_PAYLOAD_MAX 100
#define TENMILLISEC 10000   /* 10 millisecond sleep */

struct forwarding
{
    int valid;
    int dest_id;
    int port_id;
};

void sigchld_handler1(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr1(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void init_forwarding_table(struct forwarding table[100])
{
    for(int i = 0; i < 100; i++){
        table[i].port_id = i;
        table[i].valid = 0;   // initialize all invalid
        table[i].dest_id = -1; //set dest_id = -1 for undefined
    }
}
int get_host_at_port(struct forwarding table[100], int port_id)
{
    if(table[port_id].valid == 1) return table[port_id].dest_id;
    else return -1;
}
int set_src_at_port(struct forwarding table[100], int host_id, int port_id)
{
    //when we find a destination and a port, add it to the table
    table[port_id].dest_id = host_id;
    table[port_id].valid = 1;
}
int find_host_in_table(struct forwarding table[100], int host_id)
{
    //return the port number of a host we are looking for
    //returns -1 if the host is not defined on a port
    for(int i = 0; i < 100; i++){
        if(table[i].dest_id==host_id && table[i].valid==1)
            return i;
    }
    return -1;
}
void print_ftable(struct forwarding table[100])
{
	for(int i = 0; i < 100; i++){
		if(table[i].valid ==1)
		printf("port: %d, hostid: %d\n", i, table[i].dest_id);
	}
}

/*
 *  Main
 */

void switch_main(int host_id)
{

/* State */
char dir[MAX_DIR_NAME];
int dir_valid = 0;

char man_msg[MAN_MSG_LENGTH];
char man_reply_msg[MAN_MSG_LENGTH];
char man_cmd;
struct man_port_at_host *man_port;  // Port to the manage

struct net_port *node_port_list;
struct net_port **node_port;  // Array of pointers to node ports
int node_port_num;            // Number of node ports

int ping_reply_received;

int i, k, n;
int dst;
char name[MAX_FILE_NAME];
char string[PKT_PAYLOAD_MAX+1];

FILE *fp;

struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;

struct net_port *p;
struct host_job *new_job;
struct host_job *new_job2;

struct job_queue job_q;

/* forwarding table  */
struct forwarding f_table[100];
init_forwarding_table(f_table);
int f_table_length = 0;

/*
 * Initialize pipes
 * Get link port to the manager
 */

man_port = net_get_host_port(host_id);


/* Create an array node_port[ ] to store the network link ports
 * at the switch.  The number of ports is node_port_num
 */
node_port_list = net_get_port_list(host_id);

//	  Count the number of network link ports
node_port_num = 0;
for(p=node_port_list; p!=NULL; p=p->next)
{
	node_port_num++;
}
//	 Create memory space for the array
node_port = (struct net_port **) malloc(node_port_num*sizeof(struct net_port *));

//	Load ports into the array
p = node_port_list;
for (k = 0; k < node_port_num; k++)
{
	node_port[k] = p;
	p = p->next;
}

/* Initialize the job queue */
job_q_init(&job_q);
char packet_dest;



// SOCKET INFO FOR SETUP
int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
struct addrinfo hints, *servinfo, *ps;
struct sockaddr_storage their_addr; // connector's address information
struct sigaction sa;
int yes=1;
int sin_size;
char s[INET6_ADDRSTRLEN];
int rv;

int flag_socket = 0;
p = node_port_list;
for (k = 0; k < node_port_num; k++)
{
	if(p->type == SOCKET)
	{
		// printf("It's a SOCCCCCCCKET\n");
		// printf("NODE ID: 	%d\n", p->pipe_host_id);
		// printf("NODE ID: 	%d\n", p->port1);
		// printf("NODE ID: 	%s\n", p->domain1);
		// printf("NODE ID: 	%d\n", p->port2);
		// printf("NODE ID: 	%s\n", p->domain2);
		flag_socket = 1;

		// -------------------------------- SERVER SETUP DONE

		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE; // use my IP

		char sport1[p->domain1size];
		snprintf (sport1, sizeof(sport1), "%d", p->port1);
		printf("SWITCH Port: %s\n", sport1);

		if ((rv = getaddrinfo(p->domain1, sport1, &hints, &servinfo)) != 0)
	  {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
			return;
		}

		// loop through all the results and bind to the first we can
		for(ps = servinfo; ps != NULL; ps = ps->ai_next)
	  {
	    // Create a socket
			if ((sockfd = socket(ps->ai_family, ps->ai_socktype, ps->ai_protocol)) == -1)
	    {
				perror("server: socket");
				continue;
			}

      fcntl(sockfd, F_SETFL, O_NONBLOCK); 		// Change the socket into non-blocking state

			if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
	    {
				perror("setsockopt");
				exit(1);
			}

	    // Bind socket
			if (bind(sockfd, ps->ai_addr, ps->ai_addrlen) == -1)
	    {
				close(sockfd);
				perror("server: bind");
				continue;
			}
			break;
		}

		if (ps == NULL)
	  {
			fprintf(stderr, "server: failed to bind\n");
			return;
		}

		freeaddrinfo(servinfo); // all done with this structure

		// LISTEN
		if (listen(sockfd, BACKLOG) == -1)
	  {
			perror("listen");
			exit(1);
		}

		sa.sa_handler = sigchld_handler1; // reap all dead processes
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		if (sigaction(SIGCHLD, &sa, NULL) == -1)
	  {
			perror("sigaction");
			exit(1);
		}

		//printf("SERVER 		SET 		UP 		DONE!!\n");

		// -------------------------------- SERVER SETUP DONE

	}
	p = p->next;
}



while(1)
{

	/*
	 * Get packets from incoming links and translate to jobs
   * Put jobs in job queue
 	 */


	for (k = 0; k < node_port_num; k++)
  	{ /* Scan all ports */
		in_packet = (struct packet *) malloc(sizeof(struct packet));
		n = packet_recv(node_port[k], in_packet);

    if(flag_socket && node_port[k]->type == SOCKET)
		{
			// -------------------------											!!!!!!!!!!!!!!!!!!!!!!!!!
			sin_size = sizeof their_addr;

	    fd_set rfds;
	    struct timeval tv;
	    int retval;

	    /* Watch connections to see when it has input. */
	    FD_ZERO(&rfds);
	    FD_SET(sockfd, &rfds);
	    /* Wait up to five seconds. */
	    tv.tv_sec = 0.5;
	    tv.tv_usec = 0;
	    retval = select(sockfd+1, &rfds, NULL, NULL, &tv);
	    /* Don’t rely on the value of tv now! */

	    if (retval == -1)
	        perror("select()");

	    else if (retval)
	    {
					new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
					if (new_fd == -1)
					{
						perror("accept");
						continue;
					}
          fcntl(new_fd, F_SETFL, O_NONBLOCK); 		// Change the socket into non-blocking state


					inet_ntop(their_addr.ss_family, get_in_addr1((struct sockaddr *)&their_addr), s, sizeof s);
					//printf("SWITCH SERVER: got connection from %s\n", s);

          // if(send(new_fd, "Hello, world!", 13, 0) == -1)
          // { perror("send"); }

					char msg[100+4];
					int d;

					n = recv(new_fd, msg, 100-1, 0);

          // printf("RECEIEVED:    %d\n", n);

					if(n>0)
					{
            // for(d=0; d < n; d++)
            // {
            //   printf("%c", msg[d]);
            // }

						in_packet->src = (char) msg[0];
						in_packet->dst = (char) msg[1];
						in_packet->type = (char) msg[2];
						in_packet->length = (int) msg[3];
						for (d=0; d < in_packet->length; d++)
						{
							in_packet->payload[d] = msg[d+4];
						}
					}
			}
	    else
	    {}   //printf("No data within five seconds.\n");

		// -------------------------											!!!!!!!!!!!!!!!!!!!!!!!!!

		}

		if(n>0)
    {
			#ifdef DEBUG
				printf("switch:packet received from port %d of %d for host %d \n", k, node_port_num, in_packet->dst);
			#endif
			new_job = (struct host_job *)
				malloc(sizeof(struct host_job));
			new_job->in_port_index = k;
			new_job->packet = in_packet;

			if(get_host_at_port(f_table, k)==-1){
				set_src_at_port(f_table, (int) in_packet->src , k);
					f_table_length++;
			#ifdef DEBUG
				printf("switch:adding to forwarding table id: %d at port %d\n", in_packet->src, k);
			#endif
			}

			job_q_add(&job_q, new_job);
		}
		else {
			if(in_packet!= NULL){
				free(in_packet);
				in_packet=NULL;
			}

		}


	/*
 	 * Execute one job in the job queue
 	 */

	if (job_q_num(&job_q) > 0) {

		/* Get a new job from the job queue */
		new_job = job_q_remove(&job_q);

		// int i=0;
		packet_dest = (int)new_job->packet->dst;
		#ifdef DEBUG
		printf("switch: forwarding table\n");
		print_ftable(f_table);
		#endif

		if(find_host_in_table(f_table, packet_dest)==-1)
    {
			//host not in table
			//send to all ports except the received port

			for (k=0; k<node_port_num; k++)
      {
				if(k != new_job->in_port_index){
					#ifdef DEBUG
					printf("switch:sending packet on port %d of %d\n", k, node_port_num);
					#endif
					packet_send(node_port[k], new_job->packet);
				}
			}
		}
		else {
			#ifdef DEBUG
			printf("switch: sending packet to host %d on port %d \n", packet_dest, find_host_in_table(f_table, packet_dest));
			#endif
			packet_send(node_port[find_host_in_table(f_table, packet_dest)], new_job->packet);
		}

    if(new_job->packet != NULL){
			free(new_job->packet);
			new_job->packet = NULL;
		}
		if(new_job != NULL){
			free(new_job);
			new_job = NULL;
		}

	}





	/* The host goes to sleep for 10 ms */
	usleep(TENMILLISEC);

} /* End of while loop */

}
}
