#ifndef __HTTP_H__
#define __HTTP_H__

#include "wstypes.h"

int http_read(ep_t *ep);
int http_write(ep_t *ep);
int http_prepare_response(buf2_t b, const char *s);

#endif /* #ifndef __HTTP_H__ */
