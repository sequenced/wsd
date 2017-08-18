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

DEFINE_HASHTABLE(sk_hash, 4);
const wsd_config_t *wsd_cfg = NULL;
int epfd = -1;
unsigned int wsd_errno = WSD_CHECKERRNO;

static struct list_head *work_list = NULL;

static void sigterm(int sig);
static int sock_accept(int lfd);
static int sock_close(sk_t *sk);
static int post_read(sk_t *sk);
static unsigned long int hash(struct sockaddr_in *saddr);
static void list_init(struct list_head **list);
static int on_iteration(const struct timespec *now);

int
wschild_main(const wsd_config_t *cfg)
{
     wsd_cfg = cfg;

     list_init(&work_list);
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

     if (0 > sock_init(lsk, wsd_cfg->lfd, 0ULL)) {
          free(lsk);
          return (-1);
     }
     
     lsk->events = EPOLLIN;
     lsk->ops->accept = sock_accept;
     lsk->ops->close = sock_close;

     AZ(listen(lsk->fd, 4));
     AZ(register_for_events(lsk));

     int rv = event_loop(on_iteration, post_read, DEFAULT_TIMEOUT);

     /* TODO Close open sockets */

     AZ(close(epfd));

     return rv;
}

int
on_iteration(const struct timespec *now) {
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

     return 0;
}

int
sock_close(sk_t *sk)
{
     hash_del(&sk->hash_node);

     sk_t *pos = NULL, *k = NULL;
     list_for_each_entry_safe(pos, k, work_list, work_node) {
          if (sk->fd == pos->fd) {
               list_del(&pos->work_node);
          }
     }

     AZ(close(sk->fd));
     sock_destroy(sk);
     free(sk);

     return 0;
}

int
post_read(sk_t *sk)
{
     sk_t *pos;
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
     // TODO
}

int
sock_accept(int lfd)
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

     if (0 > sock_init(sk, fd, hash(&sk->src_addr))) {
          AZ(close(fd));
          free(sk);
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     sk->ops->recv = http_recv;
     sk->ops->close = sock_close;
     sk->proto->decode_handshake = ws_decode_handshake;

     if (0 > register_for_events(sk)) {
          AZ(close(fd));
          free(sk);
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     hash_add(sk_hash, &sk->hash_node, sk->hash);

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
