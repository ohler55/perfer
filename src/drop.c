// Copyright 2016 by Peter Ohler, All Rights Reserved

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "perfer.h"
#include "pool.h"
#include "dtime.h"
#include "drop.h"

static const char	content_length[] = "Content-Length:";

void
drop_init(Drop d, struct _Perfer *h) {
    memset(d, 0, sizeof(struct _Drop));
    d->h = h;
}

void
drop_cleanup(Drop d) {
    if (0 != d->sock) {
	close(d->sock);
    }
    d->sock = 0;
    d->pp = NULL;
    memset(d->pipeline, 0, sizeof(d->pipeline));
    d->phead = 0;
    d->ptail = 0;
    d->rcnt = 0;
    d->xsize = 0;
    *d->buf = '\0';
}

int
drop_pending(Drop d) {
    int	len = d->ptail - d->phead;

    if (len < 0) {
	len += PIPELINE_SIZE;
    }
    return len;
}

void
drop_connect(Drop d, Pool p, struct addrinfo *res) {
    int	optval = 1;

    if (0 > (d->sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) ||
	0 > setsockopt(d->sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) ||
	0 > connect(d->sock, res->ai_addr, res->ai_addrlen)) {
	printf("*-*-* error opening socket: %s\n", strerror(errno));
	p->err_cnt++;
	p->finished = true;
	p->actual_end = dtime();
    } else {
	fcntl(d->sock, F_SETFL, O_NONBLOCK);
	d->rcnt = 0;
    }
}

void
drop_send(Drop d, Pool p) {
    if (PIPELINE_SIZE - 1 <= drop_pending(d)) {
	return;
    }
    struct _Perfer	*perf = d->h;
    int			scnt;

    // TBD allow partial sends by checking return
    if (d->h->replace) {
	char	body[16384];
	char	seq_buf[16];
	char	*s = perf->req_body;
	int	prev = 0;
	int	off = 0;
	int	seq = atomic_fetch_add(&perf->seq, 1);

	snprintf(seq_buf, sizeof(seq_buf), "%011d", seq);
	while (NULL != (s = strstr(perf->req_body + prev, "${sequence}"))) {
	    off = s - perf->req_body;
	    memcpy(body + prev, perf->req_body + prev, off - prev);
	    memcpy(body + off, seq_buf, 11);
	    off += 11;
	    prev = off;
	}
	strcpy(body + prev, perf->req_body + prev);
	scnt = send(d->sock, body, perf->req_len, 0);
    } else {
	scnt = send(d->sock, perf->req_body, perf->req_len, 0);
    }
    if (perf->req_len != scnt) {
	printf("*-*-* error sending request: %s\n", strerror(errno));
	p->err_cnt++;
	drop_cleanup(d);
    } else {
	p->sent_cnt++;
	d->pipeline[d->ptail] = dtime();
	d->ptail++;
	if (PIPELINE_SIZE <= d->ptail) {
	    d->ptail = 0;
	}
    }
}

void
drop_recv(Drop d, Pool p, bool enough) {
    ssize_t	rcnt;

    if (0 > (rcnt = recv(d->sock, d->buf + d->rcnt, sizeof(d->buf) - d->rcnt - 1, 0))) {
	printf("*-*-* error reading response on %d: %s\n", d->sock, strerror(errno));
	drop_cleanup(d);
	return;
    }
    d->rcnt += rcnt;
    d->buf[d->rcnt] = '\0';


    // TBD loop until all processed
    while (true) {
	if (0 >= d->xsize) {
	    char	*hend = strstr(d->buf, "\r\n\r\n");
	    char	*cl = strstr(d->buf, content_length);

	    if (NULL == hend) {
		return;
	    }
	    if (NULL == cl) {
		d->xsize = hend - d->buf + 4;
	    } else {
		cl += sizeof(content_length);
		for (; ' ' == *cl; cl++) {
		}
		char	*end;
		long	len = strtol(cl, &end, 10);

		if ('\r' != *end) {
		    printf("*-*-* error reading content length on %d.\n", d->sock);
		    drop_cleanup(d);
		    return;
		}
		d->xsize = hend - d->buf + 4 + len;
	    }
	    if (0 >= d->xsize) {
		break;
	    }
	}
	if (d->rcnt <= d->xsize) {
	    double	dt = dtime() - d->pipeline[d->phead];
	
	    p->ok_cnt++;
	    d->pipeline[d->phead] = 0.0;
	    d->phead++;
	    if (PIPELINE_SIZE <= d->phead) {
		d->phead = 0;
	    }
	    p->lat_sum += dt;
	    /* TBD debugging, uses something better than a simple average for latency analysis.
	       if (0.01 < dt && p->lat_sum * 2.0 / p->ok_cnt < dt) {
	       printf("*** long latency: %0.3f msecs\n", dt * 1000.0);
	       }
	    */
	    if (d->h->verbose) {
		d->buf[d->xsize] = '\0';
		printf("%s\n", d->buf);
	    }
	    if (enough || !d->h->keep_alive) {
		drop_cleanup(d);
	    } else {
		if ( d->xsize < d->rcnt) {
		    memmove(d->buf, d->buf + d->xsize, d->rcnt - d->xsize);
		    d->rcnt -= d->xsize;
		} else {
		    d->rcnt = 0;
		    break;
		}
		d->xsize = 0;
	    }
	}
    }
}
