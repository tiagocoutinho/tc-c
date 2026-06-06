#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <bits/time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LISTEN_BACKLOG 50
#define MAX_EVENTS 10

typedef struct sockaddr Addr;
typedef struct sockaddr_in InetAddr;

typedef struct epoll_event Event;

void elog(const char *format, ...) {
  char time_buf[32];
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm *tm_info = localtime(&ts.tv_sec);
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
  fprintf(stderr, "[%s.%06ld] ", time_buf, ts.tv_nsec / 1000);
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
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

Str str_chop_left(Str str, size_t n) {
  if (n > str.size) {
    n = str.size;
  }
  Str result = str;
  result.size -= n;
  result.data += n;
  return result;
}

bool str_eq(Str a, Str b) {
  return (a.size != b.size) ? false : memcmp(a.data, b.data, a.size) == 0;
}

// HTTP ----------------------------------------------------------------------

typedef enum {
  s200 = 200,
  s404 = 404,
} StatusCode;

typedef struct {
  StatusCode status_code;
  Str message;
} Status;

const Status S200 = {s200, SL("OK")};
const Status S404 = {s404, SL("Not found")};
const Str mime_html = SL("text/html");

// Request -------------------------------------------------------------------
//
typedef struct {
  Str status_line;
  Str method;
  Str path;
  Str protocol;
  int fd;
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
  int sock_fd;
  int epoll_fd;
  Event events[MAX_EVENTS];
  int nb_clients;

  Router router;
} Server;

void Server_init(Server *server) {
  server->sock_fd = 0;
  server->epoll_fd = 0;
  server->nb_clients = 0;
}

int isetsockopt(int fd, int level, int option, int value) {
  return setsockopt(fd, level, option, &value, sizeof(value));
}

int Server_prepare_tcp_socket(int fd) {
  if (isetsockopt(fd, IPPROTO_TCP, TCP_NODELAY, 1) == -1) {
    return -1;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int Server_create_tcp(Server *server, const char *host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    return -1;
  }

  server->sock_fd = fd;

  if (isetsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 1) == -1) {
    close(fd);
    return -1;
  }

  if (Server_prepare_tcp_socket(fd) == -1) {
    close(fd);
    return -1;
  }

  struct in_addr sin_addr;
  if (inet_pton(AF_INET, host, &sin_addr) != 1) {
    close(fd);
    return -1;
  }

  InetAddr addr = {
      .sin_family = AF_INET, .sin_port = htons(port), .sin_addr = sin_addr};

  if (bind(fd, (Addr *)&addr, sizeof(addr)) == -1) {
    close(fd);
    return -1;
  }

  if (listen(fd, LISTEN_BACKLOG) == -1) {
    close(fd);
    return -1;
  }

  const int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    close(fd);
    return -1;
  }

  server->epoll_fd = epoll_fd;
  Event event = {.events = EPOLLIN, .data = {.fd = fd}};
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
    close(epoll_fd);
    close(fd);

    return -1;
  }

  return 0;
}

void Server_stop(Server *s) {
  if (s->epoll_fd > 0) {
    close(s->epoll_fd);
    s->epoll_fd = 0;
  }
  if (s->sock_fd > 0) {
    close(s->sock_fd);
    s->sock_fd = 0;
  }
}

int Server_accept(Server *s) {
  InetAddr client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  int fd = accept(s->sock_fd, (Addr *)&client_addr, &client_addr_len);
  if (Server_prepare_tcp_socket(fd) == -1) {
    return -1;
  }
  char buf[128];
  Str name =
      str_from_c0(inet_ntop(AF_INET, &client_addr.sin_addr, buf, sizeof(buf)));
  elog("Received connection from " STR_Fmt " %d\n", STR_Arg(name),
       client_addr.sin_port);
  return fd;
}

int Server_wait(Server *s) {
  return epoll_wait(s->epoll_fd, s->events, MAX_EVENTS, -1);
}

int Server_add_client(Server *s, int fd) {
  elog("adding new client (%d)\n", s->nb_clients + 1);
  Event event = {.events = EPOLLIN, .data = {.fd = fd}};
  if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
    return -1;
  }
  s->nb_clients++;
  return 0;
}

int Server_remove_client(Server *s, int fd) {
  elog("removing client (%d)\n", s->nb_clients - 1);
  if (epoll_ctl(s->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
    return -1;
  }
  s->nb_clients--;
  return 0;
}

Request Request_parse(const Str payload) {
  // GET /path HTTP/1.1
  Request req = {.status_line = str_subc(payload, '\r')};
  req.method = str_subc(req.status_line, ' ');
  Str status_line = str_chop_left(req.status_line, req.method.size + 1);
  req.path = str_subc(status_line, ' ');
  req.protocol = str_chop_left(status_line, req.path.size + 1);
  return req;
}

void Request_log(const Request req) {
  elog(STR_Fmt " " STR_Fmt "|" STR_Fmt "\n", STR_Arg(req.method),
       STR_Arg(req.path), STR_Arg(req.protocol));
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
  char read_buf[4096];
  int n = recv(fd, read_buf, 4096, 0);
  if (n == -1) {
    return -1;
  } else if (n == 0) {
    return -2;
  }
  const Str payload = str_from_parts(read_buf, n);
  Request req = Request_parse(payload);
  req.fd = fd;

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  elog("[START] " STR_Fmt "\n", STR_Arg(req.status_line));
  RequestHandler func = Router_find(&s->router, req.path);
  int result = func(&req);
  clock_gettime(CLOCK_MONOTONIC, &end);
  char buf[32];
  Str dt = time_delta(&start, &end, buf, sizeof(buf));
  elog("[ END ] " STR_Fmt " " STR_Fmt "\n", STR_Arg(req.status_line),
       STR_Arg(dt));
  return result;
}

int Server_handle(Server *s, int n) {
  for (int i = 0; i < n; ++i) {
    int fd = s->events[i].data.fd;
    if (fd == s->sock_fd) {
      int client_fd = Server_accept(s);
      if (client_fd == -1) {
        return -1;
      }
      if (Server_add_client(s, client_fd) == -1) {
        return -1;
      }
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
  return 0;
}

int Server_loop(Server *s) {
  while (1) {
    int n = Server_wait(s);
    if (n == -1) {
      return -1;
    }
    if (Server_handle(s, n) == -1) {
      return -1;
    }
  }
}

int send_all(int fd, const char *payload, int n) {
  while (n > 0) {
    int i = send(fd, payload, n, 0);
    if (i == -1) {
      return -1;
    }
    payload += i;
    n -= i;
  }
  return 0;
}

int send_header(int fd, Status status, Str content_type, size_t content_size) {

  char write_buf[256];
  int n = sprintf(write_buf,
                  "HTTP/1.1 %d " STR_Fmt NL "Content-Type: " STR_Fmt NL
                  "Content-Length: %ld" NL NL,
                  status.status_code, STR_Arg(status.message),
                  STR_Arg(content_type), content_size);
  return send_all(fd, write_buf, n);
}

int send_static(int fd, Status status, Str content_type, Str content) {
  if (send_header(fd, status, content_type, content.size) == -1) {
    return -1;
  }
  return send_all(fd, content.data, content.size);
}

int send_file(int dst, const char *filename, Status status, Str content_type) {
  int src_fd = open(filename, 0, O_RDONLY);
  if (src_fd == -1) {
    return -1;
  }
  struct stat s;
  if (fstat(src_fd, &s) == -1) {
    close(src_fd);
    return -1;
  }
  if (send_header(dst, status, content_type, s.st_size) == -1) {
    close(src_fd);
    return -1;
  }
  int result = sendfile(dst, src_fd, NULL, s.st_size) == -1 ? -1 : 0;
  close(src_fd);
  return result;
}

int about_page(Request *request) {
  return send_file(request->fd, "about.html", S200, mime_html);
}

int home_page(Request *request) {
  Str content = STR("<html>"
                    "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                    "<body><h1>Hello, world!</h1></body>"
                    "</html>");

  return send_static(request->fd, S200, mime_html, content);
}

int not_found(Request *request) {
  Str content = STR("<html>"
                    "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                    "<body><h1>404 Not found</h1></body>"
                    "</html>");
  return send_static(request->fd, S404, mime_html, content);
}

int main(int argc, char **argv) {
  elog("Starting httpd...\n");

  Server serv;
  Server_init(&serv);

  serv.router = (Router){
      .routes =
          (Route[]){
              {.handler = home_page, .path = STR("/")},
              {.handler = about_page, .path = STR("/about")},
          },
      .size = 2,
      .NotFound = {.handler = not_found, .path = STR_NULL},
  };

  if (Server_create_tcp(&serv, "127.0.0.1", 3210) == -1) {
    perror("create tcp error");
    return 1;
  }

  elog("Ready to receive requests\n");
  Server_loop(&serv);
  Server_stop(&serv);
  elog("Finished httpd\n");

  return 0;
}
