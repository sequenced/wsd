#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <assert.h>
#include "parser.h"

char *samples[] = { "safari-9.1.1-varnish-4.1.3-sample" };

int
main()
{
     int fd;
     for (int i = 0; i < 1; i++) {
          if (0 > (fd = open(samples[i], O_RDONLY)))
          {
               fprintf(stderr, "%s, line %d: ", __FILE__, __LINE__);
               perror("open");
               return 1;
          }

          char buf[8192];
          bzero(buf, 8192);
          if (0 > read(fd, buf, 8191))
          {
               fprintf(stderr, "%s, line %d: ", __FILE__, __LINE__);
               perror("read");
               return 1;
          }

          string_t *t = malloc(sizeof(string_t));

          if (0 >= http_header_tok(buf, t))
          {
               fprintf(stderr, "%s, line %d: errno=%d",
                       __FILE__,
                       __LINE__,
                       errno);
               return 1;
          }

          int rv;
          while (0 < (rv = http_header_tok(NULL, t)))
          {
               assert(0 < t->len);
          }
          
          if (0 > rv)
          {
               fprintf(stderr, "%s, line %d: errno=%d\n",
                       __FILE__,
                       __LINE__,
                       errno);
               return 1;
          }

          assert(0 == rv && t->len == 0);
          
          free(t);
          close(fd);
     }
     
     return 0;
}
