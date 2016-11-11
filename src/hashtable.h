#ifndef __HASHTABLE_H__
#define __HASHTABLE_H__

#include <stdbool.h>
#include "list.h"

#define HASH_SIZE(name) (ARRAY_SIZE(name))
#define HASH_BITS(name) (HASH_SIZE(name) >> 1)
#define DECLARE_HASHTABLE(name, bits)           \
     struct hlist_head name[1 << (bits)]
#define DEFINE_HASHTABLE(name, bits)                    \
     struct hlist_head name[1 << (bits)] =              \
     { [0 ... ((1 << (bits)) - 1)] = HLIST_HEAD_INIT }
#define hash_init(hashtable) __hash_init(hashtable, HASH_SIZE(hashtable))
#define hash_add(hashtable, node, key)                                  \
     hlist_add_head(node, &hashtable[hash_min(key, HASH_BITS(hashtable))])
#define hash_for_each_possible(name, obj, member, key)			\
     hlist_for_each_entry(obj, &name[hash_min(key, HASH_BITS(name))], member)
#define hash_long(val, bits) (val >> (64 - bits))
#define hash_32(val, bits) (val >> (32 - bits))
#define hash_min(val, bits)                                             \
     (sizeof(val) <= 4 ? hash_32(val, bits) : hash_long(val, bits))
#define hash_for_each(name, bkt, obj, member)				\
     for ((bkt) = 0, obj = NULL; obj == NULL && (bkt) < HASH_SIZE(name); \
          (bkt)++)                                                      \
          hlist_for_each_entry(obj, &name[bkt], member)

static inline void
__hash_init(struct hlist_head *ht, unsigned int sz)
{
     unsigned int i;

     for (i = 0; i < sz; i++)
          INIT_HLIST_HEAD(&ht[i]);
}

static inline bool
hash_hashed(struct hlist_node *node)
{
     return !hlist_unhashed(node);
}

static inline void
__hlist_del(struct hlist_node *n)
{
     struct hlist_node *next = n->next;
     struct hlist_node **pprev = n->pprev;

     WRITE_ONCE(*pprev, next);
     if (next)
          next->pprev = pprev;
}

static inline void
hlist_del_init(struct hlist_node *n)
{
     if (!hlist_unhashed(n)) {
          __hlist_del(n);
          INIT_HLIST_NODE(n);
     }
}

static inline void
hash_del(struct hlist_node *node)
{
     hlist_del_init(node);
}

#endif /* #ifndef __HASHTABLE_H__ */
