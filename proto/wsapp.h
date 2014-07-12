#ifndef __WSAPP_H__
#define __WSAPP_H__

#include <limits.h>
#include "wstypes.h"

#define wsapp_set_fin_bit(byte) (byte|=0x80)
#define wsapp_set_opcode(byte, val) (byte|=(0xf&val))
#define wsapp_set_payload_bits(byte, val) (byte|=(0x7f&val))
#define WSAPP_WS_TEXT_FRAME 0x1

static int
wsapp_set_payload_len(buf_t *out, const long long len)
{
  char byte2=0;

  if (buf_len(out)<1)
    return -1;

  if (len<126)
    {
      wsapp_set_payload_bits(byte2, len);
      buf_put(out, byte2);
    }
  else if (len<=USHRT_MAX)
    {
      wsapp_set_payload_bits(byte2, 126);
      buf_put(out, byte2);

      if (buf_len(out)<2)
        return -1;

      buf_put_short(out, htobe16(len));
    }
  else if (len<=(ULONG_MAX>>1))
    {
      wsapp_set_payload_bits(byte2, 127);
      buf_put(out, byte2);

      if (buf_len(out)<8)
        return -1;

      buf_put_long(out, htobe64(len));
    }

  return 0;
}

#endif /* #ifndef __WSAPP_H__ */
