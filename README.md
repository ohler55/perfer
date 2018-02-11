# perfer

[![Build Status](https://img.shields.io/travis/ohler55/perfer/master.svg)](http://travis-ci.org/ohler55/perfer?branch=master)

HTTP performance benchmark tool.

## Usage

```
$ perfer --threads 4 --connections 20 --path index.html locahost:8080
```

## Installation

```
$ make
```

After running make the *perfer* app should be in the `bin` directory.

## What is it?

*perfer* is an HTTP performance benchmark tool. It will attempt to make as
many request as it can to an address and port. While sending requests and
receiving responses it tracks the throughtput and latency averages and reports
those at the end of the run. *perfer* was developed to be able to find the
limits on [OpO](http://opo.technology) and then on the
[Agoo](https://github.com/ohler55/agoo) Ruby gem.

## Releases

See [file:CHANGELOG.md](CHANGELOG.md)

Releases are made from the master branch. The default branch for checkout is
the develop branch. Pull requests should be made against the develop branch.

## Links

 - *GitHub* *repo*: https://github.com/ohler55/perfer

Follow [@peterohler on Twitter](http://twitter.com/#!/peterohler) for
announcements and news about the perfer application.


