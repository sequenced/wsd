#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "config_parser.h"

/* wsd.conf keywords (case sensitive) */
#define CONFIG_LOCATION_KEYWORD "Location"
#define CONFIG_SET_HANDLER_KEYWORD "SetHandler"
#define CONFIG_REQUIRE_KEYWORD "Require"

int line=1;

static int parse_element(struct list_head *parent, buf_t *input);
static int parse_element_prologue(buf_t *input, string_t *prologue,
                                  string_t *path);
static int parse_element_epilogue(buf_t *input, string_t *epilogue);
static void parse_comment(buf_t *input);
static int parse_location_element(buf_t *input, string_t *key, string_t *value);
static int parse_key(buf_t *input, string_t *key);
static int parse_value(buf_t *input, string_t *value);

int
parse_config(struct list_head *parent, buf_t *input)
{
  char c;
  while ('\0'!=(c=buf_safe_get(input)))
    {
      if (c==' '|| c=='\t' || c=='\r')
        continue;

      if (c=='\n')
        {
          line++;
          continue;
        }

      if (c=='#')
        {
          parse_comment(input);
          continue;
        }

      if (c=='<')
        if (0>parse_element(parent, input))
          return -1;
    }

  return 0;
}

static int
parse_element_prologue(buf_t *input, string_t *prologue, string_t *path)
{
  int old_pos=buf_pos(input);
  prologue->start=buf_ref(input);
  char c;
  do
    c=buf_safe_get(input);
  while (c!='\0' && c!=' ' && c!='\t' && c!='\n');

  if (c=='\0')
    {
      fprintf(stderr, "syntax error on line %d\n", line);
      return -1;
    }

  if (c=='\n')
    line++;

  prologue->len=buf_pos(input)-old_pos;

  old_pos=buf_pos(input);
  path->start=buf_ref(input);
  do
    c=buf_safe_get(input);
  while (c!='\0' && c!='>' && c!='\n');

  if (c=='\0')
    {
      fprintf(stderr, "syntax error on line %d\n", line);
      return -1;
    }

  if (c=='\n')
    line++;

  path->len=buf_pos(input)-old_pos-1; /* loose trailing '>' */

  return 0;
}

static int
parse_element(struct list_head *parent, buf_t *input)
{
  string_t epilogue;
  string_t prologue;
  string_t path;
  if (0>parse_element_prologue(input, &prologue, &path))
    return -1;

  trim(&prologue);

  if (0==strncmp(CONFIG_LOCATION_KEYWORD,
                 prologue.start,
                 prologue.len<strlen(CONFIG_LOCATION_KEYWORD)?prologue.len:
                 strlen(CONFIG_LOCATION_KEYWORD)))
    {
      string_t key, value;
      if (0>parse_location_element(input, &key, &value))
        return -1;

      if (0>parse_element_epilogue(input, &epilogue))
        return -1;
      trim(&epilogue);

      if (0!=strncmp(CONFIG_LOCATION_KEYWORD,
                     epilogue.start,
                     epilogue.len<strlen(CONFIG_LOCATION_KEYWORD)?epilogue.len:
                     strlen(CONFIG_LOCATION_KEYWORD)))
        {
          fprintf(stderr, "prologue/epilogue do not match on line %d\n", line);
          return -1;
        }

      /* syntax correct, create location directive in configuration list */
      location_config_t *loc;
      if (!(loc=malloc(sizeof(location_config_t))))
        return -1;

      trim(&path);
      loc->url=malloc(path.len+1);
      memset((void*)loc->url, 0x0, path.len+1);
      strncpy(loc->url, path.start, path.len);

      trim(&value);
      loc->protocol=malloc(value.len+1);
      memset((void*)loc->protocol, 0x0, value.len+1);
      strncpy(loc->protocol, value.start, value.len);

      list_add_tail(&loc->list_head, parent);
    }
  else
    {
      fprintf(stderr, "unknown element on line %d\n", line);
      return -1;
    }

  return 0;
}

static void
parse_comment(buf_t *input)
{
  char c;
  do
    c=buf_safe_get(input);
  while (c!='\0' && c!='\n');

  if (c=='\n')
    line++;
}

static int
parse_location_element(buf_t *input, string_t *key, string_t *value)
{
  char c;
  while ('\0'!=(c=buf_safe_get(input)))
    {
      if (c==' '|| c=='\t')
        continue;

      if (c=='\n')
        {
          line++;
          continue;
        }

      if (c=='#')
        {
          parse_comment(input);
          continue;
        }

      if (c=='<')
        break;

      buf_rwnd(input, 1);
      if (0>parse_key(input, key))
        return -1;
      trim(key);

      if (0!=strncmp(key->start, CONFIG_SET_HANDLER_KEYWORD,
                     key->len<strlen(CONFIG_SET_HANDLER_KEYWORD)?key->len:
                     strlen(CONFIG_SET_HANDLER_KEYWORD)))
        {
          fprintf(stderr, "unknown key on line %d\n", line);
          return -1;
        }

      if (0>parse_value(input, value))
        return -1;
      trim(value);
    }

  if (c=='\0')
    {
      fprintf(stderr, "missing '<' on line %d\n", line);
      return -1;
    }

  return 0;
}

static int
parse_element_epilogue(buf_t *input, string_t *epilogue)
{
  if ('/'!=buf_safe_get(input))
    {
      fprintf(stderr, "missing '/' on line %d\n", line);
      return -1;
    }

  int old_pos=buf_pos(input);
  epilogue->start=buf_ref(input);

  char c;
  do
    c=buf_safe_get(input);
  while (c!='\0' && c!='>');

  if (c=='\0')
    {
      fprintf(stderr, "missing closing '>' on line %d\n", line);
      return -1;
    }

  epilogue->len=buf_pos(input)-old_pos-1; /* -1 to lose trailing '>' */
  return 0;
}

static int
parse_key(buf_t *input, string_t *key)
{
  int old_pos=buf_pos(input);
  key->start=buf_ref(input);
  char c;
  do
    c=buf_safe_get(input);
  while ('\0'!=c && '\t'!=c && ' '!=c && '\n'!=c);

  if ('\0'==c)
    {
      fprintf(stderr, "syntax error on line %d\n", line);
      return -1;
    }

  if ('\n'==c)
    line++;

  key->len=buf_pos(input)-old_pos-1; /* -1 to lose whitespace */
  return 0;
}

static int
parse_value(buf_t *input, string_t *value)
{
  int old_pos=buf_pos(input);
  value->start=buf_ref(input);
  char c;
  do
    c=buf_safe_get(input);
  while ('\0'!=c && '\n'!=c && '<'!=c);

  if ('\0'==c)
    {
      fprintf(stderr, "syntax error on line %d\n", line);
      return -1;
    }

  if ('\n'==c)
    line++;

  value->len=buf_pos(input)-old_pos-1;

  if (value->len>CONFIG_MAX_VALUE_LENGTH)
    {
      fprintf(stderr, "value too long on line %d\n", line);
      return -1;
    }

  /* put back, don't consume */
  if ('<'==c)
    buf_rwnd(input, 1);

  return 0;
}
