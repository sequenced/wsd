#include <stdlib.h>
#include "wstypes.h"

inline buf_t*
buf_alloc(int capacity)
{
  buf_t *b=malloc(sizeof(buf_t));
  if (NULL==b)
    return NULL;

  b->capacity=capacity;
  b->p=malloc(b->capacity);
  if (NULL==b->p)
    return NULL;

  b->pos=0;
  b->limit=b->capacity;
  b->swap='\0';

  return b;
}

inline void
buf_free(buf_t *b)
{
  if (b)
    if (b->p)
      free(b->p);

  free(b);
  b=NULL;
}

inline void
buf_clear(buf_t *b)
{
  b->limit=b->capacity;
  b->pos=0;
  b->swap='\0';
}

inline int
buf_pos(buf_t *b)
{
  return b->pos;
}

inline void
buf_put(buf_t *b, int len)
{
  b->pos+=len;
}

inline char
buf_get(buf_t *b)
{
  char c=*(b->p+b->pos);
  b->pos++;
  return c;
}

inline char*
buf_ref(buf_t *b)
{
  return (b->p+b->pos);
}

inline int
buf_len(buf_t *b)
{
  return (b->limit-b->pos);
}

inline char*
buf_flip(buf_t *b)
{
  if (b->limit<b->capacity)
    {
      b->pos=b->limit;
      *(b->p+b->pos)=b->swap;
      b->limit=b->capacity;
    }
  else
    {
      b->swap=*(b->p+b->pos);
      *(b->p+b->pos)='\0';
      b->limit=b->pos;
      b->pos=0;
    }

  return (b->p+b->pos);
}
