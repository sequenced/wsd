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
buf_rwnd(buf_t *b, int len)
{
  b->pos-=len;
}

inline void
buf_fwd(buf_t *b, int len)
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

inline void
buf_put(buf_t *b, char c)
{
  *(b->p+b->pos)=c;
  b->pos++;
}


inline void
trim(string_t *str)
{
  int i=0;
  while (str->len)
    {
      char c=*(str->start+i);
      if (' '==c || '\t'==c)
        {
          str->start++;
          str->len--;
          i++;
        }
      else
        break;
    }

  while (str->len)
    {
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
  static int pos=0;
  static string_t rv;
  static string_t *s;
  if (NULL!=str)
    {
      s=str;
      pos=0;
    }

  if (pos==s->len)
    {
      rv.len=(-1); /* end of input string reached */
      return &rv;
    }

  rv.start=(s->start+pos);
  rv.len=0;
  while (pos<s->len)
    {
      if (del==*(s->start+pos))
        {
          pos++; /* move past delimiter for next call */
          break;
        }

      rv.len++;
      pos++;
    }

  return &rv;
}

inline unsigned short
buf_get_short(buf_t *b)
{
  unsigned short s=*((unsigned short*)(b->p+b->pos));
  b->pos+=2;
  return s;

}

inline void
buf_put_short(buf_t *b, unsigned short val)
{
  *(unsigned short*)(b->p+b->pos)=val;
  b->pos+=2;
}

inline long
buf_get_long(buf_t *b)
{
  long l=*((long*)(b->p+b->pos));
  b->pos+=8;
  return l;
}

inline int
buf_get_int(buf_t *b)
{
  int k=*((int*)(b->p+b->pos));
  b->pos+=4;
  return k;
}

inline void
buf_set_pos(buf_t *b, int pos)
{
  b->pos=pos;
}

inline void
buf_compact(buf_t *b)
{
  int i=0;
  while (b->pos<b->limit)
    *(b->p+i++)=*(b->p+b->pos++);

  b->pos=i;
  b->limit=b->capacity;
}

inline void
buf_slice(buf_t *a, buf_t *b, int len)
{
  /* a is going to be a subsequence of b */
  a->p=b->p;
  a->pos=b->pos;
  a->limit=b->pos+len;
  a->capacity=b->capacity;
  a->swap='\0';
}

inline void
buf_put_string(buf_t *b, char *s)
{
  while ('\0'!=*s++)
    buf_put(b, *s);
}

void
*hash32_table_lookup(hash32_table_t *t, int key)
{
  unsigned int k=t->hash(key);

  int i;
  for (i=0; i<HASH_ENTRY_BUCKETS; i++)
    if (key==t->entries[k].buckets[i].key)
      break;

  return t->entries[k].buckets[i].data;
}

int
hash32_table_insert(hash32_table_t *t, int key, void *data)
{
  int rv=0;
  unsigned int k=t->hash(key);

  int i;
  for (i=0; i<HASH_ENTRY_BUCKETS; i++)
    if (0==t->entries[k].buckets[i].key
        || key==t->entries[k].buckets[i].key)
      break;

  if (i!=HASH_ENTRY_BUCKETS-1)
    {
      t->entries[k].buckets[i].data=data;
      t->entries[k].buckets[i].key=key;
    }
  else
    /* HASH_ENTRY_BUCKETS-1 serves as end marker */
    rv=-1;

  return rv;
}

unsigned int
hash32_table_hash(int val)
{
  /* Use same function as Apache httpd 2.4.7 */
  unsigned int v=val;
  v^=(v>>16);
  return ((v>>8)^v)&HASH32_TABLE_SIZE;
}
