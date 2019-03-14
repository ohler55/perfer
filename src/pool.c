// Copyright 2016 by Peter Ohler, All Rights Reserved

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "dtime.h"
#include "drop.h"
#include "perfer.h"
#include "pool.h"

// Returns addrinfo for a host[:port] string with the default port of 80.
static struct addrinfo*
get_addr_info(const char *host, const char *port) {
    struct addrinfo	hints;
    struct addrinfo	*res;
    int			err;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (NULL == port) {
	err = getaddrinfo(host, "80", &hints, &res);
    } else {
	err = getaddrinfo(host, port, &hints, &res);
    }
    if (0 != err) {
	printf("*-*-* Failed to resolve %s.\n", host);
	return NULL;
    }
    return res;
}

static void*
loop(void *x) {
    Pool		p = (Pool)x;
    Perfer		h = p->perfer;
    Drop		d;
    struct addrinfo	*res = get_addr_info(h->addr, h->port);

    if (NULL == res) {
	perfer_stop(h);
	return NULL;
    }
    while (!h->done) {
	if (NULL == (d = queue_pop(&h->q, 0.00001))) {
	    continue;
	}
	if (drop_recv(d, p)) {
	    continue;
	}
	while (drop_pending(d) < h->backlog && !h->enough) {
	    drop_send(d, p);
	    // TBD if return of EAGAIN then put on queue again
	}
    }
    freeaddrinfo(res);
    p->finished = true;

    return NULL;
}

int
pool_start(Pool p, Perfer perfer) {
    p->finished = false;
    p->perfer = perfer;

    return pthread_create(&p->thread, NULL, loop, p);
}

void
pool_wait(Pool p) {
    // Wait for thread to finish. Join is not used as we want to kill the thread
    // if it does not exit correctly.
    pthread_detach(p->thread); // cleanup thread resources when completed
    if (!p->finished) {
	double  late = dtime() + p->perfer->duration + 2.0;

	while (!p->finished && dtime() < late) {
	    dsleep(0.5);
        }
	pthread_cancel(p->thread);
    }
}
