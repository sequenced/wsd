#include <stdio.h>
#include "chat1.h"

int
chat1_on_frame(wschild_conn_t *conn, wsframe_t *wsf)
{
  printf("chat1: ");
  int len=wsf->payload_len;
  while (0<len--)
    printf("%c", buf_get(conn->buf_in));
  printf("\n");

  return 1;
}
