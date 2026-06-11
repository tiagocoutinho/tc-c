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

#define LISTEN_BACKLOG 50
#define MAX_EVENTS 10

typedef struct sockaddr Addr;
typedef struct sockaddr_in InetAddr;
typedef struct sockaddr_un UnixAddr;

typedef struct epoll_event Event;

void elog(const char *format, ...) {
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

int isetsockopt(int fd, int level, int option, int value) {
  return setsockopt(fd, level, option, &value, sizeof(value));
}

#define TRY(stmt)                                                              \
  if ((stmt) == -1) {                                                          \
    return -1;                                                                 \
  }

#define TRY_CATCH(stmt, catch)                                                 \
  if ((stmt) == -1) {                                                          \
    catch;                                                                     \
    return -1;                                                                 \
  }

// Str -----------------------------------------------------------------------

typedef struct {
  size_t size;
  const char *data;
} Str;

#define SL(c) ((Str){.size = sizeof(c) - 1, .data = c})
#define STR(c) str_from_parts(c, sizeof(c) - 1)
#define STR_NULL str_from_parts(NULL, 0)
#define STR_Fmt "%.*s"
#define STR_Arg(s) (int)(s).size, (s).data

#define NL "\r\n"

Str str_from_parts(const char *d, size_t s) {
  return (Str){.size = s, .data = d};
}
Str str_from_c0(const char *d) { return (Str){.size = strlen(d), .data = d}; }

bool str_index(Str str, char c, size_t *index) {
  for (size_t i = 0; i < str.size; ++i) {
    if (str.data[i] == c) {
      *index = i;
      return true;
    }
  }
  return false;
}

Str str_subc(Str str, char c) {
  size_t index;
  if (str_index(str, c, &index)) {
    return str_from_parts(str.data, index);
  }
  return str;
}

bool str_indexstr(Str str, Str sub, size_t *index) {
  for (size_t i = 0; i < (str.size - sub.size + 1); ++i) {
    if (memcmp(str.data + i, sub.data, sub.size) == 0) {
      *index = i;
      return true;
    }
  }
  return false;
}

Str str_substr(Str str, Str sub) {
  size_t index;
  if (str_indexstr(str, sub, &index)) {
    return str_from_parts(str.data, index);
  }
  return str;
}

Str str_trim_left(Str str, size_t n) {
  if (n > str.size) {
    n = str.size;
  }
  return (Str){.size = str.size - n, .data = str.data + n};
}

bool str_eq(Str a, Str b) {
  return (a.size != b.size) ? false : memcmp(a.data, b.data, a.size) == 0;
}

Str str_strip_left(Str str) {
  size_t i = 0, n = str.size;
  while (i < n && str.data[i] == ' ') {
    i += 1;
  }
  return str_trim_left(str, i);
}

Str str_strip_right(Str str) {
  size_t i = 0, n = str.size;
  while (i < n && str.data[n - 1 - i] == ' ') {
    i += 1;
  }
  return str_from_parts(str.data, n - i);
}

Str str_strip(Str str) { return str_strip_right(str_strip_left(str)); }

bool str_starts_with(Str str, Str prefix) {
  if (prefix.size <= str.size) {
    Str actual_prefix = str_from_parts(str.data, prefix.size);
    return str_eq(actual_prefix, prefix);
  }
  return false;
}

// Address -------------------------------------------------------------------

typedef struct {
  int domain;
  union {
    InetAddr inet;
    UnixAddr unix;
  };
} Address;

Address Address_inet(Str host, int port) {
  Address addr = {.domain = AF_INET};
  if (inet_pton(AF_INET, host.data, &addr.inet.sin_addr) == -1) {
    return addr;
  };
  addr.inet.sin_family = AF_INET;
  addr.inet.sin_port = htons(port);
  return addr;
}

Address Address_unix(Str path) {
  Address addr = {.domain = AF_UNIX};
  memset(&addr.unix, 0, sizeof(addr.unix));
  addr.unix.sun_family = AF_UNIX;
  strncpy(addr.unix.sun_path, path.data, path.size);
  return addr;
}

Address Address_url(Str url) {
  const Str unix = STR("unix://");
  const Str tcp = STR("tcp://");
  if (str_starts_with(url, unix)) {
    Str path = str_trim_left(url, unix.size);
    return Address_unix(path);
  }
  url = str_trim_left(url, tcp.size);
  size_t index = 0;
  str_index(url, ':', &index);
  const Str host = str_from_parts(url.data, index);
  const int port = atoi(url.data + index + 1);
  return Address_inet(host, port);
}

// Connection ----------------------------------------------------------------

typedef struct {
  Address addr;
  int fd;
} Connection;

int Connection_connect(Connection *conn, Address addr) {
  conn->addr = addr;
  conn->fd = -1;
  int fd;
  TRY(fd = socket(addr.domain, SOCK_STREAM | SOCK_NONBLOCK, 0));
  socklen_t socklen;
  Addr *address;
  if (addr.domain == AF_UNIX) {
    unlink(addr.unix.sun_path);
    socklen = sizeof(addr.unix);
    address = (Addr *)&addr.unix;
  } else {
    TRY_CATCH(isetsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 1), close(fd));
    TRY_CATCH(isetsockopt(fd, SOL_SOCKET, SO_REUSEPORT, 1), close(fd));
    TRY_CATCH(isetsockopt(fd, IPPROTO_TCP, TCP_NODELAY, 1), close(fd));
    socklen = sizeof(addr.inet);
    address = (Addr *)&addr.inet;
  }
  TRY_CATCH(bind(fd, address, socklen), close(fd));
  TRY_CATCH(listen(fd, LISTEN_BACKLOG), close(fd));
  conn->fd = fd;
  return 0;
}

int Connection_accept(Connection *conn) {
  int fd, flags;
  if (conn->addr.domain == AF_UNIX) {
    UnixAddr addr;
    socklen_t addr_len = sizeof(addr);
    TRY(fd = accept(conn->fd, (Addr *)&addr, &addr_len));
    TRY(flags = fcntl(fd, F_GETFL, 0));
    TRY(fcntl(fd, F_SETFL, flags | O_NONBLOCK));
    Str name = str_from_c0(addr.sun_path);
    elog("Received connection from " STR_Fmt "\n", STR_Arg(name));
  } else {
    InetAddr addr;
    socklen_t addr_len = sizeof(conn->addr.inet);
    TRY(fd = accept(conn->fd, (Addr *)&conn->addr.inet, &addr_len));
    TRY(flags = fcntl(fd, F_GETFL, 0));
    TRY(fcntl(fd, F_SETFL, flags | O_NONBLOCK));
    TRY_CATCH(isetsockopt(fd, IPPROTO_TCP, TCP_NODELAY, 1), close(fd));
    char buf[128];
    Str name =
        str_from_c0(inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf)));
    elog("Received connection from " STR_Fmt " %d\n", STR_Arg(name),
         addr.sin_port);
  }
  return fd;
}

void Connection_close(Connection *conn) {
  close(conn->fd);
  conn->fd = -1;
}

// HTTP ----------------------------------------------------------------------

typedef enum {
  s200 = 200,
  s304 = 304,
  s404 = 404,
} StatusCode;

typedef struct {
  StatusCode status_code;
  Str message;
} Status;

const Status S200 = {s200, SL("OK")};
const Status S304 = {s304, SL("Not Modifified")};
const Status S404 = {s404, SL("Not Found")};
const Str MIME_HTML = SL("text/html");
const Str HEADER_BODY_SEPARATOR = SL("\r\n\r\n");

// Header --------------------------------------------------------------------

typedef struct {
  Str name;
  Str value;
} Header;

// Request -------------------------------------------------------------------

typedef struct {
  int fd;
  Str payload;
  Str status_line;
  Str method;
  Str path;
  Str protocol;
  Str body;
} Request;

// Router --------------------------------------------------------------------

typedef int (*RequestHandler)(Request *);

typedef struct {
  Str path;
  RequestHandler handler;
} Route;

typedef struct {
  Route *routes;
  size_t size;
  Route NotFound;
} Router;

RequestHandler Router_find(Router *router, Str path) {
  for (size_t i = 0; i < router->size; ++i) {
    if (str_eq(path, router->routes[i].path)) {
      return router->routes[i].handler;
    }
  }
  return router->NotFound.handler;
}

// Server --------------------------------------------------------------------

typedef struct {
  Connection connection;
  int epoll_fd;

  Router router;
} Server;

int Server_init(Server *server, Connection conn) {
  int epoll_fd;
  TRY(epoll_fd = epoll_create1(0));
  server->connection = conn;
  server->epoll_fd = epoll_fd;
  Event event = {.events = EPOLLIN, .data = {.fd = server->connection.fd}};
  TRY_CATCH(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->connection.fd, &event),
            { close(epoll_fd); });
  return 0;
}

void Server_close(Server *s) {
  Connection_close(&s->connection);
  if (s->epoll_fd > 0) {
    close(s->epoll_fd);
    s->epoll_fd = 0;
  }
}

int Server_add_client(Server *s, int fd) {
  elog("adding new client\n");
  Event event = {.events = EPOLLIN, .data = {.fd = fd}};
  TRY(epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, fd, &event));
  return 0;
}

int Server_remove_client(Server *s, int fd) {
  elog("removing client\n");
  TRY(epoll_ctl(s->epoll_fd, EPOLL_CTL_DEL, fd, NULL));
  return 0;
}

Request Request_new(const Str payload) {
  // GET /path HTTP/1.1
  size_t status_size = 0;
  str_index(payload, '\r', &status_size);
  Request req = {.payload = payload,
                 .status_line = str_from_parts(payload.data, status_size)};
  req.method = str_subc(req.status_line, ' ');
  Str rest_status_line = str_trim_left(req.status_line, req.method.size + 1);
  req.path = str_subc(rest_status_line, ' ');
  req.protocol = str_trim_left(rest_status_line, req.path.size + 1);
  return req;
}

Str Request_header(Request *req) {
  size_t start = req->status_line.size + 2;
  Str rest =
      str_from_parts(req->payload.data + start, req->payload.size - (start));
  size_t hb_index = 0;
  str_indexstr(rest, HEADER_BODY_SEPARATOR, &hb_index);
  return str_from_parts(rest.data, hb_index + 2);
}

int Request_headers(Request *req, Header *headers, size_t max_size) {
  Str header = Request_header(req);
  size_t index, sep = 0;
  for (int i = 0; i < max_size; ++i) {
    if (str_index(header, '\r', &index) == false) {
      return i;
    }
    Str line = str_from_parts(header.data, index);
    str_index(line, ':', &sep);
    Str value = str_from_parts(line.data + sep + 1, line.size - (sep + 1));
    value = str_strip(value);
    headers[i] =
        (Header){.name = str_from_parts(line.data, sep), .value = value};
    header = str_trim_left(header, line.size + 2);
  }
  return 0;
}

Str Headers_get_header(Header *headers, int n, Str name) {
  for (int i = 0; i < n; ++i) {
    if (str_eq(headers[i].name, name)) {
      return headers[i].value;
    }
  }
  return STR_NULL;
}

Str Request_body(Request *req) {
  size_t hb_index = 0;
  str_indexstr(req->payload, HEADER_BODY_SEPARATOR, &hb_index);
  size_t start = hb_index + HEADER_BODY_SEPARATOR.size;
  return str_trim_left(req->payload, start);
}

Str time_delta(const struct timespec *start, const struct timespec *end,
               char *buf, size_t bufsize) {
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
  return str_from_parts(buf, n);
}

int Server_handle_request(Server *s, int fd) {
  char rbuf[16 * 1024], lbuf[1024];
  int n;
  TRY(n = recv(fd, rbuf, sizeof(rbuf), 0));
  if (n == 0) {
    return -2;
  }
  const Str payload = str_from_parts(rbuf, n);
  Request req = Request_new(payload);
  req.fd = fd;

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  elog(STR_Fmt " START\n", STR_Arg(req.status_line));
  RequestHandler func = Router_find(&s->router, req.path);
  int result = func(&req);
  clock_gettime(CLOCK_MONOTONIC, &end);
  Str dt = time_delta(&start, &end, lbuf, sizeof(lbuf));
  elog(STR_Fmt " END " STR_Fmt "\n", STR_Arg(req.status_line), STR_Arg(dt));
  return result;
}

int Server_loop(Server *s) {
  Event events[MAX_EVENTS];
  while (1) {
    int n;
    TRY(n = epoll_wait(s->epoll_fd, events, MAX_EVENTS, -1));
    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      if (fd == s->connection.fd) {
        int client_fd;
        TRY_CATCH(client_fd = Connection_accept(&s->connection),
                  perror("Accept error"));
        TRY(Server_add_client(s, client_fd));
      } else {
        const int result = Server_handle_request(s, fd);
        if (result < 0) {
          if (result == -1) {
            perror("Handle error");
          }
          Server_remove_client(s, fd);
          close(fd);
        }
      }
    }
  }
}

int send_all(int fd, const char *payload, int n) {
  int i;
  while (n > 0) {
    TRY(i = send(fd, payload, n, 0));
    payload += i;
    n -= i;
  }
  return 0;
}

int send_header(Request *req, Status status, Str content_type,
                size_t content_size, Str etag) {

  char write_buf[512];
  int n = sprintf(write_buf, STR_Fmt " %d " STR_Fmt NL, STR_Arg(req->protocol),
                  status.status_code, STR_Arg(status.message));
  if (content_type.size) {
    n += sprintf(write_buf + n, "Content-Type: " STR_Fmt NL,
                 STR_Arg(content_type));
  }
  if (content_size) {
    n += sprintf(write_buf + n, "Content-Length: %ld" NL, content_size);
  }
  if (etag.size) {
    n += sprintf(write_buf + n, "Etag: " STR_Fmt NL, STR_Arg(etag));
  }
  n += sprintf(write_buf + n, NL);
  return send_all(req->fd, write_buf, n);
}

int send_static(Request *req, Status status, Str content_type, Str content) {
  TRY(send_header(req, status, content_type, content.size, STR_NULL));
  return send_all(req->fd, content.data, content.size);
}

int send_file(Request *req, const char *filename, Status status,
              Str content_type, Str etag) {
  int src_fd, result;
  struct stat s;
  char tagbuf[64];
  TRY(stat(filename, &s));

  int etag_size = snprintf(tagbuf, sizeof(tagbuf), "\"%ld-%ld.%ld\"", s.st_size,
                           s.st_mtim.tv_sec, s.st_mtim.tv_nsec);
  Str local_etag = str_from_parts(tagbuf, etag_size);
  printf("ETAG " STR_Fmt " == " STR_Fmt "\n", STR_Arg(etag),
         STR_Arg(local_etag));
  if (str_eq(etag, local_etag)) {
    return send_header(req, S304, STR_NULL, 0, etag);
  }
  TRY(src_fd = open(filename, 0, O_RDONLY));
  TRY_CATCH(send_header(req, status, content_type, s.st_size, local_etag),
            close(src_fd));
  TRY_CATCH(result = sendfile(req->fd, src_fd, NULL, s.st_size) == -1 ? -1 : 0,
            close(src_fd));
  close(src_fd);
  return result;
}

// APP =======================================================================

int about_page(Request *request) {
  Header headers[100];
  int n = Request_headers(request, headers, 100);
  Str etag = Headers_get_header(headers, n, SL("If-None-Match"));
  return send_file(request, "about.html", S200, MIME_HTML, etag);
}

int home_page(Request *request) {
  Str content = STR("<!DOCTYPE html>"
                    "<html>"
                    "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                    "<body><h1>Hello, world!</h1></body>"
                    "</html>");
  return send_static(request, S200, MIME_HTML, content);
}

int not_found(Request *request) {
  Str content = STR("<html>"
                    "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                    "<body><h1>404 Not found</h1></body>"
                    "</html>");
  return send_static(request, S404, MIME_HTML, content);
}

int debug_page(Request *request) {
  Str content = STR("<html>"
                    "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                    "<body><h1>Hello, world!</h1></body>"
                    "</html>");
  Str header = Request_header(request);
  Str body = Request_body(request);
  printf("HEADER='" STR_Fmt "'\nBODY='" STR_Fmt "'\n", STR_Arg(header),
         STR_Arg(body));

  Header headers[100];
  int n = Request_headers(request, headers, 100);
  for (int i = 0; i < n; ++i) {
    printf("H '" STR_Fmt "' = '" STR_Fmt "'\n", STR_Arg(headers[i].name),
           STR_Arg(headers[i].value));
  }
  return send_static(request, S200, MIME_HTML, content);
}

int info_page(Request *request) {
  const int N = 8 * 1024;
  char buf[N];

  Header headers[100];
  int n = Request_headers(request, headers, 100);
  int size =
      snprintf(buf, N, "<p>" STR_Fmt "</p>", STR_Arg(request->status_line));
  for (int i = 0; i < n; ++i) {
    size += snprintf(buf + size, N - size, STR_Fmt ": " STR_Fmt "<br/>",
                     STR_Arg(headers[i].name), STR_Arg(headers[i].value));
  }
  Str response = str_from_parts(buf, size);
  return send_static(request, S200, MIME_HTML, response);
}

int main(int argc, char **argv) {
  elog("Starting httpd...\n");

  Server serv;
  serv.router = (Router){
      .routes =
          (Route[]){
              {.handler = home_page, .path = STR("/")},
              {.handler = about_page, .path = STR("/about")},
              {.handler = debug_page, .path = STR("/debug")},
              {.handler = info_page, .path = STR("/info")},
          },
      .size = 4,
      .NotFound = {.handler = not_found, .path = STR_NULL},
  };

  Connection conn;
  // Address addr = Address_inet(STR("127.0.0.1"), 3210);
  //  Address addr = Address_unix(STR("/tmp/httpd.sock"));
  Address addr = Address_url(STR("tcp://127.0.0.1:3210"));
  TRY_CATCH(Connection_connect(&conn, addr), perror("Connect"));
  TRY_CATCH(Server_init(&serv, conn), perror("Server init"));

  elog("Ready to receive requests\n");
  Server_loop(&serv);
  Server_close(&serv);
  elog("Finished httpd\n");

  return 0;
}
