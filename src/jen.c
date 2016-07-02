#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "jen.h"

#define UNASSIGNED (-1)
#define JEN_BUF_SIZE 512

static int md_in=UNASSIGNED;
static int md_out=UNASSIGNED;
static int md_ref_count=0;
static buf_t *scratch=NULL;
static const wsd_config_t *wsd_cfg;
static const char *inbound = "-inbound";
static const char *outbound = "-outbound";

static void close_if_open();

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
jen_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *in, buf_t *out)
{
  buf_clear(scratch);
  /* TODO Make hop ref id unique. */
  buf_put_long(scratch, (unsigned long long)conn->pfd->fd);
  buf_put_buf(scratch, in);
  buf_flip(scratch);

  if (LOG_VVERBOSE <= wsd_cfg->verbose)
    printf("jen: on_frame: fd=%d: src_fd=%d: %d byte(s)\n",
           md_in,
           conn->pfd->fd,
           buf_len(scratch));

  if (0 > ssys_shmem_write(md_in, buf_ref(scratch), buf_len(scratch)))
    perror("jen: ssys_shmem_write");

  return 0;
}

int
jen_on_open(const wsd_config_t *cfg, wsconn_t *conn)
{
  if (0 < md_ref_count)
    goto finish;

  assert(scratch == NULL);
  scratch=buf_alloc(JEN_BUF_SIZE);
  if (!scratch)
    return (-1);

  buf_t *path=buf_alloc(strlen(conn->location->url)+strlen(inbound)+1);
  if (!path)
    return (-1);

  buf_put_string(path, conn->location->url);
  buf_put_string(path, inbound);
  buf_flip(path);

  assert(md_in == UNASSIGNED);
  md_in=ssys_shmem_open(buf_ref(path),
                        SSYS_SHMEM_FLAG_WRITE|SSYS_SHMEM_FLAG_CREATE,
                        SSYS_SHMEM_MODE_PIPE);
  buf_free(path);
  path=0;
  if (0 > md_in)
    {
      perror("jen: ssys_shmem_open");
      md_in=UNASSIGNED;
      return (-1);
    }

  path=buf_alloc(strlen(conn->location->url)+strlen(outbound)+1);
  if (!path)
    return (-1);

  buf_put_string(path, conn->location->url);
  buf_put_string(path, outbound);
  buf_flip(path);

  assert(md_out == UNASSIGNED);
  md_out=ssys_shmem_open(buf_ref(path),
                         SSYS_SHMEM_FLAG_READ|SSYS_SHMEM_FLAG_CREATE,
                         SSYS_SHMEM_MODE_PIPE);
  buf_free(path);
  path=0;
  if (0 > md_out)
    {
      perror("jen: ssys_shmem_open");
      md_out=UNASSIGNED;
      close_if_open();
      return (-1);
    }

  wsd_cfg=cfg;

  int rv;
  rv=wsd_cfg->register_user_fd(md_out,
                               jen_on_shmem_read,
                               jen_on_shmem_write,
                               POLLIN);
  if (0 > rv)
    {
      perror("jen: register_user_fd");
      close_if_open();
      return (-1);
    }

 finish:
  md_ref_count++;

  if (LOG_VVERBOSE <= wsd_cfg->verbose)
    printf("jen: on_open: fd=%d, md_ref_count: %d\n",
           conn->pfd->fd,
           md_ref_count);

  return md_ref_count;
}

void
jen_on_close(wsconn_t *conn)
{
  if (0 < md_ref_count)
    md_ref_count--;

  if (LOG_VVERBOSE <= wsd_cfg->verbose)
    printf("jen: on_close: fd=%d, md_ref_count=%d\n",
           conn->pfd->fd,
           md_ref_count);

  if (0 == md_ref_count)
    close_if_open();
}

static void
close_if_open()
{
  if (md_in != UNASSIGNED)
    {
      if (0 > ssys_shmem_close(md_in))
        perror("jen: ssys_shmem_close");
      md_in=UNASSIGNED;
    }

  if (md_out != UNASSIGNED)
    {
      if (0 > ssys_shmem_close(md_out))
        perror("jen: ssys_shmem_close");
      md_out=UNASSIGNED;
    }

  if (scratch)
    {
      buf_free(scratch);
      scratch=NULL;
    }
}

int
jen_on_shmem_read(wsconn_t *conn)
{
  buf_clear(conn->buf_in);

  int len;
  len=ssys_shmem_read(conn->pfd->fd,
                      buf_ref(conn->buf_in),
                      buf_len(conn->buf_in));

  if (0 > len)
    return (-1);

  if (0 == len)
    return 0;

  /* TODO Clean-up hacky access of first eight bytes. */
  int fd;
  fd=(int)buf_get_long(conn->buf_in);

  wsconn_t *dst;
  dst=wsd_cfg->lookup_kernel_fd(fd);
  if (!dst)
    return (-1);

  /* jen protocol exchanges null-terminated strings: find null byte */
  int j=0;
  char *s=buf_ref(conn->buf_in);
  while (*s++ != '\0')
    j++;

  len=j;

  buf_fwd(conn->buf_in, len);
  buf_flip(conn->buf_in);
  buf_get_long(conn->buf_in); /* TODO Make repetitive read unnecessary. */

  if (LOG_VVERBOSE == wsd_cfg->verbose)
    printf("jen: on_shmem_read: fd=%d: dst_fd=%d: %d byte(s)\n",
           conn->pfd->fd,
           dst->pfd->fd,
           len);
  else if (LOG_VVVERBOSE == wsd_cfg->verbose)
    {
      printf("jen: fd=%d: dst_fd=%d: ", conn->pfd->fd, dst->pfd->fd);
      int old=buf_len(conn->buf_in);

      while (0<buf_len(conn->buf_in))
        printf("%c", buf_get(conn->buf_in));
      printf("\n");

      buf_rwnd(conn->buf_in, old);
    }

  long frame_len=wsapp_calculate_frame_length(len);
  if (0>frame_len)
    {
      if (LOG_VVVERBOSE <= wsd_cfg->verbose)
        printf("jen: fd=%d: negative frame length, discarding data\n",
               conn->pfd->fd);

      return 0;
    }

  if (buf_len(dst->buf_out)<frame_len)
    {
      if (LOG_VVVERBOSE <= wsd_cfg->verbose)
        printf("jen: fd=%d: output buffer too small, discarding data\n",
               conn->pfd->fd);

      return 0;
    }

  char byte1=0;
  wsapp_set_fin_bit(byte1);
  wsapp_set_opcode(byte1, WS_TEXT_FRAME);
  buf_put(dst->buf_out, byte1);
  wsapp_set_payload_len(dst->buf_out, len);

  while (0<buf_len(conn->buf_in))
    buf_put(dst->buf_out, buf_get(conn->buf_in));

  dst->pfd->events|=POLLOUT;

  return 1;
}

int
jen_on_shmem_write(wsconn_t *conn)
{
  return (-1);
}
