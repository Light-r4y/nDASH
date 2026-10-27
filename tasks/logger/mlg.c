#include "mlg.h"
#include <stdbool.h>
#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static void put_u32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static void put_f32(uint8_t *p, float f)
{
  uint32_t v;
  memcpy(&v, &f, sizeof(v));
  put_u32(p, v);
}

size_t mlg_header_size(int nfields, const char *info)
{
  size_t n = MLG_HEADER_BYTES + (size_t)nfields * MLG_FIELD_BYTES;
  if (info != NULL && info[0] != '\0')
  {
    n += strlen(info) + 1;
  }
  return n;
}

size_t mlg_build_header(uint8_t *buf, size_t cap, const mlg_field_t *fields, int nfields, const char *info,
                        uint32_t unix_ts)
{
  if (nfields < 1 || nfields > 255 || buf == NULL)
  {
    return 0;
  }
  const size_t total = mlg_header_size(nfields, info);
  const size_t fields_end = MLG_HEADER_BYTES + (size_t)nfields * MLG_FIELD_BYTES;
  const bool has_info = info != NULL && info[0] != '\0';
  if (cap < total || total > 0xFFFF)
  {
    return 0;
  }
  memset(buf, 0, total);
  memcpy(buf, "MLVLG\0", 6);
  put_u16(buf + 6, 1);                                     // format version
  put_u32(buf + 8, unix_ts);                               // start time (0 - none)
  put_u16(buf + 12, has_info ? (uint16_t)fields_end : 0);  // where the info string starts
  put_u32(buf + 14, (uint32_t)total);                      // where the data starts
  put_u16(buf + 18, (uint16_t)(nfields * 4));              // the length of the values of one record
  put_u16(buf + 20, (uint16_t)nfields);                    // the number of fields

  for (int i = 0; i < nfields; i++)
  {
    uint8_t *p = buf + MLG_HEADER_BYTES + (size_t)i * MLG_FIELD_BYTES;
    p[0] = 7;                                                 // F32
    strncpy((char *)p + 1, fields[i].name, MLG_NAME_MAX);     // 34 bytes, the last one is zero (memset)
    strncpy((char *)p + 35, fields[i].units, MLG_UNITS_MAX);  // 10 bytes
    p[45] = 0;                                                // shown as a number
    put_f32(p + 46, 1.0f);                                    // scale
    put_f32(p + 50, 0.0f);                                    // transform
    p[54] = (uint8_t)fields[i].digits;
  }
  if (has_info)
  {
    strcpy((char *)buf + fields_end, info);
  }
  return total;
}

size_t mlg_build_record(uint8_t *buf, size_t cap, uint8_t counter, uint64_t time_us, const float *values, int nfields)
{
  const size_t size = mlg_record_size(nfields);
  if (cap < size || nfields < 1)
  {
    return 0;
  }
  buf[0] = 0;  // type: field data
  buf[1] = counter;
  put_u16(buf + 2, (uint16_t)((time_us / 10) & 0xFFFF));  // 10 us per unit, wraps
  uint8_t sum = 0;
  for (int i = 0; i < nfields; i++)
  {
    uint8_t *p = buf + 4 + (size_t)i * 4;
    put_f32(p, values[i]);
    sum = (uint8_t)(sum + p[0] + p[1] + p[2] + p[3]);
  }
  buf[4 + (size_t)nfields * 4] = sum;
  return size;
}
