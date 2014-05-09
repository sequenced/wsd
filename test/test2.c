#include <string.h>
#include <assert.h>
#include "wstypes.h"

const char *delimited="one,two,three,four";
const char *not_delimited="one";
const char *at_beginning=",two";
const char *at_end="one,";

int
main(int argc, char **argv)
{
  string_t s;
  s.start=delimited;
  s.len=strlen(delimited);
  string_t *rv=tok(&s, ',');
  assert(0<rv->len);
  assert(0==strncmp("one", rv->start, rv->len));
  rv=tok(NULL, ',');
  assert(0<rv->len);
  assert(0==strncmp("two", rv->start, rv->len));
  rv=tok(NULL, ',');
  assert(0<rv->len);
  assert(0==strncmp("three", rv->start, rv->len));
  rv=tok(NULL, ',');
  assert(0<rv->len);
  assert(0==strncmp("four", rv->start, rv->len));
  rv=tok(NULL, ',');
  assert(-1==rv->len);

  s.start=not_delimited;
  s.len=strlen(not_delimited);
  rv=tok(&s, ',');
  assert(0<rv->len);
  assert(0==strncmp("one", rv->start, rv->len));
  rv=tok(NULL, ',');
  assert(-1==rv->len);

  s.start=at_beginning;
  s.len=strlen(at_beginning);
  rv=tok(&s, ',');
  assert(0==rv->len);
  rv=tok(NULL, ',');
  assert(0<rv->len);
  assert(0==strncmp("two", rv->start, rv->len));
  rv=tok(NULL, ',');
  assert(-1==rv->len);

  s.start=at_end;
  s.len=strlen(at_end);
  rv=tok(&s, ',');
  assert(0<rv->len);
  assert(0==strncmp("one", rv->start, rv->len));
  rv=tok(NULL, ',');
  assert(-1==rv->len);

  return 0;
}
