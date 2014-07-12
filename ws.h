#ifndef __WS_H__
#define __WS_H__

#include "wstypes.h"

int ws_on_handshake(wsconn_t *conn, http_req_t *hr);
int ws_on_read(wsconn_t *conn);
int ws_on_write(wsconn_t *conn);

#endif /* #ifndef __WS_H__ */
