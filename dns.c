 /*
  * dns.c
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
#include "packet.h"
#include "host.h"
#include "dns.h"
#define DEBUG
#define BACKLOG 10

#define MAX_FILE_BUFFER 1000
#define MAX_MSG_LENGTH 100
#define MAX_DIR_NAME 100
#define MAX_FILE_NAME 100
#define PKT_PAYLOAD_MAX 100
#define TENMILLISEC 10000   /* 10 millisecond sleep */

struct naming
{
    int id;
    int empty; //if 1 then it is empty, if 0 it is filled
    char name[50];
};

void sigchld_handler2(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr2(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void init_naming_table(struct naming table[255])
{
	int i;
	for(i=0;i<255;i++){
		table[i].empty = 1;
	}
}

int add_naming_table(struct naming table[255], char name[50], int id)
{
    //Find the first empty entry
    for(int i=0; i<255; i++){
	if(table[i].empty==1) //if we're empty add the entry
	{
		for(int j=0; i<50; j++){
			table[i].name[j]=name[j];
		}

		table[i].id=id;
		table[i].empty=0;
		return 1;
	}
     }
	return -1;
}

int find_name_in_table(struct naming table[255], char name[50])
{
    //return the port number of a host we are looking for
    //returns -1 if the host is not defined on a port
	int i;
    for(i = 0; i < 255; i++){
        if(table[i].name==name && table[i].empty==0)
            return table[i].id;
    }
    return -1;
}
void print_ntable(struct naming table[255])
{
	int i;
	for(i = 0; i < 255; i++){
		if(table[i].empty==0)
		printf("name: %s, hostid: %d\n", table[i].name, table[i].id);
	}
}

/*
 *  Main
 */

void dns_main(int host_id)
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
char dns[50];
char name[MAX_FILE_NAME];
char string[PKT_PAYLOAD_MAX+1];

FILE *fp;

struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;

struct net_port *p;
struct host_job *new_job;
struct host_job *new_job2;

struct job_queue job_q;

/* naming table  */
struct naming n_table[255];
init_naming_table(n_table);
int n_table_length = 0;

/*
 * Initialize pipes
 * Get link port to the manager
 */



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
int packet_dest;



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
for (k = 0; k < node_port_num; k++)
{
	p = node_port[k]; 
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

		sa.sa_handler = sigchld_handler2; // reap all dead processes
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
}



while(1)
{

	/*
	 * Get packets from incoming links and translate to jobs
   * Put jobs in job queue
 	 */


	for (k = 0; k < node_port_num; k++)
  	{ /* Scan all ports */
	 	 n = 0 ;
		in_packet = (struct packet *) malloc(sizeof(struct packet));
		if(node_port[k]->type ==PIPE)
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


					char msg[100+4];
					int d;

					n = recv(new_fd, msg, 100+4, 0);

         			 printf("DNS SERVER RECEIEVED:    %d\n", n);

					if(n>0)
					{


						in_packet->src = (char) msg[0]; 
						in_packet->dst = (char) msg[1];
						in_packet->type = (char) msg[2];
		
						in_packet->length = (int) msg[3];
						for (d=0; d < in_packet->length; d++)
						{
							in_packet->payload[d] = msg[d+4];
						}
						#ifdef DEBUG
							printf("packet contents: src id %d\n", (int) in_packet->src);
							printf("packet contents: dst id %d\n", (int) in_packet->dst);
							printf("packet contents: length %d\n", (int) in_packet->length);
						#endif
					}
					close(new_fd);
			}
			else
			{}   //printf("No data within five seconds.\n");

		// -------------------------											!!!!!!!!!!!!!!!!!!!!!!!!!

		}

		if((n>0) && ((int) in_packet->dst == host_id))
		{
			n=0;
			new_job = (struct host_job *)
					malloc(sizeof(struct host_job));
			new_job->in_port_index = k;
			new_job->packet = in_packet;
			switch(in_packet->type)
			{
				case (char) PKT_REG_REQ:
				new_job->type = JOB_REG_WAIT_FOR_REPLY;
				job_q_add(&job_q, new_job);
				#ifdef DEBUG
				printf("DNS SERVER received PKT_REG_REQ\n");
				#endif
				break;
				
				case (char) PKT_FIND_REQ:
				new_job->type = JOB_FIND_WAIT_FOR_REPLY;
				job_q_add(&job_q, new_job);
				break;
				
				default:
					if(new_job!=NULL){
						free(new_job);
						new_job=NULL;
					}
					break;
			}
		}
		else {
			if(in_packet!=NULL){
				free(in_packet);
				in_packet = NULL;
			}
		}
	}
				
	/*
 	 * Execute one job in the job queue
 	 */
	  
	if (job_q_num(&job_q) > 0) {

		/* Get a new job from the job queue */
		new_job = job_q_remove(&job_q);

		switch(new_job->type) {
			case JOB_REG_WAIT_FOR_REPLY:
				#ifdef DEBUG
				printf("DNS SERVER working on JOB_REG_WAIT_FOR_REPLY\n");
				#endif
				for(i=0;new_job->packet->payload[i]!='\0';i++)
				{
					dns[i] = new_job->packet->payload[i];
					printf("\n\n%c\n\n",dns[i]);
				}
				i=add_naming_table(n_table, dns, (int) new_job->packet->src);
				printf("got here");
				print_ntable(n_table);
				//send packet back to host that registered name
				if(i!=-1) new_job->packet->payload[0]=i;
                                else new_job->packet->payload[0]=-1;
				packet_send(node_port[0], new_job->packet);
    				if(new_job->packet != NULL){
					free(new_job->packet);
					new_job->packet = NULL;
				}
				if(new_job != NULL){
					free(new_job);
					new_job = NULL;
				}
			break;
			
			case JOB_FIND_WAIT_FOR_REPLY:
				for(i=0;new_job->packet->payload[i] != '\0'; i++)
				{
					dns[i] = new_job->packet->payload[i];
				}
				i = find_name_in_table(n_table, dns);
				//send packet back to host with the host id
				if(i!=-1) new_job->packet->payload[0]=i;
				else new_job->packet->payload[0]=-1;
				packet_send(node_port[0], new_job->packet);
				
				if(new_job->packet != NULL){
                                        free(new_job->packet);
                                        new_job->packet = NULL;
                                }
                                if(new_job != NULL){
                                        free(new_job);
                                        new_job = NULL;
                                }
			break;		

	}
}




	/* The host goes to sleep for 10 ms */
	usleep(TENMILLISEC);

} /* End of while loop */

}
