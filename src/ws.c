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
static int is_valid_proto(http_req_t *hr, location_config_t *loc);
static int prepare_handshake(buf_t *b, http_req_t *hr);
static int generate_accept_val(buf_t *b, http_req_t *hr);
static int fill_in_wsframe_details(buf_t *b, wsframe_t *wsf);
static void decode(buf_t *b, wsframe_t *wsf);
static int on_close_frame(wsconn_t *conn, buf_t *b);
static int on_ping_frame(buf_t *b, wsframe_t *wsf);
static int on_pong_frame(buf_t *b, wsframe_t *wsf);
static int start_closing_handshake(wsconn_t *conn, wsframe_t *wsf, int status);
static int prepare_close_frame(buf_t *b, int status);
static int dispatch(wsconn_t *conn, wsframe_t *wsf);
static location_config_t *lookup_location(http_req_t *hr);

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
prepare_handshake(buf_t *b, http_req_t *hr)
{
     int len = strlen(HTTP_101);
     if (buf_len(b) < len)
          return -1;
     int old_pos = buf_pos(b);
     strcpy(buf_ref(b), HTTP_101);
     buf_fwd(b, len);

     len = WS_ACCEPT_KEY_LEN + 2; /* +2: `\r\n' */
     if (buf_len(b) < len)
          goto error;
     if (0>generate_accept_val(b, hr))
          goto error;

     len = strlen(WS_VER) + strlen(FLD_SEC_WS_VER_VAL) + 2; /* +2 `\r\n' */
     if (buf_len(b) < len)
          goto error;
     strcpy(buf_ref(b), WS_VER);
     buf_fwd(b, strlen(WS_VER));
     strcpy(buf_ref(b), FLD_SEC_WS_VER_VAL);
     buf_fwd(b, strlen(FLD_SEC_WS_VER_VAL));
     buf_put(b, '\r');
     buf_put(b, '\n');

     /* TODO for now echo back requested protocol */
     trim(&(hr->sec_ws_proto));
     len = strlen(WS_PROTO) + hr->sec_ws_proto.len + 2; /* +2 `\r\n' */
     if (buf_len(b) < len)
          goto error;
     strcpy(buf_ref(b), WS_PROTO);
     buf_fwd(b, strlen(WS_PROTO));
     strncpy(buf_ref(b), hr->sec_ws_proto.start, hr->sec_ws_proto.len);
     buf_fwd(b, hr->sec_ws_proto.len);
     buf_put(b, '\r');
     buf_put(b, '\n');

     /* terminating response as per RFC2616 section 6 */
     if (buf_len(b) < 2)
          goto error;
     buf_put(b, '\r');
     buf_put(b, '\n');

     return 1;

error:
     buf_rwnd(b, buf_pos(b)-old_pos);
     return -1;
}

static int
generate_accept_val(buf_t *b, http_req_t *hr)
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

     BIO *bio, *b64;
     BUF_MEM *p;
     b64 = BIO_new(BIO_f_base64());
     bio = BIO_new(BIO_s_mem());
     b64 = BIO_push(b64, bio);
     BIO_write(b64, md, SHA_DIGEST_LENGTH);
     (void)BIO_flush(b64);
     BIO_get_mem_ptr(b64, &p);
     memcpy(buf_ref(b), p->data, p->length - 1);
     buf_fwd(b, p->length - 1);

     BIO_free_all(b64);

     buf_put(b, '\r');
     buf_put(b, '\n');

     return 1;
}

int
ws_on_handshake(wsconn_t *conn, http_req_t *hr)
{
     if (!is_valid_ver(hr))
          goto bad;

     location_config_t *loc;
     if (!(loc = lookup_location(hr)))
          goto bad;

     if (!is_valid_proto(hr, loc))
          goto bad;

     /* handshake syntactically and semantically correct */

     assert(buf_pos(conn->buf_out) == 0);

     if (0 > prepare_handshake(conn->buf_out, hr))
     {
          if (0>http_prepare_response(conn->buf_out, HTTP_500))
               return -1;
     }

     /* "switch" into websocket mode */
     conn->on_read = ws_on_read;
     conn->on_write = ws_on_write;

     /* hook up protocol handlers */
     conn->on_data_frame = loc->on_data_frame;
     conn->on_close = loc->on_close;

     /* link connection with the location it serves */
     conn->location = loc;

     /* application protocol can reject */
     if (0 > loc->on_open(wsd_cfg, conn))
          goto bad;

     goto ok;

bad:
     if (0 > http_prepare_response(conn->buf_out, HTTP_400))
          return (-1);
     conn->close_on_write = 1;

ok:
     buf_clear(conn->buf_in);
     conn->write = 1;

     return 1;
}

int
ws_on_read(wsconn_t *conn)
{
     buf_flip(conn->buf_in);

     if (buf_len(conn->buf_in) < WS_MASKED_FRAME_LEN)
     {
          /* need at least WS_MASKED_FRAME_LEN bytes; see RFC6455 section 5.2 */
          buf_flip(conn->buf_in);
          return 1;
     }

     int old_pos = buf_pos(conn->buf_in);

     wsframe_t wsf;
     memset(&wsf, 0x0, sizeof(wsframe_t));
     wsf.byte1 = buf_get(conn->buf_in);
     wsf.byte2 = buf_get(conn->buf_in);

     /* see RFC6455 section 5.2 */
     if (RSV1_BIT(wsf.byte1) != 0
         || RSV2_BIT(wsf.byte1) != 0
         || RSV3_BIT(wsf.byte1) != 0
         || MASK_BIT(wsf.byte2) == 0)
     {
          /* TODO fail connection */
     }

     if (0>fill_in_wsframe_details(conn->buf_in, &wsf))
     {
          buf_rwnd(conn->buf_in, buf_pos(conn->buf_in)-old_pos);
          buf_flip(conn->buf_in);
          return 1;
     }

     if (LOG_VVERBOSE <= wsd_cfg->verbose)
          printf("ws: on_read: sfd=%d: frame: 0x%hhx, 0x%hhx, opcode=0x%x, len=%lu\n",
                 conn->sfd,
                 wsf.byte1,
                 wsf.byte2,
                 OPCODE(wsf.byte1),
                 wsf.payload_len);

     /* TODO check that payload64 has left-most bit off */

     if (buf_len(conn->buf_in) < wsf.payload_len)
     {
          buf_rwnd(conn->buf_in, buf_pos(conn->buf_in)-old_pos);
          buf_flip(conn->buf_in);
          return 1;
     }

     decode(conn->buf_in, &wsf);
     return dispatch(conn, &wsf);
}

int
dispatch(wsconn_t *conn, wsframe_t *wsf)
{
     int rv;
     buf_t slice;
     buf_slice(&slice, conn->buf_in, wsf->payload_len);

     if (WS_CLOSE_FRAME == OPCODE(wsf->byte1))
          rv = on_close_frame(conn, &slice);
     else if (WS_PING_FRAME == OPCODE(wsf->byte1))
          rv = on_ping_frame(&slice, wsf);
     else if (WS_PONG_FRAME == OPCODE(wsf->byte1))
          rv = on_pong_frame(&slice, wsf);
     else if (0x0 == OPCODE(wsf->byte1)
              || WS_TEXT_FRAME == OPCODE(wsf->byte1)
              || WS_BINARY_FRAME == OPCODE(wsf->byte1))
          rv = conn->on_data_frame(conn, wsf, &slice, conn->buf_out);
     else
     {
          if (LOG_VVVERBOSE <= wsd_cfg->verbose)
               printf("jen: unknown opcode: sfd=%d: 0x%x\n",
                      conn->sfd,
                      OPCODE(wsf->byte1));

          /* unknown opcode */
          rv = start_closing_handshake(conn, wsf, WS_1011);
     }

     /* clear buffer by consuming this frame's payload */
     buf_fwd(conn->buf_in, wsf->payload_len);
     buf_compact(conn->buf_in);

     return rv;
}

int
ws_on_write(wsconn_t *conn)
{
     return -1;
}

static void
decode(buf_t *buf, wsframe_t *wsf)
{
     int pos = buf_pos(buf);
     int j, i;
     for (i = 0; i < wsf->payload_len; i++)
     {
          char b = buf_get(buf);
          j = i%4;
          unsigned char mask;
          if (j == 0)
               mask = (unsigned char)(wsf->masking_key&0x000000ff);
          else if (j == 1)
               mask = (unsigned char)((wsf->masking_key&0x0000ff00)>>8);
          else if (j == 2)
               mask = (unsigned char)((wsf->masking_key&0x00ff0000)>>16);
          else
               mask = (unsigned char)((wsf->masking_key&0xff000000)>>24);

          b^=mask;

          /* decode in-place */
          buf_rwnd(buf, 1);
          buf_put(buf, b);
     }

     buf_set_pos(buf, pos);
}

static int
fill_in_wsframe_details(buf_t *b, wsframe_t *wsf)
{
     /* extended payload length; see RFC6455 section 5.2 */
     wsf->payload_len = PAYLOAD_LEN(wsf->byte2);
     if (wsf->payload_len < 126)
     {
          if (buf_len(b) < 4)
               return -1;

          wsf->masking_key = buf_get_int(b);
     }
     else if (wsf->payload_len == 126)
     {
          if (buf_len(b) < (2 + 4))
               return -1;

          wsf->payload_len = be16toh(buf_get_short(b));
          wsf->masking_key = buf_get_int(b);
     }
     else if (wsf->payload_len == 127)
     {
          if (buf_len(b) < (2 + 8))
               return -1;

          wsf->payload_len = be64toh(buf_get_long(b));
          wsf->masking_key = buf_get_int(b);
     }

     return 0;
}

static int
on_close_frame(wsconn_t *conn, buf_t *b)
{
     int rv = 1;
     unsigned short status = 0;
     if (WS_FRAME_STATUS_LEN<=buf_len(b))
          status = be16toh(buf_get_short(b));

     if (LOG_VVERBOSE <= wsd_cfg->verbose)
          printf("ws: on_close_frame: sfd=%d: status=%u\n",
                 conn->sfd,
                 status);

     if (!conn->closing)
     {
          if (0>prepare_close_frame(conn->buf_out, status))
               return -1;

          conn->write = 1;
          conn->close_on_write = 1;
          conn->closing = 1;
     }
     else
          /* closing handshake completed (reply to our earlier close frame) */
          rv = -1;

     /* don't process data after close frame, see RFC6455 section 5.5.1 */
     buf_clear(conn->buf_in);

     return rv;
}

static int
on_ping_frame(buf_t *b, wsframe_t *wsf)
{
     return -1;
}

static int
on_pong_frame(buf_t *b, wsframe_t *wsf)
{
     return -1;
}

static int
prepare_close_frame(buf_t *b, int status)
{
     if (buf_len(b) < WS_UNMASKED_FRAME_LEN)
          return -1;

     char byte1 = 0, byte2 = 0;
     SET_FIN_BIT(byte1);
     SET_OPCODE(byte1, WS_CLOSE_FRAME);

     if (0 < status)
          /* echo back status code; see RFC6455 section 5.5.1 */
          SET_PAYLOAD_LEN(byte2, 2); /* 2 = status code typed unsigned short */

     buf_put(b, byte1);
     buf_put(b, byte2);

     if (0 < status)
          buf_put_short(b, htobe16(status));

     return 0;
}

static int
start_closing_handshake(wsconn_t *conn, wsframe_t *wsf, int status)
{
     if (conn->closing)
          /* closing handshake in progress */
          return 1;

     if (0>prepare_close_frame(conn->buf_out, status))
          return -1;

     conn->write = 1;
     conn->closing = 1;

     if (LOG_VVERBOSE <= wsd_cfg->verbose)
          printf("ws: starting_closing_handshake: sfd=%d: status=%u\n",
                 conn->sfd,
                 status);

     return 1;
}

static int
is_valid_proto(http_req_t *hr, location_config_t *loc)
{
     trim(&(hr->sec_ws_proto));
     if (0 != strncmp(hr->sec_ws_proto.start,
                      loc->protocol,
                      hr->sec_ws_proto.len))
          return 0;

     return 1;
}

static location_config_t *
lookup_location(http_req_t *hr)
{
     location_config_t *rv = 0, *cursor;
     list_for_each_entry(cursor, &wsd_cfg->location_list, list_head)
     {
          if (0 == strncasecmp(hr->req_target.start,
                               cursor->url,
                               hr->req_target.len))
               rv = cursor;
     }

     return rv;
}
