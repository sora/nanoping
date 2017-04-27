#ifndef _HWTSTAMP_CONFIG_H_
#define _HWTSTAMP_CONFIG_H_

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/if.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>

int hwtstamp_config_set(const char *, int, int);
int hwtstamp_config_get(const char *, int *, int *);

#endif  /* _HWTSTAMP_CONFIG_H_ */

