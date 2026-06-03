#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_BACKLOG 50
#define MAX_EVENTS 10

typedef struct sockaddr Addr;
typedef struct sockaddr_in InetAddr;

typedef struct epoll_event Event;

typedef struct {
  int sock_fd;
  int epoll_fd;
  Event events[MAX_EVENTS];
  int nb_clients;
} Server;

void Server_init(Server *server) {
  server->sock_fd = 0;
  server->epoll_fd = 0;
  server->nb_clients = 0;
}

int Server_create_tcp(Server *server, const char *host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    return -1;
  }
  server->sock_fd = fd;
  const int reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) == -1) {
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
  int client_fd = accept(s->sock_fd, (Addr *)&client_addr, &client_addr_len);
  char name[128];
  inet_ntop(AF_INET, &client_addr.sin_addr, name, 128);
  printf("Received connection from %s %d\n", name, client_addr.sin_port);
  return client_fd;
}

int Server_wait(Server *s) {
  return epoll_wait(s->epoll_fd, s->events, MAX_EVENTS, -1);
}

int Server_add_client(Server *s, int fd) {
  Event event = {.events = EPOLLIN, .data = {.fd = fd}};
  if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
    return -1;
  }
  return 0;
}

int Server_remove_client(Server *s, int fd) {
  if (epoll_ctl(s->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
    return -1;
  }
  return 0;
}

int Server_handle_request(Server *s, int fd) {
  char buf[4096] = {0};
  int n = recv(fd, buf, 4096, 0);
  if (n == 0) {
    Server_remove_client(s, fd);
    close(fd);
    return -1;
  } else {
    printf("received '%s'\n", buf);
  }
  return 0;
}

int Server_loop(Server *s) {
  while (1) {
    int n = Server_wait(s);
    if (n == -1) {
      return -1;
    }
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
        Server_handle_request(s, fd);
      }
    }
  }
}

int main(int argc, char **argv) {
  printf("Starting httpd...\n");

  Server serv;
  Server_init(&serv);

  if (Server_create_tcp(&serv, "127.0.0.1", 3210) == -1) {
    perror("create tcp error");
    return 1;
  }

  int running = 1;
  printf("Ready to receive requests\n");
  Server_loop(&serv);
  Server_stop(&serv);
  printf("Finished httpd\n");

  return 0;
}
