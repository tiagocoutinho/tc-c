
#define HTTPD_IMPLEMENTATION
#include "httpd.h"

HttpdStr home_page = STR("<html><body><h1>Hello, world!</h1></body></html>");
HttpdStr not_found = STR("<html><body><h3>404 Not Found</h3></body></html>");

int main(int argc, char **argv) {
  HttpdRoute routes[] = {
      httpd_route_static_html(STR("/"), home_page),
  };
  HttpdRouter router = {
      .routes = routes,
      .size = sizeof(routes)/sizeof(HttpdRoute),
      .NotFound = httpd_route_static_html(STR_NULL, not_found),
  };
  HttpdServer serv;
  TRY_CATCH(httpd_server_init(&serv, router), perror("HttpdServer init"));
  
  HttpdAddress addr = httpd_address_parse(STR("tcp://127.0.0.1:3210"));
  TRY_CATCH(httpd_server_bind(&serv, addr), perror("HttpdServer bind"));
  
  httpd_server_run(&serv);
  httpd_server_close(&serv);
  return 0;
}
