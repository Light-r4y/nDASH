#pragma once
#include <stdbool.h>
#include <stdint.h>

// Signal value store: the decoupling between a source (can_task or the "demo"
// generator) and the widgets. A signal is identified by a name ("rpm", "speed").
// A widget gets an id when the screen is built and reads the value every tick;
// it knows nothing about where the value came from.
//
// A value lives while it is being refreshed: a signal has a timeout (ms), and if
// more than that has passed since the last write, lnd_signal_get() says "no data"
// and widgets show "--". Timeout 0 - never expires.
//
// Writing and reading a float is atomic on the ESP32-S3, there are no locks on the
// values; registering names takes a spinlock and is rare (screen build, table change).

#define LND_SIGNAL_NAME_MAX 16
#define LND_SIGNAL_MAX_COUNT 24

// Finds a signal by name or creates it. min/max is the expected range (only used by
// the generator; updated on repeated registration). Returns the id, or -1 if the
// table is full or the name is empty.
int lnd_signal_register(const char *name, float min, float max);

// Finds a signal by name or creates one with the range 0..100, leaving the range of
// an existing one alone. For writers that do not know the range (can_task).
int lnd_signal_id(const char *name);

// Signal timeout, ms (0 - never expires). 0 by default.
void lnd_signal_set_timeout(int id, uint32_t timeout_ms);

// Writes a value (from can_task); refreshes the timestamp.
void lnd_signal_set(int id, float value);

// Reads a value. false - no data: never written, or timed out.
bool lnd_signal_get(int id, float *out);

// Drops a value back to "no data" (one signal or all).
void lnd_signal_invalidate(int id);
void lnd_signal_invalidate_all(void);

// Bench: a value pinned by hand (from the web). While it is held (hold_ms after the last
// lnd_signal_hold call) the source (can_task) and the generator do not overwrite it and its
// timeout does not apply; lnd_signal_release() or the end of the time gives the signal back.
void lnd_signal_hold(int id, float value, uint32_t hold_ms);
void lnd_signal_release(int id);

// Finds a signal by name without creating it; -1 if there is none.
int lnd_signal_find(const char *name);

// A look at the whole table, for the web.
typedef struct {
    char name[LND_SIGNAL_NAME_MAX];
    float min, max, value;
    bool valid;       // has a value (written lately enough)
    bool held;        // pinned by hand
    uint32_t age_ms;  // since the last write
} lnd_signal_info_t;
int lnd_signal_count(void);
bool lnd_signal_info(int id, lnd_signal_info_t *out);

// Test generator ("demo"): while it is on, every registered signal is driven by a
// triangle wave over its range. Turning it off drops the values.
// A signal with a real source (accelerometer): the generator does not overwrite it.
void lnd_signal_sim_exempt(int id);
void lnd_signal_sim_enable(bool enable);

// Is the generator on: sources (can_task) write nothing while it is.
bool lnd_signal_sim_is_enabled(void);

// Generator step; call once per tick from the ui task. Off - does nothing.
void lnd_signal_sim_step(uint32_t now_ms);
