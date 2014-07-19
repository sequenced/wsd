#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "chatterbox1.h"

chatterbox1_t box;

int
chatterbox1_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *in, buf_t *out)
{
  const int len=buf_len(in);

  if (LOG_VVVERBOSE<=wsd_cfg->verbose)
    {
      printf("chatterbox1: fd=%d: ", conn->pfd->fd);
      while (0<buf_len(in))
        printf("%c", buf_get(in));
      printf("\n");
      buf_rwnd(in, len);
    }

  chat_t *cursor=0;
  list_for_each_entry(cursor, &box.chat_list, list_head)
    {
      long frame_len=wsapp_calculate_frame_length(len);
      if (0>frame_len)
        /* frame too large, silently ignore */
        continue;

      if (buf_len(cursor->conn->buf_out)<frame_len)
        /* buffer too small, silently ignore this frame */
        continue;

      char byte1=0;
      wsapp_set_fin_bit(byte1);
      wsapp_set_opcode(byte1, WSAPP_WS_TEXT_FRAME);
      buf_put(cursor->conn->buf_out, byte1);
      wsapp_set_payload_len(cursor->conn->buf_out, len);

      while (0<buf_len(in))
        buf_put(cursor->conn->buf_out, buf_get(in));

      buf_rwnd(in, len);

      cursor->conn->pfd->events|=POLLOUT;
    }

  return 1;
}

int
chatterbox1_on_open(wsconn_t *conn)
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
chatterbox1_on_close(wsconn_t *conn)
{
  chat_t *rv=0, *cursor=0;
  list_for_each_entry(cursor, &box.chat_list, list_head)
    {
      if (conn==cursor->conn)
        rv=cursor;
    }

  if (rv)
    {
      list_del(&rv->list_head);
      free(rv);
      rv=0;
    }
}
