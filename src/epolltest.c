#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
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

#define MAX_EVENTS 256
#define BUF_SIZE 8192
#define DEFAULT_QUANTUM 5000

#define ERRET(exp, text)                        \
     if (exp) { perror(text); return (-1); }

struct glue {
     int fd;
     int (*on_read)(struct glue*);
     int (*on_write)(struct glue*);
     int (*on_close)(struct glue*);
     buf_t *buf_in;
     buf_t *buf_out;
     struct glue *other;
};

struct endpoint {
     char snd_buf[BUF_SIZE];
     char rcv_buf[BUF_SIZE];
     int spos;
     int rpos;
     int fd;
     int (*read)(ep_t *ep);
     int (*write)(ep_t *ep);
     int (*close)(ep_t *ep);
};
typedef struct endpoint ep_t;

static const char *snd_pathname = "/tmp/pipe-inbound";
static const char *rcv_pathname = "/tmp/pipe-outbound";

static int epfd;
static ep_t *snd_pipe;
static ep_t *rcv_pipe;

static int sock_on_read(struct glue *eg);
static int sock_on_write(struct glue *eg);
static int sock_on_close(struct glue *eg);
static int pipe_write(struct glue *eg);
static int pipe_read(struct glue *eg);
static int pipe_close(struct glue *eg);
static int on_accept(int lfd);

int main(int argc, char **argv)
{
     struct sigaction sac;
     memset(&sac, 0, sizeof(sac));
     sac.sa_handler = SIG_IGN;
     sac.sa_flags = SA_RESTART;
     AZ(sigaction(SIGPIPE, &sac, NULL));

     snd_pipe = malloc(sizeof(ep_t));
     A(snd_pipe);
     memset(snd_pipe, 0, sizeof(ep_t));
     snd_pipe->fd = -1;
     snd_pipe->write = pipe_write;
     snd_pipe->close = pipe_close;

     rcv_pipe = malloc(sizeof(ep_t));
     A(rcv_pipe);
     memset(rcv_pipe, 0, sizeof(ep_t));
     rcv_pipe->fd = -1;
     rcv_pipe->read = pipe_read;
     rcv_pipe->close = pipe_close;

     int lfd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
     A(lfd >= 0);

     int opt = 1;
     AZ(setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)));

     struct sockaddr_in addr;
     memset(&addr, 0x0, sizeof(addr));
     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = INADDR_ANY;
     addr.sin_port = htons(3000);
     AZ(bind(lfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)));

     epfd = epoll_create(1);
     A(epfd >= 0);

     AZ(listen(lfd, 5));

     struct epoll_event evs[MAX_EVENTS];
     memset((void*)&evs, 0, sizeof(evs));

     struct epoll_event ev;
     memset((void*)&ev, 0, sizeof(ev));

     ev.events = EPOLLIN;
     ev.data.fd = lfd;
     AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev));

     while (1) {
          int nfd = epoll_wait(epfd, evs, MAX_EVENTS, DEFAULT_QUANTUM);
          A(nfd >= 0);
          A(nfd <= MAX_EVENTS);

          printf("*** %s: nfd=%d\n", __func__, nfd);

          int n;
          for (n = 0; n < nfd; ++n) {

               if (evs[n].data.fd == lfd) {
                    AZ(on_accept(evs[n].data.fd));
                    continue;
               }
               
               struct glue *eg = (struct glue*)evs[n].data.ptr;
               if (evs[n].events & EPOLLIN) {
                    int rv = eg->on_read(eg);
                    if (0 > rv)
                         AZ(eg->on_close(eg));
               } else if (evs[n].events & EPOLLOUT) {
                    int rv = eg->on_write(eg);
                    if (0 > rv)
                         AZ(eg->on_close(eg));
               } else if (evs[n].events & EPOLLHUP ||
                          evs[n].events & EPOLLRDHUP) {
                    AZ(eg->on_close(eg));
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

     printf("*** %s: eg->fd=%d\n", __func__, fd);

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = EPOLLIN | EPOLLRDHUP;
     ev.data.ptr = malloc(sizeof(struct glue));
     A(ev.data.ptr);
     memset(ev.data.ptr, 0, sizeof(struct glue));
     struct glue *eg = (struct glue*)ev.data.ptr;
     eg->fd = fd;
     eg->on_read = sock_on_read;
     eg->on_write = sock_on_write;
     eg->on_close = sock_on_close;
     eg->buf_in = buf_alloc(BUF_SIZE);
     eg->buf_out = buf_alloc(BUF_SIZE);
     AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev));

     return 0;
}

static int
pipe_read(ep_t *ep)
{
     printf("*** %s: ep->fd=%d\n", __func__, ep->fd);

     int len = read(ep->fd, ep->rcv_buf, sizeof ep->rcv_buf);
     printf("\t%s: len=%d\n", __func__, len);
     ERRET(0 > len, "read");

     /* EOF */
     if (0 == len)
          return (-1);

     ep->rpos = len;

     while (buf_len(//TODO continue
     buf_put_buf(eg->other->buf_out, eg->buf_in);
     buf_flip(eg->other->buf_out);

     buf_compact(eg->buf_in);

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
     ev.data.ptr = eg->other;

     AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, eg->other->fd, &ev));

     return 0;
}

static int
sock_on_read(struct glue *eg)
{
     printf("*** %s: eg->fd=%d\n", __func__, eg->fd);

     int len = read(eg->fd, buf_ref(eg->buf_in), buf_len(eg->buf_in));
     ERRET(0 > len, "read");

     /* EOF */
     if (0 == len)
          return (-1);

     printf("\t%s: read %d byte(s)\n", __func__, len);

     buf_fwd(eg->buf_in, len);
     buf_flip(eg->buf_in);

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

          while (snd_pipe->spos < sizeof(snd_pipe->snd_buf))
               snd_pipe->snd_buf[snd_pipe->spos++] = buf_get(eg->buf_in);

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLOUT | EPOLLRDHUP;
          ev.data.ptr = snd_pipe;
          AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, snd_pipe->fd, &ev));
     }

     if (-1 == rcv_pipe->fd) {
          int rv = mkfifo(rcv_pathname, 0600);
          ERRET(0 > rv && errno != EEXIST);

          rcv_pipe->fd = open(rcv_pathname, O_RDONLY | O_NONBLOCK);
          A(rcv_pipe->fd >= 0);

          printf("\t%s: opened %s\n", __func__, rcv_pathname);

          struct epoll_event ev;
          memset(&ev2, 0, sizeof(ev));
          ev.events = EPOLLIN;
          ev.data.ptr = rcv_pipe;
          AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, rcv_pipe->fd, &ev));
     } else {
          while (snd_pipe->spos < sizeof(snd_pipe->snd_buf))
               snd_pipe->snd_buf[snd_pipe->spos++] = buf_get(eg->buf_in);

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLOUT | EPOLLRDHUP;
          ev.data.ptr = snd_pipe;
          AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, snd_pipe->fd, &ev));
     }

     buf_compact(eg->buf_in);

     return 0;
}

static int
sock_on_write(struct glue *eg)
{
     printf("*** %s: eg->fd=%d\n", __func__, eg->fd);

     A(eg->fd >= 0);
     A(eg->buf_out);
     A(buf_len(eg->buf_out) > 0);
     int len = write(eg->fd, buf_ref(eg->buf_out), buf_len(eg->buf_out));
     if (0 < len) {
          printf("\t%s: wrote %d byte(s)\n", __func__, len);

          buf_fwd(eg->buf_out, len);
          buf_compact(eg->buf_out);
          A(BUF_SIZE == buf_len(eg->buf_out));

          if (BUF_SIZE == buf_len(eg->buf_out)) {
               struct epoll_event ev;
               memset(&ev, 0, sizeof(ev));
               ev.events = EPOLLIN | EPOLLRDHUP;
               ev.data.ptr = eg;
               AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, eg->fd, &ev));

               printf("\t%s: removing EPOLLOUT: fd=%d\n",
                      __func__,
                      eg->fd);
          }
     } else {
          A(0 > len);
     }

     return len;
}

static int sock_on_close(struct glue *eg)
{
     printf("*** %s: eg->fd=%d\n", __func__, eg->fd);

     A(eg->fd >= 0);
     A(eg->on_read != NULL);
     A(eg->on_write != NULL);
     A(eg->buf_in != NULL);

     if (0 < buf_len(eg->buf_in))
          printf("\t%s: lost %d byte(s) in buf_in\n",
                 __func__,
                 buf_len(eg->buf_in));

     A(eg->buf_out != NULL);

     if (0 < buf_len(eg->buf_out))
          printf("\t%s: lost %d byte(s) in buf_out\n",
                 __func__,
                 buf_len(eg->buf_out));

     AZ(close(eg->fd));
     buf_free(eg->buf_in);
     buf_free(eg->buf_out);

     if (eg->other)
          AZ(eg->other->on_close(eg->other));

     free(eg);

     return 0;
}

static int
pipe_write(ep_t *ep)
{
     printf("*** %s: ep->fd=%d\n", __func__, ep->fd);

     A(ep->fd >= 0);
     A(ep->snd_buf);
     A(ep->spos > 0);
     int len = write(ep->fd, ep->snd_buf, ep->spos);
     if (0 < len) {
          printf("\t%s: wrote %d byte(s)\n", __func__, len);

          A(len == ep->spos);
          ep->spos = 0;

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLRDHUP;
          ev.data.ptr = ep;
          AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, ep->fd, &ev));

          printf("\t%s: removing EPOLLOUT: fd=%d\n",
                 __func__,
                 ep->fd);
          }
     } else {
          A(0 > len);
     }

     return len;
}

static int
pipe_close(struct glue *eg)
{
     printf("*** %s: eg->fd=%d\n", __func__, eg->fd);

     A(eg->fd >= 0);
     A(eg->other);
     A(eg->on_read == NULL ? eg->on_write != NULL : eg->on_write == NULL);
     A(eg->buf_in == NULL ? eg->buf_out != NULL : eg->buf_in != NULL);

     if (eg->buf_in) {
          if (0 < buf_len(eg->buf_in))
               printf("\t%s: lost %d byte(s) in buf_in\n",
                      __func__,
                      buf_len(eg->buf_in));

          buf_free(eg->buf_in);
     }

     if (eg->buf_out) {
          if (0 < buf_len(eg->buf_out))
               printf("\t%s: lost %d byte(s) in buf_out\n",
                      __func__,
                      buf_len(eg->buf_out));

          buf_free(eg->buf_out);
     }

     AZ(close(eg->fd));
     free(eg);

     return 0;
}
