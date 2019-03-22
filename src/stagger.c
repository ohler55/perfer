// Copyright 2019 by Peter Ohler, All Rights Reserved

#include <stdatomic.h>
#include <stdio.h>

#include "stagger.h"

// < 256            [ array of counts for each value ]
// < 256 * 16       [ array of counts for each value >> 4 ]
// < 256 * 16 * 16  [ array of counts for each value >> 8 ]
// ...

#define SLOT_CNT	256

typedef atomic_uint_fast64_t	Slot;

typedef struct _level {
    uint64_t	top; // top of range stored
    uint64_t	inc; // increment between each
    Slot	slots[SLOT_CNT];
} *Level;

typedef struct _stagger {
    struct _level	levels[15];
} *Stagger;

static struct _stagger	bank = {
    .levels = {
	{ .top = 0x0000000000000100ULL, .inc = 0x0000000000000001ULL, { 0 }},
	{ .top = 0x0000000000001000ULL, .inc = 0x0000000000000010ULL, { 0 }},
	{ .top = 0x0000000000010000ULL, .inc = 0x0000000000000100ULL, { 0 }},
	{ .top = 0x0000000000100000ULL, .inc = 0x0000000000001000ULL, { 0 }},
	{ .top = 0x0000000001000000ULL, .inc = 0x0000000000010000ULL, { 0 }},
	{ .top = 0x0000000010000000ULL, .inc = 0x0000000000100000ULL, { 0 }},
	{ .top = 0x0000000100000000ULL, .inc = 0x0000000001000000ULL, { 0 }},
	{ .top = 0x0000001000000000ULL, .inc = 0x0000000010000000ULL, { 0 }},
	{ .top = 0x0000010000000000ULL, .inc = 0x0000000100000000ULL, { 0 }},
	{ .top = 0x0000100000000000ULL, .inc = 0x0000001000000000ULL, { 0 }},
	{ .top = 0x0001000000000000ULL, .inc = 0x0000010000000000ULL, { 0 }},
	{ .top = 0x0010000000000000ULL, .inc = 0x0000100000000000ULL, { 0 }},
	{ .top = 0x0100000000000000ULL, .inc = 0x0001000000000000ULL, { 0 }},
	{ .top = 0x1000000000000000ULL, .inc = 0x0010000000000000ULL, { 0 }},
	{ .top = 0x0000000000000000ULL, .inc = 0x0000000000000000ULL, { 0 }},
    }
};

void
stagger_init() {
    for (Level level = bank.levels; 0 != level->top; level++) {
	int	i = SLOT_CNT;

	for (Slot *sp = level->slots; 0 < i; i--, sp++) {
	    atomic_init(sp, 0);
	}
    }
}

void
stagger_add(uint64_t val) {
    for (Level level = bank.levels; 0 != level->top; level++) {
	if (level->top > val) {
	    atomic_fetch_add(level->slots + val / level->inc, 1);
	    break;
	}
    }
}

uint64_t
stagger_count() {
    uint64_t	cnt = 0;

    for (Level level = bank.levels; 0 != level->top; level++) {
	int	i = SLOT_CNT;

	for (Slot *sp = level->slots; 0 < i; i--, sp++) {
	    cnt += atomic_load(sp);
	}
    }
    return cnt;
}

uint64_t
stagger_at(double target) {
    uint64_t	total = stagger_count();
    uint64_t	tcnt = (uint64_t)(total * target);
    uint64_t	cnt = 0;
    uint64_t	inc;
    uint64_t	val = 0;

    for (Level level = bank.levels; 0 != level->top; level++) {
	int	i = 0;

	for (Slot *sp = level->slots; i < SLOT_CNT; i++, sp++) {
	    inc = atomic_load(sp);
	    if (0 < inc) {
		cnt += inc;
		val = level->inc * i;
		if (tcnt == cnt) {
		    return val;
		} else if (tcnt < cnt) {
		    val = level->inc * (i- 1) + (tcnt - (cnt - inc)) * level->inc / inc;
		    return val;
		}
	    }
	}
    }
    return val;
}

uint64_t
stagger_range(uint64_t min, uint64_t max) {
    uint64_t	cnt = 0;

    for (Level level = bank.levels; 0 != level->top; level++) {
	if (level->top < min) {
	    continue;
	}
	int		i = 0;
	uint64_t	v;

	for (Slot *sp = level->slots; i < SLOT_CNT; i++, sp++) {
	    v = level->inc * i;
	    if (max < v) {
		return cnt;
	    }
	    if (min < v) {
		cnt += atomic_load(sp);
	    }
	}
    }
    return cnt;
}

uint64_t
stagger_average() {
    uint64_t	cnt = 0;
    double	sum = 0.0;
    uint64_t	scnt;

    for (Level level = bank.levels; 0 != level->top; level++) {
	int	i = 0;

	for (Slot *sp = level->slots; i < SLOT_CNT; i++, sp++) {
	    scnt = atomic_load(sp);
	    if (0 < scnt) {
		cnt += scnt;
		sum += (double)(level->inc * i) * (double)scnt;
	    }
	}
    }
    if (0 == cnt) {
	return 0;
    }
    return (uint64_t)(sum / (double)cnt);
}

uint64_t
stagger_min() {
    for (Level level = bank.levels; 0 != level->top; level++) {
	int		i = 0;
	uint64_t	scnt;

	for (Slot *sp = level->slots; i < SLOT_CNT; i++, sp++) {
	    if (0 < (scnt = atomic_load(sp))) {
		return level->inc * i;
	    }
	}
    }
    return 0;
}

uint64_t
stagger_max() {
    uint64_t	last = 0;

    for (Level level = bank.levels; 0 != level->top; level++) {
	int		i = 0;
	uint64_t	scnt;

	for (Slot *sp = level->slots; i < SLOT_CNT; i++, sp++) {
	    if (0 < (scnt = atomic_load(sp))) {
		last = level->inc * i;
	    }
	}
    }
    return last;
}


double
stagger_stddev() {
    // TBD

    return 0.0;
}
