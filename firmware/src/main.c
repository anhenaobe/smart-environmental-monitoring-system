/* Smart Environmental Monitor v0.3.0 — DHT11.
 * Shared by NUCLEO-L432KC and NUCLEO-L412KB; see firmware/README.md.
 * USART1 is the data UART, NOT the USB ST-LINK VCP.
 */
#include "stm32l4xx_hal.h"
#include "env_control.h"
#include "env_dht11.h"
#include "env_protocol.h"

#include <stdio.h>
#include <string.h>

#if !defined(STM32L432xx) && !defined(STM32L412xx)
#error "Select the nucleo_l432kc or nucleo_l412kb build environment"
#endif

#if defined(SEM_SYNTH_SENSOR)
#define FW_VERSION "0.3.90" /* Explicit synthetic-only test image. */
#elif defined(SEM_WATCHDOG_TEST)
#define FW_VERSION "0.3.91" /* Explicit one-shot watchdog test image. */
#else
#define FW_VERSION "0.3.0"
#endif
#define RX_Q_SIZE 128u
#define STACK_PATTERN 0xA5C39E71u
#define GREEN_PIN GPIO_PIN_3 /* PB3, LD3/D13, active high */
#define RED_PIN GPIO_PIN_0   /* PB0, D3, external, active high */
#define BUTTON_PIN GPIO_PIN_12 /* PA12, D2, external, active low */
#define DHT_PIN GPIO_PIN_5    /* PB5, D11, CN3.14, open drain */
#define CPU_CYCLES_PER_US 16u
#define DHT_START_US 20000u
#define DHT_MAX_START_US 30000u
#define DHT_SAMPLE_MS 3000u
#define DHT_STARTUP_MS 2000u

static UART_HandleTypeDef huart1;
static volatile uint8_t rx_q[RX_Q_SIZE];
static volatile uint8_t rx_head, rx_tail;
static uint8_t rx_byte;
static volatile bool rx_overflow;
static env_rx_t uart_parser;
static env_control_t control;
static uint32_t next_sample_ms, serial_seq, sensor_errors;
#ifndef SEM_SYNTH_SENSOR
static uint32_t dht_start_cycle;
static bool dht_start_pending;
#endif
static uint32_t uptime_s, uptime_ms, last_tick_ms, last_sta_ms, loop_max_us;
static uint8_t reset_cause;
#ifdef SEM_WATCHDOG_TEST
static bool stack_test_done;
__attribute__((noinline)) static void stack_test(void) {
    volatile uint32_t buffer[512];
    for (unsigned i = 0; i < 512u; ++i) buffer[i] = i;
    /* Read it to keep "set but not used" diagnostics clean. */
    (void)buffer[511];
}
#endif
extern uint32_t _stack_bottom;
static uintptr_t stack_paint_end;

void SysTick_Handler(void) { HAL_IncTick(); } /* Do not rely on weak vector. */

/* The linker reserves 4 KiB at the top of SRAM. Paint only below the active
 * MSP, before HAL initialization. The portion already used during startup
 * and this call is conservatively treated as used. */
__attribute__((noinline)) static void stack_seed(void) {
    volatile uint32_t *p = &_stack_bottom;
    const uintptr_t sp = (uintptr_t)__get_MSP();
    while ((uintptr_t)p + sizeof(*p) <= sp) *p++ = STACK_PATTERN;
    stack_paint_end = (uintptr_t)p;
}

static uint32_t stack_free_bytes(void) {
    volatile const uint32_t *p = &_stack_bottom;
    const uintptr_t end = stack_paint_end;
    while ((uintptr_t)p + sizeof(*p) <= end && *p == STACK_PATTERN) ++p;
    return (uint32_t)((uintptr_t)p - (uintptr_t)&_stack_bottom);
}

static void led_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GREEN_PIN | RED_PIN, GPIO_PIN_RESET);

    GPIO_InitTypeDef g = {0};
    g.Pin = GREEN_PIN | RED_PIN;
#ifdef SEM_TRACE_LOOP
    /* PB1/D6 produces one scope pulse per completed loop. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
    g.Pin |= GPIO_PIN_1;
#endif
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = BUTTON_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);


}

static void fail_blink(void) {
    HAL_GPIO_WritePin(GPIOB, GREEN_PIN, GPIO_PIN_RESET);
    for (;;) {
        HAL_GPIO_TogglePin(GPIOB, RED_PIN);
        HAL_Delay(100u);
        /* Never refresh IWDG here: a fatal error should reset once active. */
    }
}

static void clock_init(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) fail_blink();
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) fail_blink();

    /* DWT cycle counter: all loop iterations include HAL calls and IRQs.
     * SWD debug remains enabled; no need for an extra timer or GPIO. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    const uint32_t before = DWT->CYCCNT;
    __NOP();
    __NOP();
    if (DWT->CYCCNT == before) fail_blink();
}

static uint8_t capture_reset_cause(void) {
    const uint32_t flags = RCC->CSR;
    uint8_t cause = 4;
    /* A reset can set several flags (e.g. PINRSTF as well as IWDGRSTF). */
    if (flags & RCC_CSR_IWDGRSTF) cause = 2;
    else if (flags & RCC_CSR_SFTRSTF) cause = 3;
    else if (flags & RCC_CSR_BORRSTF) cause = 0;
    else if (flags & (RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF | RCC_CSR_OBLRSTF))
        cause = 4;
    else if (flags & RCC_CSR_PINRSTF) cause = 1;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    return cause;
}

static void watchdog_start(void) {
    /* IWDG starts independent LSI automatically. Divide by 64, count
     * 2000 ticks = ~4 s nominal at 32 kHz (not a calibrated timeout).
     * Direct registers shared by both STM32L4 MCUs. */
    IWDG->KR = 0xCCCCu;
    IWDG->KR = 0x5555u;
    IWDG->PR = 4u;
    IWDG->RLR = 1999u;
    const uint32_t t0 = HAL_GetTick();
    while (IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) {
        if (HAL_GetTick() - t0 >= 100u) fail_blink();
    }
    IWDG->KR = 0xAAAAu;
}

void HAL_UART_MspInit(UART_HandleTypeDef *h) {
    if (h->Instance != USART1) return;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP; /* RX remains idle high if cable disconnected */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void uart_init(void) {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) fail_blink();
    env_rx_init(&uart_parser);
    if (HAL_UART_Receive_IT(&huart1, &rx_byte, 1u) != HAL_OK) fail_blink();
}

void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) {
    if (h != &huart1) return;
    const uint8_t next = (uint8_t)((rx_head + 1u) & (RX_Q_SIZE - 1u));
    if (next != rx_tail) {
        rx_q[rx_head] = rx_byte;
        rx_head = next;
    } else {
        rx_overflow = true;
    }
    (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1u);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *h) {
    if (h != &huart1) return;
    rx_overflow = true; /* Do not treat a partially lost frame as valid. */
    (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1u);
}

static void parse_incoming(void) {
    /* Briefly mask interrupts for shared ring resynchronization. */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (rx_overflow) {
        rx_tail = rx_head;
        rx_overflow = false;
        uart_parser.length = 0u;
        ++uart_parser.errors;
    }
    if (huart1.RxState == HAL_UART_STATE_READY)
        (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1u);
    __set_PRIMASK(primask);

    char payload[ENV_MAX_FRAME + 1u];
    /* Limit work per iteration, even if a noisy sender floods UART. */
    for (unsigned i = 0; i < RX_Q_SIZE && rx_tail != rx_head; ++i) {
        const char b = (char)rx_q[rx_tail];
        rx_tail = (uint8_t)((rx_tail + 1u) & (RX_Q_SIZE - 1u));
        if (rx_overflow) break;
        const size_t n = env_rx_byte(&uart_parser, b, payload, sizeof(payload));
        if (!n || rx_overflow) continue;
        if (n == 5u && !memcmp(payload, "SYS,", 4u) &&
            payload[4] >= '0' && payload[4] <= '3') {
            env_control_sys(&control, (uint8_t)(payload[4] - '0'), HAL_GetTick());
        } else if (n == 7u && !memcmp(payload, "ACK,RST", 7u)) {
            env_control_ack(&control);
        }
    }
}

static void tx_payload(const char *payload) {
    char frame[ENV_MAX_FRAME + 1u];
    const size_t n = env_frame(frame, sizeof(frame), payload);
    if (n) (void)HAL_UART_Transmit(&huart1, (uint8_t *)frame, (uint16_t)n, 20u);
}

static void report_measurement(uint8_t error, int32_t tc, uint16_t rh) {
    char payload[ENV_MAX_FRAME + 1u];
    control.sensor_status = error;
    if (error)
        (void)snprintf(payload, sizeof(payload), "ENV,%08lu,,,%u",
                       (unsigned long)serial_seq, (unsigned)error);
    else
        (void)snprintf(payload, sizeof(payload), "ENV,%08lu,%+06ld,%05u,0",
                       (unsigned long)serial_seq, (long)tc, (unsigned)rh);
    tx_payload(payload);
    serial_seq = (serial_seq + 1u) % 100000000u;
}

#ifndef SEM_SYNTH_SENSOR
static void dht_init(void) {
    /* In open-drain output mode a written 1 releases the wire; IDR
     * remains readable. The external 4.7k pull-up goes to 3.3 V. */
    HAL_GPIO_WritePin(GPIOB, DHT_PIN, GPIO_PIN_SET);
    GPIO_InitTypeDef g = {0};
    g.Pin = DHT_PIN;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
}

/* DWT is running at HSI16. Subtraction is safe across the 32-bit
 * wrap; each call has a hard deadline (~120 us). The GPIO read stays
 * inline, and UART/SysTick interrupts remain enabled so SYS/ACK can
 * still arrive. Severe timing interference is a rejected observation. */
static bool dht_wait_level(bool high, uint32_t timeout_us, uint32_t *edge) {
    const uint32_t start = DWT->CYCCNT;
    const uint32_t timeout_cycles = timeout_us * CPU_CYCLES_PER_US;
    for (;;) {
        const uint32_t now = DWT->CYCCNT;
        if (now - start >= timeout_cycles) return false;
        if (((GPIOB->IDR & DHT_PIN) != 0u) == high) {
            *edge = now;
            return true;
        }
    }
}

static uint32_t dht_width_us(uint32_t a, uint32_t b) {
    return (b - a) / CPU_CYCLES_PER_US;
}

static bool dht_read_frame(uint8_t data[5]) {
    uint32_t rise, fall, next_rise;
    /* Start low was generated asynchronously; release immediately before
     * polling for the response. Never drive high against the sensor. */
    /* Do not miss the short idle-high gap (nominal 10..20 us) just
     * after release because of a UART ISR. The IRQ-masked section is
     * bounded to 45 us even on a stuck bus; all data pulses are read
     * with interrupts enabled (115200 8N1 byte period is 86.8 us). */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    GPIOB->BSRR = DHT_PIN;
    const bool released_high = dht_wait_level(true, 45u, &rise);
    __set_PRIMASK(primask);
    if (!released_high || !dht_wait_level(false, 120u, &fall) ||
        !dht_wait_level(true, 120u, &next_rise))
        return false;
    uint32_t width = dht_width_us(fall, next_rise);
    if (width < 55u || width > 115u) return false;   /* response low */

    rise = next_rise;
    if (!dht_wait_level(false, 120u, &fall)) return false;
    width = dht_width_us(rise, fall);
    if (width < 55u || width > 115u) return false;   /* response high */

    for (unsigned i = 0; i < 40u; ++i) {
        if (!dht_wait_level(true, 120u, &rise)) return false;
        width = dht_width_us(fall, rise);
        if (width < 25u || width > 85u) return false; /* bit low */
        if (!dht_wait_level(false, 120u, &fall)) return false;
        const int bit = env_dht11_bit(dht_width_us(rise, fall));
        if (bit < 0) return false;
        const unsigned k = i / 8u;
        data[k] = (uint8_t)((data[k] << 1) | (unsigned)bit);
    }

    /* Verify the final low and release. A bus stuck at LOW must not
     * produce an apparent valid last bit plus valid checksum. */
    if (!dht_wait_level(true, 120u, &rise)) return false;
    width = dht_width_us(fall, rise);
    return width >= 25u && width <= 85u;
}

static void report_sensor_result(dht11_status_t status, int32_t tc, uint16_t rh) {
    /* Wire field err_i2c is a legacy name; with DHT11 it counts
     * communication errors (timeout/timing and checksum). */
    if ((status == DHT11_TIMEOUT || status == DHT11_BAD_CHECKSUM) &&
        sensor_errors != UINT32_MAX) ++sensor_errors;
    report_measurement((uint8_t)status, tc, rh);
}
#endif

static void sensor_task(uint32_t now) {
#ifdef SEM_SYNTH_SENSOR
    /* Synthetic diagnostic mode has no GPIO acquisition. UART at 1 Hz. */
    if ((int32_t)(now - next_sample_ms) >= 0) {
        next_sample_ms += 1000u;
        if ((int32_t)(now - next_sample_ms) >= 0) next_sample_ms = now + 1000u;
        report_measurement(0u, 2150, 4500u);
    }
#else
    if (!dht_start_pending) {
        if ((int32_t)(now - next_sample_ms) < 0) return;

        /* Rebase at the actual start; never catch up with closely spaced
         * reads after a delayed loop. Every trigger is >=3 s apart. */
        next_sample_ms = now + DHT_SAMPLE_MS;
        if (!(GPIOB->IDR & DHT_PIN)) {
            report_sensor_result(DHT11_TIMEOUT, 0, 0);
            return; /* short, missing pull-up, or sensor holding the line */
        }
        GPIOB->BSRR = (uint32_t)DHT_PIN << 16;
        dht_start_cycle = DWT->CYCCNT;
        dht_start_pending = true;
        return;
    }

    /* 18..30 ms START low is nonblocking for the 1 Hz heartbeat, LEDs,
     * and button. Only the subsequent edge capture occupies the loop. */
    const uint32_t elapsed_us = (DWT->CYCCNT - dht_start_cycle) / CPU_CYCLES_PER_US;
    if (elapsed_us < DHT_START_US) return;
    dht_start_pending = false;
    if (elapsed_us >= DHT_MAX_START_US) {
        GPIOB->BSRR = DHT_PIN;
        report_sensor_result(DHT11_TIMEOUT, 0, 0);
        return;
    }

    uint8_t frame[5] = {0};
    if (!dht_read_frame(frame)) {
        report_sensor_result(DHT11_TIMEOUT, 0, 0);
        return;
    }
    int32_t tc = 0;
    uint16_t rh = 0;
    const dht11_status_t status = env_dht11_decode(frame, &tc, &rh);
    report_sensor_result(status, tc, rh);
#endif
}

static void report_status(void) {
    char payload[ENV_MAX_FRAME + 1u];
    const uint32_t stack = stack_free_bytes();
    (void)snprintf(payload, sizeof(payload), "STA,%08lu,%lu,%u,%s,%05lu,%05lu",
                   (unsigned long)uptime_s, (unsigned long)sensor_errors,
                   (unsigned)reset_cause, FW_VERSION, (unsigned long)loop_max_us,
                   (unsigned long)stack);
    tx_payload(payload);
    loop_max_us = 0;
}

static void update_uptime(uint32_t now) {
    /* Unlike HAL_GetTick()/1000, this does not reset after a 49.7-day wrap. */
    uptime_ms += now - last_tick_ms;
    last_tick_ms = now;
    uptime_s += uptime_ms / 1000u;
    uptime_ms %= 1000u;
}

int main(void) {
    stack_seed();
    reset_cause = capture_reset_cause();
    HAL_Init();
    led_init();
    clock_init();
    watchdog_start();
    uart_init();
#ifndef SEM_SYNTH_SENSOR
    dht_init();
#endif

    const uint32_t start = HAL_GetTick();
    last_tick_ms = last_sta_ms = start;
#ifdef SEM_SYNTH_SENSOR
    next_sample_ms = start;
#else
    next_sample_ms = start + DHT_STARTUP_MS;
#endif
    env_control_init(&control, start);
    report_status(); /* At boot, before the first measurement. */

    for (;;) {
#ifdef SEM_TRACE_LOOP
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
#endif
        const uint32_t cycle_start = DWT->CYCCNT;
        uint32_t now = HAL_GetTick();
        update_uptime(now);
        parse_incoming();
        sensor_task(HAL_GetTick());

        now = HAL_GetTick();
        /* While START is low, do not start a blocking BTN/STA TX that
         * could extend the pulse past 30 ms. At most ~20 ms deferral,
         * shorter than button debounce; UART RX and LED stay active. */
#ifdef SEM_SYNTH_SENSOR
        const bool can_tx = true;
#else
        const bool can_tx = !dht_start_pending;
#endif
        if (can_tx && env_control_button(&control, HAL_GPIO_ReadPin(GPIOA, BUTTON_PIN) == GPIO_PIN_RESET, now))
            tx_payload("BTN,RST");
        const env_leds_t led = env_control_leds(&control, now);
        HAL_GPIO_WritePin(GPIOB, GREEN_PIN, led.green ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, RED_PIN, led.red ? GPIO_PIN_SET : GPIO_PIN_RESET);

        if (can_tx && now - last_sta_ms >= 10000u) {
            last_sta_ms = now;
            report_status();
        }

#ifdef SEM_WATCHDOG_TEST
        /* One shot per non-IWDG boot, then report cause 2 on reboot.
         * This build is for PI-06 only: normal firmware has no test hook. */
        if (uptime_s >= 15u && reset_cause != 2u) {
            for (;;) {} /* Stop refreshing, leave interrupts enabled. */
        } else if (uptime_s >= 15u && !stack_test_done) {
            /* After the IWDG reboot, PI-16 can compare STA at 10 and 20 s. */
            stack_test_done = true;
            stack_test();
        }
#endif
        /* Refresh only at the end of a completed loop (never in an ISR). */
        IWDG->KR = 0xAAAAu;
        const uint32_t elapsed_us = (DWT->CYCCNT - cycle_start) / CPU_CYCLES_PER_US;
        if (elapsed_us > loop_max_us)
            loop_max_us = elapsed_us > 99999u ? 99999u : elapsed_us;
#ifdef SEM_TRACE_LOOP
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
#endif
    }
}
