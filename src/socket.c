/*
 * socket.c
 *
 *  Created on: Nov. 21, 2018
 *      Author: greg
 */


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include "modbusgw.h"

const uint16_t System_Status_Change_Reg_Addr = 0;
const uint16_t System_Configuration_Change_Reg_Addr = 1;

int fd_udp = -1;
int fd_tcp = -1;

volatile pthread_t listen_thread;
volatile pthread_t m_h_primary_worker_thread;
volatile int m_nTCPWorkThreads = 0;
TCPWORKERTHREADINFO_T m_tcp_worker_thread_info[MAXTCPWORKTHREADS + 1];

static pthread_mutex_t listener_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int tcp_work_threads = 0;
static volatile CHANGEREGS_T system_status[MAXTCPWORKTHREADS];
static pthread_cond_t listener_wakeup = PTHREAD_COND_INITIALIZER;

int
setup_udp(int port)
{
	struct sockaddr_in saddr;

	if(fd_udp != -1) {
		close(fd_udp);
	}

	/*
	 * Create the socket
	 */
	fd_udp = socket(PF_INET, SOCK_DGRAM, 0);

	/*
	 * Bind an address to it
	 */
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(port);

	if(bind(fd_udp, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
		perror("setup_udp(): bind()");
		fd_udp = -1;
		return(-1);
	}

	return fd_udp;
}



int
setup_tcp(int port)
{
	struct sockaddr_in saddr;

	if(fd_tcp != -1) {
		close(fd_tcp);
	}

	/*
	 * Create the socket
	 */
	fd_tcp = socket(PF_INET, SOCK_STREAM, 0);

	/*
	 * Bind an address to it
	 */
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(port);

	if(bind(fd_tcp, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
		perror("setup_tcp(): bind()");
		fd_tcp = -1;
		return(-1);
	}

	listen(fd_tcp,2);

	return fd_tcp;
}

int is_register_in_request(const uint16_t reg, unsigned char * req, int cb_req)
{
	/*
	* In a Sola Modbus Read Holding Register (function code 3) request, the bytes are:
	* Offset 0: Unit ID
	* Offset 1: Function Code (0x03)
	* Offset 2-3: Beginning register address
	* Offset 4-5: Register count
	* Offset 6-7: 2 bytes of CRC16
	*/
	uint16_t bra;
	uint16_t count;
#ifdef _DEBUG
	uint8_t qer[8];
	int i_i;
	for (i_i = 0;(i_i < cb_req) && i_i < sizeof(qer) / sizeof(uint8_t); i_i++)
	{
		qer[i_i] = *(req + i_i);
	}
	if ((uint8_t)0x03 == (uint8_t)(*(req + 1)))
	{
		i_i = (256 * (*(req + 2))) + (*(req + 3));
		i_i = (256 * (*(req + 4))) + (*(req + 5));
	}
#endif
	bra = ((uint8_t)0x03 == (uint8_t)(*(req + 1))) ? (256 * (*(req + 2))) + (*(req + 3)) : 0;
	count = ((uint8_t)0x03 == (uint8_t)(*(req + 1))) ? (256 * (*(req + 4))) + (*(req + 5)) : 0;

	return ((reg >= bra) && reg < (bra+count) && (0 < count));
}

uint16_t get_value_from_response(const uint16_t reg, unsigned char * req, int cb_req, unsigned char * resp, int cb_resp)
{
	/*
	* Sola response to Read Holding Register (function code 3) contains:
	* Offset 0: Unit ID
	* Offset 1: Function code (0x03)
	* Offset 2: Count n of data payload bytes returned in response
	* Offset 3 -> n: Data from the beginning register up to
	*/
	uint16_t bra;
	uint16_t count;
	uint16_t value = 0;
#ifdef _DEBUG
	uint8_t pser[256];
	int i_i;
	int offset_0;
	int offset_1;
	void * p_v;
#endif

	bra = ((uint8_t)0x03 == (uint8_t)(*(req + 1))) ? (256 * (*(req + 2))) + (*(req + 3)) : 0;
	count = ((uint8_t)0x03 == (uint8_t)(*(req + 1))) ? (256 * (*(req + 4))) + (*(req + 5)) : 0;
#ifdef _DEBUG
	p_v = memset((void*)pser,0,sizeof(pser));
	for (i_i = 0; (i_i < cb_resp) && i_i < sizeof(pser) / sizeof(uint8_t); i_i++)
	{
		pser[i_i] = (uint8_t)(*(resp + i_i));
	}
	if ((uint8_t)0x03 == (uint8_t)(*(resp + 1)))
	{
		i_i = (256 * (*(resp + 2))) + (*(resp + 3));
		i_i = (256 * (*(resp + 4))) + (*(resp + 5));
	}
	offset_0 = 3 + (2 * (reg - bra));
	offset_1 = 4 + (2 * (reg - bra));
	value = ((reg >= bra) && reg < (bra + count) && (0 < count)) ? \
		(256 * (*(resp + offset_0))) + (*(resp + offset_1)) : 0;
	value =  ((reg >= bra) && reg < (bra + count) && (0 < count)) ? \
		(256 * (*(resp + 3 + (2 * (reg - bra))))) + (*(resp + 4 + (2 * (reg - bra)))) : 0;
#endif
	return ((reg >= bra) && reg < (bra + count) && (0 < count)) ? \
		(256 * (*(resp + 3 + (2*(reg-bra))))) + (*(resp + 4 + (2*(reg-bra)))) : 0;
}

void set_value_in_response(uint16_t reg, uint16_t val, unsigned char * req, int cb_req, unsigned char * resp, int cb_resp)
{
	uint16_t bra;
	int offset_0;
	int offset_1;
	if (0 != is_register_in_request(reg, req, cb_req))
	{
		bra = ((uint8_t)0x03 == (uint8_t)(*(req + 1))) ? (256 * (*(req + 2))) + (*(req + 3)) : 0;
		offset_0 = 3 + (2 * (reg - bra));
		offset_1 = 4 + (2 * (reg - bra));
		if (offset_0 < cb_req && offset_1 < cb_req)
		{
			*(resp + offset_0) = val / 256;
			*(resp + offset_1) = val & 0x00ff;
		}
	}
}


/*
 * This function must be called from the primary worker thread.
 * It copies the Sola system status change bitmask registers 0 and 1 to the
 * registers which will be read by any other client worker threads. The non-primary
 * worker threads won't directly read those register over the Modbus. In so doing we
 * preserve the bits of the registers to tell other clients that statuses have
 * changed. Otherwise our reading of the status register has cleared them and the
 * status changes possibly won't get picked up by other clients. See Honeywell Sola
 * Software Interface Specification for further details.
 */
void copy_the_change_bitmasks(unsigned char * req,int cb_req, unsigned char * resp, int count)
{
	int i_i;
	uint16_t value;
	if (0 < count) /* There has to be non-zero number of characters in response */
	{
		if (0 != is_register_in_request(System_Status_Change_Reg_Addr, req, cb_req))
		{
			value = get_value_from_response(System_Status_Change_Reg_Addr, req, cb_req, resp, count);
			pthread_mutex_lock(&listener_mutex);
#ifdef _DEBUG
			i_i = (2 <= m_nTCPWorkThreads) ? 0 : 0;
#endif
			for (i_i = 0; i_i < MAXTCPWORKTHREADS; i_i++)
			{
				m_tcp_worker_thread_info[i_i].system_status.status |= value;
			}
			pthread_mutex_unlock(&listener_mutex);
		}
		if (0 != is_register_in_request(System_Configuration_Change_Reg_Addr, req, cb_req))
		{
			value = get_value_from_response(System_Configuration_Change_Reg_Addr, req, cb_req, resp, count);
			pthread_mutex_lock(&listener_mutex);
			for (i_i = 0; i_i < MAXTCPWORKTHREADS; i_i++)
			{
				m_tcp_worker_thread_info[i_i].system_status.configuration |= value;
			}
			pthread_mutex_unlock(&listener_mutex);
		}
	}
}

void set_the_primary_worker_thread(int thread_count)
{
	int i_i;
	int i_worker_count = 0;
	m_h_primary_worker_thread = (0 == thread_count) ? 0 : m_h_primary_worker_thread;
	if (1 == thread_count)
	{
		for (i_i = 0; i_i < MAXTCPWORKTHREADS; i_i++)
		{
			m_h_primary_worker_thread = (0 != m_tcp_worker_thread_info[i_i].h_TCP_Worker_Thread) ? m_tcp_worker_thread_info[i_i].h_TCP_Worker_Thread : m_h_primary_worker_thread;
			i_worker_count = (0 != m_tcp_worker_thread_info[i_i].h_TCP_Worker_Thread) ? ++i_worker_count : i_worker_count;
		}
	}
#ifdef _DEBUG
	if (1 < i_worker_count)
	{
		perror("Invalid worker thread count > 1\n");
	}
#endif
	if (1 < thread_count)
	{
		for (i_i = 0; i_i < MAXTCPWORKTHREADS; i_i++)
		{
			if (0 != m_tcp_worker_thread_info[i_i].h_TCP_Worker_Thread)
			{
				m_h_primary_worker_thread = m_tcp_worker_thread_info[i_i].h_TCP_Worker_Thread;
				break;
			}
		}
	}

}

pthread_t get_the_primary_worker_thread()
{
	return m_h_primary_worker_thread;
}


void set_the_change_bitmasks(unsigned char * req, int cb_req, unsigned char * resp, int count)
{
	int i_i;
	uint16_t value;
	uint16_t crc;

	if (0 < count) /* There has to be non-zero number of characters in response */
	{
		if (0 != is_register_in_request(System_Status_Change_Reg_Addr, req, cb_req))
		{
			pthread_mutex_lock(&listener_mutex);
#ifdef _DEBUG
			i_i = (2 <= m_nTCPWorkThreads) ? 0 : 0;
#endif
			for (i_i = 0; i_i < MAXTCPWORKTHREADS; i_i++)
			{
				if (0 != pthread_equal(pthread_self(),m_tcp_worker_thread_info[i_i].h_TCP_Worker_Thread))
				{
					value = m_tcp_worker_thread_info[i_i].system_status.status;
					set_value_in_response(System_Status_Change_Reg_Addr, value, req, cb_req, resp, count);
					m_tcp_worker_thread_info[i_i].system_status.status = 0;
					/*
					 * Possibly changed the response, so recompute the CRC and append to the resp message
					 */
					crc = calc_crc16(resp, count-2);
					resp[count-2] = (crc >> 8) & 0xFF;
					resp[count-1] = crc & 0xFF;
				}
			}
			pthread_mutex_unlock(&listener_mutex);
		}
		if (0 != is_register_in_request(System_Configuration_Change_Reg_Addr, req, cb_req))
		{
			pthread_mutex_lock(&listener_mutex);
			for (i_i = 0; i_i < MAXTCPWORKTHREADS; i_i++)
			{
				if (0 != pthread_equal(pthread_self(),m_tcp_worker_thread_info[i_i].h_TCP_Worker_Thread))
				{
					value = m_tcp_worker_thread_info[i_i].system_status.configuration;
					set_value_in_response(System_Configuration_Change_Reg_Addr, value, req, cb_req, resp, count);
					m_tcp_worker_thread_info[i_i].system_status.configuration = 0;
					/*
					 * Possibly changed the response, so recompute the CRC and append to the resp message
					 */
					crc = calc_crc16(resp, count - 2);
					resp[count - 2] = (crc >> 8) & 0xFF;
					resp[count - 1] = crc & 0xFF;
				}
			}
			pthread_mutex_unlock(&listener_mutex);
		}
	}
}

int is_the_primary_thread(pthread_t h_thread)
{
	int i_r = 0;
	pthread_mutex_lock(&listener_mutex);
	i_r = pthread_equal(h_thread,m_h_primary_worker_thread);
	pthread_mutex_unlock(&listener_mutex);
	return i_r;
}

uint16_t get_request_reg_count(unsigned char * req, int cb_req)
{
	uint16_t u16_rc;
	return (u16_rc = (256 * (*(req + 4))) + (*(req + 5)));
}


uint8_t get_response_byte_count(unsigned char * resp, int cb_resp)
{
	uint8_t u8_rc;
	return (u8_rc = *(resp + 2));
}


int do_modified_request_response(uint16_t start_addr, uint16_t excluded_count, unsigned char *src, unsigned short int srclen,
	unsigned char *dst, unsigned short int dstlen, int timeout, int gap)
{
	void * p_v;
	uint8_t modified_serial_packet[MBMAXMESSAGE];
	uint8_t modified_serial_response[256];
#ifdef _DEBUG
	uint8_t debug_serial_response[256];
#endif
	uint16_t modified_request_count;
	uint16_t modified_response_count;
	uint16_t modified_start_addr;
	uint16_t modified_crc16;
	int i_i;
	int i_rc = 0;
	p_v = memset((void*)modified_serial_packet,0,sizeof(modified_serial_packet));
	p_v = memset((void*)modified_serial_response,0,sizeof(modified_serial_response));
#ifdef _DEBUG
	p_v = memset((void*)debug_serial_response,0,sizeof(debug_serial_response));
#endif
	/*
	 * Build up modified request packet
	 */
	modified_serial_packet[0] = *src; /* Get device ID */
	modified_serial_packet[1] = *(src + 1); /* Get function code (should be 3) */
	modified_start_addr = ((*(src + 2) << 8) + (*(src + 3))) + excluded_count;
	modified_serial_packet[2] = (modified_start_addr >> 8) & 0xff;
	modified_serial_packet[3] = (modified_start_addr & 0xff);
	modified_request_count = get_request_reg_count(src, srclen) - excluded_count;
	modified_serial_packet[4] = (modified_request_count >> 8) & 0xff;
	modified_serial_packet[5] = (modified_request_count & 0xff);
	modified_crc16 = calc_crc16(modified_serial_packet, 6);
	modified_serial_packet[6] = (modified_crc16 >> 8) & 0xff;
	modified_serial_packet[7] = (modified_crc16 & 0xff);
	/*
	 * Send the request and read the response
	 */
	i_rc = transaction(
		(unsigned char*)modified_serial_packet,
		(unsigned short)8,
		(unsigned char*)modified_serial_response,
		sizeof(modified_serial_response),
		timeout,
		100);
	if (0 == i_rc)
	{
		return i_rc;
	}
	/*
	 * Build modified response packet, allowing space for the excluded registers to be
	 * inserted
	 */
	modified_response_count = get_response_byte_count((unsigned char*)modified_serial_response,i_rc);
	*dst = modified_serial_response[0];
	*(dst + 1) = modified_serial_response[1];
	*(dst + 2) = modified_serial_response[2] + (2*excluded_count); /* The expected response byte count */
#ifdef _DEBUG
	debug_serial_response[0] = modified_serial_response[0];
	debug_serial_response[1] = modified_serial_response[1];
	debug_serial_response[2] = modified_serial_response[2] + (2 * excluded_count); /* The expected response byte count */
#endif
	/*
	 * Insert bytes into dst buffer, starting at offset allowing space
	 * for the excluded registers at the beginning of the response data payload
	 */
	for (i_i = 0; ((dst + i_i + 7) < (dst + dstlen)) && (i_i < modified_response_count); i_i++)
	{
		*(dst + i_i + 7) = modified_serial_response[3 + i_i];
#ifdef _DEBUG
		debug_serial_response[i_i + 7] = modified_serial_response[3 + i_i];
#endif
	}
	/*
	 * Finally insert CRC16 bytes at end of data payload in dst buffer
	 */
	modified_crc16 = calc_crc16(dst, i_i+7);
	*(dst + i_i + 7) = (modified_crc16 >> 8) & 0xff;
	*(dst + i_i + 8) = (modified_crc16 & 0xff);
#ifdef _DEBUG
	debug_serial_response[i_i + 7] = (modified_crc16 >> 8) & 0xff;
	debug_serial_response[i_i + 8] = (modified_crc16 & 0xff);
#endif
	return (i_i + 9);
}


int manage_request_response(void * lp_parms, unsigned char *src, unsigned short int srclen,
	unsigned char *dst, unsigned short int dstlen, int timeout, int gap)
{
	TCPWORKERTHREADINFO_T * pt_info = (TCPWORKERTHREADINFO_T *)lp_parms;
	int i_rc = 0;
	if (!is_the_primary_thread(m_tcp_worker_thread_info[pt_info->thread_index].h_TCP_Worker_Thread) &&
		(0 == is_register_in_request(System_Status_Change_Reg_Addr, src, srclen)) &&
		(0 == is_register_in_request(System_Configuration_Change_Reg_Addr, src, srclen)))
	{
		return (i_rc = transaction(src, srclen, dst, dstlen, timeout, 100));
	}
	if (!is_the_primary_thread(m_tcp_worker_thread_info[pt_info->thread_index].h_TCP_Worker_Thread) &&
		(0 != is_register_in_request(System_Status_Change_Reg_Addr, src, srclen)) &&
		(get_request_reg_count(src, srclen) > 2))
	{
		return (i_rc = do_modified_request_response((uint16_t)System_Status_Change_Reg_Addr, (int)2, src, srclen, dst, dstlen, timeout, 100));
	}
	return i_rc;
}


int do_transaction(void * lp_parms, unsigned char *src, unsigned short int srclen,
	unsigned char *dst, unsigned short int dstlen, int timeout, int gap)
{
	TCPWORKERTHREADINFO_T * pt_info = (TCPWORKERTHREADINFO_T *)lp_parms;
	int i_rc = 0;
	if (is_the_primary_thread(m_tcp_worker_thread_info[pt_info->thread_index].h_TCP_Worker_Thread))
	{
		return (i_rc = transaction(src, srclen, dst, dstlen, timeout, 100));
	}
	if (!is_the_primary_thread(m_tcp_worker_thread_info[pt_info->thread_index].h_TCP_Worker_Thread))
	{
		return (i_rc = manage_request_response((void*)pt_info, src, srclen, dst, dstlen, timeout, 100));
	}
	return i_rc;
}

void
tcp_worker_thread(void * thread_info)
{
	int rc;
	TCPWORKERTHREADINFO_T * pt_info = (TCPWORKERTHREADINFO_T*)thread_info;
	unsigned char ipbuffer[MBMAXPDU];
	unsigned char serpacket[MBMAXMESSAGE];
	unsigned char mbapbuffer[MBAPBUFSIZE];
	mbap_t m;
	int temp;
	sigset_t sigs;
	unsigned char response[256];
	int count;
	int errors;
	uint16_t crc;
	pthread_t h_temp_00;
	pthread_t h_temp_01;


	pthread_detach(pthread_self());
	errors = 0;

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGTERM);

	if(pthread_sigmask(SIG_BLOCK, &sigs, NULL) != 0) {
		if(debug) {
			fprintf(stderr, "pthread_sigmask() failed\n");
		}
	}

	while(!die && errors < MAXERRCOUNT) {
		/*
		 * If we're the only thread running, make ourselves the primary worker
		 */
		pthread_mutex_lock(&listener_mutex);
		m_h_primary_worker_thread = (tcp_work_threads == 1) ? pthread_self() : m_h_primary_worker_thread;
		pthread_mutex_unlock(&listener_mutex);
		/*
		 * Read an MBAP header
		 */
		rc = read(pt_info->fd, mbapbuffer, sizeof(mbapbuffer));
		if(rc == 0) {
			/* EOF */
			break;
		} else if((rc == -1) && (errno != EAGAIN)) {
			/* ERROR */
			if(debug) {
				perror("worker_thread(): read()");
			}
			break;
		} else {
			/* DATA */
			(void) buf2mbap(mbapbuffer, sizeof(mbapbuffer), &m);
		}

		if(debug && m.length > 256) {
			fprintf(stderr, "Warning: invalid packet received (tcp)\n");
		}

		if(m.protocol_id != 0 || m.length > sizeof(ipbuffer)) {
			/*
			 * Invalid packet.  Swallow the PDU.
			 */
			while(m.length > 0) {
				if(m.length < sizeof(ipbuffer))
					temp = m.length;
				else
					temp = sizeof(ipbuffer);

				(void) read(pt_info->fd, ipbuffer, temp);

				m.length = m.length - temp;
			}
			errors++;
		} else {
			/*
			 * MBAP header looked OK
			 *
			 * Read however many bytes the header specified
			 * are in the PDU
			 */

			rc = read(pt_info->fd, ipbuffer, m.length-1);

			if(debug)
			{
				fprintf(stderr, "Worker thread 0x%08lx processing transaction %d\n",
					(unsigned long int) pthread_self(),
					m.transaction_id);
			}

			count = build_serial_pdu(&m, ipbuffer, sizeof(ipbuffer),
						 serpacket, sizeof(serpacket));

			/*
			 * Only the primary thread can request the status change registers. If the request is from
			 * a non-primary thread and includes the status and configuration change registers,
			 * the request has to be modified to exclude the change registers, but request the balance
			 * of the registers requested. The response packet then has to re-assembled to include the
			 * change registers if it's not the primary thread. Since the serial packet is already
			 * decoded it's easiest to just create a new serial packet excluding the change registers,
			 * make the transaction, then carry on with obtaining the change bitmasks.
			 */
			rc = do_transaction((void*)pt_info, serpacket, count, response, sizeof(response), timeout, 100);

			if ((0 < rc) && is_the_primary_thread(m_tcp_worker_thread_info[pt_info->thread_index].h_TCP_Worker_Thread))
			{
				copy_the_change_bitmasks(serpacket, count, response, rc);
			}
			if ((0 < rc) && !is_the_primary_thread(m_tcp_worker_thread_info[pt_info->thread_index].h_TCP_Worker_Thread))
			{
				set_the_change_bitmasks(serpacket, count, response, rc);
			}

#ifdef _DEBUG
			if (is_register_in_request(System_Status_Change_Reg_Addr, serpacket, count))
			{
				uint16_t value = get_value_from_response(System_Status_Change_Reg_Addr, serpacket, count, response, rc);
				if (0 != value)
				{
					fprintf(stderr, "Worker thread 0x%08lx status change %04x\n",
						m_tcp_worker_thread_info[pt_info->thread_index].h_TCP_Worker_Thread,
						value);
				}
			}
			if (is_register_in_request(System_Configuration_Change_Reg_Addr, serpacket, count))
			{
				uint16_t value = get_value_from_response(System_Configuration_Change_Reg_Addr, serpacket, count, response, rc);
				if (0 != value)
				{
					fprintf(stderr, "Worker thread 0x%08lx configuration change %04x\n",
						m_tcp_worker_thread_info[pt_info->thread_index].h_TCP_Worker_Thread,
						value);
				}
			}
#endif

#ifdef _DEBUG
			if (2 <= m_nTCPWorkThreads)
			{
				h_temp_00 = get_the_primary_worker_thread();
				h_temp_01 = pthread_self();
			}
#endif
			if (rc == 0)
			{
				if (debug)
				{
					fprintf(stderr,"No response\n");
				}
				// Insert the "Gateway problem - the target device failed to respond" Modbus exception
				// into the response, followed by 2 bytes of dummy CRC and set the response length to 5
				response[0] = 0x00;
				response[1] = serpacket[1] + 0x80;	// Exception
				response[2] = 0x0b;	// Gateway problem - no response from target
				crc = calc_crc16(response, 3);
				response[3] = (crc >> 8) & 0xFF;
				response[4] = crc & 0xFF;
				rc = 5;
			}
			else if (rc < 0)
			{
				if (debug)
				{
					fprintf(stderr,"transaction() failed, aborting\n");
				}
				break;
			}

			/*
			 * Convert the response back
			 */
			count = build_ip_pdu(&m,
				response,
				rc,
				ipbuffer,
				sizeof(ipbuffer));

			/*
			 * Send it back up the socket
			 */
			if(debug) {
				printf("write(fd=%d, %d)\n", pt_info->fd, count);
			}
			write(pt_info->fd, ipbuffer, count);



			errors = 0;
		}

	}

	/*
	 * This thread is dying.  Decrement the worker thread
	 * count and notify anybody who is waiting for a worker
	 * thread that they can now wake up.
	 */
	pthread_mutex_lock(&listener_mutex);
	pt_info->h_TCP_Worker_Thread = listen_thread;
	pt_info->thread_index = 0;
	pt_info->system_status.configuration = 0;
	pt_info->system_status.status = 0;
	pt_info->system_status.my_thread = listen_thread;
	tcp_work_threads--;
	pthread_cond_signal(&listener_wakeup);
	pthread_mutex_unlock(&listener_mutex);

	if(debug) {
		if(errors >= MAXERRCOUNT) {
			fprintf(stderr, "Worker thread 0x%lx shutting down after %d errors\n",
				(unsigned long int) pthread_self(), errors);
		} else {
			fprintf(stderr, "Worker thread 0x%lx shutting down\n",
				(unsigned long int) pthread_self());
		}
	}
	close(pt_info->fd);

/*	return 1; */
	pthread_exit(NULL);
}

void
tcp_listen_thread(int fd)
{
	void *p_v;
	int rc;
	int i_i;
	int newfd;
	int nTCPWorkerThread;
	fd_set fdset_r;
	struct timeval timeout;
	struct sockaddr_in saddr;
	socklen_t addrlen;
	sigset_t sigs;
#ifdef _DEBUG
	pthread_t h_temp_00;
#endif

	pthread_detach(pthread_self());
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGTERM);

	if(pthread_sigmask(SIG_BLOCK, &sigs, NULL) != 0)
	{
		if(debug)
		{
			fprintf(stderr, "pthread_sigmask() failed\n");
		}
	}

	FD_ZERO(&fdset_r);

	listen_thread = pthread_self();
	pthread_mutex_lock(&listener_mutex);
	p_v = memset((void*)system_status,0,sizeof(system_status));
	p_v = memset((void*)m_tcp_worker_thread_info,0,sizeof(m_tcp_worker_thread_info));
	for (i_i=0;i_i<sizeof(m_tcp_worker_thread_info)/sizeof(TCPWORKERTHREADINFO_T);i_i++)
	{
		m_tcp_worker_thread_info[i_i].h_TCP_Worker_Thread = pthread_self();
	}
	pthread_mutex_unlock(&listener_mutex);


	while(!die) {

		/*
		 * Check to make sure there are not too many threads
		 * already running.  If there are, wait until one
		 * of them signals us that it is shutting down.
		 */
		pthread_mutex_lock(&listener_mutex);
		while(tcp_work_threads > MAXTCPWORKTHREADS) {
			if(debug) {
				fprintf(stderr, "Waiting for available worker thread\n");
			}
			pthread_cond_wait(&listener_wakeup, &listener_mutex);
			if(debug) {
				fprintf(stderr, "A worker thread might be available\n");
			}
		}
		pthread_mutex_unlock(&listener_mutex);

		/*
		 * We got here because a listener thread is
		 * available.  However, the reason one is available
		 * might be that the server is shutting down.  Check
		 * here to make sure that is not the case before
		 * going on.
		 */
		if(die) {
			/*
			 * Server shutting down
			 */
			break;
		}

		/*
		 * select on the listening descriptor
		 */
		FD_SET(fd, &fdset_r);
		timeout.tv_sec = 15;
		timeout.tv_usec = 0;

		rc = select(fd+1, &fdset_r, NULL, NULL, &timeout);

		if(rc == -1) {
			perror("tcp_listen_thread(): select()");
			break;
		}

		/*
		 * See if a connection is ready to be accepted
		 */
		if(FD_ISSET(fd, &fdset_r)) {
			addrlen = sizeof(saddr);
			newfd = accept(fd, (struct sockaddr *) &saddr,
				       &addrlen);
			if(newfd >= 0) {
				/*
				 * Set up thread info structure at next available index location
				 */
				for (nTCPWorkerThread = 0; nTCPWorkerThread < MAXTCPWORKTHREADS; nTCPWorkerThread++)
				{
#ifdef _DEBUG
					h_temp_00 = m_tcp_worker_thread_info[nTCPWorkerThread].h_TCP_Worker_Thread;
#endif
					if (0 != pthread_equal(pthread_self(),m_tcp_worker_thread_info[nTCPWorkerThread].h_TCP_Worker_Thread))
					{
						break;
					}
				}
#ifdef _DEBUG
				if (MAXTCPWORKTHREADS <= nTCPWorkerThread)
				{
					perror("Invalid nTCPWorkerThread > MAXTCPWORKTHREADS");
				}
#endif
				nTCPWorkerThread = (nTCPWorkerThread < MAXTCPWORKTHREADS) ? nTCPWorkerThread : 0;
				m_tcp_worker_thread_info[nTCPWorkerThread].fd = newfd;
				m_tcp_worker_thread_info[nTCPWorkerThread].thread_index = nTCPWorkerThread;

				/*
				 * Create a new worker thread
				 */
				pthread_create(
						&m_tcp_worker_thread_info[nTCPWorkerThread].h_TCP_Worker_Thread,
				       NULL,
				       (pthread_startroutine_t) tcp_worker_thread,
				       (void *)&m_tcp_worker_thread_info[nTCPWorkerThread]);
				if(debug)
				{
					fprintf(stderr, "Started worker 0x%08lx\n",(unsigned long int) m_tcp_worker_thread_info[nTCPWorkerThread].h_TCP_Worker_Thread);
				}

				/*
				 * Increment the count of worker threads
				 */
				pthread_mutex_lock(&listener_mutex);
				m_h_primary_worker_thread = (tcp_work_threads == 0) ? m_tcp_worker_thread_info[nTCPWorkerThread].h_TCP_Worker_Thread : m_h_primary_worker_thread;
				system_status[tcp_work_threads].my_thread = (tcp_work_threads == 0) ? m_tcp_worker_thread_info[nTCPWorkerThread].h_TCP_Worker_Thread : system_status[tcp_work_threads].my_thread;
				tcp_work_threads++;
				pthread_mutex_unlock(&listener_mutex);

			} else
			{
				perror("tcp_listen_thread(): accept()");
				break;
			}
		}

	}

/*	return 1; */
	pthread_exit(NULL);
}

void
udp_worker_thread(int fd)
{
	int rc;
	struct sockaddr_in from;
	socklen_t fromlen;
	unsigned char udpbuffer[MBMAXMESSAGE+MBAPBUFSIZE];
	sigset_t sigs;
	mbap_t header;
	unsigned char *data;
	unsigned char serpacket[MBMAXMESSAGE];
	unsigned char serresponse[MBMAXMESSAGE];
	int datasize;
	int count;

	pthread_detach(pthread_self());
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGTERM);

	if(pthread_sigmask(SIG_BLOCK, &sigs, NULL) != 0) {
		if(debug) {
			fprintf(stderr, "pthread_sigmask() failed\n");
		}
	}


	while(!die) {
		fromlen = sizeof(from);
		rc = recvfrom(fd, udpbuffer, sizeof(udpbuffer), 0,
			      (struct sockaddr *) &from, &fromlen);
		if(debug) {
			fprintf(stderr, "Got a udp request\n");
		}
		if(rc<0) {
			if(debug) {
				perror("udp_worker_thread(): recvfrom()");
			}
			continue;
		}

		//
		// Extract the MBAP header
		//
		(void) buf2mbap(udpbuffer, sizeof(udpbuffer), &header);

		//
		// Data begins immediately after the MBAP header
		//
 		data = udpbuffer + MBAPBUFSIZE;

		if(header.protocol_id != 0 || header.length > 256) {
			if(debug) {
				fprintf(stderr, "Invalid UDP message received, protocol %u, length %u\n",
					header.protocol_id, header.length);
			}
			continue;
		}

		if(rc != header.length + MBAPBUFSIZE - MBADDRSIZE) {
			if(debug) {
				fprintf(stderr, "Invalid UDP message received, packet length mismatch: expected %d bytes but got %d\n",
					header.length + MBAPBUFSIZE - MBADDRSIZE,
					rc);
			}
			continue;
		}

		datasize = rc - MBAPBUFSIZE + MBADDRSIZE;

		if(verbose) {
			fprintf(stderr,
			    "Received %d byte request, transaction id %d, unit %u, function=0x%02X\n",
			    rc, header.transaction_id, header.unit, data[0]);
		}

		//
		// Build the serial PDU
		//
		count = build_serial_pdu(&header,
				data, datasize,
				serpacket, sizeof(serpacket));

		rc = transaction(  serpacket,
				   count,
				   serresponse,
				   sizeof(serresponse),
				   timeout, 100);
		if(rc==0) {
			if(debug) {
				fprintf(stderr, "No response\n");
			}
			continue;
		} else if(rc<0) {
			if(debug) {
				fprintf(stderr, "transaction() failed, disconnecting\n");
			}
			break;
		} else {
			header.length = MBAPBUFSIZE+MBADDRSIZE;

			count = build_ip_pdu(&header,
				serresponse,
				rc,
				udpbuffer,
				sizeof(udpbuffer));

			if(verbose) {
				fprintf(stderr, "Detected a %d byte response, generating a %d byte packet: \n",
					rc,
					count);
			}



			rc = sendto(fd, udpbuffer, count, 0,
			      (struct sockaddr *) &from, fromlen);

			if(verbose) {
				if(rc < 0)
					perror("sendto()");
				else
					fprintf(stderr, "%d sent\n", rc);
			}

		}




	}

	close(fd);

/*	return 1; */
	pthread_exit(NULL);

}
