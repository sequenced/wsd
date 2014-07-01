#ifndef __WSTYPES_H__
#define __WSTYPES_H__

#include <sys/types.h>
#include "list.h"

#define HASH32_TABLE_SIZE  256
#define HASH_ENTRY_BUCKETS 8
#define UNASSIGNED         (-1)
#define HTTP_500 "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"

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

typedef struct
{
  char byte1;
  char byte2;
  unsigned long payload_len;
  unsigned int masking_key;
} wsframe_t;

struct wschild_conn
{
  struct pollfd *pfd;
  buf_t *buf_in;
  buf_t *buf_out;
  int (*on_read)(struct wschild_conn *conn);
  int (*on_write)(struct wschild_conn *conn);
  int (*on_data_frame)(struct wschild_conn *conn, wsframe_t *wsf);
  int close_on_write;
};
typedef struct wschild_conn wschild_conn_t;

typedef struct
{
  struct list_head list_head;
  char *url;
  char *protocol;
  int (*on_frame)(wschild_conn_t *conn, wsframe_t *wsf);
} location_config_t;

typedef struct
{
  uid_t uid;
  char *username;
  char *hostname;
  int sock;
  int port;
  int verbose;
  int no_fork;
  struct list_head list_head;
  struct list_head location_list;
} wsd_config_t;

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
short buf_get_short(buf_t *b);
int buf_get_int(buf_t *b);
long buf_get_long(buf_t *b);
char* buf_ref(buf_t *b);
int buf_len(buf_t *b);
int buf_pos(buf_t *b);
void buf_set_pos(buf_t *b, int pos);
char* buf_flip(buf_t *b);

typedef struct
{
  void *data;
  int key;
} bucket32_t;

typedef struct
{
  bucket32_t buckets[HASH_ENTRY_BUCKETS];
} hash_table_entry32_t;

typedef struct
{
  unsigned int (*hash)(int val);
  hash_table_entry32_t entries[HASH32_TABLE_SIZE];
} hash32_table_t;

void *hash32_table_lookup(hash32_table_t *t, int key);
int hash32_table_insert(hash32_table_t *t, int key, void *data);
unsigned int hash32_table_hash(int val);

#endif /* #ifndef __WSTYPES_H__ */
