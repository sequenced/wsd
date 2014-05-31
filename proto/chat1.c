#include <stdio.h>
#include "chat1.h"

int
chat1_on_frame(wschild_conn_t *conn, wsframe_t *wsf)
{
  printf("chat1: %s\n", buf_ref(conn->buf_in));
  buf_clear(conn->buf_in);
  return 1;
}
