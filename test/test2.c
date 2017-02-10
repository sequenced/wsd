#include <string.h>
#include <assert.h>
#include <wstypes.h>

int
main(int argc, char **argv)
{
     buf2_t *b = malloc(sizeof(buf2_t));
     memset(b, 0, sizeof(buf2_t));

     strcpy(b->p, "foobarx@"); // trailing '@' is a canary value
     b->wrpos += 7;

     assert(buf_rdsz(b) == 7);
     assert(buf_wrsz(b) == (sizeof(b->p) - 7));
     assert(b->rdpos == 0);

     // simulate reading "fooba"
     b->rdpos += 5;

     assert(*(char*)(b->p + b->rdpos) == 'r');
     
     buf_compact(b);
     
     assert(buf_rdsz(b) == 2);
     assert(buf_wrsz(b) == (sizeof(b->p) - 2));
     assert(b->rdpos == 0);
     assert(b->wrpos == 2);
     assert(*(char*)b->p == 'r');
     assert(*(char*)(b->p + 1) == 'x');
     assert(*(char*)(b->p + 2) == 'o'); // must not copy trailing '@'
     
     return 0;
}
