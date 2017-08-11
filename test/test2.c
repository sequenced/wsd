#include <string.h>
#include <assert.h>
#include "wschild.h"
#include "types.h"

static void compact_buffer();
static void compact_empty_buffer();

int
main(int argc, char **argv)
{
     compact_buffer();
     compact_empty_buffer();

     return 0;
}

void
compact_buffer()
{
     skb_t b;
     memset(&b, 0, sizeof(skb_t));

     strcpy(&b.data[b.wrpos], "foobarx@"); // trailing '@' is a canary value
     b.wrpos += 7;

     assert(skb_rdsz(b) == 7);
     assert(skb_wrsz(b) == (sizeof(b.data) - 7));
     assert(b.rdpos == 0);

     // simulate reading "fooba"
     b.rdpos += 5;

     assert(b.data[b.rdpos] == 'r');
     
     skb_compact(&b);
     
     assert(skb_rdsz(b) == 2);
     assert(skb_wrsz(b) == (sizeof(b.data) - 2));
     assert(b.rdpos == 0);
     assert(b.wrpos == 2);
     assert(b.data[0] == 'r');
     assert(b.data[1] == 'x');
     assert(b.data[2] == 'o'); // must not copy trailing '@'

     strcpy(&b.data[b.wrpos], "abc#"); // trailing '#' is a canary value
     b.wrpos += 3;

     assert(skb_rdsz(b) == 5);
     assert(skb_wrsz(b) == (sizeof(b.data) - 5));
     assert(b.rdpos == 0);

     // simulate reading "rx"
     b.rdpos += 2;

     assert(b.data[b.rdpos] == 'a');
     assert(b.data[b.rdpos + 1] == 'b');
     assert(b.data[b.rdpos + 2] == 'c');

     skb_compact(&b);

     assert(b.data[0] == 'a');
     assert(b.data[1] == 'b');
     assert(b.data[2] == 'c');
     assert(b.data[3] == 'b'); // must not copy trailing '#'
}

void
compact_empty_buffer()
{
     skb_t b;
     memset(&b, 0, sizeof(skb_t));

     assert(b.wrpos == 0);
     assert(b.rdpos == 0);
     char expected = b.data[0];

     skb_compact(&b);

     assert(b.wrpos == 0);
     assert(b.rdpos == 0);
     assert(b.data[0] == expected);

     b.wrpos = 16;
     b.rdpos = 16;

     skb_compact(&b);

     assert(b.wrpos == 0);
     assert(b.rdpos == 0);
     
}
