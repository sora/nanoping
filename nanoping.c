#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>
#include <time.h>

#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <net/if.h>

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

//#define NDEBUG

static void __attribute__((noreturn)) usage(const char *name)
{
	printf("\n%s\n", name);
	printf("\t -i interface\n");
	printf("\t -d destination IPv4 address\n");
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
		printf("success\n");

	return true;
}

static bool rx(int sock)
{
	return true;
}

static void read_timestamp(int sock, struct timespec *ts)
{
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
	return;
}

int main(int argc, char **argv)
{
	const char *ifname = 0;
	const char *dst = 0;
	struct sockaddr_in addr_tx = {0}, addr_rx = {0};
	const int nonblocking = 1;
	int sock_tx, sock_rx;
	struct ifreq dev;
	int res;

	int ch;
	while ((ch = getopt(argc, argv, "i:d:")) != -1) {
		switch (ch) {
			case 'i':
				ifname = optarg;
				break;
			case 'd':
				dst = optarg;
				break;
			default:
				usage(argv[0]);
		}
	}

	if (ifname == 0 || dst == 0) {
		usage(argv[0]);
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
		if (res == true)
			read_timestamp(sock_tx, &ts0);

		// get the current time
		clock_gettime(CLOCK_MONOTONIC, &ts0);

		// set a timeout
		setitimer(ITIMER_REAL, &timer, 0);
		done = false;

		// wait for a replay
		while (likely(done == false)) {  // timeout
			res = rx(sock_rx);
			if (res == true)  // success
				read_timestamp(sock_rx, &ts1);
				break;
		}

		// get the current time
		clock_gettime(CLOCK_MONOTONIC, &ts1);

		// stop the timeout
		setitimer(ITIMER_REAL, &stop, 0);

		// print RTT
		diff = time_diff(&ts0, &ts1);
		printf("diff: %lld\n", diff);

		sleep(1);
	}

	close(sock_tx);
	close(sock_rx);
}
