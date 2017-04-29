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

struct hw_timestamp {
	struct timespec ts;
	struct timespec sw;
};

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


int sk_tx_timeout = 1000;
static short sk_events = POLLPRI;
static short sk_revents = POLLPRI;

static int _recvpkt(int fd, struct hw_timestamp *hwts, int recvmsg_flags)
{
	struct timespec *ts = NULL;
	struct sockaddr_in sin;
	struct iovec iov;
	struct msghdr msg = {0};
	char ctrl[512] = {};
	char buf[256] = {};
	int level, type;
	int count = 0;
	int res = 0;

	struct cmsghdr *cm;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctrl;
	msg.msg_controllen = sizeof(ctrl);
	msg.msg_name = (caddr_t)&sin;
	msg.msg_namelen = sizeof(sin);

	iov.iov_base = &buf;
	iov.iov_len = sizeof(buf);

	if (recvmsg_flags == MSG_ERRQUEUE) {
		struct pollfd pfd = { fd, sk_events, 0 };
		res = poll(&pfd, 1, sk_tx_timeout);
		if (res < 1) {
			fprintf(stderr, "%s: res=%d: %s\n", "poll: tx timestamp", res, strerror(errno));
			return res;
		} else if (!(pfd.revents & sk_revents)) {
			fprintf(stderr, "%s: %s\n", "poll: non ERR event", strerror(errno));
			return -1;
		}
	}

	count = recvmsg(fd, &msg, recvmsg_flags);
	if (count < 1) {
		fprintf(stderr, "%s: %s\n", "recvmsg", strerror(errno));
	} else {
		printf("recvmsg: success\n");
	}

	for (cm = CMSG_FIRSTHDR(&msg); cm != NULL; cm = CMSG_NXTHDR(&msg, cm)) {
		level = cm->cmsg_level;
		type = cm->cmsg_type;
		if (level == SOL_SOCKET && type == SO_TIMESTAMPING) {
			ts = (struct timespec *)CMSG_DATA(cm);	
		}
		if (level == SOL_SOCKET && type == SO_TIMESTAMPNS) {
			//sw = (struct timespec *)CMSG_DATA(cm);	
			//hwts = *sw;
			;
		}
	}

	printf("ts0: %lld.%.9ld\n", (long long)ts[0].tv_sec, ts[0].tv_nsec);
	printf("ts1: %lld.%.9ld\n", (long long)ts[1].tv_sec, ts[1].tv_nsec);
	printf("ts2: %lld.%.9ld\n", (long long)ts[2].tv_sec, ts[2].tv_nsec);

	return count;
}

static inline bool rx(int sock)
{
	return _recvpkt(sock, NULL, 0);
}

static inline bool read_timestamp(int fd, struct hw_timestamp *hwts)
{

	return _recvpkt(fd, hwts, MSG_ERRQUEUE);
}

int main(int argc, char **argv)
{
	// getopt
	enum _rx_mode { NONE, ALL } rx_mode = NONE;
	enum _tx_mode { OFF, ON } tx_mode = OFF;
	const char *ifname = 0;
	const char *dst = 0;

	unsigned int txsockopt = SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
	struct sockaddr_in addr_tx = {0}, addr_rx = {0};
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

	if (setsockopt(sock_tx, SOL_SOCKET, SO_TIMESTAMPING, (char *)&txsockopt, sizeof(txsockopt)))
		error(1, 0, "setsockopt timestamping");
	if (setsockopt(sock_tx, SOL_SOCKET, SO_SELECT_ERR_QUEUE, (char *)&txsockopt, sizeof(txsockopt)))
		error(1, 0, "setsockopt timestamping");


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

	signal(SIGALRM, &timeout);

	const struct itimerval timer = {.it_value.tv_sec = 1, .it_value.tv_usec = 0};
	const struct itimerval stop = {.it_value.tv_sec = 0, .it_value.tv_usec = 0};
	while (1) {
		struct hw_timestamp hwts0, hwts1;
		uint64_t diff;
		
		// send the packet
		res = tx(sock_tx, (struct sockaddr *)&addr_tx, sizeof(addr_tx));
		if (res) {
			res = read_timestamp(sock_tx, &hwts0);
		}

		// get the current time
//		clock_gettime(CLOCK_MONOTONIC, &ts0);

#if 0
		// set a timeout
		setitimer(ITIMER_REAL, &timer, 0);
		done = false;

		// wait for a replay
		while (likely(done == false)) {  // timeout
			res = rx(sock_rx);
			if (res)                     // success
				res = read_timestamp(sock_rx, &hwts1);
				break;
		}

		// get the current time
//		clock_gettime(CLOCK_MONOTONIC, &ts1);

		// stop the timeout
		setitimer(ITIMER_REAL, &stop, 0);

		// print RTT
		diff = time_diff(&hwts0.ts, &hwts1.ts);
		printf("diff: %ld\n", diff);

#endif
		sleep(1); // todo
	}

	close(sock_tx);
	close(sock_rx);
}
