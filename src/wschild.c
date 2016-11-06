#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
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

#define MAX_EVENTS 256
#define DEFAULT_QUANTUM 5000

#define ERRET(exp, text)                        \
     if (exp) { perror(text); return (-1); }

#define buf_read_sz(buf) (buf.wrpos - buf.rdpos)
#define buf_write_sz(buf) ((unsigned int)sizeof(buf.p) - buf.wrpos)
#define buf_reset(buf) buf.wrpos = 0; buf.rdpos = 0;
#define buf_put(buf, obj)                       \
     *(typeof(obj)*)(&buf.p[buf.wrpos]) = obj;  \
     buf.wrpos += sizeof(typeof(obj));
#define buf_get(buf, obj)                       \
     obj = *(typeof(obj)*)(&buf.p[buf.rdpos]);  \
     buf.rdpos += sizeof(typeof(obj));

const wsd_config_t *wsd_cfg = NULL;

static const char *snd_pathname = "/tmp/pipe-inbound";
static const char *rcv_pathname = "/tmp/pipe-outbound";

static DEFINE_HASHTABLE(ep_hash, 4);
static int epfd;
static ep_t *snd_pipe;
static ep_t *rcv_pipe;

static int sock_read(ep_t *ep);
static int sock_write(ep_t *ep);
static int sock_close(ep_t *ep);
static int pipe_write(ep_t *ep);
static int pipe_read(ep_t *ep);
static int pipe_close(ep_t *ep);
static int on_accept(int lfd);
static void init_snd_pipe();
static void init_rcv_pipe();
static long unsigned int hash(struct sockaddr_in *saddr, struct timespec *ts);
static int io_loop();
static void sigterm(int sig);

int
wschild_main(const wsd_config_t *cfg)
{
     wsd_cfg = cfg;

     struct sigaction sac;
     memset(&sac, 0x0, sizeof(struct sigaction));
     sac.sa_handler = sigterm;
     AZ(sigaction(SIGTERM, &sac, NULL));

     memset(&sac, 0, sizeof(sac));
     sac.sa_handler = SIG_IGN;
     sac.sa_flags = SA_RESTART;
     AZ(sigaction(SIGPIPE, &sac, NULL));

     snd_pipe = malloc(sizeof(ep_t));
     A(snd_pipe);
     init_snd_pipe();

     rcv_pipe = malloc(sizeof(ep_t));
     A(rcv_pipe);
     init_rcv_pipe();

     hash_init(ep_hash);
     printf("*** %s: endpoint hashtable has %lu entries\n",
            __func__,
            sizeof ep_hash);

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

static long unsigned int
hash(struct sockaddr_in *saddr, struct timespec *ts) {
     long unsigned int h = saddr->sin_addr.s_addr;
     h <<= 16;
     h |= saddr->sin_port;
     h <<= 16;
     h |= ((ts->tv_nsec &0x00000000ffff0000 >> 16));
     return h;
}

static void
init_snd_pipe() {
     memset(snd_pipe, 0, sizeof(ep_t));
     snd_pipe->fd = -1;
     snd_pipe->write = pipe_write;
     snd_pipe->close = pipe_close;
}

static void
init_rcv_pipe() {
     memset(rcv_pipe, 0, sizeof(ep_t));
     rcv_pipe->fd = -1;
     rcv_pipe->read = pipe_read;
     rcv_pipe->close = pipe_close;
}

static void
sigterm(int sig)
{
     /* TODO */
}

static int
io_loop() {
     struct epoll_event evs[MAX_EVENTS];
     memset((void*)&evs, 0, sizeof(evs));

     while (1) {
          int nfd = epoll_wait(epfd, evs, MAX_EVENTS, DEFAULT_QUANTUM);
          A(nfd >= 0);
          A(nfd <= MAX_EVENTS);

          int n;
          for (n = 0; n < nfd; ++n) {

               if (evs[n].data.fd == wsd_cfg->lfd) {
                    AZ(on_accept(evs[n].data.fd));
                    continue;
               }

               printf("*** %s: nfd=%d, events=0x%x\n",
                      __func__,
                      nfd,
                      evs[n].events);

               ep_t *ep = (ep_t*)evs[n].data.ptr;
               if (evs[n].events & EPOLLIN ||
                   evs[n].events & EPOLLPRI) {
                    int rv = ep->read(ep);
                    if (0 > rv)
                         AZ(ep->close(ep));
               } else if (evs[n].events & EPOLLOUT) {
                    int rv = ep->write(ep);
                    if (0 > rv)
                         AZ(ep->close(ep));
               } else if (evs[n].events & EPOLLHUP ||
                          evs[n].events & EPOLLRDHUP ||
                          evs[n].events & EPOLLERR) {
                    AZ(ep->close(ep));
               }
          }
     }

     return 0;
}

static int
on_accept(int lfd)
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

     memset(ev.data.ptr, 0, sizeof(ep_t));
     ep_t *ep = (ep_t*)ev.data.ptr;
     ep->fd = fd;
     ep->read = sock_read;
     ep->write = sock_write;
     ep->close = sock_close;

     struct timespec ts;
     memset((void*)&ts, 0, sizeof(ts));
     AZ(clock_gettime(CLOCK_REALTIME_COARSE, &ts));
     ep->hash = hash(&saddr, &ts);
     AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev));

     printf("*** %s: eg->fd=%d, hash=0x%lx\n",
            __func__,
            fd,
            ep->hash);

     return 0;
}

static int
pipe_read(ep_t *ep)
{
     printf("*** %s: ep->fd=%d\n", __func__, ep->fd);

     AZ(ep->rcv_buf.wrpos);
     int len = read(ep->fd, ep->rcv_buf.p, buf_write_sz(ep->rcv_buf));
     printf("\t%s: len=%d\n", __func__, len);
     ERRET(0 > len, "read");

     /* EOF */
     if (0 == len)
          return (-1);

     ep->rcv_buf.wrpos += len;

     long unsigned int hash;
     buf_get(ep->rcv_buf, hash);
     A(hash != 0);
     ep_t *sock = NULL;
     hash_for_each_possible(ep_hash, sock, hash_node, hash) {
          if (sock->hash == hash)
               break;
     }

     if (NULL == sock) {
          printf("\tsocket went way: dropping %d byte(s)\n",
                 buf_read_sz(ep->rcv_buf));
          /* Socket went away; drop data on the floor */
          buf_reset(ep->rcv_buf);

          return 0;
     }

     while (buf_write_sz(sock->snd_buf) && buf_read_sz(ep->rcv_buf))
          sock->snd_buf.p[sock->snd_buf.wrpos++] =
               ep->rcv_buf.p[ep->rcv_buf.rdpos++];
     AZ(buf_read_sz(ep->rcv_buf));
     buf_reset(ep->rcv_buf);

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
     ev.data.ptr = sock;

     AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, sock->fd, &ev));

     return 0;
}

static int
sock_read(ep_t *ep)
{
     printf("*** %s: ep->fd=%d\n", __func__, ep->fd);

     AZ(ep->rcv_buf.wrpos);
     int len = read(ep->fd, ep->rcv_buf.p, buf_write_sz(ep->rcv_buf));
     ERRET(0 > len, "read");

     /* EOF */
     if (0 == len)
          return (-1);

     printf("\t%s: read %d byte(s)\n", __func__, len);

     ep->rcv_buf.wrpos += len;

     if (-1 == snd_pipe->fd) {
          int rv = mkfifo(snd_pathname, 0600);
          ERRET(0 > rv && errno != EEXIST, "mkfifo");

          snd_pipe->fd = open(snd_pathname, O_WRONLY | O_NONBLOCK);
          if (0 > snd_pipe->fd) {
               // see open(2) return values
               A(errno == ENODEV || errno == ENXIO);
               return (-1);
          }

          printf("\t%s: opened %s\n", __func__, snd_pathname);

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLOUT | EPOLLRDHUP;
          ev.data.ptr = snd_pipe;
          AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, snd_pipe->fd, &ev));
     }

     if (-1 == rcv_pipe->fd) {
          int rv = mkfifo(rcv_pathname, 0600);
          ERRET(0 > rv && errno != EEXIST, "mkfifo");

          rcv_pipe->fd = open(rcv_pathname, O_RDONLY | O_NONBLOCK);
          A(rcv_pipe->fd >= 0);

          printf("\t%s: opened %s\n", __func__, rcv_pathname);

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLIN;
          ev.data.ptr = rcv_pipe;
          AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, rcv_pipe->fd, &ev));
     }

     // TODO plug in protocol handler
     A(buf_write_sz(snd_pipe->snd_buf) > sizeof(long unsigned int));
     buf_put(snd_pipe->snd_buf, ep->hash);

     while (buf_write_sz(snd_pipe->snd_buf) && buf_read_sz(ep->rcv_buf))
          snd_pipe->snd_buf.p[snd_pipe->snd_buf.wrpos++] =
               ep->rcv_buf.p[ep->rcv_buf.rdpos++];

     AZ(buf_read_sz(ep->rcv_buf));
     buf_reset(ep->rcv_buf);

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = EPOLLOUT | EPOLLRDHUP;
     ev.data.ptr = snd_pipe;
     AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, snd_pipe->fd, &ev));

     hash_add(ep_hash, &ep->hash_node, ep->hash);

     return 0;
}

static int
sock_write(ep_t *ep)
{
     printf("*** %s: ep->fd=%d, ep->snd_buf.rdpos=%d\n",
            __func__,
            ep->fd,
            ep->snd_buf.rdpos);

     A(ep->fd >= 0);
     A(buf_read_sz(ep->snd_buf) > 0);
     int len = write(ep->fd, ep->snd_buf.p, buf_read_sz(ep->snd_buf));
     if (0 < len) {
          printf("\t%s: wrote %d byte(s)\n", __func__, len);

          ep->snd_buf.rdpos += len;
          AZ(buf_read_sz(ep->snd_buf));
          buf_reset(ep->snd_buf);

          if (0 == ep->snd_buf.rdpos) {
               struct epoll_event ev;
               memset(&ev, 0, sizeof(ev));
               ev.events = EPOLLIN | EPOLLRDHUP;
               ev.data.ptr = ep;
               AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, ep->fd, &ev));

               printf("\t%s: removing EPOLLOUT: ep->fd=%d\n",
                      __func__,
                      ep->fd);
          }
     } else {
          A(0 > len);
     }

     return len;
}

static int sock_close(ep_t *ep)
{
     printf("*** %s: ep->fd=%d, read_sz=%u, write_sz=%u\n",
            __func__,
            ep->fd,
            buf_read_sz(ep->rcv_buf),
            buf_write_sz(ep->snd_buf));

     A(ep->hash != 0L);
     A(ep->fd >= 0);
     A(ep->close != NULL);
     A(ep->read != NULL);
     A(ep->write != NULL);
     AZ(close(ep->fd));

     if (hash_hashed(&ep->hash_node)) {
          printf("\t %s: removing ep->fd=%d from hashtable\n",
                 __func__,
                 ep->fd);
          hash_del(&ep->hash_node);
     }

     free(ep);

     return 0;
}

static int
pipe_write(ep_t *ep)
{
     printf("*** %s: ep->fd=%d\n", __func__, ep->fd);

     A(ep->fd >= 0);
     A(ep->snd_buf.p);
     A(buf_read_sz(ep->snd_buf) > 0);
     int len = write(ep->fd, ep->snd_buf.p, buf_read_sz(ep->snd_buf));
     if (0 < len) {
          printf("\t%s: wrote %d byte(s)\n", __func__, len);

          ep->snd_buf.rdpos += len;
          AZ(buf_read_sz(ep->snd_buf));
          buf_reset(ep->snd_buf);

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLRDHUP;
          ev.data.ptr = ep;
          AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, ep->fd, &ev));

          printf("\t%s: removing EPOLLOUT: fd=%d\n",
                 __func__,
                 ep->fd);
     } else {
          A(0 > len);
     }

     return len;
}

static int
pipe_close(ep_t *ep)
{
     printf("*** %s: ep->fd=%d\n", __func__, ep->fd);

     A(ep->fd >= 0);
     A(ep->read == NULL ? ep->write != NULL : ep->write == NULL);
     A(ep->rcv_buf.p == NULL ? ep->snd_buf.p != NULL : ep->rcv_buf.p != NULL);

     AZ(close(ep->fd));

     if (snd_pipe == ep)
          init_snd_pipe();
     else if (rcv_pipe == ep)
          init_rcv_pipe();

     return 0;
}
