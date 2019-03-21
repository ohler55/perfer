// Copyright 2016, 2019 by Peter Ohler, All Rights Reserved

#ifndef PERFER_DROP_H
#define PERFER_DROP_H

#include <stdatomic.h>
#include <stdbool.h>
#include <poll.h>
#include <pthread.h>
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

typedef struct _drop {
    struct _perfer	*perfer; // for addr and request body
    volatile int	sock;
    struct pollfd	*pp;
#ifdef WITH_OPENSSL
    BIO			*bio;
#endif
    atomic_flag		queued;
    atime		sent_time;
    atime		recv_time;
    volatile int64_t	current_time; // sent time for the current read

    pthread_mutex_t	moo;

    volatile long	sent_cnt;
    volatile long	con_cnt;
    volatile long	err_cnt;
    volatile long	ok_cnt;
    volatile double	lat_sum;
    volatile double	lat_sq_sum;
    volatile int64_t	start_time;
    volatile int64_t	end_time;

    volatile bool	finished;
    long		rcnt;    // recv count
    long		xsize;   // expected size of message
    char		buf[MAX_RESP_SIZE];
} *Drop;

extern void	drop_init(Drop d, struct _perfer *perfer);
extern void	drop_cleanup(Drop d);
extern int	drop_pending(Drop d);

extern int	drop_connect(Drop d);
extern int	drop_recv(Drop d);

#endif /* PERFER_DROP_H */
