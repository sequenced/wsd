#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include "wstypes.h"
#include "list.h"

typedef struct
{
  struct list_head list_head;
  struct list_head string_list;
} test4_t;

typedef struct
{
  struct list_head list_head;
  char *string;
  struct timeval tv;
} test4_string_t;

static test4_string_t*
new_entry(char *s)
{
  test4_string_t *e;
  e=malloc(sizeof(test4_string_t));
  memset((void*)e, 0x0, sizeof(test4_string_t));
  e->string=s;
  gettimeofday(&e->tv, NULL);
  return e;
}

int
main(int argc, char **argv)
{
  test4_t t;
  memset((void*)&t, 0x0, sizeof(test4_t));
  init_list_head(&t.string_list);

  test4_string_t *s=new_entry("one");
  list_add_tail(&s->list_head, &t.string_list);
  s=new_entry("two");
  list_add_tail(&s->list_head, &t.string_list);
  s=new_entry("three");
  list_add_tail(&s->list_head, &t.string_list);

  test4_string_t *e;
  list_for_each_entry(e, &t.string_list, list_head)
    if (0!=strcmp("one", e->string)
        && 0!=strcmp("two", e->string)
        && 0!=strcmp("three", e->string))
      return 1;

  return 0;
}
