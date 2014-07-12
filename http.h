#ifndef __HTTP_H__
#define __HTTP_H__

#include "wstypes.h"

int http_on_read(ws_conn_t *conn);
int http_on_write(ws_conn_t *conn);
int http_prepare_response(buf_t *b, const char *s);

#endif /* #ifndef __HTTP_H__ */
