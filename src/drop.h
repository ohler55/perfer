// Copyright 2016 by Peter Ohler, All Rights Reserved

#ifndef PERFER_DROP_H
#define PERFER_DROP_H

#include <stdbool.h>
#include <poll.h>

//#define MAX_RESP_SIZE	16384
#define MAX_RESP_SIZE	64000
#define PIPELINE_SIZE	16

struct _perfer;
struct _pool;
struct addrinfo;

typedef struct _drop {
    struct _perfer	*h; // for addr and request body
    int			sock;
    struct pollfd	*pp;

    double		pipeline[PIPELINE_SIZE];
    int			phead;
    int			ptail;
    
    long		rcnt;    // recv count
    long		xsize;   // expected size of message
    char		buf[MAX_RESP_SIZE];
} *Drop;

extern void	drop_init(Drop d, struct _perfer *h);
extern void	drop_cleanup(Drop d);
extern int	drop_pending(Drop d);

extern int	drop_connect(Drop d, struct _pool *p, struct addrinfo *res);
extern bool	drop_send(Drop d, struct _pool *p);
extern bool	drop_recv(Drop d, struct _pool *p, bool enough);

#endif /* PERFER_DROP_H */
