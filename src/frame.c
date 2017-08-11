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

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifdef WSD_DEBUG
#include <stdio.h>
#endif
#include "types.h"
#include "common.h"
#include "list.h"
#include "hashtable.h"
#include "wschild.h"
#include "frame.h"
#include "pp2.h"

extern DECLARE_HASHTABLE(sk_hash, 4);
extern unsigned int wsd_errno;
extern const wsd_config_t *wsd_cfg;
extern sk_t *pp2_sk;

static int get_frame_length(skb_t *skb);

int
frame_recv(sk_t *sk) {
     AN(skb_rdsz(sk->recvbuf));

     int rv, frames = 0;
     while (0 == (rv = frame_decode(sk)))
          frames++;

     if (frames) {
          if (!(pp2_sk->events & EPOLLOUT)) {
               turn_on_events(pp2_sk, EPOLLOUT);
          }

          if (!(sk->events & EPOLLIN)) {
               turn_on_events(sk, EPOLLIN);
          }
     }
     
     return rv;
}

int
frame_decode(sk_t *sk)
{
     unsigned int len;
     if (!(len = get_frame_length(sk->recvbuf))) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     /* TODO should be payload len plus pp2 header len */
     if ((len + 32) > skb_wrsz(pp2_sk->sendbuf)) {
          wsd_errno = WSD_EAGAIN;
          return (-1);
     }

     /* TODO put pp2 header */
     pp2_put_connhash(pp2_sk->sendbuf, sk->hash);
     pp2_put_payloadlen(pp2_sk->sendbuf, len);

     while (len--)
          pp2_sk->sendbuf->data[pp2_sk->sendbuf->wrpos++] =
               sk->recvbuf->data[sk->recvbuf->rdpos++];

     skb_compact(sk->recvbuf);

     return 0;
}

int
get_frame_length(skb_t *skb)
{
     int len = skb_rdsz(skb);
     char *s = &skb->data[skb->rdpos];
     while (len--) {
          if (*s == 'a'
              && 3 <= len
              && *(s + 1) == 'b'
              && *(s + 2) == 'c'
              && *(s + 3) == 'd')
               return (skb_rdsz(skb) - len + 3);

          s++;
     }

     return 0;
}
