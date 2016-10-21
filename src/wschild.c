#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "wstypes.h"
//#include "http.h"
//#include "ws.h"

#define MAX_EVENTS 256
#define BUF_SIZE 8192
#define DEFAULT_QUANTUM (5000)

#define ALERT(func, file, line)                                         \
     syslog(LOG_ALERT, "non-zero result: %s, %s:%s", func, file, line)
#define AZ(exp)                                                         \
     if (!((exp) == 0)) { ALERT(__func__, __FILE__, __LINE__); exit(1); }
#define AN(exp)                                                         \
     if (!((exp) != 0)) { ALERT(__func__, __FILE__, __LINE__); exit(1); }
#define A(exp)                                                          \
     if (!(exp)) { ALERT(__func__, __FILE__, __LINE__); exit(1); }
#define log_addr(msg, func, addr, fd)                                   \
     printf(msg, func, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd);

const wsd_config_t *wsd_cfg = NULL;
static int epfd = -1;

/* forward declarations */
static int on_accept(int fd);
static int on_read(wsconn_t *conn);
static int on_write(wsconn_t *conn);
static void cleanup(struct epoll_event *ev);
static void on_close(wsconn_t *conn);
static int io_loop();
static void sigterm(int sig);

int
wschild_main(const wsd_config_t *cfg)
{
     wsd_cfg = cfg;

     struct sigaction act;
     memset(&act, 0x0, sizeof(struct sigaction));
     act.sa_handler = sigterm;
     AZ(sigaction(SIGTERM, &act, NULL));

     AZ(listen(wsd_cfg->lfd, 5));

     epfd = epoll_create(1);
     A(epfd >= 0);

     return io_loop();
}

static int
io_loop()
{
     struct epoll_event evs[MAX_EVENTS];
     memset((void*)&evs, 0, sizeof(evs));

     struct epoll_event ev;
     memset((void*)&ev, 0, sizeof(ev));

     ev.events = EPOLLIN;
     ev.data.fd = wsd_cfg->lfd;
     AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, wsd_cfg->lfd, &ev));

     while (1) {
          int nfd = epoll_wait(epfd, evs, MAX_EVENTS, DEFAULT_QUANTUM);
          A(nfd >= 0);
          A(nfd <= MAX_EVENTS);

          int n = 0;
          for (; n < nfd; ++n) {
               if (evs[n].data.fd == wsd_cfg->lfd) {
                    AZ(on_accept(evs[n].data.fd));
               } else if (evs[n].events & EPOLLIN) {
                    int rv = on_read((wsconn_t*)evs[n].data.ptr);
                    if (0 >= rv) {
                         cleanup(&evs[n]);
                    }
               } else if (evs[n].events & EPOLLERR ||
                          evs[n].events & EPOLLHUP ||
                          evs[n].events & EPOLLRDHUP) {
                    cleanup(&evs[n]);
               }
          }
     }

     return 0;
}

static void
cleanup(struct epoll_event *ev)
{
     printf("wschild: %s: fd=%d\n", __func__,
            ((wsconn_t*)ev->data.ptr)->fd);

     AZ(close(((wsconn_t*)ev->data.ptr)->fd));
     free(ev->data.ptr);
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
     memset((void*)&ev, 0, sizeof(ev));

     ev.events = EPOLLIN | EPOLLRDHUP;
     ev.data.ptr = malloc(sizeof(wsconn_t));
     A(ev.data.ptr);
     memset((void*)ev.data.ptr, 0, sizeof(wsconn_t));
     ((wsconn_t*)ev.data.ptr)->fd = fd;

     AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev));

     if (wsd_cfg->verbose) {
          log_addr("wschild: %s: %s:%d, fd=%d\n", __func__, saddr, fd);
     }

     return 0;
}

static int
on_read(wsconn_t *conn)
{
/*     int len;
       len = read(conn->fd,
       buf_ref(conn->buf_in),
       buf_len(conn->buf_in));

       if (0>len)
       {
       if (errno==EAGAIN || errno==EWOULDBLOCK)
       return 1;

       return -1;
       }

       if (0==len)
       return 0;

       buf_fwd(conn->buf_in, len);
       if (0>conn->on_read(conn))
       return 0;*/

     char buf[1024];
     memset((void*)buf, 0, 1024);
     int rv = read(conn->fd, buf, 1023);
     printf("%s", buf);

     return rv;
}

static int
on_write(wsconn_t *conn)
{
/*     buf_flip(conn->buf_out);

       if (LOG_VVERBOSE<=wsd_cfg->verbose)
       printf("wschild: on_write: fd=%d: %d byte(s)\n",
       conn->fd, buf_len(conn->buf_out));

       int len;
       len=write(conn->fd,
       buf_ref(conn->buf_out),
       buf_len(conn->buf_out));

       if (0>len)
       {
       if (errno==EAGAIN || errno==EWOULDBLOCK)
       {
       buf_flip(conn->buf_out);
       return 1;
       }

       return -1;
       }

       if (0==len)
       return 0;

       buf_fwd(conn->buf_out, len);
       buf_compact(conn->buf_out);
       if (0==buf_pos(conn->buf_out))
       {*/
     /* buffer empty, nothing else to write */
/*          conn->pfd->events&=(~POLLOUT);

            if (conn->close_on_write)
            {
            if (LOG_VVERBOSE<=wsd_cfg->verbose)
            printf("wschild: on_write: fd=%d: close-on-write true and buffer empty\n",
            conn->pfd->fd);

            return 0;
            }
            }*/

     return 1;
}

static void
sigterm(int sig)
{
/*  int num_use=0;
    int i;
    for (i=0; i < MAX_DESC; i++)
    {
    if (pfd_is_in_use(i))
    {
    on_close(&conn[i]);
    free_conn_and_pfd(i);
    num_use++;
    }
    }
*/
//  syslog(LOG_INFO, "caught signal, terminating %d client(s)", num_use);
}

static void
on_close(wsconn_t *conn)
{
/*  if (wsd_cfg->verbose)
    printf("wschild: on_close: fd=%d\n", conn->pfd->fd);

    if (conn->on_close)
    conn->on_close(conn);*/
}

int wschild_register_user_fd(int fd,
                             int (*on_read)(struct wsconn *conn),
                             int (*on_write)(struct wsconn *conn),
                             short events)
{
     return 0;
}

struct wsconn* wschild_lookup_kernel_fd(int fd)
{
     return NULL;
}
