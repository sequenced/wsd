#include <string.h>
#include <poll.h>
#include <stdlib.h>
#include <assert.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include "http.h"
#include "ws.h"

#define SCRATCH_SIZE 64
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_ACCEPT_KEY_LEN 28
#define HTTP_400 "HTTP/1.1 400 Bad Request\r\nSec-WebSocket-Version: 13\r\nContent-Length: 0\r\n\r\n"
#define HTTP_101 "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "

static const char *FLD_SEC_WS_VER_VAL="13";

static int is_valid_ver(http_req_t *hr);
static int prepare_handshake(buf_t *b, http_req_t *hr);
static int generate_accept_val(buf_t *b, http_req_t *hr);

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
  BIO_flush(b64);
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
  if (0>(is_valid_ver(hr)))
    {
      if (0>http_prepare_response(conn->buf_out, HTTP_400))
        return -1;

      buf_clear(conn->buf_in);
      buf_flip(conn->buf_out);
      conn->pfd->events|=POLLOUT;

      return 1;
    }

  /* handshake syntactically and semantically correct */

  assert(buf_pos(conn->buf_out)==0);

  if (0>prepare_handshake(conn->buf_out, hr))
    {
      if (0>http_prepare_response(conn->buf_out, HTTP_500))
        return -1;
    }

  buf_clear(conn->buf_in);
  buf_flip(conn->buf_out);
  conn->pfd->events|=POLLOUT;

  return 1;
}

int
on_read(wschild_conn_t *conn)
{
  return -1;
}

int
on_write(wschild_conn_t *conn)
{
  return -1;
}
