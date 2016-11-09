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
#include "jen.h"

extern const wsd_config_t *wsd_cfg;

static const char *path = "/tmp";
static const char *inbound = "-in";
static const char *outbound = "-out";

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

/*     buf_clear(scratch);
     buf_put_long(scratch, (unsigned long long)conn->hash);
     buf_put_buf(scratch, in);
     buf_flip(scratch);

     if (LOG_VVERBOSE <= wsd_cfg->verbose)
          printf("jen: on_frame: sfd=%d: src_fd=%d: %d byte(s)\n",
                 pfd_in,
                 conn->sfd,
                 buf_len(scratch));

                 AN(write(pfd_in, buf_ref(scratch), buf_len(scratch)));*/

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

     /* A(conn->pfd_out == -1); */
     /* A(conn->pfd_in == -1); */

     /* char pathname[128]; //TODO check length at parse time */
     /* memset(&pathname, 0, sizeof(pathname)); */
     /* strcpy(pathname, path); */
     /* strcpy((pathname + strlen(path)), conn->location->url); */
     /* strcpy((pathname + strlen(path) + strlen(conn->location->url)), outbound); */
     
     /* int rv = mkfifo(pathname, 0600); */
     /* if (0 > rv && errno != EEXIST) { */
     /*      perror("mkfifo"); */
     /*      return (-1); */
     /* } */

     /* conn->pfd_out = open(pathname, O_RDONLY | O_NONBLOCK); */
     /* A(conn->pfd_out >= 0); */

     /* memset(&pathname, 0, sizeof(pathname)); */
     /* strcpy(pathname, path); */
     /* strcpy((pathname + strlen(path)), conn->location->url); */
     /* strcpy((pathname + strlen(path) + strlen(conn->location->url)), inbound); */
     
     /* rv = mkfifo(pathname, 0600); */
     /* if (0 > rv && errno != EEXIST) { */
     /*      perror("mkfifo"); */
     /*      return (-1); */
     /* } */

     /* conn->pfd_in = open(pathname, O_WRONLY | O_NONBLOCK); */
     /* if (0 > conn->pfd_in) { */
     /*      A(errno == ENODEV || errno == ENXIO); // see open(2) return values */
     /*      return (-1); */
     /* } */
     /* A(conn->pfd_in >= 0); */
     
     /* wsd_cfg = cfg; */

     /* struct epoll_event ev; */
     /* memset((void*)&ev, 0, sizeof(ev)); */
     /* ev.events = EPOLLOUT | EPOLLRDHUP; */
     /* ev.data.ptr = conn; */

     /* AZ(epoll_ctl(wsd_cfg->epfd, EPOLL_CTL_ADD, conn->pfd_in, &ev)); */

     /* memset((void*)&ev, 0, sizeof(ev)); */
     /* ev.events = EPOLLIN; */
     /* ev.data.ptr = conn; */

     /* AZ(epoll_ctl(wsd_cfg->epfd, EPOLL_CTL_ADD, conn->pfd_out, &ev)); */

     /* if (LOG_VVERBOSE <= wsd_cfg->verbose) */
     /*      printf("jen: on_open: sfd=%d, pfd_in=%d, pfd_out=%d\n", */
     /*             conn->sfd, */
     /*             conn->pfd_in, */
     /*             conn->pfd_out); */

     /* return 0; */
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

