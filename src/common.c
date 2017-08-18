/*
 *  Copyright (C) 2017 Michael Goldschmidt
 *
 *  This file is part of wsd/wscat.
 *
 *  wsd/wscat is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  wsd/wscat is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with wsd/wscat.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/epoll.h>

#include "common.h"

#define MAX_EVENTS 256

extern int epfd;
extern int wsd_errno;
extern const wsd_config_t *wsd_cfg;

bool done = false;

static int sock_read(sk_t *sk);
static int sock_write(sk_t *sk);

inline void
turn_off_events(sk_t *sk, unsigned int events)
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, events=0x%x\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sk->fd,
                 events);
     }

     sk->events &= ~events;

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = sk->events;
     ev.data.ptr = sk;
     AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, sk->fd, &ev));
}

inline void
turn_on_events(sk_t *sk, unsigned int events)
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, events=0x%x\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sk->fd,
                 events);
     }

     sk->events |= events;

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = sk->events;
     ev.data.ptr = sk;
     AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, sk->fd, &ev));
}

int
register_for_events(sk_t *fd)
{
     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = fd->events;
     ev.data.ptr = fd;
     return epoll_ctl(epfd, EPOLL_CTL_ADD, fd->fd, &ev);
}

inline void
skb_compact(skb_t *b)
{
     memmove(b->data, b->data + b->rdpos, b->wrpos - b->rdpos);
     b->wrpos -= b->rdpos;
     b->rdpos = 0;
}

inline void
trim(chunk_t *chk)
{
     unsigned int i = 0;
     while (chk->len) {

          char c = *(chk->p + i);
          if (' ' == c || '\t' == c) {
               chk->p++;
               chk->len--;
               i++;
          }
          else
               break;
     }

     while (chk->len) {

          char c = *(chk->p + chk->len - 1); /* -1: 1st char at position zero */
          if (' ' == c || '\t' == c)
               chk->len--;
          else
               break;
     }
}

inline int
skb_put_str(skb_t *b, const char *s)
{
     return skb_put_strn(b, s, strlen(s));
}

inline int
skb_put_strn(skb_t *b, const char *s, size_t n) {
     if (skb_wrsz(b) < n)
          return (-1);

     strncpy(&b->data[b->wrpos], s, n);
     b->wrpos += n;

     return 0;
}

int
sock_init(sk_t *sk, int fd, unsigned long int hash)
{
     sk->proto = malloc(sizeof(struct proto));
     if (!sk->proto) {
          wsd_errno = WSD_ENOMEM;
          return (-1);
     }
     memset(sk->proto, 0, sizeof(struct proto));

     sk->ops = malloc(sizeof(struct ops));
     if (!sk->ops) {
          wsd_errno = WSD_ENOMEM;
          return (-1);
     }
     memset(sk->ops, 0, sizeof(struct ops));

     sk->sendbuf = malloc(sizeof(skb_t));
     if (!sk->sendbuf) {
          wsd_errno = WSD_ENOMEM;
          return (-1);
     }
     memset(sk->sendbuf, 0, sizeof(skb_t));

     sk->recvbuf = malloc(sizeof(skb_t));
     if (!sk->recvbuf) {
          wsd_errno = WSD_ENOMEM;
          return (-1);
     }
     memset(sk->recvbuf, 0, sizeof(skb_t));

     sk->hash = hash;
     sk->fd = fd;
     sk->events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
     sk->ops->read = sock_read;
     sk->ops->write = sock_write;

     return 0;
}

void
sock_destroy(sk_t *sk)
{
     if (sk->ops)
          free(sk->ops);

     if (sk->proto)
          free(sk->proto);

     memset(sk, 0, sizeof(sk_t));
}

int
on_epoll_event(struct epoll_event *evt,
               int (*post_read)(sk_t *sk),
               const struct timespec *now)
{
     int rv = 0;
     sk_t *sk = (sk_t*)evt->data.ptr;
     A(sk->fd >= 0);

     if (evt->events & EPOLLIN ||
         evt->events & EPOLLPRI) {

          A(!sk->close_on_write);
          A(!sk->close);

          rv = sk->ops->read(sk);
          ts_last_io_set(sk, now);
          if (0 > rv && wsd_errno != WSD_EAGAIN) {
               AZ(sk->ops->close(sk));
          } else {
               rv = (*post_read)(sk);
               if (0 > rv &&
                   wsd_errno != WSD_EAGAIN &&
                   wsd_errno != WSD_EINPUT) {
                    AZ(sk->ops->close(sk));
               } else if (sk->close) {
                    AZ(sk->ops->close(sk));
               }
          }

     } else if (evt->events & EPOLLOUT) {

          rv = sk->ops->write(sk);
          ts_last_io_set(sk, now);
          if (0 > rv && wsd_errno != WSD_EAGAIN) {
               AZ(sk->ops->close(sk));
          } else if (sk->close_on_write && 0 == skb_rdsz(sk->sendbuf)) {
               AZ(sk->ops->close(sk));
          } else if (sk->close) {
               AZ(sk->ops->close(sk));
          }

     } else if (evt->events & EPOLLERR ||
                evt->events & EPOLLHUP ||
                evt->events & EPOLLRDHUP) {
          AZ(sk->ops->close(sk));
     }

     return rv;
}

int
sock_read(sk_t *sk)
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, wrsz=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sk->fd,
                 skb_wrsz(sk->recvbuf));
     }

     A(0 <= sk->fd);

     if (0 == skb_wrsz(sk->recvbuf)) {
          turn_off_events(sk, EPOLLIN);
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     A(0 < skb_wrsz(sk->recvbuf));
     int len = read(sk->fd,
                    &sk->recvbuf->data[sk->recvbuf->wrpos],
                    skb_wrsz(sk->recvbuf));

     /* ECONNREFUSED or others */
     if (0 > len) {
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     /* EOF */
     if (0 == len) {
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("\t%s:%d: read %d byte(s)\n", __func__, __LINE__, len);
     }
     
     sk->recvbuf->wrpos += len;
     return 0;
}

int
sock_write(sk_t *sk)
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, rdsz=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sk->fd,
                 skb_rdsz(sk->sendbuf));
     }

     if (0 == skb_rdsz(sk->sendbuf)) {
          turn_off_events(sk, EPOLLOUT);
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     A(0 < skb_rdsz(sk->sendbuf));
     int len = write(sk->fd,
                     &sk->sendbuf->data[sk->sendbuf->rdpos],
                     skb_rdsz(sk->sendbuf));

     if (0 > len) {
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("\t%s:%d: wrote %d byte(s)\n", __func__, __LINE__, len);
     }

     sk->sendbuf->rdpos += len;
     skb_compact(sk->sendbuf);

     return 0;
}

int
has_rnrn_termination(skb_t *b)
{
     unsigned int len = skb_rdsz(b);
     char *s = &b->data[b->rdpos];
     while (len--) {
          if (*s == '\r'
              && 3 <= len
              && *(s + 1) == '\n'
              && *(s + 2) == '\r'
              && *(s + 3) == '\n')
               return 1;

          s++;
     }

     return 0;
}

inline char
mask(char c, unsigned int i, unsigned int key)
{
     unsigned int j = i % 4;

     /* See section 5.3 RFC6455 for specification */
     unsigned char mask;
     switch (j) {
     case 0:
          mask = (unsigned char)(key & 0x000000ff);
          break;
     case 1:
          mask = (unsigned char)((key & 0x0000ff00) >> 8);
          break;
     case 2:
          mask = (unsigned char)((key & 0x00ff0000) >> 16);
          break;
     case 3:
          mask = (unsigned char)((key & 0xff000000) >> 24);
          break;
     default:
          return (-1);
     }

     return (c ^= mask);
}

int
skb_print(FILE *stream, skb_t *b, size_t n) {
     while (n--)
          fprintf(stream, "%c", (unsigned char)b->data[b->rdpos++]);

     int rv;
     if (0 > (rv = fflush(stream)))
          wsd_errno = WSD_CHECKERRNO;

     return rv;
}

int
event_loop(int (*on_iteration)(const struct timespec *now),
           int (*post_read)(sk_t *sk),
           int timeout)
{
     int rv = 0;
     struct epoll_event evs[MAX_EVENTS];
     memset(&evs, 0, sizeof(evs));

     struct timespec *now = malloc(sizeof(struct timespec));
     AN(now);
     memset(now, 0, sizeof(struct timespec));

     while (!done) {
          AZ(clock_gettime(CLOCK_MONOTONIC, now));
          AZ((*on_iteration)(now));

          int nfd = epoll_wait(epfd, evs, MAX_EVENTS, timeout);
          if (0 > nfd && EINTR == errno)
               continue;

          A(nfd >= 0);
          A(nfd <= MAX_EVENTS);

          int n;
          for (n = 0; n < nfd; n++) {

               sk_t *sk = (sk_t*)evs[n].data.ptr;
               if (sk->fd == wsd_cfg->lfd) {
                    AZ(sk->ops->accept(sk->fd));
                    continue;
               }

               rv = on_epoll_event(&evs[n], post_read, now);

          }
     }

     free(now);
     return rv;
}
