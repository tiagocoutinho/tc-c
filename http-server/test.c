#include <inttypes.h>

#define HTTPD_IMPLEMENTATION
#include <stdio.h>
#include <string.h>

#include "httpd.h"

#define RESULT(r, e)                                                           \
  ((Result){.ok = r,                                                           \
            .filename = __FILE__,                                              \
            .function = __FUNCTION__,                                          \
            .line = __LINE__,                                                  \
            .expression = e})
#define OK RESULT(true, "")
#define ASSERT_EQ(expr)                                                        \
  if (!(expr)) {                                                               \
    return RESULT(false, #expr);                                               \
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

int read_line_with_window(const char *filename, int target_line, int window,
                          char *buffer, size_t buffer_size) {
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    return -1;
  }

  int start_line = target_line - window;
  if (start_line < 1) {
    start_line = 1;
  }
  int end_line = target_line + window;

  char line_buffer[4096];
  size_t pos = 0;
  int current_line = 0;

  buffer[0] = '\0';

  while (fgets(line_buffer, sizeof(line_buffer), fp)) {
    current_line++;

    if (current_line < start_line) {
      continue;
    }
    if (current_line > end_line) {
      break;
    }

    size_t line_len = strlen(line_buffer);

    if (pos + line_len + 4 >= buffer_size) {
      fclose(fp);
      buffer[0] = '\0';
      return -1;
    }
    memcpy(buffer + pos, (current_line == target_line) ? "--> " : "    ", 4);
    pos += 4;
    memcpy(buffer + pos, line_buffer, line_len);
    pos += line_len;
    buffer[pos] = '\0';
  }

  fclose(fp);
  return 0;
}
void _failed(const char *filename, size_t line, const char *func,
             const char *expression) {
  char buf[2048];
  read_line_with_window(filename, line, 2, buf, sizeof(buf));
  fprintf(stderr, "%s:%zu: %s failed at '%s'\n%s\n", filename, line, func,
          expression, buf);
}

Result test_httpd_str_construct() {
  ASSERT_EQ(httpd_str_eq(STR("Foo"), STR("Foo")));
  ASSERT_EQ(!httpd_str_eq(httpd_str_from_parts("FOO", 3), STR("BAR")));
  return OK;
}

#define RUN_TEST(expr)                                                         \
  {                                                                            \
    printf("Running " #expr " ... ");                                          \
    fflush(stdout);                                                            \
    Result r = expr();                                                         \
    fprint_result(stderr, r);                                                  \
  }

int main(void) {
  RUN_TEST(test_httpd_str_construct)
  return 0;
}
