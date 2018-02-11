// Copyright 2016 by Peter Ohler, All Rights Reserved

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arg.h"
#include "drop.h"
#include "pool.h"
#include "perfer.h"

#ifndef OSX_OS
// this is gnu
extern int asprintf(char **strp, const char *fmt, ...);
#endif

#define VERSION	"1.0"

static struct _Perfer	perfer = {
    .inited = false,
    .done = false,
    .pools = NULL,
    .addr = NULL,
    .path = "index.html",
    .tcnt = 1,
    .ccnt = 1,
    .duration = 1.0,
    .start_time = 0.0,
    .ready_cnt = 0,
    .seq = 0,
    .req_file = NULL,
    .req_body = NULL,
    .req_len = 0,
    .keep_alive = false,
    .verbose = false,
    .replace = false,
};

static const char	*help_lines[] = {
    "saturate a web server with HTTP requests while tracking the number of requests,",
    "latency, and throughput. After the duration specified no more requests are sent",
    "and the application terminates when all responses have been received or 10",
    "seconds, which ever is sooner.",
    "",
    "  -h                      This help page.",
    "  --help",
    "",
    "  -v                      Verbose mode.",
    "",
    "  --version               Version of this application.",
    "",
    "  -d <duration>           Duration in seconds for the run. Positive decimal",
    "  --duration <duration>   values are accepted.",
    "",
    "  -n <count>              Number of requests to send. The value will be",
    "  --number <count>        rounded to the lowest multiple of the number of",
    "                          threads.",
    "",
    "  -t <number>             Number of threads to use for sending requests.",
    "  --threads <number>      (default: 1)",
    "",
    "  -c <number>             Number of connection to use for sending requests.",
    "  --connections <number>  (default: 1000)",
    "",
    "  -p <path>               URL path of the HTTP request. (default: /)",
    "  --path <path>",
    "",
    "  -r <file>               File with the full content of the HTTP request.",
    "  --request <file>        The -keep-alive option is ignored.",
    "",
    "  -k                      Keep connections alive instead of closing.",
    "  --keep-alive",
    "",
    "  <server-address>        IP address of the server to send requests to.",
    ""
};

static void
help(const char *app_name) {
    printf("\nusage: %s [options] <server-address>\n\n", app_name);
    for (const char **lp = help_lines; NULL != *lp; lp++) {
	printf("%s\n", *lp);
    }
}

static int
perfer_init(Perfer h, int argc, const char **argv) {
    const char	*app_name = *argv;
    const char	*opt_val = NULL;
    char	*end;
    int		cnt;
    long	num = 0;
    
    argv++;
    argc--;
    for (; 0 < argc; argc -= cnt, argv += cnt) {
	if (0 != (cnt = arg_match(argc, argv, &opt_val, "h", "-help"))) {
	    help(app_name);
	    return 1;
	}
	switch (cnt = arg_match(argc, argv, NULL, "-version", "-version")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    printf("perfer version %s\n", VERSION);
	    return 1;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, NULL, "v", "-verbose")) {
	case 0: // no match
	    break;
	case 1:
	    h->verbose = true;
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "d", "-duration")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    h->duration = strtod(opt_val, &end);
	    if ('\0' != *end || 0.0 > h->duration) {
		printf("'%s' is not a valid duration.\n", opt_val);
		help(app_name);
		return -1;
	    }
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "n", "-number")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    num = strtol(opt_val, &end, 10);
	    if ('\0' != *end || 0 >= num) {
		printf("'%s' is not a valid number.\n", opt_val);
		help(app_name);
		return -1;
	    }
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "t", "-threads")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    h->tcnt = strtol(opt_val, &end, 10);
	    if ('\0' != *end || 1 > h->tcnt) {
		printf("'%s' is not a valid thread count.\n", opt_val);
		help(app_name);
		return -1;
	    }
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "c", "-connection")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    h->ccnt = strtol(opt_val, &end, 10);
	    if ('\0' != *end || 1 > h->ccnt) {
		printf("'%s' is not a valid connection number.\n", opt_val);
		help(app_name);
		return -1;
	    }
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &h->path, "p", "-path")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &h->req_file, "r", "-request")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, NULL, "k", "-keep-alive")) {
	case 0: // no match
	    break;
	case 1:
	    h->keep_alive = true;
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	if (NULL == h->addr) {
	    h->addr = *argv;
	    cnt = 1;
	} else {
	    printf("Only one server address is allowed.\n");
	    help(app_name);
	    return -1;
	}
    }
    if (NULL == h->addr) {
	printf("A server address is required.\n");
	help(app_name);
	return -1;
    }
    if (NULL == h->req_file) {
	if ('/' == *h->path) {
	    h->path = h->path + 1;
	}	
	if (h->keep_alive) {
	    h->req_len = asprintf(&h->req_body, "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: Keep-Alive\r\n\r\n", h->path, h->addr);
	} else {
	    h->req_len = asprintf(&h->req_body, "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: Close\r\n\r\n", h->path, h->addr);
	}
	if (0 > h->req_len) {
	    printf("*-*-* Failed to allocate memory for GET request.\n");
	    return -1;
	}
    } else {
	FILE	*f = fopen(h->req_file, "r");

	if (NULL == f) {
	    printf("-*-*- Failed to open '%s'. %s\n", h->req_file, strerror(errno));
	    return -1;
	}
	fseek(f, 0, SEEK_END);
	h->req_len = (int)ftell(f);
	rewind(f);
	if (NULL == (h->req_body = (char*)malloc(h->req_len + 1))) {
	    printf("-*-*- Failed to allocate memory for request.\n");
	    return -1;
	}
	if (h->req_len != fread(h->req_body, 1, h->req_len, f)) {
	    printf("-*-*- Failed to read %s.\n", h->req_file);
	    return -1;
	}
	h->req_body[h->req_len] = '\0';
	fclose(f);
	h->replace = (NULL != strstr(h->req_body, "${sequence}"));
    }
    h->inited = true;
    if (NULL == (h->pools = (Pool)malloc(sizeof(struct _Pool) * h->tcnt))) {
	printf("-*-*- Failed to allocate %ld threads.\n", h->tcnt);
	return -1;
    }
    Pool	p;
    int		i;
    int		err;
    long	n = num / h->tcnt;
    
    for (p = h->pools, i = h->tcnt; 0 < i; i--, p++) {
	if (0 != (err = pool_init(p, h, n))) {
	    return err;
	}
    }
    return 0;
}

static void
perfer_cleanup(Perfer h) {
    if (!h->inited) {
	return;
    }
    h->inited = false;
    Pool	p;
    int		i;

    for (p = h->pools, i = h->tcnt; 0 < i; i--, p++) {
	pool_cleanup(p);
    }
    free(h->pools);
    free(h->req_body);
}

void
perfer_stop(Perfer h) {
    h->done = true;
    
    Pool	p;
    int		i;

    for (p = h->pools, i = h->tcnt; 0 < i; i--, p++) {
	pool_wait(p);
    }
    perfer_cleanup(h);
}

static int
perfer_start(Perfer h) {
    Pool	p;
    int		i;
    int		err;
    long	sent_cnt = 0;
    long	ok_cnt = 0;
    long	err_cnt = 0;
    double	lat_sum = 0.0;
    double	psum = 0.0;
    
    for (p = h->pools, i = h->tcnt; 0 < i; i--, p++) {
	if (0 != (err = pool_start(p))) {
	    perfer_stop(h);
	    return err;
	}
    }
    for (p = h->pools, i = h->tcnt; 0 < i; i--, p++) {
	pool_wait(p);
	sent_cnt += p->sent_cnt;
	ok_cnt += p->ok_cnt;
	err_cnt += p->err_cnt;
	lat_sum += p->lat_sum;
	psum += p->actual_end - h->start_time;
    }
    // TBD actual times for each thread
    if (0 < err_cnt) {
	printf("%s encountered %ld errors.\n", h->addr, err_cnt);
    }
    if (ok_cnt + err_cnt < sent_cnt) {
	printf("%s did not respond to %ld requests.\n", h->addr, sent_cnt - ok_cnt - err_cnt);
    }
    printf("%s processed %ld requests in %0.3f seconds for a rate of %ld Requests/sec.\n",
	   h->addr, ok_cnt, psum / h->tcnt, (long)((double)ok_cnt / (psum / h->tcnt)));
    printf("with an average latency of %0.3f msecs\n", lat_sum * 1000.0 / ok_cnt);

    return 0;
}

static void
sig_handler(int sig) {
    if (perfer.inited) {
        perfer_stop(&perfer);
        perfer.inited = false;
    }
    exit(sig);
}

int
main(int argc, const char **argv) {
    int	err;
    
    if (0 != (err = perfer_init(&perfer, argc, argv))) {
	return err;
    }
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    if (0 != (err = perfer_start(&perfer))) {
	return err;
    }
    perfer_cleanup(&perfer);

    return 0;
}
