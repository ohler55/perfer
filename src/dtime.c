// Copyright 2009, 2015, 2016, 2018 by Peter Ohler, All Rights Reserved

#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "dtime.h"

#define MIN_SLEEP	(1.0 / (double)CLOCKS_PER_SEC)
#define MIN_NSLEEP	(1000000000ULL / CLOCKS_PER_SEC)

#ifndef CLOCK_REALTIME_COURSE
#define CLOCK_REALTIME_COURSE	CLOCK_REALTIME
#endif

double
dtime(void) {
    struct timespec	ts;

    clock_gettime(CLOCK_REALTIME_COURSE, &ts);

    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int64_t
ntime(void) {
    struct timespec	ts;

    clock_gettime(CLOCK_REALTIME_COURSE, &ts);

    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

double
dsleep(double t) {
    struct timespec	req, rem;

    if (MIN_SLEEP > t) {
	t = MIN_SLEEP;
    }
    req.tv_sec = (time_t)t;
    req.tv_nsec = (long)(1000000000.0 * (t - (double)req.tv_sec));
    if (nanosleep(&req, &rem) == -1 && EINTR == errno) {
	return (double)rem.tv_sec + (double)rem.tv_nsec / 1000000000.0;
    }
    return 0.0;
}

void
nwait(int64_t nsec) {
    if (MIN_NSLEEP < nsec) {
	struct timespec	req, rem;

	nsec -= MIN_NSLEEP;
	req.tv_sec = (time_t)(nsec / 1000000000LL);
	req.tv_nsec = (long)(nsec - req.tv_sec * 1000000000LL);
	nanosleep(&req, &rem);
    }
}

double
dwait(double t) {
    double	end = dtime() + t;

    if (MIN_SLEEP < t) {
	struct timespec	req, rem;

	t -= MIN_SLEEP;
	req.tv_sec = (time_t)t;
	req.tv_nsec = (long)(1000000000.0 * (t - (double)req.tv_sec));
	nanosleep(&req, &rem);
    }
    while (dtime() < end) {
	continue;
    }
    return 0.0;
}
