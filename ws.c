#include <string.h>
#include <poll.h>
#include <stdlib.h>
#include <assert.h>
#include <endian.h>
#include <sys/types.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include "http.h"
#include "ws.h"
#include "chat1.h"

#define HTTP_400 "HTTP/1.1 400 Bad Request\r\nSec-WebSocket-Version: 13\r\nContent-Length: 0\r\n\r\n"
#define HTTP_101 "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "

#define SCRATCH_SIZE 64

#define WS_MASKING_KEY_LEN 4
#define WS_FRAME_LEN       6
#define WS_FRAME_LEN16     8
#define WS_FRAME_LEN64     16
#define WS_ACCEPT_KEY_LEN  28
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_VER "Sec-WebSocket-Version: "
#define WS_PROTO "Sec-WebSocket-Protocol: "
#define FIN_BIT(byte) (0x80&byte)
#define RSV1_BIT(byte) (0x40&byte)
#define RSV2_BIT(byte) (0x20&byte)
#define RSV3_BIT(byte) (0x10&byte)
#define OPCODE(byte) (0xf&byte)
#define MASK_BIT(byte) (0x80&byte)
#define PAYLOAD_LEN(byte) (unsigned long)(0x7f&byte)
/* #define PAYLOAD_LEN16(frame)                                            \ */
/*   ((unsigned long int)be16toh(*(unsigned short*)(frame->byte3))) */
/* #define PAYLOAD_LEN64(frame)                                    \ */
/*   ((unsigned long int)be64toh(*(unsigned long*)(frame->byte3))) */
#define MASKING_KEY(p) *((unsigned int*)(p))
/* #define MASKING_KEY16(frame) (unsigned int)*(unsigned int*)(frame->byte5) */
/* #define MASKING_KEY64(frame) (unsigned int)*(unsigned int*)(frame->byte11) */

extern const wsd_config_t *wsd_cfg;

static const char *FLD_SEC_WS_VER_VAL="13";

static int is_valid_ver(http_req_t *hr);
static int is_valid_proto(http_req_t *hr);
static int prepare_handshake(buf_t *b, http_req_t *hr);
static int generate_accept_val(buf_t *b, http_req_t *hr);
static int fill_in_wsframe_details(buf_t *b, wsframe_t *wsf);
static void decode(buf_t *b, wsframe_t *wsf);
static void on_close(buf_t *b, wsframe_t *wsf);
static void on_ping(buf_t *b, wsframe_t *wsf);
static void on_pong(buf_t *b, wsframe_t *wsf);
static int start_closing_handshake(wschild_conn_t *conn, wsframe_t *wsf);

static int
is_valid_ver(http_req_t *hr)
{
  trim(&(hr->sec_ws_ver));
  if (0!=strcmp(hr->sec_ws_ver.start,
                FLD_SEC_WS_VER_VAL))
    return 0;

  return 1;
}

static int
prepare_handshake(buf_t *b, http_req_t *hr)
{
  int len=strlen(HTTP_101);
  if (buf_len(b)<len)
    return -1;
  int old_pos=buf_pos(b);
  strcpy(buf_ref(b), HTTP_101);
  buf_fwd(b, len);

  len=WS_ACCEPT_KEY_LEN+2; /* +2: `\r\n' */
  if (buf_len(b)<len)
    goto error;
  if (0>generate_accept_val(b, hr))
    goto error;

  len=strlen(WS_VER)+strlen(FLD_SEC_WS_VER_VAL)+2; /* +2 `\r\n' */
  if (buf_len(b)<len)
    goto error;
  strcpy(buf_ref(b), WS_VER);
  buf_fwd(b, strlen(WS_VER));
  strcpy(buf_ref(b), FLD_SEC_WS_VER_VAL);
  buf_fwd(b, strlen(FLD_SEC_WS_VER_VAL));
  buf_put(b, '\r');
  buf_put(b, '\n');

  /* TODO for now echo back requested protocol */
  trim(&(hr->sec_ws_proto));
  len=strlen(WS_PROTO)+hr->sec_ws_proto.len+2; /* +2 `\r\n' */
  if (buf_len(b)<len)
    goto error;
  strcpy(buf_ref(b), WS_PROTO);
  buf_fwd(b, strlen(WS_PROTO));
  strncpy(buf_ref(b), hr->sec_ws_proto.start, hr->sec_ws_proto.len);
  buf_fwd(b, hr->sec_ws_proto.len);
  buf_put(b, '\r');
  buf_put(b, '\n');

  /* terminating response as per RFC2616 section 6 */
  if (buf_len(b)<2)
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
  strcpy((char*)(scratch+hr->sec_ws_key.len), WS_GUID);

  unsigned char *md=SHA1((unsigned char*)scratch,
                         strlen(scratch),
                         NULL);

  BIO *bio, *b64;
  BUF_MEM *p;
  b64=BIO_new(BIO_f_base64());
  bio=BIO_new(BIO_s_mem());
  b64=BIO_push(b64, bio);
  BIO_write(b64, md, SHA_DIGEST_LENGTH);
  (void)BIO_flush(b64);
  BIO_get_mem_ptr(b64, &p);
  memcpy(buf_ref(b), p->data, p->length-1);
  buf_fwd(b, p->length-1);

  BIO_free_all(b64);

  buf_put(b, '\r');
  buf_put(b, '\n');

  return 1;
}

int
ws_on_handshake(wschild_conn_t *conn, http_req_t *hr)
{
  if (!is_valid_ver(hr)
      || !is_valid_proto(hr))
    {
      if (0>http_prepare_response(conn->buf_out, HTTP_400))
        return -1;

      buf_clear(conn->buf_in);
      buf_flip(conn->buf_out);
      conn->pfd->events|=POLLOUT;

      return 1;
    }

  /* TODO check requested resource */

  /* handshake syntactically and semantically correct */

  assert(buf_pos(conn->buf_out)==0);

  if (0>prepare_handshake(conn->buf_out, hr))
    {
      if (0>http_prepare_response(conn->buf_out, HTTP_500))
        return -1;
    }

  /* switch into websocket mode */
  conn->on_read=ws_on_read;
  conn->on_write=ws_on_write;
  /* TODO lookup protocol */
  conn->on_data_frame=chat1_on_frame;

  buf_clear(conn->buf_in);
  buf_flip(conn->buf_out);
  conn->pfd->events|=POLLOUT;

  return 1;
}

int
ws_on_read(wschild_conn_t *conn)
{
  buf_flip(conn->buf_in);

  if (buf_len(conn->buf_in)<WS_FRAME_LEN)
    /* need at least WS_FRAME_LEN bytes; see RFC6455 section 5.2 */
    /* TODO retry */
    return 1;

  wsframe_t wsf;
  memset(&wsf, 0x0, sizeof(wsframe_t));
  wsf.byte1=buf_get(conn->buf_in);
  wsf.byte2=buf_get(conn->buf_in);

  /* see RFC6455 section 5.2 */
  if (RSV1_BIT(wsf.byte1)!=0
      || RSV2_BIT(wsf.byte1)!=0
      || RSV3_BIT(wsf.byte1)!=0
      || MASK_BIT(wsf.byte2)==0)
    {
      /* TODO fail connection */
    }

  if (0>fill_in_wsframe_details(conn->buf_in, &wsf))
    /* TODO retry */
    return 1;

  if (wsd_cfg->verbose)
    printf("ws frame: 0x%hhx, 0x%hhx, 0x%x (opcode)\n",
           wsf.byte1,
           wsf.byte2,
           OPCODE(wsf.byte1));

  /* TODO check that payload64 has left-most bit off */

  if (buf_len(conn->buf_in)<wsf.payload_len)
    /* TODO retry */
    return 1;

  decode(conn->buf_in, &wsf);

  if (0x8==OPCODE(wsf.byte1))
    on_close(conn->buf_in, &wsf);
  else if (0x9==OPCODE(wsf.byte1))
    on_ping(conn->buf_in, &wsf);
  else if (0xa==OPCODE(wsf.byte1))
    on_pong(conn->buf_in, &wsf);
  else if (0x0==OPCODE(wsf.byte1)
           || 0x1==OPCODE(wsf.byte1)
           || 0x2==OPCODE(wsf.byte1))
    conn->on_data_frame(conn, &wsf);
  else
    /* unknown opcode */
    start_closing_handshake(conn, &wsf);

  assert(buf_pos(conn->buf_in)==0);    

  return 1;
}

int
ws_on_write(wschild_conn_t *conn)
{
  return -1;
}

static void
decode(buf_t *buf, wsframe_t *wsf)
{
  int pos=buf_pos(buf);
  int j, i;
  for (i=0; i<wsf->payload_len; i++)
    {
      char b=buf_get(buf);
      j=i%4;
      unsigned char mask;
      if (j==0)
        mask=(unsigned char)(wsf->masking_key&0x000000ff);
      else if (j==1)
        mask=(unsigned char)((wsf->masking_key&0x0000ff00)>>8);
      else if (j==2)
        mask=(unsigned char)((wsf->masking_key&0x00ff0000)>>16);
      else
        mask=(unsigned char)((wsf->masking_key&0xff000000)>>24);

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
  wsf->payload_len=PAYLOAD_LEN(wsf->byte2);
  if (wsf->payload_len<126)
    {
      if (buf_len(b)<4)
        return -1;

      wsf->masking_key=buf_get_int(b);
    }
  else if (wsf->payload_len==126)
    {
      if (buf_len(b)<(2+4))
        return -1;

      wsf->payload_len=buf_get_short(b);
      wsf->masking_key=buf_get_int(b);
    }
  else if (wsf->payload_len==127)
    {
      if (buf_len(b)<(2+8))
        return -1;

      wsf->payload_len=buf_get_long(b);
      wsf->masking_key=buf_get_int(b);
    }

  return 0;
}

void
on_close(buf_t *b, wsframe_t *wsf)
{

}

void
on_ping(buf_t *b, wsframe_t *wsf)
{

}

void
on_pong(buf_t *b, wsframe_t *wsf)
{

}

int
start_closing_handshake(wschild_conn_t *conn, wsframe_t *wsf)
{
  return -1;
}

int
is_valid_proto(http_req_t *hr)
{
  trim(&(hr->sec_ws_proto));
  /* TODO lookup supported protocols */
  if (0!=strcmp(hr->sec_ws_proto.start,
                "chat1"))
    return 0;

  return 1;
}
