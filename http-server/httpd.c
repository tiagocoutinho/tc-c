#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#define LISTEN_BACKLOG 50
#define MAX_EVENTS 10

typedef struct sockaddr Addr;
typedef struct sockaddr_in InetAddr;

typedef struct epoll_event Event;

struct _Server {
    int sock_fd;
};
typedef struct _Server Server;

struct _Server create_tcp_server(const char* host, int port) {
    const int serv_fd = socket(AF_INET, SOCK_STREAM, 0);
    Server result = {.sock_fd = serv_fd};
    
    if (serv_fd == -1) {
        perror("httpd socket create error");
        return result;
    }

    const int reuse = 1;
    if (setsockopt(serv_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) == -1) {
        close(serv_fd);
        perror("httpd socket option error");
        return result;
    }
    
    struct in_addr sin_addr;
    if (inet_pton(AF_INET, "127.0.0.1", &sin_addr) != 1) {
        close(serv_fd);
        perror("httpd socket option error");
        return result;
    }

    InetAddr addr = {.sin_family = AF_INET, .sin_port = htons(3210), .sin_addr = sin_addr};

    if (bind(serv_fd, (Addr*)&addr, sizeof(addr)) == -1) {
        close(serv_fd);
        perror("http socket bind error");
        return result;
    }
    
    if (listen(serv_fd, LISTEN_BACKLOG) == -1) {
        close(serv_fd);
        perror("httpd socket listen error");
        return result;
    }

    return result;
}

int main(int argc, char **argv) {
    printf("Starting httpd...\n");

    const int serv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_fd == -1) {
        perror("httpd socket create error");
        return 1;
    }

    const int reuse = 1;
    if (setsockopt(serv_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) == -1) {
        close(serv_fd);
        perror("httpd socket option error");
        return 2;
    }
    
    struct in_addr sin_addr;
    if (inet_pton(AF_INET, "127.0.0.1", &sin_addr) != 1) {
        close(serv_fd);
        perror("httpd socket option error");
        return 2;
    }

    InetAddr addr = {.sin_family = AF_INET, .sin_port = htons(3210), .sin_addr = sin_addr};
    if (bind(serv_fd, (Addr*)&addr, sizeof(addr)) == -1) {
        close(serv_fd);
        perror("http socket bind error");
        return 3;
    }
    
    if (listen(serv_fd, LISTEN_BACKLOG) == -1) {
        close(serv_fd);
        perror("httpd socket listen error");
        return 4;
    }

    const int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        close(serv_fd);
        perror("httpd epoll create error");
        return 5;
    }

    struct epoll_event ev = { .events = EPOLLIN, .data = {.fd = serv_fd } };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, serv_fd, &ev) == -1) {
        close(epoll_fd);
        close(serv_fd);
        perror("httpd epoll ctl error");
        return 6;
    }
    
    int running = 1;
    Event events[MAX_EVENTS];
    printf("Ready to receive requests\n");
    while (running) {
        const int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        printf("Event(s)!\n");
        if (n == -1) {
            perror("httpd epoll wait error");
            close(epoll_fd);
            close(serv_fd);
            return 7;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == serv_fd) {
                InetAddr client_addr;
                socklen_t client_addr_len;
                int client_fd = accept(serv_fd, (Addr*)&client_addr, &client_addr_len);
                if (client_fd == -1) {
                    perror("httpd socket accept error");
                    close(epoll_fd);
                    close(serv_fd);
                    return 8;
                }
                printf("Received connection from %d\n", client_addr.sin_port);
            }
        }
    }

    close(epoll_fd);
    close(serv_fd);
    printf("Finished httpd\n");
    return 0;
}
