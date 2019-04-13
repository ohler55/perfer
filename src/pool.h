// Copyright 2016 by Peter Ohler, All Rights Reserved

#ifndef PERFER_POOL_H
#define PERFER_POOL_H

#include <pthread.h>
#include <stdbool.h>

#include "queue.h"

struct _perfer;
struct _drop;

typedef struct _pool {
    struct _perfer	*perfer;
    volatile bool	recv_finished;
    volatile bool	poll_finished;

    struct _queue	q;
    struct _drop	*drops;
    long		dcnt;
    int			xsize;
    char		*xbuf;
    pthread_t		poll_thread;
    pthread_t		recv_thread;
} *Pool;

struct _perfer;

extern int	pool_init(Pool p, struct _perfer *perfer, int dcnt);
extern int	pool_start(Pool p);
extern void	pool_wait(Pool p);
extern void	pool_cleanup(Pool p);
extern int	pool_warmup(Pool p);
extern int	pool_send(Pool p, int i);

#endif /* PERFER_POOL_H */
