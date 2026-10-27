#include "units.h"

#define KPA_TO_PSI 0.1450377f
#define KPA_TO_INHG 0.2952999f

bool lnd_units_convert(lnd_pressure_t kind, const lnd_display_settings_t *u, float kpa, lnd_shown_t *out)
{
  switch (kind)
  {
    case LND_PRESSURE_ABS:
      if (u->baro == LND_BARO_INHG)
      {
        *out = (lnd_shown_t){kpa * KPA_TO_INHG, "inHg", 2};
      }
      else
      {
        *out = (lnd_shown_t){kpa, "kPa", 0};
      }
      return true;
    case LND_PRESSURE_BOOST:
    {
      const float gauge = kpa - LND_ATMOSPHERE_KPA;
      if (u->boost == LND_BOOST_PSI)
      {
        *out = gauge >= 0.0f ? (lnd_shown_t){gauge * KPA_TO_PSI, "psi", 1} : (lnd_shown_t){gauge * KPA_TO_INHG, "inHg", 1};
      }
      else
      {
        *out = (lnd_shown_t){gauge / 100.0f, "bar", 2};
      }
      return true;
    }
    default:
      return false;
  }
}

bool lnd_units_differ(const lnd_display_settings_t *a, const lnd_display_settings_t *b)
{
  return a->baro != b->baro || a->boost != b->boost;
}
