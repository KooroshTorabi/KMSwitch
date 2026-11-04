// Status LED: WS2812 on the RP2040-Zero (variant A) or red / green / blue LEDs with PWM (variant B).
// MCU-A also drives the optional external WS2812 / SK6812 on the RGB header.
#include "led.h"
#include "board.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/time.h"

static uint8_t cr, cg, cb, pat;
static uint32_t pat_start;

// WS2812 PIO program (800 kHz, T1 = 2, T2 = 5, T3 = 3 cycles)
static const uint16_t ws2812_prog[] = {0x6221, 0x1123, 0x1400, 0xa442};
static const struct pio_program ws2812_program = {.instructions = ws2812_prog, .length = 4, .origin = -1};
static PIO ws_pio = pio1;
static int ws_off = -1;

__attribute__((unused)) static void ws_init(uint sm, uint pin) {
    if (ws_off < 0) ws_off = (int)pio_add_program(ws_pio, &ws2812_program);
    pio_gpio_init(ws_pio, pin);
    pio_sm_set_consecutive_pindirs(ws_pio, sm, pin, 1, true);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, (uint)ws_off, (uint)ws_off + 3);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (800000.0f * 10));
    pio_sm_init(ws_pio, sm, (uint)ws_off, &c);
    pio_sm_set_enabled(ws_pio, sm, true);
}
__attribute__((unused)) static void ws_put(uint sm, uint8_t r, uint8_t g, uint8_t b) {
    if (!pio_sm_is_tx_fifo_full(ws_pio, sm))
        pio_sm_put(ws_pio, sm, ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8));
}

void led_init(void) {
#if KMS_BOARD == 'A'
    ws_init(0, PIN_LED_WS2812);
#else
    const uint pins[3] = {PIN_LED_R, PIN_LED_G, PIN_LED_B};
    for (int i = 0; i < 3; i++) {
        gpio_set_function(pins[i], GPIO_FUNC_PWM);
        uint s = pwm_gpio_to_slice_num(pins[i]);
        pwm_set_wrap(s, 255);
        pwm_set_enabled(s, true);
        pwm_set_gpio_level(pins[i], 0);
    }
#endif
#ifdef KMS_ROLE_A
    ws_init(1, PIN_RGB_EXT);
#endif
}

void led_set(uint8_t pattern, uint8_t r, uint8_t g, uint8_t b) {
    if (pattern != pat) pat_start = to_ms_since_boot(get_absolute_time());
    pat = pattern; cr = r; cg = g; cb = b;
}

static void out(uint8_t r, uint8_t g, uint8_t b) {
#if KMS_BOARD == 'A'
    ws_put(0, r / 4, g / 4, b / 4);   // the on-board LED is very bright
#else
    // perceptual balance of the three discrete LEDs (red 1k, green 330R, blue 100R)
    pwm_set_gpio_level(PIN_LED_R, r);
    pwm_set_gpio_level(PIN_LED_G, (uint16_t)(g * 3 / 4));
    pwm_set_gpio_level(PIN_LED_B, b);
#endif
#ifdef KMS_ROLE_A
    ws_put(1, r, g, b);
#endif
}

void led_task(void) {
    static uint32_t last;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last < 20) return;
    last = now;
    uint32_t t = now - pat_start;
    bool on = true;
    switch (pat) {
    case LED_BLINK_FAST: on = (t / 125) % 2 == 0; break;
    case LED_BLINK_SLOW: on = (t / 500) % 2 == 0; break;
    case LED_FLASH_TWICE: on = t < 800 && (t / 200) % 2 == 0; break;
    default: break;
    }
    if (on) out(cr, cg, cb); else out(0, 0, 0);
}
