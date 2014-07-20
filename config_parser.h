#ifndef __CONFIG_PARSER_H__
#define __CONFIG_PARSER_H__

#include "wstypes.h"

#define CONFIG_MAX_VALUE_LENGTH 64

int parse_config(struct list_head *parent, buf_t *input);

#endif /* #ifndef __CONFIG_PARSER_H__ */
