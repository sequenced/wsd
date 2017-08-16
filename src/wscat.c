#include <time.h>
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

#include "ws.h"
#include "types.h"
#include "common.h"
#include "parser.h"

#define skb_put_chunk(dst, src)                 \
     skb_put_strn(dst, src.p, src.len)

#define MAX_EVENTS       256
#define DEFAULT_TIMEOUT  128

int epfd = -1;
int wsd_errno = 0;
wsd_config_t *wsd_cfg = NULL;
struct timespec *ts_now = NULL;
sk_t *pp2sk = NULL; /* TODO avoid this reference */

static struct option long_opt[] = {
     {"sec-ws-ver",   required_argument, 0, 'V'},
     {"sec-ws-proto", required_argument, 0, 'P'},
     {"sec-ws-key",   required_argument, 0, 'K'},
     {"user-agent",   required_argument, 0, 'A'},
     {"idle-timeout", required_argument, 0, 'i'},
     {"json",         required_argument, 0, 'j'},
     {"verbose",      no_argument,       0, 'v'},
     {"help",         no_argument,       0, 'h'},
     {0, 0, 0, 0}
};
static const char *optstring = "V:P:K:A:i:vhj";
static sk_t *fdin = NULL;
static sk_t *wssk = NULL;
static bool done = false;
static bool is_json = false;

static int wssk_init();
static int io_loop(void);
static int stdin_close(sk_t *sk);
static int wssk_close(sk_t *sk);
static int post_read(sk_t *sk);
static int wssk_http_recv(sk_t *sk);
static int stdin_recv(sk_t *sk);
static int stdin_decode_frame(sk_t *sk);
static int wssk_ws_recv(sk_t *sk);
static int wssk_ws_decode_frame(sk_t *sk);
static int wssk_ws_encode_frame(sk_t *sk, wsframe_t *wsf);
static int wssk_ws_start_closing_handshake();
static int wssk_ws_encode_data_frame(skb_t *dst,
                                     skb_t *src,
                                     unsigned int maxlen);
static void on_iteration();
static void check_idle_timeout();
static bool timed_out_idle();
static int skb_put_http_req(skb_t *buf, http_req_t *req);
static int balanced_span(const skb_t *buf, const char begin, const char end);
static void print_help();

int
main(int argc, char **argv)
{
     int opt;
     bool is_json_arg = false;
     int idle_timeout_arg = -1;
     int verbose_arg = 0;
     char *fwd_hostname_arg = "localhost";
     char *fwd_port_arg = "6084";
     char *user_agent_arg = "wscat";
     char *sec_ws_proto_arg = NULL;
     char *sec_ws_ver_arg = "13";
     char *sec_ws_key_arg = "B9Pc1t39Tqoj+fidr/bzeg==";

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
          case 'h':
               print_help(argv[0]);
               exit(EXIT_SUCCESS);
               break;
          default:
               fprintf(stderr,
                       "Try '%s --help' for more information.\n",
                       argv[0]);
               exit(EXIT_FAILURE);
          };
     }

     if (optind < argc)
          fwd_hostname_arg = argv[optind++];

     if (optind < argc)
          fwd_port_arg = argv[optind];

     is_json = is_json_arg;

     wsd_cfg = malloc(sizeof(wsd_config_t));
     A(wsd_cfg);
     memset(wsd_cfg, 0, sizeof(wsd_config_t));

     ts_now = malloc(sizeof(struct timespec));
     A(ts_now);
     memset(ts_now, 0, sizeof(struct timespec));

     wsd_cfg->idle_timeout = idle_timeout_arg;
     wsd_cfg->fwd_hostname = fwd_hostname_arg;
     wsd_cfg->fwd_port = fwd_port_arg;
     wsd_cfg->verbose = verbose_arg;

     if (0 > wssk_init())
          exit(EXIT_FAILURE);

     /* Kick off handshake with HTTP upgrade request. */
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

     A(req.host.p = malloc(strlen(wsd_cfg->fwd_hostname) + 1));
     strcpy(req.host.p, wsd_cfg->fwd_hostname);
     req.host.len = strlen(wsd_cfg->fwd_hostname);

     A(req.origin.p = malloc(strlen(wsd_cfg->fwd_hostname) + 1));
     strcpy(req.origin.p, wsd_cfg->fwd_hostname);
     req.origin.len = strlen(wsd_cfg->fwd_hostname);

     if (sec_ws_proto_arg) {
          A(req.sec_ws_proto.p = malloc(strlen(sec_ws_proto_arg) + 1));
          strcpy(req.sec_ws_proto.p, sec_ws_proto_arg);
          req.sec_ws_proto.len = strlen(sec_ws_proto_arg);
     }

     A(req.sec_ws_key.p = malloc(strlen(sec_ws_key_arg) + 1));
     strcpy(req.sec_ws_key.p, sec_ws_key_arg);
     req.sec_ws_key.len = strlen(sec_ws_key_arg);

     A(req.sec_ws_ver.p = malloc(strlen(sec_ws_ver_arg) + 1));
     strcpy(req.sec_ws_ver.p, sec_ws_ver_arg);
     req.sec_ws_ver.len = strlen(sec_ws_ver_arg);

     A(req.user_agent.p = malloc(strlen(user_agent_arg) + 1));
     strcpy(req.user_agent.p, user_agent_arg);
     req.user_agent.len = strlen(user_agent_arg);
     
     AZ(skb_put_http_req(wssk->sendbuf, &req));

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          unsigned int len = skb_rdsz(wssk->sendbuf);
          skb_print(stdout, wssk->sendbuf, len);
          skb_rd_rewind(wssk->sendbuf, len);
     }

     turn_on_events(wssk, EPOLLOUT);

     int rv = io_loop();

     AZ(close(epfd));

     free(ts_now);
     free(wsd_cfg);

     return rv;
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
     int rv = getaddrinfo(wsd_cfg->fwd_hostname,
                          wsd_cfg->fwd_port,
                          &hints,
                          &res);
     if (0 > rv) {
          fprintf(stderr, "wscat: cannot resolve: %s\n", gai_strerror(rv));
          AZ(close(fd));
          return (-1);
     }

     if (AF_INET != res->ai_family) {
          fprintf(stderr,
                  "wscat: unexpected address family: %d\n",
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
          fprintf(stderr, "wscat: connect: %s\n", strerror(errno));
          AZ(close(fd));
          return (-1);
     }

     wssk = malloc(sizeof(sk_t));
     if (!wssk) {
          perror("malloc");
          return (-1);
     }
     memset(wssk, 0, sizeof(sk_t));

     if (0 > sock_init(wssk, fd, -1ULL)) {
          fprintf(stderr, "wscat: sock_init: 0x%x\n", wsd_errno);
          AZ(close(fd));
          return (-1);
     }

     wssk->ops->recv = wssk_http_recv;
     wssk->ops->close = wssk_close;
     wssk->proto->decode_frame = wssk_ws_decode_frame;
     wssk->proto->encode_frame = wssk_ws_encode_frame;

     epfd = epoll_create(1);
     A(epfd >= 0);

     AZ(register_for_events(wssk));

     return 0;
}

int
io_loop()
{
     int rv = 0;
     struct epoll_event evs[MAX_EVENTS];
     memset(&evs, 0x0, sizeof(evs));

     int timeout = DEFAULT_TIMEOUT;
     while (!done) {
          AZ(clock_gettime(CLOCK_MONOTONIC, ts_now));

          on_iteration();

          int nfd = epoll_wait(epfd, evs, MAX_EVENTS, timeout);
          if (0 > nfd && EINTR == errno)
               continue;

          A(nfd >= 0);
          A(nfd <= MAX_EVENTS);

          int n;
          for (n = 0; n < nfd; n++) {
               rv = on_epoll_event(&evs[n], post_read);
          }
     }

     return rv;
}

int
wssk_close(sk_t *sk) {
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
     if (-1 == wsd_cfg->idle_timeout)
          wssk_ws_start_closing_handshake();

     AZ(close(sk->fd));
     sock_destroy(sk);
     free(sk);

     fdin = NULL;

     return 0;
}

int
post_read(sk_t *sk)
{
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
                  "wscat: failed tokenising HTTP header: 0x%x\n",
                  wsd_errno);

          goto error;
     }

     http_req_t hreq;
     memset(&hreq, 0, sizeof(http_req_t));
     while (0 < http_header_tok(NULL, &tok)) {

          if (0 > parse_header_field(&tok, &hreq)) {
               fprintf(stderr,
                       "wscat: failed parsing HTTP header field: 0x%x\n",
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
     sock_init(fdin, 0, -1ULL);
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
                                      wsf->payload_len);
}

int
wssk_ws_encode_data_frame(skb_t *dst, skb_t *src, unsigned int maxlen)
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

     if (LOG_VERBOSE <= wsd_cfg->verbose) {
          fprintf(stderr,
                  "TX:0x%hhx|0x%hhx|opcode=%hhu|maskbit=%hhu|payload_len=%lu\n",
                  wsf.byte1,
                  wsf.byte2,
                  OPCODE(wsf.byte1),
                  MASK_BIT(wsf.byte2),
                  wsf.payload_len);
     }

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
          if (!(sk->events & EPOLLIN) && !sk->close_on_write) {
               turn_on_events(sk, EPOLLIN);
          }
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

          if (0 == num)
               return (pos - buf->rdpos);

          if (begin == buf->data[pos]) {

               if (-1 == num)
                    num = 1;
               else
                    num++;

          } else if (end == buf->data[pos]) {
               num--;
          }
          
          pos++;

     }

     return (-1);
}

void
on_iteration()
{
     check_idle_timeout();
}

void
check_idle_timeout()
{
     if (-1 == wsd_cfg->idle_timeout)
          return;

     if (NULL == wssk)
          return;

     if (wssk->closing || wssk->close || wssk->close_on_write)
          return;

     if (0 == wssk->ts_last_io.tv_sec && 0 == wssk->ts_last_io.tv_nsec)
          return;

     if (timed_out_idle(wsd_cfg->idle_timeout))
          wssk_ws_start_closing_handshake();
}

int
wssk_ws_start_closing_handshake()
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s:\n", __FILE__, __LINE__, __func__);
     }

     A(wssk);
     AZ(wssk->closing);

     int rv = ws_start_closing_handshake(wssk, WS_1011, true);
     if (0 > rv) {
          done = true;
          return (-1);
     }

     if (!(wssk->events & EPOLLOUT)) {
          turn_on_events(wssk, EPOLLOUT);
     }

     return 0;
}

bool
timed_out_idle(int timeout_in_millis)
{
     time_t tv_sec_diff = ts_now->tv_sec - wssk->ts_last_io.tv_sec;
     long tv_nsec_diff = ts_now->tv_nsec - wssk->ts_last_io.tv_nsec;

     unsigned long int diff_in_millis = tv_sec_diff * 1000;
     diff_in_millis += (tv_nsec_diff / 1000000);

     return (diff_in_millis > timeout_in_millis ? true : false);
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
     printf("Usage: %s [OPTION]... [HOST [PORT]]\n", bin);
     fputs("\
Write to and read from remote system HOST via websocket protocol.\n\n\
  -A, --user-agent    set User-Agent string in HTTP upgrade request\n\
  -V, --sec-ws-ver    set Sec-WebSocket-Version string\n\
  -P, --sec-ws-proto  set Sec-WebSocket-Protocol string\n\
  -K, --sec-ws-key    set Sec-WebSocket-Key string\n\
  -i, --idle-timeout  idle read/write timeout in milliseconds\n\
  -j, --json          assume JSON-formatted input\n\
  -v, --verbose       be verbose (use multiple times for maximum effect)\n\
  -h, --help          display this help and exit\n\
", stdout);
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

     if (LOG_VERBOSE <= wsd_cfg->verbose) {
          fprintf(stderr,
                  "RX:0x%hhx|0x%hhx|opcode=%hhu|maskbit=%hhu|payload len=%lu\n",
                  wsf.byte1,
                  wsf.byte2,
                  OPCODE(wsf.byte1),
                  MASK_BIT(wsf.byte2),
                  wsf.payload_len);
     }

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

     int rv = 0;
     switch (OPCODE(wsf.byte1)) {
     case WS_TEXT_FRAME:
     case WS_BINARY_FRAME:
     case WS_FRAG_FRAME:
          AZ(skb_print(stdout, sk->recvbuf, wsf.payload_len));
          skb_compact(sk->recvbuf);
          break;
     case WS_CLOSE_FRAME:
          rv = ws_finish_closing_handshake(sk, true);
          break;
     case WS_PING_FRAME:
     case WS_PONG_FRAME:
          /* TODO */
          wsd_errno = WSD_EBADREQ;
          rv = -1;
     }

     return rv;
}
