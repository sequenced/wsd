#ifndef __WSTYPES_H__
#define __WSTYPES_H__

#include <sys/types.h>

#define UNASSIGNED (-1)

typedef struct
{
  uid_t uid;
  char *name;
  int sock;
  int port;
  int verbose;
  int no_fork;
} wsd_config_t;

typedef struct
{
  char *start;
  int len;
} char_range_t;

typedef struct
{
  char *p;
  int pos;
  int capacity;
  int limit;
  char swap;
} buf_t;

buf_t* buf_alloc(int capacity);
void buf_free(buf_t *b);
void buf_clear(buf_t *b);
void buf_put(buf_t *b, int len);
char buf_get(buf_t *b);
char* buf_ref(buf_t *b);
int buf_len(buf_t *b);
int buf_pos(buf_t *b);
char* buf_flip(buf_t *b);

struct wschild_conn
{
  struct pollfd *pfd;
  buf_t *buf;
  int (*on_read)(struct wschild_conn *conn);
  int (*on_write)(struct wschild_conn *conn);
};
typedef struct wschild_conn wschild_conn_t;

#endif /* #ifndef __WSTYPES_H__ */
