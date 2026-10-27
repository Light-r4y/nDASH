#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "layout.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pressure units of the display. The signals keep kPa (the log and the CAN table too); only what is
// shown is converted, per signal ("pressure" in the signal table) and by the settings (display.baro,
// display.boost):
//   "abs"    absolute pressure: kPa or inHg
//   "boost"  the signal is the absolute MAP: it is shown against the atmosphere (LND_ATMOSPHERE_KPA),
//            in bar, or in psi with the vacuum (below the atmosphere) in inHg and negative, as on a
//            boost / vacuum gauge
// The decimals and the unit come from here; the widget's "div" and "unit" do not apply to such a signal.

#define LND_ATMOSPHERE_KPA 101.325f

typedef struct {
  float value;          // what to show
  const char *unit;     // static string
  uint8_t decimals;
} lnd_shown_t;

// Converts a signal value. Returns false when nothing applies (pressure NONE): out is not touched.
bool lnd_units_convert(lnd_pressure_t kind, const lnd_display_settings_t *u, float kpa, lnd_shown_t *out);

// The settings have changed in the way that needs the screen built again.
bool lnd_units_differ(const lnd_display_settings_t *a, const lnd_display_settings_t *b);

#ifdef __cplusplus
} /*extern "C"*/
#endif
