#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include "wstypes.h"
#include "hashtable.h"
#include "jen.h"

extern const wsd_config_t *wsd_cfg;
extern DECLARE_HASHTABLE(ep_hash, 4);
extern int epfd;

static ep_t *jen_sock = NULL;

static int sock_write(ep_t *ep);
static int sock_read(ep_t *ep);
static int sock_close(ep_t *ep);

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

     if (!ep_connected(jen_sock)) {
          return (-1);
     }

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          char rpl = ep->rcv_buf->p[ep->rcv_buf->rdpos + wsf->payload_len];
          ep->rcv_buf->p[ep->rcv_buf->rdpos + wsf->payload_len] = '\0';
          printf("%s\n", &ep->rcv_buf->p[ep->rcv_buf->rdpos]);
          ep->rcv_buf->p[ep->rcv_buf->rdpos + wsf->payload_len] = rpl;
     }

     /* hash + frame length + '\0' */
     int len = sizeof(long unsigned int) + wsf->payload_len + 1;
     if (buf_write_sz(jen_sock->snd_buf) < len)
          return (-1);

     buf_put(jen_sock->snd_buf, ep->hash);

     int k = wsf->payload_len;
     while (k--)
          jen_sock->snd_buf->p[jen_sock->snd_buf->wrpos++] =
               ep->rcv_buf->p[ep->rcv_buf->rdpos++];

     buf_put(jen_sock->snd_buf, (char)'\0');

     struct epoll_event ev;
     memset(&ev, 0, sizeof(ev));
     ev.events = EPOLLOUT | EPOLLRDHUP;
     ev.data.ptr = jen_sock;
     AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, jen_sock->fd, &ev));

     hash_add(ep_hash, &ep->hash_node, ep->hash);

     return 0;
}

int
jen_close()
{
     if (jen_sock && 0 <= jen_sock->fd)
          AZ(sock_close(jen_sock));

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

     if (!jen_sock) {
          jen_sock = malloc(sizeof(ep_t));
          A(jen_sock);
          ep_init(jen_sock);
     }

     if (!ep_connected(jen_sock)) {
          ep_init(jen_sock);
          jen_sock->read = sock_read;
          jen_sock->write = sock_write;
          jen_sock->close = sock_close;

          ERRET(0 > (jen_sock->fd = socket(AF_INET,
                                           SOCK_STREAM|SOCK_NONBLOCK,
                                           0)), "socket");

          struct addrinfo hints, *res;
          memset(&hints, 0, sizeof(struct addrinfo));
          hints.ai_family = AF_INET;
          hints.ai_socktype = SOCK_STREAM;
          hints.ai_flags = AI_NUMERICSERV;

          int rv = getaddrinfo(wsd_cfg->fwd_hostname,
                               wsd_cfg->fwd_port,
                               &hints,
                               &res);
          if (0 > rv) {
               if (LOG_VVERBOSE <= wsd_cfg->verbose) {
                    printf("\t%s: cannot resolve %s\n",
                           __func__,
                           gai_strerror(rv));
               }

               return (-1);
          }

          AZ(res->ai_next);

          if (AF_INET != res->ai_family)
               return (-1);

          rv = connect(jen_sock->fd, res->ai_addr, res->ai_addrlen);
          freeaddrinfo(res);
          if (0 > rv && EINPROGRESS != errno) {
               if (LOG_VVERBOSE <= wsd_cfg->verbose) {
                    printf("\t%s: connect: %s\n",
                           __func__,
                           strerror(errno));
               }

               return (-1);
          }

          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: fd=%d, connecting to %s:%s\n",
                      __func__,
                      jen_sock->fd,
                      wsd_cfg->fwd_hostname,
                      wsd_cfg->fwd_port);
          }

          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.data.ptr = jen_sock;
          ev.events = EPOLLIN | EPOLLRDHUP;
          AZ(epoll_ctl(epfd, EPOLL_CTL_ADD, jen_sock->fd, &ev));
     }

     return 0;
}

static int
sock_write(ep_t *ep)
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
     int len = write(ep->fd,
                     ep->snd_buf->p + ep->snd_buf->rdpos,
                     buf_read_sz(ep->snd_buf));
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
          ev.events = EPOLLIN | EPOLLRDHUP;
          ev.data.ptr = ep;
          AZ(epoll_ctl(epfd, EPOLL_CTL_MOD, ep->fd, &ev));
     } else {
          A(0 > len);
     }

     return len;
}

static int
sock_close(ep_t *ep)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     A(ep->fd >= 0);
     AZ(close(ep->fd));
     ep_destroy(ep);
     A(!ep_connected(ep));

     return 0;
}

static int
sock_read(ep_t *ep)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     A(0 < buf_write_sz(ep->rcv_buf));
     int len = read(ep->fd,
                    ep->rcv_buf->p + ep->rcv_buf->wrpos,
                    buf_write_sz(ep->rcv_buf));
     if (0 > len) {
          if (ECONNREFUSED == errno) {
               if (LOG_VVERBOSE <= wsd_cfg->verbose) {
                    printf("\t%s: connection refused\n", __func__);
               }

               return (-1);
          }
     }
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

     ep_t *sock = NULL;
     hash_for_each_possible(ep_hash, sock, hash_node, hash) {
          if (sock->hash == hash)
               break;
     }

     if (NULL == sock) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: no socket for hash: 0x%lx, dropping %d byte(s)\n",
                      __func__,
                      hash,
                      buf_read_sz(ep->rcv_buf));
          }
          /* Socket closed; drop data on the floor */
          buf_reset(ep->rcv_buf);

          return 0;
     }

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          char rpl = ep->rcv_buf->p[ep->rcv_buf->wrpos];
          ep->rcv_buf->p[ep->rcv_buf->wrpos] = '\0';
          printf("%s\n", &ep->rcv_buf->p[ep->rcv_buf->rdpos]);
          ep->rcv_buf->p[ep->rcv_buf->wrpos] = rpl;
     }

     return sock->proto.send_data_frame(sock, ep);
}
