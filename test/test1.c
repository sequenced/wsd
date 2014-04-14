#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <assert.h>
#include "ws.h"

#define BUF_SIZE 2048

wsd_config_t *wsd_cfg;

static const char *VER="13";
static const char *KEY="dGhlIHNhbXBsZSBub25jZQ==";
static const char *EXPECTED_ACCEPT_VAL="s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

int
main(int argc, char **argv)
{
  wschild_conn_t wsc;
  memset((void*)&wsc, 0x0, sizeof(wsc));
  wsc.pfd=malloc(sizeof(struct pollfd));
  memset((void*)wsc.pfd, 0x0, sizeof(struct pollfd));
  wsc.buf_in=buf_alloc(BUF_SIZE);
  wsc.buf_out=buf_alloc(BUF_SIZE);

  http_req_t hr;
  memset((void*)&hr, 0x0, sizeof(hr));
  hr.sec_ws_ver.start=VER;
  hr.sec_ws_ver.len=strlen(VER);
  hr.sec_ws_key.start=KEY;
  hr.sec_ws_key.len=strlen(KEY);

  ws_on_handshake(&wsc, &hr);

  buf_flip(wsc.buf_out);
  printf("%s", buf_ref(wsc.buf_out));

  return 0;
}
