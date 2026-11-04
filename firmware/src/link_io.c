// UART side of the MCU-A <-> MCU-B link: interrupt-driven receive ring, blocking framed send.
#include "link_io.h"
#include "board.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/time.h"

#define RING 8192
static volatile uint8_t ring[RING];
static volatile uint32_t head, tail, overruns;
static kms_link_rx_t rx;
static uint32_t last_rx_ms;

static void on_uart(void) {
    while (uart_is_readable(LINK_UART)) {
        uint8_t b = (uint8_t)uart_get_hw(LINK_UART)->dr;
        uint32_t h = head, n = (h + 1) % RING;
        if (n == tail) { overruns++; continue; }
        ring[h] = b;
        head = n;
    }
}

void link_init(void) {
    uart_init(LINK_UART, LINK_BAUD);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    gpio_pull_up(PIN_UART_RX);   // the isolator output idles high; keep it defined while the other side is off
    uart_set_fifo_enabled(LINK_UART, true);
    int irq = LINK_UART == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, on_uart);
    irq_set_enabled(irq, true);
    uart_set_irq_enables(LINK_UART, true, false);
}

void link_send(uint8_t type, const void *payload, size_t n) {
    static uint8_t buf[LINK_MAX_PAYLOAD + LINK_MAX_PAYLOAD / 254 + 8];
    size_t k = kms_link_encode(type, (const uint8_t *)payload, n, buf, sizeof buf);
    if (k) uart_write_blocking(LINK_UART, buf, k);
}

bool link_poll(uint8_t *type, const uint8_t **payload, size_t *len) {
    while (tail != head) {
        uint8_t b = ring[tail];
        tail = (tail + 1) % RING;
        if (kms_link_rx(&rx, b, type, payload, len)) {
            last_rx_ms = to_ms_since_boot(get_absolute_time());
            return true;
        }
    }
    return false;
}

bool link_alive(void) {
    return last_rx_ms && to_ms_since_boot(get_absolute_time()) - last_rx_ms < 1500;
}
uint32_t link_errors(void) { return rx.bad + overruns; }
