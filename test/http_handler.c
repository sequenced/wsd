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
#include <wstypes.h>

#define NUM_SAMPLES 5

wsd_config_t *wsd_cfg;

char *samples[] = {
     "safari-9.1.1-varnish-4.1.3-sample",
     "firefox-47-varnish-4.1.3-sample",
     "chrome-51-varnish-4.1.3-sample",
     "safari-9.1.1-sample",
     "firefox-47-sample",
     "chrome-51-sample"
};

int noop(ep_t *conn, http_req_t *req) { return 0; }

int
main() {
     int fd;
     for (int i = 0; i < NUM_SAMPLES; i++) {

          assert(0 < (fd = open(samples[i], O_RDONLY)));

          ep_t *ep = malloc(sizeof(ep_t));
          ep_init(ep);
          ep->proto.handshake = noop;

          int len;
          assert(0 < (len = read(fd, ep->rcv_buf, buf_write_sz(ep->rcv_buf))));
          ep->rcv_buf->wrpos += len;
          
          wsd_cfg=malloc(sizeof(wsd_config_t));
          bzero(wsd_cfg, sizeof(wsd_config_t));


          assert(0 == http_recv(ep));

          ep_destroy(ep);
          
          close(fd);
     }     

     return 0;
}
