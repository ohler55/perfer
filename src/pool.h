// Copyright 2016 by Peter Ohler, All Rights Reserved

#ifndef __PERFER_POOL_H__
#define __PERFER_POOL_H__

#include <pthread.h>
#include <stdbool.h>

struct _Perfer;

typedef struct _Pool {
    struct _Drop	*drops;
    int			dcnt;
    int			max_pending;
    long		num;
    long		sent_cnt;
    long		err_cnt;
    long		ok_cnt;
    double		lat_sum;
    double		lat_sq_sum;
    double		actual_end;
    struct _Perfer	*perfer;
    pthread_t		thread;
    volatile bool	finished;
} *Pool;

struct _Perfer;

extern int	pool_init(Pool p, struct _Perfer *h, long num);
extern void	pool_cleanup(Pool p);
extern int	pool_start(Pool p);
extern void	pool_wait(Pool p);

#endif /* __PERFER_POOL_H__ */
