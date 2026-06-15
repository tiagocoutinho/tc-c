// #define HTTPD_ELOG
#define HTTPD_IMPLEMENTATION

#include "httpd.h"

int about_page(HttpdRequest *request) {
  return httpd_request_send_file(request, STR("about.html"));
}

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

int info_page(HttpdRequest *request) {
  const int N = 8 * 1024;
  char buf[N];

  HttpdHeader headers[100];
  int n = httpd_request_headers(request, headers, 100);
  int size =
      snprintf(buf, N, "<p>" STR_Fmt "</p>", STR_Arg(request->status_line));
  for (int i = 0; i < n; ++i) {
    size += snprintf(buf + size, N - size, STR_Fmt ": " STR_Fmt "<br/>",
                     STR_Arg(headers[i].name), STR_Arg(headers[i].value));
  }
  HttpdStr response = httpd_str_from_parts(buf, size);
  return httpd_request_send_static(request, S200, MIME_HTML, response);
}

int run(HttpdAddress addr) {
  httpd_elog("Starting httpd...\n");

  HttpdServer serv;
  serv.router = (HttpdRouter){
      .routes =
          (HttpdRoute[]){
              {.handler = home_page, .method = METHOD_GET, .path = STR("/")},
              {.handler = about_page,
               .method = METHOD_ANY,
               .path = STR("/about")},
              {.handler = info_page,
               .method = METHOD_GET,
               .path = STR("/info")},
          },
      .size = 3,
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

int main(int argc, char **argv) {
  HttpdAddress addr = httpd_address_url(STR("tcp://127.0.0.1:3210"));

  return run(addr);
}
