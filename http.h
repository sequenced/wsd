#ifndef __HTTP_H__
#define __HTTP_H__

#include "wstypes.h"

int http_on_read(wschild_conn_t *conn);
int http_on_write(wschild_conn_t *conn);

#endif /* #ifndef __HTTP_H__ */
