#ifndef __CHATTERBOX_H__
#define __CHATTERBOX_H__

#include "wsapp.h"
#include "list.h"

int chatterbox_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *in, buf_t *out);
int chatterbox_on_open(wsconn_t *conn);
void chatterbox_on_close(wsconn_t *conn);

typedef struct
{
  struct list_head list_head;
  wsconn_t *conn;
} chat_t;

typedef struct
{
  struct list_head list_head;
  struct list_head chat_list;
} chatterbox_t;

#endif /* #ifndef __CHATTERBOX_H__ */
