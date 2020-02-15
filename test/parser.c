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

#define NUM_SAMPLES 7

unsigned int wsd_errno = WSD_CHECKERRNO;

char *samples[] = {
     "safari-9.1.1-varnish-4.1.3-sample",
     "safari-9.1.1-sample",
     "firefox-47-varnish-4.1.3-sample",
     "firefox-47-sample",
     "firefox-60-varnish-6.2.0-sample",
     "chrome-51-varnish-4.1.3-sample",
     "chrome-51-sample"
};

static void
GIVEN_sample_field_value_WHEN_parsing_THEN_recognised(chunk_t *f)
{
     chunk_t result;
     assert(0 < http_field_value_tok(f, &result));
}

static void
GIVEN_request_WHEN_parsing_THEN_recognised() {
     int fd;
     for (int i = 0; i < NUM_SAMPLES; i++) {
          assert(0 < (fd = open(samples[i], O_RDONLY)));

          char buf[8192];
          memset(buf, 0, sizeof(buf));
          assert(0 < read(fd, buf, 8191));

          chunk_t *t = malloc(sizeof(chunk_t));

          assert (0 < http_header_tok(buf, t));

          http_req_t *req = malloc(sizeof(http_req_t));
          memset(req, 0, sizeof(http_req_t));
          assert(0 == parse_request_line(t, req));
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
          GIVEN_sample_field_value_WHEN_parsing_THEN_recognised(&req->conn);
          assert(0 < req->upgrade.len);
          GIVEN_sample_field_value_WHEN_parsing_THEN_recognised(&req->upgrade);
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
GIVEN_field_value_WHEN_parsing_THEN_recognised()
{
     static char *input1 = "one,two, threee,,four, ,five,";
     static char *input2 = "nodelim";

     chunk_t s;
     s.p = input1;
     s.len = strlen(input1);

     chunk_t result;

     assert(0 < http_field_value_tok(&s, &result));
     assert(3 == result.len);
     assert(0 == strncmp(result.p, "one", 3));
     assert(0 < http_field_value_tok(NULL, &result));
     assert(3 == result.len);
     assert(0 == strncmp(result.p, "two", 3));
     assert(0 < http_field_value_tok(NULL, &result));
     assert(7 == result.len);
     assert(0 == strncmp(result.p, " threee", 7));
     assert(0 == http_field_value_tok(NULL, &result));
     assert(0 == result.len);
     assert(0 < http_field_value_tok(NULL, &result));
     assert(4 == result.len);
     assert(0 == strncmp(result.p, "four", 4));
     assert(0 < http_field_value_tok(NULL, &result));
     assert(1 == result.len);
     assert(0 == strncmp(result.p, " ", 1));
     assert(0 < http_field_value_tok(NULL, &result));
     assert(4 == result.len);
     assert(0 == strncmp(result.p, "five", 4));
     assert(0 > http_field_value_tok(NULL, &result));
     assert(wsd_errno == WSD_EINPUT);
     assert(0 == result.len);
     assert(0 == result.p);
     assert(0 > http_field_value_tok(NULL, &result));
     assert(wsd_errno == WSD_EINPUT);
     assert(0 == result.len);
     assert(0 == result.p);

     s.p = input2;
     s.len = strlen(input2);

     assert(0 < http_field_value_tok(&s, &result));
     assert(strlen(input2) == result.len);
     assert(0 == strncmp(result.p, input2, result.len));
     assert(0 > http_field_value_tok(NULL, &result));
     assert(wsd_errno == WSD_EINPUT);
     assert(0 == result.len);
     assert(0 == result.p);
}

int
main() {
     GIVEN_request_WHEN_parsing_THEN_recognised();
     GIVEN_field_value_WHEN_parsing_THEN_recognised();
     return 0;
}
