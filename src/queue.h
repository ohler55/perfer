// Copyright 2015, 2016, 2018, 2019 by Peter Ohler, All Rights Reserved

#ifndef PERFER_QUEUE_H
#define PERFER_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#include "err.h"

struct _drop;

typedef struct _queue {
    struct _drop		**q;
    struct _drop		**end;
    _Atomic(struct _drop**)	head;
    _Atomic(struct _drop**)	tail;
    atomic_flag			push_lock; // set to true when push in progress
    atomic_flag			pop_lock; // set to true when push in progress
} *Queue;

extern int		queue_init(Queue q, size_t qsize);
extern void		queue_cleanup(Queue q);

extern void		queue_push(Queue q, struct _drop *item);
extern struct _drop*	queue_pop(Queue q, double timeout);
extern bool		queue_empty(Queue q);
extern int		queue_count(Queue q);

#endif // PERFER_QUEUE_H
