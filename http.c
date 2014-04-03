#include <string.h>
#include <stdio.h>
#include <poll.h>
#include "http.h"

#define HTTP_500 "HTTP/1.1 500 Internal Server Error\r\n\r\n"

static const char *METHOD_GET="GET";
static const char *HTTP_VER="HTTP/1.1";
static const char *URI_ANY="*";
static const char *URI_ABSOLUTE="http://";

/* as per RFC2616 section 5.1 */
typedef struct
{
  char_range_t method;
  char_range_t req_uri;
  char_range_t http_ver;
  int is_uri_abs;
} http_req_line_t;

static int is_valid_req_line_syntax(http_req_line_t *hrl);
static int prepare_500_response(buf_t *b);

static inline int
parse_el_crlf(buf_t *b)
{
  int start=buf_pos(b);
  while ('\r'!=buf_get(b) && buf_len(b));

  if (!buf_len(b))
    return 0;

  if ('\n'!=buf_get(b))
    return -1;

  return (buf_pos(b)-start)-2; /* -2: adjust for `\r\n' */
}

static inline int
parse_el(buf_t *b)
{
  int start=buf_pos(b);
  while (' '!=buf_get(b) && buf_len(b));

  if (!buf_len(b))
    return 0;

  return (buf_pos(b)-start)-1; /* -1: adjust for space */
}

static int
parse_req_line(buf_t *b, http_req_line_t *hrl)
{
  int rv;

  hrl->method.start=buf_ref(b);
  if (0>=(rv=parse_el(b)))
    return rv;
  hrl->method.len=rv;

  hrl->req_uri.start=buf_ref(b);
  if (0>=(rv=parse_el(b)))
    return rv;
  hrl->req_uri.len=rv;

  hrl->http_ver.start=buf_ref(b);
  if (0>=(rv=parse_el_crlf(b)))
    return rv;
  hrl->http_ver.len=rv;

  return 1;
}

int
http_on_read(wschild_conn_t *conn)
{
  buf_flip(conn->buf_in);

  http_req_line_t hrl;
  memset(&hrl, 0x0, sizeof(hrl));
  int rv=parse_req_line(conn->buf_in, &hrl);
  if (0>rv)
    /* parse error */
    return -1;
  else if (0==rv)
    {
      /* not enough data */
      buf_flip(conn->buf_in);
      return 1;
    }

  /* have three tokens terminated with `\r\n' (see RFC2616 section 5.1) */
  if (!is_valid_req_line_syntax(&hrl))
    {
      if (0>prepare_500_response(conn->buf_out))
        /* request wrong, can't prepare response so bail out */
        return -1;

      buf_clear(conn->buf_in);
      buf_flip(conn->buf_out);
      conn->pfd->events|=POLLOUT;
      conn->close_on_write=1;

      return 1;
    }

  printf("%s", buf_ref(conn->buf_in));

  buf_clear(conn->buf_in);
  return 1;
}

int
http_on_write(wschild_conn_t *conn)
{
  
  return -1;
}

static int
is_valid_req_line_syntax(http_req_line_t *hrl)
{
  if (0!=strncmp(hrl->method.start,
                 METHOD_GET,
                 hrl->method.len))
    return 0;

  if (0!=strncmp(hrl->http_ver.start,
                 HTTP_VER,
                 hrl->http_ver.len))
    return 0;

  /* enforce RFC2616 section 5.1.2 */
  if (1>hrl->req_uri.len)
    return 0;

  if (0==strncmp(hrl->req_uri.start,
                 URI_ANY,
                 hrl->req_uri.len))
    return 0;

  if (0==strncasecmp(hrl->req_uri.start,
                     URI_ABSOLUTE,
                     7)) /* strlen(URI_ABSOLUTE) */
    hrl->is_uri_abs=1;

  return 1;
}

static int
prepare_500_response(buf_t *b)
{
  /* see RFC2616 section 6.1.1 */
  if (buf_len(b)<(strlen(HTTP_500)))
    return -1;

  strcpy(buf_ref(b), HTTP_500);
  buf_fwd(b, strlen(HTTP_500));

  return 1;
}
