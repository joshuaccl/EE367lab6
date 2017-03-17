 /*
  * switch.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <unistd.h>
#include <fcntl.h>

#include "main.h"
#include "net.h"
#include "man.h"
#include "host.h"
#include "packet.h"
#include "switch.h"

#define MAX_FILE_BUFFER 1000
#define MAX_MSG_LENGTH 100
#define MAX_DIR_NAME 100
#define MAX_FILE_NAME 100
#define PKT_PAYLOAD_MAX 100
#define TENMILLISEC 10000   /* 10 millisecond sleep */

/* Types of packets */
/*
struct file_buf {
	char name[MAX_FILE_NAME];
	int name_length;
	char buffer[MAX_FILE_BUFFER+1];
	int head;
	int tail;
	int occ;
	FILE *fd;
};
*/

/*
 * File buffer operations
 */
/*
 Initialize file buffer data structure 
void file_buf_init(struct file_buf *f)
{
f->head = 0;
f->tail = MAX_FILE_BUFFER;
f->occ = 0;
f->name_length = 0;
}
*/

/* 
 * Get the file name in the file buffer and store it in name 
 * Terminate the string in name with tne null character
 */
/*
void file_buf_get_name(struct file_buf *f, char name[])
{
int i;

for (i=0; i<f->name_length; i++) {
	name[i] = f->name[i];
}
name[f->name_length] = '\0';
}
*/
/*
 *  Put name[] into the file name in the file buffer
 *  length = the length of name[]
 */
/*
void file_buf_put_name(struct file_buf *f, char name[], int length)
{
int i;

for (i=0; i<length; i++) {
	f->name[i] = name[i];
}
f->name_length = length;
}
*/
/*
 *  Add 'length' bytes n string[] to the file buffer
 */
/*
int file_buf_add(struct file_buf *f, char string[], int length)
{
int i = 0;

while (i < length && f->occ < MAX_FILE_BUFFER) {
	f->tail = (f->tail + 1) % (MAX_FILE_BUFFER + 1);
	f->buffer[f->tail] = string[i];
	i++;
        f->occ++;
}
return(i);
}
*/
/*
 *  Remove bytes from the file buffer and store it in string[] 
 *  The number of bytes is length.
 */
/*
int file_buf_remove(struct file_buf *f, char string[], int length)
{
int i = 0;

while (i < length && f->occ > 0) {
	string[i] = f->buffer[f->head];
	f->head = (f->head + 1) % (MAX_FILE_BUFFER + 1);
	i++;
        f->occ--;
}

return(i);
}

*/
/*
 * Operations with the manager
 */
/*
int get_man_command(struct man_port_at_host *port, char msg[], char *c) {

int n;
int i;
int k;

n = read(port->recv_fd, msg, MAN_MSG_LENGTH);  Get command from manager */
/*
if (n>0) {   Remove the first char from "msg" 
	for (i=0; msg[i]==' ' && i<n; i++);
	*c = msg[i];
	i++;
	for (; msg[i]==' ' && i<n; i++);
	for (k=0; k+i<n; k++) {
		msg[k] = msg[k+i];
	}
	msg[k] = '\0';
}
return n;

}
*/
/*
 * Operations requested by the manager
 */

/* Send back state of the host to the manager as a text message */
/*
void reply_display_host_state(
		struct man_port_at_host *port,
		char dir[],
		int dir_valid,
		int host_id)
{
int n;
char reply_msg[MAX_MSG_LENGTH];

if (dir_valid == 1) {
	n =sprintf(reply_msg, "%s %d", dir, host_id);
}
else {
	n = sprintf(reply_msg, "None %d", host_id);
}

write(port->send_fd, reply_msg, n);
}

*/

/* Job queue operations */

/* Add a job to the job queue */
/*
void job_q_add(struct job_queue *j_q, struct host_job *j)
{
if (j_q->head == NULL ) {
	j_q->head = j;
	j_q->tail = j;
	j_q->occ = 1;
}
else {
	(j_q->tail)->next = j;
	j->next = NULL;
	j_q->tail = j;
	j_q->occ++;
}
}
*/


/* Remove job from the job queue, and return pointer to the job*/
/*
struct host_job *job_q_remove(struct job_queue *j_q)
{
struct host_job *j;

if (j_q->occ == 0) return(NULL);
j = j_q->head;
j_q->head = (j_q->head)->next;
j_q->occ--;
return(j);
}


 Initialize job queue */
/*void job_q_init(struct job_queue *j_q)
{
j_q->occ = 0;
j_q->head = NULL;
j_q->tail = NULL;
}

int job_q_num(struct job_queue *j_q)
{
return j_q->occ;
}
*/

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
struct man_port_at_host *man_port;  // Port to the manager

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


/*
 * Initialize pipes 
 * Get link port to the manager
 */


man_port = net_get_host_port(host_id);


/* Create an array node_port[ ] to store the network link ports
 * at the host.  The number of ports is node_port_num
 */
node_port_list = net_get_port_list(host_id);

//	  Count the number of network link ports 
node_port_num = 0;
for (p=node_port_list; p!=NULL; p=p->next) {
	node_port_num++;
}
//	 Create memory space for the array 
node_port = (struct net_port **) 
	malloc(node_port_num*sizeof(struct net_port *));

//	Load ports into the array 
p = node_port_list;
for (k = 0; k < node_port_num; k++) {
	node_port[k] = p;
	p = p->next;
}	



/* Initialize the job queue */
job_q_init(&job_q);
// Initialize the forwarding table
char forwarding_table[100][2];
char packet_dest;
int table_length=0;

while(1) {
	
	/*
	 * Get packets from incoming links and translate to jobs
  	 * Put jobs in job queue
 	 */

	for (k = 0; k < node_port_num; k++) { /* Scan all ports */

		in_packet = (struct packet *) malloc(sizeof(struct packet));
		n = packet_recv(node_port[k], in_packet);

		if ((n > 0) && ((int) in_packet->dst == host_id)) {
			new_job = (struct host_job *) 
				malloc(sizeof(struct host_job));
			new_job->in_port_index = k;
			new_job->packet = in_packet;

			switch(in_packet->type) {
				/* Consider the packet type */

				/* 
				 * The next two packet types are 
				 * the ping request and ping reply
				 */
				case (char) PKT_PING_REQ: 
					new_job->type = JOB_PING_SEND_REPLY;
					job_q_add(&job_q, new_job);
					break;

				case (char) PKT_PING_REPLY:
					ping_reply_received = 1;
					free(in_packet);
					free(new_job);
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
					new_job->type 
						= JOB_FILE_UPLOAD_RECV_START;
					job_q_add(&job_q, new_job);
					break;

				case (char) PKT_FILE_UPLOAD_END:
					new_job->type 
						= JOB_FILE_UPLOAD_RECV_END;
					job_q_add(&job_q, new_job);
					break;
				default:
					free(in_packet);
					free(new_job);
			}
		}
		else {
			free(in_packet);
		}
	}

	/*
 	 * Execute one job in the job queue
 	 */

	if (job_q_num(&job_q) > 0) {

		/* Get a new job from the job queue */
		new_job = job_q_remove(&job_q);
		
		int i=0; 
		packet_dest = new_job->packet->dst;

		while( i<100 || forwarding_table[1][i] != new_job->packet->dst)
		{
			i++;
		}
		
		//did not find the address in table
		if(i==100)
		{
			forwarding_table[0][table_length]='1';
			forwarding_table[1][table_length]=packet_dest;
			forwarding_table[2][table_length]=new_job->packet->src;
			table_length++;
				
			

		/* Send packets on all ports */	
			for (k=0; k<node_port_num; k++) {
				packet_send(node_port[k], new_job->packet);
			}
			free(new_job->packet);
			free(new_job);
			break;



		}
		else //send packet on the port found in the forwarding table
		{
		packet_send(node_port[forwarding_table[2][i]] , new_job->packet);
		free(new_job->packet);
		free(new_job);
		}
	}




	/* The host goes to sleep for 10 ms */
	usleep(TENMILLISEC);

} /* End of while loop */

}




