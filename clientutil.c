#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<math.h>
#include<stdint.h>
#include<string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <pthread.h>
#include"p2mpclient.h"

int n;
int mss;
FILE *file;
int server_port;
segment *send_buffer;
int no_of_receivers;
int soc;
host_info client_addr;
host_info *server_addr;
struct sockaddr_in sender_addr;

int *retransmit; //stores index of receivers to whom segment has to be retransmitted

int oldest_unacked;   //denotes the no. of oldest unacknowledged segment in buffer
int next_seq_num;     //to populate seq no. field in seg hdr 

pthread_mutex_t mutex_retransmit = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ack = PTHREAD_MUTEX_INITIALIZER;

int udt_send(int seg_index,int server_index)
{
	struct sockaddr_in their_addr; // connector's address information
	struct hostent *he;
	int numbytes;
	int addr_len;
	int len;
	char buf[MAXLEN];
    
	if ((he=gethostbyname(server_addr[server_index].ip_addr)) == NULL) {  // get the host info
		printf("\nHost not found %s\n",server_addr[server_index].ip_addr);
		exit(-1);
    	}

	their_addr.sin_family = AF_INET;     // host byte order
	their_addr.sin_port = htons(server_port); // short, network byte order
	their_addr.sin_addr = *((struct in_addr *)he->h_addr);
	strcpy(buf,"");
//	printf("\n\nData length = %d",(int)strlen(send_buffer[seg_index].data));
	sprintf(buf,"%u\n%u\n%u\n%s",send_buffer[seg_index].seq_num,send_buffer[seg_index].checksum,send_buffer[seg_index].pkt_type,send_buffer[seg_index].data);
	len = strlen(buf);
	printf("\n\n***********Bytes Sent: %d\ndata length is %d",len,(int)strlen(send_buffer[seg_index].data));
	if (sendto(soc,buf, len, 0, (struct sockaddr *)&their_addr, sizeof (their_addr)) == -1) {
       		printf("Error in sending");
       		exit(-1);
	}	

}

// Read 1 MSS worth data from file and return the data - reads byte-by-byte
char * read_file()
{
    int j=0;
    
    char *temp_buf = (char *)malloc(mss*sizeof(char));
    char buff[1];

    for(j=0;j<mss;j++)
    {
    if((fread(buff,sizeof(char),1,file))<0)
{
perror("\nRead error");
exit(1);
}
temp_buf[j] = buff[0];
   }
   temp_buf[j] = '\0';
   return temp_buf;
}

uint16_t create_checksum(char *data)
{
	uint16_t padd=0; //in case data has odd no. of octets
	uint16_t word16; //stores 16 bit words out of adjacent 8 bits
	uint32_t sum;
	int i;

	int len_udp = strlen(data)+strlen(data)%2; //no. of octets
	sum=0;

	// make 16 bit words out of every two adjacent 8 bit words and
	// calculate the sum of all 16 bit words

	for (i=0; i<len_udp; i=i+2) 
	{
		word16 =((data[i]<<8)&0xFF00)+(data[i+1]&0xFF);
		sum = sum + (unsigned long)word16;
	}

	// keep only the last 16 bits of the 32 bit calculated sum and add the carries
	while (sum>>16) 
		sum = (sum & 0xFFFF)+(sum >> 16);

	// Take the one's complement of sum
	sum = ~sum;

	printf("Checksum is %x\n",(uint16_t)sum);
	return ((uint16_t) sum);
}

// appends header to payload
void create_segment(uint32_t seg_num)
{
	int i;
	send_buffer[(seg_num)%n].seq_num = seg_num;
	send_buffer[(seg_num)%n].pkt_type = 0x5555; //indicates data packet - 0101010101010101
	send_buffer[(seg_num)%n].checksum = create_checksum(send_buffer[(seg_num)%n].data);
	send_buffer[(seg_num)%n].ack = (int *)malloc(no_of_receivers*sizeof(int));
	for(i=0;i<no_of_receivers;i++)
		send_buffer[seg_num%n].ack[i] = 0;
}

void populate_public_ip()
{

	struct ifaddrs *myaddrs, *ifa;
	void *in_addr;
	char buf[64], intf[128];

	strcpy(client_addr.iface_name, "");

	if(getifaddrs(&myaddrs) != 0) {
		printf("getifaddrs failed! \n");
		exit(-1);
	}

	for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next) {

		if (ifa->ifa_addr == NULL)
			continue;

		if (!(ifa->ifa_flags & IFF_UP))
			continue;

		switch (ifa->ifa_addr->sa_family) {
        
			case AF_INET: { 
				struct sockaddr_in *s4 = (struct sockaddr_in *)ifa->ifa_addr;
				in_addr = &s4->sin_addr;
				break;
			}

			case AF_INET6: {
				struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ifa->ifa_addr;
				in_addr = &s6->sin6_addr;
				break;
			}

			default:
				continue;
		}

		if (inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf))) {
			if ( ifa->ifa_addr->sa_family == AF_INET && strcmp(ifa->ifa_name, "lo")!=0 ) {
				printf("\nDo you want the sender to bind to %s interface?(y/n): ", ifa->ifa_name);
				scanf("%s", intf);
				if ( strcmp(intf, "n") == 0 )
					continue;
				sprintf(client_addr.ip_addr, "%s", buf);
				sprintf(client_addr.iface_name, "%s", ifa->ifa_name);
			
			}
		}
	}

	freeifaddrs(myaddrs);
	
	if ( strcmp(client_addr.iface_name, "") == 0 ) {
		printf("Either no Interface is up or you did not select any interface ..... \nclient Exiting .... \n\n");
		exit(0);
	}

	 printf("\n\nMy public interface and IP is:  %s %s\n\n", client_addr.iface_name, client_addr.ip_addr);

}


/*
void initialize_sender_buffer()
{
	int i=0;
	send_buffer = (segment *)malloc(n*sizeof(segment));
	for(i=0;i<n;i++){
		//send_buffer[i].data = (char *)malloc(mss*sizeof(char));
		//strcpy(send_buffer[i].data, read_file()); //copy 1 segment data into sender buffer
		//send_buffer[i].data[strlen(send_buffer[i].data)] = '\0';
		//create_segments((uint32_t) i);
		
		send_buffer[i].seq_num = - 1;
	}
	printf("iterated %d times\n",i);
}
*/

int second_dup_ack()
{
}

void update_retransmit_arr()
{
}

void update_ack_arr()
{
}

void wait_for_ack()
{
	
	char buf[MAXLEN];
        int numbytes,i;

	char *a;
	uint32_t recvd_seq_num;

	char recv_addr[16] = {0}; //to store IP address of receiver from data from socket 
	
        int addr_len = sizeof (sender_addr);
        strcpy(buf,"");

	numbytes=recvfrom(soc, buf, MAXLEN , 0,(struct sockaddr *)&sender_addr, &addr_len);
        if(numbytes == -1) {
                printf(" Error in receiving\n");
                exit(-1);
        }
	
	inet_ntop(AF_INET, &sender_addr.sin_addr.s_addr, recv_addr, sizeof recv_addr); //extract IP address from sender_addr

	a=strtok(buf,"\n");
		
	recvd_seq_num = (uint32_t)atoi(a);

	if(recvd_seq_num >= oldest_unacked)
	{
		for(i=0;i<no_of_receivers;i++)
		{
			if(strcmp(server_addr[i].ip_addr,recv_addr)==0)
			{
				send_buffer[(recvd_seq_num)%n].ack[i]+=1;
				break;
			}
			
		}
	}		
}

void *recv_ack(void *ptr)
{
	int retval;
	while(1)
	{
		wait_for_ack();
		
		pthread_mutex_lock(&mutex_ack);
			retval = second_dup_ack();
		pthread_mutex_unlock(&mutex_ack);
			
		if(retval == 1)
		{
			pthread_mutex_lock(&mutex_retransmit);
				update_retransmit_arr();
			pthread_mutex_unlock(&mutex_retransmit);
		}
		else
		{
			pthread_mutex_lock(&mutex_ack);
				update_ack_arr();
			pthread_mutex_unlock(&mutex_ack);
		}
	}
}

void * timeout_process(void *ptr)
{

}

void start_timer_thread(pthread_t timer_thread)
{
	pthread_create(&timer_thread,NULL,timeout_process,NULL);

}

int is_buffer_avail()
{
	if(next_seq_num-oldest_unacked<n)
		return TRUE;
	return FALSE;
}

int is_pkt_earliest_transmitted()
{
	if(oldest_unacked == next_seq_num)
		return TRUE;
	return FALSE;
}

void * rdt_send(void *ptr)
{
	int i,j;
	pthread_t timer_thread;
	while(1)
	{
		if(is_buffer_avail())	
		{
			strcpy(send_buffer[next_seq_num].data, read_file()); //copy 1 segment data into sender buffer
                	send_buffer[next_seq_num].data[strlen(send_buffer[next_seq_num].data)] = '\0';
			//read_file();
			create_segment(next_seq_num);
			
			for(j=0;j<no_of_receivers;j++)
				udt_send(next_seq_num,j);

			if(is_pkt_earliest_transmitted())
				start_timer_thread(timer_thread);

			next_seq_num=(next_seq_num+1)%MAX_SEQ;
		}		
		pthread_mutex_lock(&mutex_retransmit);
			for(i=0;i<no_of_receivers;i++)
			{
				if(retransmit[i] == 1)
				{
					retransmit[i]=0;
					udt_send(oldest_unacked,i);
				}
			}
		pthread_mutex_unlock(&mutex_retransmit);
	}
}

int init_sender(int argc,char *argv[])
{
	int i;

	oldest_unacked = 0;
	next_seq_num = 0;
	
	pthread_t sender_thread;
//	pthread_t timer_thread;
	pthread_t recv_thread;

	if(argc==1)
	{
		printf("Incorrect command line agruments\n");
		exit(-1);
	}

	mss = atoi(argv[argc-1]);
	printf("MSS is:%d\n",mss);
	n = atoi(argv[argc-2]);
	printf("Window size:%d\n",n);
	if(!(file = fopen(argv[argc-3],"rb")))
	{
		printf("File opening failed\n");
		exit(-1);
	}
	server_port = atoi(argv[argc-4]);
	printf("Server port is: %d\n",server_port);

	populate_public_ip();
//	populate_server_ip(argc);
	
	no_of_receivers = argc - 5;
	server_addr = (host_info*)malloc(sizeof(host_info)*(no_of_receivers));	
	
	for(i=argc-5;i>=1;i--){
		strcpy(server_addr[i-1].ip_addr,argv[i]);
	}

	printf("no of receivers: %d\n",no_of_receivers);
	for(i=0;i<no_of_receivers;i++)
	{
		printf("Receiver %d, ip addr %s\n",i,server_addr[i].ip_addr);
	}

	if ((soc = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
	        printf("Error creating socket\n");
        	exit(-1);
	}

//	initialize_sender_buffer();	

	send_buffer = (segment *)malloc(n*sizeof(segment)); //allocate space for sender buffer 
	retransmit = (int *)malloc(no_of_receivers*sizeof(int));	
	
	for(i=0;i<no_of_receivers;i++)
		retransmit[i]=0;

	pthread_create(&sender_thread,NULL,rdt_send,NULL);
//	pthread_create(&timer_thread,NULL,timeout_process,NULL);
	pthread_create(&recv_thread,NULL,recv_ack,NULL);

	
}


void print_segments()
{
int i = 0;
while(i<n)
{
printf("\nSegment %d's data : %s",i,send_buffer[i].data);
i++;
}
printf("\nDone !!\n");
}

