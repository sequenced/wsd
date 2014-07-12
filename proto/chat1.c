#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "chat1.h"

int
chat1_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *in, buf_t *out)
{
  printf("chat1: ");
  while (0<buf_len(in))
    printf("%c", buf_get(in));
  printf("\n");

  char byte1=0;
  wsapp_set_fin_bit(byte1);
  wsapp_set_opcode(byte1, WSAPP_WS_TEXT_FRAME);
  buf_put(out, byte1);
  wsapp_set_payload_len(out, wsf->payload_len);

  int len=wsf->payload_len;
  while (len--)
    {
      buf_rwnd(in, 1);
      buf_put(out, buf_get(in));
      buf_rwnd(in, 1);
    }

  conn->pfd->events|=POLLOUT;

  return 1;
}

int
chat1_on_open(wsconn_t *conn)
{
  return 0;
}

void
chat1_on_close(wsconn_t *conn)
{

}
