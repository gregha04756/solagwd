/*
 * modbusgw.h
 *
 *  Created on: Nov. 21, 2018
 *      Author: greg
 */

#ifndef MODBUSGW_H_
#define MODBUSGW_H_

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <pwd.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Configuration parameters
 */
#define DEFAULTPORT "/dev/ttyUSB0"
#define DEFAULTBAUD 38400
#define DEFAULTBITS 8
#define DEFAULTPARITY 'n'
#define DEFAULTSTOP 1

#define DEFAULTDELAY 10
#define DEFAULTTIMEOUT 100
#define DEFAULTTRIES 2

#define DEFAULTTCPENABLED 1
#define DEFAULTTCPPORT 502

#define DEFAULTUDPENABLED 1
#define DEFAULTUDPPORT 502

#define DEFAULTUID 0

#define DEFAULTDEBUG 1
#define DEFAULTVERBOSE 1

#define MAXTCPWORKTHREADS 2

#define MAXERRCOUNT 3

/*
 * Protocol constants
 */
#define MBAPBUFSIZE 7
#define MBADDRSIZE 1
#define MBCRCSIZE 2
#define MBMAXMESSAGE 255
#define MBMAXPDU (MBMAXMESSAGE-MBADDRSIZE-MBCRCSIZE)

/*
 * Data structures
 */

struct mbap {
	unsigned short int transaction_id;
	unsigned short int protocol_id;
	unsigned short int length;
	unsigned char unit;
};
typedef struct mbap mbap_t;

typedef struct changeregs {
	uint16_t status;
	uint16_t configuration;
	pthread_t my_thread;
} CHANGEREGS_T,*PCHANGEREGS;

typedef struct tcpworkerthreadinfo {
	int fd;
	int thread_index;
	pthread_t h_TCP_Worker_Thread;
	CHANGEREGS_T system_status;
} TCPWORKERTHREADINFO_T;

extern TCPWORKERTHREADINFO_T m_tcp_worker_thread_info[];


/*
 * Globals
 */
extern char *port;
extern unsigned short int baud;
extern char parity;
extern unsigned int delay;
extern unsigned int timeout;
unsigned short int tries;
extern char bits;
extern char stops;

extern unsigned int debug;
extern unsigned int verbose;

extern volatile int die;
extern volatile int m_nTCPWorkThreads;

extern int udp_port;
extern int tcp_port;
extern char tcp_enabled;
extern char udp_enabled;
extern uid_t uid;

extern volatile pthread_t m_h_primary_worker_thread;

/*
 * Function prototypes
 */
int parseargs(int argc, char **argv);
int help(char *argv0, FILE *out);

int setup_udp(int port);
int setup_tcp(int port);

void tcp_worker_thread(void * thread_info);
void udp_worker_thread(int fd);
void tcp_listen_thread(int fd);

int mbopen(char *device,
	   unsigned short int baudrate,
	   char numbits,
	   char paritysetting,
	   char stopbits);

int mbwrite(int fd, unsigned char *bytes, int numbytes);
int mbread(int fd, unsigned char *bytes, int numbytes, int mstimeout, int msgap);

int transaction(unsigned char *src, unsigned short int srclen,
	    unsigned char *dst, unsigned short int dstlen,
	    int timeout, int gap);

int mbap2buf(mbap_t *mbapheader, unsigned char *buf, int bufsize);
int buf2mbap(unsigned char *buf, int bufsize, mbap_t *mbapheader);

int build_serial_pdu(mbap_t *header, unsigned char *srcbuf, int srcbufsize,
	unsigned char *destbuf, int destbufsize);

int build_ip_pdu(mbap_t *header, unsigned char *response, int responselen,
		unsigned  char *destbuf, int destbuflen);

int is_the_primary_thread(pthread_t h_thread);
int is_register_in_request(const uint16_t reg, unsigned char * req,int cb_req);
uint16_t get_value_from_response(const uint16_t reg, unsigned char * req, int cb_req, unsigned char * resp, int cb_resp);
uint16_t get_request_reg_count(unsigned char * req, int cb_req);
uint8_t get_response_byte_count(unsigned char * resp, int cb_resp);
void set_value_in_response(uint16_t reg, uint16_t val, unsigned char * req, int cb_req, unsigned char * resp, int cb_resp);
void copy_the_change_bitmasks(unsigned char * req, int cb_req, unsigned char * resp, int count);
void set_the_change_bitmasks(unsigned char * req, int cb_req, unsigned char * resp, int count);
void set_the_primary_worker_thread(int thread_count);
pthread_t get_the_primary_worker_thread();
int do_transaction(void * lp_parms, unsigned char *src, unsigned short int srclen,
	unsigned char *dst, unsigned short int dstlen, int timeout, int gap);
int manage_request_response(void * lp_parms, unsigned char *src, unsigned short int srclen,
	unsigned char *dst, unsigned short int dstlen, int timeout, int gap);
int do_modified_request_response(uint16_t start_addr, uint16_t excluded_count, unsigned char *src, unsigned short int srclen,
	unsigned char *dst, unsigned short int dstlen, int timeout, int gap);
unsigned short int calc_crc16(unsigned char *ptr, int len);
int check_crc16(unsigned char *buf, int buflen);


typedef void	*(*pthread_startroutine_t) __P((void *));



#endif /* MODBUSGW_H_ */
