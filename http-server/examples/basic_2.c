#define HTTPD_IMPLEMENTATION
#include "httpd.h"

const char home_page[] = "<!DOCTYPE html>"
                         "<html>"
                         "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                         "<body><h1>Hello, world!</h1></body>"
                         "</html>";

int not_found(HttpdRequest *request) {
  const char content[] = "<html>"
                         "<head><link rel=\"icon\" href=\"data:,\" /></head>"
                         "<body><h1>404 Not found</h1></body>"
                         "</html>";
  return httpd_request_send_static(request, S404, MIME_HTML, STR(content));
}

int info_page(HttpdRequest *request) {
  const int N = 8 * 1024;
  char buf[N];
  HttpdStrIO out = httpd_str_io(buf, N);

  httpd_strio_printf(&out,
                     "<h3>Status line</h3>"
                     "<code>" STR_Fmt "</code>"
                     "<h3>Path</h3>"
                     "<pre><code>"
                     "Full path: " STR_Fmt "\n"
                     "  Path: " STR_Fmt "\n"
                     "  Query: " STR_Fmt "\n",
                     STR_Arg(request->status_line), STR_Arg(request->full_path),
                     STR_Arg(request->path), STR_Arg(request->query));

  HttpdIterator qit = httpd_request_query_iterator(request);

  while (qit.data.size) {
    HttpdKeyValue param = httpd_query_iterator_next(&qit);
    httpd_strio_printf(&out, "    " STR_Fmt " = " STR_Fmt "\n",
                       STR_Arg(param.key), STR_Arg(param.value));
  }
  httpd_strio_printf(&out, "</code></pre><h3>Headers</h3><pre><code>");

  HttpdHeaderIterator hit = httpd_request_header_iterator(request);
  while (hit.data.size) {
    HttpdHeader header = httpd_header_iterator_next(&hit);
    httpd_strio_printf(&out, STR_Fmt ": " STR_Fmt "\n", STR_Arg(header.key),
                       STR_Arg(header.value));
  }

  HttpdStr response = httpd_strio_str(out);
  return httpd_request_send_static(request, S200, MIME_HTML, response);
}

int main(int argc, char **argv) {
  HttpdAddress addr = httpd_address_parse(STR("tcp://0.0.0.0:3210"));
  httpd_elog("Starting httpd...\n");

  HttpdRouter router = {
      .routes =
          (HttpdRoute[]){
              httpd_route_static_html(STR("/"), STR(home_page)),
              {.handler = info_page, .path = STR("/info")},
              httpd_route_static_file(STR("/about"), STR("about.html")),
          },
      .size = 3,
      .NotFound = {.handler = not_found, .path = STR_NULL},
  };

  HttpdServer serv;
  TRY_CATCH(httpd_server_init(&serv, router, NULL), perror("HttpdServer init"));
  TRY_CATCH(httpd_server_bind(&serv, addr), perror("HttpdServer bind"));
  httpd_server_run(&serv);
  httpd_server_close(&serv);
  httpd_elog("Finished httpd\n");
  return 0;
}
