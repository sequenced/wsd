#ifndef __PARSER_H__
#define __PARSER_H__

#include "wstypes.h"

#define EUNEXPECTED_EOS  (-100)
#define EINPUT           (-101)
#define EUNEXPECTED_CHAR (-102)

int http_header_tok(char *s, string_t *result);
int parse_request_line(string_t *tok, http_req_t *req);
int parse_header_field(string_t *tok, http_req_t *req);
int http_field_value_tok(string_t *s, string_t *result);

#endif /* #ifndef __PARSER_H__ */
