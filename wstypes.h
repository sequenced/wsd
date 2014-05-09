#ifndef __WSTYPES_H__
#define __WSTYPES_H__

#include <sys/types.h>

#define UNASSIGNED (-1)
#define HTTP_500 "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"

typedef struct
{
  uid_t uid;
  char *name;
  char *hostname;
  int sock;
  int port;
  int verbose;
  int no_fork;
} wsd_config_t;

typedef struct
{
  const char *start;
  int len;
} string_t;

typedef struct
{
  char *p;
  int pos;
  int capacity;
  int limit;
  char swap;
} buf_t;

struct wschild_conn
{
  struct pollfd *pfd;
  buf_t *buf_in;
  buf_t *buf_out;
  int (*on_read)(struct wschild_conn *conn);
  int (*on_write)(struct wschild_conn *conn);
  int close_on_write;
};
typedef struct wschild_conn wschild_conn_t;

/* as per RFC2616 section 5.1 */
typedef struct
{
  string_t method;
  string_t req_uri;
  string_t http_ver;
  string_t host;
  string_t origin;
  string_t user_agent;
  string_t conn;
  string_t upgrade;
  string_t sec_ws_key;
  string_t sec_ws_ver;
  string_t sec_ws_ext;
  string_t sec_ws_proto;
  int is_uri_abs;
} http_req_t;

/* trims whitespace from beginning and end of string */
void trim(string_t *str);
string_t *tok(string_t *str, const char del);
buf_t* buf_alloc(int capacity);
void buf_free(buf_t *b);
void buf_clear(buf_t *b);
void buf_rwnd(buf_t *b, int len);
void buf_fwd(buf_t *b, int len);
void buf_put(buf_t *b, char c);
char buf_get(buf_t *b);
char* buf_ref(buf_t *b);
int buf_len(buf_t *b);
int buf_pos(buf_t *b);
char* buf_flip(buf_t *b);

typedef struct __attribute__ ((__packed__))
{
  char byte1;
  char byte2;
  char byte3;
  char byte5;
  char byte11;
} wsframe_t;

#endif /* #ifndef __WSTYPES_H__ */
