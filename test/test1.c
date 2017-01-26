#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ws.h>

wsd_config_t *wsd_cfg;

static char *PROTO="chat1";
static char *URI="/chatterbox";
static char *VER="13";
static char *KEY="dGhlIHNhbXBsZSBub25jZQ==";
static const char *EXPECTED_ACCEPT_VAL="s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

int
main(int argc, char **argv)
{
     /* setup configuration */
     wsd_cfg = malloc(sizeof(wsd_config_t));
     AZ(wsd_cfg);
     memset(wsd_cfg, 0, sizeof(wsd_config_t));

     /* setup endpoint */
     ep_t ep;
     memset(&ep, 0, sizeof(ep));
     ep_init(&ep);

     /* setup http request */
     http_req_t hr;
     memset(&hr, 0, sizeof(hr));
     hr.sec_ws_ver.start = VER;
     hr.sec_ws_ver.len = strlen(VER);
     hr.req_target.start = URI;
     hr.req_target.len = strlen(URI);
     hr.sec_ws_key.start = KEY;
     hr.sec_ws_key.len = strlen(KEY);
     hr.sec_ws_proto.start = PROTO;
     hr.sec_ws_proto.len = strlen(PROTO);

     /* test this function */
     int rv = ws_handshake(&ep, &hr);
     AZ(rv);
     A(0 < buf_write_sz(ep.snd_buf));
     
//     assert(NULL!=strstr(buf_ref(wsc.buf_out), EXPECTED_ACCEPT_VAL));
//     assert(wsc.pfd->events&POLLOUT);
//     assert(0xdeadbeef==wsc.on_data_frame(NULL, NULL, NULL, NULL));

     ep_destroy(&ep);
     free(wsd_cfg);

     return 0;
}
