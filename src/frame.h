#ifndef __FRAME_H__
#define __FRAME_H__

#include "types.h"

int frame_recv(sk_t *sk);
int frame_decode(sk_t *sk);

#endif /* #ifndef __FRAME_H__ */
