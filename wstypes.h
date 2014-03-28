#ifndef __WSTYPES_H__
#define __WSTYPES_H__

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
  char *p;
  int limit;
  int capacity;
  int pos;
  int swap;
} buf_t;

typedef struct
{
  struct pollfd *pfd;
  buf_t buf;
} wschild_conn_t;

#endif /* #ifndef __WSTYPES_H__ */
