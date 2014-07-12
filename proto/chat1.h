#ifndef __CHAT1_H__
#define __CHAT1_H__

#include "wstypes.h"

int chat1_on_frame(ws_conn_t *conn, wsframe_t *wsf, buf_t *b);
int chat1_on_open(ws_conn_t *conn);
void chat1_on_close(ws_conn_t *conn);

#endif /* #ifndef __CHAT1_H__ */
