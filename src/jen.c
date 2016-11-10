#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include "wstypes.h"
#include "hashtable.h"
#include "jen.h"

extern const wsd_config_t *wsd_cfg;
extern DECLARE_HASHTABLE(ep_hash, 4);
extern int epfd;

static const char *snd_pathname = "/tmp/pipe-inbound";
static const char *rcv_pathname = "/tmp/pipe-outbound";

static ep_t *snd_pipe = NULL;
static ep_t *rcv_pipe = NULL;

static int pipe_write(ep_t *ep);
static int pipe_read(ep_t *ep);
static int pipe_close(ep_t *ep);
static void init_snd_pipe();
static void init_rcv_pipe();

/*
 * JSON rpc frame layout:
 *
 * 0th byte           8th byte          ...
 * +------------------+------------------+
 * | sender reference | JSON rpc payload |
 * +------------------+------------------+
 *
 */

int
jen_data_frame(ep_t *ep, wsframe_t *wsf)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          ep->rcv_buf->p[ep->rcv_buf->wrpos] = '\0';
          printf("%s\n", &ep->rcv_buf->p[ep->rcv_buf->rdpos]);
     }

     A(buf_write_sz(snd_pipe->snd_buf) > sizeof(long unsigned int));
     buf_put(snd_pipe->snd_buf, ep->hash);
     
     while (buf_write_sz(snd_pipe->snd_buf) && buf_read_sz(ep->rcv_buf))
          snd_pipe->snd_buf->p[snd_pipe->snd_buf->wrpos++] =
               ep->rcv_buf->p[ep->rcv_buf->rdpos++];

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

int
jen_open()
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s\n",
                 __FILE__,
                 __LINE__,
                 __func__);
     }

     if (!snd_pipe) {
          snd_pipe = malloc(sizeof(ep_t));
          A(snd_pipe);
          init_snd_pipe();
          A(0 > snd_pipe->fd);
     }

     if (!rcv_pipe) {
          rcv_pipe = malloc(sizeof(ep_t));
          A(rcv_pipe);
          init_rcv_pipe();
          A(0 > rcv_pipe->fd);
     }

     if (-1 == snd_pipe->fd) {
          int rv = mkfifo(snd_pathname, 0600);
          ERRET(0 > rv && errno != EEXIST, "mkfifo");

          snd_pipe->fd = open(snd_pathname, O_WRONLY | O_NONBLOCK);
          if (0 > snd_pipe->fd) {
               // see open(2) return values
               A(errno == ENODEV || errno == ENXIO);
               return (-1);
          }

          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: opened %s\n", __func__, snd_pathname);
          }

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.data.ptr = snd_pipe;
          AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, snd_pipe->fd, &ev));
     }

     if (-1 == rcv_pipe->fd) {
          int rv = mkfifo(rcv_pathname, 0600);
          ERRET(0 > rv && errno != EEXIST, "mkfifo");

          rcv_pipe->fd = open(rcv_pathname, O_RDONLY | O_NONBLOCK);
          A(rcv_pipe->fd >= 0);

          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: opened %s\n", __func__, rcv_pathname);
          }

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLIN;
          ev.data.ptr = rcv_pipe;
          AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, rcv_pipe->fd, &ev));
     }

     return 0;
}

/* int */
/* jen_on_shmem_read(wsconn_t *conn) */
/* { */
  /* buf_clear(conn->buf_in); */

  /* int len; */
  /* len=ssys_shmem_read(conn->sfd, */
  /*                     buf_ref(conn->buf_in), */
  /*                     buf_len(conn->buf_in)); */

  /* if (0 > len) */
  /*   return (-1); */

  /* if (0 == len) */
  /*   return 0; */

  /* /\* TODO Clean-up hacky access of first eight bytes. *\/ */
  /* int fd; */
  /* fd=(int)buf_get_long(conn->buf_in); */

  /* wsconn_t *dst; */
  /* dst=wsd_cfg->lookup_kernel_fd(fd); */
  /* if (!dst) */
  /*   return (-1); */

  /* /\* jen protocol exchanges null-terminated strings: find null byte *\/ */
  /* int j=0; */
  /* char *s=buf_ref(conn->buf_in); */
  /* while (*s++ != '\0') */
  /*   j++; */

  /* len=j; */

  /* buf_fwd(conn->buf_in, len); */
  /* buf_flip(conn->buf_in); */
  /* buf_get_long(conn->buf_in); /\* TODO Make repetitive read unnecessary. *\/ */

  /* if (LOG_VVERBOSE == wsd_cfg->verbose) */
  /*   printf("jen: on_shmem_read: fd=%d: dst_fd=%d: %d byte(s)\n", */
  /*          conn->fd, */
  /*          dst->fd, */
  /*          len); */
  /* else if (LOG_VVVERBOSE == wsd_cfg->verbose) */
  /*   { */
  /*     printf("jen: fd=%d: dst_fd=%d: ", conn->fd, dst->fd); */
  /*     int old=buf_len(conn->buf_in); */

  /*     while (0<buf_len(conn->buf_in)) */
  /*       printf("%c", buf_get(conn->buf_in)); */
  /*     printf("\n"); */

  /*     buf_rwnd(conn->buf_in, old); */
  /*   } */

  /* long frame_len=wsapp_calculate_frame_length(len); */
  /* if (0>frame_len) */
  /*   { */
  /*     if (LOG_VVVERBOSE <= wsd_cfg->verbose) */
  /*       printf("jen: fd=%d: negative frame length, discarding data\n", */
  /*              conn->fd); */

  /*     return 0; */
  /*   } */

  /* if (buf_len(dst->buf_out)<frame_len) */
  /*   { */
  /*     if (LOG_VVVERBOSE <= wsd_cfg->verbose) */
  /*       printf("jen: fd=%d: output buffer too small, discarding data\n", */
  /*              conn->fd); */

  /*     return 0; */
  /*   } */

  /* char byte1=0; */
  /* wsapp_set_fin_bit(byte1); */
  /* wsapp_set_opcode(byte1, WS_TEXT_FRAME); */
  /* buf_put(dst->buf_out, byte1); */
  /* wsapp_set_payload_len(dst->buf_out, len); */

  /* while (0<buf_len(conn->buf_in)) */
  /*   buf_put(dst->buf_out, buf_get(conn->buf_in)); */

  /* dst->write = 1; */

/*   return 1; */
/* } */

static int
pipe_write(ep_t *ep)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     A(ep->fd >= 0);
     A(ep->snd_buf->p);
     A(buf_read_sz(ep->snd_buf) > 0);
     int len = write(ep->fd, ep->snd_buf->p, buf_read_sz(ep->snd_buf));
     if (0 < len) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {          
               printf("\t%s: fd=%d: wrote %d byte(s)\n",
                      __func__,
                      ep->fd,
                      len);
          }

          ep->snd_buf->rdpos += len;
          AZ(buf_read_sz(ep->snd_buf));
          buf_reset(ep->snd_buf);

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLRDHUP;
          ev.data.ptr = ep;
          AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, ep->fd, &ev));

          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: removing EPOLLOUT: fd=%d\n", __func__, ep->fd);
          }
     } else {
          A(0 > len);
     }

     return len;
}

static int
pipe_close(ep_t *ep)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     A(ep->fd >= 0);
     A(ep->read == NULL ? ep->write != NULL : ep->write == NULL);
     A(ep->rcv_buf->p == NULL ?
       ep->snd_buf->p != NULL : ep->rcv_buf->p != NULL);

     AZ(close(ep->fd));

     if (snd_pipe == ep)
          init_snd_pipe();
     else if (rcv_pipe == ep)
          init_rcv_pipe();

     return 0;
}

static int
pipe_read(ep_t *ep)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     AZ(ep->rcv_buf->wrpos);
     int len = read(ep->fd, ep->rcv_buf->p, buf_write_sz(ep->rcv_buf));
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("\t%s: fd=%d: read %d byte(s)\n", __func__, ep->fd, len);
     }
     ERRET(0 > len, "read");

     /* EOF */
     if (0 == len)
          return (-1);

     ep->rcv_buf->wrpos += len;

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
          sock->snd_buf->p[sock->snd_buf->wrpos++] =
               ep->rcv_buf->p[ep->rcv_buf->rdpos++];
     AZ(buf_read_sz(ep->rcv_buf));
     buf_reset(ep->rcv_buf);

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
     ev.data.ptr = sock;

     AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, sock->fd, &ev));

     return 0;
}

static void
init_snd_pipe() {
     memset(snd_pipe, 0, sizeof(ep_t));
     snd_pipe->fd = -1;
     snd_pipe->write = pipe_write;
     snd_pipe->close = pipe_close;
     snd_pipe->snd_buf = malloc(sizeof(buf2_t));
     A(snd_pipe->snd_buf);
     snd_pipe->rcv_buf = malloc(sizeof(buf2_t));
     A(snd_pipe->rcv_buf);
}

static void
init_rcv_pipe() {
     memset(rcv_pipe, 0, sizeof(ep_t));
     rcv_pipe->fd = -1;
     rcv_pipe->read = pipe_read;
     rcv_pipe->close = pipe_close;
     rcv_pipe->snd_buf = malloc(sizeof(buf2_t));
     A(rcv_pipe->snd_buf);
     rcv_pipe->rcv_buf = malloc(sizeof(buf2_t));
     A(rcv_pipe->rcv_buf);
}
