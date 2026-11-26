#ifndef ENV_CONTROL_H
#define ENV_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

/* HAL-independent button state machine and LED policy. All intervals use
 * unsigned subtraction so the period boundaries survive HAL_GetTick wrap. */
typedef struct {
    uint32_t raw_changed_ms, pressed_ms, attempt_ms, last_sys_ms;
    uint8_t attempts, sys_state, sensor_status;
    bool raw_pressed, pressed, triggered, pending, failed, sys_seen;
} env_control_t;

typedef struct {
    bool green, red;
} env_leds_t;

void env_control_init(env_control_t *ctl, uint32_t now);
void env_control_sys(env_control_t *ctl, uint8_t state, uint32_t now);
void env_control_ack(env_control_t *ctl);
bool env_control_button(env_control_t *ctl, bool raw_pressed, uint32_t now);
env_leds_t env_control_leds(const env_control_t *ctl, uint32_t now);
#endif
