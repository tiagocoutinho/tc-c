//#define HTTPD_ELOG
#define HTTPD_IMPLEMENTATION

#include "httpd.h"

typedef struct {
  char first_name[32];
  char last_name[32];
  char email[64];
} User;

User user = {
    .first_name = "John", .last_name = "Doe", .email = "john.doe@example.com"};

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

int view_contact(HttpdRequest *request) {
  char buf[1024];
  int n = snprintf(
      buf, 1024,
      "<!DOCTYPE html>"
      "<div hx-target:inherited=\"this\" hx-swap:inherited=\"outerHTML\">"
      "<div><label>First Name</label>: %s</div>"
      "<div><label>Last Name</label>: %s</div>"
      "<div><label>Email</label>: %s</div>"
      "<button hx-get=\"/edit\">Click To Edit</button>"
      "</div>",
      user.first_name, user.last_name, user.email);
  HttpdStr content = httpd_str_from_parts(buf, n);
  return httpd_request_send_static(request, S200, MIME_HTML, content);
}

int update_contact(HttpdRequest *request) {

  HttpdIterator it = httpd_request_form_iterator(request);

  while (it.data.size) {
    HttpdKeyValue param = httpd_form_iterator_next(&it);
    if (httpd_str_eq(param.key, STR("firstName"))) {
      memset(user.first_name, 0, sizeof(user.first_name));
      memcpy(user.first_name, param.value.data, param.value.size);
    } else if (httpd_str_eq(param.key, STR("lastName"))) {
      memset(user.last_name, 0, sizeof(user.first_name));
      memcpy(user.last_name, param.value.data, param.value.size);
    } else if (httpd_str_eq(param.key, STR("email"))) {
      memset(user.email, 0, sizeof(user.first_name));
      memcpy(user.email, param.value.data, param.value.size);
    }
  }
  return view_contact(request);
}

int edit_form(HttpdRequest *request) {
  char buf[1024];
  int n = snprintf(buf, 1024,
                   "<form hx-put=\"/contact\" hx-target:inherited=\"this\" "
                   "hx-swap:inherited=\"outerHTML\">"
                   "<div>"
                   "<label>First Name</label>"
                   "<input type=\"text\" name=\"firstName\" value=\"%s\">"
                   "</div>"
                   "<div>"
                   "<label>Last Name</label>"
                   "<input type=\"text\" name=\"lastName\" value=\"%s\">"
                   "</div>"
                   "<div>"
                   "<label>Email Address</label>"
                   "<input type=\"email\" name=\"email\" value=\"%s\">"
                   "</div>"
                   "<button type=\"submit\">Submit</button>"
                   "<button hx-get=\"/contact\">Cancel</button>"
                   "</form>",
                   user.first_name, user.last_name, user.email);
  HttpdStr content = httpd_str_from_parts(buf, n);
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
              {.handler = view_contact,
               .method = METHOD_GET,
               .path = STR("/contact")},
              {.handler = update_contact,
               .method = METHOD_PUT,
               .path = STR("/contact")},
              {.handler = edit_form,
               .method = METHOD_GET,
               .path = STR("/edit")},
          },
      .size = 4,
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
