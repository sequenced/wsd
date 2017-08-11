#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <ctype.h>
#include "parser.h"

#define CRLF 0x0a0d
#define HTTP_version 0x312e312f50545448

extern unsigned int wsd_errno;

static unsigned int
is_rfc7230_start_line(char c)
{
     if ('A' <= c && c <= 'Z')
          return 1;
     else if ('a' <= c && c <= 'z')
          return 1;
     else if ('0' <= c && c <= '9')
          return 1;

     switch (c) {
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

     switch (c) {
     case '\t':
     case '=':
     case '_':
     case ';':
     case ',':
     case '(':
     case ')':
     case '+':
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

     switch (c) {
     case '-':
          break;
     default:
          return 0;
     }

     return 1;
}

int
http_header_tok(char *s, chunk_t *result)
{
     static char *prev = NULL;

     char *cur = s;
     if (NULL == cur)
          cur = prev;
     else
          prev = NULL;

     result->p = NULL;
     result->len = 0;
     wsd_errno = 0;
     
     if (NULL == cur) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     char *start = cur;
     
     if (NULL == prev)
          /* invocation with new input */
          while (is_rfc7230_start_line(*cur++));
     else {
          /* resuming with saved input */
          while (is_rfc7230_header_field(*cur++));

          if (NULL == cur) {
               wsd_errno = WSD_EEOS;
               return (-1);
          } else if (':' != *(char*)(cur - 1))
               goto finish;

          while (is_rfc7230_field_value(*cur++));
     }

finish:
     /* cur should point to CRLF; otherwise it's an error */
     if (NULL == cur) {
          wsd_errno = WSD_EEOS;
          return (-1);
     } else if (CRLF != *(short*)(cur - 1)) {
          /* Implementing as MUST; see RFC7230, section 3.5 */
          wsd_errno = WSD_ECHAR;
          return (-1);
     }

     prev = cur + 1; /* exited w/ cur = lf; skip LF */
     result->p = start;
     result->len = cur - start - 1; /* excludes CRLF */
     
     return result->len;
}

/* Parses HTTP 1.1 request line as per RFC7230 */
int
parse_request_line(chunk_t *tok, http_req_t *req)
{
     if (NULL == tok || 0 == tok->len) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     int i = 0;
     char *cur = tok->p;
     /* Request method is case-sensitive; see section 3.1.1 */
     while (i++ < tok->len)
          if (!isupper(*cur++))
               break;

     int len = cur - tok->p;
     if (0 == len || ' ' != *(cur - 1)) {
          wsd_errno = WSD_ECHAR;
          return (-1);
     }

     req->method.p = tok->p;
     req->method.len = len - 1; /* excludes SP */

     char *start = cur;
     while (i++ < tok->len)
          if (' ' == *cur++)
               break;

     len = cur - start;
     if (0 == len || i == len) {
          wsd_errno = WSD_ECHAR;
          return (-1);
     }

     req->req_target.p = start;
     req->req_target.len = len - 1; /* excludes SP */

     if (8 != (tok->len - i)
         || HTTP_version != *(long long*)(cur)) {
          wsd_errno = WSD_ECHAR;
          return (-1);
     }

     req->http_ver.p = start;
     req->http_ver.len = 8;

     return 0;
}

#define ASSIGN(to)                              \
     to.p = tok->p + len;               \
     to.len = tok->len - len;

#define CMP(to, to_len)                         \
     (0 == strncasecmp(tok->p, to, to_len))

int
parse_header_field(chunk_t *tok, http_req_t *req)
{
     char *cur = tok->p;
     while (':' != *cur++);

     int len = cur - tok->p; /* inclusive colon */

     if (CMP("Host", 4)) {
          ASSIGN(req->host);
          return 1;
     } else if (CMP("Upgrade", 7)) {
          ASSIGN(req->upgrade);
          return 1;
     } else if (CMP("Connection", 10)) {
          /*
           * Connection header field can appear multiple times
           * and implementations are supposed to combine values
           * in an ordered list. See section 6.1. But I don't
           * like allocating memory thus this implementation
           * assumes Connection appears at most two times.
           */
          if (0 == req->conn.len) {
               ASSIGN(req->conn);
          } else {
               ASSIGN(req->conn2);
          }
          
          return 1;
     } else if (CMP("Sec-WebSocket-Key", 17)) {
          ASSIGN(req->sec_ws_key);
          return 1;
     } else if (CMP("Sec-WebSocket-Version", 21)) {
          ASSIGN(req->sec_ws_ver);
          return 1;
     } else if (CMP("Sec-WebSocket-Protocol", 22)) {
          ASSIGN(req->sec_ws_proto);
          return 1;
     } else if (CMP("Sec-WebSocket-Extensions", 24)) {
          ASSIGN(req->sec_ws_ext);
          return 1;
     } else if (CMP("Origin", 6)) {
          ASSIGN(req->origin);
          return 1;
     } else if (CMP("User-Agent", 10)) {
          ASSIGN(req->user_agent);
          return 1;
     }

     return 0;
}

/* "," delimits s; if not then simply returns result = s */
int
http_field_value_tok(chunk_t *s, chunk_t *result)
{
     static chunk_t *prev = NULL;

     chunk_t *cur = s;
     if (NULL == cur)
          cur = prev;
     else
          prev = NULL;

     result->p = NULL;
     result->len = 0;
     
     if (NULL == cur) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     if (0 > cur->len) {
          wsd_errno = WSD_EINPUT;
          return (-1);
     }

     char *start = cur->p;
     int found = 0, len = cur->len;
     while (cur->len--) {
          if (',' == *cur->p++) {
               found = 1;
               break;
          }
     }

     prev = cur;
     result->p = start;
     if (found)
          result->len = cur->p - start - 1; /* minus comma */
     else
          result->len = len;

     return result->len;
}
