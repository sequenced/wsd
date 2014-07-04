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

  /* test buffer routines put, get and compact */
  const int len=128;
  buf_t *b=buf_alloc(len);
  
  assert(len==buf_len(b));

  buf_put(b, 'a');
  buf_put(b, 'b');
  buf_put(b, 'c');
  buf_put(b, 'd');
  buf_put(b, 'e');

  buf_flip(b);

  assert('a'==buf_get(b));
  assert('b'==buf_get(b));
  assert('c'==buf_get(b));

  buf_compact(b);

  assert(2==buf_pos(b));
  
  buf_put(b, 'f');
  buf_put(b, 'g');

  buf_flip(b);

  assert('d'==buf_get(b));
  assert('e'==buf_get(b));
  assert('f'==buf_get(b));
  assert('g'==buf_get(b));

  buf_rwnd(b, 1);

  assert('g'==buf_get(b));
  assert(0==buf_len(b));

  buf_compact(b);

  assert(len==buf_len(b));

  buf_put(b, '1');
  buf_put(b, '2');

  buf_flip(b);

  assert('1'==buf_get(b));

  buf_flip(b);
  buf_put(b, '3');
  buf_put(b, '4');
  buf_flip(b);

  assert('1'==buf_get(b));
  assert('2'==buf_get(b));
  assert('3'==buf_get(b));
  assert('4'==buf_get(b));

  buf_compact(b);

  assert(len==buf_len(b));

  return 0;
}
