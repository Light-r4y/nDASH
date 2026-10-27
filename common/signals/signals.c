#include "signals.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

typedef struct {
    char name[LND_SIGNAL_NAME_MAX];
    volatile float value;
    volatile bool valid;
    volatile uint32_t stamp_ms; // when written
    uint32_t timeout_ms;
    float min, max;
    bool sim_exempt; // real source (sensor): the demo generator leaves it alone
    volatile bool hold;           // pinned by hand (bench)
    volatile uint32_t hold_until; // ms; the pin ends there
} signal_t;

static signal_t s_sig[LND_SIGNAL_MAX_COUNT];
static int s_count;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_sim;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline bool is_held(const signal_t *s)
{
    return s->hold && (int32_t)(now_ms() - s->hold_until) < 0;
}

// update_range: whether to overwrite the range of an existing signal.
static int find_or_add(const char *name, float min, float max, bool update_range)
{
    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    int id = -1;
    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_sig[i].name, name, LND_SIGNAL_NAME_MAX - 1) == 0) {
            id = i;
            if (update_range) {
                s_sig[i].min = min; // range from the fresh layout
                s_sig[i].max = max;
            }
            break;
        }
    }
    if (id < 0 && s_count < LND_SIGNAL_MAX_COUNT) {
        id = s_count++;
        strlcpy(s_sig[id].name, name, sizeof(s_sig[id].name));
        s_sig[id].min = min;
        s_sig[id].max = max;
    }
    portEXIT_CRITICAL(&s_lock);
    return id;
}

int lnd_signal_register(const char *name, float min, float max)
{
    return find_or_add(name, min, max, true);
}

int lnd_signal_id(const char *name)
{
    return find_or_add(name, 0, 100, false);
}

void lnd_signal_set_timeout(int id, uint32_t timeout_ms)
{
    if (id >= 0 && id < LND_SIGNAL_MAX_COUNT) {
        s_sig[id].timeout_ms = timeout_ms;
    }
}

void lnd_signal_set(int id, float value)
{
    if (id < 0 || id >= LND_SIGNAL_MAX_COUNT) {
        return;
    }
    if (is_held(&s_sig[id])) {
        return; // the bench has the signal
    }
    s_sig[id].value = value;
    s_sig[id].stamp_ms = now_ms();
    s_sig[id].valid = true;
}

void lnd_signal_hold(int id, float value, uint32_t hold_ms)
{
    if (id < 0 || id >= LND_SIGNAL_MAX_COUNT) {
        return;
    }
    s_sig[id].value = value;
    s_sig[id].stamp_ms = now_ms();
    s_sig[id].hold_until = now_ms() + hold_ms;
    s_sig[id].hold = true;
    s_sig[id].valid = true;
}

void lnd_signal_release(int id)
{
    if (id >= 0 && id < LND_SIGNAL_MAX_COUNT && s_sig[id].hold) {
        s_sig[id].hold = false;
        s_sig[id].valid = false; // the source writes it again, or "no data"
    }
}

int lnd_signal_find(const char *name)
{
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_sig[i].name, name, LND_SIGNAL_NAME_MAX - 1) == 0) {
            return i;
        }
    }
    return -1;
}

int lnd_signal_count(void)
{
    return s_count;
}

bool lnd_signal_info(int id, lnd_signal_info_t *out)
{
    if (id < 0 || id >= s_count) {
        return false;
    }
    const signal_t *s = &s_sig[id];
    strlcpy(out->name, s->name, sizeof(out->name));
    out->min = s->min;
    out->max = s->max;
    out->held = is_held(s);
    out->age_ms = (uint32_t)(now_ms() - s->stamp_ms);
    out->value = s->value;
    out->valid = false;
    float v;
    if (lnd_signal_get(id, &v)) {
        out->valid = true;
        out->value = v;
    }
    return true;
}

bool lnd_signal_get(int id, float *out)
{
    if (id < 0 || id >= LND_SIGNAL_MAX_COUNT || !s_sig[id].valid) {
        return false;
    }
    uint32_t to = is_held(&s_sig[id]) ? 0 : s_sig[id].timeout_ms;
    if (to != 0 && (uint32_t)(now_ms() - s_sig[id].stamp_ms) > to) {
        return false;
    }
    *out = s_sig[id].value;
    return true;
}

void lnd_signal_invalidate(int id)
{
    if (id >= 0 && id < LND_SIGNAL_MAX_COUNT) {
        s_sig[id].valid = false;
    }
}

void lnd_signal_invalidate_all(void)
{
    for (int i = 0; i < LND_SIGNAL_MAX_COUNT; i++) {
        s_sig[i].valid = false;
    }
}

void lnd_signal_sim_exempt(int id)
{
    if (id >= 0 && id < LND_SIGNAL_MAX_COUNT) {
        s_sig[id].sim_exempt = true;
    }
}

void lnd_signal_sim_enable(bool enable)
{
    bool was = s_sim;
    s_sim = enable;
    if (was && !enable) {
        lnd_signal_invalidate_all();
    }
}

bool lnd_signal_sim_is_enabled(void)
{
    return s_sim;
}

void lnd_signal_sim_step(uint32_t t_ms)
{
    if (!s_sim) {
        return;
    }
    uint32_t stamp = now_ms();
    for (int i = 0; i < s_count; i++) {
        if (s_sig[i].sim_exempt || is_held(&s_sig[i])) {
            continue;
        }
        // Period 5-8 s, different for each signal so they do not move in step.
        uint32_t period = 5000 + (uint32_t)(i % 4) * 1000;
        uint32_t t = (t_ms + (uint32_t)i * 777) % period;
        float x = (float)t / (float)period;           // 0..1
        float tri = x < 0.5f ? x * 2.0f : (1.0f - x) * 2.0f;
        s_sig[i].value = s_sig[i].min + (s_sig[i].max - s_sig[i].min) * tri;
        s_sig[i].stamp_ms = stamp;
        s_sig[i].valid = true;
    }
}
