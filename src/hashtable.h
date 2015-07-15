#ifndef __HASHTABLE_H__
#define __HASHTABLE_H__

#include "list.h"

struct table_head
{
  unsigned int (*hash_func)(unsigned int);
  struct list_head bucket[16];
};

static inline void
hashtable_add(struct list_head *new, unsigned int key, struct table_head *table)
{
  unsigned int hash=table->hash_func(key);

  if (NULL==table->bucket[hash].next)
    init_list_head(&(table->bucket[hash]));

  list_add_tail(new, &(table->bucket[hash]));
}

#endif /* #ifndef __HASHTABLE_H__ */
