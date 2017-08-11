#ifndef __PP2_H__
#define __PP2_H__

#include <stdint.h>
#include "types.h"

#define PP2_TYPE_MIN_CUSTOM    0xE0
#define PP2_TYPE_CONNHASH      PP2_TYPE_MIN_CUSTOM
#define PP2_TYPE_PAYLOADLEN    0xE1
#define PP2_TYPE_MAX_CUSTOM    0xEF

struct proxy_hdr_v2 {
     uint8_t sig[12];  /* hex 0D 0A 0D 0A 00 0D 0A 51 55 49 54 0A */
     uint8_t ver_cmd;  /* protocol version and command */
     uint8_t fam;      /* protocol family and address */
     uint16_t len;     /* number of following bytes part of the header */
} __attribute__((packed));

struct pp2_tlv {
     uint8_t type;
     uint8_t length_hi;
     uint8_t length_lo;
     uint8_t value[0];
} __attribute__((packed));

int pp2_recv(sk_t *sk);
int pp2_decode_frame(sk_t *sk);
int pp2_encode_frame(sk_t *sk, wsframe_t *wsf);

#endif /* #ifndef __PP2_H__ */
