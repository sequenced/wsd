#include "config.h"
#include <string.h>
#include <poll.h>
#include <stdlib.h>
#include <assert.h>
#ifdef HAVE_ENDIAN_H
#include <endian.h>
#else
/* TODO */
#endif
#include <sys/types.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include "http.h"
#include "ws.h"
#include "jen.h"

#define HTTP_400              "HTTP/1.1 400 Bad Request\r\nSec-WebSocket-Version: 13\r\nContent-Length: 0\r\n\r\n"
#define HTTP_101              "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
#define SCRATCH_SIZE          64
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

#define FIN_BIT(byte) (0x80&byte)
#define SET_FIN_BIT(byte) (byte |= 0x80)
#define RSV1_BIT(byte) (0x40&byte)
#define RSV2_BIT(byte) (0x20&byte)
#define RSV3_BIT(byte) (0x10&byte)
#define OPCODE(byte) (0xf&byte)
#define SET_OPCODE(byte, val) (byte |= (0xf&val))
#define MASK_BIT(byte) (0x80&byte)
#define PAYLOAD_LEN(byte) (unsigned long)(0x7f&byte)
#define SET_PAYLOAD_LEN(byte, val) (byte |= (0x7f&val))
/* #define PAYLOAD_LEN16(frame)                                            \ */
/*   ((unsigned long int)be16toh(*(unsigned short*)(frame->byte3))) */
/* #define PAYLOAD_LEN64(frame)                                    \ */
/*   ((unsigned long int)be64toh(*(unsigned long*)(frame->byte3))) */
#define MASKING_KEY(p) *((unsigned int*)(p))
/* #define MASKING_KEY16(frame) (unsigned int)*(unsigned int*)(frame->byte5) */
/* #define MASKING_KEY64(frame) (unsigned int)*(unsigned int*)(frame->byte11) */

extern const wsd_config_t *wsd_cfg;

static const char *FLD_SEC_WS_VER_VAL = "13";

static int is_valid_ver(http_req_t *hr);
static int is_valid_proto(http_req_t *hr);
static int prepare_handshake(buf2_t *b, http_req_t *hr);
static int generate_accept_val(buf2_t *b, http_req_t *hr);
static int fill_in_wsframe_details(buf2_t *b, wsframe_t *wsf);
static void decode(buf2_t *buf, wsframe_t *wsf);
static int close_frame(ep_t *ep, wsframe_t *b);
static int ping_frame(ep_t *b, wsframe_t *wsf);
static int pong_frame(ep_t *b, wsframe_t *wsf);
static int start_closing_handshake(ep_t *ep, wsframe_t *wsf, int status);
static int prepare_close_frame(buf2_t *b, int status);
static int dispatch(ep_t *ep, wsframe_t *wsf);

static int
is_valid_ver(http_req_t *hr)
{
     trim(&(hr->sec_ws_ver));
     if (0 != strncmp(hr->sec_ws_ver.start,
                      FLD_SEC_WS_VER_VAL,
                      hr->sec_ws_ver.len))
          return 0;

     return 1;
}

static int
prepare_handshake(buf2_t *b, http_req_t *hr)
{
     int len = strlen(HTTP_101);
     if (buf_write_sz(b) < len)
          return -1;
     int old_wrpos = b->wrpos;
     strcpy(&b->p[b->wrpos], HTTP_101);
     b->wrpos += len;

     len = WS_ACCEPT_KEY_LEN + 2; /* +2: `\r\n' */
     if (buf_write_sz(b) < len)
          goto error;
     if (0 > generate_accept_val(b, hr))
          goto error;
     len = strlen(WS_VER) + strlen(FLD_SEC_WS_VER_VAL) + 2; /* +2 `\r\n' */
     if (buf_write_sz(b) < len)
          goto error;
     strcpy(&b->p[b->wrpos], WS_VER);
     b->wrpos += strlen(WS_VER);
     strcpy(&b->p[b->wrpos], FLD_SEC_WS_VER_VAL);
     b->wrpos += strlen(FLD_SEC_WS_VER_VAL);
     buf_put(b, (char)'\r');
     buf_put(b, (char)'\n');

     /* TODO for now echo back requested protocol */
     trim(&(hr->sec_ws_proto));
     len = strlen(WS_PROTO) + hr->sec_ws_proto.len + 2; /* +2 `\r\n' */
     if (buf_write_sz(b) < len)
          goto error;
     strcpy(&b->p[b->wrpos], WS_PROTO);
     b->wrpos += strlen(WS_PROTO);
     strncpy(&b->p[b->wrpos], hr->sec_ws_proto.start, hr->sec_ws_proto.len);
     b->wrpos += hr->sec_ws_proto.len;
     buf_put(b, (char)'\r');
     buf_put(b, (char)'\n');

     /* terminating response as per RFC2616 section 6 */
     if (buf_write_sz(b) < 2)
          goto error;
     buf_put(b, (char)'\r');
     buf_put(b, (char)'\n');

     return 0;

error:
     b->wrpos = old_wrpos;
     return (-1);
}

static int
generate_accept_val(buf2_t *b, http_req_t *hr)
{
     trim(&(hr->sec_ws_key));

     /* concatenate as per RFC6455 section 4.2.2 */
     char scratch[SCRATCH_SIZE];
     memset(scratch, 0x0, SCRATCH_SIZE);
     strncpy(scratch, hr->sec_ws_key.start, hr->sec_ws_key.len);
     strcpy((char*)(scratch + hr->sec_ws_key.len), WS_GUID);

     unsigned char *md = SHA1((unsigned char*)scratch,
                              strlen(scratch),
                              NULL);
     A(md);

     BIO *bio, *b64;
     BUF_MEM *p;
     b64 = BIO_new(BIO_f_base64());
     bio = BIO_new(BIO_s_mem());
     b64 = BIO_push(b64, bio);
     BIO_write(b64, md, SHA_DIGEST_LENGTH);
     (void)BIO_flush(b64);
     BIO_get_mem_ptr(b64, &p);
     memcpy(&b->p[b->wrpos], p->data, p->length - 1);
     b->wrpos += p->length - 1;

     BIO_free_all(b64);

     buf_put(b, (char)'\r');
     buf_put(b, (char)'\n');

     return 1;
}

int
ws_handshake(ep_t *ep, http_req_t *hr)
{
     if (!is_valid_ver(hr))
          goto bad;

     /* TODO check location */

     if (!is_valid_proto(hr))
          goto bad;

     /* handshake syntactically and semantically correct */

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     AN(buf_write_sz(ep->snd_buf));
     if (0 > prepare_handshake(ep->snd_buf, hr)) {
          if (0 > http_prepare_response(ep->snd_buf, HTTP_500))
               return (-1);
     }

     /* "switch" into websocket mode */
     ep->proto.recv = ws_recv;
     ep->proto.data_frame = jen_data_frame;
     ep->proto.open = jen_open;

     if (0 > ep->proto.open()) {
          goto bad;
     }

     goto ok;

bad:
     if (0 > http_prepare_response(ep->snd_buf, HTTP_400))
          return (-1);

     ep->close_on_write = 1;

ok:
     buf_reset(ep->rcv_buf);

     return 0;
}

int
ws_recv(ep_t *ep)
{
     AN(buf_read_sz(ep->rcv_buf));

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, read_sz=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd,
                 buf_read_sz(ep->rcv_buf));
     }

     if (buf_read_sz(ep->rcv_buf) < WS_MASKED_FRAME_LEN) {
          /* need WS_MASKED_FRAME_LEN bytes; see RFC6455 section 5.2 */
          return 1;
     }

     int old_rdpos = ep->rcv_buf->rdpos;

     wsframe_t wsf;
     memset(&wsf, 0x0, sizeof(wsframe_t));
     buf_get(ep->rcv_buf, wsf.byte1);
     buf_get(ep->rcv_buf, wsf.byte2);

     /* see RFC6455 section 5.2 */
     if (RSV1_BIT(wsf.byte1) != 0
         || RSV2_BIT(wsf.byte1) != 0
         || RSV3_BIT(wsf.byte1) != 0
         || MASK_BIT(wsf.byte2) == 0) {
          /* TODO fail connection */
     }

     if (0 > fill_in_wsframe_details(ep->rcv_buf, &wsf)) {
          ep->rcv_buf->rdpos = old_rdpos;
          return 1;
     }

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("\t%s: fd=%d, frame: 0x%hhx, 0x%hhx, opcode=0x%x, len=%lu\n",
                 __func__,
                 ep->fd,
                 wsf.byte1,
                 wsf.byte2,
                 OPCODE(wsf.byte1),
                 wsf.payload_len);
     }

     /* TODO check that payload64 has left-most bit off */

     if (buf_read_sz(ep->rcv_buf) < wsf.payload_len) {
          ep->rcv_buf->rdpos = old_rdpos;
          return 1;
     }

     decode(ep->rcv_buf, &wsf);
     return dispatch(ep, &wsf);
}

int
dispatch(ep_t *ep, wsframe_t *wsf)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, payload_len=%lu\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd,
                 wsf->payload_len);
     }

     int rv;
     if (WS_CLOSE_FRAME == OPCODE(wsf->byte1))
          rv = close_frame(ep, wsf);
     else if (WS_PING_FRAME == OPCODE(wsf->byte1))
          rv = ping_frame(ep, wsf);
     else if (WS_PONG_FRAME == OPCODE(wsf->byte1))
          rv = pong_frame(ep, wsf);
     else if (0x0 == OPCODE(wsf->byte1)
              || WS_TEXT_FRAME == OPCODE(wsf->byte1)
              || WS_BINARY_FRAME == OPCODE(wsf->byte1))
          rv = ep->proto.data_frame(ep, wsf);
     else {
          if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: unknown opcode: fd=%d: 0x%x\n",
                      __func__,
                      ep->fd,
                      OPCODE(wsf->byte1));
          }

          /* unknown opcode */
          rv = start_closing_handshake(ep, wsf, WS_1011);
     }

/*      /\* clear buffer by consuming this frame's payload *\/ */
/*      buf_fwd(conn->buf_in, wsf->payload_len); */
/*      buf_compact(conn->buf_in); */

     /* return rv; */
     return 0;
}

static void
decode(buf2_t *buf, wsframe_t *wsf)
{
     int old_rdpos = buf->rdpos;
     int j, i;
     for (i = 0; i < wsf->payload_len; i++) {
          char b;
          buf_get(buf, b);
          j = i % 4;

          unsigned char mask;
          if (j == 0)
               mask = (unsigned char)(wsf->masking_key & 0x000000ff);
          else if (j == 1)
               mask = (unsigned char)((wsf->masking_key & 0x0000ff00) >> 8);
          else if (j == 2)
               mask = (unsigned char)((wsf->masking_key & 0x00ff0000) >> 16);
          else
               mask = (unsigned char)((wsf->masking_key & 0xff000000) >> 24);

          b ^= mask;

          /* decode in-place */
          buf->p[buf->rdpos - 1] = b;
     }

     buf->rdpos = old_rdpos;
}

static int
fill_in_wsframe_details(buf2_t *b, wsframe_t *wsf)
{
     /* extended payload length; see RFC6455 section 5.2 */
     wsf->payload_len = PAYLOAD_LEN(wsf->byte2);
     if (wsf->payload_len < 126) {
          if (buf_read_sz(b) < 4)
               return -1;

          buf_get(b, wsf->masking_key);
     } else if (wsf->payload_len == 126) {
          if (buf_read_sz(b) < (2 + 4))
               return -1;

          buf_get(b, wsf->payload_len);
          wsf->payload_len = be16toh(wsf->payload_len);
          buf_get(b, wsf->masking_key);
     } else if (wsf->payload_len == 127) {
          if (buf_read_sz(b) < (2 + 8))
               return -1;

          buf_get(b, wsf->payload_len);
          wsf->payload_len = be64toh(wsf->payload_len);
          buf_get(b, wsf->masking_key);
     }

     return 0;
}

static int
close_frame(ep_t *ep, wsframe_t *wsf)
{
     int rv = 0;
     unsigned short status = 0;
     if (WS_FRAME_STATUS_LEN <= buf_write_sz(ep->snd_buf)) {
          buf_get(ep->rcv_buf, status);
          status = be16toh(status);
     }

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, status=%u\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd,
                 wsf->payload_len);
     }

     if (!ep->closing) {
          if (0 > prepare_close_frame(ep->snd_buf, status))
               return -1;

          ep->close_on_write = 1;
          ep->closing = 1;
     } else {
          /* closing handshake completed (reply to our earlier close frame) */
          rv = -1;
     }

     /* don't process data after close frame, see RFC6455 section 5.5.1 */
     buf_reset(ep->rcv_buf);

     return rv;
}

static int
ping_frame(ep_t *ep, wsframe_t *wsf)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     return -1;
}

static int
pong_frame(ep_t *ep, wsframe_t *wsf)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     return -1;
}

static int
prepare_close_frame(buf2_t *b, int status)
{
     if (buf_write_sz(b) < WS_UNMASKED_FRAME_LEN)
          return -1;

     char byte1 = 0, byte2 = 0;
     SET_FIN_BIT(byte1);
     SET_OPCODE(byte1, WS_CLOSE_FRAME);

     if (0 < status)
          /* echo back status code; see RFC6455 section 5.5.1 */
          SET_PAYLOAD_LEN(byte2, 2); /* 2 = status code typed unsigned short */

     buf_put(b, (char)byte1);
     buf_put(b, (char)byte2);

     if (0 < status)
          buf_put(b, htobe16(status));

     return 0;
}

static int
start_closing_handshake(ep_t *ep, wsframe_t *wsf, int status)
{
     /* Closing handshake in progress? */
     if (ep->closing)
          return 0;

     if (0 > prepare_close_frame(ep->snd_buf, status))
          return -1;

     ep->closing = 1;

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd);
     }

     return 0;
}

static int
is_valid_proto(http_req_t *hr)
{
     trim(&(hr->sec_ws_proto));
     if (0 != strncmp(hr->sec_ws_proto.start, "jen", hr->sec_ws_proto.len))
          return 0;

     return 1;
}
