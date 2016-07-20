#include <string.h>
#include <stdio.h>
#include <poll.h>
#include <errno.h>
#include "ws.h"
#include "http.h"
#include "parser.h"

#define HTTP_400 "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"

extern const wsd_config_t *wsd_cfg;

static const char *METHOD_GET = "GET";
static const char *URI_ANY = "*";
static const char *URI_ABSOLUTE = "http://";

static int is_valid_req_line(http_req_t *hr);
static int is_valid_host_header_field(http_req_t *hr);
static int is_valid_upgrade_header_field(http_req_t *hr);
static int is_valid_connection_header_field(http_req_t *hr);
static int found_upgrade(string_t *result);
static int tokenise_connection(string_t *s);

int
http_on_read(wsconn_t *conn)
{
     buf_flip(conn->buf_in);

     if (LOG_VVERBOSE == wsd_cfg->verbose)
          printf("http: on_read: fd=%d: %d byte(s)\n",
                 conn->pfd->fd,
                 buf_len(conn->buf_in));
     else if (LOG_VVVERBOSE == wsd_cfg->verbose)
          printf("http: on_read: fd=%d: %s",
                 conn->pfd->fd,
                 buf_ref(conn->buf_in));

     int rv;
     string_t t;
     bzero(&t, sizeof(string_t));

     /* Tokenise start line ... */
     if (0 > (rv = http_header_tok(buf_ref(conn->buf_in), &t)))
     {
          if (LOG_VVERBOSE == wsd_cfg->verbose)
               printf("http_header_tok: errno=%d\n", errno);

          if (EUNEXPECTED_EOS == errno)
          {
               /* not enough data */
               buf_flip(conn->buf_in);
               /* try again */
               return 1;
          }
          
          return (-1);
     }

     http_req_t hr;
     bzero(&hr, sizeof(http_req_t));

     /* ... parse status line, see whether or not it's a request ... */
     if (0 > (rv = parse_request_line(&t, &hr)))
     {
          if (LOG_VVERBOSE == wsd_cfg->verbose)
               printf("parse_request_line: errno=%d\n", errno);

          if (0 > http_prepare_response(conn->buf_out, HTTP_400))
               return (-1);

          goto error;
     }

     /* ... validate request line ... */
     if (!is_valid_req_line(&hr))
     {
          if (0 > http_prepare_response(conn->buf_out, HTTP_400))
               return (-1);

          goto error;
     }

     /* ... tokenise and parse header fields ... */
     while (0 < (rv = http_header_tok(NULL, &t)))
     {
          if (0 > (rv = parse_header_field(&t, &hr)))
          {
               if (LOG_VVERBOSE == wsd_cfg->verbose)
                    printf("parse_header_field: errno=%d\n", errno);

               if (0 > http_prepare_response(conn->buf_out, HTTP_400))
                    return (-1);

               goto error;
          }
     }

     /* ... and finally, validate HTTP-related fields. */
     if (!is_valid_host_header_field(&hr))
     {
          if (LOG_VVERBOSE == wsd_cfg->verbose)
               printf("invalid host header field\n");

          /* Implementing as MUST; see RFC7230, section 5.4 and
           * RFC6455, section 4.1
           */
          if (0 > http_prepare_response(conn->buf_out, HTTP_400))
               return (-1);

          goto error;
     }

     if (!is_valid_upgrade_header_field(&hr))
     {
          if (LOG_VVERBOSE == wsd_cfg->verbose)
               printf("invalid upgrade header field\n");

          if (0 > http_prepare_response(conn->buf_out, HTTP_400))
               return (-1);

          goto error;
     }
     
     if (!is_valid_connection_header_field(&hr))
     {
          if (LOG_VVERBOSE == wsd_cfg->verbose)
               printf("invalid connection header field\n");

          if (0 > http_prepare_response(conn->buf_out, HTTP_400))
               return (-1);

          goto error;
     }

     if (0 > conn->on_handshake(conn, &hr))
          return -1;

     return 1;

error:
     buf_clear(conn->buf_in);
     conn->pfd->events|=POLLOUT;
     conn->close_on_write = 1;

     return 1;
}

int
http_on_write(wsconn_t *conn)
{
     return -1;
}

static int
is_valid_req_line(http_req_t *hr)
{
     if (hr->method.len == 3 &&
         0 != strncmp(hr->method.start, METHOD_GET, hr->method.len))
          return 0;

     /* HTTP-version checked as side-effect of parsing request line */

     if (0 == strncmp(hr->req_target.start,
                      URI_ANY,
                      hr->req_target.len))
          return 0;

     if (0 == strncasecmp(hr->req_target.start, URI_ABSOLUTE, 7))
          hr->is_uri_abs = 1;

     /* Request-target check delegated to ws handler */

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
is_valid_host_header_field(http_req_t *hr)
{
     if (0 == hr->host.len)
          return 0;

     /* TODO check as per RFC6455, section 4.1 */
     
     return 1;
}

static int
is_valid_upgrade_header_field(http_req_t *hr)
{
     string_t result;
     
     while (0 < http_field_value_tok(&hr->upgrade, &result))
     {
          trim(&result);
          if (0 == strncasecmp("websocket", result.start, 9))
               return 1;
     }

     return 0;
}

static int
found_upgrade(string_t *result)
{
     trim(result);
     return (0 == strncasecmp("Upgrade", result->start, 7)) ? 1 : 0;
}

static int
tokenise_connection(string_t *s)
{
     string_t result;

     if (0 < http_field_value_tok(s, &result)) {
          if (found_upgrade(&result))
               return 1;
          
          while (0 < http_field_value_tok(NULL, &result))
               if (found_upgrade(&result))
                    return 1;
     }

     return 0;
}

static int
is_valid_connection_header_field(http_req_t *hr)
{
     if (tokenise_connection(&hr->conn))
          return 1;
     
     /* Got two ... */
     return tokenise_connection(&hr->conn2);
}
