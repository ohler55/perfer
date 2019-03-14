// Copyright 2015, 2016, 2018 by Peter Ohler, All Rights Reserved

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "dtime.h"
#include "queue.h"
#include "drop.h"

// lower gives faster response but burns more CPU. This is a reasonable compromise.
#define RETRY_SECS	0.00001

// head and tail both increment and wrap.
// tail points to next open space.
// When head == tail the queue is full. This happens when tail catches up with head.
//

int
queue_init(Queue q, size_t qsize) {
    if (qsize < 4) {
	qsize = 4;
    }
    if (NULL == (q->q = (Drop*)calloc(qsize, sizeof(Drop)))) {
	return ENOMEM;
    }
    q->end = q->q + qsize;

    atomic_init(&q->head, q->q);
    atomic_init(&q->tail, q->q + 1);
    atomic_flag_clear(&q->push_lock);
    atomic_flag_clear(&q->pop_lock);

    return 0;
}

void
queue_cleanup(Queue q) {
    free(q->q);
    q->q = NULL;
    q->end = NULL;
}

void
queue_push(Queue q, Drop item) {
    Drop	*tail;

    while (atomic_flag_test_and_set(&q->push_lock)) {
	dsleep(RETRY_SECS);
    }
    if (item->queued) {
	atomic_flag_clear(&q->push_lock);
	return;
    }
    // Wait for head to move on.
    while (atomic_load(&q->head) == atomic_load(&q->tail)) {
	dsleep(RETRY_SECS);
    }
    *(Drop*)atomic_load(&q->tail) = item;
    tail = (Drop*)atomic_load(&q->tail) + 1;

    if (q->end <= tail) {
	tail = q->q;
    }
    item->queued = true;
    atomic_store(&q->tail, tail);
    atomic_flag_clear(&q->push_lock);
}

Drop
queue_pop(Queue q, double timeout) {
    Drop	item;
    Drop	*next;
    int 	cnt;

    while (atomic_flag_test_and_set(&q->pop_lock)) {
	dsleep(RETRY_SECS);
    }
    item = *(Drop*)atomic_load(&q->head);

    if (NULL != item) {
	*(Drop*)atomic_load(&q->head) = NULL;
	atomic_flag_clear(&q->pop_lock);

	return item;
    }
    next = (Drop*)atomic_load(&q->head) + 1;

    if (q->end <= next) {
	next = q->q;
    }
    // If the next is the tail then wait for something to be appended.
    for (cnt = (int)(timeout / RETRY_SECS); atomic_load(&q->tail) == next; cnt--) {
	if (cnt <= 0) {
	    atomic_flag_clear(&q->pop_lock);
	    return NULL;
	}
	dsleep(RETRY_SECS);
    }
    atomic_store(&q->head, next);
    item = *next;
    if (NULL != item) {
	item->queued = false;
    }
    *next = NULL;
    atomic_flag_clear(&q->pop_lock);

    return item;
}

bool
queue_empty(Queue q) {
    Drop	*head = atomic_load(&q->head);
    Drop	*next = head + 1;

    if (q->end <= next) {
	next = q->q;
    }
    if (NULL == *head && atomic_load(&q->tail) == next) {
	return true;
    }
    return false;
}

int
queue_count(Queue q) {
    int	size = (int)(q->end - q->q);

    return ((Drop*)atomic_load(&q->tail) - (Drop*)atomic_load(&q->head) + size) % size;
}
