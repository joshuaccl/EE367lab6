 /*
  * host.c
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

#define DEBUG

#define MAX_FILE_BUFFER 1000
#define MAX_MSG_LENGTH 100
#define MAX_DIR_NAME 100
#define MAX_FILE_NAME 100
#define PKT_PAYLOAD_MAX 100
#define TENMILLISEC 10000   /* 10 millisecond sleep */
#define BACKLOG 10

/* Types of packets */

struct file_buf
{
	char name[MAX_FILE_NAME];
	int name_length;
	char buffer[MAX_FILE_BUFFER+1];
	int head;
	int tail;
	int occ;
	FILE *fd;
};


void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


/*
 * File buffer operations
 */

/* Initialize file buffer data structure */
void file_buf_init(struct file_buf *f)
{
	f->head = 0;
	f->tail = 0;
	f->occ = 0;
	f->name_length = 0;
}

/*
 * Get the file name in the file buffer and store it in name
 * Terminate the string in name with tne null character
 */

void file_buf_get_name(struct file_buf *f, char name[])
{
	int i;

	for (i=0; i<f->name_length; i++)
	{
		name[i] = f->name[i];
	}
	name[f->name_length] = '\0';
}

/*
 *  Put name[] into the file name in the file buffer
 *  length = the length of name[]
 */
void file_buf_put_name(struct file_buf *f, char name[], int length)
{
	int i;
	for (i=0; i<length; i++)
	{
		f->name[i] = name[i];
	}
	f->name_length = length;
}

/*
 *  Add 'length' bytes n string[] to the file buffer
 */
int file_buf_add(struct file_buf *f, char string[], int length)
{
	
	int i = 0;
	while (i < length && f->occ < MAX_FILE_BUFFER)
	{

		
		f->buffer[f->occ] = string[i];
		f->tail = f->tail +1;
		i++;
	  f->occ++;
	}
	f->buffer[f->occ] = '\0';
	return(i);
}

/*
 *  Remove bytes from the file buffer and store it in string[]
 *  The number of bytes is length.
 */
int file_buf_remove(struct file_buf *f, char string[], int length)
{
	int i = 0;

	while (i < length && f->occ > 0)
	{
		string[i] = f->buffer[f->head];
		f->head = (f->head + 1) % (MAX_FILE_BUFFER + 1);
		i++;
    f->occ--;
	}
	return(i);
}


/*
 * Operations with the manager
 */
int get_man_command(struct man_port_at_host *port, char msg[], char *c)
{
	int n;
	int i;
	int k;

	n = read(port->recv_fd, msg, MAN_MSG_LENGTH); /* Get command from manager */
	if (n>0)
	{  /* Remove the first char from "msg" */
		for (i=0; msg[i]==' ' && i<n; i++);
		*c = msg[i];
		i++;
		for (; msg[i]==' ' && i<n; i++);
		for (k=0; k+i<n; k++)
		{
			msg[k] = msg[k+i];
		}
		msg[k] = '\0';
	}
	return n;
}

/*
 * Operations requested by the manager
 */

/* Send back state of the host to the manager as a text message */
void reply_display_host_state(
		struct man_port_at_host *port,
		char dir[],
		int dir_valid,
		int host_id)
{
	int n;
	char reply_msg[MAX_MSG_LENGTH];

	if (dir_valid == 1)
	{
		n = sprintf(reply_msg, "%s %d", dir, host_id);
	}
	else
	{
		n = sprintf(reply_msg, "None %d", host_id);
	}
	write(port->send_fd, reply_msg, n);
}



/* Job queue operations */

/* Add a job to the job queue */
void job_q_add(struct job_queue *j_q, struct host_job *j)
{
	if (j_q->head == NULL )
	{
		j_q->head = j;
		j_q->tail = j;
		j_q->occ = 1;
		j->next = NULL;
	}
	else {
		(j_q->tail)->next = j;
		j->next = NULL;
		j_q->tail = j;
		j_q->occ++;
	}
}

/* Remove job from the job queue, and return pointer to the job*/
struct host_job *job_q_remove(struct job_queue *j_q)
{
	struct host_job *j;

	if (j_q->occ == 0) return(NULL);
	j = j_q->head;
	j_q->head = (j_q->head)->next;
	j_q->occ--;
	return(j);
}

/* Initialize job queue */
void job_q_init(struct job_queue *j_q)
{
	j_q->occ = 0;
	j_q->head = NULL;
	j_q->tail = NULL;
}

int job_q_num(struct job_queue *j_q)
{
	return j_q->occ;
}

int job_q_len(struct job_queue *j_q)
{
	int i = 0;
	struct host_job *j;
	j = j_q->head;
	while( j != NULL)
	{
		j = j->next;
		i++;
	}
	return i;
}

/*
 *  Main
 */

void host_main(int host_id)
{

/* State */
char dir[MAX_DIR_NAME];
int dir_valid = 0;

char man_msg[MAN_MSG_LENGTH];
char man_reply_msg[MAN_MSG_LENGTH];
char man_cmd;
struct man_port_at_host *man_port;  // Port to the manager

struct net_port *node_port_list;
struct net_port **node_port;  // Array of pointers to node ports
int node_port_num;            // Number of node ports

int ping_reply_received;

int i, k, n;
int dst;
char name[MAX_FILE_NAME];
char string[PKT_PAYLOAD_MAX+1];
char buf[MAX_FILE_BUFFER];

FILE *fp;

struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;
struct packet *new_packet2;


struct net_port *p;
struct host_job *new_job;
struct host_job *new_job2;
struct host_job *new_job3;

struct job_queue job_q;

struct file_buf f_buf_upload;
struct file_buf f_buf_download;

file_buf_init(&f_buf_upload);
file_buf_init(&f_buf_download);

/*
 * Initialize pipes
 * Get link port to the manager
 */

man_port = net_get_host_port(host_id);

/*
 * Create an array node_port[ ] to store the network link ports
 * at the host.  The number of ports is node_port_num
 */

node_port_list = net_get_port_list(host_id);

/*  Count the number of network link ports */
node_port_num = 0;
for (p=node_port_list; p!=NULL; p=p->next)
{
	node_port_num++;
}
	/* Create memory space for the array */
node_port = (struct net_port **) malloc(node_port_num*sizeof(struct net_port *));

/* Load ports into the array */
p = node_port_list;
for (k = 0; k < node_port_num; k++)
{
	node_port[k] = p;
	p = p->next;
}

/* Initialize the job queue */
job_q_init(&job_q);


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
		printf("HOST Port: %s\n", sport1);

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

		sa.sa_handler = sigchld_handler; // reap all dead processes
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
/* initialize tree variables */
int local_root_id = host_id;
int local_root_dist = 0;
int local_parent = -1;
int local_port_tree[node_port_num];
//initialize local_port_tree
for(i = 0; i < node_port_num; i++){
	local_port_tree[i]=1;
}
int send_tree_pkt = 0;
while(1)
{
	/* Execute command from manager, if any */
		/* Get command from manager */
	n = get_man_command(man_port, man_msg, &man_cmd);

		/* Execute command */
	if (n>0)
	{
		switch(man_cmd)
		{
			case 's':
				reply_display_host_state(man_port,
					dir,
					dir_valid,
					host_id);
				break;

			case 'm':
				dir_valid = 1;
				for (i=0; man_msg[i] != '\0'; i++) {
					dir[i] = man_msg[i];
				}
				dir[i] = man_msg[i];
				break;

			case 'p': // Sending ping request
				// Create new ping request packet
				sscanf(man_msg, "%d", &dst);
				new_packet = (struct packet *)
						malloc(sizeof(struct packet));
				new_packet->src = (char) host_id;
				new_packet->dst = (char) dst;
				new_packet->type = (char) PKT_PING_REQ;
				new_packet->length = 0;
				new_job = (struct host_job *)
						malloc(sizeof(struct host_job));
				new_job->packet = new_packet;
				new_job->type = JOB_SEND_PKT_ALL_PORTS;
				job_q_add(&job_q, new_job);

				new_job2 = (struct host_job *)
						malloc(sizeof(struct host_job));
				ping_reply_received = 0;
				new_job2->type = JOB_PING_WAIT_FOR_REPLY;
				new_job2->ping_timer = 10;
				new_job2->packet = NULL;
				job_q_add(&job_q, new_job2);

				break;

			case 'u': /* Upload a file to a host */
				sscanf(man_msg, "%d %s", &dst, name);
				new_job = (struct host_job *)
						malloc(sizeof(struct host_job));
				new_job->type = JOB_FILE_UPLOAD_SEND;
				new_job->file_upload_dst = dst;
				for (i=0; name[i] != '\0'; i++) {
					new_job->fname_upload[i] = name[i];
				}
				new_job->fname_upload[i] = '\0';
				new_job->packet= NULL;
				job_q_add(&job_q, new_job);

				break;
			case 'd':
				sscanf(man_msg, "%d %s", &dst, name);
				new_packet = (struct packet *)
						malloc(sizeof(struct packet));
				new_packet->src = (char) host_id;
				new_packet->dst = (char) dst;
				new_packet->type = (char) PKT_UPLOAD_REQ;

				for (i=0; name[i] != '\0'; i++) {
					new_packet->payload[i] = name[i];
				}
				new_packet->payload[i] = '\0';
				new_packet->length = i;
				new_job = (struct host_job *)
						malloc(sizeof(struct host_job));
				new_job->type = JOB_SEND_PKT_ALL_PORTS;
				new_job->packet = new_packet;
				job_q_add(&job_q, new_job);

				break;
		}
	}

	/*
	 * Get packets from incoming links and translate to jobs
   * Put jobs in job queue
 	 */

	for (k = 0; k < node_port_num; k++)
	{ /* Scan all ports */
	n = 0;
		in_packet = (struct packet *) malloc(sizeof(struct packet));
		if(node_port[k]->type ==PIPE){
			n = packet_recv(node_port[k], in_packet);
			#ifdef DEBUG
			// if(n>0){
			// 	printf("Packet contents:  \n");
			// 	for(i = 0; i < in_packet->length; i++){
			// 		printf("%c", in_packet->payload[i]);
			// 	}
			// }
			#endif

		}
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


					inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);

					char msg[100+4];
					int d;


					n = recv(new_fd, msg, 100+4, 0);
         	 		printf("RECEIEVED:    %d\n", n);

					if(n>0)
					{

						in_packet->src = (char) msg[0];
						in_packet->dst = (char) msg[1];
						in_packet->type = (char) msg[2];
						in_packet->length = (int) msg[3];
						for (d=0; d<in_packet->length; d++)
						{
							in_packet->payload[d] = msg[d+4];
						}
						#ifdef DEBUG
							printf("packet contents: src id %d\n", (int) in_packet->src);
							printf("packet contents: dst id %d\n", (int) in_packet->dst);
							printf("packet contents: length %d\n", in_packet->length);
							printf("packet payload: \n %s \n", in_packet->payload);
						#endif
					}
					close(new_fd);

			}
			else
			{}    //printf("No data within five seconds.\n");

		// -------------------------											!!!!!!!!!!!!!!!!!!!!!!!!!

		}
		if ((n > 0) && (in_packet->type == (char)PKT_TREE)){
				/* RECEIVE TREE PACKET */
				// payload[0] packetRootID
				// payload[1] packetRootDist
				// payload[2] packetSenderType
				// payload[3] packetSenderChild
				if(in_packet->payload[2]=='S'){
						local_root_id =	(int)in_packet->payload[0];
						local_parent = k;
						local_root_dist = (int)in_packet->payload[1] +1;
						local_port_tree[k] = 1;
				}
				else local_port_tree[k]=0;

			if(in_packet!= NULL){
					free(in_packet);
					in_packet = NULL;
				}
		}
		else if ((n > 0) && ((int) in_packet->dst == host_id))
		{
			n = 0;
			#ifdef DEBUG
			printf("host %d:packet received from port %d of %d\n", host_id, k, node_port_num);
			#endif
			new_job = (struct host_job *)
				malloc(sizeof(struct host_job));
			new_job->in_port_index = k;
			new_job->packet = in_packet;

			switch(in_packet->type)
			{
				/* Consider the packet type */

				/*
				 * The next two packet types are
				 * the ping request and ping reply
				 */
				case (char) PKT_PING_REQ:
					#ifdef DEBUG
						printf("host %d: received- PKT_PING_RQ \n", host_id);
					#endif

					new_job->type = JOB_PING_SEND_REPLY;
					job_q_add(&job_q, new_job);
					#ifdef DEBUG
						printf("host %d: received- PKT_PING_RQ -- added job to q %d\n", host_id, job_q.occ);
					#endif
					break;

				case (char) PKT_PING_REPLY:
					#ifdef DEBUG
						printf("host %d: received- PKT_PING_REPLY \n", host_id);
					#endif
					ping_reply_received = 1;
					if(in_packet != NULL){
						free(in_packet);

						in_packet=NULL;
					}
					if(new_job!= NULL){
						free(new_job);
						new_job=NULL;
					}
					break;

				/*
				 * The next two packet types
				 * are for the upload file operation.
				 *
				 * The first type is the start packet
				 * which includes the file name in
				 * the payload.
				 *
				 * The second type is the end packet
				 * which carries the content of the file
				 * in its payload
				 */

				case (char) PKT_FILE_UPLOAD_START:
					#ifdef DEBUG
						printf("host %d: received- PKT_FILE_UPLOAD_START \n", host_id);
					#endif
					new_job->type
						= JOB_FILE_UPLOAD_RECV_START;
					job_q_add(&job_q, new_job);
					break;

				case (char) PKT_FILE_UPLOAD_END:
					new_job->type
						= JOB_FILE_UPLOAD_RECV_END;
					job_q_add(&job_q, new_job);
					#ifdef DEBUG
					printf("host %d: received- PKT_FILE_UPLOAD_END \n", host_id);
					#endif
					break;
				case (char) PKT_UPLOAD_REQ:
					#ifdef DEBUG
					printf("host %d: received- PKT_UPLOAD_REQ \n", host_id);
					#endif

					sscanf(man_msg, "%d %s", &dst, name);
					new_job = (struct host_job *)
							malloc(sizeof(struct host_job));
					new_job->type = JOB_FILE_UPLOAD_SEND;
					new_job->file_upload_dst = (int) in_packet->src; //KASEY
						for (i=0; i < in_packet->length; i++) {
							new_job->fname_upload[i] = in_packet->payload[i];
						}
					new_job->fname_upload[i] = '\0';
					new_job->packet= NULL;
					#ifdef DEBUG
					printf("host %d: filename -%s- to src: %d \n", host_id, new_job->fname_upload,
						(int)in_packet->src);
					#endif
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
			if(send_tree_pkt > 10){
			
				new_job = (struct host_job *)
					malloc(sizeof(struct host_job));
				new_job->type = JOB_SEND_TREE_PKT;	
				new_job->packet = in_packet;
				
				job_q_add(&job_q, new_job);
				send_tree_pkt = 0;

			}
			else if(in_packet!= NULL){
				free(in_packet);
				in_packet=NULL;
			}

		}
	}

	/*
 	 * Execute one job in the job queue
 	 */

	if (job_q_num(&job_q) > 0) {

		/* Get a new job from the job queue */

		new_job = job_q_remove(&job_q);

		/* Send packet on all ports */
		switch(new_job->type) {

			/* Send packets on all ports */
			case JOB_SEND_PKT_ALL_PORTS:
				//this actually sends packets on only tree ports 
				for (k=0; k<node_port_num; k++) {
					if(local_port_tree[k]==1){
						#ifdef DEBUG
						printf("host %d: sending packet to port %d of %d\n",host_id, k, node_port_num);
						#endif
						packet_send(node_port[k], new_job->packet);
					}
				}
				if(new_job->packet != NULL){
					free(new_job->packet);
					new_job->packet = NULL;
				}
				if(new_job != NULL){
					free(new_job);
					new_job = NULL;
				}

				break;
			case JOB_SEND_TREE_PKT:
			//tree packets go on all ports
				for (k=0; k<node_port_num; k++) {
						new_job->packet->src = (char) host_id;
						new_job->packet->dst = (char) 0;
						new_job->packet->type = (char) PKT_TREE;
						new_job->packet->length = 4;
						new_job->packet->payload[0] = (char) local_root_id;
						new_job->packet->payload[1] = (char) local_root_dist;
						new_job->packet->payload[2] = 'H'; //packetSenderType
						new_job->packet->payload[3] = 'Y'; //packetSenderChild
						#ifdef DEBUGTREE
							printf("host %d: local root %d  \n", host_id, local_root_id);
						#endif
						packet_send(node_port[k], new_job->packet);
				}
				if(new_job->packet != NULL){
					free(new_job->packet);
					new_job->packet = NULL;
				}
				if(new_job != NULL){
					free(new_job);
					new_job = NULL;
				}
				break;

			/* The next three jobs deal with the pinging process */
			case JOB_PING_SEND_REPLY:
			#ifdef DEBUG
			printf("host %d: JOB_PING_SEND_REPLY \n", host_id);

			#endif
				/* Send a ping reply packet */

				/* Create ping reply packet */
				new_packet = (struct packet *)
					malloc(sizeof(struct packet));
				new_packet->dst = (char) new_job->packet->src; //KASEY
				new_packet->src = (char) host_id;
				new_packet->type = PKT_PING_REPLY;
				new_packet->length = 0;

				/* Create job for the ping reply */
				new_job2 = (struct host_job *)
					malloc(sizeof(struct host_job));
				new_job2->type = JOB_SEND_PKT_ALL_PORTS;
				new_job2->packet = new_packet;

				/* Enter job in the job queue */
				job_q_add(&job_q, new_job2);

				/* Free old packet and job memory space */
				if(new_job->packet != NULL){
					free(new_job->packet);
					new_job->packet = NULL;
				}
				if(new_job != NULL){
					free(new_job);
					new_job = NULL;
				}

				break;

			case JOB_PING_WAIT_FOR_REPLY:
			#ifdef DEBUG
			printf("host %d:JOB_PING_WAIT_FOR_REPLY \n", host_id);

			#endif
				/* Wait for a ping reply packet */

				if (ping_reply_received == 1) {
					n = sprintf(man_reply_msg, "Ping acked!");

					write(man_port->send_fd, man_reply_msg, n);
					if(new_job->packet != NULL){
						free(new_job->packet);
						new_job->packet = NULL;
					}
					if(new_job != NULL){
						free(new_job);
						new_job = NULL;
					}
				}
				else if (new_job->ping_timer > 1) {
					new_job->ping_timer--;
					job_q_add(&job_q, new_job);
				}
				else { /* Time out */
					n = sprintf(man_reply_msg, "Ping time out!");

					write(man_port->send_fd, man_reply_msg, n);
					if(new_job->packet != NULL){
						free(new_job->packet);
						new_job->packet = NULL;
					}
					if(new_job != NULL){
						free(new_job);
						new_job = NULL;
					}
				}

				break;


		/* The next three jobs deal with uploading a file */

				/* This job is for the sending host */
			case JOB_FILE_UPLOAD_SEND:
			#ifdef DEBUG
			printf("host %d: JOB_FILE_UPLOAD_SEND \n", host_id);
			#endif
				/* Open file */
				if (dir_valid == 1) {
					n = sprintf(name, "./%s/%s",
						dir, new_job->fname_upload);
						printf("length: %d\n", n);
					name[n] = '\0';
					fp = fopen(name, "r");
					if (fp != NULL) {
						#ifdef DEBUG
						printf("host %d: JOB_FILE_UPLOAD_SEND p2 \n", host_id);
						#endif
							/*
						* Create first packet which
						* has the file name
						*/

						new_packet = (struct packet *)
							malloc(sizeof(struct packet));
						new_packet->dst
							= (char) new_job->file_upload_dst;
						new_packet->src = (char) host_id;
						new_packet->type
							= (char) PKT_FILE_UPLOAD_START;
						for (i=0;
							new_job->fname_upload[i]!= '\0';
							i++) {
							new_packet->payload[i] =
								new_job->fname_upload[i];
						}
						new_packet->length = i;
						new_packet->payload[i]='\0';

						new_job2 = (struct host_job *)
							malloc(sizeof(struct host_job));
						new_job2->type = JOB_SEND_PKT_ALL_PORTS;
						new_job2->packet = new_packet;
						job_q_add(&job_q, new_job2);

						/*
						* Create the second packet which
						* has the file contents
						*/
						while(!feof(fp)){
							new_packet2 = (struct packet *)
								malloc(sizeof(struct packet));
							new_packet2->dst
								= (char) new_job->file_upload_dst;
							new_packet2->src = (char) host_id;
							new_packet2->type = (char) PKT_FILE_UPLOAD_END;


							n = fread(string,sizeof(char),
								PKT_PAYLOAD_MAX, fp);

							string[n] = '\0';

							for (i=0; i<n; i++) {
								new_packet2->payload[i]
									= string[i];
							}
							if(n < 100) new_packet2->payload[i]='\0';
							new_packet2->length = n;

							/*
							* Create a job to send the packet
							* and put the job in the job queue
							*/

							new_job3 = (struct host_job *)
								malloc(sizeof(struct host_job));
							new_job3->type
								= JOB_SEND_PKT_ALL_PORTS;
							new_job3->packet = new_packet2;

							printf("host creating packet of file contents for %d payload length: %d\n", new_job->file_upload_dst, new_packet2->length);
							job_q_add(&job_q, new_job3);
							n = 0;
						}

					}
						fclose(fp);
						fp = NULL;


				}
						if(new_job->packet != NULL){
							free(new_job->packet);
							new_job->packet = NULL;
						}
						if(new_job != NULL){
							free(new_job);
							new_job = NULL;
						}
				break;

			/* The next two jobs are for the receving host */

			case JOB_FILE_UPLOAD_RECV_START:
			#ifdef DEBUG
			printf("host %d: JOB_FILE_UPLOAD_RECV_START \n", host_id);
			#endif
				/* Initialize the file buffer data structure */
				file_buf_init(&f_buf_upload);

				/*
				* Transfer the file name in the packet payload
				* to the file buffer data structure
				*/
				file_buf_put_name(&f_buf_upload,
					new_job->packet->payload,
					new_job->packet->length);

					if(new_job->packet != NULL){
						free(new_job->packet);
						new_job->packet = NULL;
					}
					if(new_job != NULL){
						free(new_job);
						new_job = NULL;
					}
				break;

			case JOB_FILE_UPLOAD_RECV_END:
			#ifdef DEBUG
				printf("host %d: JOB_FILE_UPLOAD_RECV_END \n", host_id);
			#endif
				/*
				* Download packet payload into file buffer
				* data structure
				*/
				file_buf_add(&f_buf_upload,
					new_job->packet->payload,
					new_job->packet->length);



				if((int) new_job->packet->length < 100){
					if (dir_valid == 1) {
						/*
						* Get file name from the file buffer
						* Then open the file
						*/
						printf("file buf length: %d, tail: %d\n", f_buf_upload.occ, f_buf_upload.tail);
						file_buf_get_name(&f_buf_upload, string);
						n = sprintf(name, "./%s/%s", dir, string);
						name[n] = '\0';
						fp = fopen(name, "w");

						if (fp != NULL) {
							fprintf(fp, f_buf_upload.buffer);
							f_buf_upload.buffer[0]='\0';
							memset(f_buf_upload.buffer, 0,MAX_FILE_BUFFER+1);

						}
						fclose(fp);
						fp=NULL;



							printf("-----------------------\n");
							printf("%s", f_buf_upload.buffer);

					}
				}
						if(new_job->packet != NULL){
							free(new_job->packet);
							new_job->packet = NULL;
						}
						if(new_job != NULL){
							free(new_job);
							new_job = NULL;
						}
				break;
			default:
				if(new_job->packet != NULL){
					free(new_job->packet);
					new_job->packet = NULL;
				}
				if(new_job!=NULL){
					free(new_job);
					new_job= NULL;
				}
				break;
		}

	} // end of job



	send_tree_pkt++;
	// The host goes to sleep for 10 ms
	usleep(TENMILLISEC);
	
} // End of while loop

}
