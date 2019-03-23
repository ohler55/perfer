// Copyright 2009, 2015, 2018, 2016 by Peter Ohler, All Rights Reserved

#ifndef PERFER_DTIME_H
#define PERFER_DTIME_H

#include <stdint.h>

extern double	dtime(void);
extern double	dsleep(double t);
extern double	dwait(double t);
extern void	nwait(int64_t nsec);

extern int64_t	ntime();

#endif /* PERFER_DTIME_H */
