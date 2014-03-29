#include <stdio.h>
#include "http.h"

/* as per RFC2616 section 5.1 */
typedef struct
{
  char_range_t method;
  char_range_t req_uri;
  char_range_t http_ver;
} http_req_line_t;

static inline int
parse_el_crlf(buf_t *b)
{
  while ('\r'!=buf_get(b) && buf_len(b));

  if (!buf_len(b))
    return 0;

  if ('\n'!=buf_get(b))
    return -1;

  return 1;
}

static inline int
parse_el(buf_t *b)
{
  while (' '!=buf_get(b) && buf_len(b));

  if (!buf_len(b))
    return 0;

  return 1;
}

static int
parse_req_line(buf_t *b, http_req_line_t *hrl)
{
  int rv;

  hrl->method.start=buf_ref(b);
  if (0>=(rv=parse_el(b)))
    return rv;
  hrl->method.end=buf_ref(b);

  hrl->req_uri.start=buf_ref(b);
  if (0>=(rv=parse_el(b)))
    return rv;
  hrl->req_uri.end=buf_ref(b);

  hrl->http_ver.start=buf_ref(b);
  if (0>=(rv=parse_el_crlf(b)))
    return rv;
  hrl->http_ver.end=buf_ref(b);

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

  printf("%s", buf_ref(conn->buf));

  int len;
  len=hrl.req_uri.end-hrl.req_uri.start;
  char *uri=malloc(len+1);
  memset(uri, 0x0, len+1);
  strncpy(uri, hrl.req_uri.start, len);
  printf("req_uri=%s\n", uri);
  free(uri);
  len=hrl.method.end-hrl.method.start;
  char *method=malloc(len+1);
  memset(method, 0x0, len+1);
  strncpy(method, hrl.method.start, len);
  printf("method=%s\n", method);
  free(method);
  len=hrl.http_ver.end-hrl.http_ver.start;
  char *http_ver=malloc(len+1);
  memset(http_ver, 0x0, len+1);
  strncpy(http_ver, hrl.http_ver.start, len);
  printf("http_ver=%s\n", http_ver);
  free(http_ver);


  buf_clear(conn->buf);
  return 1;
}

int
http_on_write(wschild_conn_t *conn)
{
  return -1;
}
