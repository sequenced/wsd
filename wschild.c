#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
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

#define UNASSIGNED (-1)
/* assumes max poll file descriptors equals max connections */
#define MAX_DESC 32
#define MAX_CONN MAX_DESC
#define BUF_SIZE 8192
#define REPLY "<foobar>blah</foobar>"

#define log_addr(msg, addr)                                     \
  printf(msg, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

static int num_pfd=0;
static struct pollfd pfd[MAX_DESC];
static wschild_conn_t conn[MAX_CONN];
static const wsd_config_t *wsd_cfg=NULL;

/* forward declarations */
static int on_accept(int fd);
static int on_read(wschild_conn_t *conn);
static int on_write(wschild_conn_t *conn);
static int io_loop();
static void sighup(int sig);

static inline int
buf_init(buf_t *buf, int size)
{
  buf->swap=UNASSIGNED;
  buf->capacity=size;
  buf->limit=buf->capacity;
  buf->p=malloc(buf->capacity);
  if (NULL==buf->p)
    return -1;
  return 0;
}

#define buf_clear(buf)                                          \
  buf.pos=0; buf.swap=UNASSIGNED; buf.limit=buf.capacity;
#define buf_get(buf) (buf.p+buf.pos)
#define buf_len(buf) (buf.limit-buf.pos)
#define buf_put(buf, len) (buf.pos+=len)
#define buf_flip(buf)                                 \
  if (UNASSIGNED==buf.swap)                           \
    {                                                 \
      buf.swap=*(char*)(buf.p+buf.pos);               \
      *(buf.p+buf.pos)='\0';                          \
      buf.limit=buf.pos;                              \
      buf.pos=0;                                      \
    }                                                 \
  else                                                \
    {                                                 \
      buf.pos=buf.limit;                              \
      buf.limit=buf.capacity;                         \
      *(buf.p+buf.pos)=(char)buf.swap;                \
      buf.swap=UNASSIGNED;                            \
    }

static inline int
conn_init()
{
  memset(&conn, 0x0, sizeof(conn));
  int i;
  for (i=0; i<MAX_CONN; i++)
    if (0>buf_init(&conn[i].buf, BUF_SIZE))
      return -1;
  return 0;
}

static inline int
conn_set(int slot, struct pollfd *pfd)
{
  if (slot>=MAX_CONN)
    return -1;

  conn[slot].pfd=pfd;
  conn[slot].proto=WSCHILD_PROTO_HTTP;
  return 0;
}

static inline int
conn_clear(int slot)
{
  if (slot>=MAX_CONN)
    return -1;

  for (;slot<(MAX_CONN-1); slot++)
    {
      conn[slot].pfd=conn[slot+1].pfd;
      conn[slot].buf=conn[slot+1].buf;
      conn[slot].proto=conn[slot+1].proto;
    }

  conn[slot].proto=WSCHILD_PROTO_HTTP;
  conn[slot].pfd=NULL;
  buf_clear(conn[slot].buf);
  
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
pfd_clear(int slot)
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
pfd_set(int slot, int fd, short events)
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
  (pfd[i].fd>=0?1:0)

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
  pfd_set(slot, wsd_cfg->sock, POLLIN);
  conn_set(slot, &pfd_get(slot));

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
        log_addr("no slots: closing: %s:%d\n", cl);
    }
  else
    {
      pfd_set(slot, s, POLLIN);
      conn_set(slot, &pfd_get(slot));
      if (wsd_cfg->verbose)
        log_addr("accepting: %s:%d\n", cl);
    }

  return 0;
}

static int
on_write(wschild_conn_t *conn)
{
  return -1;
}

static int
on_read(wschild_conn_t *conn)
{
  int len;
  len=read(conn->pfd->fd,
           buf_get(conn->buf),
           buf_len(conn->buf));

  if (0>len)
    return -1;

  if (0==len)
    return 0;

  buf_put(conn->buf, len);

  int rv;
  if (conn->proto&WSCHILD_PROTO_HTTP)
    rv=on_http_data(conn);
  else if (conn->proto&WSCHILD_PROTO_WS)
    rv=on_ws_data(conn);
  else
    rv=(-1);

  return rv;
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
                          close(pfd[i].fd);
                          pfd_clear(i);
                          conn_clear(i);
                        }
                    }
                  else
                    {
                      rv=on_read(&conn[i]);
                      if (0>=rv)
                        {
                          if (rv>0)
                            perror("on_read");
                          close(pfd[i].fd);
                          pfd_clear(i);
                          conn_clear(i);
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
                      close(pfd[i].fd);
                      pfd_clear(i);
                      conn_clear(i);
                    }
                }

              if ((POLLHUP|POLLERR)&pfd[i].revents)
                {
                  close(pfd[i].fd);
                  pfd_clear(i);
                  conn_clear(i);
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
          close(pfd_get(i).fd);
          pfd_clear(i);
          conn_clear(i);
        }
    }
}
