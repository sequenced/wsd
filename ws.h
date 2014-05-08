#ifndef __WS_H__
#define __WS_H__

#include "wstypes.h"

#define WS_FIN_BIT     0x1
#define WS_RSV1_BIT    0x2
#define WS_RSV2_BIT    0x4
#define WS_OPCODE      0xf
#define WS_MASK_BIT    0x100
#define WS_PAYLOAD_LEN 0xe00

int ws_on_handshake(wschild_conn_t *conn, http_req_t *hr);
int ws_on_read(wschild_conn_t *conn);
int ws_on_write(wschild_conn_t *conn);

#endif /* #ifndef __WS_H__ */
