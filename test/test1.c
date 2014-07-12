#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <assert.h>
#include "ws.h"

#define BUF_SIZE 2048

wsd_config_t *wsd_cfg;

static const char *PROTO="chat1";
static char *URI="/chatterbox";
static const char *VER="13";
static const char *KEY="dGhlIHNhbXBsZSBub25jZQ==";
static const char *EXPECTED_ACCEPT_VAL="s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
static const int EXPECTED_RET_VAL=1;

static int
on_frame(ws_conn_t *ignored, wsframe_t *this_too, buf_t *b)
{
  return 0xdeadbeef;
}

static int
on_open(ws_conn_t *ignored)
{
  return 0;
}

int
main(int argc, char **argv)
{
  wsd_cfg=malloc(sizeof(wsd_config_t));
  memset((void*)wsd_cfg, 0x0, sizeof(wsd_config_t));
  init_list_head(&wsd_cfg->location_list);

  /* configure a single location */
  location_config_t *loc;
  if (!(loc=malloc(sizeof(location_config_t))))
    return -1;
  loc->url=URI;
  loc->protocol="chat1";
  loc->on_data_frame=on_frame;
  loc->on_open=on_open;
  list_add_tail(&loc->list_head, &wsd_cfg->location_list);

  /* setup record of a connection */
  ws_conn_t wsc;
  memset((void*)&wsc, 0x0, sizeof(wsc));
  wsc.pfd=malloc(sizeof(struct pollfd));
  memset((void*)wsc.pfd, 0x0, sizeof(struct pollfd));
  wsc.buf_in=buf_alloc(BUF_SIZE);
  wsc.buf_out=buf_alloc(BUF_SIZE);

  /* make up an http request */
  http_req_t hr;
  memset((void*)&hr, 0x0, sizeof(hr));
  hr.sec_ws_ver.start=VER;
  hr.sec_ws_ver.len=strlen(VER);
  hr.req_uri.start=URI;
  hr.req_uri.len=strlen(URI);
  hr.sec_ws_key.start=KEY;
  hr.sec_ws_key.len=strlen(KEY);
  hr.sec_ws_proto.start=PROTO;
  hr.sec_ws_proto.len=strlen(PROTO);

  /* test this function */
  int rv=ws_on_handshake(&wsc, &hr);

  assert(rv==EXPECTED_RET_VAL);
  buf_flip(wsc.buf_out);
  assert(NULL!=strstr(buf_ref(wsc.buf_out), EXPECTED_ACCEPT_VAL));
  assert(wsc.pfd->events&POLLOUT);
  assert(0xdeadbeef==wsc.on_data_frame(NULL, NULL, NULL));

  free(wsc.pfd);
  free(loc);
  free(wsd_cfg);

  return 0;
}
