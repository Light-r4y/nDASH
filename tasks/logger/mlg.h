#pragma once
#include <stddef.h>
#include <stdint.h>

// TunerStudio / MegaLogViewer binary log (MLVLG, format version 1, EFI Analytics: MLG_Binary_LogFormat_1.0.pdf).
// All numbers are big-endian.
//
// The file: a header (22 bytes) + the field descriptions (55 bytes each) + an optional info string,
// then the records one after another. A record: type 0, a counter (1 byte, wraps), the time (2 bytes,
// 10 us steps, wraps every 655.36 ms), the field values (float32 each), a checksum (the sum of the
// value bytes, 1 byte).

#define MLG_NAME_MAX 33   // a field name: 34 bytes with the terminating zero
#define MLG_UNITS_MAX 9   // units: 10 bytes with the terminating zero
#define MLG_HEADER_BYTES 22
#define MLG_FIELD_BYTES 55
#define MLG_RECORD_OVERHEAD 5  // type, counter, time (2), sum

typedef struct {
  char name[MLG_NAME_MAX + 1];
  char units[MLG_UNITS_MAX + 1];
  int8_t digits;  // decimals shown
} mlg_field_t;

// The size of the header with the field descriptions and the info string (info may be NULL).
size_t mlg_header_size(int nfields, const char *info);

// Writes the header to buf (cap >= mlg_header_size). unix_ts - the start time (0 - unknown).
// Returns the size of the header, or 0 if buf is too small or there are no / too many fields.
size_t mlg_build_header(uint8_t *buf, size_t cap, const mlg_field_t *fields, int nfields, const char *info,
                        uint32_t unix_ts);

// The size of one record.
static inline size_t mlg_record_size(int nfields)
{
  return (size_t)nfields * 4 + MLG_RECORD_OVERHEAD;
}

// Writes a record to buf (cap >= mlg_record_size). counter - the number of the record (its low byte
// is used), time_us - the time from the start of the recording in microseconds. Returns the size.
size_t mlg_build_record(uint8_t *buf, size_t cap, uint8_t counter, uint64_t time_us, const float *values, int nfields);
