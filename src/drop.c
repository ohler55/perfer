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
static const char	transfer_encoding[] = "Transfer-Encoding:";

void
drop_init(Drop d, struct _perfer *h) {
    memset(d, 0, sizeof(struct _drop));
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

// Return non-zero on error.
int
drop_connect(Drop d, Pool p, struct addrinfo *res) {
    int	optval = 1;

    if (0 > (d->sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) ||
	0 > setsockopt(d->sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) ||
	0 > connect(d->sock, res->ai_addr, res->ai_addrlen)) {
	printf("*-*-* error opening socket: %s\n", strerror(errno));
	p->err_cnt++;
	p->finished = true;
	p->actual_end = dtime();
	return errno;
    }
    fcntl(d->sock, F_SETFL, O_NONBLOCK);
    d->rcnt = 0;

    return 0;
}

bool
drop_send(Drop d, Pool p) {
    if (d->h->backlog <= drop_pending(d)) {
	return false;
    }
    struct _perfer	*perf = d->h;
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
	return true;
    }
    p->sent_cnt++;
    d->pipeline[d->ptail] = dtime();
    d->ptail++;
    if (PIPELINE_SIZE <= d->ptail) {
	d->ptail = 0;
    }
    return false;
}

bool
drop_recv(Drop d, Pool p, bool enough) {
    ssize_t	rcnt;
    long	hsize = 0;

    if (0 > (rcnt = recv(d->sock, d->buf + d->rcnt, sizeof(d->buf) - d->rcnt - 1, 0))) {
	printf("*-*-* error reading response on %d: %s\n", d->sock, strerror(errno));
	drop_cleanup(d);
	return true;
    }
    d->rcnt += rcnt;
    //d->buf[d->rcnt] = '\0';

    while (0 < d->rcnt) {
	if (0 >= d->xsize) {
	    char	*hend = strstr(d->buf, "\r\n\r\n");
	    char	*cl = strstr(d->buf, content_length);

	    if (NULL == hend) {
		return false;
	    }
	    if (NULL == cl) {
		char	*te = strstr(d->buf, transfer_encoding);

		// TBD Handle chunking correctly. This approach only works
		// when all the chunks come in one read and no more than that.
		if (NULL != te && 0 == strncasecmp("chunked\r", te + sizeof(transfer_encoding), 8)) {
		    d->xsize = d->rcnt;
		} else {
		    d->xsize = hend - d->buf + 4;
		}
	    } else {
		cl += sizeof(content_length);
		for (; ' ' == *cl; cl++) {
		}
		char	*end;
		long	len = strtol(cl, &end, 10);

		if ('\r' != *end) {
		    printf("*-*-* error reading content length on %d.\n", d->sock);
		    drop_cleanup(d);
		    return true;
		}
		d->xsize = hend - d->buf + 4 + len;
	    }
	    if (0 >= d->xsize) {
		break;
	    }
	    hsize = hend - d->buf;
	}
	if (d->xsize <= d->rcnt) {
	    double	dt = dtime() - d->pipeline[d->phead];
	    double	ave;
	    double	dif;

	    p->ok_cnt++;
	    d->pipeline[d->phead] = 0.0;
	    d->phead++;
	    if (PIPELINE_SIZE <= d->phead) {
		d->phead = 0;
	    }
	    p->lat_sum += dt;
	    if (0 < p->ok_cnt) {
		ave = p->lat_sum / (double)p->ok_cnt;
		dif = dt - ave;
		p->lat_sq_sum += dif * dif;
	    }
	    /* TBD debugging, uses something better than a simple average for latency analysis.
	       if (0.01 < dt && p->lat_sum * 2.0 / p->ok_cnt < dt) {
	       printf("*** long latency: %0.3f msecs\n", dt * 1000.0);
	       }
	    */
	    if (d->h->verbose) {
		char	save = d->buf[d->xsize];
		
		d->buf[d->xsize] = '\0';
		pthread_mutex_lock(&p->perfer->print_mutex);
		printf("\n%ld %ld %ld --------------------------------------------------------------------------------\n%s\n", d->xsize, d->rcnt, hsize, d->buf);
		pthread_mutex_unlock(&p->perfer->print_mutex);
		d->buf[d->xsize] = save;
	    }
	    if ((enough || !d->h->keep_alive) && 0 >= drop_pending(d) ) {
		drop_cleanup(d);
		return true;
	    } else {
		if (d->xsize < d->rcnt) {
		    memmove(d->buf, d->buf + d->xsize, d->rcnt - d->xsize);
		    d->rcnt -= d->xsize;
		    d->xsize = 0;
		} else {
		    d->rcnt = 0;
		    d->xsize = 0;
		    break;
		}
	    }
	} else {
	    break;
	}
    }
    return false;
}
