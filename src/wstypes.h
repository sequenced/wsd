#ifndef __WSTYPES_H__
#define __WSTYPES_H__

#include <stdlib.h>
#include <sys/types.h>
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

#define buf_read_sz(buf) (buf.wrpos - buf.rdpos)
#define buf_write_sz(buf) ((unsigned int)sizeof(buf.p) - buf.wrpos)
#define buf_reset(buf) buf.wrpos = 0; buf.rdpos = 0;
#define buf_put(buf, obj)                       \
     *(typeof(obj)*)(&buf.p[buf.wrpos]) = obj;  \
     buf.wrpos += sizeof(typeof(obj));
#define buf_get(buf, obj)                       \
     obj = *(typeof(obj)*)(&buf.p[buf.rdpos]);  \
     buf.rdpos += sizeof(typeof(obj));

#define BUF_SIZE      512
#define UNASSIGNED    (-1)
#define LOG_VVVERBOSE 3
#define LOG_VVERBOSE  2
#define HTTP_500      "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"

typedef struct {
     char *start;
     int len;
} string_t;

/* see RFC7320, section 3 */
typedef struct {
     string_t method;
     string_t req_target;
     string_t http_ver;
     string_t host;
     string_t origin;
     string_t user_agent;
     string_t conn;
     string_t conn2;
     string_t upgrade;
     string_t sec_ws_key;
     string_t sec_ws_ver;
     string_t sec_ws_ext;
     string_t sec_ws_proto;
     int is_uri_abs;
} http_req_t;

typedef struct {
     char byte1;
     char byte2;
     unsigned long payload_len;
     unsigned int masking_key;
} wsframe_t;

typedef struct {
     char p[BUF_SIZE];
     unsigned int rdpos;
     unsigned int wrpos;
} buf2_t;

struct endpoint;

typedef struct {
     int (*recv)(struct endpoint *ep);
     int (*send)(struct endpoint *ep);
} proto_t;

struct endpoint {
     long unsigned int hash;
     buf2_t snd_buf;
     buf2_t rcv_buf;
     int fd;
     int (*read)(struct endpoint *ep);
     int (*write)(struct endpoint *ep);
     int (*close)(struct endpoint *ep);
     struct hlist_node hash_node;
     unsigned char close_on_write:1;
     proto_t proto;
};
typedef struct endpoint ep_t;

typedef struct {
     uid_t uid;
     char *username;
     char *hostname;
     int lfd;
     int port;
     int verbose;
     int no_fork;
} wsd_config_t;

void trim(string_t *str);
string_t *tok(string_t *str, const char del);

#endif /* #ifndef __WSTYPES_H__ */
