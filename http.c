#include <stdio.h>
#include "http.h"

static const char *GET="GET";
static const char *HTTP_VER="HTTP/1.1";

/* as per RFC2616 section 5.1 */
typedef struct
{
  char_range_t method;
  char_range_t req_uri;
  char_range_t http_ver;
} http_req_line_t;

static int is_valid_request_line(http_req_line_t *hrl);

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
  buf_flip(conn->buf);

  http_req_line_t hrl;
  int rv=parse_req_line(conn->buf, &hrl);
  if (0>rv)
    /* parse error */
    return -1;
  else if (0==rv)
    {
      /* not enough data */
      buf_flip(conn->buf);
      return 1;
    }

  /* three tokens terminated with `\r\n' (see RFC2616 section 5.1) */
  if (!is_valid_request_line(&hrl))
    /* TODO return 500 or someting */
    ;

  printf("%s", buf_ref(conn->buf));

  char *uri=malloc(hrl.req_uri.len+1);
  memset(uri, 0x0, hrl.req_uri.len+1);
  strncpy(uri, hrl.req_uri.start, hrl.req_uri.len);
  printf("req_uri=`%s'\n", uri);
  free(uri);
  char *method=malloc(hrl.method.len+1);
  memset(method, 0x0, hrl.method.len+1);
  strncpy(method, hrl.method.start, hrl.method.len);
  printf("method=`%s'\n", method);
  free(method);
  char *http_ver=malloc(hrl.http_ver.len+1);
  memset(http_ver, 0x0, hrl.http_ver.len+1);
  strncpy(http_ver, hrl.http_ver.start, hrl.http_ver.len);
  printf("http_ver=`%s'\n", http_ver);
  free(http_ver);


  buf_clear(conn->buf);
  return 1;
}

int
http_on_write(wschild_conn_t *conn)
{
  return -1;
}

static int
is_valid_request_line(http_req_line_t *hrl)
{
  return 0;
}
