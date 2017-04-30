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

#define pr_info(S, ...)   printf("\x1b[1m\x1b[94minfo:\x1b[0m " S "\n", ##__VA_ARGS__)
#define pr_err(S, ...)    fprintf(stderr, "\x1b[1m\x1b[31merror:\x1b[0m " S "\n", ##__VA_ARGS__)
#define pr_warn(S, ...)   if(warn) fprintf(stderr, "\x1b[1m\x1b[33mwarn :\x1b[0m " S "\n", ##__VA_ARGS__)
#define pr_debug(S, ...)  if (debug) fprintf(stderr, "\x1b[1m\x1b[90mdebug:\x1b[0m " S "\n", ##__VA_ARGS__)


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
	printf("\t -C ping mode\n");
	printf("\t -D pong mode\n");
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

static ssize_t tx(int sock, struct sockaddr *addr, socklen_t addr_len)
{
	ssize_t count;

	count = sendto(sock, "HELLO", 5, 0, addr, addr_len);
	if (count < 1)
		pr_err("%s: %s", "sendto", strerror(errno));
	else
		pr_info("sendto: success");

	return count;
}


static ssize_t _recvpkt(int fd, struct hw_timestamp *hwts, int recvmsg_flags)
{
	struct timespec *ts = NULL;
	struct sockaddr_in sin;
	struct iovec iov;
	struct msghdr msg = {0};
	char ctrl[512] = {};
	char buf[256] = {};
	int level, type;
	ssize_t count;
	int res;

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
		struct pollfd pfd = { fd, POLLPRI, 0 };
		res = poll(&pfd, 1, 1);
		if (res < 1) {
			pr_err("%s: res=%d: %s", "poll: tx timestamp", res, strerror(errno));
			return res;
		} else if (!(pfd.revents & POLLPRI)) {
			pr_err("%s: %s", "poll: non ERR event", strerror(errno));
			return -1;
		}
	}

	count = recvmsg(fd, &msg, recvmsg_flags);
	if (count < 1) {
		pr_err("%s: %s", "recvmsg", strerror(errno));
	} else {
		pr_info("recvmsg: success");
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

	pr_info("ts0: %lld.%.9ld", (long long)ts[0].tv_sec, ts[0].tv_nsec);
	pr_info("ts1: %lld.%.9ld", (long long)ts[1].tv_sec, ts[1].tv_nsec);
	pr_info("ts2: %lld.%.9ld", (long long)ts[2].tv_sec, ts[2].tv_nsec);
	pr_info("ts3: %lld.%.9ld", (long long)ts[3].tv_sec, ts[3].tv_nsec);
	pr_info("ts4: %lld.%.9ld", (long long)ts[4].tv_sec, ts[4].tv_nsec);
	pr_info("ts5: %lld.%.9ld", (long long)ts[5].tv_sec, ts[5].tv_nsec);
	pr_info("ts6: %lld.%.9ld", (long long)ts[6].tv_sec, ts[6].tv_nsec);

	return count;
}

static inline ssize_t rx(int sock)
{
	return _recvpkt(sock, NULL, 0);
}

static inline ssize_t read_timestamp(int fd, struct hw_timestamp *hwts)
{

	return _recvpkt(fd, hwts, MSG_ERRQUEUE);
}

static int txsock_init(const char *ifname, const char *dst, struct sockaddr_in *addr)
{
	unsigned int txskopt = SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
	struct ifreq dev;
	int fd;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		pr_err("%s: %s", "socket", strerror(errno));
		goto err;
	}

	memset(&dev, 0, sizeof(dev));
	strncpy(dev.ifr_name, ifname, sizeof(dev.ifr_name));
	if (ioctl(fd, SIOCGIFADDR, &dev) < 0) {
		pr_err("%s: %s", "ioctl: SIOCGIFADDR", strerror(errno));
		goto err;
	}

	addr->sin_family = AF_INET;
	addr->sin_port = htons(12345);
	addr->sin_addr.s_addr = inet_addr(dst);

	if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, (char *)&txskopt, sizeof(txskopt))) {
		pr_err("%s: %s", "setsockopt timestamping", strerror(errno));
	}	
	if (setsockopt(fd, SOL_SOCKET, SO_SELECT_ERR_QUEUE, (char *)&txskopt, sizeof(txskopt))) {
		pr_err("%s: %s", "setsockopt timestamping", strerror(errno));
	}

err:
	return fd;
}

static int rxsock_init(const char *ifname, struct sockaddr_in *addr)
{
	int fd;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		pr_err("%s: %s", "socket", strerror(errno));
		goto err;
	}

	addr->sin_family = AF_INET;
	addr->sin_port = htons(12345);
	addr->sin_addr.s_addr = INADDR_ANY;

	bind(fd, (struct sockaddr *)&addr, sizeof(addr));

err:
	return fd;
}

int main(int argc, char **argv)
{
	// getopt
	enum _ping_mode { NO_MODE, PING_MODE, PONG_MODE } ping_mode = NO_MODE;
	enum _rx_mode { NONE, ALL } rx_mode = NONE;
	enum _tx_mode { OFF, ON } tx_mode = OFF;
	const char *ifname = 0;
	const char *dst = 0;

	struct sockaddr_in addr_tx, addr_rx;
	int sock_tx, sock_rx;
	int res;

	int ch;
	while ((ch = getopt(argc, argv, "CDi:d:H:")) != -1) {
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
					pr_err("%s: %s", "-H", "Unknown mode");
					exit(1);
				}
				break;
			case 'C':
				if (ping_mode == PONG_MODE)
					usage(argv[0]);
				ping_mode = PING_MODE;
				break;
			case 'D':
				if (ping_mode == PING_MODE)
					usage(argv[0]);
				ping_mode = PONG_MODE;
				break;
			default:
				usage(argv[0]);
		}
	}
	if (ifname == 0 || dst == 0 || ping_mode == NO_MODE) {
		usage(argv[0]);
	}

	// hw_config
	res = hwtstamp_config_set(ifname, tx_mode, rx_mode);
	if (res < 0) {
		pr_err("%s: %s", "hwtstamp_config_set", strerror(errno));
		return 1;
	}

	// tx setup
	sock_tx = txsock_init(ifname, dst, &addr_tx);
	if (sock_tx < 0) {
		pr_err("%s: %s", "tx socket", strerror(errno));
		exit(1);
	}

	// rx setup
	sock_rx = rxsock_init(ifname, &addr_rx);
	if (sock_rx < 0) {
		pr_err("%s: %s", "rx socket", strerror(errno));
		exit(1);
	}

	signal(SIGALRM, &timeout);

	const struct itimerval timer = {.it_value.tv_sec = 1, .it_value.tv_usec = 0};
	const struct itimerval stop = {.it_value.tv_sec = 0, .it_value.tv_usec = 0};
	while (1) {
		struct hw_timestamp hwts0, hwts1;
		uint64_t diff;
		ssize_t count;
		
		// send the packet
		count = tx(sock_tx, (struct sockaddr *)&addr_tx, sizeof(addr_tx));
		if (count > 0) {
			count = read_timestamp(sock_tx, &hwts0);
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

