#ifndef __WS_WSD_H__
#define __WS_WSD_H__

#include "types.h"
#include "http.h"

int ws_recv(sk_t *sk);
int ws_decode_handshake(sk_t *sk, http_req_t *req);

#endif /* #ifndef __WS_WSD_H__ */
