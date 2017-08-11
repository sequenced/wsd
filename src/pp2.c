/*
 * 0th byte          8th byte 12th byte
 * +---------+---------+---------+---------+
 * |           HA proxy header V2          |
 * +---------+---------+---------+---------+
 * |        proxy address*       |  TLVs   |  *assumes TCP/UDP over IPv4
 * +---------+---------+---------+---------+
 * |           payload as per TLV          |
 * +---------+---------+---------+---------+
 *
 * See http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
 */

#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>

#ifdef WSD_DEBUG
#include <stdio.h>
#endif

#include "common.h"
#include "list.h"
#include "hashtable.h"
#include "pp2.h"

#define PP2_HEADER_LEN 18

extern sk_t *pp2_sk;
extern unsigned int wsd_errno;
extern DECLARE_HASHTABLE(sk_hash, 4);

static void pp2_put_connhash(skb_t *dst, long unsigned int hash);
static void pp2_put_payloadlen(skb_t *dst, unsigned int len);
static long int pp2_get_connhash(skb_t *src);
static int pp2_get_payloadlen(skb_t *src);
static int is_complete_pp2_frame_header(skb_t *skb);

int
pp2_recv(sk_t *sk)
{
     AN(skb_rdsz(sk->recvbuf));

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
     if (!is_complete_pp2_frame_header(sk->recvbuf)) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     int old_rdpos = sk->recvbuf->rdpos;

     long int val = pp2_get_connhash(sk->recvbuf);
     if (0 > val) {
          sk->recvbuf->rdpos = old_rdpos;
          wsd_errno = WSD_ENUM;
          return (-1);
     }
     unsigned long int hash = (unsigned long int)val;

     val = pp2_get_payloadlen(sk->recvbuf);
     if (0 > val) {
          sk->recvbuf->rdpos = old_rdpos;
          wsd_errno = WSD_ENUM;
          return (-1);
     }
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
}

int
pp2_encode_frame(sk_t *sk, wsframe_t *wsf)
{
     if ((wsf->payload_len + PP2_HEADER_LEN) > skb_wrsz(pp2_sk->sendbuf)) {
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     /* TODO put pp2 header */

     pp2_put_connhash(pp2_sk->sendbuf, sk->hash);
     pp2_put_payloadlen(pp2_sk->sendbuf, wsf->payload_len);

     unsigned int len = wsf->payload_len;
     while (len--)
          pp2_sk->sendbuf->data[pp2_sk->sendbuf->wrpos++] =
               sk->recvbuf->data[sk->recvbuf->rdpos++];

     skb_compact(sk->recvbuf);

     return 0;
}

int
is_complete_pp2_frame_header(skb_t *skb)
{
     if (skb_rdsz(skb) >= PP2_HEADER_LEN)
          return 1;

     return 0;
}

void
pp2_put_connhash(skb_t *dst, unsigned long int hash)
{
     struct pp2_tlv *tlv = (struct pp2_tlv*)&dst->data[dst->wrpos];
     tlv->type = PP2_TYPE_CONNHASH;
     tlv->length_hi = 0;
     tlv->length_lo = sizeof(unsigned long int);
     *(unsigned long int*)tlv->value = hash;
     dst->wrpos += sizeof(struct pp2_tlv) + sizeof(unsigned long int);
}

void
pp2_put_payloadlen(skb_t *dst, unsigned int len)
{
     struct pp2_tlv *tlv = (struct pp2_tlv*)&dst->data[dst->wrpos];
     tlv->type = PP2_TYPE_PAYLOADLEN;
     tlv->length_hi = 0;
     tlv->length_lo = sizeof(unsigned int);
     *(unsigned int*)tlv->value = len;
     dst->wrpos += sizeof(struct pp2_tlv) + sizeof(unsigned int);
}

long int
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

int
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
