/*
 *  Copyright (C) 2014-2020 Michael Goldschmidt
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

#define _GNU_SOURCE

#include <time.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "common.h"
#include "hashtable.h"
#include "wschild.h"
#include "http.h"
#include "ws_wsd.h"
#include "ws.h"

#define DEFAULT_TIMEOUT 128

extern bool done;

DEFINE_HASHTABLE(sk_hash, 4);
const wsd_config_t *wsd_cfg = NULL;
int epfd = -1;
unsigned int wsd_errno = WSD_CHECKERRNO;
struct list_head *sk_list = NULL;                    /* List of open sockets */

static struct list_head *work_list = NULL;           /* List of pending work */

static void sigterm(int sig);
static int sk_accept(int lfd);
static int sk_close(sk_t *sk);
static int post_read(sk_t *sk);
static unsigned long int hash(struct sockaddr_in *saddr);
static void list_init(struct list_head **list);
static int on_iteration(const struct timespec *now);
static void check_timeouts_for_each(const struct timespec *now);
static void check_timeouts(sk_t *sk, const struct timespec *now);
static void try_recv();
static int check_closing_handshake_timeout(const sk_t *sk,
                                           const struct timespec *now,
                                           const int timeout);

int
wschild_main(const wsd_config_t *cfg)
{
     wsd_cfg = cfg;

     list_init(&work_list);
     list_init(&sk_list);
     hash_init(sk_hash);

     struct sigaction sac;
     memset(&sac, 0x0, sizeof(struct sigaction));
     sac.sa_handler = sigterm;
     AZ(sigaction(SIGTERM, &sac, NULL));

     epfd = epoll_create(1);
     A(epfd >= 0);

     sk_t *lsk = malloc(sizeof(sk_t));
     if (!lsk) {
          return (-1);
     }
     memset(lsk, 0, sizeof(sk_t));

     if (0 > sk_init(lsk, wsd_cfg->lfd, 0ULL)) {
          free(lsk);
          return (-1);
     }
     
     lsk->events = EPOLLIN;
     lsk->ops->accept = sk_accept;
     lsk->ops->close = sk_close;

     AZ(listen(lsk->fd, 4));
     AZ(register_for_events(lsk));

     int rv = event_loop(on_iteration, post_read, DEFAULT_TIMEOUT);

     int num = 0;
     sk_t *pos = NULL, *k = NULL;
     list_for_each_entry_safe(pos, k, sk_list, sk_node) {
          pos->ops->close(pos);
          num++;
     }
     syslog(LOG_INFO, "Closed %d open socket(s)", num);

     AZ(close(epfd));
     return rv;
}

int
on_iteration(const struct timespec *now) {
     try_recv();
     check_timeouts_for_each(now);
     return 0;
}

void
check_timeouts_for_each(const struct timespec *now)
{
     sk_t *pos = NULL, *k = NULL;
     list_for_each_entry_safe(pos, k, sk_list, sk_node) {
          check_timeouts(pos, now);
     }
}

void
check_timeouts(sk_t *sk, const struct timespec *now)
{
     if (-1 != wsd_cfg->idle_timeout
         && check_timeout(sk, now, wsd_cfg->idle_timeout)) {
          if (!sk->proto->start_closing_handshake
              || 0 > sk->proto->start_closing_handshake(sk, WS_1000, false)) {
               /* Closing handshake failed or not required: close socket */
               AZ(sk->ops->close(sk));
               return;
          } else {
               /* Started closing handshake, arm timeout. */
               sk->ts_closing_handshake_start.tv_sec = now->tv_sec;
               sk->ts_closing_handshake_start.tv_nsec = now->tv_nsec;
          }
     }

     if (check_closing_handshake_timeout(sk,
                                         now,
                                         wsd_cfg->closing_handshake_timeout)) {
          /* Closing handshake timed out: close socket */
          AZ(sk->ops->close(sk));
          return;
     }

     if (check_timeout(sk, now, wsd_cfg->ping_interval))
          /* Ignoring return value; don't close socket on a failed ping. */
          sk->proto->ping(sk, false);
}

int
check_closing_handshake_timeout(const sk_t *sk,
                                const struct timespec *now,
                                const int timeout)
{
     A(0 < timeout);
     AN(sk);

     if (!sk->closing)
          return 0;
     
     return has_timed_out(&sk->ts_closing_handshake_start,
                          now,
                          timeout) ? 1 : 0;
}

void
try_recv()
{
     sk_t *pos = NULL, *k = NULL;
     list_for_each_entry_safe(pos, k, work_list, work_node) {
          int rv = pos->ops->recv(pos);
          if (0 == rv) {

               /* All data received and processed */
               list_del(&pos->work_node);

          } else if (0 > rv && wsd_errno == WSD_EINPUT) {

               /* 
                * No enough input; delete from work list.
                * Next read puts it into work list again.
                */
               list_del(&pos->work_node);

          } else if (0 > rv && wsd_errno == WSD_EAGAIN) {

               /* Try again on next iteration. */

          } else if (0 > rv) {

               /* Fatal error. */
               list_del(&pos->work_node);
               if (!pos->close_on_write) {
                    AZ(pos->ops->close(pos));
               }
          }
     }
}

int
sk_close(sk_t *sk)
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: hash=0x%lx, rdsz=%d, wrsz=%d, fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sk->hash,
                 skb_rdsz(sk->sendbuf),
                 skb_wrsz(sk->recvbuf),
                 sk->fd);
     }

     hash_del(&sk->hash_node);
     list_del(&sk->sk_node);

     sk_t *pos = NULL, *k = NULL;
     list_for_each_entry_safe(pos, k, work_list, work_node) {
          if (sk->fd == pos->fd) {
               list_del(&pos->work_node);
          }
     }

     AZ(close(sk->fd));
     sk_destroy(sk);
     free(sk);

     sk = NULL;

     return 0;
}

int
post_read(sk_t *sk)
{
     sk_t *pos = NULL;
     list_for_each_entry(pos, work_list, work_node) {
          if (pos->fd == sk->fd)
               return 0;
     }
     list_add_tail(&sk->work_node, work_list);
     return 0;
}

void
sigterm(int sig)
{
     done = true;
}

int
sk_accept(int lfd)
{
     sk_t *sk = malloc(sizeof(sk_t));
     if (!sk) {
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }
     memset(sk, 0, sizeof(sk_t));

     socklen_t saddr_len = sizeof(sk->src_addr);
     int fd = accept4(lfd,
                      (struct sockaddr *)&sk->src_addr,
                      &saddr_len,
                      SOCK_NONBLOCK);
     if (0 > fd) {
          free(sk);
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     saddr_len = sizeof(sk->dst_addr);
     if (0 > getsockname(fd, (struct sockaddr *)&sk->dst_addr, &saddr_len)) {
          AZ(close(fd));
          free(sk);
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     if (0 > sk_init(sk, fd, hash(&sk->src_addr))) {
          AZ(close(fd));
          free(sk);
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     sk->ops->recv = http_recv;
     sk->ops->close = sk_close;
     sk->proto->decode_handshake = ws_decode_handshake;

     if (0 > register_for_events(sk)) {
          AZ(close(fd));
          free(sk);
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     hash_add(sk_hash, &sk->hash_node, sk->hash);
     list_add_tail(&sk->sk_node, sk_list);

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: hash=0x%lx, rdsz=%d, wrsz=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sk->hash,
                 skb_rdsz(sk->sendbuf),
                 skb_wrsz(sk->recvbuf));
     }

     return 0;
}

unsigned long int
hash(struct sockaddr_in *saddr) {
     struct timespec ts;
     memset((void*)&ts, 0, sizeof(ts));
     AZ(clock_gettime(CLOCK_REALTIME_COARSE, &ts));

     unsigned long int h = saddr->sin_addr.s_addr;
     h <<= 16;
     h |= saddr->sin_port;
     h <<= 16;
     h |= ((ts.tv_nsec &0x00000000ffff0000 >> 16));
     return h;
}

void
list_init(struct list_head **list)
{
     *list = malloc(sizeof(struct list_head));
     AN(list);
     memset(*list, 0, sizeof(struct list_head));
     init_list_head(*list);
}
