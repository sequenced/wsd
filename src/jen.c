#include "jen.h"

static int md=0;

int
jen_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *in, buf_t *out)
{
  if (0>ssys_shmem_write(md, buf_ref(in), buf_len(in)))
    perror("ssys_shmem_write");

  return 0;
}

int
jen_on_open(const wsd_config_t *cfg, wsconn_t *conn)
{
  if (0>(md=ssys_shmem_open(conn->location->url,
                            SSYS_SHMEM_FLAG_WRITE|SSYS_SHMEM_FLAG_CREATE,
                            SSYS_SHMEM_MODE_PIPE)))
    {
      perror("ssys_shmem_open");
      return -1;
    }

  return 0;
}

void
jen_on_close(wsconn_t *conn)
{
  if (0>md)
    return;

  if (0>ssys_shmem_close(md))
    perror("ssys_shmem_close");

  md=0;
}
