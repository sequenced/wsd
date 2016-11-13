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
 * 0th byte           8th byte        nth byte  n+1 byte
 * +------------------+------------------+---------+
 * | sender reference | JSON rpc payload |   0x0   | 
 * +------------------+------------------+---------+
 *
 */

int
jen_recv_data_frame(ep_t *ep, wsframe_t *wsf)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          char rpl = ep->rcv_buf->p[ep->rcv_buf->rdpos + wsf->payload_len];
          ep->rcv_buf->p[ep->rcv_buf->rdpos + wsf->payload_len] = '\0';
          printf("%s\n", &ep->rcv_buf->p[ep->rcv_buf->rdpos]);
          ep->rcv_buf->p[ep->rcv_buf->rdpos + wsf->payload_len] = rpl;
     }

     /* hash + frame length + '\0' */
     int len = sizeof(long unsigned int) + wsf->payload_len + 1;
     if (buf_write_sz(snd_pipe->snd_buf) < len)
          return (-1);

     buf_put(snd_pipe->snd_buf, ep->hash);

     int k = wsf->payload_len;
     while (k-- && buf_read_sz(ep->rcv_buf))
          snd_pipe->snd_buf->p[snd_pipe->snd_buf->wrpos++] =
               ep->rcv_buf->p[ep->rcv_buf->rdpos++];

     buf_put(snd_pipe->snd_buf, (char)'\0');

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
jen_close()
{
     if (snd_pipe && 0 < snd_pipe->fd)
          AZ(pipe_close(snd_pipe));

     if (rcv_pipe && 0 < rcv_pipe->fd)
          AZ(pipe_close(rcv_pipe));

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
               printf("\t%s: wrote %d byte(s)\n",
                      __func__,
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
               printf("\t%s: removing EPOLLOUT\n", __func__);
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
          printf("\t%s: read %d byte(s)\n", __func__, len);
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
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: hash=0x%lx: socket closed: dropping %d byte(s)\n",
                      __func__,
                      hash,
                      buf_read_sz(ep->rcv_buf));
          }
          /* Socket closed; drop data on the floor */
          buf_reset(ep->rcv_buf);

          return 0;
     }

     return sock->proto.send_data_frame(sock, ep);
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
