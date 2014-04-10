#include <string.h>
#include <poll.h>
#include "http.h"
#include "ws.h"

#define WS_GUID "258EAFA5E91447DA95CAC5AB0DC85B11"
#define HTTP_400 "HTTP/1.1 400 Bad Request\r\nSec-WebSocket-Version: 13\r\nContent-Length: 0\r\n\r\n"

static const char *FLD_SEC_WS_VER_VAL="13";

static int is_valid_ws_ver(http_req_t *hr);

static int
is_valid_ws_ver(http_req_t *hr)
{
  trim(&(hr->sec_ws_ver));
  if (0!=strcmp(hr->sec_ws_ver.start,
                FLD_SEC_WS_VER_VAL))
    return 0;

  return 1;
}

int
ws_on_handshake(wschild_conn_t *conn, http_req_t *hr)
{
  if (0>(is_valid_ws_ver(hr)))
    {
      if (0>http_prepare_response(conn->buf_out, HTTP_400))
        return -1;

      buf_clear(conn->buf_in);
      buf_flip(conn->buf_out);
      conn->pfd->events|=POLLOUT;

      return 1;
    }

  /* handshake syntactically and semantically correct */

  buf_clear(conn->buf_in);

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
