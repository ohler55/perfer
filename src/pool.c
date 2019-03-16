// Copyright 2016 by Peter Ohler, All Rights Reserved

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <unistd.h>

#include "dtime.h"
#include "drop.h"
#include "perfer.h"
#include "pool.h"

static void*
loop(void *x) {
    Pool		p = (Pool)x;
    Perfer		h = p->perfer;
    Drop		d;
    int			err;

    while (!h->done) {
	if (NULL == (d = queue_pop(&h->q, 0.1))) {
	    continue;
	}
#if 1
	if (0 == drop_recv(d)) {
	    while (drop_pending(d) < h->backlog && !h->enough) {
		if (0 != (err = drop_send(d))) {
		    if (EAGAIN == err) {
			queue_push(&h->q, d);
		    }
		    break;
		}
	    }
	}
#else
	while (drop_pending(d) < h->backlog && !h->enough) {
	    if (0 != (err = drop_send(d))) {
		if (EAGAIN == err) {
		    queue_push(&h->q, d);
		}
		break;
	    }
	}
	drop_recv(d);
#endif
	atomic_flag_clear(&d->queued);
    }
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
