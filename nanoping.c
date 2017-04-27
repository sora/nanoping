#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <assert.h>
#include <error.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <signal.h>

#include <net/if.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/errqueue.h>

#include "hwtstamp_config.h"


#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

#ifndef MSG_ERRQUEUE    // for debug
#define MSG_ERRQUEUE    0x2000
#endif

//#define NDEBUG

static void __attribute__((noreturn)) usage(const char *name)
{
	printf("\n%s\n", name);
	printf("\t -i interface\n");
	printf("\t -d destination IPv4 address\n");
	printf("\t -H Hardware timestamp mode: <tx or rx or both>\n");
	printf("\n");

	exit(1);
}

static inline uint64_t time_diff(const struct timespec *ts0, const struct timespec *ts1)
{
	return ((1e9 * (ts1->tv_sec - ts0->tv_sec)) + (ts1->tv_nsec - ts0->tv_nsec));
}

static bool done = false;

static void timeout(int signum)
{
	done = true;
}

static bool tx(int sock, struct sockaddr *addr, socklen_t addr_len)
{
	int res;

	res = sendto(sock, "HELLO", 5, 0, addr, addr_len);
	if (res < 0)
		fprintf(stderr, "%s: %s\n", "sendto", strerror(errno));
	else
		printf("sendto: success\n");

	return true;
}

static void print_timestamp(struct scm_timestamping *tss, int tstype, int tskey, int payload_len)
{
	struct timespec *cur = &tss->ts[0];

	if (!(cur->tv_sec | cur->tv_nsec))
		return;

	fprintf(stderr, "  %lu s %lu us (seq=%u, len=%u)",
		cur->tv_sec, cur->tv_nsec / 1000, tskey, payload_len);
	
}

static void _recv_errmsg_cmsg(struct msghdr *msg, int payload_len)
{
	struct sock_extended_err *serr = NULL;
	struct scm_timestamping *tss = NULL;
	struct cmsghdr *cm;
	int batch = 0;
	
	for (cm = CMSG_FIRSTHDR(msg); cm && cm->cmsg_len; cm = CMSG_NXTHDR(msg, cm)) {
		if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_TIMESTAMPING) {
			tss = (void *) CMSG_DATA(cm);
		} else if ((cm->cmsg_level == SOL_IP && cm->cmsg_type == IP_RECVERR) ||
			   (cm->cmsg_level == SOL_IPV6 && cm->cmsg_type == IPV6_RECVERR)) {
			serr = (void *) CMSG_DATA(cm);
			if (serr->ee_errno != ENOMSG || serr->ee_origin != SO_EE_ORIGIN_TIMESTAMPING) {
				fprintf(stderr, "unknown ip error %d %d\n", serr->ee_errno, serr->ee_origin);
				serr = NULL;
			}
		} else {
			fprintf(stderr, "unknown cmsg %d,%d\n", cm->cmsg_level, cm->cmsg_type);
		}

		if (serr && tss) {
			print_timestamp(tss, serr->ee_info, serr->ee_data, payload_len);
			serr = NULL;
			tss = NULL;
			batch++;
		}
	}
	if (batch > 1)
		fprintf(stderr, "batched %d timestamps\n", batch);

}

static int _recvpkt(int sock, struct timespec *ts, int recvmsg_flags)
{
	struct sockaddr_in sin;
	struct iovec entry;
	struct msghdr msg = {0};
	char buf[256];
	struct {
		struct cmsghdr cmsg;
		char cbuf[512];
	} ctrl;
	int res = 0;

	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	entry.iov_base = buf;
	entry.iov_len = sizeof(buf);
	msg.msg_name = (caddr_t)&sin;
	msg.msg_namelen = sizeof(sin);
	msg.msg_control = &ctrl;
	msg.msg_controllen = sizeof(ctrl);

	res = recvmsg(sock, &msg, MSG_ERRQUEUE);
	if (res < 0) {
		fprintf(stderr, "%s: %s\n", "recvmsg", strerror(errno));
	} else {
		printf("recvmsg: success\n");
		_recv_errmsg_cmsg(&msg, res);
	}

	return res == -1;
}

static inline bool rx(int sock)
{
	return _recvpkt(sock, NULL, 0);
}

static inline bool read_timestamp(int sock, struct timespec *ts)
{

	return _recvpkt(sock, ts, MSG_ERRQUEUE);
}

static void __poll(int fd)
{
	struct pollfd pollfd;
	int ret;

	memset(&pollfd, 0, sizeof(pollfd));
	pollfd.fd = fd;
	ret = poll(&pollfd, 1, 100);
	if (ret != 1) {
		printf("hoge\n");
		error(1, errno, "poll");
	}
}

int main(int argc, char **argv)
{
	// getopt
	enum _rx_mode { NONE, ALL } rx_mode = NONE;
	enum _tx_mode { OFF, ON } tx_mode = OFF;
	const char *ifname = 0;
	const char *dst = 0;

	struct sockaddr_in addr_tx = {0}, addr_rx = {0};
	const int nonblocking = 1;
	int sock_tx, sock_rx;
	struct ifreq dev;
	int res;

	int ch;
	while ((ch = getopt(argc, argv, "i:d:H:")) != -1) {
		switch (ch) {
			case 'i':
				ifname = optarg;
				break;
			case 'd':
				dst = optarg;
				break;
			case 'H':
				if (strcmp("tx", optarg) == 0) {
					rx_mode = NONE;
					tx_mode = ON;
				} else if (strcmp("rx", optarg) == 0) {
					rx_mode = ALL;
					tx_mode = OFF;
				} else if (strcmp("both", optarg) == 0) {
					rx_mode = ALL;
					tx_mode = ON;
				} else {
					printf("none\n");
					fprintf(stderr, "%s: %s\n", "-H", "Unknown mode");
					exit(1);
				}
				break;
			default:
				usage(argv[0]);
		}
	}
	if (ifname == 0 || dst == 0) {
		usage(argv[0]);
	}

	// hw_config
	printf("txmode: %d, rxmode: %d\n", tx_mode, rx_mode);
	res = hwtstamp_config_set(ifname, tx_mode, rx_mode);
	if (res < 0) {
		fprintf(stderr, "%s: %s\n", "hwtstamp_config_set", strerror(errno));
		return 1;
	}

	// tx	
	sock_tx = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_tx < 0) {
		fprintf(stderr, "%s: %s\n", "socket", strerror(errno));
		exit(1);
	}

	memset(&dev, 0, sizeof(dev));
	strncpy(dev.ifr_name, ifname, sizeof(dev.ifr_name));
	if (ioctl(sock_tx, SIOCGIFADDR, &dev) < 0) {
		fprintf(stderr, "%s: %s\n", "ioctl: SIOCGIFADDR", strerror(errno));
		exit(1);
	}

	addr_tx.sin_family = AF_INET;
	addr_tx.sin_port = htons(12345);
	addr_tx.sin_addr.s_addr = inet_addr(dst);

	// rx
	sock_rx = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_rx < 0) {
		fprintf(stderr, "%s: %s\n", "socket", strerror(errno));
		exit(1);
	}

	addr_rx.sin_family = AF_INET;
	addr_rx.sin_port = htons(12345);
	addr_rx.sin_addr.s_addr = INADDR_ANY;

	bind(sock_rx, (struct sockaddr *)&addr_rx, sizeof(addr_rx));

	ioctl(sock_rx, FIONBIO, &nonblocking);

	signal(SIGALRM, &timeout);

	const struct itimerval timer = {.it_value.tv_sec = 1, .it_value.tv_usec = 0};
	const struct itimerval stop = {{0}};
	while (1) {
		struct timespec ts0, ts1;
		uint64_t diff;
		
		// send the packet
		res = tx(sock_tx, (struct sockaddr *)&addr_tx, sizeof(addr_tx));
		if (res) {
			usleep(50 * 1000);
			__poll(sock_tx);
			while (!(res = read_timestamp(sock_tx, &ts0))) {}
		}

		// get the current time
		clock_gettime(CLOCK_MONOTONIC, &ts0);

		// set a timeout
		setitimer(ITIMER_REAL, &timer, 0);
		done = false;

		// wait for a replay
		while (likely(done == false)) {  // timeout
			res = rx(sock_rx);
			if (res)                     // success
				res = read_timestamp(sock_rx, &ts1);
				break;
		}

		// get the current time
		clock_gettime(CLOCK_MONOTONIC, &ts1);

		// stop the timeout
		setitimer(ITIMER_REAL, &stop, 0);

		// print RTT
		diff = time_diff(&ts0, &ts1);
		printf("diff: %ld\n", diff);

		sleep(1);
	}

	close(sock_tx);
	close(sock_rx);
}
