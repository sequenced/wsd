#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "wstypes.h"
#include "list.h"
#include "config_parser.h"

extern int line;

const char *input1=" \t   \r  \n\n   ";
const char *input2=" # first comment \n# *** second comment ***\n\n\t\n";
const char *input3=" < UnknownDirective /foobar    > \n\tSetHandler";
const char *input4="<Location /foobar> SetHandler foobarlib </ Location > ";
const char *input5="# s\n<Location /bar1>\nSetHandler bar12\n</Location>\n";

typedef struct
{
  struct list_head list_head;
  struct list_head location_list;
} test5_t;

static buf_t*
prepare_buffer(const char *content)
{
  buf_t *b=buf_alloc(strlen(content));
  buf_put_string(b, content);
  buf_flip(b);
  return b;
}

int
main(int argc, char **argv)
{
  buf_t *b=prepare_buffer(input1);
  assert(1==line);
  assert(0==parse_config(NULL, b));
  assert(3==line);
  line=1;
  buf_free(b);

  b=prepare_buffer(input2);
  assert(1==line);
  assert(0==parse_config(NULL, b));
  assert(5==line);
  line=1;
  buf_free(b);

  b=prepare_buffer(input3);
  assert(1==line);
  assert(-1==parse_config(NULL, b));
  assert(2==line);
  line=1;
  buf_free(b);

  test5_t t;
  memset((void*)&t, 0x0, sizeof(test5_t));
  init_list_head(&t.location_list);

  b=prepare_buffer(input4);
  assert(1==line);
  assert(0==parse_config(&t.location_list, b));
  assert(1==line);
  buf_free(b);

  b=prepare_buffer(input5);
  assert(1==line);
  assert(0==parse_config(&t.location_list, b));
  assert(5==line);
  line=1;
  buf_free(b);

  return 0;
}
