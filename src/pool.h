// Copyright 2016 by Peter Ohler, All Rights Reserved

#ifndef PERFER_POOL_H
#define PERFER_POOL_H

#include <pthread.h>
#include <stdbool.h>

struct _perfer;

typedef struct _pool {
    struct _drop	*drops;
    int			dcnt;
    int			max_pending;
    long		num;
    long		con_cnt;
    long		sent_cnt;
    long		err_cnt;
    long		ok_cnt;
    double		lat_sum;
    double		lat_sq_sum;
    double		actual_end;
    struct _perfer	*perfer;
    pthread_t		thread;
    volatile bool	finished;
} *Pool;

struct _perfer;

extern int	pool_init(Pool p, struct _perfer *h, long num);
extern void	pool_cleanup(Pool p);
extern int	pool_start(Pool p);
extern void	pool_wait(Pool p);

#endif /* PERFER_POOL_H */
