#include <string.h>
#include <assert.h>
#include <wstypes.h>
#include <hashtable.h>

DEFINE_HASHTABLE(test_hash, 6);

struct some_struct {
     long unsigned int hash;
     short small_value;
     long unsigned int large_value;
     struct hlist_node hash_node;
};

int
main(int argc, char **argv)
{
     hash_init(test_hash);

     for (int i = 0; i < 8; i++) {
          struct some_struct *s = malloc(sizeof(struct some_struct));
          AN(s);
          memset(s, 0, sizeof(struct some_struct));
          s->small_value = 1 ^ i;
          s->large_value = i << 8;
          s->hash = i;
          hash_add(test_hash, &s->hash_node, s->hash);
     }

     long unsigned int hash = 7;
     struct some_struct *p = 0;
     hash_for_each_possible(test_hash, p, hash_node, hash) {
          if (p->hash == hash)
               break;
     }
     
     AN(p);
     A(p->small_value == 1 ^ 7);
     A(p->large_value == 7 << 8);

     return 0;
}
