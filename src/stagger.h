// Copyright 2019 by Peter Ohler, All Rights Reserved

#ifndef PERFER_STAGGER_H
#define PERFER_STAGGER_H

#include <stdint.h>

extern void	stagger_init();
extern void	stagger_add(uint64_t val);

// Analysis functions.
extern uint64_t	stagger_count();
extern uint64_t	stagger_at(double target);
extern uint64_t	stagger_range(uint64_t min, uint64_t max);
extern uint64_t	stagger_average();
extern uint64_t	stagger_min();
extern uint64_t	stagger_max();
extern double	stagger_stddev();

#endif /* PERFER_STAGGER_H */
