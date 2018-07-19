/*
 *  Copyright (C) 2017-2018 Michael Goldschmidt
 *
 *  This file is part of wsd/wscat.
 *
 *  wsd/wscat is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  wsd/wscat is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with wsd/wscat.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#ifdef HAVE_LIBSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#endif
#include "ws.h"
#include "types.h"
#include "common.h"
#include "parser.h"
#include "uri.h"

#define skb_put_chunk(dst, src)                 \
     skb_put_strn(dst, src.p, src.len)

#define DEFAULT_TIMEOUT  128

extern bool done;

int epfd = -1;
char *bin = NULL;
int wsd_errno = 0;
wsd_config_t *wsd_cfg = NULL;
sk_t *pp2sk = NULL; /* TODO avoid this reference */

static struct option long_opt[] = {
     {"sec-ws-ver",           required_argument, 0, 'V'},
     {"sec-ws-proto",         required_argument, 0, 'P'},
     {"sec-ws-key",           required_argument, 0, 'K'},
     {"user-agent",           required_argument, 0, 'A'},
     {"idle-timeout",         required_argument, 0, 'i'},
     {"json",                 required_argument, 0, 'j'},
     {"repeat-last",          required_argument, 0, 'R'},
     {"no-handshake",         no_argument,       0, 'N'},
     {"verbose",              no_argument,       0, 'v'},
#ifdef HAVE_LIBSSL
     {"no-check-certificate", no_argument,       0, 'x'},
#endif
     {"help",                 no_argument,       0, 'h'},
     {0, 0, 0, 0}
};
static const char *optstring = "V:P:K:A:i:R:vhjNx";
static sk_t *fdin = NULL;
static sk_t *wssk = NULL;
static bool is_json = false;
static bool no_handshake = false;
static int repeat_last_num = -1;
static bool repeat_last_armed = false;
static skb_t *last_input = NULL;

static int wssk_init();
static int stdin_close(sk_t *sk);
static int wssk_close(sk_t *sk);
static int post_read(sk_t *sk);
static int wssk_http_recv(sk_t *sk);
static int stdin_recv(sk_t *sk);
static int stdin_decode_frame(sk_t *sk);
static int wssk_ws_recv(sk_t *sk);
static int wssk_ws_decode_frame(sk_t *sk);
static int wssk_ws_encode_frame(sk_t *sk, wsframe_t *wsf);
static void wssk_ws_start_closing_handshake();
static int wssk_ws_encode_data_frame(skb_t *dst,
                                     skb_t *src,
                                     const unsigned int maxlen,
                                     const uint64_t hash);
static int repeat_last();
static void try_repeating_last();
static int on_iteration(const struct timespec *now);
static int create_http_req(sk_t *sk);
static int skb_put_http_req(skb_t *buf, http_req_t *req);
static int balanced_span(const skb_t *buf, const char begin, const char end);
static void print_help();
#ifdef HAVE_LIBSSL
static int wssk_tls_init(sk_t *sk, const char *hostname);
static int wssk_tls_handshake_read(sk_t *sk);
static int wssk_tls_handshake_write(sk_t *sk);
static int wssk_tls_handshake(sk_t *sk);
static int wssk_tls_read(sk_t *sk);
static int wssk_tls_write(sk_t *sk);
static void print_tls_error();
#endif

int
main(int argc, char **argv)
{
     int opt;
     bool is_json_arg = false;
     bool no_handshake_arg = false;
     int repeat_last_num_arg = -1;
     int idle_timeout_arg = -1;
     int verbose_arg = 0;
     char *fwd_hostname_arg = NULL;
     char *fwd_port_arg = NULL;
     char *user_agent_arg = "wscat";
     char *sec_ws_proto_arg = NULL;
     char *sec_ws_ver_arg = "13";
     char *sec_ws_key_arg = "B9Pc1t39Tqoj+fidr/bzeg==";
#ifdef HAVE_LIBSSL
     bool tls_arg = false;
     bool no_check_cert_arg = false;
#endif

     char *s;
     bin = (s = strrchr(argv[0], '/')) ? (char*)(s + 1) : argv[0];

     while ((opt = getopt_long(argc,
                               argv,
                               optstring,
                               long_opt,
                               NULL)) != -1) {
          switch (opt) {
          case 'A':
               user_agent_arg = optarg;
               break;
          case 'i':
               idle_timeout_arg = atoi(optarg);
               break;
          case 'v':
               verbose_arg++;
               break;
          case 'V':
               sec_ws_ver_arg = optarg;
               break;
          case 'K':
               sec_ws_key_arg = optarg;
               break;
          case 'P':
               sec_ws_proto_arg = optarg;
               break;
          case 'j':
               is_json_arg = true;
               break;
          case 'N':
               no_handshake_arg = true;
               break;
          case 'R':
               repeat_last_num_arg = atoi(optarg);
               break;
#ifdef HAVE_LIBSSL
          case 'x':
               no_check_cert_arg = true;
               break;
#endif
          case 'h':
               print_help(bin);
               exit(EXIT_SUCCESS);
               break;
          default:
               fprintf(stderr,
                       "Try '%s --help' for more information.\n",
                       bin);
               exit(EXIT_FAILURE);
          };
     }

     if (optind < argc) {
          uri_t uri;
          memset((void*)&uri, 0, sizeof(uri_t));
          if (0 > parse_uri(argv[optind], &uri)) {
               fprintf(stderr, "%s: bad URI: %s\n", bin, argv[optind]);
               exit(EXIT_FAILURE);
          }
#ifdef HAVE_LIBSSL
          if (3 == uri.scheme.len
              && 0 == strncasecmp("wss", uri.scheme.p, 3))
               tls_arg = true;
          else if (2 != uri.scheme.len
                   || 0 != strncasecmp("ws", uri.scheme.p, 2)) {
               fprintf(stderr,
                       "%s: can`t speak: %.*3$s\n",
                       bin,
                       uri.scheme.p,
                       uri.scheme.len);
               exit(EXIT_FAILURE);
          }
#else
          if (2 != uri.scheme.len
              || 0 != strncasecmp("ws", uri.scheme.p, 2)) {
               fprintf(stderr,
                       "%s: can`t speak: %.*3$s\n",
                       bin,
                       uri.scheme.p,
                       uri.scheme.len);
               exit(EXIT_FAILURE);
          }
#endif /* #ifdef HAVE_LIBSSL */
          fwd_hostname_arg = strndup(uri.host.p, uri.host.len);
          if (uri.port.len)
               fwd_port_arg = strndup(uri.port.p, uri.port.len);
     }

     is_json = is_json_arg;
     no_handshake = no_handshake_arg;

     if (0 < repeat_last_num_arg) {
          last_input = malloc(sizeof(skb_t));
          A(last_input);
          memset(last_input, 0, sizeof(skb_t));
          repeat_last_num = repeat_last_num_arg;
     }

     if (!fwd_hostname_arg)
          fwd_hostname_arg = strdup("localhost");

#ifdef HAVE_LIBSSL
     if (!fwd_port_arg) {
          if (tls_arg)
               fwd_port_arg = strdup("443");
          else
               fwd_port_arg = strdup("80");
     }
#else
     if (!fwd_port_arg)
          fwd_port_arg = strdup("80");
#endif /* #ifdef HAVE_LIBSSL */

     wsd_cfg = malloc(sizeof(wsd_config_t));
     A(wsd_cfg);
     memset(wsd_cfg, 0, sizeof(wsd_config_t));
     wsd_cfg->idle_timeout = idle_timeout_arg;
     wsd_cfg->fwd_hostname = malloc(2);
     A(wsd_cfg->fwd_hostname);
     memset(wsd_cfg->fwd_hostname, 0, 2);
     wsd_cfg->fwd_hostname[0] = malloc(strlen(fwd_hostname_arg) + 1);
     strcpy(wsd_cfg->fwd_hostname[0], fwd_hostname_arg);
     wsd_cfg->fwd_port = fwd_port_arg;
     wsd_cfg->verbose = verbose_arg;
     wsd_cfg->lfd = -1;
     wsd_cfg->user_agent = user_agent_arg;
     wsd_cfg->sec_ws_proto = sec_ws_proto_arg;
     wsd_cfg->sec_ws_ver = sec_ws_ver_arg;
     wsd_cfg->sec_ws_key = sec_ws_key_arg;
#ifdef HAVE_LIBSSL
     wsd_cfg->tls = tls_arg;
     wsd_cfg->no_check_cert = no_check_cert_arg;
#endif

     if (0 > wssk_init())
          exit(EXIT_FAILURE);

#ifdef HAVE_LIBSSL
     if (wsd_cfg->tls)
          wssk_tls_handshake(wssk);
     else {
          if (0 > create_http_req(wssk))
               exit(EXIT_FAILURE);
     
          turn_on_events(wssk, EPOLLOUT);
     }
#else
     if (0 > create_http_req(wssk))
          exit(EXIT_FAILURE);

     turn_on_events(wssk, EPOLLOUT);
#endif /* #ifdef HAVE_LIBSSL */

     int rv = event_loop(on_iteration, post_read, DEFAULT_TIMEOUT);

     if (last_input)
          free(last_input);
     
     AZ(close(epfd));
     free(fwd_hostname_arg);
     free(fwd_port_arg);
     free(wsd_cfg->fwd_hostname);
     free(wsd_cfg);
     return rv;
}

int
create_http_req(sk_t *sk)
{
     http_req_t req;
     memset(&req, 0, sizeof(http_req_t));

     A(req.method.p = malloc(3 + 1));
     strcpy(req.method.p, "GET");
     req.method.len = 3;

     A(req.req_target.p = malloc(1 + 1));
     strcpy(req.req_target.p, "/");
     req.req_target.len = 1;

     A(req.http_ver.p = malloc(8 + 1));
     strcpy(req.http_ver.p, "HTTP/1.1");
     req.http_ver.len = 8;

     A(req.host.p = malloc(strlen(wsd_cfg->fwd_hostname[0]) + 1));
     strcpy(req.host.p, wsd_cfg->fwd_hostname[0]);
     req.host.len = strlen(wsd_cfg->fwd_hostname[0]);

     A(req.origin.p = malloc(strlen(wsd_cfg->fwd_hostname[0]) + 1));
     strcpy(req.origin.p, wsd_cfg->fwd_hostname[0]);
     req.origin.len = strlen(wsd_cfg->fwd_hostname[0]);

     if (wsd_cfg->sec_ws_proto) {
          A(req.sec_ws_proto.p = malloc(strlen(wsd_cfg->sec_ws_proto) + 1));
          strcpy(req.sec_ws_proto.p, wsd_cfg->sec_ws_proto);
          req.sec_ws_proto.len = strlen(wsd_cfg->sec_ws_proto);
     }

     A(req.sec_ws_key.p = malloc(strlen(wsd_cfg->sec_ws_key) + 1));
     strcpy(req.sec_ws_key.p, wsd_cfg->sec_ws_key);
     req.sec_ws_key.len = strlen(wsd_cfg->sec_ws_key);

     A(req.sec_ws_ver.p = malloc(strlen(wsd_cfg->sec_ws_ver) + 1));
     strcpy(req.sec_ws_ver.p, wsd_cfg->sec_ws_ver);
     req.sec_ws_ver.len = strlen(wsd_cfg->sec_ws_ver);

     A(req.user_agent.p = malloc(strlen(wsd_cfg->user_agent) + 1));
     strcpy(req.user_agent.p, wsd_cfg->user_agent);
     req.user_agent.len = strlen(wsd_cfg->user_agent);
     
     AZ(skb_put_http_req(sk->sendbuf, &req));

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          unsigned int len = skb_rdsz(sk->sendbuf);
          skb_print(stdout, sk->sendbuf, len);
          skb_rd_rewind(sk->sendbuf, len);
     }

     return 0;
}

int
wssk_init()
{
     int fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
     if (0 > fd) {
          perror("socket");
          return (-1);
     }

     struct addrinfo hints, *res;
     memset(&hints, 0, sizeof(struct addrinfo));
     hints.ai_family = AF_INET;
     hints.ai_socktype = SOCK_STREAM;
     int rv = getaddrinfo(wsd_cfg->fwd_hostname[0],
                          wsd_cfg->fwd_port,
                          &hints,
                          &res);
     if (0 > rv) {
          fprintf(stderr, "%s: cannot resolve: %s\n", bin, gai_strerror(rv));
          AZ(close(fd));
          return (-1);
     }

     if (AF_INET != res->ai_family) {
          fprintf(stderr,
                  "%s: unexpected address family: %d\n",
                  bin,
                  res->ai_family);
          AZ(close(fd));
          freeaddrinfo(res);
          return (-1);
     }

     struct addrinfo *pos = res;
     while (pos) {
          if (-1 != connect(fd, pos->ai_addr, pos->ai_addrlen))
               break;
          pos = pos->ai_next;
     }
     freeaddrinfo(res);
     if (0 > rv && EINPROGRESS != errno) {
          fprintf(stderr, "%s: connect: %s\n", bin, strerror(errno));
          AZ(close(fd));
          return (-1);
     }

     wssk = malloc(sizeof(sk_t));
     if (!wssk) {
          perror("malloc");
          return (-1);
     }
     memset(wssk, 0, sizeof(sk_t));

     if (0 > sock_init(wssk, fd, 0ULL)) {
          fprintf(stderr, "%s: sock_init: 0x%x\n", bin, wsd_errno);
          AZ(close(fd));
          return (-1);
     }

#ifdef HAVE_LIBSSL
     if (wsd_cfg->tls && 0 > wssk_tls_init(wssk, wsd_cfg->fwd_hostname[0])) {
          fprintf(stderr, "%s: wssk_tls_init", bin);
          AZ(close(fd));
          return (-1);
     }
#endif

     wssk->ops->recv = wssk_http_recv;
     wssk->ops->close = wssk_close;
     wssk->proto->decode_frame = wssk_ws_decode_frame;
     wssk->proto->encode_frame = wssk_ws_encode_frame;
     wssk->proto->start_closing_handshake = ws_start_closing_handshake;

     epfd = epoll_create(1);
     A(epfd >= 0);

     AZ(register_for_events(wssk));

     return 0;
}

int
wssk_close(sk_t *sk) {
#ifdef HAVE_LIBSSL
     if (sk->ssl)
          SSL_shutdown(sk->ssl); /* Unidirectional shutdown only. */
#endif
     AZ(close(sk->fd));
     sock_destroy(sk);
     free(sk);

     wssk = NULL;
     done = true;

     return 0;
}

int
stdin_close(sk_t *sk)
{
     if (-1 == wsd_cfg->idle_timeout && -1 == repeat_last_num) {
          if (!no_handshake)
               wssk_ws_start_closing_handshake();
     } else if (0 < repeat_last_num) {
          repeat_last_armed = true;
          repeat_last();
     }
     
     AZ(close(sk->fd));
     sock_destroy(sk);
     free(sk);

     fdin = NULL;

     return 0;
}

int
post_read(sk_t *sk)
{
     if (!skb_rdsz(sk->recvbuf))
          return 0;

     return sk->ops->recv(sk);
}

int
wssk_http_recv(sk_t *sk)
{
     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          unsigned int len = skb_rdsz(sk->recvbuf);
          skb_print(stdout, sk->recvbuf, len);
          skb_rd_rewind(sk->recvbuf, len);
     }

     if (!has_rnrn_termination(sk->recvbuf)) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     int rv;
     chunk_t tok;
     memset(&tok, 0, sizeof(chunk_t));
     rv = http_header_tok(&sk->recvbuf->data[sk->recvbuf->rdpos], &tok);
     if (0 > rv) {
          fprintf(stderr,
                  "%s: failed tokenising HTTP header: 0x%x\n",
                  bin,
                  wsd_errno);

          goto error;
     }

     http_req_t hreq;
     memset(&hreq, 0, sizeof(http_req_t));
     while (0 < http_header_tok(NULL, &tok)) {

          if (0 > parse_header_field(&tok, &hreq)) {
               fprintf(stderr,
                       "%s: failed parsing HTTP header field: 0x%x\n",
                       bin,
                       wsd_errno);

               goto error;
          }
     }

     /* TODO validate HTTP header fields */

     sk->ops->recv = wssk_ws_recv;
     skb_reset(sk->recvbuf);

     /* Ready to speak websocket; read stdin. */
     fdin = malloc(sizeof(sk_t));
     if (!fdin) {
          perror("malloc");
          exit(EXIT_FAILURE);
     }
     memset(fdin, 0, sizeof(sk_t));
     AZ(sock_init(fdin, 0, 0ULL));
     fdin->ops->recv = stdin_recv;
     fdin->ops->close = stdin_close;
     fdin->proto->decode_frame = stdin_decode_frame;

     AZ(register_for_events(fdin));

     return 0;

error:
     skb_reset(sk->recvbuf);
     wsd_errno = WSD_EBADREQ;

     return (-1);
}

int
wssk_ws_encode_frame(sk_t *sk, wsframe_t *wsf)
{
     return wssk_ws_encode_data_frame(wssk->sendbuf,
                                      sk->recvbuf,
                                      wsf->payload_len,
                                      wssk->hash);
}

int
wssk_ws_encode_data_frame(skb_t *dst,
                          skb_t *src,
                          const unsigned int maxlen,
                          const uint64_t hash)
{
     wsframe_t wsf;
     memset(&wsf, 0, sizeof(wsframe_t));
     wsf.payload_len = skb_rdsz(src) < maxlen ? skb_rdsz(src) : maxlen;

     long frame_len = ws_calculate_frame_length(wsf.payload_len);
     if (0 > frame_len)
          return (-1);

     if (frame_len > skb_wrsz(dst)) {
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     /* Stage values ... */
     set_fin_bit(wsf.byte1);
     set_opcode(wsf.byte1, WS_TEXT_FRAME);
     set_mask_bit(wsf.byte2);
     wsf.masking_key = 0xdeadbeef;

     /* ... write to buffer. */
     skb_put(dst, wsf.byte1);
     AZ(ws_set_payload_len(dst, wsf.payload_len, wsf.byte2));
     skb_put(dst, wsf.masking_key);

     for (int i = 0; i < wsf.payload_len; i++)
          dst->data[dst->wrpos++] =
               mask(src->data[src->rdpos++], i, wsf.masking_key);
     
     skb_compact(src);

     if (LOG_VERBOSE <= wsd_cfg->verbose)
          ws_printf(stderr, &wsf, "TX", hash);

     return 0;
}

static int
wssk_ws_recv(sk_t *sk)
{
     AN(skb_rdsz(sk->recvbuf));

     int rv, frames = 0;
     while (0 == (rv = sk->proto->decode_frame(sk))) {
          frames++;
     }

     if (frames) {
          if (0 < skb_rdsz(sk->sendbuf) && !(sk->events & EPOLLOUT))
               turn_on_events(sk, EPOLLOUT);

          if (!(sk->events & EPOLLIN) && !sk->close_on_write)
               turn_on_events(sk, EPOLLIN);
     }

     if (sk->close || sk->close_on_write)
          turn_off_events(sk, EPOLLIN);

     return rv;
}

int
stdin_recv(sk_t *sk)
{
     AN(skb_rdsz(sk->recvbuf));

     int rv, frames = 0;
     while (0 == (rv = sk->proto->decode_frame(sk)))
          frames++;

     if (frames) {
          if (!(wssk->events & EPOLLOUT)) {
               turn_on_events(wssk, EPOLLOUT);
          }

          if (!(sk->events & EPOLLIN) && sk->close_on_write) {
               turn_on_events(sk, EPOLLIN);
          }
     }

     if (sk->close || sk->close_on_write)
          turn_off_events(sk, EPOLLIN);

     if (sk->close_on_write && !(sk->events & EPOLLOUT))
          turn_on_events(sk, EPOLLOUT);

     return 0;
}

int
stdin_decode_frame(sk_t *sk)
{
     if (0 == skb_rdsz(sk->recvbuf)) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     wsframe_t wsf;
     memset(&wsf, 0, sizeof(wsframe_t));

     if (is_json) {
          wsf.payload_len = balanced_span(sk->recvbuf, '{', '}');
          if (-1 == wsf.payload_len) {
               wsd_errno = WSD_EINPUT;
               return (-1);
          }
     } else {
          wsf.payload_len = 32 < skb_rdsz(sk->recvbuf) ?
               32 : skb_rdsz(sk->recvbuf);
     }

     if (0 < repeat_last_num) {
          skb_reset(last_input);
          skb_copy(last_input, sk->recvbuf, wsf.payload_len);
     }

     /* wsf.payload_len = skb_wrsz(wssk->sendbuf) < skb_rdsz(sk->recvbuf) ? */
     /*      skb_wrsz(wssk->sendbuf) : skb_rdsz(sk->recvbuf); */

     return wssk->proto->encode_frame(sk, &wsf);
}

int
balanced_span(const skb_t *buf, const char begin, const char end)
{
     int num = -1;
     unsigned int pos = buf->rdpos;
     while (pos < buf->wrpos) {

          if (begin == buf->data[pos]) {

               if (-1 == num)
                    num = 1;
               else
                    num++;

          } else if (end == buf->data[pos]) {
               num--;

               if (0 == num)
                    return (pos - buf->rdpos + 1);

          }
          
          pos++;

     }

     return (-1);
}

int
on_iteration(const struct timespec *now)
{
     if (1 == check_idle_timeout(wssk, now, wsd_cfg->idle_timeout)) {
          if (!no_handshake)
               wssk_ws_start_closing_handshake();
     }

     try_repeating_last();
     
     return 0;
}

void
try_repeating_last()
{
     if (!repeat_last_armed)
          return;

     if (NULL == wssk)
          return;

     if (wssk->closing)
          return;
     
     if (0 < repeat_last_num) 
          repeat_last();

     if (0 == repeat_last_num)
          repeat_last_armed = false;

     if (0 == repeat_last_num && -1 == wsd_cfg->idle_timeout) {
          if (!no_handshake)
               wssk_ws_start_closing_handshake();
     }
}

int
repeat_last()
{
     if (0 == skb_rdsz(last_input)) {
          /* Never sent anything, nothing to repeat. */
          repeat_last_num = 0;
          return 0;
     }

     sk_t *sk = malloc(sizeof(sk_t));
     A(sk);
     AZ(sock_init(sk, -1, 0ULL));

     int rv = 0;
     while (repeat_last_num) {

          if (skb_wrsz(sk->recvbuf) < skb_rdsz(last_input)) {
               wsd_errno = WSD_EAGAIN;
               rv = -1;
               break;
          }

          skb_copy(sk->recvbuf, last_input, skb_rdsz(last_input));
          rv = stdin_decode_frame(sk);
          if (0 == rv)
               repeat_last_num--;

          /* A serious problem, don't try repeating more frame(s). */
          if (0 > rv && wsd_errno != WSD_EAGAIN)
               repeat_last_num = 0;
     }

     sock_destroy(sk);
     free(sk);

     return (0 == repeat_last_num ? 0 : rv);
}

int
skb_put_http_req(skb_t *buf, http_req_t *req)
{
     skb_put_chunk(buf, req->method);
     skb_put(buf, (char)' ');
     skb_put_chunk(buf, req->req_target);
     skb_put(buf, (char)' ');
     skb_put_chunk(buf, req->http_ver);
     skb_put_strn(buf, "\r\n", 2);
     skb_put_strn(buf, "Upgrade: websocket\r\n", 20);
     skb_put_strn(buf, "Connection: Upgrade\r\n", 21);
     skb_put_strn(buf, "Host: ", 6);
     skb_put_chunk(buf, req->host);
     skb_put_strn(buf, "\r\n", 2);

     if (wsd_cfg->tls)
          skb_put_strn(buf, "Origin: https://", 16);
     else
          skb_put_strn(buf, "Origin: http://", 15);
     skb_put_chunk(buf, req->origin);
     skb_put_strn(buf, "\r\n", 2);

     if (req->sec_ws_proto.len) {
          skb_put_strn(buf, WS_PROTO, 24);
          skb_put_chunk(buf, req->sec_ws_proto);
          skb_put_strn(buf, "\r\n", 2);
     }

     skb_put_strn(buf, "Pragma: no-cache\r\n", 18);
     skb_put_strn(buf, "Cache-Control: no-cache\r\n", 25);
     skb_put_strn(buf, "Sec-WebSocket-Key: ", 19);
     skb_put_chunk(buf, req->sec_ws_key);
     skb_put_strn(buf, "\r\n", 2);
     skb_put_strn(buf, WS_VER, 23);
     skb_put_chunk(buf, req->sec_ws_ver);
     skb_put_strn(buf, "\r\n", 2);
     skb_put_strn(buf, "User-Agent: ", 12);
     skb_put_chunk(buf, req->user_agent);
     skb_put_strn(buf, "\r\n\r\n", 4);

     return 0;
}

void
print_help(const char *bin)
{
     printf("Usage: %s [OPTIONS]... [URI]\n", bin);
     fputs("\
Write to and read from remote system HOST via websocket protocol.\n\n\
  -A, --user-agent    set User-Agent string in HTTP upgrade request\n\
  -V, --sec-ws-ver    set Sec-WebSocket-Version string\n\
  -P, --sec-ws-proto  set Sec-WebSocket-Protocol string\n\
  -K, --sec-ws-key    set Sec-WebSocket-Key string\n\
  -i, --idle-timeout  idle read/write timeout in milliseconds\n\
  -j, --json          assume JSON-formatted input\n\
  -R, --repeat-last   repeat last input as many times as specified\n\
  -N, --no-handshake  do not start or finish a closing handshake\n\
  -v, --verbose       be verbose (use multiple times for maximum effect)\n\
"
#ifdef HAVE_LIBSSL
"\
  -x, --no-check-certificate\n\
                      do not check server certificate\n"
#endif
"\
  -h, --help          display this help and exit\n\n\
", stdout);
     printf("Version %s - Please send bug reports to: %s\n",
            PACKAGE_VERSION,
            PACKAGE_BUGREPORT);
}

int
wssk_ws_decode_frame(sk_t *sk)
{
     if (skb_rdsz(sk->recvbuf) < WS_UNMASKED_FRAME_LEN) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     unsigned int old_rdpos = sk->recvbuf->rdpos;

     wsframe_t wsf;
     memset(&wsf, 0, sizeof(wsframe_t));
     skb_get(sk->recvbuf, wsf.byte1);
     skb_get(sk->recvbuf, wsf.byte2);

     /* See section 5.2 RFC6455 */
     if (RSV1_BIT(wsf.byte1) != 0 ||
         RSV2_BIT(wsf.byte1) != 0 ||
         RSV3_BIT(wsf.byte1) != 0 ||
         MASK_BIT(wsf.byte2) != 0) {

          wsd_errno = WSD_EBADREQ;
          return (-1);
     }

     wsf.payload_len = ws_decode_payload_len(sk->recvbuf, wsf.byte2);
     if (0 > wsf.payload_len) {
          skb_rd_reset(sk->recvbuf, old_rdpos);
          return (-1);
     }

     if (LOG_VERBOSE <= wsd_cfg->verbose)
          ws_printf(stderr, &wsf, "RX", sk->hash);

     /* Protect against really large frames */
     if (wsf.payload_len > sizeof(sk->recvbuf->data)) {
          skb_rd_reset(sk->recvbuf, old_rdpos);
          wsd_errno = WSD_EBADREQ;
          return (-1);
     }

     if (wsf.payload_len > skb_rdsz(sk->recvbuf)) {
          skb_rd_reset(sk->recvbuf, old_rdpos);
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     A(wsf.payload_len <= skb_rdsz(sk->recvbuf));

     int rv = 0;
     switch (OPCODE(wsf.byte1)) {
     case WS_TEXT_FRAME:
     case WS_BINARY_FRAME:
     case WS_FRAG_FRAME:
          AZ(skb_print(stdout, sk->recvbuf, wsf.payload_len));
          skb_compact(sk->recvbuf);
          break;
     case WS_CLOSE_FRAME:
          if (no_handshake) {
               skb_reset(sk->recvbuf);
          } else {
               rv = ws_finish_closing_handshake(sk, true, wsf.payload_len);
          }
          break;
     case WS_PING_FRAME:
     case WS_PONG_FRAME:
          /* TODO */
          wsd_errno = WSD_EBADREQ;
          rv = -1;
     }

     return rv;
}

void
wssk_ws_start_closing_handshake()
{
     if (0 > wssk->proto->start_closing_handshake(wssk, WS_1000, true))
          done = true;
}

#ifdef HAVE_LIBSSL
int
wssk_tls_init(sk_t *sk, const char *hostname)
{
     if (!(sk->sslctx = SSL_CTX_new(TLS_client_method()))) {
          ERR_print_errors_fp(stderr);
          return (-1);
     }

     if (!SSL_CTX_set_min_proto_version(sk->sslctx, TLS1_VERSION)) {
          ERR_print_errors_fp(stderr);
          return (-1);
     }

     SSL_CTX_set_options(sk->sslctx, SSL_OP_ALL | SSL_OP_SINGLE_DH_USE);
     if (!SSL_CTX_set_default_verify_paths(sk->sslctx)) {
          ERR_print_errors_fp(stderr);
          return (-1);
     }

     // TODO Could set cipher list here

     int mode = SSL_VERIFY_PEER;
     if (wsd_cfg->no_check_cert)
          mode = SSL_VERIFY_NONE;
     SSL_CTX_set_verify(sk->sslctx, mode, NULL);

     if (!(sk->ssl = SSL_new(sk->sslctx))) {
          ERR_print_errors_fp(stderr);
          return (-1);
     }
     SSL_set_connect_state(sk->ssl);
     if (0 >= SSL_set1_host(sk->ssl, hostname)) {
          ERR_print_errors_fp(stderr);
          return (-1);
     }
     SSL_set_hostflags(sk->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
     if (!SSL_set_fd(sk->ssl, sk->fd)) {
          ERR_print_errors_fp(stderr);
          return (-1);
     }
     if (!SSL_set_tlsext_host_name(sk->ssl, hostname)) {
          ERR_print_errors_fp(stderr);
          return (-1);
     }

     sk->ops->read = wssk_tls_handshake_read;
     sk->ops->write = wssk_tls_handshake_write;
     
     return 0;
}

int
wssk_tls_handshake_read(sk_t *sk)
{
     turn_off_events(sk, EPOLLIN);
     return wssk_tls_handshake(sk);
}

int
wssk_tls_handshake_write(sk_t *sk)
{
     turn_off_events(sk, EPOLLOUT);
     return wssk_tls_handshake(sk);
}

int
wssk_tls_handshake(sk_t *sk)
{
     int ret;
     if (0 >= (ret = SSL_do_handshake(sk->ssl))) {
          int rv = SSL_get_error(sk->ssl, ret);
          if (rv == SSL_ERROR_WANT_READ)
               turn_on_events(sk, EPOLLIN);
          else if (rv == SSL_ERROR_WANT_WRITE)
               turn_on_events(sk, EPOLLOUT);
          else {
               print_tls_error();
               return (-1);
          }
     } else {
          if (LOG_VERBOSE <= wsd_cfg->verbose)
               fprintf(stderr, "%s: TLS negotiated\n", bin);
          sk->ops->read = wssk_tls_read;
          sk->ops->write = wssk_tls_write;
          if (0 > create_http_req(sk))
               return (-1);
          turn_on_events(sk, EPOLLOUT|EPOLLIN);
     }
     return 0;
}

int
wssk_tls_read(sk_t *sk)
{
     A(0 <= sk->fd);

     if (0 == skb_wrsz(sk->recvbuf)) {
          turn_off_events(sk, EPOLLIN);
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     A(0 < skb_wrsz(sk->recvbuf));
     int len = SSL_read(sk->ssl,
                        &sk->recvbuf->data[sk->recvbuf->wrpos],
                        skb_wrsz(sk->recvbuf));

     if (len > 0) {
          sk->recvbuf->wrpos += len;
          return 0;
     }

     int rv = SSL_get_error(sk->ssl, len);
     if (rv == SSL_ERROR_WANT_READ) {
          /* Noop - try again. */
          return 0;
     } else if (rv == SSL_ERROR_WANT_WRITE) {
          if (LOG_VERBOSE <= wsd_cfg->verbose)
               fprintf(stderr, "%s: TLS renegotiation upon read\n", bin);
          sk->ops->read = wssk_tls_handshake_read;
          sk->ops->write = wssk_tls_handshake_write;
          turn_on_events(sk, EPOLLOUT);
          return 0;
     }

     print_tls_error();
     return (-1);
}

int
wssk_tls_write(sk_t *sk)
{
     if (0 == skb_rdsz(sk->sendbuf)) {
          turn_off_events(sk, EPOLLOUT);
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     A(0 < skb_rdsz(sk->sendbuf));
     int len = SSL_write(sk->ssl,
                         &sk->sendbuf->data[sk->sendbuf->rdpos],
                         skb_rdsz(sk->sendbuf));

     if (len > 0) {
          sk->sendbuf->rdpos += len;
          skb_compact(sk->sendbuf);
          return 0;
     }

     int rv = SSL_get_error(sk->ssl, len);
     if (rv == SSL_ERROR_WANT_WRITE) {
          /* Noop - try again. */
          return 0;
     } else if (rv == SSL_ERROR_WANT_READ) {
          if (LOG_VERBOSE <= wsd_cfg->verbose)
               fprintf(stderr, "%s: TLS renegotiation upon write\n", bin);
          sk->ops->read = wssk_tls_handshake_read;
          sk->ops->write = wssk_tls_handshake_write;
          turn_on_events(sk, EPOLLIN);
          return 0;
     }

     print_tls_error();
     return (-1);
}

void
print_tls_error()
{
     unsigned long code;
     while (0 != (code = ERR_get_error()))
          fprintf(stderr,
                  "%s: %s\n",
                  bin,
                  ERR_error_string(code, NULL));
}
#endif /* #ifdef HAVE_LIBSSL */
