// Copyright 2016 by Peter Ohler, All Rights Reserved

#ifndef PERFER_PERFER_H
#define PERFER_PERFER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "queue.h"

struct _pool;
struct addrinfo;

typedef struct _header {
    struct _header	*next;
    const char		*line;
} *Header;

typedef struct _perfer {
    bool		inited;
    volatile bool	done;
    volatile bool	enough;
    struct _queue	q;
    struct _drop	*drops;

    struct _pool	*pools;
    long		tcnt;
    long		ccnt;
    const char		*url;
    const char		*addr;
    const char		*port;
    const char		*path;
    const char		*post;
    struct addrinfo	*addr_info;
    double		duration;
    double		start_time;
    const char		*req_file;
    char		*req_body;
    long		req_len;
    int			backlog;
    int			graph_width;
    int			graph_height;
    int			poll_timeout;
    bool		keep_alive;
    bool		verbose;
    bool		replace;
    bool		tls;
    bool		json;
    atomic_int		ready_cnt;
    atomic_int		seq;
    Header		headers;

    pthread_mutex_t	print_mutex;
} *Perfer;

extern void	perfer_stop(Perfer h);

#endif /* PERFER_PERFER_H */
