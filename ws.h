#ifndef __WS_H__
#define __WS_H__

#include "wstypes.h"

int ws_on_handshake(ws_conn_t *conn, http_req_t *hr);
int ws_on_read(ws_conn_t *conn);
int ws_on_write(ws_conn_t *conn);

#endif /* #ifndef __WS_H__ */
