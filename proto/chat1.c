#include <poll.h>
#include <stdio.h>
#include <string.h>
#include "chat1.h"

#define SET_FIN_BIT(byte) (byte|=0x80)
#define SET_OPCODE(byte, val) (byte|=(0xf&val))
#define SET_PAYLOAD_LEN(byte, val) (byte|=(0x7f&val))
#define WS_TEXT_FRAME 0x1
#define MY_MESSAGE    "Hello world from chat1.c!"

int
chat1_on_frame(wschild_conn_t *conn, wsframe_t *wsf, buf_t *b)
{
  printf("chat1: ");
  while (0<buf_len(b))
    printf("%c", buf_get(b));
  printf("\n");

  char byte1=0, byte2=0;
  SET_FIN_BIT(byte1);
  SET_OPCODE(byte1, WS_TEXT_FRAME);
  SET_PAYLOAD_LEN(byte2, strlen(MY_MESSAGE));
  buf_put(conn->buf_out, byte1);
  buf_put(conn->buf_out, byte2);
  buf_put_string(conn->buf_out, MY_MESSAGE);
  conn->pfd->events|=POLLOUT;

  return 1;
}
