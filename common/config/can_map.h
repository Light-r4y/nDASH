#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// CAN table: which part of which frame is which signal. Works as a DBC. Stored in
// NVS as JSON (LND_CAN_MAP_NVS_KEY), in memory as a parsed array.
//
// {
//   "version": 1,
//   "entries": [
//     { "signal": "rpm", "id": "0x201", "ext": false, "start": 0, "len": 16,
//       "order": "intel", "signed": false, "factor": 1, "offset": 0, "timeout": 2000 }
//   ]
// }
//
// value = raw * factor + offset. Bits are numbered exactly as in a DBC: for "intel"
// (@1) "start" is the least significant bit, for "motorola" (@0) the most significant
// one in DBC numbering (7..0, 15..8, ...). A field of 1..32 bits must fit in 8 bytes.
// "timeout" is the ms without frames (100..60000, 2000 by default) after which the
// signal counts as lost: numbers show "--", the needle goes to the start of the scale.
// "id" is a number or a string ("0x201"); "ext" tells an 11-bit ID from a 29-bit one.
// Several entries may use the same frame; the same signal may appear only once.

#define LND_CAN_MAP_MAX 24
#define LND_CAN_SIGNAL_NAME_LEN 16

typedef struct {
  char signal[LND_CAN_SIGNAL_NAME_LEN];
  uint32_t id;
  bool ext;
  uint8_t start, len;
  bool motorola;
  bool is_signed;
  float factor, offset;
  uint32_t timeout_ms;
} lnd_can_map_entry_t;

typedef struct {
  uint8_t count;
  lnd_can_map_entry_t e[LND_CAN_MAP_MAX];
} lnd_can_map_t;

// Reads the table from NVS; takes the built-in rusEFI preset if there is no saved
// one (an explicitly saved empty table stays empty). Call after nvs_flash_init().
void lnd_can_map_init(void);

// A copy of the current table.
void lnd_can_map_copy(lnd_can_map_t *out);

// Parses the JSON, checks it, stores it in NVS and makes it current. ESP_ERR_INVALID_ARG -
// it did not parse or an entry is wrong (the old table stays).
esp_err_t lnd_can_map_apply_json(const char *json);

// The JSON of the current table on the heap (free() it); NULL on out of memory.
char *lnd_can_map_json_dup(void);

// Brings the rusEFI preset back and erases the saved table.
esp_err_t lnd_can_map_reset(void);

// Grows on every change; can_task rebuilds its tables when it sees a new value.
uint32_t lnd_can_map_generation(void);

// Takes the field of one entry out of an 8-byte frame: raw bits, sign, factor and
// offset applied.
float lnd_can_map_extract(const lnd_can_map_entry_t *e, const uint8_t data[8]);
