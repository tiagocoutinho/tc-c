#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <bits/time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifndef HTTPD_LISTEN_BACKLOG
#define HTTPD_LISTEN_BACKLOG 50
#endif

#ifndef HTTPD_MAX_EVENTS
#define HTTPD_MAX_EVENTS 10
#endif

#ifdef HTTPD_ELOG
void httpd_elog(const char *format, ...) {
  char time_buf[32];
  pthread_t th = pthread_self();
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm *tm_info = localtime(&ts.tv_sec);
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
  char buf[512];
  snprintf(buf, sizeof(buf), "[%s.%06ld] [%lu] %s", time_buf, ts.tv_nsec / 1000,
           (unsigned long)th, format);
  va_list args;
  va_start(args, buf);
  vfprintf(stderr, buf, args);
  va_end(args);
}
#else
#define httpd_elog(...)
#endif

int httpd_isetsockopt(int fd, int level, int option, int value) {
  return setsockopt(fd, level, option, &value, sizeof(value));
}

#ifdef TRY
#define _HTTPD_TRY TRY
#endif
#define TRY(stmt)                                                              \
  if ((stmt) == -1) {                                                          \
    return -1;                                                                 \
  }

#ifdef TRY_CATCH
#define _HTTPD_TRY_CATCH TRY_CATCH
#endif
#define TRY_CATCH(stmt, catch)                                                 \
  if ((stmt) == -1) {                                                          \
    catch;                                                                     \
    return -1;                                                                 \
  }

// HttpdStr ------------------------------------------------------------------

typedef struct {
  size_t size;
  const char *data;
} HttpdStr;

#define STR(c) ((HttpdStr){.size = sizeof(c) - 1, .data = c})
#define STR_NULL httpd_str_from_parts(NULL, 0)
#define STR_Fmt "%.*s"
#define STR_Arg(s) (int)(s).size, (s).data
#define CRLF "\r\n"

HttpdStr httpd_str_from_parts(const char *d, size_t s) {
  return (HttpdStr){.size = s, .data = d};
}

bool httpd_str_index(HttpdStr str, char c, size_t *index) {
  for (size_t i = 0; i < str.size; ++i) {
    if (str.data[i] == c) {
      *index = i;
      return true;
    }
  }
  return false;
}

bool httpd_str_rindex(HttpdStr str, char c, size_t *index) {
  for (size_t i = str.size - 1; i >= 0; --i) {
    if (str.data[i] == c) {
      *index = i;
      return true;
    }
  }
  return false;
}

HttpdStr httpd_str_subc(HttpdStr str, char c) {
  size_t index;
  if (httpd_str_index(str, c, &index)) {
    return httpd_str_from_parts(str.data, index);
  }
  return str;
}

bool httpd_str_indexstr(HttpdStr str, HttpdStr sub, size_t *index) {
  for (size_t i = 0; i < (str.size - sub.size + 1); ++i) {
    if (memcmp(str.data + i, sub.data, sub.size) == 0) {
      *index = i;
      return true;
    }
  }
  return false;
}

HttpdStr httpd_str_substr(HttpdStr str, HttpdStr sub) {
  size_t index;
  if (httpd_str_indexstr(str, sub, &index)) {
    return httpd_str_from_parts(str.data, index);
  }
  return str;
}

void httpd_str_self_trim_left(HttpdStr *str, size_t n) {
  if (n > str->size) {
    n = str->size;
  }
  str->data += n;
  str->size -= n;
}

HttpdStr httpd_str_trim_left(HttpdStr str, size_t n) {
  httpd_str_self_trim_left(&str, n);
  return str;
}

bool httpd_str_eq(HttpdStr a, HttpdStr b) {
  return (a.size != b.size) ? false : memcmp(a.data, b.data, a.size) == 0;
}

HttpdStr httpd_str_strip_left(HttpdStr str) {
  size_t i = 0, n = str.size;
  while (i < n && str.data[i] == ' ') {
    i += 1;
  }
  return httpd_str_trim_left(str, i);
}

HttpdStr httpd_str_strip_right(HttpdStr str) {
  size_t i = 0, n = str.size;
  while (i < n && str.data[n - 1 - i] == ' ') {
    i += 1;
  }
  return httpd_str_from_parts(str.data, n - i);
}

HttpdStr httpd_str_strip(HttpdStr str) {
  return httpd_str_strip_right(httpd_str_strip_left(str));
}

bool httpd_str_starts_with(HttpdStr str, HttpdStr prefix) {
  if (prefix.size <= str.size) {
    HttpdStr actual_prefix = httpd_str_from_parts(str.data, prefix.size);
    return httpd_str_eq(actual_prefix, prefix);
  }
  return false;
}

bool httpd_str_ends_with(HttpdStr str, HttpdStr suffix) {
  if (suffix.size <= str.size) {
    HttpdStr actual_suffix =
        httpd_str_from_parts(str.data + str.size - suffix.size, suffix.size);
    return httpd_str_eq(actual_suffix, suffix);
  }
  return false;
}

// HttpdStrIO ----------------------------------------------------------------

typedef struct {
  char *buf;
  size_t max_size;
  size_t size;
} HttpdStrIO;

HttpdStrIO httpd_str_io(char *buf, size_t max_size) {
  return (HttpdStrIO){.buf = buf, .max_size = max_size, .size = 0};
}

void httpd_strio_printf(HttpdStrIO *s, const char *format, ...) {
  va_list args;
  va_start(args, format);
  s->size += vsnprintf(s->buf + s->size, s->max_size - s->size, format, args);
  va_end(args);
}

HttpdStr httpd_strio_str(HttpdStrIO s) {
  return httpd_str_from_parts(s.buf, s.size);
}

// Tools ---------------------------------------------------------------------

HttpdStr httpd_time_delta(const struct timespec *start,
                          const struct timespec *end, char *buf,
                          size_t bufsize) {
  struct timespec delta;

  delta.tv_sec = end->tv_sec - start->tv_sec;
  delta.tv_nsec = end->tv_nsec - start->tv_nsec;

  long seconds = delta.tv_sec;
  long ms = delta.tv_nsec / 1000000L;
  long us = (delta.tv_nsec % 1000000L) / 1000L;
  long ns = delta.tv_nsec % 1000L;

  int n;
  if (seconds > 0) {
    n = snprintf(buf, bufsize, "%ld.%03ld s", seconds, ms);
  } else if (ms > 0) {
    n = snprintf(buf, bufsize, "%ld.%03ld ms", us, ns / 1);
  } else if (us > 0) {
    n = snprintf(buf, bufsize, "%ld.%03ld us", us, ns / 1);
  } else {
    n = snprintf(buf, bufsize, "%ld ns", ns);
  }
  return httpd_str_from_parts(buf, n);
}

// HttpdAddress --------------------------------------------------------------

typedef struct {
  int domain;
  union {
    struct sockaddr address;
    struct sockaddr_in inet;
    struct sockaddr_in6 inet6;
    struct sockaddr_un unx;
  };
} HttpdAddress;

HttpdAddress httpd_address_inet(HttpdStr host, int port) {
  HttpdAddress addr = {.domain = AF_INET};
  if (inet_pton(AF_INET, host.data, &addr.inet.sin_addr) == -1) {
    return addr;
  };
  addr.inet.sin_family = AF_INET;
  addr.inet.sin_port = htons(port);
  return addr;
}

HttpdAddress httpd_address_inet6(HttpdStr host, int port) {
  HttpdAddress addr = {.domain = AF_INET6};
  if (inet_pton(AF_INET6, host.data, &addr.inet6.sin6_addr) == -1) {
    return addr;
  };
  addr.inet6.sin6_family = AF_INET6;
  addr.inet6.sin6_port = htons(port);
  return addr;
}

HttpdAddress httpd_address_unx(HttpdStr path) {
  HttpdAddress addr = {.domain = AF_UNIX};
  memset(&addr.unx, 0, sizeof(addr.unx));
  addr.unx.sun_family = AF_UNIX;
  strncpy(addr.unx.sun_path, path.data, path.size);
  return addr;
}

HttpdAddress httpd_address_url(HttpdStr url) {
  const HttpdStr unx = STR("unix://");
  const HttpdStr tcp = STR("tcp://");
  const HttpdStr tcp6 = STR("tcp6://");
  if (httpd_str_starts_with(url, unx)) {
    HttpdStr path = httpd_str_trim_left(url, unx.size);
    return httpd_address_unx(path);
  } else if (httpd_str_starts_with(url, tcp6)) {
    url = httpd_str_trim_left(url, tcp6.size);
    size_t index = 0;
    httpd_str_rindex(url, ':', &index);
    const HttpdStr host = httpd_str_from_parts(url.data, index);
    const int port = atoi(url.data + index + 1);
    return httpd_address_inet6(host, port);
  }
  url = httpd_str_trim_left(url, tcp.size);
  size_t index = 0;
  httpd_str_index(url, ':', &index);
  const HttpdStr host = httpd_str_from_parts(url.data, index);
  const int port = atoi(url.data + index + 1);
  return httpd_address_inet(host, port);
}

int httpd_address_len(HttpdAddress *address) {
  if (address->domain == AF_INET) {
    return sizeof(address->inet);
  }
  if (address->domain == AF_INET6) {
    return sizeof(address->inet6);
  }
  return sizeof(address->unx);
}

// HttpdConnection -----------------------------------------------------------

typedef struct {
  HttpdAddress addr;
  int fd;
} HttpdConnection;

int httpd_connection_connect(HttpdConnection *conn, HttpdAddress addr) {
  conn->addr = addr;
  conn->fd = -1;
  int fd;
  TRY(fd = socket(addr.domain, SOCK_STREAM | SOCK_NONBLOCK, 0));
  socklen_t socklen = httpd_address_len(&addr);
  if (addr.domain == AF_UNIX) {
    unlink(addr.unx.sun_path);
  } else {
    TRY_CATCH(httpd_isetsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 1), close(fd));
    TRY_CATCH(httpd_isetsockopt(fd, SOL_SOCKET, SO_REUSEPORT, 1), close(fd));
    TRY_CATCH(httpd_isetsockopt(fd, IPPROTO_TCP, TCP_NODELAY, 1), close(fd));
  }
  TRY_CATCH(bind(fd, &addr.address, socklen), close(fd));
  TRY_CATCH(listen(fd, HTTPD_LISTEN_BACKLOG), close(fd));
  conn->fd = fd;
  return 0;
}

int httpd_connection_accept(HttpdConnection *conn) {
  int fd, flags;
  HttpdAddress peer = {.domain = conn->addr.domain};
  socklen_t addr_len = httpd_address_len(&peer);
  TRY(fd = accept(conn->fd, &peer.address, &addr_len));
  TRY(flags = fcntl(fd, F_GETFL, 0));
  TRY(fcntl(fd, F_SETFL, flags | O_NONBLOCK));

  if (conn->addr.domain == AF_UNIX) {
    httpd_elog("Received connection from %s", peer.unx.sun_path);
  } else {
    TRY_CATCH(httpd_isetsockopt(fd, IPPROTO_TCP, TCP_NODELAY, 1), close(fd));
    char buf[128];
    if (conn->addr.domain == AF_INET) {
      inet_ntop(AF_INET, &peer.inet.sin_addr, buf, sizeof(buf));
      httpd_elog("Received connection from %s:%d\n", buf, peer.inet.sin_port);
    } else {
      inet_ntop(AF_INET6, &peer.inet6.sin6_addr, buf, sizeof(buf));
      httpd_elog("Received connection from %s:%d\n", buf, peer.inet6.sin6_port);
    }
  }
  return fd;
}

void httpd_connection_close(HttpdConnection *conn) {
  close(conn->fd);
  conn->fd = -1;
}

// HTTP ----------------------------------------------------------------------

const HttpdStr MGET = STR("GET");
const HttpdStr MPUT = STR("PUT");
const HttpdStr MPOST = STR("POST");
const HttpdStr MDELETE = STR("DELETE");
const HttpdStr MPATCH = STR("PATCH");
const HttpdStr MHEAD = STR("HEAD");
const HttpdStr MOPTIONS = STR("OPTIONS");

typedef enum {
  METHOD_ANY = 0,
  METHOD_GET = 1 << 0,
  METHOD_POST = 1 << 1,
  METHOD_PUT = 1 << 2,
  METHOD_DELETE = 1 << 3,
  METHOD_PATCH = 1 << 4,
  METHOD_HEAD = 1 << 5,
  METHOD_OPTIONS = 1 << 6,
} HttpdMethod;

int httpd_method_parse(HttpdStr m) {
  if (!m.size)
    return 0;
  if (httpd_str_eq(m, MGET))
    return METHOD_GET;
  if (httpd_str_eq(m, MPUT))
    return METHOD_PUT;
  if (httpd_str_eq(m, MPOST))
    return METHOD_POST;
  if (httpd_str_eq(m, MDELETE))
    return METHOD_DELETE;
  if (httpd_str_eq(m, MPATCH))
    return METHOD_PATCH;
  if (httpd_str_eq(m, MHEAD))
    return METHOD_HEAD;
  if (httpd_str_eq(m, MOPTIONS))
    return METHOD_OPTIONS;
  return METHOD_ANY;
}

typedef enum {
  s200 = 200,
  s304 = 304,
  s404 = 404,
} HttpdStatusCode;

typedef struct {
  HttpdStatusCode status_code;
  HttpdStr message;
} HttpdStatus;

const HttpdStatus S200 = {s200, STR("OK")};
const HttpdStatus S304 = {s304, STR("Not Modifified")};
const HttpdStatus S404 = {s404, STR("Not Found")};
const HttpdStr MIME_PLAIN = STR("text/plain");
const HttpdStr MIME_HTML = STR("text/html");
const HttpdStr HEADER_BODY_SEPARATOR = STR("\r\n\r\n");

typedef struct {
  HttpdStr key;
  HttpdStr value;
} HttpdKeyValue;

typedef struct {
  HttpdStr data;
  char _buf[4096];
  size_t _buf_size;
} HttpdIterator;

HttpdIterator httpd_iterator(HttpdStr data) {
  return (HttpdIterator){.data = data, ._buf_size = 0};
}

HttpdKeyValue httpd_iterator_next(HttpdIterator *q) {
  if (!q->data.size) {
    return (HttpdKeyValue){};
  }
  size_t field_sep = 0, field_end = 0;
  httpd_str_index(q->data, '=', &field_sep);
  if (!httpd_str_index(q->data, '&', &field_end)) {
    field_end = q->data.size;
  }
  HttpdKeyValue r = {
      .key = httpd_str_from_parts(q->data.data, field_sep),

  };
  HttpdStr raw_value = httpd_str_from_parts(q->data.data + field_sep + 1,
                                            field_end - field_sep - 1);
  httpd_str_self_trim_left(&q->data, field_end + 1);
  size_t percent;
  if (httpd_str_index(raw_value, '%', &percent)) {
    char *out = q->_buf + q->_buf_size;
    r.value = httpd_str_from_parts(out, 0);
    for (size_t i = 0; i < raw_value.size; ++i) {
      char c = raw_value.data[i];
      if (c == '%') {
        char hex[3] = {raw_value.data[i + 1], raw_value.data[i + 2], '\0'};
        c = (char)strtol(hex, NULL, 16);
        i += 2;
      }
      *out++ = c;
      r.value.size += 1;
    }
    q->_buf_size += r.value.size;
  } else {
    r.value = raw_value;
  }
  return r;
};

// HttpdQuery ----------------------------------------------------------------

HttpdIterator (*const httpd_query_iterator)(HttpdStr) = httpd_iterator;
HttpdKeyValue (*const httpd_query_iterator_next)(HttpdIterator *) =
    httpd_iterator_next;

// HttpdHeaderIterator -------------------------------------------------------

typedef HttpdKeyValue HttpdHeader;
typedef HttpdIterator HttpdHeaderIterator;

HttpdHeaderIterator httpd_header_iterator(HttpdStr data) {
  return (HttpdHeaderIterator){.data = data};
}

HttpdHeader httpd_header_iterator_next(HttpdHeaderIterator *it) {
  if (!it->data.size) {
    return (HttpdHeader){};
  }
  size_t field_sep = 0;
  httpd_str_index(it->data, ':', &field_sep);
  HttpdHeader r = {
      .key = httpd_str_from_parts(it->data.data, field_sep),
  };
  size_t field_end = 0;
  if (!httpd_str_indexstr(it->data, STR(CRLF), &field_end)) {
    field_end = it->data.size;
  }
  r.value = httpd_str_from_parts(it->data.data + field_sep + 1,
                                 field_end - field_sep - 1);
  r.value = httpd_str_strip(r.value);

  httpd_str_self_trim_left(&it->data, field_end + 2);
  return r;
}

// HttpdFormIterator ---------------------------------------------------------

HttpdIterator (*const httpd_form_iterator)(HttpdStr) = httpd_iterator;
HttpdKeyValue (*const httpd_form_iterator_next)(HttpdIterator *) =
    httpd_iterator_next;

// HttpdRequest --------------------------------------------------------------

typedef struct {
  int fd;
  HttpdStr payload;

  HttpdStr status_line;
  int method;

  HttpdStr full_path;
  HttpdStr path;
  HttpdStr query;
  HttpdStr protocol;

  HttpdStr header;
  HttpdStr body;
} HttpdRequest;

void httpd_request_parse_status_path(HttpdRequest *req) {
  size_t query_index = req->full_path.size;
  if (httpd_str_index(req->full_path, '?', &query_index)) {
    req->query = httpd_str_trim_left(req->full_path, query_index + 1);
  }
  req->path = httpd_str_from_parts(req->full_path.data, query_index);
}

HttpdRequest httpd_request_new(HttpdStr data) {
  // GET /path HTTP/1.1
  HttpdRequest req = {.payload = data,
                      .status_line = httpd_str_subc(data, '\r')};
  HttpdStr method = httpd_str_subc(req.status_line, ' ');
  req.method = httpd_method_parse(method);
  HttpdStr rest_status_line =
      httpd_str_trim_left(req.status_line, method.size + 1);
  req.full_path = httpd_str_subc(rest_status_line, ' ');

  httpd_request_parse_status_path(&req);

  req.protocol = httpd_str_trim_left(rest_status_line, req.full_path.size + 1);

  httpd_str_self_trim_left(&data, req.status_line.size + 2);
  size_t hb_index = 0;
  httpd_str_indexstr(data, HEADER_BODY_SEPARATOR, &hb_index);
  req.header = httpd_str_from_parts(data.data, hb_index + 2);
  req.body = httpd_str_trim_left(data, hb_index + 4);
  return req;
}

HttpdIterator httpd_request_query_iterator(HttpdRequest *req) {
  return httpd_query_iterator(req->query);
}

HttpdHeaderIterator httpd_request_header_iterator(HttpdRequest *req) {
  return httpd_header_iterator(req->header);
}

HttpdIterator httpd_request_form_iterator(HttpdRequest *req) {
  return httpd_form_iterator(req->body);
}

HttpdStr httpd_request_get_header(HttpdRequest *req, HttpdStr name) {
  HttpdHeaderIterator it = httpd_request_header_iterator(req);
  while (it.data.size) {
    HttpdHeader header = httpd_header_iterator_next(&it);
    if (httpd_str_eq(header.key, name)) {
      return header.value;
    }
  }
  return STR_NULL;
}

// HttpdRouter ---------------------------------------------------------------

typedef int (*HttpdRequestHandler)(HttpdRequest *);

typedef struct {
  int method;
  HttpdStr path;
  HttpdRequestHandler handler;
} HttpdRoute;

typedef struct {
  HttpdRoute *routes;
  size_t size;
  HttpdRoute NotFound;
} HttpdRouter;

HttpdRequestHandler HttpdRouter_find(HttpdRouter *router, HttpdRequest *req) {
  for (size_t i = 0; i < router->size; ++i) {
    const HttpdRoute *route = &router->routes[i];
    if (!route->method || (req->method & route->method)) {
      if (httpd_str_eq(req->path, router->routes[i].path)) {
        return router->routes[i].handler;
      }
    }
  }
  return router->NotFound.handler;
}

// HttpdServer  --------------------------------------------------------------

typedef struct {
  HttpdConnection connection;
  int epoll_fd;

  HttpdRouter router;
} HttpdServer;

int httpd_server_init(HttpdServer *server, HttpdConnection conn) {
  int epoll_fd;
  TRY(epoll_fd = epoll_create1(0));
  server->connection = conn;
  server->epoll_fd = epoll_fd;
  struct epoll_event event = {.events = EPOLLIN,
                              .data = {.fd = server->connection.fd}};
  TRY_CATCH(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->connection.fd, &event),
            { close(epoll_fd); });
  return 0;
}

void httpd_server_close(HttpdServer *s) {
  httpd_connection_close(&s->connection);
  if (s->epoll_fd > 0) {
    close(s->epoll_fd);
    s->epoll_fd = 0;
  }
}

int httpd_server_add_client(HttpdServer *s, int fd) {
  httpd_elog("adding new client\n");
  struct epoll_event event = {.events = EPOLLIN, .data = {.fd = fd}};
  TRY(epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, fd, &event));
  return 0;
}

int httpd_server_remove_client(HttpdServer *s, int fd) {
  httpd_elog("removing client\n");
  TRY(epoll_ctl(s->epoll_fd, EPOLL_CTL_DEL, fd, NULL));
  return 0;
}

int httpd_server_handle_request(HttpdServer *s, int fd) {
  char rbuf[4096];
  int n;
  TRY(n = recv(fd, rbuf, sizeof(rbuf), 0));
  if (n == 0) {
    return -2;
  }

#ifdef HTTPD_ELOG
  char logbuf[1024];
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif

  const HttpdStr payload = httpd_str_from_parts(rbuf, n);
  HttpdRequest req = httpd_request_new(payload);
  req.fd = fd;

#ifdef HTTPD_ELOG
  httpd_elog(STR_Fmt " START\n", STR_Arg(req.status_line));
  HttpdRequestHandler func = HttpdRouter_find(&s->router, &req);
  int result = func(&req);
  clock_gettime(CLOCK_MONOTONIC, &end);
  HttpdStr dt = httpd_time_delta(&start, &end, logbuf, sizeof(logbuf));
  httpd_elog(STR_Fmt " END " STR_Fmt "\n", STR_Arg(req.status_line),
             STR_Arg(dt));
  return result;
#else
  HttpdRequestHandler func = HttpdRouter_find(&s->router, &req);
  return func(&req);
#endif
}

int httpd_server_run(HttpdServer *s) {
  struct epoll_event events[HTTPD_MAX_EVENTS];
  while (1) {
    int n;
    TRY(n = epoll_wait(s->epoll_fd, events, HTTPD_MAX_EVENTS, -1));
    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      if (fd == s->connection.fd) {
        int client_fd;
        TRY_CATCH(client_fd = httpd_connection_accept(&s->connection),
                  perror("Accept error"));
        TRY(httpd_server_add_client(s, client_fd));
      } else {
        const int result = httpd_server_handle_request(s, fd);
        if (result < 0) {
          if (result == -1) {
            perror("Handle error");
          }
          httpd_server_remove_client(s, fd);
          close(fd);
        }
      }
    }
  }
}

int httpd_send_all(int fd, const char *payload, int n) {
  int i;
  while (n > 0) {
    TRY(i = send(fd, payload, n, 0));
    payload += i;
    n -= i;
  }
  return 0;
}

int httpd_put_header(char *buf, HttpdStr k, HttpdStr v) {
  if (v.size) {
    return sprintf(buf, STR_Fmt ": " STR_Fmt CRLF, STR_Arg(k), STR_Arg(v));
  }
  return 0;
}

int httpd_request_send_headers(HttpdRequest *req, HttpdStatus status,
                               HttpdStr content_type, size_t content_size,
                               HttpdStr extra_headers) {

  char write_buf[4096];
  char *buf = write_buf;
  buf += sprintf(buf, STR_Fmt " %d " STR_Fmt CRLF, STR_Arg(req->protocol),
                 status.status_code, STR_Arg(status.message));
  buf += httpd_put_header(buf, STR("Content-Type"), content_type);
  if (content_size) {
    buf += sprintf(buf, "Content-Length: %ld" CRLF, content_size);
  }
  if (extra_headers.size) {
    buf += sprintf(buf, STR_Fmt, STR_Arg(extra_headers));
  }
  buf += sprintf(buf, CRLF);
  return httpd_send_all(req->fd, write_buf, buf - write_buf);
}

int httpd_request_send_static(HttpdRequest *req, HttpdStatus status,
                              HttpdStr content_type, HttpdStr content) {
  TRY(httpd_request_send_headers(req, status, content_type, content.size,
                                 STR_NULL));
  return httpd_send_all(req->fd, content.data, content.size);
}

HttpdStr _httpd_file_etag(char *buf, struct stat *s) {
  int n = sprintf(buf, "\"%ld-%ld.%ld\"", s->st_size, s->st_mtim.tv_sec,
                  s->st_mtim.tv_nsec);
  return httpd_str_from_parts(buf, n);
}

int httpd_raw_send_file(HttpdRequest *req, const char *filename,
                        HttpdStatus status, HttpdStr content_type,
                        HttpdStr request_etag) {
  int src_fd, result;
  char _etag[64], _etag_header[96];
  struct stat s;
  TRY(stat(filename, &s));
  HttpdStr etag = _httpd_file_etag(_etag, &s);
  int etag_header_size = httpd_put_header(_etag_header, STR("ETag"), etag);
  HttpdStr etag_header = httpd_str_from_parts(_etag_header, etag_header_size);

  if (httpd_str_eq(request_etag, etag)) {
    return httpd_request_send_headers(req, S304, STR_NULL, 0, etag_header);
  }
  TRY(src_fd = open(filename, 0, O_RDONLY));
  TRY_CATCH(httpd_request_send_headers(req, status, content_type, s.st_size,
                                       etag_header),
            close(src_fd));
  TRY_CATCH(result = sendfile(req->fd, src_fd, NULL, s.st_size) == -1 ? -1 : 0,
            close(src_fd));
  close(src_fd);
  return result;
}

int httpd_request_send_file(HttpdRequest *req, HttpdStr filename) {
  HttpdStr etag = httpd_request_get_header(req, STR("If-None-Match"));
  HttpdStr mime = MIME_PLAIN;
  if (httpd_str_ends_with(filename, STR(".html"))) {
    mime = MIME_HTML;
  }

  return httpd_raw_send_file(req, filename.data, S200, mime, etag);
}

#ifdef _HTTPD_TRY
#define TRY _HTTPD_TRY
#undef _HTTPD_TRY
#endif

#ifdef _HTTPD_TRY_CATCH
#define TRY _HTTPD_TRY_CATH
#undef _HTTPD_TRY_CATCH
#endif
