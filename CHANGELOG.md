# CHANGELOG

### 1.5.2 - 2019-04-08

Duration bug fix

- Fixed the duration calculation for multiple threads in JSON output.

### 1.5.1 - 2019-04-02

Even faster!

- Changes in the code for better performance and cleanup of JSON output.

### 1.5.0 - 2019-03-23

Faster.

- Internals reworked for better performance.

- Latency spread and graph.

- Total bytes received added to results.

- JSON output option.

- Meter option for better latency comparisons.

### 1.4.0 - 2019-03-07

Command line options changed with a focus on using a URL.

- URL for host, port, and path.

- JSON ouput option added.

- `--post` option added to make it easier to benchmark POST requests.

### 1.3.0 - 2019-01-30

Add connection count to output.

### 1.2.0 - 2018-03-29

Changed results display and added standard deviation for latency.

### 1.1.1 - 2018-02-25

Fixed segfault on exit.

### 1.1.0 - 2018-02-25

Pipeline backlog option added.

### 1.0.1 - 2018-02-24

Fixed bug where zero bytes received counted as a response.

### 1.0.0 - 2018-02-11

Initial release.
