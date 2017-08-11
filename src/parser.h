#ifndef __PARSER_H__
#define __PARSER_H__

#include "types.h"

int http_header_tok(char *s, chunk_t *result);
int parse_request_line(chunk_t *tok, http_req_t *req);
int parse_header_field(chunk_t *tok, http_req_t *req);
int http_field_value_tok(chunk_t *s, chunk_t *result);

#endif /* #ifndef __PARSER_H__ */
