/*
 * args.c
 *
 *  Created on: Nov. 21, 2018
 *      Author: greg
 */


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "modbusgw.h"


const char validargs[] =
"\nValid Arguments:\n"
"\t-port <path>             device name (required, e.g. \"/dev/cua0\")\n"
"\t-baud <baud>             port speed (default 9600)\n"
"\t-parity {none|even|odd}  parity (default none)\n"
"\t-bits {7|8}              word size (default 8)\n"
"\t-stop {1|2}              number of stop bits (default 1)\n"
"\t-timeout <t>             timeout between messages, in milliseconds\n"
"\t-delay <t>               minimum delay between message, in milliseconds\n"
"\t-tries <t>               number of poll attempts (default 3)\n"
"\t-tcp <port>              TCP port (default 502)\n"
"\t-notcp                   Disable TCP listener\n"
"\t-udp <port>              UDP port\n"
"\t-noudp                   Disable UDP listener (default)\n"
"\t-user <login>            setuid to user after I/O is initialized\n"
"\t-debug                   enable debugging output (useful for developers)\n"
"\t-verbose                 enable verbose output (useful for integrators)\n";

int
parseargs(int argc, char **argv)
{
	unsigned long int temp;

	while(--argc) {
		argv++;

		/*
		 * TTY name
		 */
		if(strcasecmp(*argv, "-port")==0) {
			if(--argc < 1) {
				return 0;
			} else {
				port = *++argv;
			}
		}
		/*
		 * Baud rate
		 */
		else if(strcasecmp(*argv, "-baud")==0) {
			if(--argc < 1)
				return 0;

			argv++;
			if(!isdigit(**argv))
				return 0;

			temp = strtoul(*argv, NULL, 10);
			switch(temp) {
				case 300:
				case 1200:
				case 2400:
				case 4800:
				case 9600:
				case 19200:
				case 57600:
				case 115200:
					baud = (unsigned short int) temp;
					break;
				default:
					return 0;
			}

		}

		/*
		 * word size
		 */
		else if(strcasecmp(*argv, "-bits")==0) {
			if(--argc < 1)
				return 0;

			argv++;
			if(!isdigit(**argv))
				return 0;

			temp = strtoul(*argv, NULL, 10);
			switch(temp) {
				case 8:
					bits = 8;
					break;
				case 7:
					bits = 7;
					break;
				default:
					return 0;
			}

		}

		/*
		 * stop bits
		 */
		else if(strcasecmp(*argv, "-stop")==0) {
			if(--argc < 1)
				return 0;

			argv++;
			if(!isdigit(**argv))
				return 0;

			temp = strtoul(*argv, NULL, 10);
			switch(temp) {
				case 1:
					stops = 1;
					break;
				case 2:
					stops = 2;
					break;
				default:
					return 0;
			}
		}

		else if(strcasecmp(*argv, "-parity")==0) {
			if(--argc < 1)
				return 0;
			argv++;
			if(strcasecmp(*argv, "none")==0) {
				parity = 'n';
			} else if(strcasecmp(*argv, "even")==0) {
				parity = 'e';
			} else if(strcasecmp(*argv, "odd")==0) {
				parity = 'o';
			} else {
				fprintf(stderr, "parity must be odd, even, or none\n");
				return 0;
			}
		}

		/*
		 * Verbose mode
		 */
		else if((strcasecmp(*argv, "-verbose")==0) ||
			  (strcasecmp(*argv, "-v")==0)) {
			printf("verbose output enabled\n");
			verbose++;
		}

		else if(strcasecmp(*argv, "-debug")==0) {
			debug++;
		}

		/*
		 * User name
		 */
		else if(strcasecmp(*argv, "-user")==0) {
			struct passwd *p;

			if(--argc < 1)
				return 0;
			argv++;

			p = getpwnam(*argv);
			if(p==NULL) {
				fprintf(stderr, "Username '%s' not found.\n", *argv);
				exit(1);
			}

			uid = p->pw_uid;
		}

		/*
		 * disable TCP
		 */
		else if(strcasecmp(*argv, "-notcp")==0) {
			tcp_enabled = 0;
			if(verbose) {
				printf("TCP disabled\n");
			}
		}

		/*
		 * disable UDP
		 */
		else if(strcasecmp(*argv, "-noudp")==0) {
			udp_enabled = 0;
			if(verbose) {
				printf("UDP disabled\n");
			}
		}


		/*
		 * TCP port
		 */
		else if(strcasecmp(*argv, "-tcp")==0) {
			if(--argc < 1)
				return 0;

			argv++;
			if(!isdigit(**argv))
				return 0;

			temp = strtoul(*argv, NULL, 10);
			if(temp <= 65535) {
				tcp_port = (unsigned int) temp;
				tcp_enabled = 1;
			} else
				return 0;
		}


		/*
		 * UDP port
		 */
		else if(strcasecmp(*argv, "-udp")==0) {
			if(--argc < 1)
				return 0;

			argv++;
			if(!isdigit(**argv))
				return 0;

			temp = strtoul(*argv, NULL, 10);
			if(temp <= 65535) {
				udp_port = (unsigned int) temp;
				udp_enabled = 1;
			} else
				return 0;
		}

		/*
		 * Delay between polls
		 */
		else if(strcasecmp(*argv, "-delay")==0) {
			if(--argc < 1)
				return 0;

			argv++;
			if(!isdigit(**argv))
				return 0;

			temp = strtoul(*argv, NULL, 10);
			if(temp <= 1000)
				delay = (unsigned int) temp;
			else
				return 0;
		}

		/*
		 * Poll timeout
		 */
		else if(strcasecmp(*argv, "-timeout")==0) {
			if(--argc < 1)
				return 0;

			argv++;
			if(!isdigit(**argv))
				return 0;

			temp = strtoul(*argv, NULL, 10);
			if(temp <= 60000)
				delay = (unsigned int) temp;
			else
				return 0;
		}

		/*
		 * Number of attempts per poll
		 */
		else if(strcasecmp(*argv, "-tries")==0) {
			if(--argc < 1)
				return 0;

			argv++;
			if(!isdigit(**argv))
				return 0;

			temp = strtoul(*argv, NULL, 10);
			if(temp > 0 && temp < 20)
				delay = (unsigned short int) temp;
			else
				return 0;
		}

		/*
		 * Invalid argument
		 */
		else
			return 0;
	} /* end while */

	return 1;
}


int
help(char *argv0, FILE *out)
{
	fprintf(out, "usage: %s [arguments]\n%s\n", argv0, validargs);

	return 0;
}
