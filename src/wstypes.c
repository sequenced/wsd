#include <string.h>
#include <stdlib.h>
#include "wstypes.h"

inline void
trim(string_t *str)
{
     int i=0;
     while (str->len) {
          char c=*(str->start+i);
          if (' '==c || '\t'==c) {
               str->start++;
               str->len--;
               i++;
          }
          else
               break;
     }

     while (str->len) {
          char c=*(str->start+str->len-1); /* -1: 1st char at position zero */
          if (' '==c || '\t'==c)
               str->len--;
          else
               break;
     }
}

string_t *
tok(string_t *str, const char del)
{
     static int pos = 0;
     static string_t rv;
     static string_t *s;
     if (NULL != str) {
          s = str;
          pos = 0;
     }

     if (pos==s->len) {
          rv.len=(-1); /* end of input string reached */
          return &rv;
     }

     rv.start = (s->start+pos);
     rv.len = 0;
     while (pos<s->len) {
          if (del == *(s->start+pos)) {
               pos++; /* move past delimiter for next call */
               break;
          }

          rv.len++;
          pos++;
     }

     return &rv;
}

void
ep_init(ep_t *ep)
{
     memset(ep, 0, sizeof(ep_t));
     ep->fd = -1;
     ep->snd_buf = malloc(sizeof(buf2_t));
     A(ep->snd_buf);
     memset(ep->snd_buf, 0, sizeof(buf2_t));
     ep->rcv_buf = malloc(sizeof(buf2_t));
     A(ep->rcv_buf);
     memset(ep->rcv_buf, 0, sizeof(buf2_t));
}

void
ep_destroy(ep_t *ep)
{
     A(ep->snd_buf);
     free(ep->snd_buf);
     A(ep->rcv_buf);
     free(ep->rcv_buf);
     memset(ep, 0, sizeof(ep_t));
     ep->fd = -1;
}
