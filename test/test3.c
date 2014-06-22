#include <string.h>
#include <assert.h>
#include "wstypes.h"

int
main(int argc, char **argv)
{
  hash32_table_t t;
  memset((void*)&t, 0x0, sizeof(hash32_table_t));
  t.hash=hash32_table_hash;

  char *test_data[] = {"", "one", "two", "three", "four", "five", "six"};
  assert(0==hash32_table_insert(&t, 1, test_data[0]));
  assert(0==strcmp(test_data[0], (char*)hash32_table_lookup(&t, 1)));

  /* insert new value for existing key */
  assert(0==hash32_table_insert(&t, 1, test_data[1]));
  assert(0==strcmp(test_data[1], (char*)hash32_table_lookup(&t, 1)));

  /* store a bunch of values and lookup them */
  int i;
  for (i=1; i<7; i++)
    assert(0==hash32_table_insert(&t, i, test_data[i]));

  for (i=1; i<7; i++)
    assert(0==strcmp(test_data[i], (char*)hash32_table_lookup(&t, i)));

  return 0;
}
