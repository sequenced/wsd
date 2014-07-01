#ifndef __LIST_H__
#define __LIST_H__

#include <stddef.h> /* for offsetof */

/* A list implementation; copied from linux/list.h and slightly adapted */

#define container_of(ptr, type, member) ({                              \
      const typeof(((type *)0)->member)*__mptr = (ptr);                 \
      (type *)((char *)__mptr - offsetof(type, member)); })

struct list_head {
  struct list_head *next, *prev;
};

static inline void
init_list_head(struct list_head *list)
{
  list->next = list;
  list->prev = list;
}

static inline void
__list_add(struct list_head *new,
           struct list_head *prev,
           struct list_head *next)
{
  next->prev = new;
  new->next = next;
  new->prev = prev;
  prev->next = new;
}

static inline void
list_add_tail(struct list_head *new, struct list_head *head)
{
  __list_add(new, head->prev, head);
}

#define list_entry(ptr, type, member)           \
  container_of(ptr, type, member)

#define list_for_each_entry(pos, head, member)				\
  for (pos = list_entry((head)->next, typeof(*pos), member);            \
       &pos->member != (head);                                          \
       pos = list_entry(pos->member.next, typeof(*pos), member))

#endif /* #ifndef __LIST_H__ */
