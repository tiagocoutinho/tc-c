//#define HTTPD_ELOG
#define HTTPD_IMPLEMENTATION

#include "httpd.h"

int home_page(HttpdRequest *request) {
  HttpdStr content = STR("<!DOCTYPE html>"
                         "<html>"
                         "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                         "<body><h1>Hello, world!</h1></body>"
                         "</html>");
  return httpd_request_send_static(request, S200, MIME_HTML, content);
}

int not_found(HttpdRequest *request) {
  HttpdStr content = STR("<html>"
                         "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                         "<body><h1>404 Not found</h1></body>"
                         "</html>");
  return httpd_request_send_static(request, S404, MIME_HTML, content);
}

int run(HttpdAddress addr) {
  httpd_elog("Starting httpd...\n");

  HttpdServer serv;
  serv.router = (HttpdRouter){
      .routes =
          (HttpdRoute[]){
              {.handler = home_page, .path = STR("/")},
          },
      .size = 1,
      .NotFound = {.handler = not_found, .path = STR_NULL},
  };

  HttpdConnection conn;
  TRY_CATCH(httpd_connection_connect(&conn, addr), perror("Connect"));
  TRY_CATCH(httpd_server_init(&serv, conn), perror("HttpdServer init"));

  httpd_elog("Ready to receive requests\n");
  httpd_server_run(&serv);
  httpd_server_close(&serv);
  httpd_elog("Finished httpd\n");
  return 0;
}

void* run_th(void *data) {
  run(*(HttpdAddress*)data);
  return nullptr;
}

const int N = 8;

int main(int argc, char **argv) {
  HttpdAddress addr = httpd_address_url(STR("tcp://127.0.0.1:3210"));

  pthread_t threads[N];
  for(int i =0; i < N; ++i) {
    pthread_create(&threads[i], NULL, run_th, &addr);
  }
  return run(addr);
}
