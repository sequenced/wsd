#include <string.h>
#include "hashtable.h"

struct test6_struct
{
  struct list_head list_head;
  char *s;
};

unsigned int
test6_hash_func(unsigned int val)
{
  return val%16;
}

int
main(int argc, char **argv)
{
  struct table_head table;
  memset((void*)&table, 0x0, sizeof(struct table_head));
  table.hash_func=test6_hash_func;

  struct test6_struct ts;
  memset((void*)&ts, 0x0, sizeof(struct test6_struct));
  ts.s="foobar";

  hashtable_add(&ts.list_head, 7, &table);
  hashtable_add(&ts.list_head, 23, &table);

  return 0;
}
