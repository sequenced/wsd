#include <string.h>
#include <assert.h>
#include <wstypes.h>

int
main(int argc, char **argv)
{
     buf2_t *b = malloc(sizeof(buf2_t));
     memset(b, 0, sizeof(buf2_t));

     strcpy(b->p + b->wrpos, "foobarx@"); // trailing '@' is a canary value
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

     strcpy(b->p + b->wrpos, "abc#"); // trailing '#' is a canary value
     b->wrpos += 3;

     assert(buf_rdsz(b) == 5);
     assert(buf_wrsz(b) == (sizeof(b->p) - 5));
     assert(b->rdpos == 0);

     // simulate reading "rx"
     b->rdpos += 2;

     assert(*(char*)(b->p + b->rdpos) == 'a');
     assert(*(char*)(b->p + b->rdpos + 1) == 'b');
     assert(*(char*)(b->p + b->rdpos + 2) == 'c');

     buf_compact(b);

     assert(*(char*)(b->p) == 'a');
     assert(*(char*)(b->p + 1) == 'b');
     assert(*(char*)(b->p + 2) == 'c');
     assert(*(char*)(b->p + 3) == 'b'); // must not copy trailing '#'

     return 0;
}
