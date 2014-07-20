#ifndef __CHATTERBOX1_H__
#define __CHATTERBOX1_H__

#include "wsapp.h"
#include "list.h"

int chatterbox1_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *in, buf_t *out);
int chatterbox1_on_open(wsconn_t *conn);
void chatterbox1_on_close(wsconn_t *conn);

typedef struct
{
  struct list_head list_head;
  wsconn_t *conn;
} chat_t;

typedef struct
{
  struct list_head list_head;
  struct list_head chat_list;
} chatterbox1_t;

#endif /* #ifndef __CHATTERBOX1_H__ */
