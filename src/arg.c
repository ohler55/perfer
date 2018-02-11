// Copyright 2015, 2016 by Peter Ohler, All Rights Reserved

#include <string.h>
#include <stdio.h>

#include "arg.h"

int
arg_match(int argc, const char **argv, const char **argp, const char *pat1, const char *pat2) {
    const char	*a = *argv;
    int		plen1, plen2;

    if ('-' != *a) {
	return 0;
    }
    a++; // past the -
    if (0 == strcmp(a, pat1) || 0 == strcmp(a, pat2)) {
	if (NULL != argp) { // expect another arg
	    if (2 > argc) {
		printf("option -%s expects a value.\n", a);
		return -1;
	    }
	    argv++;
	    argc--;
	    *argp = *argv;
	    return 2;
	}
	return 1;
    }
    // If an arg value is not expected then nothing else to check.
    if (NULL == argp) {
	return 0;
    }
    // Could be a -x=something formatted argument.
    plen1 = strlen(pat1);
    plen2 = strlen(pat2);
    if (0 == strncmp(a, pat1, plen1) && '=' == a[plen1]) {
	*argp = a + plen1 + 1;
	return 1;
    }
    if (0 == strncmp(a, pat2, plen2) && '=' == a[plen2]) {
	*argp = a + plen2 + 1;
	return 1;
    }
    return 0;
}
