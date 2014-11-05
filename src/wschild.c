#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <assert.h>
#include "wstypes.h"
#include "http.h"
#include "ws.h"

/* assumes max poll file descriptors equals max connections */
#define MAX_DESC 32
#define MAX_CONN MAX_DESC
#define BUF_SIZE 8192

#define log_addr(msg, addr, fd, slot)                                   \
  printf(msg, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, slot);

static int num_pfd=0;
static struct pollfd pfd[MAX_DESC];
static wsconn_t conn[MAX_CONN];

const wsd_config_t *wsd_cfg=NULL;

/* forward declarations */
static int on_accept(int fd);
static int on_read(wsconn_t *conn);
static int on_write(wsconn_t *conn);
static void on_close(wsconn_t *conn);
static int io_loop();
static void sighup(int sig);
static void free_conn_and_pfd(const int slot);

static inline int
conn_init()
{
  memset(&conn, 0x0, sizeof(conn));
  int i;
  for (i=0; i<MAX_CONN; i++)
    if (NULL==(conn[i].buf_in=buf_alloc(BUF_SIZE))
        || NULL==(conn[i].buf_out=buf_alloc(BUF_SIZE)))
      return -1;

  return 0;
}

static inline int
conn_alloc(int slot, struct pollfd *pfd)
{
  if (slot>=MAX_CONN)
    return -1;

  conn[slot].pfd=pfd;
  conn[slot].on_read=http_on_read;
  conn[slot].on_write=http_on_write;
  return 0;
}

static inline int
conn_free(int slot)
{
  if (slot>=MAX_CONN)
    return -1;

  buf_free(conn[slot].buf_in);
  buf_free(conn[slot].buf_out);

  for (;slot<(MAX_CONN-1); slot++)
    {
      conn[slot].pfd=conn[slot+1].pfd;
      conn[slot].buf_in=conn[slot+1].buf_in;
      conn[slot].buf_out=conn[slot+1].buf_out;
      conn[slot].on_read=conn[slot+1].on_read;
      conn[slot].on_write=conn[slot+1].on_write;
      conn[slot].on_data_frame=conn[slot+1].on_data_frame;
      conn[slot].on_close=conn[slot+1].on_close;
      conn[slot].close_on_write=conn[slot+1].close_on_write;
      conn[slot].closing=conn[slot+1].closing;
      conn[slot].location=conn[slot+1].location;
    }

  conn[slot].on_read=NULL;
  conn[slot].on_write=NULL;
  conn[slot].on_data_frame=NULL;
  conn[slot].on_close=NULL;
  conn[slot].pfd=NULL;
  buf_clear(conn[slot].buf_in);
  buf_clear(conn[slot].buf_out);
  conn[slot].close_on_write=0;
  conn[slot].closing=0;
  conn[slot].location=NULL;

  return 0;
}

#define pfd_get(slot) (pfd[slot])

static inline void
pfd_init()
{
  memset(&pfd, 0x0, sizeof(pfd));
  int i;
  for (i=0; i<MAX_DESC; i++)
    pfd[i].fd=UNASSIGNED;
}

static inline int
pfd_free(int slot)
{
  if (slot>=MAX_DESC)
    return -1;

  for (;slot<(MAX_DESC-1); slot++)
    {
      pfd[slot].fd=pfd[slot+1].fd;
      pfd[slot].events=pfd[slot+1].events;
      pfd[slot].revents=pfd[slot+1].revents;
    }

  pfd[slot].fd=UNASSIGNED;
  pfd[slot].events=0;
  pfd[slot].revents=0;
  num_pfd--;
  return 0;
}

static inline int
pfd_alloc(int slot, int fd, short events)
{
  if (slot>=MAX_DESC)
    return -1;

  pfd[slot].fd=fd;
  pfd[slot].events=events;
  num_pfd++;
  return 0;
}

static inline int
pfd_find_free_slot()
{
  int i=0;
  while (i<MAX_DESC
         && pfd[i].fd>=0)
    i++;

  return (i==MAX_DESC?-1:i);
}

#define pfd_is_in_use(slot)                     \
  (pfd[slot].fd>=0?1:0)

int
wschild_main(const wsd_config_t *cfg)
{
  wsd_cfg=cfg;

  if (0>listen(wsd_cfg->sock, 5))
    {
      perror("listen");
      exit(1);
    }

  pfd_init();
  if (0>conn_init())
    {
      perror("conn_init");
      exit(1);
    }

  int slot;
  slot=pfd_find_free_slot();
  pfd_alloc(slot, wsd_cfg->sock, POLLIN);
  conn_alloc(slot, &pfd_get(slot));

  struct sigaction act;
  memset(&act, 0x0, sizeof(struct sigaction));
  act.sa_handler=sighup;
  if (0>sigaction(SIGHUP, &act, NULL))
    {
      perror("sigaction");
      exit(1);
    }

  return io_loop();
}

static int
on_accept(int fd)
{
  int slot;
  slot=pfd_find_free_slot();

  struct sockaddr_in cl;
  memset(&cl, 0x0, sizeof(cl));

  int s;
  socklen_t cl_len=sizeof(struct sockaddr_in);
  if (0>(s=accept(fd, (struct sockaddr *)&cl, &cl_len)))
    return -1;

  if (0>slot)
    {
      close(s);
      if (wsd_cfg->verbose)
        log_addr("on_accept: %s:%d: fd=%d: slot=%d: no fee slot, closing\n",
                 cl, s, slot);
    }
  else
    {
      pfd_alloc(slot, s, POLLIN);
      conn_alloc(slot, &pfd_get(slot));
      if (wsd_cfg->verbose)
        log_addr("on_accept: %s:%d: fd=%d: slot=%d\n", cl, s, slot);
    }

  return 0;
}

static int
on_write(wsconn_t *conn)
{
  buf_flip(conn->buf_out);

  if (LOG_VVERBOSE<=wsd_cfg->verbose)
    printf("on_write: fd=%d: %d byte(s)\n",
           conn->pfd->fd, buf_len(conn->buf_out));

  int len;
  len=write(conn->pfd->fd,
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
    {
      /* buffer empty, nothing else to write */
      conn->pfd->events&=(~POLLOUT);

      if (conn->close_on_write)
        {
          if (LOG_VVERBOSE<=wsd_cfg->verbose)
            printf("on_write: fd=%d: close-on-write true and buffer empty\n",
                   conn->pfd->fd);

          return 0;
        }
    }

  return 1;
}

static int
on_read(wsconn_t *conn)
{
  int len;
  len=read(conn->pfd->fd,
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
    return 0;

  return 1;
}

static int
io_loop()
{
  int rv;
  while (num_pfd)
    {
      rv=poll(pfd, num_pfd, 5000);
      if (0<rv)
        {
          int num_sel=rv;
          int i;
          for (i=0; i<MAX_DESC; i++)
            {
              if (POLLIN&pfd[i].revents)
                {
                  /* server socket: only ever accept */
                  if (pfd[i].fd==wsd_cfg->sock)
                    {
                      rv=on_accept(pfd[i].fd);
                      if (0>rv)
                        {
                          perror("on_accept");
                          free_conn_and_pfd(i);
                        }
                    }
                  else
                    {
                      rv=on_read(&conn[i]);
                      if (0>=rv)
                        {
                          if (rv>0)
                            perror("on_read");

                          on_close(&conn[i]);
                          free_conn_and_pfd(i);
                        }
                    }
                }

              if (POLLOUT&pfd[i].revents)
                {
                  rv=on_write(&conn[i]);
                  if (0>=rv)
                    {
                      if (rv>0)
                        perror("on_write");

                      on_close(&conn[i]);
                      free_conn_and_pfd(i);
                    }
                }

              if ((POLLHUP|POLLERR)&pfd[i].revents)
                {
                  on_close(&conn[i]);
                  free_conn_and_pfd(i);
                }

              if (0!=pfd[i].revents)
                num_sel--;
              if (!num_sel)
                break;
            }
        }
    }

  return 0;
}

static void
sighup(int sig)
{
  int i;
  for (i=0; i<MAX_DESC; i++)
    {
      if (pfd_is_in_use(i))
        {
          on_close(&conn[i]);
          free_conn_and_pfd(i);
        }
    }
}

static void
free_conn_and_pfd(const int slot)
{
  if (LOG_VVERBOSE<=wsd_cfg->verbose)
    printf("free_conn_and_pfd: fd=%d: slot=%d\n", pfd_get(slot).fd, slot);

  if (0>close(pfd_get(slot).fd))
    perror("close");

  pfd_free(slot);
  conn_free(slot);
}

static
void on_close(wsconn_t *conn)
{
  if (wsd_cfg->verbose)
    printf("on_close: fd=%d\n", conn->pfd->fd);

  if (conn->on_close)
    conn->on_close(conn);
}
