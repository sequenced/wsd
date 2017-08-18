#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <stdbool.h>

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#else
/* TODO */
#endif

#include <sys/epoll.h>
#include <sys/types.h>
#include "config.h"
#include "common.h"
#include "ws.h"
#include "ws_wsd.h"
#include "pp2.h"

/* see RFC6455 section 5.2 */
#define WS_PAYLOAD_7BITS       1
#define WS_PAYLOAD_7PLUS16BITS 2
#define WS_PAYLOAD_7PLUS64BITS 9

#define fin_bit(byte)               (0x80 & byte)
#define set_payload_bits(byte, val) (byte |= (0x7f & val))
#define MASKING_KEY(p)              *((unsigned int*)(p))

extern int epfd;
extern sk_t *pp2sk;
extern unsigned int wsd_errno;
extern const wsd_config_t *wsd_cfg;

static int dispatch_payload(sk_t *sk, wsframe_t *wsf);
static int encode_ping_frame(sk_t *sk, wsframe_t *wsf);
static int encode_pong_frame(sk_t *sk, wsframe_t *wsf);
static int encode_close_frame(skb_t *b, int status, bool do_mask);

int
ws_encode_frame(sk_t *sk, wsframe_t *wsf)
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n", __FILE__, __LINE__, __func__, sk->fd);
     }

     long frame_len = ws_calculate_frame_length(wsf->payload_len);
     A(0 < frame_len);

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("\t%s:%d: frame len=%d, payload len=%lu\n",
                 __func__,
                 __LINE__,
                 frame_len,
                 wsf->payload_len);
     }

     if (skb_wrsz(sk->sendbuf) < frame_len) {
          /* Destination buffer too small, drop data */
//          pp2sk->recvbuf->rdpos += wsf->payload_len;

          if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s:%d: have %ld byte(s), need %ld byte(s)\n",
                      __func__,
                      __LINE__,
                      frame_len,
                      skb_wrsz(sk->sendbuf));
          }
          
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     set_fin_bit(wsf->byte1);
     set_opcode(wsf->byte1, WS_TEXT_FRAME);
     skb_put(sk->sendbuf, wsf->byte1);
     AZ(ws_set_payload_len(sk->sendbuf, wsf->payload_len, 0));

     if (LOG_VERBOSE <= wsd_cfg->verbose)
          ws_printf(stderr, wsf, "TX");

     unsigned long k = wsf->payload_len;
     while (k--)
          sk->sendbuf->data[sk->sendbuf->wrpos++] =
               pp2sk->recvbuf->data[pp2sk->recvbuf->rdpos++];

     skb_compact(pp2sk->recvbuf);

     if (!(sk->events & EPOLLOUT)) {
          turn_on_events(sk, EPOLLOUT);
     }

     if (!(pp2sk->events & EPOLLIN)) {
          turn_on_events(sk, EPOLLIN);
     }

     return 0;
}

int
ws_decode_frame(sk_t *sk)
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sk->fd);
     }

     if (skb_rdsz(sk->recvbuf) < WS_MASKED_FRAME_LEN) {
          /* need WS_MASKED_FRAME_LEN bytes; see RFC6455 section 5.2 */
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     unsigned int old_rdpos = sk->recvbuf->rdpos;

     wsframe_t wsf;
     memset(&wsf, 0, sizeof(wsframe_t));
     skb_get(sk->recvbuf, wsf.byte1);
     skb_get(sk->recvbuf, wsf.byte2);

     /* see RFC6455 section 5.2 */
     if (RSV1_BIT(wsf.byte1) != 0 ||
         RSV2_BIT(wsf.byte1) != 0 ||
         RSV3_BIT(wsf.byte1) != 0 ||
         MASK_BIT(wsf.byte2) == 0) {

          wsd_errno = WSD_EBADREQ;
          return (-1);
     }

     wsf.payload_len = ws_decode_payload_len(sk->recvbuf, wsf.byte2);
     if (0 > wsf.payload_len) {
          skb_rd_reset(sk->recvbuf, old_rdpos);
          return (-1);
     }

     if (sizeof(wsf.masking_key) > skb_rdsz(sk->recvbuf)) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }
     skb_get(sk->recvbuf, wsf.masking_key);

     if (LOG_VERBOSE <= wsd_cfg->verbose)
          ws_printf(stderr, &wsf, "RX");

     /* TODO check that payload64 has left-most bit off */

     /* TODO introduce a practical maximum payload size */

     if (wsf.payload_len > skb_rdsz(sk->recvbuf)) {
          sk->recvbuf->rdpos = old_rdpos;
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     unsigned int end = sk->recvbuf->rdpos + wsf.payload_len;
     for (unsigned int i = sk->recvbuf->rdpos, j = 0; i < end; i++, j++)
          sk->recvbuf->data[i] =
               unmask(sk->recvbuf->data[i], j, wsf.masking_key);

     return dispatch_payload(sk, &wsf);
}

int
dispatch_payload(sk_t *sk, wsframe_t *wsf)
{
     int rv;
     switch (OPCODE(wsf->byte1)) {
     case WS_TEXT_FRAME:
     case WS_BINARY_FRAME:
     case WS_FRAG_FRAME:
          rv = pp2sk->proto->encode_frame(sk, wsf);
          break;
     case WS_CLOSE_FRAME:
          rv = ws_finish_closing_handshake(sk, false);
          break;
     case WS_PING_FRAME:
          rv = encode_ping_frame(sk, wsf);
          break;
     case WS_PONG_FRAME:
          rv = encode_pong_frame(sk, wsf);
          break;
     default:
          if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s:%d: unknown opcode: 0x%x\n",
                      __func__,
                      __LINE__,
                      sk->fd,
                      OPCODE(wsf->byte1));
          }

          rv = ws_start_closing_handshake(sk, WS_1011, false);
     }

     return rv;
}

unsigned long int
ws_decode_payload_len(skb_t *buf, const char byte2)
{
     if (PAYLOAD_LEN(byte2) < 126)
          return PAYLOAD_LEN(byte2);

     if (PAYLOAD_LEN(byte2) == 126) {
          if (skb_rdsz(buf) < 2) {
               wsd_errno = WSD_EINPUT;
               return (-1);
          }

          unsigned short k;
          skb_get(buf, k);
          return be16toh(k);
     }

     if (PAYLOAD_LEN(byte2) == 127) {
          if (skb_rdsz(buf) < 8) {
               wsd_errno = WSD_EINPUT;
               return (-1);
          }

          unsigned long int k;
          skb_get(buf, k);
          return be64toh(k);
     }

     wsd_errno = WSD_EBADREQ;
     return (-1);
}

int
ws_finish_closing_handshake(sk_t *sk, bool do_mask)
{
     unsigned short status = 0;
     if (WS_FRAME_STATUS_LEN <= skb_wrsz(sk->sendbuf)) {
          skb_get(sk->recvbuf, status);
          status = be16toh(status);
     }

     /* Closing handshake completed (reply to our earlier close frame) */
     if (sk->closing) {
          skb_reset(sk->recvbuf);
          sk->closing = 0;
          sk->close = 1;
          return 0;
     }

     int rv;
     rv = encode_close_frame(sk->sendbuf, status, do_mask);
     if (0 == rv) {
          sk->close_on_write = 1;
          sk->closing = 1;
     }

     /* don't process data after close frame, see RFC6455 section 5.5.1 */
     skb_reset(sk->recvbuf);

     return rv;
}

int
encode_ping_frame(sk_t *sk, wsframe_t *ignored)
{
     wsd_errno = WSD_EBADREQ;
     return (-1);
}

int
encode_pong_frame(sk_t *sk, wsframe_t *ignored)
{
     wsd_errno = WSD_EBADREQ;
     return (-1);
}

int
encode_close_frame(skb_t *dst, int status, bool do_mask)
{
     unsigned int frame_len = do_mask ?
          WS_MASKED_FRAME_LEN : WS_UNMASKED_FRAME_LEN;
     frame_len += (0 < status ? 2 : 0); /* See section 5.5.1 RFC6455 */
     
     if (frame_len > skb_wrsz(dst)) {
          wsd_errno = WSD_ENOMEM;
          return (-1);
     }

     wsframe_t wsf;
     memset(&wsf, 0, sizeof(wsframe_t));
     wsf.payload_len = 0 < status ? 2 : 0; 

     set_fin_bit(wsf.byte1);
     set_opcode(wsf.byte1, WS_CLOSE_FRAME);
     if (do_mask) {
          set_mask_bit(wsf.byte2);
          wsf.masking_key = 0xcafebabe;
     }

     if (LOG_VERBOSE <= wsd_cfg->verbose)
          ws_printf(stderr, &wsf, "TX");

     skb_put(dst, wsf.byte1);
     AZ(ws_set_payload_len(dst, wsf.payload_len, wsf.byte2));
     if (do_mask) {
          skb_put(dst, wsf.masking_key);
     }
     if (0 < status) {
          unsigned short k = htobe16(status);
          if (do_mask) {
               skb_put(dst, mask((char)(k >> 2), 0, wsf.masking_key));
               skb_put(dst, mask((char)(k & 0xff), 1, wsf.masking_key));
          } else {
               skb_put(dst, k);
          }
     }

     return 0;
}

int
ws_start_closing_handshake(sk_t *sk, int status, bool do_mask)
{
     AZ(sk->closing);
     
     int rv = encode_close_frame(sk->sendbuf, status, do_mask);
     if (0 == rv)
          sk->closing = 1;

     return rv;
}

long
ws_calculate_frame_length(const unsigned long len)
{
     if (len < 126) {
          return WS_PAYLOAD_7BITS + len;
     } else if (len <= USHRT_MAX) {
          return WS_PAYLOAD_7PLUS16BITS + len;
     } else if (len <= (ULONG_MAX >> 1)) {
          return WS_PAYLOAD_7PLUS64BITS + len;
     }

     wsd_errno = WSD_ENUM;
     return (-1L);
}

int
ws_set_payload_len(skb_t *b, const unsigned long len, char byte2)
{
     if (skb_wrsz(b) < 1)
          return (-1);

     if (len < 126) {
          set_payload_bits(byte2, len);
          skb_put(b, byte2);
     } else if (len <= USHRT_MAX) {
          set_payload_bits(byte2, 126);
          skb_put(b, byte2);

          if (skb_wrsz(b) < 2)
               return (-1);

          skb_put(b, (short)htobe16(len));
     } else if (len <= (ULONG_MAX>>1)) {
          set_payload_bits(byte2, 127);
          skb_put(b, byte2);

          if (skb_wrsz(b) < 8)
               return (-1);

          skb_put(b, (unsigned long)htobe64(len));
     } else {
          return (-1);
     }

     return 0;
}

void
ws_printf(FILE *stream, const wsframe_t *wsf, const char *prefix)
{
     fprintf(stream,
             "%s:0x%hhx|0x%hhx|opcode=%hhu|maskbit=%hhu|payload_len=%lu\n",
             prefix,
             wsf->byte1,
             wsf->byte2,
             OPCODE(wsf->byte1),
             MASK_BIT(wsf->byte2),
             wsf->payload_len);
}
