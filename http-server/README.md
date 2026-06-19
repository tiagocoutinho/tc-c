# Micro http server in C

Simple, efficient, single header, HTTP 1.1 server library with **no malloc**. 
Yes, you read it right: **no malloc**.

## Supported

* strictly HTTP/1.1
* Bind to TCP IPv4, IPv6 and UNIX
* Bind to multiple addresses
* Simple router

## Limitations

A lot of them:

* HTTP 1.1
* Limited request parsing. Certainly not intented to be robust or secure in the sense you would 
  be able to exploit to do some unintended action
* No long running responses like streamming or SSE
* Very simple and limited router. Path & method -> function

## Getting started

This is a [single-header file library](https://github.com/nothings/stb/tree/master#how-do-i-use-these-libraries).

Define _once_ in your project `HTTPD_IMPLEMENTATION`.

```c
#define HTTPD_IMPLEMENTATION
#include "httpd.h"

HttpdStr home_page = STR("<html><body><h1>Hello, world!</h1></body></html>");
HttpdStr not_found = STR("<html><body><h3>404 Not Found</h3></body></html>");

int main(int argc, char **argv) {
  HttpdAddress addr = httpd_address_parse(STR("tcp://127.0.0.1:3210"));
  HttpdRouter router = {
      .routes =
          (HttpdRoute[]){
              httpd_route_static_html(STR("/"), home_page),
          },
      .size = 1,
      .NotFound = httpd_route_static_html(STR_NULL, not_found),
  };

  HttpdServer serv;
  TRY_CATCH(httpd_server_init(&serv, router), perror("HttpdServer init"));
  TRY_CATCH(httpd_server_bind(&serv, addr), perror("HttpdServer bind"));
  httpd_server_run(&serv);
  httpd_server_close(&serv);
  return 0;
}

```


## Examples

Compile examples with:

```
clang -Wall -ansi -O2 -std=c23 -o example example.c
```

or 

```
gcc -Wall -ansi -O2 -std=c2x -o example example.c
```

Add `--static` to any of the 2 above to create a statically linked binary.

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

