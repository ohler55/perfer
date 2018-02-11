// Copyright 2016 by Peter Ohler, All Rights Reserved

#ifndef __PERFER_DROP_H__
#define __PERFER_DROP_H__

#include <stdbool.h>
#include <poll.h>

struct _Perfer;

typedef struct _Drop {
    struct _Perfer	*h; // for addr and request body
    int			sock;
    struct pollfd	*pp;
    bool		sent;
    long		rcnt; // recv count
    double		start;
} *Drop;

extern int	drop_init(Drop d, struct _Perfer *h);
extern void	drop_cleanup(Drop d);

#endif /* __PERFER_DROP_H__ */
