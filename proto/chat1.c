#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "chat1.h"

chatterbox1_t box;

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

  /* TODO sent payload to other connections */

  conn->pfd->events|=POLLOUT;

  return 1;
}

int
chat1_on_open(wsconn_t *conn)
{
  static char once=1;

  if (once)
    {
      init_list_head(&box.chat_list);
      once=0;
    }

  chat_t *chat=malloc(sizeof(chat_t));
  memset((void*)chat, 0x0, sizeof(chat_t));
  chat->conn=conn;

  list_add_tail(&chat->list_head, &box.chat_list);

  return 0;
}

void
chat1_on_close(wsconn_t *conn)
{
  chat_t *rv=0, *pos=0;
  list_for_each_entry(pos, &box.chat_list, list_head)
    {
      if (conn==pos->conn)
        rv=pos;
    }

  if (rv)
    {
      list_del(&rv->list_head);
      free(rv);
      rv=0;
    }
}
