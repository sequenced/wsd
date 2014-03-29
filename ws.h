#ifndef __WS_H__
#define __WS_H__

#include "wstypes.h"

int ws_on_read(wschild_conn_t *conn);
int ws_on_write(wschild_conn_t *conn);

#endif /* #ifndef __WS_H__ */
