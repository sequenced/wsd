#ifndef __COMMON_H__
#define __COMMON_H__

#include <sys/types.h>
#include <sys/epoll.h>

#include "types.h"

#define skb_rdsz(buf) (buf->wrpos - buf->rdpos)
#define skb_wrsz(buf) ((unsigned int)sizeof(buf->data) - buf->wrpos)
#define skb_put(buf, obj)                             \
     *(typeof(obj)*)(&buf->data[buf->wrpos]) = obj;   \
     buf->wrpos += sizeof(typeof(obj));
#define skb_get(buf, obj)                                  \
     obj = *(typeof(obj)*)(&buf->data[buf->rdpos]);        \
     buf->rdpos += sizeof(typeof(obj));
#define skb_reset(buf)                          \
     buf->wrpos = 0;                            \
     buf->rdpos = 0;
#define skb_rd_rewind(buf, amount)              \
     buf->rdpos -= amount;
#define skb_rd_reset(buf, pos)                  \
     buf->rdpos = pos;
#define ts_last_io_set(dst, src)                \
     dst->ts_last_io.tv_sec = src->tv_sec;      \
     dst->ts_last_io.tv_nsec = src->tv_nsec;
#define unmask mask

void sock_destroy(sk_t *sk);
int sock_init(sk_t *sk, int fd, unsigned long int hash);
void turn_on_events(sk_t *sk, unsigned int events);
void turn_off_events(sk_t *sk, unsigned int events);
int on_epoll_event(struct epoll_event *evt,
                   int (*post_read)(sk_t *sk),
                   const struct timespec *now);
int has_rnrn_termination(skb_t *b);
int register_for_events(sk_t *sk);
int skb_put_str(skb_t *b, const char *s);
int skb_put_strn(skb_t *b, const char *s, size_t n);
void skb_compact(skb_t *b);
int skb_print(FILE *stream, skb_t *b, size_t n);
void trim(chunk_t *chk);
char mask(char c, unsigned int i, unsigned int key);
int event_loop(int (*on_iteration)(const struct timespec *now),
               int (*post_read)(sk_t *sk),
               int timeout);

#endif /* #ifndef __COMMON_H__ */
