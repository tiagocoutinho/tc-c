
#define HTTPD_IMPLEMENTATION
#include "httpd.h"

#include "hiredis/hiredis.h"

HttpdStr home_page = STR("<html><body><h1>Hello, world!</h1></body></html>");
HttpdStr not_found = STR("<html><body><h3>404 Not Found</h3></body></html>");

redisContext *get_redis_context(HttpdRequest *req) {
  return (redisContext *)req->app;
}

int redis_get(HttpdRequest *req) {
  redisContext *ctx = get_redis_context(req);
  redisCommand(ctx, "SET foo 19");
  redisReply *reply = redisCommand(ctx, "GET foo");
  if (reply->type == REDIS_REPLY_NIL) {
      httpd_request_send_static(req, S200, MIME_PLAIN, STR("null"));
  } else if (reply->type == REDIS_REPLY_STRING) {

    httpd_request_send_headers(req, S200, MIME_JSON, reply->len + 2, STR_NULL);
    send(req->fd, "\"", 1, 0);
    httpd_send_all(req->fd, reply->str, reply->len);
    send(req->fd, "\"", 1, 0);
  }
  freeReplyObject(reply);
  return 0;
}

int main(int argc, char **argv) {
  redisContext *redis = redisConnect("127.0.0.1", 6379);

  HttpdRoute routes[] = {
      httpd_route_static_html(STR("/"), home_page),
      {.path = STR("/get"), .method = METHOD_GET, .handler = redis_get},
  };
  HttpdRouter router = {
      .routes = routes,
      .size = sizeof(routes) / sizeof(HttpdRoute),
      .NotFound = httpd_route_static_html(STR_NULL, not_found),
  };
  HttpdServer serv;
  TRY_CATCH(httpd_server_init(&serv, router, redis), perror("HttpdServer init"));

  HttpdAddress addr = httpd_address_parse(STR("tcp://127.0.0.1:3210"));
  TRY_CATCH(httpd_server_bind(&serv, addr), perror("HttpdServer bind"));

  httpd_server_run(&serv);
  httpd_server_close(&serv);
  redisFree(redis);
  return 0;
}
