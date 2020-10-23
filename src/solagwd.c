#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pwd.h>

#include "modbusgw.h"

/* Global configuration parameters */
char *port			= DEFAULTPORT;
unsigned short int baud		= DEFAULTBAUD;
char parity			= DEFAULTPARITY;
char bits			= DEFAULTBITS;
char stops			= DEFAULTSTOP;
unsigned short int tcpport	= DEFAULTTCPPORT;
unsigned short int udpport	= DEFAULTUDPPORT;
unsigned int delay		= DEFAULTDELAY;
unsigned int timeout		= DEFAULTTIMEOUT;
unsigned short int tries	= DEFAULTTRIES;
unsigned int debug		= DEFAULTDEBUG;
unsigned int verbose		= DEFAULTVERBOSE;
int udp_port			= DEFAULTUDPPORT;
int tcp_port			= DEFAULTTCPPORT;
char udp_enabled		= DEFAULTUDPENABLED;
char tcp_enabled		= DEFAULTTCPENABLED;
uid_t uid			= DEFAULTUID;

/* Global variables */
volatile int die = 0;

/* forward declarations */
int serial_thread(int fd);

void
croak(int signum)
{
	fprintf(stderr, "Shutting down by signal\n");
	die = 1;
}


int
main(int argc, char *argv[])
{
	int udp_listen_socket, tcp_listen_socket;
	int rc;

	pthread_t udpthread;
	pthread_t tcpthread;

	if(parseargs(argc, argv) <= 0) {
		help(argv[0], stderr);
		return 0;
	}

	if(verbose) {
		printf("Opening %s,%d,%d,%c,%d\n",
		       port, baud, bits, parity, stops);
	}


	/*
	 * Make sure at least one of tcp or udp are enabled.
	 * Otherwise, what is the point of running?
	 */
	if(!tcp_enabled && !udp_enabled) {
		fprintf(stderr, "%s: must enable tcp or udp\n",
			argv[0]);
		exit(1);
	}

	/*
	 * Create listen sockets, if enabled
	 */
	if(tcp_enabled) {
		tcp_listen_socket = setup_tcp(tcp_port);

		if(tcp_listen_socket != -1) {
			pthread_create(&udpthread,
			       NULL,
			       (pthread_startroutine_t)tcp_listen_thread,
			       (void *) tcp_listen_socket);
		} else {
			fprintf(stderr, "Could not UDP create socket\n");
			exit(1);
		}
	}

	if(udp_enabled) {
		udp_listen_socket = setup_udp(udp_port);

		if(udp_listen_socket != -1) {
			pthread_create(&tcpthread,
			       NULL,
			       (pthread_startroutine_t) udp_worker_thread,
			       (void *) udp_listen_socket);
		} else {
			fprintf(stderr, "Could not UDP create socket\n");
			exit(1);
		}
	}

	/*
	 * Open the serial port
	 */
	rc=mbopen(port, baud, bits, parity, stops);
	if(rc < 0) {
		fprintf(stderr, "%s: unable to open serial port\n", argv[0]);
		exit(1);
	}

	signal(SIGINT, croak);

	while(!die) {
		sleep(1);
	}

	return 0;
}



