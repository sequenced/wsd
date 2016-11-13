#ifndef __JEN_H__
#define __JEN_H__

int jen_recv_data_frame(ep_t *ep, wsframe_t *wsf);
int jen_open();
int jen_close();

#endif /* #ifndef __JEN_H__ */
