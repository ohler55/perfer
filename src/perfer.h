// Copyright 2016 by Peter Ohler, All Rights Reserved

#ifndef __PERFER_PERFER_H__
#define __PERFER_PERFER_H__

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

struct _Pool;

typedef struct _Header {
    struct _Header	*next;
    const char		*line;
} *Header;

typedef struct _Perfer {
    bool		inited;
    volatile bool	done;
    struct _Pool	*pools;
    long		tcnt;
    long		ccnt;
    const char		*addr;
    const char		*path;
    double		duration;
    double		start_time;
    const char		*req_file;
    char		*req_body;
    int			req_len;
    int			backlog;
    bool		keep_alive;
    bool		verbose;
    bool		replace;
    atomic_int		ready_cnt;
    atomic_int		seq;
    Header		headers;
    pthread_mutex_t	print_mutex;
} *Perfer;

extern void	perfer_stop(Perfer h);

#endif /* __PERFER_PERFER_H__ */
