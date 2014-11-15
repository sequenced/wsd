#ifndef __JEN_H__
#define __JEN_H__

#include <sshmem_api.h>
#include "ws_interface.h"

int jen_on_frame(wsconn_t *conn, wsframe_t *wsf, buf_t *in, buf_t *out);
int jen_on_open(const wsd_config_t *cfg, wsconn_t *conn);
void jen_on_close(wsconn_t *conn);

#endif /* #ifndef __JEN_H__ */
