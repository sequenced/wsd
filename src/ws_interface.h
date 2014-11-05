#ifndef __WSAPP_H__
#define __WSAPP_H__

#include <limits.h>
#include "ws.h"
#include "wstypes.h"

extern wsd_config_t *wsd_cfg;

#define wsapp_set_fin_bit(byte) (byte|=0x80)
#define wsapp_set_opcode(byte, val) (byte|=(0xf&val))
#define wsapp_set_payload_bits(byte, val) (byte|=(0x7f&val))

/* see RFC6455 section 5.2 */
#define WSAPP_PAYLOAD_7BITS       1
#define WSAPP_PAYLOAD_7PLUS16BITS 2
#define WSAPP_PAYLOAD_7PLUS64BITS 9

long wsapp_calculate_frame_length(const unsigned long payload_len);
int wsapp_set_payload_len(buf_t *out, const unsigned long payload_len);
int wsapp_lookup_conn_by_protocol(const char *proto, struct list_head result);

#endif /* #ifndef __WSAPP_H__ */
