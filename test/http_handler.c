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

#define NUM_SAMPLES 5
#define BUF_SIZE 8192

wsd_config_t *wsd_cfg;

char *samples[] = {
     "safari-9.1.1-varnish-4.1.3-sample",
     "firefox-47-varnish-4.1.3-sample",
     "chrome-51-varnish-4.1.3-sample",
     "safari-9.1.1-sample",
     "firefox-47-sample",
     "chrome-51-sample"
};

int noop(struct wsconn *conn, http_req_t *req) { return 1; }

int
main() {
     int fd;
     for (int i = 0; i < NUM_SAMPLES; i++) {

          assert(0 < (fd = open(samples[i], O_RDONLY)));

          int len;
          buf_t *in = buf_alloc(BUF_SIZE);
          assert(0 < (len = read(fd, buf_ref(in), buf_len(in))));
          buf_fwd(in, len);
          
          wsd_cfg=malloc(sizeof(wsd_config_t));
          bzero(wsd_cfg, sizeof(wsd_config_t));
          
          wsconn_t wsc;
          bzero(&wsc, sizeof(wsconn_t));
          wsc.on_handshake = noop;
          wsc.buf_in = in;
          wsc.buf_out = buf_alloc(BUF_SIZE);
          wsc.pfd = malloc(sizeof(struct pollfd));
          bzero(wsc.pfd, sizeof(struct pollfd));

          assert(0 < http_on_read(&wsc));
          assert(0 == wsc.pfd->events);
          assert(BUF_SIZE == buf_len(wsc.buf_out));
          assert(0 == wsc.close_on_write);

          buf_free(wsc.buf_in);
          buf_free(wsc.buf_out);
          close(fd);
     }     

     return 0;
}
