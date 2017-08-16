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

#define MAX_EVENTS      256
#define DEFAULT_TIMEOUT 128

DEFINE_HASHTABLE(sk_hash, 4);
struct timespec *ts_now = NULL;
const wsd_config_t *wsd_cfg = NULL;
int epfd = -1;
unsigned int wsd_errno = WSD_CHECKERRNO;

static bool done = false;
static struct list_head *work_list = NULL;

static int io_loop();
static void sigterm(int sig);
static int sock_accept(int lfd);
static int sock_close(sk_t *sk);
static int post_read(sk_t *sk);
static unsigned long int hash(struct sockaddr_in *saddr);
static void list_init(struct list_head **list);
static void on_iteration();

int
wschild_main(const wsd_config_t *cfg)
{
     wsd_cfg = cfg;

     ts_now = malloc(sizeof(struct timespec));
     A(ts_now);
     memset(ts_now, 0, sizeof(struct timespec));

     list_init(&work_list);
     hash_init(sk_hash);

     struct sigaction sac;
     memset(&sac, 0x0, sizeof(struct sigaction));
     sac.sa_handler = sigterm;
     AZ(sigaction(SIGTERM, &sac, NULL));

     epfd = epoll_create(1);
     A(epfd >= 0);

     AZ(listen(wsd_cfg->lfd, 5));
     
     struct epoll_event ev;
     memset(&ev, 0x0, sizeof(ev));
     ev.events = EPOLLIN;
     ev.data.fd = wsd_cfg->lfd;
     AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, wsd_cfg->lfd, &ev));

     int rv = io_loop();

     free(ts_now);

     return rv;
}

int
io_loop()
{
     int rv = 0;
     struct epoll_event evs[MAX_EVENTS];
     memset(&evs, 0x0, sizeof(evs));

     int timeout = DEFAULT_TIMEOUT;
     while (!done) {

          int nfd = epoll_wait(epfd, evs, MAX_EVENTS, timeout);
          if (0 > nfd && EINTR == errno)
               continue;
          A(nfd >= 0);
          A(nfd <= MAX_EVENTS);

          int n;
          for (n = 0; n < nfd; n++) {

               if (evs[n].data.fd == wsd_cfg->lfd) {
                    sock_accept(evs[n].data.fd);
                    continue;
               }

               rv = on_epoll_event(&evs[n], post_read);
          }

          on_iteration();
     }

     return rv;
}

void
on_iteration() {
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
