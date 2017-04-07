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
			if(in_packet!= NULL){
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
	
		// int i=0; 
		packet_dest = (int)new_job->packet->dst;
		#ifdef DEBUG
		printf("switch: forwarding table\n");
		print_ftable(f_table);
		#endif

		if(find_host_in_table(f_table, packet_dest)==-1){
			//host not in table
			//send to all ports except the received port
			
			for (i=0; i<node_port_num; i++) {
				if(i != new_job->in_port_index){
					#ifdef DEBUG
					printf("switch:sending packet on port %d of %d\n", i, node_port_num);
					#endif
					packet_send(node_port[i], new_job->packet);
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
		if(new_job->packet != NULL){
			free(new_job->packet);
			new_job->packet = NULL;
		}
		if(new_job != NULL){
			free(new_job);
			new_job = NULL;
		}
		
		
		// }
	}





	/* The host goes to sleep for 10 ms */
	usleep(TENMILLISEC);

} /* End of while loop */

}




