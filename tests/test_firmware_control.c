#include "env_control.h"
#include <assert.h>
#include <stdio.h>

static void check_led(env_control_t *ctl, uint32_t ms, bool g, bool r) {
    const env_leds_t l = env_control_leds(ctl, ms);
    assert(l.green == g && l.red == r);
}

static void test_led_priority(void) {
    env_control_t c;
    env_control_init(&c, 0u);
    check_led(&c, 0, true, false);
    check_led(&c, 500, false, false);
    env_control_sys(&c, 1, 1000);
    check_led(&c, 1000, true, false);
    env_control_sys(&c, 2, 2000);
    check_led(&c, 2000, true, true);
    check_led(&c, 2500, true, false);
    /* At exactly five seconds still live. At 5.001 seconds missing. */
    check_led(&c, 7000, true, true);
    check_led(&c, 8000, false, true);
    check_led(&c, 8100, false, false);
    check_led(&c, 8200, false, true);
    check_led(&c, 8300, false, false);
    check_led(&c, 8400, false, false);
    c.sensor_status = 1;
    check_led(&c, 8400, false, true);
    env_control_sys(&c, 3, 9000);
    check_led(&c, 9000, true, true);
    check_led(&c, 9100, false, false);
    check_led(&c, 15000, false, true); /* stale SYS=3 not forever */
}

static void test_button_ack(void) {
    env_control_t c;
    env_control_init(&c, 0);
    assert(!env_control_button(&c, true, 100));
    assert(!env_control_button(&c, false, 149));
    assert(!env_control_button(&c, false, 200));
    assert(!c.pressed);

    /* Press and release at 9 s; no reset. */
    assert(!env_control_button(&c, true, 1000));
    assert(!env_control_button(&c, true, 1050));
    assert(c.pressed);
    assert(!env_control_button(&c, false, 10050));
    assert(!env_control_button(&c, false, 10100));
    assert(!c.pressed && !c.pending);

    /* Sustained 10 s measured from debounced edge. */
    assert(!env_control_button(&c, true, 12000));
    assert(!env_control_button(&c, true, 12050));
    assert(!env_control_button(&c, true, 22049));
    assert(env_control_button(&c, true, 22050));
    assert(c.attempts == 1);
    assert(!env_control_button(&c, true, 23049));
    assert(env_control_button(&c, true, 23050));
    env_control_ack(&c);
    assert(!c.pending);
    assert(!env_control_button(&c, true, 50000));
    /* No automatic retry with button still held. */
}

static void test_retry_and_release(void) {
    env_control_t c;
    env_control_init(&c, 0);
    assert(!env_control_button(&c, true, 10));
    assert(!env_control_button(&c, true, 60));
    assert(env_control_button(&c, true, 10060));
    assert(!env_control_button(&c, false, 10100));
    assert(!env_control_button(&c, false, 10150));

    for (unsigned retry = 1; retry < 5u; ++retry) {
        assert(!env_control_button(&c, false, 10060 + 1000 * retry - 1));
        assert(env_control_button(&c, false, 10060 + 1000 * retry));
    }
    assert(c.attempts == 5);
    assert(!env_control_button(&c, false, 15059));
    assert(!c.failed);
    assert(!env_control_button(&c, false, 15060));
    assert(c.failed && !c.pending);
    check_led(&c, 15060, false, true);
    check_led(&c, 15160, false, false);
    check_led(&c, 15260, false, true);
    check_led(&c, 15360, false, false);
    check_led(&c, 15460, false, true);
    check_led(&c, 15560, false, false);
    check_led(&c, 15660, false, false);
    env_control_ack(&c); /* Late ACK must not create new command. */
    assert(c.failed);
    assert(!env_control_button(&c, true, 17000));
    assert(!env_control_button(&c, true, 17050));
    assert(!c.failed);
    assert(env_control_button(&c, true, 27050));
}

static void test_wrap(void) {
    env_control_t c;
    const uint32_t start = UINT32_MAX - 100u;
    env_control_init(&c, start);
    env_control_sys(&c, 1u, start);
    assert(!env_control_button(&c, true, start));
    assert(!env_control_button(&c, true, start + 50u));
    assert(env_control_button(&c, true, start + 10050u));
    assert(c.pending);
    assert(env_control_button(&c, true, start + 11050u));
    env_control_ack(&c);
    assert(!env_control_button(&c, true, start + 40000u));
}

int main(void) {
    test_led_priority();
    test_button_ack();
    test_retry_and_release();
    test_wrap();
    puts("firmware control OK");
    return 0;
}
