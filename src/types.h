#ifndef __TYPES_H__
#define __TYPES_H__

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "list.h"

#define ALERT(func, file, line)                 \
     printf("%s:%d: %s\n", file, line, func);
#define AZ(exp)                                                         \
     if (!((exp) == 0)) { ALERT(__func__, __FILE__, __LINE__); exit(1); }
#define AN(exp)                                                         \
     if (!((exp) != 0)) { ALERT(__func__, __FILE__, __LINE__); exit(1); }
#define A(exp)                                                          \
     if (!(exp)) { ALERT(__func__, __FILE__, __LINE__); exit(1); }
#define ERRET(exp, text)                        \
     if (exp) { perror(text); return (-1); }
#define ERREXIT(exp, text)                      \
     if (exp) { perror(text); exit(1); }

#define WSD_CHECKERRNO  0 /* Check system errno variable */
#define WSD_EAGAIN      1 /* Try again */
#define WSD_EBADREQ     2 /* Bad request */
#define WSD_EEOS        3 /* Unexpected end of string */
#define WSD_ECHAR       4 /* Unexpected char */
#define WSD_EINPUT      5 /* No input */
#define WSD_ENOMEM      6 /* No memory */
#define WSD_ENUM        7 /* Unexpected numerical value */
#define WSD_EOF         8 /* End of file */

#define LOG_VVVERBOSE 3
#define LOG_VVERBOSE  2
#define LOG_VERBOSE   1

#define HTTP_500 "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"

/* chunk of bytes */
typedef struct {
     char         *p;
     unsigned int  len;
} chunk_t;

/* HTTP upgrade request (see RFC7320, section 3) */
typedef struct {
     chunk_t      method;
     chunk_t      req_target;
     chunk_t      http_ver;
     chunk_t      host;
     chunk_t      origin;
     chunk_t      user_agent;
     chunk_t      conn;
     chunk_t      conn2;
     chunk_t      upgrade;
     chunk_t      sec_ws_key;
     chunk_t      sec_ws_ver;
     chunk_t      sec_ws_ext;
     chunk_t      sec_ws_proto;
     unsigned int is_uri_abs;
} http_req_t;

/* websocket frame */
typedef struct {
     char              byte1;
     char              byte2;
     unsigned long int payload_len;
     unsigned int      masking_key;
} wsframe_t;

struct sk;

/* socket buffer */
typedef struct {
     unsigned int rdpos;
     unsigned int wrpos;
     char         data[131072];
} skb_t;

struct proto;
struct ops;

/* Structure describing file descriptor, state, operations and protocol. */
struct sk {
     int                fd;
     unsigned long int  hash;
     unsigned int       events;
     skb_t             *sendbuf;
     skb_t             *recvbuf;
     struct hlist_node  hash_node;
     struct list_head   work_node;
     struct proto      *proto;
     struct ops        *ops;
     unsigned char      close_on_write:1; /* close socket once sendbuf empty */
     unsigned char      close:1;          /* close socket */
     unsigned char      closing:1;        /* closing handshake in progress   */
     struct timespec    ts_last_io;       /* records last I/O timestamp      */
     struct sockaddr_in src_addr;         /* source address iff socket       */
     struct sockaddr_in dst_addr;         /* destination address iff socket  */
};
typedef struct sk sk_t;

struct proto {
     int (*decode_handshake)(sk_t *sk, http_req_t *req);
     int (*decode_frame)(sk_t *sk);
     int (*encode_frame)(sk_t *sk, wsframe_t *wsf);
};

struct ops {
     int (*recv)(sk_t *sk);     /* passes execution to higher-level protocol */
     int (*read)(sk_t *sk);     /* reads from socket                         */
     int (*write)(sk_t *sk);    /* writes to socket                          */
     int (*close)(sk_t *sk);    /* closes socket                             */
};

typedef struct {
     uid_t       uid;
     int         lfd;
     int         port;
     char       *fwd_port;
     char       *fwd_hostname;
     const char *pidfilename;
     int         verbose;
     int         no_fork;
     int         idle_timeout; /* Set a fixed millisecond timeout for idle
                                * connections. */
} wsd_config_t;

#endif /* #ifndef __TYPES_H__ */
