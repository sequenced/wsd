#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <error.h>
#include <errno.h>
#include <assert.h>
#include <http.h>

#define NUM_SAMPLES 3

wsd_config_t *wsd_cfg;

char *samples[] = {
     "safari-9.1.1-varnish-4.1.3-sample",
     "firefox-47-varnish-4.1.3-sample",
     "chrome-51-varnish-4.1.3-sample"
};

int
main()
{
     int fd;
     for (int i = 0; i < NUM_SAMPLES; i++) {
          if (0 > (fd = open(samples[i], O_RDONLY)))
          {
               fprintf(stderr, "%s, line %d: ", __FILE__, __LINE__);
               perror("open");
               return 1;
          }

          buf_t *in = buf_alloc(8192);
          if (0 > read(fd, buf_ref(in), buf_len(in)))
          {
               fprintf(stderr, "%s, line %d: ", __FILE__, __LINE__);
               perror("read");
               return 1;
          }

          wsd_cfg=malloc(sizeof(wsd_config_t));
          bzero(wsd_cfg, sizeof(wsd_config_t));
            
          wsconn_t wsc;
          bzero(&wsc, sizeof(wsconn_t));
          wsc.buf_in = in;
          wsc.buf_out = buf_alloc(1024);
          wsc.pfd = malloc(sizeof(struct pollfd));
          bzero(wsc.pfd, sizeof(struct pollfd));

          int rv;
          if (0 > (rv = http_on_read(&wsc)))
          {
               fprintf(stderr, "%s, line %d: ", __FILE__, __LINE__);
               return 1;
          }

          assert(wsc.pfd->events & POLLOUT);

          buf_free(wsc.buf_in);
          buf_free(wsc.buf_out);
          close(fd);
     }     

     return 0;
}
