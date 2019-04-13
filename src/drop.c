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

#include "drop.h"
#include "dtime.h"
#include "perfer.h"
#include "pool.h"
#include "stagger.h"

static const char	content_length[] = "Content-Length:";
static const char	transfer_encoding[] = "Transfer-Encoding:";

void
drop_init(Drop d, struct _pool *pool) {
    memset(d, 0, sizeof(struct _drop));
    d->pool = pool;
    d->perfer = pool->perfer;
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
    atomic_fetch_add(&d->perfer->err_cnt, 1);
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
	atomic_fetch_add(&d->perfer->con_cnt, 1);
    } else {
	atomic_fetch_add(&d->perfer->err_cnt, 1);
    }
    return err;
}

int
drop_recv(Drop d) {
    if (0 >= drop_pending(d)) {
	return 0;
    }
    Perfer	pr = d->perfer;
    Pool	p = d->pool;
    ssize_t	rcnt;

    if (0 == d->sock) {
	return 0;
    }
    if (0 > (rcnt = recv(d->sock, d->buf + d->rcnt, sizeof(d->buf) - d->rcnt - 1, 0))) {
	if (EAGAIN != errno) {
	    drop_cleanup(d);
	    atomic_fetch_add(&d->perfer->err_cnt, 1);
	}
	//printf("*-*-* error reading response on %d: %s\n", d->sock, strerror(errno));
	return errno;
    }
    if (0 == rcnt) {
	return 0;
    }
    d->rcnt += rcnt;

    int64_t	recv_time = atomic_load(&d->recv_time);

    while (0 < d->rcnt) {
	if (0 >= d->xsize) {
	    if (0 < p->xsize && 0 == memcmp(p->xbuf, d->buf, p->xsize)) {
		d->xsize = p->xsize;
	    } else {
		char	*cl = strstr(d->buf, content_length);
		char	*hend;

		if (NULL == cl) {
		    hend = strstr(d->buf, "\r\n\r\n");
		} else {
		    hend = strstr(cl, "\r\n\r\n");
		}
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
			if (!d->perfer->json) {
			    printf("*-*-* error reading content length on %d.\n", d->sock);
			}
			drop_cleanup(d);
			atomic_fetch_add(&pr->err_cnt, 1);
			return EIO;
		    }
		    d->xsize = hend - d->buf + 4 + len;
		}
		if (0 >= d->xsize) {
		    break;
		}
	    }
	}
	if (d->xsize <= d->rcnt) {
	    int		head = atomic_load(&d->phead);
	    int64_t	current = atomic_load(&d->pipeline[head]);
	    int64_t	dt = recv_time - current;

	    atomic_fetch_add(&pr->byte_cnt, d->xsize);
	    if (0 < current) {
		if (dt < 0) {
		    dt = 0;
		}
		stagger_add(dt);
	    } else {
		atomic_fetch_add(&pr->err_cnt, 1);
	    }
	    d->end_time = recv_time;

	    head++;
	    if (PIPELINE_SIZE <= head) {
		head = 0;
	    }
	    atomic_store(&d->phead, head);
	    if ((pr->enough || !pr->keep_alive) && 0 >= drop_pending(d) ) {
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
    return 0;
}

int
drop_warmup_send(Drop d) {
    if (d->perfer->req_len != send(d->sock, d->perfer->req_body, d->perfer->req_len, 0)) {
	printf("*-*-* error sending request: %s\n", strerror(errno));
	drop_cleanup(d);
	return errno;
    }
    return 0;
}

int
drop_warmup_recv(Drop d) {
    ssize_t	rcnt;
    long	hsize = 0;
    double	giveup = dtime() + 2.0;
    Perfer	p = d->perfer;

    while (true) {
	if (giveup < dtime()) {
	    if (!p->json) {
		printf("*-*-* timed out waiting for a response\n");
	    }
	    return -1;
	}
	if (0 > (rcnt = recv(d->sock, d->buf + d->rcnt, sizeof(d->buf) - d->rcnt - 1, 0))) {
	    if (EAGAIN != errno) {
		if (!p->json) {
		    printf("*-*-* error reading response on %d: %s\n", d->sock, strerror(errno));
		}
		drop_cleanup(d);
		return errno;
	    }
	    dsleep(0.001);
	    continue;
	}
	d->rcnt += rcnt;
	if (0 < d->rcnt) {
	    if (0 >= d->xsize) {
		char	*cl = strstr(d->buf, content_length);
		char	*hend;

		if (NULL == cl) {
		    hend = strstr(d->buf, "\r\n\r\n");
		} else {
		    hend = strstr(cl, "\r\n\r\n");
		}
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
			if (!p->json) {
			    printf("*-*-* error reading content length on %d.\n", d->sock);
			}
			drop_cleanup(d);
			atomic_fetch_add(&p->err_cnt, 1);
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
		if (p->verbose) {
		    char	save = d->buf[d->xsize];

		    d->buf[d->xsize] = '\0';
		    pthread_mutex_lock(&p->print_mutex);
		    printf("\nsize: %ld body: %ld --------------------------------------------------------------------------------\n%s\n",
			   d->xsize, d->xsize - hsize, d->buf);
		    pthread_mutex_unlock(&p->print_mutex);
		    d->buf[d->xsize] = save;
		}
		d->rcnt = 0;
		break;
	    }
	}
    }
    d->rcnt = 0;
    // d->xsize = 0; set outside so the xsize can be grabbed and compared

    return 0;
}
