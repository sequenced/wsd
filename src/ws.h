#ifndef __WS_H__
#define __WS_H__

#include "wstypes.h"

#define WS_TEXT_FRAME   0x1
#define WS_BINARY_FRAME 0x2
#define WS_CLOSE_FRAME  0x8
#define WS_PING_FRAME   0x9
#define WS_PONG_FRAME   0xa

/* defined status codes, see RFC6455 section 7.4.1 */
#define WS_1011 1011

int ws_handshake(ep_t *ep, http_req_t *hr);
int ws_read(ep_t *ep);

#endif /* #ifndef __WS_H__ */
