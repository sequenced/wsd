#include <sys/epoll.h>
#include <string.h>
#include <errno.h>

#ifdef WSD_DEBUG
#include <stdio.h>
#endif

#include "common.h"
#include "http.h"
#include "parser.h"

#define HTTP_400 "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"

extern unsigned int wsd_errno;
extern const wsd_config_t *wsd_cfg;

static const char *METHOD_GET = "GET";
static const char *URI_ANY = "*";
static const char *URI_ABSOLUTE = "http://";

static int is_valid_req_line(http_req_t *hr);
static int is_valid_host_header_field(http_req_t *hr);
static int is_valid_upgrade_header_field(http_req_t *hr);
static int is_valid_connection_header_field(http_req_t *hr);
static int has_upgrade(chunk_t *result);
static int tokenise_connection(chunk_t *s);

int
http_recv(sk_t *sk)
{
     AN(skb_rdsz(sk->recvbuf));

     if (!has_rnrn_termination(sk->recvbuf)) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     int rv;
     chunk_t tok;
     memset(&tok, 0, sizeof(chunk_t));
     rv = http_header_tok(&sk->recvbuf->data[sk->recvbuf->rdpos], &tok);
     if (0 > rv) {
          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: errno=%d\n", __func__, errno);
          }

          goto error;
     }

     http_req_t hreq;
     memset(&hreq, 0, sizeof(http_req_t));

     /* Parse status line, see whether or not it's a request ... */
     if (0 > parse_request_line(&tok, &hreq)) {

          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: errno=%d\n", __func__, errno);
          }

          if (0 == skb_put_str(sk->sendbuf, HTTP_400)) {
               sk->close_on_write = 1;
          }

          goto error;
     }

     /* ... validate request line ... */
     if (!is_valid_req_line(&hreq)) {

          if (0 == skb_put_str(sk->sendbuf, HTTP_400)) {
               sk->close_on_write = 1;
          }

          goto error;
     }

     /* ... tokenise and parse header fields ... */
     while (0 < http_header_tok(NULL, &tok)) {

          if (0 > parse_header_field(&tok, &hreq)) {
               
               if (LOG_VVERBOSE <= wsd_cfg->verbose) {
                    printf("\t%s: errno=%d\n", __func__, errno);
               }

               if (0 == skb_put_str(sk->sendbuf, HTTP_400)) {
                    sk->close_on_write = 1;
               }

               goto error;
          }
     }

     /* ... and finally, validate HTTP protocol fields. */
     if (!is_valid_host_header_field(&hreq)) {

          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: invalid host header field\n", __func__);
          }

          /*
           * Implementing as MUST; see RFC7230, section 5.4 and
           * RFC6455, section 4.1
           */
          if (0 == skb_put_str(sk->sendbuf, HTTP_400)) {
               sk->close_on_write = 1;
          }

          goto error;
     }

     if (!is_valid_upgrade_header_field(&hreq)) {

          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: invalid upgrade header field\n", __func__);
          }

          if (0 == skb_put_str(sk->sendbuf, HTTP_400)) {
               sk->close_on_write = 1;
          }

          goto error;
     }
     
     if (!is_valid_connection_header_field(&hreq)) {

          if (LOG_VVERBOSE <= wsd_cfg->verbose) {
               printf("\t%s: invalid connection header field\n", __func__);
          }

          if (0 == skb_put_str(sk->sendbuf, HTTP_400)) {
               sk->close_on_write = 1;
          }

          goto error;
     }

     skb_reset(sk->recvbuf);

     return sk->proto->decode_handshake(sk, &hreq);

error:
     skb_reset(sk->recvbuf);
     wsd_errno = WSD_EBADREQ;

     if (sk->close_on_write) {
          turn_off_events(sk, EPOLLIN);
          if (!(sk->events & EPOLLOUT)) {
               turn_on_events(sk, EPOLLOUT);
          }
     }

     return (-1);
}

static int
is_valid_req_line(http_req_t *hreq)
{
     if (hreq->method.len == 3
         && 0 != strncmp(hreq->method.p, METHOD_GET, hreq->method.len))
          return 0;

     /* HTTP-version checked as side-effect of parsing request line */

     if (0 == strncmp(hreq->req_target.p,
                      URI_ANY,
                      hreq->req_target.len))
          return 0;

     if (0 == strncasecmp(hreq->req_target.p, URI_ABSOLUTE, 7))
          hreq->is_uri_abs = 1;

     /* Request-target check delegated to ws handler */

     return 1;
}

static int
is_valid_host_header_field(http_req_t *hreq)
{
     if (0 == hreq->host.len)
          return 0;

     /* TODO check as per RFC6455, section 4.1 */
     
     return 1;
}

static int
is_valid_upgrade_header_field(http_req_t *hreq)
{
     chunk_t result;
     
     while (0 < http_field_value_tok(&hreq->upgrade, &result)) {
          trim(&result);
          if (0 == strncasecmp("websocket", result.p, 9))
               return 1;
     }

     return 0;
}

static int
has_upgrade(chunk_t *result)
{
     trim(result);
     return (0 == strncasecmp("Upgrade", result->p, 7)) ? 1 : 0;
}

static int
tokenise_connection(chunk_t *s)
{
     chunk_t result;

     if (0 < http_field_value_tok(s, &result)) {
          if (has_upgrade(&result))
               return 1;
          
          while (0 < http_field_value_tok(NULL, &result))
               if (has_upgrade(&result))
                    return 1;
     }

     return 0;
}

static int
is_valid_connection_header_field(http_req_t *hreq)
{
     if (tokenise_connection(&hreq->conn))
          return 1;
     
     /* Got two ... */
     return tokenise_connection(&hreq->conn2);
}
