#include "ws_interface.h"

long
wsapp_calculate_frame_length(const unsigned long payload_len)
{
  if (payload_len<126)
    return WSAPP_PAYLOAD_7BITS+payload_len;
  else if (payload_len<=USHRT_MAX)
    return WSAPP_PAYLOAD_7PLUS16BITS+payload_len;
  else if (payload_len<=(ULONG_MAX>>1))
    return WSAPP_PAYLOAD_7PLUS64BITS+payload_len;

  return -1L;
}

int
wsapp_set_payload_len(buf_t *out, const unsigned long payload_len)
{
  char byte2=0;

  if (buf_len(out)<1)
    return -1;

  if (payload_len<126)
    {
      wsapp_set_payload_bits(byte2, payload_len);
      buf_put(out, byte2);
    }
  else if (payload_len<=USHRT_MAX)
    {
      wsapp_set_payload_bits(byte2, 126);
      buf_put(out, byte2);

      if (buf_len(out)<2)
        return -1;

      buf_put_short(out, htobe16(payload_len));
    }
  else if (payload_len<=(ULONG_MAX>>1))
    {
      wsapp_set_payload_bits(byte2, 127);
      buf_put(out, byte2);

      if (buf_len(out)<8)
        return -1;

      buf_put_long(out, htobe64(payload_len));
    }
  else
    return -1;

  return 0;
}

int
wsapp_lookup_conn_by_protocol(const char *proto, struct list_head result)
{
  /* continue here */
}
