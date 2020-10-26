

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pwd.h>

#include <termios.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <syslog.h>
#include <sys/reboot.h>
#include <fcntl.h>
#include <sys/time.h>
#include <assert.h>
#include <error.h>
#include <errno.h>
#include <string.h>


#define BAUDRATE B38400
#define SERIALDEVICE "/dev/ttyUSB0"

#include "modbusgw.h"

/* Global configuration parameters */
const char port[] = DEFAULTPORT;
const unsigned short int baud		= DEFAULTBAUD;
const char parity			= DEFAULTPARITY;
const char bits			= DEFAULTBITS;
const char stops			= DEFAULTSTOP;
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

static void skeleton_daemon()
{
    pid_t pid;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
    {
        exit(EXIT_FAILURE);
    }

    /* Catch, ignore and handle signals */
    //TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    chdir("/");

    /* Close all open file descriptors */
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        close (x);
    }

    /* Open the log file */
    openlog ("solagwd", LOG_PID, LOG_DAEMON);
}

int
main(int argc, char *argv[])
{
	int udp_listen_socket, tcp_listen_socket;
	int rc;

	pthread_t udpthread;
	pthread_t tcpthread;

	skeleton_daemon();

    syslog (LOG_NOTICE, "solagwd started.");

	/*
	 * Make sure at least one of tcp or udp are enabled.
	 * Otherwise, what is the point of running?
	 */
	if(!tcp_enabled && !udp_enabled)
	{
		syslog (LOG_ERR,"Error: must enable tcp or udp");
		exit(1);
	}

	/*
	 * Create listen sockets, if enabled
	 */
	if(tcp_enabled)
	{
		tcp_listen_socket = setup_tcp(tcp_port);

		if(tcp_listen_socket != -1)
		{
			pthread_create(&udpthread,
			       NULL,
			       (pthread_startroutine_t)tcp_listen_thread,
			       (void *) tcp_listen_socket);
		}
		else
		{
			syslog (LOG_ERR,"Error: Could not TCP create socket");
			exit(1);
		}
	}

	if(udp_enabled)
	{
		udp_listen_socket = setup_udp(udp_port);

		if(udp_listen_socket != -1)
		{
			pthread_create(&tcpthread,
			       NULL,
			       (pthread_startroutine_t) udp_worker_thread,
			       (void *) udp_listen_socket);
		}
		else
		{
			syslog (LOG_ERR,"Error: Could not UDP create socket");
			exit(1);
		}
	}

	/*
	 * Open the serial port
	 */
	while ((rc = mbopen(port, baud, bits, parity, stops)) < 0)
	{

		syslog (LOG_ERR,"%s: unable to open serial port\n", SERIALDEVICE);
		usleep(10000000L);
	}
	syslog(LOG_NOTICE, "%s opened.",SERIALDEVICE);

	while(!die)
	{
		sleep(1);
	}

	return 0;
}



