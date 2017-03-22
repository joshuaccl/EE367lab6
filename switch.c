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
#define DEBUG

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
struct forwarding {
    int valid;
    int dest_id;
    int port_id;
};

void init_forwarding_table(struct forwarding table[100]){
    for(int i = 0; i < 100; i++){
        table[i].port_id = i;
        table[i].valid = 0;   // initialize all invalid
        table[i].dest_id = -1; //set dest_id = -1 for undefined 
    }
}
int get_host_at_port(struct forwarding table[100], int port_id){
    if(table[port_id].valid == 1) return table[port_id].dest_id;
    else return -1;
}
int set_src_at_port(struct forwarding table[100], int host_id, int port_id){
    //when we find a destination and a port, add it to the table
    table[port_id].dest_id = host_id;
    table[port_id].valid = 1;
}
int find_host_in_table(struct forwarding table[100], int host_id){
    //return the port number of a host we are looking for
    //returns -1 if the host is not defined on a port
    for(int i = 0; i < 100; i++){
        if(table[i].dest_id==host_id && table[i].valid==1)
            return i;
    }
    return -1;
}
void print_ftable(struct forwarding table[100]){
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
// char forwarding_table[100][2];
char packet_dest;
// int table_length=0;

while(1) {
	
	/*
	 * Get packets from incoming links and translate to jobs
  	 * Put jobs in job queue
 	 */
	  

	for (k = 0; k < node_port_num; k++) { /* Scan all ports */
		in_packet = (struct packet *) malloc(sizeof(struct packet));
		n = packet_recv(node_port[k], in_packet);

		// if ((n > 0) && ((int) in_packet->dst == host_id)) {
			if(n>0){
			#ifdef DEBUG
				printf("switch:packet received from port %d of %d\n", k, node_port_num);
			#endif
			new_job = (struct host_job *) 
				malloc(sizeof(struct host_job));
			new_job->in_port_index = k;
			new_job->packet = in_packet;

			if(get_host_at_port(f_table, k)==-1){
				set_src_at_port(f_table, (int) in_packet->src , k);
					f_table_length++;
			}
			
			job_q_add(&job_q, new_job);
				
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
	
		// int i=0; 
		packet_dest = (int)new_job->packet->dst;
		#ifdef DEBUG
		printf("switch: forwarding table\n");
		print_ftable(f_table);
		#endif

		if(find_host_in_table(f_table, packet_dest)==-1){
			//host not in table
			//send to all ports except the received port
			
			for (k=0; k<node_port_num; k++) {
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
			printf("switch: sending packet to %d \n", packet_dest);
			#endif
			packet_send(node_port[find_host_in_table(f_table, packet_dest)], new_job->packet);
		}

		// while( i<100 || forwarding_table[1][i] != new_job->packet->dst)
		// {
		// 	i++;
		// }
		
		// //did not find the address in table
		// if(i==100)
		// {
		// 	forwarding_table[0][table_length]='1';
		// 	forwarding_table[1][table_length]=packet_dest;
		// 	forwarding_table[2][table_length]=new_job->packet->src;
		// 	table_length++;
				
			

		// /* Send packets on all ports */	
		// 	for (k=0; k<node_port_num; k++) {
		// 		packet_send(node_port[k], new_job->packet);
		// 	}
		// 	free(new_job->packet);
		// 	free(new_job);
		// 	break;



		// }
		// else //send packet on the port found in the forwarding table
		// {
		// packet_send(node_port[forwarding_table[2][i]] , new_job->packet);
		free(new_job->packet);
		free(new_job);
		// }
	}





	/* The host goes to sleep for 10 ms */
	usleep(TENMILLISEC);

} /* End of while loop */

}




