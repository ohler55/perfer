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

//#define MAX_RESP_SIZE	4096
#define MAX_RESP_SIZE	16384
//#define MAX_RESP_SIZE	64000
#define PIPELINE_SIZE	16

typedef atomic_int_fast64_t	atime;

struct _perfer;
struct _pool;

typedef struct _drop {
    volatile int	sock;
    struct pollfd	*pp;
    struct _perfer	*perfer; // for addr and request body
    struct _pool	*pool;
#ifdef WITH_OPENSSL
    BIO			*bio;
#endif
    atomic_flag		queued;
    atime		recv_time;
    atime		pipeline[PIPELINE_SIZE];
    atomic_int_fast8_t	phead;
    atomic_int_fast8_t	ptail;

    volatile int64_t	start_time;
    volatile int64_t	end_time;

    volatile bool	finished;
    long		rcnt;    // recv count
    long		xsize;   // expected size of message
    char		buf[MAX_RESP_SIZE];
} *Drop;

extern void	drop_init(Drop d, struct _pool *pool);
extern void	drop_cleanup(Drop d);
extern int	drop_pending(Drop d);

extern int	drop_connect(Drop d);
extern int	drop_recv(Drop d);
extern int	drop_warmup_send(Drop d);
extern int	drop_warmup_recv(Drop d);

#endif /* PERFER_DROP_H */
