#ifndef __WS_H__
#define __WS_H__

#include <stdbool.h>
#include "types.h"

#define WS_FRAG_FRAME   0x0
#define WS_TEXT_FRAME   0x1
#define WS_BINARY_FRAME 0x2
#define WS_CLOSE_FRAME  0x8
#define WS_PING_FRAME   0x9
#define WS_PONG_FRAME   0xa

#define WS_FRAME_STATUS_LEN   2
#define WS_MASKING_KEY_LEN    4
#define WS_MASKED_FRAME_LEN   6
#define WS_UNMASKED_FRAME_LEN 2
#define WS_MASKED_FRAME_LEN16 8
#define WS_MASKED_FRAME_LEN64 16
#define WS_ACCEPT_KEY_LEN     28
#define WS_GUID               "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_VER                "Sec-WebSocket-Version: "
#define WS_PROTO              "Sec-WebSocket-Protocol: "

/* defined status codes, see RFC6455 section 7.4.1 */
#define WS_1000 1000
#define WS_1011 1011

#define set_fin_bit(byte)     (byte |= 0x80)
#define set_opcode(byte, val) (byte |= (0xf & val))
#define set_mask_bit(byte)    (byte |= 0x80)
#define RSV1_BIT(byte)        (0x40 & byte)
#define RSV2_BIT(byte)        (0x20 & byte)
#define RSV3_BIT(byte)        (0x10 & byte)
#define OPCODE(byte)          (0xf & byte)
#define MASK_BIT(byte)        ((0x80 & byte) >> 7)
#define PAYLOAD_LEN(byte)     (unsigned long int)(0x7f & byte)

int ws_decode_frame(sk_t *sk);
int ws_encode_frame(sk_t *sk, wsframe_t *wsf);
long ws_calculate_frame_length(const unsigned long len);
int ws_set_payload_len(skb_t *b, const unsigned long len, char byte2);
int ws_start_closing_handshake(sk_t *sk, const int status, const bool do_mask);
int ws_finish_closing_handshake(sk_t *sk,
                                const bool do_mask,
                                const unsigned long int len);
unsigned long int ws_decode_payload_len(skb_t *buf, const char byte2);
void ws_printf(FILE *stream,
               const wsframe_t *wsf,
               const char *prefix,
               const uint64_t hash);

#endif /* #ifndef __WS_H__ */
