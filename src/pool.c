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

int
pool_init(Pool p, struct _perfer *h, long num) {
    p->finished = false;
    p->perfer = h;
    p->num = num;
    p->dcnt = h->ccnt;
    p->max_pending = 0;
    p->con_cnt = 0;
    p->sent_cnt = 0;
    p->err_cnt = 0;
    p->ok_cnt = 0;
    p->lat_sum = 0.0;
    p->lat_sq_sum = 0.0;
    p->actual_end = 0.0;
    if (NULL == (p->drops = (Drop)malloc(sizeof(struct _drop) * p->dcnt))) {
	printf("-*-*- Failed to allocate %d connections.\n", p->dcnt);
	return -1;
    }
    Drop	d;
    int		i;

    for (d = p->drops, i = p->dcnt; 0 < i; i--, d++) {
	drop_init(d, h);
    }
    return 0;
}

void
pool_cleanup(Pool p) {
/*
    Drop	d;
    int		i;

    pthread_join(p->thread, NULL);
    for (d = p->drops, i = p->dcnt; 0 < i; i--, d++) {
	drop_cleanup(d);
    }
    free(p->drops);
*/
}

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
    int			i;
    int			pcnt = h->ccnt;
    struct pollfd	ps[pcnt];
    struct pollfd	*pp;
    struct addrinfo	*res = get_addr_info(h->addr, h->port);
    double		end_time;
    double		now;
    bool		enough = false;
    int			pending;
    int			pt = h->poll_timeout;
    double		sum = 0.0;
    double		start;
    double		msum = 0.0;
    long		mcnt = 0;
    long		total = 0;
    long		ready = 0;
    pthread_mutex_t	mu;

    pthread_mutex_init(&mu, 0);

    if (NULL == res) {
	perfer_stop(h);
	return NULL;
    }
    if (h->tcnt - 1 == atomic_fetch_add(&h->ready_cnt, 1)) {
	h->start_time = dtime();
    }
    while (atomic_load(&h->ready_cnt) < h->tcnt) {
	dsleep(0.01);
    }
    end_time = h->start_time + h->duration;
    p->actual_end = end_time;
LOOP:
    while (!h->done) {
	now = dtime();
	if (0 < p->num) {
	    if (p->num <= p->sent_cnt) {
		enough = true;
	    }
	} else if (end_time <= now) {
	    enough = true;
	}
	// If sock is 0 then try to connect and add to pollfd else just add to
	// pollfd.
	for (d = p->drops, i = pcnt, pp = ps; 0 < i; i--, d++) {
	    if (0 == d->sock && !enough) {
		if (drop_connect(d, p, res)) {
		    // Failed to connect. Abort the test.
		    perfer_stop(h);
		    goto LOOP;
		}
		p->con_cnt++;
	    }
	    if (0 < d->sock) {
		pp->fd = d->sock;
		d->pp = pp;
		pending = drop_pending(d);
		if (0 < pending) {
		    pp->events = POLLERR | POLLIN | POLLOUT;
		} else if (!enough) {
		    pp->events = POLLERR | POLLOUT;
		}
		pp->revents = 0;
		pp++;
	    }
	}
	if (pp == ps) {
	    break;
	}
	start = dtime();
	pthread_mutex_lock(&mu);
	pthread_mutex_unlock(&mu);
	msum += dtime() - start;
	start = dtime();
	if (0 > (i = poll(ps, pp - ps, pt))) {
	    sum += dtime() - start;
	    mcnt++;
	    if (EAGAIN == errno) {
		continue;
	    }
	    printf("*-*-* polling error: %s\n", strerror(errno));
	    break;
	}
	sum += dtime() - start;
	mcnt++;
	if (0 == i) {
	    continue;
	}
	for (d = p->drops, i = pcnt; 0 < i; i--, d++) {
	    total++;
	    if (NULL == d->pp || 0 == d->pp->revents || 0 == d->sock) {
		continue;
	    }
	    if (0 != (d->pp->revents & POLLIN)) {
		if (drop_recv(d, p, enough)) {
		    continue;
		}
	    }
	    if (!enough && 0 != (d->pp->revents & POLLOUT)) {
		if (!(d->h->backlog <= drop_pending(d))) {
		    ready++;
		}
		if (drop_send(d, p)) {
		    continue;
		}
	    }
	    if (0 != (d->pp->revents & POLLERR)) {
		p->err_cnt++;
		drop_cleanup(d);
	    }
	}
    }
    p->actual_end = dtime();
    int	cnt = 0;
    for (d = p->drops, i = pcnt; 0 < i; i--, d++) {
	if (0 < d->sock) {
	    cnt++;
	}
    }
    freeaddrinfo(res);
    p->finished = true;
    printf("*** poll time: %f mutex: %f\n", sum * 1000.0 / mcnt, msum * 1000.0 / mcnt);
    return NULL;
}

int
pool_start(Pool p) {
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
