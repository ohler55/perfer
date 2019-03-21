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
drop_init(Drop d, struct _perfer *perfer) {
    memset(d, 0, sizeof(struct _drop));
    d->perfer = perfer;
    //atomic_init(&d->sent_time, 0);
    atomic_init(&d->recv_time, 0);

    atime	*end = d->pipeline + sizeof(d->pipeline) / sizeof(*d->pipeline);

    for (atime *tp = d->pipeline; tp < end; tp++) {
	atomic_init(tp, 0);
    }
    atomic_init(&d->phead, 0);
    atomic_init(&d->ptail, 0);
}

void
drop_cleanup(Drop d) {
    if (0 != d->sock) {
	close(d->sock);
    }
    d->sock = 0;
    d->pp = NULL;
#ifdef WITH_OPENSSL
    d->bio = NULL;
#endif
    d->rcnt = 0;
    d->xsize = 0;
    *d->buf = '\0';
}

int
drop_pending(Drop d) {
    int	len = atomic_load(&d->ptail) - atomic_load(&d->phead);

    if (len < 0) {
	len += PIPELINE_SIZE;
    }
    return len;
}

static int
drop_connect_normal(Drop d) {
    int	optval = 1;
    int	flags;

    if (0 > (d->sock = socket(d->perfer->addr_info->ai_family, d->perfer->addr_info->ai_socktype, d->perfer->addr_info->ai_protocol))) {
	if (EINPROGRESS != errno) {
	    printf("*-*-* error opening socket: %s\n", strerror(errno));
	    goto FAIL;
	}
    }
    if (0 > setsockopt(d->sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval))) {
	printf("*-*-* error setting socket option: %s\n", strerror(errno));
	goto FAIL;
    }
    if (0 > connect(d->sock, d->perfer->addr_info->ai_addr, d->perfer->addr_info->ai_addrlen)) {
	printf("*-*-* error connecting: %s\n", strerror(errno));
	goto FAIL;
    }
    flags = fcntl(d->sock, F_GETFL, 0);
    fcntl(d->sock, F_SETFL, O_NONBLOCK | flags);
    d->rcnt = 0;

    return 0;
FAIL:
    d->err_cnt++;
    d->finished = true;
    d->end_time = ntime();
    return errno;
}

static int
drop_connect_tls(Drop d) {

    // TBD

    return 0;
}

// Return non-zero on error.
int
drop_connect(Drop d) {
    int	err;

    if (d->perfer->tls) {
	err = drop_connect_tls(d);
    } else {
	err = drop_connect_normal(d);
    }
    if (0 == err) {
	d->con_cnt++;
    } else {
	d->err_cnt++;
    }
    return err;
}

int
drop_recv(Drop d) {
    if (0 >= drop_pending(d)) {
	atomic_flag_clear(&d->queued);
	return 0;
    }
    ssize_t	rcnt;
    long	hsize = 0;

    if (0 == d->sock) {
	return 0;
    }
    if (0 > (rcnt = recv(d->sock, d->buf + d->rcnt, sizeof(d->buf) - d->rcnt - 1, 0))) {
	if (EAGAIN != errno) {
	    drop_cleanup(d);
	    d->err_cnt++;
	}
	//printf("*-*-* error reading response on %d: %s\n", d->sock, strerror(errno));
	return errno;
    }
    d->rcnt += rcnt;
    //d->buf[d->rcnt] = '\0';
    while (0 < d->rcnt) {
	if (0 >= d->xsize) {
	    char	*hend = strstr(d->buf, "\r\n\r\n");
	    char	*cl = strstr(d->buf, content_length);

	    if (NULL == hend) {
		return 0;
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
		    d->err_cnt++;
		    return EIO;
		}
		d->xsize = hend - d->buf + 4 + len;
	    }
	    if (0 >= d->xsize) {
		break;
	    }
	    hsize = hend - d->buf;
	}
	if (d->xsize <= d->rcnt) {
	    int64_t	recv_time = atomic_load(&d->recv_time);
	    int		head = atomic_load(&d->phead);
	    int64_t	current = atomic_load(&d->pipeline[head]);

	    d->ok_cnt++;
	    if (0 == current) {
		if (0 < d->ok_cnt) {
		    d->lat_sum = d->lat_sum * (double)(d->ok_cnt + 1) / (double)d->ok_cnt;
		    d->lat_sq_sum = d->lat_sq_sum * (double)(d->ok_cnt + 1) / (double)d->ok_cnt;
		}
	    } else {
		double	dt = (double)(recv_time - current) / 1000000000.0;
		double	ave;
		double	dif;

		d->lat_sum += dt;
		if (0 < d->ok_cnt) {
		    ave = d->lat_sum / (double)d->ok_cnt;
		    dif = dt - ave;
		    d->lat_sq_sum += dif * dif;
		}
	    }
	    d->end_time = recv_time;

	    head++;
	    if (PIPELINE_SIZE <= head) {
		head = 0;
	    }
	    atomic_store(&d->phead, head);

	    if (d->perfer->verbose) {
		char	save = d->buf[d->xsize];

		d->buf[d->xsize] = '\0';
		pthread_mutex_lock(&d->perfer->print_mutex);
		printf("\n%ld %ld %ld --------------------------------------------------------------------------------\n%s\n",
		       d->xsize, d->rcnt, hsize, d->buf);
		pthread_mutex_unlock(&d->perfer->print_mutex);
		d->buf[d->xsize] = save;
	    }
	    if ((d->perfer->enough || !d->perfer->keep_alive) && 0 >= drop_pending(d) ) {
		drop_cleanup(d);
		return 0;
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
    atomic_flag_clear(&d->queued);

    return 0;
}
