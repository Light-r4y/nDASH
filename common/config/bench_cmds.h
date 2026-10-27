#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Bench commands: the buttons of the web page "Bench" that make the rusEFI ECU run its outputs
// (coils, injectors, relays, fans...) or start and stop the engine. Stored in NVS as JSON
// (LND_BENCH_CMDS_NVS_KEY), in memory as an array; a built-in preset (taken from the rusEFI sources,
// see bench_cmds.c) is used until a table is saved.
//
// { "version": 1, "cmds": [
//     { "name": "ign1", "label": "Coil 1", "group": "Ignition", "id": 7798796, "ext": true,
//       "sub": 18, "idx": 1, "dlc": 8, "stopped_only": true } ] }
//
// The frame is 0x66, 0x00, sub (16 bit, little endian), idx (16 bit, little endian), padded with
// zeros up to dlc (6 or 8). "stopped_only": the dash refuses to send it while the engine runs.

#define LND_BENCH_CMDS_MAX 48
#define LND_BENCH_NAME_LEN 16
#define LND_BENCH_LABEL_LEN 24
#define LND_BENCH_GROUP_LEN 16
#define LND_BENCH_FRAME_HEADER 0x66
#define LND_BENCH_DEFAULT_ID 0x77000C  // ECU_CAN_BUS_USER_CONTROL

typedef struct {
  char name[LND_BENCH_NAME_LEN];
  char label[LND_BENCH_LABEL_LEN];
  char group[LND_BENCH_GROUP_LEN];
  uint32_t id;
  bool ext;
  uint16_t sub, idx;
  uint8_t dlc;
  bool stopped_only;
} lnd_bench_cmd_t;

typedef struct {
  uint8_t count;
  lnd_bench_cmd_t c[LND_BENCH_CMDS_MAX];
} lnd_bench_cmds_t;

// Reads the table from NVS, or takes the preset. Call after nvs_flash_init().
void lnd_bench_cmds_init(void);

// A copy of the command called name; false if there is none.
bool lnd_bench_cmd_find(const char *name, lnd_bench_cmd_t *out);

// The CAN frame of a command (8 bytes buffer; returns the length to send).
uint8_t lnd_bench_cmd_frame(const lnd_bench_cmd_t *c, uint8_t data[8]);

// Parses the JSON, checks it, stores it in NVS and makes it current. ESP_ERR_INVALID_ARG -
// it did not parse or an entry is wrong (the old table stays).
esp_err_t lnd_bench_cmds_apply_json(const char *json);

// The JSON of the current table on the heap (free() it); NULL on out of memory.
char *lnd_bench_cmds_json_dup(void);

// Brings the preset back and erases the saved table.
esp_err_t lnd_bench_cmds_reset(void);
