/*
 * serial.c
 *
 *  Created on: Nov. 21, 2018
 *      Author: greg
 */


#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <pthread.h>
#include "modbusgw.h"


static int mbfd;
static pthread_mutex_t ser_mutex;

static speed_t
baudconst(int baudrate)
{
	switch(baudrate) {
		case 300:
			return B300;
		case 1200:
			return B1200;
		case 2400:
			return B2400;
		case 4800:
			return B4800;
		case 9600:
			return B9600;
		case 19200:
			return B19200;
		case 38400:
			return B38400;
		case 57600:
			return B57600;
		case 115200:
			return B115200;
		default:
			return B0;
	}

}

int
mbsetup(int fd,
	   unsigned short int baudrate,
	   char numbits,
	   char paritysetting,
	   char stopbits)
{
	struct termios t;

	if(tcgetattr(mbfd, &t) == -1) {
		perror("tcgetattr()");
		return 0;
	}
	cfmakeraw(&t);

	/*
	 * Set baud rate
	 */
	if(cfsetspeed(&t, baudconst(baudrate)) == -1) {
		perror("cfsetspeed()");
		return 0;
	}

	/*
	 * Set parity
	 */
	switch(paritysetting) {
		case 'n':
		case 'N':
			t.c_cflag &= ~PARENB;
			break;

		case 'e':
		case 'E':
			t.c_cflag |= PARENB;
			t.c_cflag &= ~PARODD;
			break;

		case 'o':
		case 'O':
			t.c_cflag |= PARENB;
			t.c_cflag |= PARODD;
			break;
		default:
			fprintf(stderr, "Invalid parity selection specified\n");
			return 0;
	}

	/*
	 * Set word size
	 */
	t.c_cflag &= ~(CSIZE);
	switch(numbits) {
		case 5:
			t.c_cflag |= CS5;
			break;

		case 6:
			t.c_cflag |= CS6;
			break;

		case 7:
			t.c_cflag |= CS7;
			break;

		case 8:
			t.c_cflag |= CS8;
			break;

		default:
			fprintf(stderr, "Invalid word size specified\n");
			return 0;
	}

	/*
	 * Set stop bits
	 */
	switch(stopbits) {
		case 1:
			t.c_cflag &= ~CSTOPB;
			break;

		case 2:
			t.c_cflag |= CSTOPB;
			break;

		default:
			fprintf(stderr, "Invalid number of stop bits specified\n");
			break;
	}


	if(tcsetattr(fd, TCSANOW, &t) != 0) {
		perror("tcsetattr()");

	}


	/*
	 * Enable RTS and CTS
	 */
//	ioctl(fd, TIOCMGET, &i);
//	i |= TIOCM_RTS | TIOCM_DTR;
//	ioctl(fd, TIOCMSET, &i);

	return 1;
}


int
mbopen(char *device,
	   unsigned short int baudrate,
	   char numbits,
	   char paritysetting,
	   char stopbits)
{

	mbfd = open(device, O_RDWR | O_FSYNC | O_NONBLOCK);
	if(mbfd == -1) {
		perror("open()");
	} else if(mbsetup(mbfd, baudrate, numbits, paritysetting, stopbits) == -1) {
		close(mbfd);
		return -1;
	}

	if(debug) {
		fprintf(stderr, "OPEN(fd=%d)\n", mbfd);
	}
	return mbfd;
}

int
mbwrite(int fd, unsigned char *bytes, int numbytes)
{
	int i, rc;

	(void) tcflush(fd, TCIFLUSH);
	(void) tcflush(fd, TCOFLUSH);

	if(debug) {
		fprintf(stderr, "SEND(fd=%d, %d):", fd, numbytes);
		for(i=0; i<numbytes; i++)
			fprintf(stderr," 0x%02X", (unsigned char) bytes[i]);
		fprintf(stderr,"\n");
	}

	i = write(fd, bytes, numbytes);
	if(i > 0) {
		rc = tcdrain(fd);
		if(debug && rc!=0) {
			perror("tcdrain()");
		}
	} else {
		if(debug)
			perror("write()");
	}

	return i;
}

int
mbread(int fd, unsigned char *bytes, int numbytes, int mstimeout, int msgap)
{
	fd_set fds;
	struct timeval timeout, now, end;
	long gsec, gusec;
	long tsec, tusec;
	int count = 0;
	int rc;

	/* Calculate gap time values */
	gsec = msgap/1000;
	gusec = (msgap%1000) * 1000;

	/* calculate timeout time values */
	tsec = mstimeout/1000;
	tusec = (mstimeout%1000) * 1000;

	/*
	 * Get the current time of day
	 */
	if(gettimeofday(&now, NULL) != 0) {
		perror("gettimeofday()");
		return -1;
	}


	/*
	 * Figure out the time at which we will
	 * have timed out
	 */
	end.tv_sec = now.tv_sec + tsec;
	end.tv_usec = now.tv_usec + tusec;
	if(end.tv_usec > 1000000) {
		end.tv_usec -= 1000000;
		end.tv_sec += 1;
	}


	/*
	 * Get the first batch of bytes
	 */
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	timeout.tv_sec = tsec;
	timeout.tv_usec = tusec;

	rc = select(fd+1, &fds, NULL, NULL, &timeout);

	if(debug && rc == 0) {
		fprintf(stderr, "timeout\n");
	}
	if(rc == 0 || rc == -1) {
		return rc;
	}

	count = read(fd, bytes, numbytes);
	if(count < 0) {
		perror("read()");
	}

	if(count == numbytes) {
		return count;
	}

	/*
	 * Now that the message has started, wait for
	 * an end-of-message gap, full buffer, or
	 * timeout
	 */

	while(1) {
		FD_SET(fd, &fds);
		timeout.tv_sec = gsec;
		timeout.tv_usec = gusec;
		rc = select(fd+1, &fds, NULL, NULL, &timeout);

		if(rc == -1) {
			if(verbose) {
				perror("select()");
			}
			return -1;
		} else if(rc == 0) {
			return count;
		}

		rc = read(fd, bytes+count, numbytes-count);
		if(rc == -1) {
			if(verbose) {
				perror("read()");
			}
			return -1;
		} else if(rc == 0) {
			if(verbose) {
				fprintf(stderr, "Warning: read() returned 0 after select\n");
			}
			return count;
		} else {
			count += rc;
			if(count == numbytes) {
				return count;
			}

			/* find out what time it is now */
			if(gettimeofday(&now, NULL)!=0) {
				perror("gettimeofday()");
				return -1;
			}

			if((now.tv_sec > end.tv_sec)
			   || (now.tv_sec>=end.tv_sec
			       && (now.tv_usec >= end.tv_usec)))
				return count;
		}
	} /* end while */
}


int
transaction(unsigned char *src, unsigned short int srclen,
	    unsigned char *dst, unsigned short int dstlen,
	    int timeout, int gap)
{
	int count;
	int i;

	count = 0;
	pthread_mutex_lock(&ser_mutex);

	if(mbwrite(mbfd, src, srclen) != srclen) {
		perror("mbwrite()");
	} else {
		count = mbread(mbfd, dst, dstlen, timeout, gap);

		if(debug) {
			fprintf(stderr, "RECV(fd=%d, %d): ", mbfd, count);
			for(i=0; i<count; i++)
				fprintf(stderr," 0x%02X", (unsigned char) dst[i]);
			fprintf(stderr,"\n");
		}


	}

	pthread_mutex_unlock(&ser_mutex);

	return count;
}
