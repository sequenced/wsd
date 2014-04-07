#include <string.h>
#include <stdio.h>
#include <poll.h>
#include "http.h"

#define HTTP_501 "HTTP/1.1 501 Not Implemented\r\n\r\n"
#define HTTP_500 "HTTP/1.1 500 Internal Server Error\r\n\r\n"

static const char *METHOD_GET="GET";
static const char *HTTP_VER="HTTP/1.1";
static const char *URI_ANY="*";
static const char *URI_ABSOLUTE="http://";
static const char *FLD_USER_AGENT="User-Agent:";
static const char *FLD_CONNECTION="Connection:";
static const char *FLD_HOST="Host:";
static const char *FLD_UPGRADE="Upgrade:";

/* as per RFC2616 section 5.1 */
typedef struct
{
  char_range_t method;
  char_range_t req_uri;
  char_range_t http_ver;
  char_range_t host;
  char_range_t user_agent;
  char_range_t conn;
  char_range_t upgrade;
  int is_uri_abs;
} http_req_t;

static int is_valid_req_line_syntax(http_req_t *hr);
static int prepare_XXX_response(buf_t *b, const char *s);

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
parse_hdr_fld_value(buf_t *b)
{
  int start=buf_pos(b);
 extended:
  while (0<buf_len(b) && '\r'!=buf_get(b));

  if (!buf_len(b))
    return -1;

  if ('\n'!=buf_get(b))
    return -1;

  /* extended value (see RFC2616 section 4.2) */
  char c=buf_get(b);
  if (' '==c || '\t'==c)
    goto extended;

  buf_rwnd(b, 1);

  return (buf_pos(b)-start-2); /* -2: adjust for \r\n */
}

static int
parse_hdr_fld(buf_t *b, char_range_t *f)
{
  f->start=buf_ref(b);
  int len;
  if (0>(len=parse_hdr_fld_value(b)))
    return -1;
  f->len=len;

  return 0;
}

static int
parse_req_hdr_flds(buf_t *b, http_req_t *hr)
{
  /* while not null line ('\r\n\r\n') */
  while (0!=strncmp(buf_ref(b), "\r\n", buf_len(b)<2?buf_len(b):2))
    {
      char_range_t cr;
      cr.start=buf_ref(b);
      while (0<buf_len(b) && ':'!=buf_get(b));
      cr.len=buf_ref(b)-cr.start;

      if (0==strncasecmp(cr.start,
                         FLD_CONNECTION,
                         strlen(FLD_CONNECTION)))
        {
          if (0>parse_hdr_fld(b, &(hr->conn)))
            return -1;
        }
      else if (0==strncasecmp(cr.start,
                              FLD_UPGRADE,
                              strlen(FLD_UPGRADE)))
        {
          if (0>parse_hdr_fld(b, &(hr->upgrade)))
            return -1;
        }
      else if (0==strncasecmp(cr.start,
                              FLD_USER_AGENT,
                              strlen(FLD_USER_AGENT)))
        {
          if (0>parse_hdr_fld(b, &(hr->user_agent)))
            return -1;
        }
      else if (0==strncasecmp(cr.start,
                              FLD_HOST,
                              strlen(FLD_HOST)))
        {
          if (0>parse_hdr_fld(b, &(hr->host)))
            return -1;
        }
      else
        {
          /* ignore unrecognised fields */
          char_range_t ignored;
          if (0>parse_hdr_fld(b, &ignored))
            return -1;
        }

      if (0==buf_len(b))
        return 0;
    }

  return 0;
}

static int
parse_req_line(buf_t *b, http_req_t *hr)
{
  int rv;

  hr->method.start=buf_ref(b);
  if (0>=(rv=parse_el(b)))
    return rv;
  hr->method.len=rv;

  hr->req_uri.start=buf_ref(b);
  if (0>=(rv=parse_el(b)))
    return rv;
  hr->req_uri.len=rv;

  hr->http_ver.start=buf_ref(b);
  if (0>=(rv=parse_el_crlf(b)))
    return rv;
  hr->http_ver.len=rv;

  return 1;
}

int
http_on_read(wschild_conn_t *conn)
{
  buf_flip(conn->buf_in);

  http_req_t hr;
  memset(&hr, 0x0, sizeof(hr));
  int rv;
  if (0>(rv=parse_req_line(conn->buf_in, &hr)))
    /* parse error */
    return -1;
  else if (0==rv)
    {
      /* not enough data */
      buf_flip(conn->buf_in);
      goto again;
    }

  /* have three tokens terminated with `\r\n' (see RFC2616 section 5.1) */
  if (!is_valid_req_line_syntax(&hr))
    {
      if (0>prepare_XXX_response(conn->buf_out, HTTP_501))
        /* request wrong, can't prepare response so bail out */
        return -1;

      goto error;
    }

  if (0>(rv=parse_req_hdr_flds(conn->buf_in, &hr)))
    {
      /* parse error in header fields */
      if (0>prepare_XXX_response(conn->buf_out, HTTP_500))
        return -1;

      goto error;
    }

  if (NULL==hr.conn.start
      || NULL==hr.upgrade.start)
    {
      /* not a handshake as per RFC6455 */
      if (0>prepare_XXX_response(conn->buf_out, HTTP_501))
        return -1;

      goto error;
    }

  printf("%s", buf_ref(conn->buf_in));
  buf_clear(conn->buf_in);
  goto success;

 error:
  buf_clear(conn->buf_in);
  buf_flip(conn->buf_out);
  conn->pfd->events|=POLLOUT;
  conn->close_on_write=1;

 again:
 success:
  return 1;
}

int
http_on_write(wschild_conn_t *conn)
{
  
  return -1;
}

static int
is_valid_req_line_syntax(http_req_t *hr)
{
  if (0!=strncmp(hr->method.start,
                 METHOD_GET,
                 hr->method.len))
    return 0;

  if (0!=strncmp(hr->http_ver.start,
                 HTTP_VER,
                 hr->http_ver.len))
    return 0;

  /* enforce RFC2616 section 5.1.2 */
  if (1>hr->req_uri.len)
    return 0;

  if (0==strncmp(hr->req_uri.start,
                 URI_ANY,
                 hr->req_uri.len))
    return 0;

  if (0==strncasecmp(hr->req_uri.start,
                     URI_ABSOLUTE,
                     7)) /* strlen(URI_ABSOLUTE) */
    hr->is_uri_abs=1;

  return 1;
}

static int
prepare_XXX_response(buf_t *b, const char *s)
{
  /* see RFC2616 section 6.1.1 */
  if (buf_len(b)<(strlen(s)))
    return -1;

  strcpy(buf_ref(b), s);
  buf_fwd(b, strlen(s));

  return 1;
}
