#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "chat1.h"

#define SET_FIN_BIT(byte) (byte|=0x80)
#define SET_OPCODE(byte, val) (byte|=(0xf&val))
#define SET_PAYLOAD_LEN(byte, val) (byte|=(0x7f&val))
#define WS_TEXT_FRAME 0x1

int
chat1_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *b)
{
  printf("chat1: ");
  while (0<buf_len(b))
    printf("%c", buf_get(b));
  printf("\n");

  char byte1=0, byte2=0;
  SET_FIN_BIT(byte1);
  SET_OPCODE(byte1, WS_TEXT_FRAME);
  buf_put(conn->buf_out, byte1);

  if (wsf->payload_len<126)
    {
      SET_PAYLOAD_LEN(byte2, wsf->payload_len);
      buf_put(conn->buf_out, byte2);
    }
  else if (wsf->payload_len<=USHRT_MAX)
    {
      SET_PAYLOAD_LEN(byte2, 126);
      buf_put(conn->buf_out, byte2);
      buf_put_short(conn->buf_out, htobe16(wsf->payload_len));
    }
  else if (wsf->payload_len<=(ULONG_MAX>>1))
    {
      SET_PAYLOAD_LEN(byte2, 127);
      buf_put(conn->buf_out, byte2);
      buf_put_long(conn->buf_out, htobe64(wsf->payload_len));
    }

  int len=wsf->payload_len;
  while (len--)
    {
      buf_rwnd(b, 1);
      buf_put(conn->buf_out, buf_get(b));
      buf_rwnd(b, 1);
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
