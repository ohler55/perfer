// Copyright 2016 by Peter Ohler, All Rights Reserved

#ifndef PERFER_POOL_H
#define PERFER_POOL_H

#include <pthread.h>
#include <stdbool.h>

struct _perfer;

typedef struct _pool {
    struct _perfer	*perfer;
    pthread_t		thread;
    volatile bool	finished;
} *Pool;

struct _perfer;

extern int	pool_start(Pool p, struct _perfer *perfer);
extern void	pool_wait(Pool p);

#endif /* PERFER_POOL_H */
