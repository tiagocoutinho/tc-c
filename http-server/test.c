#include <inttypes.h>
#include <stdio.h>

#define HTTPD_IMPLEMENTATION
#include "httpd.h"

#define RESULT(r, e)                                                           \
  ((Result){.ok = r,                                                           \
            .filename = __FILE__,                                              \
            .function = __FUNCTION__,                                          \
            .line = __LINE__,                                                  \
            .expression = e})
#define OK RESULT(true, "")
#define ASSERT(expr)                                                           \
  if (!(expr)) {                                                               \
    return RESULT(false, #expr);                                               \
  }
#define RUN_TEST(expr)                                                         \
  {                                                                            \
    fprintf(stderr, "%-74s", "Running " #expr "...");                          \
    fflush(stdout);                                                            \
    Result r = expr();                                                         \
    fprint_result(stderr, r);                                                  \
  }

typedef struct {
  bool ok;
  struct {
    const char *filename;
    const char *function;
    const char *expression;
    const size_t line;
  };
} Result;

void fprint_result(FILE *f, Result r) {
  if (r.ok) {
    fprintf(f, "[OK]\n");
  } else {
    fprintf(f, "[FAILED]\n  %s:%zu: %s failed at '%s'\n", r.filename, r.line,
            r.function, r.expression);
  }
}

Result test_httpd_str_construct() {
  ASSERT(httpd_str_eq(STR("Foo"), STR("Foo")));
  ASSERT(!httpd_str_eq(httpd_str_from_parts("FOO", 3), STR("BAR")));
  return OK;
}

Result test_httpd_str_index() {
  size_t index;
  ASSERT(httpd_str_index(STR("Hello, world!"), 'w', &index));
  ASSERT(index == 7);
  ASSERT(!httpd_str_index(STR("Hello, world!"), '@', &index));

  ASSERT(httpd_str_rindex(STR("Hello, world!"), 'l', &index));
  ASSERT(index == 10);
  ASSERT(!httpd_str_rindex(STR("Hello, world!"), '@', &index));

  return OK;
}

Result test_httpd_str_indexstr() {
  size_t index;
  ASSERT(httpd_str_indexstr(STR("Hello, world!"), STR("world"), &index));
  ASSERT(index == 7);
  ASSERT(!httpd_str_indexstr(STR("Hello, world!"), STR("foo"), &index));
  return OK;
}

Result test_httpd_str_subc() {
  ASSERT(
      httpd_str_eq(httpd_str_subc(STR("Hello, world!"), ' '), STR("Hello,")));
  ASSERT(httpd_str_eq(httpd_str_subc(STR("Hello, world!"), '&'),
                      STR("Hello, world!")));
  return OK;
}

Result test_httpd_str_substr() {
  ASSERT(httpd_str_eq(httpd_str_substr(STR("Hello, world!"), STR("wor")),
                      STR("Hello,")));
  ASSERT(httpd_str_eq(httpd_str_substr(STR("Hello, world!"), STR("foo")),
                      STR("Hello, world!")));
  return OK;
}

Result test_httpd_str_strip() {
  ASSERT(httpd_str_eq(httpd_str_strip(STR("  Foo   ")), STR("Foo")));
  ASSERT(httpd_str_eq(httpd_str_strip_left(STR("  Foo   ")), STR("Foo   ")));
  ASSERT(httpd_str_eq(httpd_str_strip_right(STR("  Foo   ")), STR("  Foo")));

  ASSERT(httpd_str_eq(httpd_str_strip(STR("    ")), STR("")));
  ASSERT(httpd_str_eq(httpd_str_strip_left(STR("    ")), STR("")));
  ASSERT(httpd_str_eq(httpd_str_strip_right(STR("   ")), STR("")));
  return OK;
}

Result test_httpd_str_trim() {
  ASSERT(httpd_str_eq(httpd_str_ltrim(STR("Hello, world!"), 7), STR("world!")));
  ASSERT(httpd_str_eq(httpd_str_ltrim(STR("Hello, world!"), 23), STR("")));
  return OK;
}

Result test_httpd_str_startswith() {
  ASSERT(httpd_str_startswith(STR("Hello, world!"), STR("Hello")));
  ASSERT(httpd_str_startswith(STR("Hello, world!"), STR("Hello, world!")));
  ASSERT(!httpd_str_startswith(STR("Hello, world!"), STR("foo")));
  return OK;
}

Result test_httpd_str_endswith() {
  ASSERT(httpd_str_endswith(STR("Hello, world!"), STR(", world!")));
  ASSERT(httpd_str_endswith(STR("Hello, world!"), STR("Hello, world!")));
  ASSERT(!httpd_str_endswith(STR("Hello, world!"), STR("foo")));
  return OK;
}

Result test_httpd_address_parse() {
    HttpdAddress addr;
    addr = httpd_address_parse(STR("tcp://bla.com:12345"));
    ASSERT(addr.domain == AF_INET);
    ASSERT(addr.inet.sin_port == ntohs(12345));

    addr = httpd_address_parse(STR("tcp6://:12345"));
    ASSERT(addr.domain == AF_INET6);
    ASSERT(addr.inet6.sin6_port == ntohs(12345));
    
    addr = httpd_address_parse(STR("unix:///tmp/httpd.sock"));
    ASSERT(addr.domain == AF_UNIX);
    ASSERT(strcmp(addr.unx.sun_path, "/tmp/httpd.sock") == 0);
    
    addr = httpd_address_parse(STR("ups://bla.com"));
    ASSERT(addr.domain == AF_UNIX);
    ASSERT(strcmp(addr.unx.sun_path, "/tmp/httpd.sock") == 0);
  return OK;
}

int main(void) {
  RUN_TEST(test_httpd_str_construct)
  RUN_TEST(test_httpd_str_strip)
  RUN_TEST(test_httpd_str_index)
  RUN_TEST(test_httpd_str_indexstr)
  RUN_TEST(test_httpd_str_subc)
  RUN_TEST(test_httpd_str_trim)
  RUN_TEST(test_httpd_str_startswith)
  RUN_TEST(test_httpd_str_endswith)

  RUN_TEST(test_httpd_address_parse)
  return 0;
}
