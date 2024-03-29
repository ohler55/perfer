// Copyright 2016 by Peter Ohler, All Rights Reserved

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef WITH_OPENSSL
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "arg.h"
#include "drop.h"
#include "dtime.h"
#include "pool.h"
#include "perfer.h"
#include "stagger.h"

#ifndef OSX_OS
// this is gnu
extern int asprintf(char **strp, const char *fmt, ...);
#endif

#define VERSION	"1.5.3"

typedef struct _results {
    long	con_cnt;
    long	sent_cnt;
    long	ok_cnt;
    long	err_cnt;
    double	lat_sum;
    double	psum;
    double	lat;
    double	rate;
    int64_t	bytes;
} *Results;

static struct _perfer	perfer = {
    .inited = false,
    .done = false,
    .go = false,
    .pools = NULL,
    .url = NULL,
    .addr = NULL,
    .port = NULL,
    .path = NULL,
    .post = NULL,
    .addr_info = NULL,
    .tcnt = 1,
    .ccnt = 1,
    .meter = 0,
    .graph_width = 0,
    .graph_height = 0,
    .duration = 1.0,
    .req_file = NULL,
    .req_body = NULL,
    .req_len = 0,
    .backlog = 1, // PIPELINE_SIZE - 1,
    .poll_timeout = 0,
    .keep_alive = false,
    .verbose = false,
    .replace = false,
    .tls = false,
    .json = false,
    .use_epoll = false,
    .headers = NULL,
    .spread = NULL,
};

static const char	*help_lines[] = {
    "Saturates a web server with HTTP requests while tracking the number of requests,",
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
    "  -m <rate>               Set a metering rate in request per second.",
    "  --meter <rate>          (default: 0, indicating no metering)",
    "",
    "  -t <number>             Number of threads to use for sending requests and",
    "  --threads <number>      receiving responses. (default: 1)",
    "",
    "  -c <number>             Total number of connection to use for sending",
    "  --connections <number>  requests (default: 1)",
    "",
    "  -b <number>             Maximum backlog for pipeline on a connection.",
    "  --backlog <number>      (default: 1, range 1 - 15)",
    "",
    "  -l <percent,...>        Percentages of latency spread to report.",
    "  --latency <percent,...> (example: 10,20,30,40,50,60,70,80,90,99)",
    "",
    "  -g <wide>x<high>        Print a latency graph with the dimensions specified.",
    "  --graph <wide>x<high>",
    "",
    "  -r <file>               File with the full content of the HTTP request.",
    "  --request <file>        The -keep-alive option is ignored.",
    "",
    "  -k                      Keep connections alive instead of closing.",
    "  --keep-alive",
    "",
    "  -a <name: value>        Add an HTTP header field with name and value.",
    "  --add <name: value>",
    "",
    "  -p <content>            HTTP POST with the content provided.",
    "  --post <content>        example: -p 'mutation { repeat(word: \"Hello\") }'",
    "",
    "  -j                      JSON output.",
    "  --json",
    "",
    "  <url>                   URL for requests.",
    "                          example: http://localhost:6464/index.html",
    "",
    NULL
};
// hidden option is -z for poll_timeout
// hidden option is -e for use_epoll

static void
help(const char *app_name) {
    printf("\nusage: %s [options] <server-address>\n\n", app_name);
    for (const char **lp = help_lines; NULL != *lp; lp++) {
	printf("%s\n", *lp);
    }
}

static void
build_req(Perfer p) {
    char	*end;
    const char	*con = p->keep_alive ? "Keep-Alive" : "Close";
    const char	*method = (NULL == p->post) ? "GET" : "POST";
    size_t	clen = 0;
    int		size = snprintf(NULL, 0, "%s /%s HTTP/1.1\r\nHost: %s\r\nConnection: Keep-Alive\r\n\r\n",
				method, NULL == p->path ? "" : p->path, p->addr);

    for (Header h = p->headers; NULL != h; h = h->next) {
	size += strlen(h->line) + 2;
    }
    if (NULL != p->post) {
	clen = strlen(p->post);
	// size of content plus Content-Length: n\n\r\n
	size += strlen(p->post) + snprintf(NULL, 0, "Content-Length: %ld\r\n", clen);
    }
    if (NULL == (p->req_body = (char*)malloc(size + 1))) {
	printf("*-*-* Out of memory.\n");
	exit(-1);
    }
    p->req_len = size;
    end = p->req_body;
    end += sprintf(end, "%s /%s HTTP/1.1\r\nHost: %s\r\nConnection: %s\r\n",
		   method, NULL == p->path ? "" : p->path, p->addr, con);
    for (Header h = p->headers; NULL != h; h = h->next) {
	end = stpcpy(end, h->line);
	*end++ = '\r';
	*end++ = '\n';
    }
    if (NULL != p->post) {
	end += sprintf(end, "Content-Length: %ld\r\n", clen);
    }
    *end++ = '\r';
    *end++ = '\n';
    if (NULL != p->post) {
	end = stpcpy(end, p->post);
    }
    p->req_body[size] = '\0';
}

static bool
has_keep_alive(const char *str) {
    char	*lo = strdup(str);
    bool	has;

    for (char *s = lo; '\0' == *s; s++) {
	*s = tolower(*s);
    }
    has = (NULL != strstr(lo, "keep-alive"));
    free(lo);

    return has;
}

static int
parse_url(Perfer p) {
    const char	*url = p->url;

    if (0 == strncasecmp("http://", url, 7)) {
	url += 7;
	p->tls = false;
    } else if (0 == strncasecmp("https://", url, 8)) {
	printf("*-*-* TLS (https) not supported yet\n");
	return -1;
	url += 8;
	p->tls = true;
    } else {
	if (NULL != strstr(url, "://")) {
	    printf("*-*-* invalid URL\n");
	    return -1;
	}
    }
    p->addr = url;

    char	*s;

    if (NULL != (s = strchr(url, ':'))) {
	*s = '\0'; // end of address
	url = s + 1;
	p->port = url;
    }
    if (NULL != (s = strchr(url, '/'))) {
	*s = '\0'; // end of port or address
	url = s + 1;
	p->path = url;
    }
    return 0;
}

// Returns addrinfo for a host[:port] string with the default port of 80.
static struct addrinfo*
get_addr_info(const char *host, const char *port) {
    struct addrinfo	hints;
    struct addrinfo	*res;
    int			err;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (NULL == port) {
	err = getaddrinfo(host, "80", &hints, &res);
    } else {
	err = getaddrinfo(host, port, &hints, &res);
    }
    if (0 != err) {
	printf("*-*-* Failed to resolve %s.\n", host);
	return NULL;
    }
    return res;
}

static int
init_pools(Perfer p) {
    int		err;
    int		i;
    long	dcnt = p->ccnt / p->tcnt;
    long	rem = p->ccnt - (dcnt * p->tcnt);
    Pool	pool;

    if (NULL == (p->pools = (Pool)calloc(p->tcnt, sizeof(struct _pool)))) {
	printf("*-*-* Failed to allocate memory for thread pool. %s\n", strerror(errno));
	return ENOMEM;
    }
    for (i = p->tcnt, pool = p->pools; 0 < i; i--, pool++) {
	if (0 < rem) {
	    if (0 != (err = pool_init(pool, p, dcnt + 1))) {
		return err;
	    }
	    rem--;
	} else {
	    if (0 != (err = pool_init(pool, p, dcnt))) {
		return err;
	    }
	}
    }
    return 0;
}

static int
perfer_init(Perfer p, int argc, const char **argv) {
    const char	*app_name = *argv;
    const char	*opt_val = NULL;
    char	*end;
    int		cnt;

    if (0 != pthread_mutex_init(&p->print_mutex, 0)) {
	printf("%s\n", strerror(errno));
	return -1;
    }
    atomic_init(&p->sent_cnt, 0);
    atomic_init(&p->con_cnt, 0);
    atomic_init(&p->err_cnt, 0);
    atomic_init(&p->byte_cnt, 0);
    atomic_init(&p->ready_cnt, 0);

    stagger_init();

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
	    p->verbose = true;
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
	    p->duration = strtod(opt_val, &end);
	    if ('\0' != *end || 0.0 > p->duration) {
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
	switch (cnt = arg_match(argc, argv, &opt_val, "g", "-graph")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    p->graph_width = strtol(opt_val, &end, 10);
	    if ('x' != *end || 0 >= p->graph_width || 1024 < p->graph_width) {
		printf("'%s' is not a valid geometry specification.\n", opt_val);
		help(app_name);
		return -1;
	    }
	    p->graph_height = strtol(end + 1, &end, 10);
	    if ('\0' != *end || 0 >= p->graph_height || 1024 < p->graph_height) {
		printf("'%s' is not a valid geometry specification.\n", opt_val);
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
	    p->tcnt = strtol(opt_val, &end, 10);
	    if ('\0' != *end || 1 > p->tcnt) {
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
	switch (cnt = arg_match(argc, argv, &opt_val, "c", "-connections")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    p->ccnt = strtol(opt_val, &end, 10);
	    if ('\0' != *end || 1 > p->ccnt) {
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
	switch (cnt = arg_match(argc, argv, &opt_val, "b", "-backlog")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    p->backlog = strtol(opt_val, &end, 10);
	    if ('\0' != *end || 1 > p->backlog || PIPELINE_SIZE <= p->backlog) {
		printf("'%s' is not a valid backlog number.\n", opt_val);
		help(app_name);
		return -1;
	    }
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "m", "-meter")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    p->meter = strtol(opt_val, &end, 10);
	    if ('\0' != *end || 0 > p->meter) {
		printf("'%s' is not a valid meter rate.\n", opt_val);
		help(app_name);
		return -1;
	    }
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &p->req_file, "r", "-request")) {
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
	    p->keep_alive = true;
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, NULL, "j", "-json")) {
	case 0: // no match
	    break;
	case 1:
	    p->json = true;
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "a", "-add")) {
	case 0: // no match
	    break;
	case 1:
	case 2: {
	    Header	h = (Header)malloc(sizeof(struct _header));

	    if (NULL == h) {
		printf("*-*-* Out of memory.\n");
		exit(-1);
	    }
	    h->next = p->headers;
	    p->headers = h;
	    h->line = opt_val;
	    continue;
	    break;
	}
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "l", "-latency")) {
	case 0: // no match
	    break;
	case 1:
	case 2: {
	    Spread	tail = NULL;
	    Spread	s;

	    end = (char*)opt_val;
	    do {
		double	n = strtod(end, &end);

		if (('\0' != *end && ',' != *end) || n <= 0.0 || 100.0 < n ) {
		    printf("'%s' is not a valid percentage (0.0 to 100.0).\n", end);
		    help(app_name);
		    return -1;
		}
		if (',' == *end) {
		    end++;
		}
		if (NULL == (s = (Spread)malloc(sizeof(struct _spread)))) {
		    printf("*-*-* Out of memory.\n");
		    exit(-1);
		}
		s->next = NULL;
		s->percent = n;
		if (NULL == tail) {
		    p->spread = s;
		} else {
		    tail->next = s;
		}
		tail = s;
	    } while ('\0' != *end);
	    continue;
	    break;
	}
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "z", "-poll_timeout")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    p->poll_timeout = strtol(opt_val, &end, 10);
	    if ('\0' != *end || 0 > p->poll_timeout || 1000 <= p->poll_timeout) {
		printf("'%s' is not a valid poll timeout number.\n", opt_val);
		help(app_name);
		return -1;
	    }
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, NULL, "e", "-epoll")) {
	case 0: // no match
	    break;
	case 1:
	case 2:
	    p->use_epoll = true;
	    continue;
	    break;
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	switch (cnt = arg_match(argc, argv, &opt_val, "p", "-post")) {
	case 0: // no match
	    break;
	case 1:
	case 2: {
	    p->post = opt_val;
	    continue;
	    break;
	}
	default: // match but something went wrong
	    help(app_name);
	    return -1;
	}
	if (NULL == p->url) {
	    p->url = *argv;
	    cnt = 1;
	} else {
	    printf("*-*-* Only one URL is allowed.\n");
	    help(app_name);
	    return -1;
	}
    }
    if (NULL == p->url) {
	printf("*-*-* A URL is required.\n");
	help(app_name);
	return -1;
    }

    if (0 != parse_url(p)) {
	return -1;
    }
    if (0 != init_pools(p)) {
	return -1;
    }
    if (NULL == p->req_file) {
	build_req(p);
	if (0 > p->req_len) {
	    printf("*-*-* Failed to allocate memory for GET request.\n");
	    return -1;
	}
    } else {
	FILE	*f = fopen(p->req_file, "r");

	if (NULL == f) {
	    printf("-*-*- Failed to open '%s'. %s\n", p->req_file, strerror(errno));
	    return -1;
	}
	if (0 != fseek(f, 0, SEEK_END) ||
	    0 > (p->req_len = ftell(f)) ||
	    0 != fseek(f, 0, SEEK_SET)) {
	    printf("-*-*- Failed to determine file size for '%s'. %s\n", p->req_file, strerror(errno));
	    return -1;
	}
	if (NULL == (p->req_body = (char*)malloc(p->req_len + 1))) {
	    printf("-*-*- Failed to allocate memory for request.\n");
	    return -1;
	}
	if (p->req_len != fread(p->req_body, 1, p->req_len, f)) {
	    printf("-*-*- Failed to read %s.\n", p->req_file);
	    return -1;
	}
	p->req_body[p->req_len] = '\0';
	if (0 != fclose(f)) {
	    printf("-*-*- Failed to close %s. %s. ignoring\n", p->req_file, strerror(errno));
	}
	p->keep_alive = has_keep_alive(p->req_body);
	p->replace = (NULL != strstr(p->req_body, "${sequence}"));
    }
    p->inited = true;
    p->addr_info = get_addr_info(p->addr, p->port);
    if (!p->keep_alive || 0 < p->meter) {
	p->backlog = 1;
    }
#ifdef WITH_OPENSSL
    if (p->tls) {
	SSL_load_error_strings();
	ERR_load_BIO_strings();
	OpenSSL_add_all_algorithms();
    }
#endif
    return 0;
}

static void
perfer_cleanup(Perfer p) {
    if (!p->inited) {
	return;
    }
    p->inited = false;
    Pool	pool;
    int		i;

    for (pool = p->pools, i = p->tcnt; 0 < i; i--, pool++) {
	pool_cleanup(pool);
    }
    free(p->addr_info);
    free(p->pools);
    free(p->req_body);
}

void
perfer_stop(Perfer p) {
    p->done = true;

    Pool	pool;
    int		i;

    for (pool = p->pools, i = p->tcnt; 0 < i; i--, pool++) {
	pool_wait(pool);
    }
    perfer_cleanup(p);
}

static void
lat_graph(int w, int h) {
    w++;
    char	g[h * w];
    char	*r;
    int		i;

    memset(g, ' ', sizeof(g));
    for (r = g, i = 0; i < h; i++, r += w) {
	r[w-1] = '\0';
    }
    char	sep[w];

    memset(sep, '-', w - 1);
    sep[w - 1] = '\0';

    printf("\n");
    printf("Latency Distribution\n");
    printf("%s\n", sep);

    uint64_t	lw = stagger_at(0.95); // width of latency
    uint64_t	lh = 1;
    uint64_t	vals[w];
    uint64_t	linc = lw / w;
    uint64_t	min;
    uint64_t	v;
    int		y;

    for (i = 0; i < 16; i++) {
	if (linc < (1ULL << (i * 4))) {
	    break;
	}
    }
    if ((1ULL << (i * 4)) - linc > linc - (1ULL << ((i - 1) * 4))) {
	i--;
    }
    linc = 1ULL << (i * 4);
    memset(vals, 0, sizeof(vals));
    for (i = 0, min = linc; i < w - 1; i++, min += linc) {
	v = stagger_range(min, min + linc);
	vals[i] = v;
	if (lh < v) {
	    lh = v;
	}
    }
    h--;
    for (i = 0; i < w; i++) {
	y = h - (int)(vals[i] * h / lh);
	g[y * w + i] = '*';
    }
    for (r = g, i = 0; i < h; i++, r += w) {
	printf("%s\n", r);
    }
    printf("%s\n\n", sep);
}

static void
print_out(Perfer p, Results r) {
    if (0 < r->err_cnt) {
	printf("%s encountered %ld errors.\n", p->addr, r->err_cnt);
    }
    if (r->ok_cnt + r->err_cnt < r->sent_cnt) {
	printf("%s did not respond to %ld requests.\n", p->addr, r->sent_cnt - r->ok_cnt - r->err_cnt);
    }
    printf("Benchmarks for:\n");
    printf("  URL:             %s://%s:%s/%s\n",
	   p->tls ? "https" : "http",
	   p->addr,
	   (NULL == p->port) ? "80" : p->port,
	   NULL == p->path ? "" : p->path);
    printf("  Threads:         %ld\n", p->tcnt);
    printf("  Connections:     %ld\n", p->ccnt);
    printf("  Duration:        %0.1f seconds\n", r->psum);
    printf("  Keep-Alive:      %s\n", p->keep_alive ? "true" : "false");
    printf("Results:\n");
    if (0 < r->err_cnt) {
	printf("  Failures:        %ld\n", r->err_cnt);
    }
    printf("  Connections:     %ld connection established\n", (long)r->con_cnt);
    printf("  Requests:        %ld requests\n", (long)r->ok_cnt);
    printf("  Received:        %0.3f MB (%0.3f MB/sec)\n", (double)r->bytes / 1024.0 /1024.0, (double)r->bytes / 1024.0 /1024.0 / r->psum);
    printf("  Throughput:      %ld requests/second\n", (long)r->rate);
    printf("  Average Latency: %0.3f +/-%0.3f msecs (and stdev)\n", stagger_average() / 1000000.0, stagger_stddev() / 1000000.0);
    if (NULL == p->spread) {
	printf("  Mean Latency:    %0.3f\n", stagger_at(0.5) / 1000000.0);
    } else {
	printf("  Latency Spread:\n");
	for (Spread s = p->spread; NULL != s; s = s->next) {
	    printf("     % 3.2f%%:      %0.3f msecs\n", s->percent, stagger_at(s->percent / 100.0) / 1000000.0);
	}
    }
    if (0 < p->graph_width && 0 < p->graph_height) {
	lat_graph(p->graph_width, p->graph_height);
    }
    printf("\n");
}

static void
json_out(Perfer p, Results r) {
    printf("{\n");
    printf("  \"options\": {\n");
    printf("    \"url\": \"%s://%s:%s/%s\",\n",
	   p->tls ? "https" : "http",
	   p->addr,
	   (NULL == p->port) ? "80" : p->port,
	   NULL == p->path ? "" : p->path);
    printf("    \"threads\": %ld,\n", p->tcnt);
    printf("    \"connections\": %ld,\n", p->ccnt);
    printf("    \"duration\": %0.1f,\n", r->psum);
    printf("    \"keepAlive\": %s\n", p->keep_alive ? "true" : "false");
    printf("  },\n");
    printf("  \"results\": {\n");
    if (0 < r->err_cnt) {
	printf("    \"failures\": %ld,\n", r->err_cnt);
    }
    if (0 < r->err_cnt) {
	printf("    \"errors\": %ld,\n", r->err_cnt);
    }
    if (r->ok_cnt + r->err_cnt < r->sent_cnt) {
	printf("    \"noResponse\": %ld,\n", r->sent_cnt - r->ok_cnt - r->err_cnt);
    }
    printf("    \"connections\": %ld,\n", (long)r->con_cnt);
    printf("    \"requests\": %ld,\n", (long)r->ok_cnt);
    printf("    \"requestsPerSecond\": %ld,\n", (long)r->rate);
    printf("    \"totalBytes\": %lld,\n", r->bytes);
    printf("    \"latencyAverageMilliseconds\": %0.3f,\n", stagger_average() / 1000000.0);
    printf("    \"latencyMeanMilliseconds\": %0.3f,\n", stagger_at(0.5) / 1000000.0);
    printf("    \"latencyStdev\": %0.3f%s\n", stagger_stddev() / 1000000.0, NULL != p->spread ? "," : "");
    if (NULL != p->spread) {
	printf("    \"latencySpread\": {\n");
	for (Spread s = p->spread; NULL != s; s = s->next) {
	    printf("      \"%3.2f\": %0.3f%s\n", s->percent, stagger_at(s->percent / 100.0) / 1000000.0, NULL == s->next ? "" : ",");
	}
	printf("    }\n");
    }
    printf("  }\n");
    printf("}\n");
}

static int
warmup(Perfer p) {
    // Initialize connections before starting the benchmarks.
    if (p->keep_alive) {
	Pool	pool;
	int	i;
	int	err;

	for (pool = p->pools, i = p->tcnt; 0 < i; pool++, i--) {
	    if (0 != (err = pool_warmup(pool))) {
		return err;
	    }
	}
    }
    return 0;
}

static int
perfer_start(Perfer p) {
    uint64_t		i;
    int			err;
    struct _results	r;
    Pool		pool;
    Drop		d;
    int			tcnt = 0;
    double		giveup;

    memset(&r, 0, sizeof(r));
    atomic_store(&p->ready_cnt, 0);

    if (0 != (err = warmup(p))) {
	return err;
    }
    for (i = p->tcnt, pool = p->pools; 0 < i; i--, pool++) {
	if (0 != (err = pool_start(pool))) {
	    printf("*-*-* Failed to create IO threads. %s\n", strerror(err));
	    perfer_stop(p);
	    return err;
	}
    }
    giveup = dtime() + 4.0;
    while (atomic_load(&p->ready_cnt) < tcnt * 2) {
	if (giveup <= dtime()) {
	    printf("*-*-* timed out waiting for threads to start.\n");
	    perfer_stop(p);
	    return 1;
	}
	dsleep(0.1);
    }
    p->go = true;
    if (0 < p->meter) {
	int64_t	dur = (int64_t)(p->duration * 1000000000.0);
	int64_t	sep = 1000000000ULL / p->meter;
	int64_t	done = ntime() + dur;
	int64_t	next = ntime();
	int64_t	now;
	int	dcnt = p->ccnt / p->tcnt;

	for (now = ntime(); now < done; now = ntime()) {
	    if (next <= now) {
		pool = p->pools + (i / dcnt) % p->tcnt;
		pool_send(pool, i);
		i++;
		next += sep;
	    } else {
		nwait(next - now);
	    }
	}
    } else {
	dsleep(p->duration);
    }
    p->enough = true;
    for (i = p->tcnt, pool = p->pools; 0 < i; i--, pool++) {
	pool_wait(pool);
    }
    if (0 < p->meter) {
	r.psum = p->duration;
	tcnt = 1;
    } else {
	int	j;

	for (i = p->tcnt, pool = p->pools; 0 < i; i--, pool++) {
	    for (d = pool->drops, j = pool->dcnt; 0 < j; j--, d++) {
		if (0 < d->start_time && 0 < d->end_time) {
		    r.psum += (double)(d->end_time - d->start_time) / 1000000000.0;
		    tcnt++;
		}
	    }
	}
    }
    r.sent_cnt = atomic_load(&p->sent_cnt);
    r.con_cnt = atomic_load(&p->con_cnt);
    r.err_cnt = atomic_load(&p->err_cnt);
    r.bytes = atomic_load(&p->byte_cnt);
    r.ok_cnt = stagger_count();
    if (0.0 < r.psum) {
	r.psum /= tcnt;
	r.rate = (double)r.ok_cnt / r.psum;
    }
    if (p->json) {
	json_out(p, &r);
    } else {
	print_out(p, &r);
    }
    perfer_cleanup(p);

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
