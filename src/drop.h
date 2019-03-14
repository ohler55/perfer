// Copyright 2016, 2019 by Peter Ohler, All Rights Reserved

#ifndef PERFER_DROP_H
#define PERFER_DROP_H

#include <stdatomic.h>
#include <stdbool.h>
#include <poll.h>
#ifdef WITH_OPENSSL
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#define MAX_RESP_SIZE	16384
//#define MAX_RESP_SIZE	64000
#define PIPELINE_SIZE	16

struct _perfer;
struct _pool;
struct addrinfo;

typedef struct _drop {
    struct _perfer	*perfer; // for addr and request body
    int			sock;
    struct pollfd	*pp;
#ifdef WITH_OPENSSL
    BIO			*bio;
#endif
    double		pipeline[PIPELINE_SIZE];
    int			phead;
    int			ptail;
    volatile bool	queued; // only used by the queue

    atomic_long		sent_cnt;
    volatile long	con_cnt;
    volatile long	err_cnt;
    volatile long	ok_cnt;
    volatile double	lat_sum;
    volatile double	lat_sq_sum;
    volatile double	start_time;
    volatile double	end_time;
    volatile bool	finished;

    long		rcnt;    // recv count
    long		xsize;   // expected size of message
    char		buf[MAX_RESP_SIZE];
} *Drop;

extern void	drop_init(Drop d, struct _perfer *perfer);
extern void	drop_cleanup(Drop d);
extern int	drop_pending(Drop d);

extern int	drop_connect(Drop d, struct addrinfo *res);
extern bool	drop_send(Drop d, struct _pool *p);
extern bool	drop_recv(Drop d, struct _pool *p);

#endif /* PERFER_DROP_H */
