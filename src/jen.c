#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "jen.h"

static int md_in=-1;
static int md_out=-1;
static const wsconn_t *extconn;
static const wsd_config_t *wsd_cfg;
static const char *inbound = "-inbound";
static const char *outbound = "-outbound";

int
jen_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *in, buf_t *out)
{
  if (0>ssys_shmem_write(md_in, buf_ref(in), buf_len(in)))
    perror("ssys_shmem_write");

  return 0;
}

int
jen_on_open(const wsd_config_t *cfg, wsconn_t *conn)
{
  buf_t *path=buf_alloc(strlen(conn->location->url)+strlen(inbound)+1);
  if (!path)
    return (-1);

  buf_put_string(path, conn->location->url);
  buf_put_string(path, inbound);
  buf_flip(path);

  md_in=ssys_shmem_open(buf_ref(path),
                        SSYS_SHMEM_FLAG_WRITE,
                        SSYS_SHMEM_MODE_PIPE);
  buf_free(path);
  path=0;
  if (0>md_in)
    {
      perror("ssys_shmem_open");
      return (-1);
    }

  path=buf_alloc(strlen(conn->location->url)+strlen(outbound)+1);
  if (!path)
    return (-1);

  buf_put_string(path, conn->location->url);
  buf_put_string(path, outbound);
  buf_flip(path);

  md_out=ssys_shmem_open(buf_ref(path),
                         SSYS_SHMEM_FLAG_READ,
                         SSYS_SHMEM_MODE_PIPE);
  buf_free(path);
  path=0;
  if (0>md_out)
    {
      perror("ssys_shmem_open");
      return (-1);
    }

  wsd_cfg=cfg;
  extconn=conn;

  /* int rv; */
  /* rv=wsd_cfg->register_user_fd(md_in, 0, jen_on_shmem_write, (short)0); */
  /* if (0==rv) */
  /*   rv= */

  /* TODO: free shmem ring when registration fails */
  return wsd_cfg->register_user_fd(md_out, jen_on_shmem_read, POLLIN);
}

void
jen_on_close(wsconn_t *conn)
{
  if (md_in>=0)
    {
      if (0>ssys_shmem_close(md_in))
        perror("ssys_shmem_close");
      md_in=(-1);
    }

  if (md_out>=0)
    {
      if (0>ssys_shmem_close(md_out))
        perror("ssys_shmem_close");
      md_out=(-1);
    }
}

int
jen_on_shmem_read(wsconn_t *conn)
{
  int len;
  buf_clear(conn->buf_in);
 again:
  if (0>(len=ssys_shmem_read(conn->pfd->fd,
                             buf_ref(conn->buf_in),
                             buf_len(conn->buf_in))))
    {
      if (EAGAIN!=errno)
        {
          perror("ssys_shmem_read");
          return (-1);
        }

      goto again;
    }

  if (0==len)
    return 0;

  buf_fwd(conn->buf_in, len);
  buf_flip(conn->buf_in);

  if (LOG_VVVERBOSE<=wsd_cfg->verbose)
    {
      printf("jen: fd=%d: ", conn->pfd->fd);
      int old=buf_len(conn->buf_in);

      while (0<buf_len(conn->buf_in))
        printf("%c", buf_get(conn->buf_in));
      printf("\n");

      buf_rwnd(conn->buf_in, old);
    }

  long frame_len=wsapp_calculate_frame_length(len);
  if (0>frame_len)
    {
      if (LOG_VVVERBOSE<=wsd_cfg->verbose)
        printf("jen: fd=%d: negative frame length, discarding data\n",
               conn->pfd->fd);

      return 0;
    }

  if (buf_len(extconn->buf_out)<frame_len)
    {
      if (LOG_VVVERBOSE<=wsd_cfg->verbose)
        printf("jen: fd=%d: output buffer too small, discarding data\n",
               conn->pfd->fd);

      return 0;
    }

  char byte1=0;
  wsapp_set_fin_bit(byte1);
  wsapp_set_opcode(byte1, WS_TEXT_FRAME);
  buf_put(extconn->buf_out, byte1);
  wsapp_set_payload_len(extconn->buf_out, len);

  while (0<buf_len(conn->buf_in))
    buf_put(extconn->buf_out, buf_get(conn->buf_in));

  extconn->pfd->events|=POLLOUT;

  return 1;
}

int
jen_on_shmem_write(wsconn_t *conn)
{
  return (-1);
}
