#include <string.h>
#include <stdio.h>
#include <errno.h>
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
http_read(ep_t *ep)
{
     AN(buf_read_sz(ep->rcv_buf));

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, read_sz=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 ep->fd,
                 buf_read_sz(ep->rcv_buf));
     }

     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          ep->rcv_buf.p[ep->rcv_buf.wrpos] = '\0';
          printf("%s", ep->rcv_buf.p[ep->rcv_buf.rdpos]);
     }

     int rv;
     string_t t;
     memset(&t, 0x0, sizeof(string_t));

     /* Tokenise start line ... */
     if (0 > (rv = http_header_tok(&ep->rcv_buf.p[ep->rcv_buf.rdpos], &t))) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s:%d: %s: errno=%d\n",
                      __FILE__,
                      __LINE__,
                      __func__,
                      errno);
          }

          if (EUNEXPECTED_EOS == errno) {
               /* not enough data, try again */
               return 0;
          }

          return (-1);
     }

     http_req_t hr;
     memset(&hr, 0x0, sizeof(http_req_t));

     /* ... parse status line, see whether or not it's a request ... */
     if (0 > (rv = parse_request_line(&t, &hr))) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s:%d: %s: errno=%d\n",
                      __FILE__,
                      __LINE__,
                      __func__,
                      errno);
          }

          if (0 > http_prepare_response(ep->snd_buf, HTTP_400))
               return (-1);

          goto error;
     }

     /* ... validate request line ... */
     if (!is_valid_req_line(&hr)) {
          if (0 > http_prepare_response(ep->snd_buf, HTTP_400))
               return (-1);

          goto error;
     }

     /* ... tokenise and parse header fields ... */
     while (0 < (rv = http_header_tok(NULL, &t))) {
          if (0 > (rv = parse_header_field(&t, &hr))) {
               if (LOG_VVERBOSE <= wsd_cfg->verbose) {
                    printf("\t%s:%d: %s: errno=%d\n",
                           __FILE__,
                           __LINE__,
                           __func__,
                           errno);
               }

               if (0 > http_prepare_response(ep->snd_buf, HTTP_400))
                    return (-1);

               goto error;
          }
     }

     /* ... and finally, validate HTTP-related fields. */
     if (!is_valid_host_header_field(&hr)) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s:%d: %s: invalid host header field\n",
                      __FILE__,
                      __LINE__,
                      __func__);
          }

          /* Implementing as MUST; see RFC7230, section 5.4 and
           * RFC6455, section 4.1
           */
          if (0 > http_prepare_response(ep->snd_buf, HTTP_400))
               return (-1);

          goto error;
     }

     if (!is_valid_upgrade_header_field(&hr)) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s:%d: %s: invalid upgrade header field\n",
                      __FILE__,
                      __LINE__,
                      __func__);
          }

          if (0 > http_prepare_response(ep->snd_buf, HTTP_400))
               return (-1);

          goto error;
     }
     
     if (!is_valid_connection_header_field(&hr)) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s:%d: %s: invalid connection header field\n",
                      __FILE__,
                      __LINE__,
                      __func__);
          }

          if (0 > http_prepare_response(ep->snd_buf, HTTP_400))
               return (-1);

          goto error;
     }

//     if (0 > conn->on_handshake(conn, &hr))
//          return -1;

     return 0;

error:
     buf_reset(ep->rcv_buf);
     ep->close_on_write = 1;

     return 0;
}

int http_write(ep_t *ep) { return (-1); }

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
http_prepare_response(buf2_t b, const char *s)
{
     size_t len = strlen(s);

     /* see RFC2616 section 6.1.1 */
     if (buf_write_sz(b) < len)
          return (-1);

     strcpy(&b.p[b.wrpos], s);
     b.wrpos += len;

     return 0;
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
     
     while (0 < http_field_value_tok(&hr->upgrade, &result)) {
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
