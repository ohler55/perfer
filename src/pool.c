// Copyright 2016 by Peter Ohler, All Rights Reserved

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "dtime.h"
#include "drop.h"
#include "perfer.h"
#include "pool.h"

static int
send_check(Perfer p, Drop d) {
    int	err;

    if (0 == d->sock) {
	if (0 != (err = drop_connect(d))) {
	    // Failed to connect. Abort the test.
	    perfer_stop(p);
	    return err;
	}
    }
    if (drop_pending(d) < p->backlog) {
	int	scnt = send(d->sock, p->req_body, p->req_len, 0);

	if (p->req_len != scnt) {
	    if (p->keep_alive) {
		if (!p->json) {
		    printf("*-*-* error sending request: %s - %d\n", strerror(errno), scnt);
		}
		atomic_fetch_add(&p->err_cnt, 1);
		drop_cleanup(d);
	    }
	    return 0;
	}
	if (0 == d->start_time) {
	    d->start_time = ntime();
	}
	atomic_fetch_add(&p->sent_cnt, 1);

	int	tail = atomic_load(&d->ptail);

	atomic_store(&d->pipeline[tail], ntime());
	tail++;
	if (PIPELINE_SIZE <= tail) {
	    tail = 0;
	}
	atomic_store(&d->ptail, tail);
    }
    return 0;
}

int
pool_send(Pool p, int i) {
    return send_check(p->perfer, p->drops + (i % p->dcnt));
}

static void*
poll_loop(void *x) {
    Pool		p = (Pool)x;
    Perfer		pr = p->perfer;
    int			dcnt = p->dcnt;
    struct pollfd	ps[dcnt];
    struct pollfd	*pp;
    Drop		d;
    int			i;
    int			pt = pr->poll_timeout;

    while (!pr->done) {
	if (pr->enough) {
	    bool	done = true;

	    for (d = p->drops, i = dcnt, pp = ps; 0 < i; i--, d++) {
		if (0 < drop_pending(d)) {
		    done = false;
		    break;
		}
	    }
	    if (done) {
		pr->done = true;
		for (d = p->drops, i = dcnt, pp = ps; 0 < i; i--, d++) {
		    drop_cleanup(d);
		}
		break;
	    }
	}
	for (d = p->drops, i = dcnt, pp = ps; 0 < i; i--, d++) {
	    if (!pr->enough && 0 == pr->meter) {
		if (0 != send_check(pr, d)) {
		    p->poll_finished = true;
		    return NULL;
		}
	    }
	    if (0 < d->sock) {
		pp->fd = d->sock;
		d->pp = pp;
		pp->events = POLLERR | POLLIN;
		pp->revents = 0;
		pp++;
	    }
	}
	if (0 > (i = poll(ps, pp - ps, pt))) {
	    if (EAGAIN == errno) {
		continue;
	    }
	    printf("*-*-* polling error: %s\n", strerror(errno));
	    break;
	}
	if (0 == i) {
	    continue;
	}
	for (d = p->drops, i = dcnt; 0 < i; i--, d++) {
	    if (NULL == d->pp || 0 == d->pp->revents || 0 == d->sock) {
		continue;
	    }
	    if (0 != (d->pp->revents & POLLERR)) {
		atomic_fetch_add(&pr->err_cnt, 1);
		drop_cleanup(d);
	    }
	    if (0 != (d->pp->revents & POLLIN)) {
		if (!atomic_flag_test_and_set(&d->queued)) {
		    atomic_store(&d->recv_time, ntime());
		    queue_push(&p->q, d);
		}
	    }
	}
    }
    p->poll_finished = true;

    return NULL;
}

static void*
recv_loop(void *x) {
    Pool	p = (Pool)x;
    Perfer	pr = p->perfer;
    Drop	d;

    while (!pr->done) {
	if (NULL == (d = queue_pop(&p->q, 0.01))) {
	    continue;
	}
	drop_recv(d);
	atomic_flag_clear(&d->queued);
    }
    p->recv_finished = true;

    return NULL;
}

int
pool_init(Pool p, Perfer perfer, int dcnt) {
    int		err;
    int		i;
    Drop	d;

    p->recv_finished = false;
    p->poll_finished = false;
    p->perfer = perfer;
    p->dcnt = dcnt;
    if (NULL == (p->drops = (Drop)calloc(dcnt, sizeof(struct _drop)))) {
	printf("*-*-* Not enough memory for connections.\n");
	return ENOMEM;
    }
    for (d = p->drops, i = p->dcnt; 0 < i; i--, d++) {
	// TBD pass in response size after a probe to the target
	drop_init(d, p);
    }
    if (0 != (err = queue_init(&p->q, dcnt + 4))) {
	printf("*-*-* Not enough memory for connection queue.\n");
	return err;
    }
    return 0;
}

int
pool_start(Pool p) {
    bool	use_epoll = p->perfer->keep_alive && !p->perfer->no_epoll;

#ifndef HAVE_EPOLL
    use_epoll = false;
#endif
    if (0 != pthread_create(&p->recv_thread, NULL, recv_loop, p)) {
	printf("*-*-* Failed to create receiving thread. %s\n", strerror(errno));
	return errno;
    }
    dsleep(0.5);
    if (use_epoll) {
#ifdef HAVE_EPOLL
	if (0 != pthread_create(&poll_thread, NULL, epoll_loop, p)) {
	    printf("*-*-* Failed to create polling thread. %s\n", strerror(errno));
	    return errno;
	}
#endif
    } else {
	if (0 != pthread_create(&p->poll_thread, NULL, poll_loop, p)) {
	    printf("*-*-* Failed to create polling thread. %s\n", strerror(errno));
	    return errno;
	}
    }
    return 0;
}

void
pool_wait(Pool p) {
    // Wait for thread to finish. Join is not used as we want to kill the thread
    // if it does not exit correctly.
    pthread_detach(p->recv_thread); // cleanup thread resources when completed
    pthread_detach(p->poll_thread); // cleanup thread resources when completed
    if (!p->recv_finished || !p->poll_finished) {
	double  late = dtime() + p->perfer->duration + 2.0;

	while ((!p->recv_finished || !p->poll_finished) && dtime() < late) {
	    dsleep(0.5);
        }
	pthread_cancel(p->recv_thread);
	pthread_cancel(p->poll_thread);
    }
}

void
pool_cleanup(Pool p) {
    Drop	d;
    int		i;

    for (d = p->drops, i = p->dcnt; 0 < i; i--, d++) {
	drop_cleanup(d);
    }
    queue_cleanup(&p->q);
    free(p->xbuf);
}

int
pool_warmup(Pool p) {
    Drop	d;
    int		i;
    int		err;

    p->xsize = 0;
    // Initialize connections before starting the benchmarks.
    for (d = p->drops, i = p->dcnt; 0 < i; i--, d++) {
	if (0 != (err = drop_connect(d)) ||
	    0 != (err = drop_warmup_send(d))) {
	    return err;
	}
    }
    for (d = p->drops, i = p->dcnt; 0 < i; i--, d++) {
	if (0 != (err = drop_warmup_recv(d))) {
	    return err;
	}
	if (0 == p->xsize) {
	    p->xsize = d->xsize;
	    p->xbuf = (char*)malloc(p->xsize + 1);
	    memcpy(p->xbuf, d->buf, p->xsize);
	    p->xbuf[p->xsize] = '\0';
	} else if (p->xsize != d->xsize) {
	    p->xsize = -1;
	    free(p->xbuf);
	    p->xbuf = NULL;
	}
	d->xsize = 0;
    }
    return 0;
}
