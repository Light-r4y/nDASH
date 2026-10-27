#pragma once
#include <stdbool.h>
#include <stdint.h>

// The decision "record or not".
//
// Modes (the numbers are those of lnd_log_mode_t in settings.h):
//   0 manual    - only by the "Start" / "Stop" buttons of the web page "Logs";
//   1 boot      - once after power-on, as soon as the card is ready; a recording stopped by hand is
//                 not resumed until the next power-on;
//   2 condition - records while the signal is above (or below) the threshold, and stop_delay seconds
//                 after the condition stopped holding. No data from the signal = the condition does not
//                 hold. A recording stopped by hand is resumed only by a new entry of the condition.
// A recording started by hand is not stopped by the condition.

#define LND_TRIG_MANUAL 0
#define LND_TRIG_BOOT 1
#define LND_TRIG_CONDITION 2

typedef struct {
  int mode;
  bool less;            // false: signal > value, true: signal < value
  float value;
  uint16_t stop_delay;  // s
} lnd_trig_cfg_t;

typedef struct {
  bool rec;            // recording
  bool manual;         // started by hand: the condition does not stop it
  bool boot_done;      // the "boot" mode has already worked
  bool latched;        // stopped, until the condition goes away
  bool false_pending;  // the condition is gone, waiting for stop_delay
  uint32_t false_since_ms;
} lnd_trig_t;

typedef enum {
  LND_TRIG_NONE = 0,
  LND_TRIG_START,
  LND_TRIG_STOP,
} lnd_trig_act_t;

// One step. card_ok - the card is ready; sig_valid/sig - the value of the condition's signal
// (sig_valid=false: no data). req_start/req_stop - the buttons of the web page. Returns the action;
// the rec state changes inside.
lnd_trig_act_t lnd_trig_step(lnd_trig_t *t, const lnd_trig_cfg_t *cfg, bool card_ok, bool sig_valid, float sig,
                             bool req_start, bool req_stop, uint32_t now_ms);

// The recording was stopped from outside (a card error, no space, a new layout): it does not
// resume by itself until the condition goes away.
void lnd_trig_forced_stop(lnd_trig_t *t);
