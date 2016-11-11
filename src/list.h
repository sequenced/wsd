#ifndef __LIST_H__
#define __LIST_H__

#include <stddef.h> /* for offsetof */

/* A list implementation; copied from linux/list.h and slightly adapted */

#define container_of(ptr, type, member) ({                              \
               const typeof(((type *)0)->member)*__mptr = (ptr);        \
               (type *)((char *)__mptr - offsetof(type, member)); })

#define HLIST_HEAD_INIT { .first = NULL }
#define INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL)
#define WRITE_ONCE(x, val) x = (val)
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct list_head {
     struct list_head *next, *prev;
};

struct hlist_head {
     struct hlist_node *first;
};

struct hlist_node {
     struct hlist_node *next, **pprev;
};

static inline void
init_list_head(struct list_head *list)
{
     list->next=list;
     list->prev=list;
}

static inline void
__list_add(struct list_head *new,
           struct list_head *prev,
           struct list_head *next)
{
     next->prev=new;
     new->next=next;
     new->prev=prev;
     prev->next=new;
}

static inline void
list_add_tail(struct list_head *new, struct list_head *head)
{
     __list_add(new, head->prev, head);
}

#define list_entry(ptr, type, member)           \
     container_of(ptr, type, member)

#define list_for_each_entry(pos, head, member)                          \
     for (pos=list_entry((head)->next, typeof(*pos), member);           \
          &pos->member!=(head);                                         \
          pos=list_entry(pos->member.next, typeof(*pos), member))

static inline void
__list_del(struct list_head *prev, struct list_head *next)
{
     next->prev=prev;
     prev->next=next;
}

static inline void
list_del(struct list_head *entry)
{
     __list_del(entry->prev, entry->next);
}

#define list_for_each_entry_safe(pos, n, head, member)			\
     for (pos=list_entry((head)->next, typeof(*pos), member),           \
               n=list_entry(pos->member.next, typeof(*pos), member);    \
          &pos->member!=(head);                                         \
          pos=n, n=list_entry(n->member.next, typeof(*n), member))

static inline int
list_empty(const struct list_head *head)
{
     return head->next==head;
}

static inline void
INIT_HLIST_NODE(struct hlist_node *h)
{
     h->next = NULL;
     h->pprev = NULL;
}

static inline void
hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
     struct hlist_node *first = h->first;
     n->next = first;
     if (first)
          first->pprev = &n->next;
     WRITE_ONCE(h->first, n);
     n->pprev = &h->first;
}

static inline int
hlist_unhashed(const struct hlist_node *h)
{
     return !h->pprev;
}

#define hlist_entry(ptr, type, member) container_of(ptr, type, member)

#define hlist_entry_safe(ptr, type, member)                     \
     ({ typeof(ptr) ____ptr = (ptr);                            \
          ____ptr ? hlist_entry(____ptr, type, member) : NULL;  \
     })
#define hlist_for_each_entry(pos, head, member)				\
     for (pos = hlist_entry_safe((head)->first, typeof(*(pos)), member); \
          pos;                                                          \
          pos = hlist_entry_safe((pos)->member.next, typeof(*(pos)), member))

#endif /* #ifndef __LIST_H__ */
