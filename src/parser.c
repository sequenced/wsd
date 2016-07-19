#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include "parser.h"

#define EUNEXPECTED_EOS  (-100)
#define EINPUT           (-101)
#define EUNEXPECTED_CHAR (-102)

#define crlf 0x0a0d

static char *prev = NULL;

static unsigned int
is_rfc7230_start_line(char c)
{
     if ('A' <= c && c <= 'Z')
          return 1;
     else if ('a' <= c && c <= 'z')
          return 1;
     else if ('0' <= c && c <= '9')
          return 1;

     switch (c)
     {
     case ' ':
     case '/':
     case '.':
     case '?':
     case '-':
     case ':':
     case '*':
          break;
     default:
          return 0;
     }

     return 1;
}

static unsigned int
is_rfc7230_field_value(char c)
{
     if (is_rfc7230_start_line(c))
          return 1;

     switch (c)
     {
     case '\t':
     case '=':
     case '_':
     case ';':
     case ',':
     case '(':
     case ')':
          break;
     default:
          return 0;
     }
     return 1;
}

static unsigned int
is_rfc7230_header_field(char c)
{
     if ('A' <= c && c <= 'Z')
          return 1;
     else if ('a' <= c && c <= 'z')
          return 1;
     else if ('0' <= c && c <= '9')
          return 1;

     switch (c)
     {
     case '-':
          break;
     default:
          return 0;
     }

     return 1;
}

int
http_header_tok(char *s, string_t *result)
{
     char *cur = s;
     if (NULL == cur)
          cur = prev;
     else
          prev = NULL;

     result->start = NULL;
     result->len = 0;
     errno = 0;
     
     if (NULL == cur)
     {
          errno = EINPUT;
          return (-1);
     }

     const char *start = cur;
     
     if (NULL == prev)
          /* invocation with new input */
          while (is_rfc7230_start_line(*cur++));
     else
     {
          /* resuming with saved input */
          while (is_rfc7230_header_field(*cur++));

          if (NULL == cur)
          {
               errno = EUNEXPECTED_EOS;
               return (-1);
          }
          else if (':' != *(char*)(cur - 1))
               goto finish;

          while (is_rfc7230_field_value(*cur++));
     }

finish:
     /* cur should point to crlf; otherwise it's an error */
     if (NULL == cur)
     {
          errno = EUNEXPECTED_EOS;
          return (-1);
     }
     else if (crlf != *(short*)(cur - 1))
     {
          /* Implementing as MUST; see RFC7230, section 3.5 */
          errno = EUNEXPECTED_CHAR;
          return (-1);
     }

     prev = cur + 1; /* exited w/ cur = lf */
     result->start = start;
     result->len = cur - start - 1; /* excludes crlf */
     
     return result->len;
}
