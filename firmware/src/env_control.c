#include "env_control.h"
#include <string.h>

void env_control_init(env_control_t *ctl, uint32_t now) {
    memset(ctl, 0, sizeof(*ctl));
    ctl->last_sys_ms = now;   /* Startup grace for the first Linux heartbeat. */
    ctl->raw_changed_ms = now;
}

void env_control_sys(env_control_t *ctl, uint8_t state, uint32_t now) {
    if (state > 3u) return;
    ctl->sys_state = state;
    ctl->last_sys_ms = now;
    ctl->sys_seen = true;
}

void env_control_ack(env_control_t *ctl) {
    if (ctl->pending) ctl->pending = false;
}

/* Returns true exactly once per RST request attempt. Release before 10 s
 * cancels; once a request has begun, complete the ACK/retry transaction even
 * if the button is released. The last attempt gets a full 1 s ACK window. */
bool env_control_button(env_control_t *ctl, bool raw_pressed, uint32_t now) {
    if (ctl->raw_pressed != raw_pressed) {
        ctl->raw_pressed = raw_pressed;
        ctl->raw_changed_ms = now;
    }
    if (ctl->pressed != ctl->raw_pressed &&
        now - ctl->raw_changed_ms >= 50u) {
        ctl->pressed = ctl->raw_pressed;
        if (ctl->pressed) {
            ctl->pressed_ms = now;
            ctl->triggered = ctl->pending;
            if (!ctl->pending) ctl->failed = false;
        }
    }
    if (ctl->pending) {
        if (now - ctl->attempt_ms < 1000u) return false;
        if (ctl->attempts >= 5u) {
            ctl->pending = false;
            ctl->failed = true;
            return false;
        }
    } else {
        if (!ctl->pressed || !ctl->raw_pressed || ctl->triggered ||
            now - ctl->pressed_ms < 10000u) return false;
        ctl->triggered = true;
        ctl->pending = true;
        ctl->attempts = 0u;
    }
    ++ctl->attempts;
    ctl->attempt_ms = now;
    return true;
}

env_leds_t env_control_leds(const env_control_t *ctl, uint32_t now) {
    env_leds_t led = {false, false};
    const bool live = now - ctl->last_sys_ms <= 5000u;
    const bool slow = now % 1000u < 500u;
    const bool fast = now % 200u < 100u;

    if (ctl->failed) {
        /* Repeat the three-flash failure pattern once per 2 seconds until
         * a new debounced press starts; never retry while the old press holds. */
        const uint32_t phase = (now - ctl->attempt_ms - 1000u) % 2000u;
        led.red = phase < 600u && phase % 200u < 100u;
    } else if (ctl->pressed || ctl->pending ||
               (live && ctl->sys_seen && ctl->sys_state == 3u)) {
        led.green = led.red = fast;
    } else if (ctl->sensor_status != 0u) {
        led.red = true;
    } else if (!live) {
        const uint32_t phase = now % 2000u;
        led.red = phase < 400u && phase % 200u < 100u;
    } else if (ctl->sys_state == 2u) {
        led.green = true;
        led.red = slow;
    } else if (!ctl->sys_seen || ctl->sys_state == 0u) {
        led.green = slow;
    } else {
        led.green = true;
    }
    return led;
}
