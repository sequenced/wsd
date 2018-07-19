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
 *  0th byte          8th byte 12th byte
 *  +---------+---------+---------+---------+
 *  |           HA proxy header V2          |
 *  +---------+---------+---------+---------+
 *  |        proxy address*       |  TLVs   |  *assumes TCP/UDP over IPv4
 *  +---------+---------+---------+---------+
 *  |           payload as per TLV          |
 *  +---------+---------+---------+---------+
 *
 *  See http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
 */

#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#else
/* TODO */
#endif

#include "common.h"
#include "list.h"
#include "hashtable.h"
#include "pp2.h"

#define PP2_SIG_VER_CMD_FAM_LEN 14
#define PP2_ADDR_LEN            12
#define PP2_ADDR_TLVS_LEN       (18 + PP2_ADDR_LEN)
#define PP2_HEADER_LEN         (2 + PP2_ADDR_TLVS_LEN + PP2_SIG_VER_CMD_FAM_LEN)
#define PP2_VER_BITS(byte)      ((0xf0 & byte) >> 4)
#define PP2_CMD_BITS(byte)      (0xf & byte)
#define PP2_FAM_BITS(byte)      ((0xf0 & byte) >> 4)
#define PP2_PROTO_BITS(byte)    (0xf & byte)

sk_t *pp2sk = NULL;

extern unsigned int wsd_errno;
extern DECLARE_HASHTABLE(sk_hash, 4);
extern const wsd_config_t *wsd_cfg;
extern struct list_head *sk_list;

static const uint8_t pp2_sig_ver_cmd_fam[] =
{ 0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, /* pp2 signature               */
  0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, /* cont'd pp2 signature        */
  0x21, 0x11 };                       /* version, command and family */
static const uint8_t pp2_sig[] =
{ 0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a };

static void pp2_put_proxy_hdr_v2(skb_t *dst, const uint8_t *h);
static void pp2_put_connhash(skb_t *dst, long unsigned int hash);
static void pp2_put_payloadlen(skb_t *dst, unsigned int len);
static long int pp2_get_connhash(skb_t *src);
static int pp2_get_payloadlen(skb_t *src);
static void pp2_printf(FILE *stream, char *p);
static void pp2_encode_header(skb_t *buf,
                              struct sockaddr_in *src,
                              struct sockaddr_in *dst,
                              const unsigned long int hash,
                              const unsigned long int payload_len);

int
pp2_recv(sk_t *sk)
{
     AN(skb_rdsz(sk->recvbuf));
     AN(pp2sk);

     int rv, frames = 0;
     while (0 == (rv = sk->proto->decode_frame(sk)))
          frames++;

     if (frames) {
          if (!sk->events & EPOLLIN) {
               turn_on_events(sk, EPOLLIN);
          }
     }

     return rv;
}

int
pp2_decode_frame(sk_t *sk)
{
     if (PP2_HEADER_LEN >= skb_rdsz(sk->recvbuf)) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     unsigned int old_rdpos = sk->recvbuf->rdpos;

     struct proxy_hdr_v2 *hdr =
          (struct proxy_hdr_v2*)&sk->recvbuf->data[sk->recvbuf->rdpos];
     sk->recvbuf->rdpos += sizeof(struct proxy_hdr_v2);

     if (0 != memcmp(hdr->sig, pp2_sig, sizeof(pp2_sig)))
          goto error;

     if (0x2 != PP2_VER_BITS(hdr->ver_cmd) ||
         0x1 != PP2_CMD_BITS(hdr->ver_cmd) ||
         0x1 != PP2_FAM_BITS(hdr->fam)     ||
         0x1 != PP2_PROTO_BITS(hdr->fam))
          goto error;
     
     union proxy_addr *addr =
          (union proxy_addr*)&sk->recvbuf->data[sk->recvbuf->rdpos];
     sk->recvbuf->rdpos += PP2_ADDR_LEN;
     
     long int val = pp2_get_connhash(sk->recvbuf);
     if (0 > val)
          goto error;

     unsigned long int hash = (unsigned long int)val;

     val = pp2_get_payloadlen(sk->recvbuf);
     if (0 > val)
          goto error;

     unsigned int len = (unsigned int)val;

     if (skb_rdsz(sk->recvbuf) < len) {
          sk->recvbuf->rdpos = old_rdpos;
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     sk_t *cln_sk = NULL;
     hash_for_each_possible(sk_hash, cln_sk, hash_node, hash) {
          if (cln_sk->hash == hash)
               break;
     }

     if (NULL == cln_sk) {
          sk->recvbuf->rdpos += len;
          skb_compact(sk->recvbuf);
          return 0;
     }

     wsframe_t wsf;
     memset(&wsf, 0, sizeof(wsf));
     wsf.payload_len = len;
     
     return cln_sk->proto->encode_frame(cln_sk, &wsf);

     error:
     sk->recvbuf->rdpos = old_rdpos;
     wsd_errno = WSD_ENUM;
 
     return (-1);
}

int
pp2_encode_frame(sk_t *sk, wsframe_t *wsf)
{
     if ((wsf->payload_len + PP2_HEADER_LEN) > skb_wrsz(pp2sk->sendbuf)) {
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     pp2_encode_header(pp2sk->sendbuf,
                       &sk->src_addr,
                       &sk->dst_addr,
                       sk->hash,
                       wsf->payload_len);

     if (LOG_VVERBOSE <= wsd_cfg->verbose) {
          pp2_printf(stdout,
                     &pp2sk->sendbuf->data[pp2sk->sendbuf->wrpos -
                                            PP2_HEADER_LEN]);
     }

     unsigned int len = wsf->payload_len;
     while (len--)
          pp2sk->sendbuf->data[pp2sk->sendbuf->wrpos++] =
               sk->recvbuf->data[sk->recvbuf->rdpos++];

     skb_compact(sk->recvbuf);

     return 0;
}

void
pp2_encode_header(skb_t *buf,
                  struct sockaddr_in *src,
                  struct sockaddr_in *dst,
                  const unsigned long int hash,
                  const unsigned long int payload_len)
{
     memcpy(&buf->data[buf->wrpos],
            pp2_sig_ver_cmd_fam,
            PP2_SIG_VER_CMD_FAM_LEN);
     buf->wrpos += PP2_SIG_VER_CMD_FAM_LEN;

     skb_put(buf, htobe16(PP2_ADDR_TLVS_LEN));
     skb_put(buf, src->sin_addr);
     skb_put(buf, dst->sin_addr);
     skb_put(buf, src->sin_port);
     skb_put(buf, dst->sin_port);
     pp2_put_connhash(buf, hash);
     pp2_put_payloadlen(buf, payload_len);
}

inline void
pp2_put_connhash(skb_t *dst, unsigned long int hash)
{
     struct pp2_tlv *tlv = (struct pp2_tlv*)&dst->data[dst->wrpos];
     tlv->type = PP2_TYPE_CONNHASH;
     tlv->length_hi = 0;
     tlv->length_lo = sizeof(unsigned long int);
     *(unsigned long int*)tlv->value = hash;
     dst->wrpos += sizeof(struct pp2_tlv) + sizeof(unsigned long int);
}

inline void
pp2_put_payloadlen(skb_t *dst, unsigned int len)
{
     struct pp2_tlv *tlv = (struct pp2_tlv*)&dst->data[dst->wrpos];
     tlv->type = PP2_TYPE_PAYLOADLEN;
     tlv->length_hi = 0;
     tlv->length_lo = sizeof(unsigned int);
     *(unsigned int*)tlv->value = len;
     dst->wrpos += sizeof(struct pp2_tlv) + sizeof(unsigned int);
}

inline long int
pp2_get_connhash(skb_t *src)
{
     struct pp2_tlv *tlv = (struct pp2_tlv*)&src->data[src->rdpos];
     if (tlv->type != PP2_TYPE_CONNHASH ||
         tlv->length_hi != 0 ||
         tlv->length_lo != sizeof(unsigned long int))
          return (-1);

     src->rdpos += sizeof(struct pp2_tlv) + sizeof(unsigned long int);
     return *(unsigned long int*)tlv->value;
}

inline int
pp2_get_payloadlen(skb_t *src)
{
     struct pp2_tlv *tlv = (struct pp2_tlv*)&src->data[src->rdpos];
     if (tlv->type != PP2_TYPE_PAYLOADLEN ||
         tlv->length_hi != 0 ||
         tlv->length_lo != sizeof(unsigned int))
          return (-1);

     src->rdpos += sizeof(struct pp2_tlv) + sizeof(unsigned int);
     return *(unsigned int*)tlv->value;
}

inline void
pp2_printf(FILE *stream, char *p)
{
     struct proxy_hdr_v2 *hdr = (struct proxy_hdr_v2*)p;
     union proxy_addr *addr =
          (union proxy_addr*)(p + sizeof(struct proxy_hdr_v2));

     char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];

     fprintf(stream,
             "0x%hhx,0x%hhx,%hu,%s:%hu->%s:%hu\n",
             hdr->ver_cmd,
             hdr->fam,
             be16toh(hdr->len),
             inet_ntop(AF_INET,
                       (void*)&addr->ipv4_addr.src_addr,
                       src,
                       INET_ADDRSTRLEN),
             be16toh(addr->ipv4_addr.src_port),
             inet_ntop(AF_INET,
                       (void*)&addr->ipv4_addr.dst_addr,
                       dst,
                       INET_ADDRSTRLEN),
             be16toh(addr->ipv4_addr.dst_port));
}

int
pp2_close(sk_t *sk) {
     if (LOG_VVVERBOSE <= wsd_cfg->verbose)
          printf("%s:%d: %s: fd=%d\n", __FILE__, __LINE__, __func__, sk->fd);
     AZ(close(sk->fd));
     list_del(&sk->sk_node);
     sock_destroy(sk);
     free(sk);
     pp2sk = NULL;
     return 0;
}
