#ifndef __TYPES_H__
#define __TYPES_H__

#include "config.h"
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#ifdef HAVE_LIBSSL
#include <openssl/ssl.h>
#endif
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
     char         data[524288];
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
     struct hlist_node  hash_node;       /* Hash table of every open socket  */
     struct list_head   work_node;       /* List of work pending             */
     struct list_head   sk_node;         /* List of every open socket        */
     struct proto      *proto;
     struct ops        *ops;
     unsigned char      close_on_write:1;/* Close socket once sendbuf empty  */
     unsigned char      close:1;         /* Close socket                     */
     unsigned char      closing:1;       /* Closing handshake in progress    */
     struct timespec    ts_last_io;      /* Records time of last I/O         */
     struct timespec    ts_closing_handshake_start;
     struct sockaddr_in src_addr;        /* Source address iff socket        */
     struct sockaddr_in dst_addr;        /* Destination address iff socket   */
#ifdef HAVE_LIBSSL
     SSL_CTX           *sslctx;
     SSL               *ssl;
#endif
};
typedef struct sk sk_t;

struct proto {
     int (*decode_handshake)(sk_t *sk, http_req_t *req);
     int (*decode_frame)(sk_t *sk);                 /* Decodes single frame  */
     int (*encode_frame)(sk_t *sk, wsframe_t *wsf); /* Encodes single frame  */
     int (*start_closing_handshake)(sk_t *sk, int status, bool mask);
};

struct ops {
     int (*recv)(sk_t *sk);     /* Passes received data to protocol layer    */
     int (*read)(sk_t *sk);     /* Reads from socket                         */
     int (*write)(sk_t *sk);    /* Writes to socket                          */
     int (*close)(sk_t *sk);    /* Closes socket                             */
     int (*accept)(int fd);     /* Accepts socket                            */
};

typedef struct {
     uid_t       uid;
     int         lfd;          /* Listening socket fd iff wsd                */
     int         port;
     char       *fwd_port;
     char       *fwd_hostname;
     const char *pidfilename;
     int         verbose;
     bool        no_fork;      /* Does not fork, stays attached to terminal  */
     int         idle_timeout; /* Sets idle timeout (ms) after read/write op */
     int         closing_handshake_timeout;
     char       *user_agent;
     char       *sec_ws_proto;
     char       *sec_ws_ver;
     char       *sec_ws_key;
#ifdef HAVE_LIBSSL
     bool        tls;          /* TLS requested                              */
     bool        no_check_cert;/* TLS certificate checking not requested     */
#endif
} wsd_config_t;

#endif /* #ifndef __TYPES_H__ */
