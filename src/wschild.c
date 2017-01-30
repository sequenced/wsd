#define _GNU_SOURCE
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "wstypes.h"
#include "hashtable.h"
#include "http.h"
#include "ws.h"

#define MAX_EVENTS 256
#define DEFAULT_QUANTUM 5000

const wsd_config_t *wsd_cfg = NULL;

DEFINE_HASHTABLE(ep_hash, 4);
int epfd;

static bool done = false;

static int sock_read(ep_t *ep);
static int sock_write(ep_t *ep);
static int sock_close(ep_t *ep);
static int sock_accept(int lfd);
static int io_loop();
static void sigterm(int sig);
static long unsigned int hash(struct sockaddr_in *saddr, struct timespec *ts);

int
wschild_main(const wsd_config_t *cfg)
{
     wsd_cfg = cfg;

     struct sigaction sac;
     memset(&sac, 0x0, sizeof(struct sigaction));
     sac.sa_handler = sigterm;
     AZ(sigaction(SIGTERM, &sac, NULL));

     hash_init(ep_hash);
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: endpoint hashtable has %lu entries\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sizeof ep_hash);
     }

     epfd = epoll_create(1);
     A(epfd >= 0);

     AZ(listen(wsd_cfg->lfd, 5));

     struct epoll_event ev;
     memset((void*)&ev, 0, sizeof(ev));
     ev.events = EPOLLIN;
     ev.data.fd = wsd_cfg->lfd;
     AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, wsd_cfg->lfd, &ev));

     return io_loop();
}

static void
sigterm(int sig)
{
     done = true;
}

static int
io_loop() {
     struct epoll_event evs[MAX_EVENTS];
     memset((void*)&evs, 0, sizeof(evs));

     while (!done) {
          int nfd = epoll_wait(epfd, evs, MAX_EVENTS, DEFAULT_QUANTUM);
          if (0 > nfd && EINTR == errno)
               continue;
          A(nfd >= 0);
          A(nfd <= MAX_EVENTS);

          if (0 < nfd && LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("%s:%d: %s: nfd=%d\n",
                      __FILE__,
                      __LINE__,
                      __func__,
                      nfd);
          }

          int n;
          for (n = 0; n < nfd; ++n) {

               if (evs[n].data.fd == wsd_cfg->lfd) {
                    AZ(sock_accept(evs[n].data.fd));
                    continue;
               }

               ep_t *ep = (ep_t*)evs[n].data.ptr;

               if (LOG_VVERBOSE <= wsd_cfg->verbose) {
                    printf("\t%s: fd=%d, events=0x%x\n",
                           __func__,
                           ep->fd,
                           evs[n].events);
               }

               A(ep->read);
               A(ep->write);
               A(ep->close);

               if (evs[n].events & EPOLLIN ||
                   evs[n].events & EPOLLPRI) {
                    int rv = ep->read(ep);
                    if (0 > rv) {
                         AZ(ep->close(ep));
                    } else {
                         A(0 <= rv);

                         if (0 == buf_read_sz(ep->send_buf))
                              continue;

                         struct epoll_event ev;
                         memset(&ev, 0, sizeof(ev));
                         ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
                         ev.data.ptr = ep;
                         AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, ep->fd, &ev));
                    }
               } else if (evs[n].events & EPOLLOUT) {
                    int rv = ep->write(ep);
                    if (0 > rv) {
                         AZ(ep->close(ep));
                    } else {
                         A(0 <= rv);
                    }
               } else if (evs[n].events & EPOLLHUP ||
                          evs[n].events & EPOLLRDHUP ||
                          evs[n].events & EPOLLERR) {
                    AZ(ep->close(ep));
               }
          }
     }

     int bkt, num = 0;
     ep_t *ep;
     hash_for_each(ep_hash, bkt, ep, hash_node) {
          AN(ep->close);
          AZ(ep->close(ep));

          if (ep->proto.close)
               AZ(ep->proto.close());

          num++;
     }
     AZ(close(wsd_cfg->lfd));
     num++;
     syslog(LOG_INFO, "closed %d socket(s)", num);

     return 0;
}

static int
sock_accept(int lfd)
{
     struct sockaddr_in saddr;
     memset(&saddr, 0x0, sizeof(saddr));

     socklen_t saddr_len = sizeof(saddr);
     int fd = accept4(lfd,
                      (struct sockaddr *)&saddr,
                      &saddr_len,
                      SOCK_NONBLOCK);
     A(fd >= 0);

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = EPOLLIN | EPOLLRDHUP;
     ev.data.ptr = malloc(sizeof(ep_t));
     A(ev.data.ptr);
     ep_t *ep = (ep_t*)ev.data.ptr;
     ep_init(ep);
     ep->fd = fd;
     ep->read = sock_read;
     ep->write = sock_write;
     ep->close = sock_close;
     ep->proto.recv = http_recv;
     ep->proto.handshake = ws_handshake;

     struct timespec ts;
     memset((void*)&ts, 0, sizeof(ts));
     AZ(clock_gettime(CLOCK_REALTIME_COARSE, &ts));
     ep->hash = hash(&saddr, &ts);
     AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev));

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, hash=0x%lx\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 fd,
                 ep->hash);
     }

     return 0;
}

static int
sock_read(ep_t *ep)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {     
          printf("%s:%d: %s: fd=%d, wrsz=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd,
                 buf_write_sz(ep->recv_buf));
     }

     AN(buf_write_sz(ep->recv_buf));
     int len = read(ep->fd, ep->recv_buf->p, buf_write_sz(ep->recv_buf));
     ERRET(0 > len, "read");

     /* EOF */
     if (0 == len)
          return (-1);

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("\t%s: read %d byte(s)\n", __func__, len);
     }

     ep->recv_buf->wrpos += len;
     AN(ep->proto.recv);
     return ep->proto.recv(ep);
}

static int
sock_write(ep_t *ep)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, rdsz=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd,
                 buf_read_sz(ep->send_buf));
     }

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          ep->send_buf->p[ep->send_buf->wrpos] = '\0';
          printf("%s\n", &ep->send_buf->p[ep->send_buf->rdpos]);
     }

     A(ep->fd >= 0);
     A(buf_read_sz(ep->send_buf) > 0);
     int len = write(ep->fd, ep->send_buf->p, buf_read_sz(ep->send_buf));
     if (0 < len) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: wrote %d byte(s)\n", __func__, len);
          }

          ep->send_buf->rdpos += len;
          AZ(buf_read_sz(ep->send_buf));
          buf_reset(ep->send_buf);

          if (0 == ep->send_buf->rdpos) {
               struct epoll_event ev;
               memset(&ev, 0, sizeof(ev));
               ev.events = EPOLLIN | EPOLLRDHUP;
               ev.data.ptr = ep;
               AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, ep->fd, &ev));

               if (ep->close_on_write)
                    sock_close(ep);
          }
     } else {
          A(0 > len);
     }

     return len;
}

static int sock_close(ep_t *ep)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, write_sz=%d, read_size=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd,
                 buf_write_sz(ep->send_buf),
                 buf_write_sz(ep->recv_buf));
     }

     A(ep->hash != 0L);
     A(ep->fd >= 0);
     A(ep->close != NULL);
     A(ep->read != NULL);
     A(ep->write != NULL);
     AZ(close(ep->fd));

     if (hash_hashed(&ep->hash_node)) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: fd=%d: removing from hashtable\n",
                      __func__,
                      ep->fd);
          }
          hash_del(&ep->hash_node);
     }

     ep_destroy(ep);
     free(ep);

     return 0;
}

static long unsigned int
hash(struct sockaddr_in *saddr, struct timespec *ts) {
     long unsigned int h = saddr->sin_addr.s_addr;
     h <<= 16;
     h |= saddr->sin_port;
     h <<= 16;
     h |= ((ts->tv_nsec &0x00000000ffff0000 >> 16));
     return h;
}
