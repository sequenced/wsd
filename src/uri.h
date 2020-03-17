#ifndef __URI_H__
#define __URI_H__

#include "types.h"

/* See RFC3986, section 3 for components of an URI */
typedef struct {
     chunk_t scheme;
     chunk_t host;
     chunk_t port;
     chunk_t path;
} uri_t;

int parse_uri(char *uri_arg, uri_t *uri);

#endif /* #ifndef __URI_H__ */
