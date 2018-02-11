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
pool_init(Pool p, struct _Perfer *h, long num) {
    p->finished = false;
    p->perfer = h;
    p->num = num;
    p->dcnt = h->ccnt;
    p->sent_cnt = 0;
    p->err_cnt = 0;
    p->ok_cnt = 0;
    p->lat_sum = 0.0;
    p->actual_end = 0.0;
    if (NULL == (p->drops = (Drop)malloc(sizeof(struct _Drop) * p->dcnt))) {
	printf("-*-*- Failed to allocate %d connections.\n", p->dcnt);
	return -1;
    }
    Drop	d;
    int		i;
    int		err;

    for (d = p->drops, i = p->dcnt; 0 < i; i--, d++) {
	if (0 != (err = drop_init(d, h))) {
	    return err;
	}
    }
    return 0;
}

void
pool_cleanup(Pool p) {
    Drop	d;
    int		i;

    for (d = p->drops, i = p->dcnt; 0 < i; i--, d++) {
	drop_cleanup(d);
    }
    free(p->drops);
}

// Returns addrinfo for a host[:port] string with the default port of 80.
static struct addrinfo*
get_addr_info(const char *addr) {
    struct addrinfo	hints;
    struct addrinfo	*res;
    char		host[1024];
    const char		*port;
    int			err;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (NULL == (port = strchr(addr, ':'))) {
	err = getaddrinfo(addr, "80", &hints, &res);
    } else if (sizeof(host) <= port - addr - 1) {
	printf("*-*-* Host name too long: %s.\n", addr);
	return NULL;
    } else {
	strncpy(host, addr, port - addr);
	port++;
	if (0 != (err = getaddrinfo(host, port, &hints, &res))) {
	    printf("*-*-* Failed to resolve %s. %s\n", addr, gai_strerror(err));
	    return NULL;
	}
    }
    if (0 != err) {
	printf("*-*-* Failed to resolve %s.\n", addr);
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
    struct addrinfo	*res = get_addr_info(h->addr);
    double		end_time;
    double		now;
    double		dt;
    bool		enough = false;
    ssize_t		rcnt;
    ssize_t		xsize = 0; // expected size of response/page
    int			sock;
    int			optval = 1;
    char		target[16384];
    char		buf[16384];
    int			sent_cnt;

    if (NULL == res) {
	perfer_stop(h);
	return NULL;
    }
    // Preload the page to check the expected size. This should be a blocking
    // connection and recv.
    if (0 > (sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol))) {
	printf("*-*-* error creating socket: %s\n", strerror(errno));
	p->finished = true;
	return NULL;
    }
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

    if (0 > connect(sock, res->ai_addr, res->ai_addrlen)) {
	printf("*-*-* error connecting socket: %s\n", strerror(errno));
	close(sock);
	p->finished = true;
	return NULL;
    }
    if (h->replace) {
	char	body[16384];
	char	seq_buf[16];
	char	*s = h->req_body;
	int	prev = 0;
	int	off = 0;
	int	seq = atomic_fetch_add(&h->seq, 1);

	snprintf(seq_buf, sizeof(seq_buf), "%011d", seq);
	while (NULL != (s = strstr(h->req_body + prev, "${sequence}"))) {
	    off = s - h->req_body;
	    memcpy(body + prev, h->req_body + prev, off - prev);
	    memcpy(body + off, seq_buf, 11);
	    off += 11;
	    prev = off;
	}
	strcpy(body + prev, h->req_body + prev);
	sent_cnt = send(sock, body, h->req_len, 0);
    } else {
	sent_cnt = send(sock, h->req_body, h->req_len, 0);
    }
    if (h->req_len != sent_cnt) {
	printf("*-*-* error sending request: %s\n", strerror(errno));
	close(sock);
	p->finished = true;
	return NULL;
    }
    while (0 < (rcnt = recv(sock, target, sizeof(target) - 1, 0))) {
	xsize += rcnt;
	break; // TBD only needed for keep-alive in theory and then could fail for longer replies.
    }
    if (0 >= xsize) {
	printf("*-*-* failed to GET %s from %s.\n", h->path, h->addr);
	p->finished = true;
	return NULL;
    }
    target[xsize] = '\0';
    close(sock);
    if (h->verbose) {
	printf("response: '%s'\n", target);
    }
    if (h->tcnt - 1 == atomic_fetch_add(&h->ready_cnt, 1)) {
	h->start_time = dtime();
    }
    while (atomic_load(&h->ready_cnt) < h->tcnt) {
	dsleep(0.01);
    }
    end_time = h->start_time + h->duration;
    p->actual_end = end_time;
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
		if (0 > (d->sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) ||
		    0 > setsockopt(d->sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) ||
		    0 > connect(d->sock, res->ai_addr, res->ai_addrlen)) {
		    printf("*-*-* error opening socket: %s\n", strerror(errno));
		    p->err_cnt++;
		    p->finished = true;
		    p->actual_end = dtime();
		    return NULL;
		}
		fcntl(d->sock, F_SETFL, O_NONBLOCK);
		d->sent = false;
		d->rcnt = 0;
		// A connection is established but the socket may not be
		// writable for a while so start latency timing when the socket
		// can be sent on.
	    }
	    if (0 < d->sock) {
		pp->fd = d->sock;
		d->pp = pp;
		// Errors will show up on read and write so don't bother
		// checking in the poll.
		if (d->sent) {
		    pp->events = POLLERR | POLLIN;
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
	if (0 > (i = poll(ps, pp - ps, 10))) {
	    if (EAGAIN == errno) {
		continue;
	    }
	    printf("*-*-* polling error: %s\n", strerror(errno));
	    break;
	}
	if (0 == i) {
	    continue;
	}
	now = dtime();
	for (d = p->drops, i = pcnt; 0 < i; i--, d++) {
	    if (NULL == d->pp || 0 == d->pp->revents || 0 == d->sock) {
		continue;
	    }
	    if (0 != (d->pp->revents & POLLIN)) {
		if (0 > (rcnt = recv(d->sock, buf + d->rcnt, xsize - d->rcnt, 0))) {
		    printf("*-*-* error reading response on %d: %s\n", d->sock, strerror(errno));
		    drop_cleanup(d);
		    continue;
		}
		d->rcnt += rcnt;
		if (0 == rcnt || xsize <= d->rcnt) {
		    p->ok_cnt++;
		    dt = now - d->start;
		    p->lat_sum += dt;
		    /* TBD debugging, uses something better than a simple average for latency analysis.
		    if (0.01 < dt && p->lat_sum * 2.0 / p->ok_cnt < dt) {
			printf("*** long latency: %0.3f msecs\n", dt * 1000.0);
		    }
		    */
		    if (h->verbose) {
			buf[d->rcnt] = '\0';
			if (0 != strncmp(target, buf, xsize)) {
			    buf[xsize] = '\0';
			    printf("*-*-* response mismatch \n%s\n", buf);
			    p->err_cnt++;
			    drop_cleanup(d);
			    continue;
			}
		    }
		    if (enough || !h->keep_alive) {
			drop_cleanup(d);
		    } else {
			int	ds = d->sock;

			drop_init(d, h);
			d->sock = ds;
			d->start = now;
		    }
		}
	    } else if (!enough && 0 != (d->pp->revents & POLLOUT)) {
		if (!d->sent) { // not really needed
		    // TBD allow partial sends by checking return
		    if (h->replace) {
			char	body[16384];
			char	seq_buf[16];
			char	*s = h->req_body;
			int	prev = 0;
			int	off = 0;
			int	seq = atomic_fetch_add(&h->seq, 1);

			snprintf(seq_buf, sizeof(seq_buf), "%011d", seq);
			while (NULL != (s = strstr(h->req_body + prev, "${sequence}"))) {
			    off = s - h->req_body;
			    memcpy(body + prev, h->req_body + prev, off - prev);
			    memcpy(body + off, seq_buf, 11);
			    off += 11;
			    prev = off;
			}
			strcpy(body + prev, h->req_body + prev);
			sent_cnt = send(d->sock, body, h->req_len, 0);
		    } else {
			sent_cnt = send(d->sock, h->req_body, h->req_len, 0);
		    }
		    if (h->req_len != sent_cnt) {
			printf("*-*-* error sending request: %s\n", strerror(errno));
			p->err_cnt++;
			drop_cleanup(d);
			continue;
		    }
		    d->sent = true;
		    p->sent_cnt++;
		    d->start = now;
		}
	    } else if (0 != (d->pp->revents & POLLERR)) {
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
