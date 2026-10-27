#include "logger_logic.h"

static bool cond_true(const lnd_trig_cfg_t *cfg, bool valid, float v)
{
  if (!valid)
  {
    return false;
  }
  return cfg->less ? (v < cfg->value) : (v > cfg->value);
}

void lnd_trig_forced_stop(lnd_trig_t *t)
{
  t->rec = false;
  t->manual = false;
  t->false_pending = false;
  t->latched = true;
}

lnd_trig_act_t lnd_trig_step(lnd_trig_t *t, const lnd_trig_cfg_t *cfg, bool card_ok, bool sig_valid, float sig,
                             bool req_start, bool req_stop, uint32_t now_ms)
{
  const bool cond = cfg->mode == LND_TRIG_CONDITION && cond_true(cfg, sig_valid, sig);

  if (!card_ok)
  {
    if (t->rec)
    {
      lnd_trig_forced_stop(t);
      return LND_TRIG_STOP;
    }
    return LND_TRIG_NONE;
  }

  if (req_stop)
  {
    if (t->rec)
    {
      t->rec = false;
      t->manual = false;
      t->false_pending = false;
      t->latched = cond;  // a condition that has fired does not start the recording again
      return LND_TRIG_STOP;
    }
    return LND_TRIG_NONE;
  }

  if (req_start && !t->rec)
  {
    t->rec = true;
    t->manual = true;
    t->false_pending = false;
    return LND_TRIG_START;
  }

  if (t->rec)
  {
    if (t->manual || cfg->mode != LND_TRIG_CONDITION)
    {
      return LND_TRIG_NONE;
    }
    if (cond)
    {
      t->false_pending = false;
      return LND_TRIG_NONE;
    }
    if (!t->false_pending)
    {
      t->false_pending = true;
      t->false_since_ms = now_ms;
    }
    if ((uint32_t)(now_ms - t->false_since_ms) >= (uint32_t)cfg->stop_delay * 1000U)
    {
      t->rec = false;
      t->false_pending = false;
      return LND_TRIG_STOP;
    }
    return LND_TRIG_NONE;
  }

  if (cfg->mode == LND_TRIG_BOOT && !t->boot_done)
  {
    t->boot_done = true;
    t->rec = true;
    t->manual = false;
    return LND_TRIG_START;
  }
  if (cfg->mode == LND_TRIG_CONDITION)
  {
    if (!cond)
    {
      t->latched = false;
    }
    else if (!t->latched)
    {
      t->rec = true;
      t->manual = false;
      t->false_pending = false;
      return LND_TRIG_START;
    }
  }
  return LND_TRIG_NONE;
}
