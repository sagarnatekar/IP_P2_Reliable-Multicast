#ifndef P2MPCLIENT
#define P2MPCLIENT

#define MAX_SEQ 4294967295
#define MAXLEN 5000
#pragma pack(1)
typedef struct segment_t{
	uint16_t checksum;
	uint16_t pkt_type;
	uint32_t seq_num;
	char *data;
	int *ack;
} segment;
#pragma pack(0)

typedef struct host_info_t{
//      int portnum;
        char iface_name[64];
        char ip_addr[128];
} host_info;

extern segment *send_buffer; //sender side buffer = array of segments
//extern send_buffer sbuf; 
extern int mss;
extern int n;
extern FILE *file;
extern int server_port;

#endif
