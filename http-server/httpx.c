#define HTTPD_ELOG
#define HTTPD_IMPLEMENTATION

#include "httpd.h"

int home_page(HttpdRequest *request) {
  HttpdStr content =
      STR("<!DOCTYPE html>"
          "<html>"
          "<head><link rel=\"icon\" href=\"data:,\" />"
          "<script "
          "src=\"https://cdn.jsdelivr.net/npm/htmx.org@4.0.0-beta4\"></script>"
          "</head>"
          "<body>"
          "<div hx-get=\"/contact\" hx-swap=\"outerHTML\" "
          "hx-trigger=\"load\"></div>"
          "</body>"
          "</html>");
  return httpd_request_send_static(request, S200, MIME_HTML, content);
}

int contact(HttpdRequest *request) {
  HttpdStr content =
      STR("<!DOCTYPE html>"
          "<div hx-target:inherited=\"this\" hx-swap:inherited=\"outerHTML\">"
          "<div><label>First Name</label>: Joe</div>"
          "<div><label>Last Name</label>: Blow</div>"
          "<div><label>Email</label>: joe@blow.com</div>"
          "<button hx-get=\"/edit\">Click To Edit</button>"
          "</div>");
  return httpd_request_send_static(request, S200, MIME_HTML, content);
}

int edit_form(HttpdRequest *request) {
  HttpdStr content =
      STR("<form hx-put=\"/contact\" hx-target:inherited=\"this\" "
          "hx-swap:inherited=\"outerHTML\">"
          "<div>"
          "<label>First Name</label>"
          "<input type=\"text\" name=\"firstName\" value=\"Joe\">"
          "</div>"
          "<div>"
          "<label>Last Name</label>"
          "<input type=\"text\" name=\"lastName\" value=\"Blow\">"
          "</div>"
          "<div>"
          "<label>Email Address</label>"
          "<input type=\"email\" name=\"email\" value=\"joe@blow.com\">"
          "</div>"
          "<button type=\"submit\">Submit</button>"
          "<button hx-get=\"/contact\">Cancel</button>"
          "</form>");
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
              {.handler = home_page, .method = METHOD_GET, .path = STR("/")},
              {.handler = contact,
               .method = METHOD_GET,
               .path = STR("/contact")},
              {.handler = edit_form,
               .method = METHOD_GET,
               .path = STR("/edit")},
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
