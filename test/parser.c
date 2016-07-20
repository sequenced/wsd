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
#include <string.h>
#include "parser.h"

#define NUM_SAMPLES 3

char *samples[] = {
     "safari-9.1.1-varnish-4.1.3-sample",
     "firefox-47-varnish-4.1.3-sample",
     "chrome-51-varnish-4.1.3-sample"
};

static void
parse_samples() {
     int fd;
     for (int i = 0; i < NUM_SAMPLES; i++) {
          assert(0 < (fd = open(samples[i], O_RDONLY)));

          char buf[8192];
          bzero(buf, 8192);
          assert(0 < read(fd, buf, 8191));

          string_t *t = malloc(sizeof(string_t));

          assert (0 < http_header_tok(buf, t));

          http_req_t *req = malloc(sizeof(http_req_t));
          bzero(req, sizeof(http_req_t));
          assert(0 < parse_request_line(t, req));
          assert(0 < req->method.len);
          assert(0 < req->req_target.len);
          assert(8 == req->http_ver.len);
          
          int rv;
          while (0 < (rv = http_header_tok(NULL, t))) {
               assert(0 < t->len);
               assert(0 <= parse_header_field(t, req));
          }

          assert(0 == rv);
          assert(0 < req->host.len);
          assert(0 < req->origin.len);
          assert(0 < req->conn.len);
          assert(0 < req->upgrade.len);
          assert(0 < req->sec_ws_ver.len);
          assert(0 < req->sec_ws_proto.len);
          assert(0 < req->sec_ws_key.len);
          assert(0 < req->user_agent.len);

          free(req);
          free(t);
          close(fd);
     }
}

static void
field_value_tokenisation()
{
     static char *input1 = "one,two, threee,,four, ,five,";
     static char *input2 = "nodelim";

     string_t s;
     s.start = input1;
     s.len = strlen(input1);

     string_t result;

     assert(0 < http_field_value_tok(&s, &result));
     assert(3 == result.len);
     assert(0 == strncmp(result.start, "one", 3));
     assert(0 < http_field_value_tok(NULL, &result));
     assert(3 == result.len);
     assert(0 == strncmp(result.start, "two", 3));
     assert(0 < http_field_value_tok(NULL, &result));
     assert(7 == result.len);
     assert(0 == strncmp(result.start, " threee", 7));
     assert(0 == http_field_value_tok(NULL, &result));
     assert(0 == result.len);
     assert(0 < http_field_value_tok(NULL, &result));
     assert(4 == result.len);
     assert(0 == strncmp(result.start, "four", 4));
     assert(0 < http_field_value_tok(NULL, &result));
     assert(1 == result.len);
     assert(0 == strncmp(result.start, " ", 1));
     assert(0 < http_field_value_tok(NULL, &result));
     assert(4 == result.len);
     assert(0 == strncmp(result.start, "five", 4));
     assert(0 == http_field_value_tok(NULL, &result));
     assert(0 == result.len);
     assert(0 > http_field_value_tok(NULL, &result));

     s.start = input2;
     s.len = strlen(input2);

     assert(0 < http_field_value_tok(&s, &result));
     assert(strlen(input2) == result.len);
     assert(0 == strncmp(result.start, input2, result.len));
     assert(0 > http_field_value_tok(NULL, &result));
}

int
main() {
     parse_samples();
     field_value_tokenisation();
     return 0;
}
