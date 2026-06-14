# Micro http server in C

Simple, efficient http server with no malloc.

A lot of limitations:

* HTTP/1.1 only with limited request parsing. Certainly not intented to be robust or secure in the sense you would 
  be able to exploit to do some unintended action
* No long running responses like streamming
* Very simple and limited router. Path -> function


Compile examples with:

```
clang -Wall -ansi -O2 -std=c23 -o httpx httpx.c
```

or 

```
gcc -Wall -ansi -O2 -std=c2x -o httpx httpx.c
```

Add `-static` to any of the 2 above to create a statically linked binary.

Add `-DHTTPD_ELOG` to log requests on stderr.

Run with:

```
./httpx
```

For bit of extra performance:

```
taskset -c 1 ./httpx
```

The server is started with sockets in "REUSE_PORT" mode. This means you can spawn multiple
threads or processes with clones of the exact same server. The OS takes care of load balancing
the requests for you!
There are some caveats with "REUSE_PORT" so make sure you read about it before taking
advantage of this feature.

