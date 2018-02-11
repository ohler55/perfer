// Copyright 2016 by Peter Ohler, All Rights Reserved

#include <sys/socket.h>
#include <unistd.h>

#include "perfer.h"
#include "drop.h"

int
drop_init(Drop d, struct _Perfer *h) {
    d->h = h;
    d->sock = 0;
    d->sent = false;
    d->rcnt = 0;
    d->pp = NULL;
    d->start = 0.0;
    
    return 0;
}

void
drop_cleanup(Drop d) {
    if (0 != d->sock) {
	close(d->sock);
    }
    d->sock = 0;
    d->sent = false;
    d->rcnt = 0;
    d->pp = NULL;
    d->start = 0.0;
}


