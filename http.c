#include <string.h>
#include <stdio.h>
#include <poll.h>
#include "ws.h"
#include "http.h"

#define HTTP_400 "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"
#define HTTP_501 "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\n\r\n"

extern const wsd_config_t *wsd_cfg;

static const char *METHOD_GET="GET";
static const char *HTTP_VER="HTTP/1.1";
static const char *URI_ANY="*";
static const char *URI_ABSOLUTE="http://";
static const char *FLD_USER_AGENT="User-Agent:";
static const char *FLD_CONNECTION="Connection:";
static const char *FLD_CONNECTION_VAL="Upgrade";
static const char *FLD_HOST="Host:";
static const char *FLD_SEC_WS_KEY="Sec-WebSocket-Key:";
static const char *FLD_SEC_WS_VER="Sec-WebSocket-Version:";
static const char *FLD_SEC_WS_EXT="Sec-WebSocket-Extensions:";
static const char *FLD_SEC_WS_PROTO="Sec-WebSocket-Protocol:";
static const char *FLD_UPGRADE="Upgrade:";
static const char *FLD_UPGRADE_VAL="websocket";
static const char *FLD_ORIGIN="Origin:";

static int is_valid_req_line_syntax(http_req_t *hr);
static int is_valid_upgrade_req(http_req_t *hr);

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
parse_hdr_fld(buf_t *b, string_t *f)
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
      string_t cr;
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
      else if (0==strncasecmp(cr.start,
                              FLD_SEC_WS_KEY,
                              strlen(FLD_SEC_WS_KEY)))
        {
          if (0>parse_hdr_fld(b, &(hr->sec_ws_key)))
            return -1;
        }
      else if (0==strncasecmp(cr.start,
                              FLD_SEC_WS_VER,
                              strlen(FLD_SEC_WS_VER)))
        {
          if (0>parse_hdr_fld(b, &(hr->sec_ws_ver)))
            return -1;
        }
      else if (0==strncasecmp(cr.start,
                              FLD_SEC_WS_EXT,
                              strlen(FLD_SEC_WS_EXT)))
        {
          if (0>parse_hdr_fld(b, &(hr->sec_ws_ext)))
            return -1;
        }
      else if (0==strncasecmp(cr.start,
                              FLD_ORIGIN,
                              strlen(FLD_ORIGIN)))
        {
          if (0>parse_hdr_fld(b, &(hr->origin)))
            return -1;
        }
      else if (0==strncasecmp(cr.start,
                              FLD_SEC_WS_PROTO,
                              strlen(FLD_SEC_WS_PROTO)))
        {
          if (0>parse_hdr_fld(b, &(hr->sec_ws_proto)))
            return -1;
        }
      else
        {
          /* ignore unrecognised fields */
          string_t ignored;
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
http_on_read(ws_conn_t *conn)
{
  buf_flip(conn->buf_in);

  if (LOG_VVVERBOSE==wsd_cfg->verbose)
    printf("%s", buf_ref(conn->buf_in));

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
      /* try again */
      return 1;
    }

  /* have three tokens terminated with `\r\n' (see RFC2616 section 5.1) */
  if (!is_valid_req_line_syntax(&hr))
    {
      if (0>http_prepare_response(conn->buf_out, HTTP_501))
        return -1;

      goto error;
    }

  if (0>(rv=parse_req_hdr_flds(conn->buf_in, &hr)))
    {
      /* parse error in header fields */
      if (0>http_prepare_response(conn->buf_out, HTTP_500))
        return -1;

      goto error;
    }

  if (NULL==hr.host.start)
    {
      /* HTTP 1.1 requires `Host:' header field (RFC2616 section 14.23) */
      if (0>http_prepare_response(conn->buf_out, HTTP_400))
        return -1;

      goto error;
    }

  /* TODO check value of `Host:' header field */

  if (!is_valid_upgrade_req(&hr))
    {
      /* not an upgrade request as per RFC6455 */
      if (0>http_prepare_response(conn->buf_out, HTTP_501))
        return -1;

      goto error;
    }

  if (0>ws_on_handshake(conn, &hr))
    return -1;

  return 1;

 error:
  buf_clear(conn->buf_in);
  conn->pfd->events|=POLLOUT;
  conn->close_on_write=1;

  return 1;
}

int
http_on_write(ws_conn_t *conn)
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

int
http_prepare_response(buf_t *b, const char *s)
{
  /* see RFC2616 section 6.1.1 */
  if (buf_len(b)<(strlen(s)))
    return -1;

  strcpy(buf_ref(b), s);
  buf_fwd(b, strlen(s));

  return 1;
}

static int
is_valid_upgrade_req(http_req_t *hr)
{
  /* see RFC6455 section 4.2.1 */
  if (NULL==hr->conn.start
      || NULL==hr->upgrade.start)
    return 0;

  trim(&(hr->conn));

  /* request header fields can have multiple values; see RFC2616 section 4.2 */
  int match=0;
  string_t *token=tok(&(hr->conn), ',');

  while (0<token->len)
    {
      trim(token);
      if (0==strncasecmp(token->start,
                         FLD_CONNECTION_VAL,
                         token->len))
        {
          match=1;
        }
 
      token=tok(NULL, ',');
    }

  if (!match)
    return 0;

  trim(&(hr->upgrade));
  if (0!=strncasecmp(hr->upgrade.start,
                     FLD_UPGRADE_VAL,
                     hr->upgrade.len))
    return 0;

  return 1;
}
