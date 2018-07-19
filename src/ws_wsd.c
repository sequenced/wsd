/*
 *  Copyright (C) 2014-2018 Michael Goldschmidt
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

#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#include "ws_wsd.h"
#include "common.h"
#include "pp2.h"
#include "ws.h"

#define HTTP_101  "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
#define HTTP_400 "HTTP/1.1 400 Bad Request\r\nSec-WebSocket-Version: 13\r\nContent-Length: 0\r\n\r\n"

#define SCRATCH_SIZE 64

extern unsigned int wsd_errno;
extern const wsd_config_t *wsd_cfg;
extern struct list_head *sk_list;
extern sk_t *pp2sk;

static const char *FLD_SEC_WS_VER_VAL = "13";

static bool is_valid_proto(http_req_t *hr);
static bool is_valid_ver(http_req_t *hr);
static int prepare_handshake(skb_t *b, http_req_t *hr);
static int generate_accept_val(skb_t *b, http_req_t *hr);
static int sock_open();

int
ws_recv(sk_t *sk)
{
     if (LOG_VVVERBOSE <= wsd_cfg->verbose) {
          printf("%s:%d: %s: fd=%d, rdsz=%d\n",
                 __FILE__,
                 __LINE__,
                 __func__,
                 sk->fd,
                 skb_rdsz(sk->recvbuf));
     }

     AN(skb_rdsz(sk->recvbuf));

     if (NULL == pp2sk) {
          if (0 > sock_open())
               return (-1);
     }

     int rv, frames = 0;
     while (0 == (rv = sk->proto->decode_frame(sk)))
          frames++;

     if (frames) {
          if (0 < skb_rdsz(pp2sk->sendbuf) && !(pp2sk->events & EPOLLOUT))
               turn_on_events(pp2sk, EPOLLOUT);

          if (!(sk->events & EPOLLIN) && sk->close_on_write)
               turn_on_events(sk, EPOLLIN);
     }

     if (sk->close || sk->close_on_write)
          turn_off_events(sk, EPOLLIN);

     if (sk->close_on_write && !(sk->events & EPOLLOUT))
          turn_on_events(sk, EPOLLOUT);

     return rv;
}

int
ws_decode_handshake(sk_t *sk, http_req_t *req)
{
     if (!is_valid_ver(req)) {

          if (0 == skb_put_str(sk->sendbuf, HTTP_400)) {
               sk->close_on_write = 1;
          }

          goto error;
     }

     /* TODO check location */

     /* TODO make protocol configurable */
     if (!is_valid_proto(req)) {
          
          if (0 == skb_put_str(sk->sendbuf, HTTP_400)) {
               sk->close_on_write = 1;
          }

          goto error;
     }

     /* handshake syntactically and semantically correct */

     AN(skb_wrsz(sk->sendbuf));

     if (0 > prepare_handshake(sk->sendbuf, req)) {

          if (0 == skb_put_str(sk->sendbuf, HTTP_500)) {
               sk->close_on_write = 1;
          }
          
          goto error;
     }

     /* "switch" into websocket mode */
     sk->proto->decode_frame = ws_decode_frame;
     sk->proto->encode_frame = ws_encode_frame;
     sk->proto->start_closing_handshake = ws_start_closing_handshake;
     sk->ops->recv = ws_recv;

     AZ(skb_rdsz(sk->recvbuf));
     AN(skb_wrsz(sk->sendbuf));

     skb_compact(sk->recvbuf);

     if (!(sk->events & EPOLLOUT)) {
          turn_on_events(sk, EPOLLOUT);
     }

     return 0;

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

bool
is_valid_ver(http_req_t *hr)
{
     trim(&(hr->sec_ws_ver));
     if (0 != strncmp(hr->sec_ws_ver.p,
                      FLD_SEC_WS_VER_VAL,
                      hr->sec_ws_ver.len))
          return false;

     return true;
}

bool
is_valid_proto(http_req_t *hr)
{
     trim(&(hr->sec_ws_proto));
     if (0 != strncmp(hr->sec_ws_proto.p, "jen", hr->sec_ws_proto.len))
          return false;

     return true;
}

int
prepare_handshake(skb_t *b, http_req_t *req)
{
     int len = strlen(HTTP_101);
     if (skb_wrsz(b) < len)
          return -1;
     int old_wrpos = b->wrpos;
     strcpy(&b->data[b->wrpos], HTTP_101);
     b->wrpos += len;

     len = WS_ACCEPT_KEY_LEN + 2; /* +2: `\r\n' */
     if (skb_wrsz(b) < len)
          goto error;
     if (0 > generate_accept_val(b, req))
          goto error;
     len = strlen(WS_VER) + strlen(FLD_SEC_WS_VER_VAL) + 2; /* +2 `\r\n' */
     if (skb_wrsz(b) < len)
          goto error;
     strcpy(&b->data[b->wrpos], WS_VER);
     b->wrpos += strlen(WS_VER);
     strcpy(&b->data[b->wrpos], FLD_SEC_WS_VER_VAL);
     b->wrpos += strlen(FLD_SEC_WS_VER_VAL);
     skb_put(b, (char)'\r');
     skb_put(b, (char)'\n');

     /* TODO for now echo back requested protocol */
     trim(&(req->sec_ws_proto));
     len = strlen(WS_PROTO) + req->sec_ws_proto.len + 2; /* +2 `\r\n' */
     if (skb_wrsz(b) < len)
          goto error;
     strcpy(&b->data[b->wrpos], WS_PROTO);
     b->wrpos += strlen(WS_PROTO);
     strncpy(&b->data[b->wrpos], req->sec_ws_proto.p, req->sec_ws_proto.len);
     b->wrpos += req->sec_ws_proto.len;
     skb_put(b, (char)'\r');
     skb_put(b, (char)'\n');

     /* terminating response as per RFC2616 section 6 */
     if (skb_wrsz(b) < 2)
          goto error;
     skb_put(b, (char)'\r');
     skb_put(b, (char)'\n');

     return 0;

error:
     b->wrpos = old_wrpos;
     return (-1);
}

int
generate_accept_val(skb_t *b, http_req_t *hr)
{
     trim(&(hr->sec_ws_key));

     /* concatenate as per RFC6455 section 4.2.2 */
     char scratch[SCRATCH_SIZE];
     memset(scratch, 0, SCRATCH_SIZE);
     strncpy(scratch, hr->sec_ws_key.p, hr->sec_ws_key.len);
     strcpy((char*)(scratch + hr->sec_ws_key.len), WS_GUID);

     unsigned char *md = SHA1((unsigned char*)scratch,
                              strlen(scratch),
                              NULL);
     A(md);

     BIO *bio, *b64;
     BUF_MEM *p;
     b64 = BIO_new(BIO_f_base64());
     bio = BIO_new(BIO_s_mem());
     b64 = BIO_push(b64, bio);
     BIO_write(b64, md, SHA_DIGEST_LENGTH);
     (void)BIO_flush(b64);
     BIO_get_mem_ptr(b64, &p);
     memcpy(&b->data[b->wrpos], p->data, p->length - 1);
     b->wrpos += p->length - 1;

     BIO_free_all(b64);

     skb_put(b, (char)'\r');
     skb_put(b, (char)'\n');

     return 1;
}

int
sock_open()
{
     AZ(pp2sk);
     if (!(pp2sk = malloc(sizeof(sk_t)))) {
          wsd_errno = WSD_ENOMEM;
          return (-1);
     }
     memset(pp2sk, 0, sizeof(sk_t));

     int fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
     if (0 > fd) {
          free(pp2sk);
          wsd_errno = WSD_CHECKERRNO;
          return (-1);
     }

     struct addrinfo hints, *res;
     memset(&hints, 0, sizeof(struct addrinfo));
     hints.ai_family = AF_INET;
     hints.ai_socktype = SOCK_STREAM;
     hints.ai_flags = AI_NUMERICSERV;
     int rv = getaddrinfo(wsd_cfg->fwd_hostname,
                          wsd_cfg->fwd_port,
                          &hints,
                          &res);
     if (0 > rv) {
          syslog(LOG_ERR, "%s: %s", gai_strerror(rv), wsd_cfg->fwd_hostname);
          wsd_errno = WSD_CHECKERRNO;
          goto error;
     }
     AZ(res->ai_next);
     if (AF_INET != res->ai_family) {
          wsd_errno = WSD_ENUM;
          goto error;
     }

     rv = connect(fd, res->ai_addr, res->ai_addrlen);
     freeaddrinfo(res);
     if (0 > rv && EINPROGRESS != errno) {
          wsd_errno = WSD_EBADREQ;
          goto error;
     }

     if (0 > sock_init(pp2sk, fd, -1ULL))
          goto error;

     pp2sk->ops->recv = pp2_recv;
     pp2sk->ops->close = pp2_close;
     pp2sk->proto->decode_frame = pp2_decode_frame;
     pp2sk->proto->encode_frame = pp2_encode_frame;
     list_add_tail(&pp2sk->sk_node, sk_list);
     AZ(register_for_events(pp2sk));
     return 0;

error:
     AZ(close(fd));
     sock_destroy(pp2sk);
     free(pp2sk);
     pp2sk = NULL;

     return (-1);
}
