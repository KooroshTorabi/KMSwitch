// microSD in SPI mode (MCU-A). Single and multi block read / write, SDSC / SDHC / SDXC.
#include "sd_spi.h"
#include <string.h>
#include "board.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

static bool ok, block_addr;
static uint32_t sectors;

static inline void cs(bool on) { gpio_put(PIN_SD_CS, !on); }
static uint8_t xfer(uint8_t b) {
    uint8_t r;
    spi_write_read_blocking(SD_SPI, &b, &r, 1);
    return r;
}
static bool wait_ready(uint32_t ms) {
    absolute_time_t t = make_timeout_time_ms(ms);
    while (xfer(0xFF) != 0xFF)
        if (time_reached(t)) return false;
    return true;
}
static void deselect(void) {
    cs(false);
    xfer(0xFF);
}
static bool select_card(void) {
    cs(true);
    xfer(0xFF);
    if (wait_ready(500)) return true;
    deselect();
    return false;
}

static uint8_t cmd(uint8_t c, uint32_t arg) {
    if (c & 0x80) {   // ACMD
        c &= 0x7F;
        uint8_t r = cmd(55, 0);
        if (r > 1) return r;
    }
    if (c != 12) {
        deselect();
        if (!select_card()) return 0xFF;
    }
    uint8_t crc = c == 0 ? 0x95 : c == 8 ? 0x87 : 0x01;
    uint8_t f[6] = {(uint8_t)(0x40 | c), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg, crc};
    spi_write_blocking(SD_SPI, f, 6);
    if (c == 12) xfer(0xFF);
    uint8_t r;
    int n = 10;
    do r = xfer(0xFF); while ((r & 0x80) && --n);
    return r;
}

static bool rx_block(uint8_t *buf, size_t n) {
    absolute_time_t t = make_timeout_time_ms(200);
    uint8_t tok;
    do {
        tok = xfer(0xFF);
        if (time_reached(t)) return false;
    } while (tok == 0xFF);
    if (tok != 0xFE) return false;
    memset(buf, 0xFF, n);
    spi_read_blocking(SD_SPI, 0xFF, buf, n);
    xfer(0xFF);   // CRC
    xfer(0xFF);
    return true;
}

static bool tx_block(const uint8_t *buf, uint8_t token) {
    if (!wait_ready(500)) return false;
    xfer(token);
    if (token == 0xFD) return true;   // stop tran
    spi_write_blocking(SD_SPI, buf, 512);
    xfer(0xFF);
    xfer(0xFF);
    return (xfer(0xFF) & 0x1F) == 0x05;
}

bool sd_present(void) { return !gpio_get(PIN_SD_DET); }

bool sd_init(void) {
    static bool pins;
    if (!pins) {
        spi_init(SD_SPI, 400 * 1000);
        gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI);
        gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
        gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
        gpio_pull_up(PIN_SD_MISO);
        gpio_init(PIN_SD_CS);
        gpio_set_dir(PIN_SD_CS, GPIO_OUT);
        gpio_init(PIN_SD_DET);
        gpio_pull_up(PIN_SD_DET);
        pins = true;
    }
    ok = false;
    sectors = 0;
    if (!sd_present()) return false;
    spi_set_baudrate(SD_SPI, 400 * 1000);
    cs(false);
    for (int i = 0; i < 10; i++) xfer(0xFF);
    if (cmd(0, 0) != 1) { deselect(); return false; }
    bool v2 = false;
    if (cmd(8, 0x1AA) == 1) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = xfer(0xFF);
        if (r7[2] != 0x01 || r7[3] != 0xAA) { deselect(); return false; }
        v2 = true;
    }
    absolute_time_t t = make_timeout_time_ms(1000);
    while (cmd(0x80 | 41, v2 ? 1u << 30 : 0) != 0)
        if (time_reached(t)) { deselect(); return false; }
    block_addr = false;
    if (v2 && cmd(58, 0) == 0) {
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
        block_addr = ocr[0] & 0x40;
    }
    if (!block_addr) cmd(16, 512);
    // capacity from CSD
    uint8_t csd[16];
    if (cmd(9, 0) == 0 && rx_block(csd, 16)) {
        if ((csd[0] >> 6) == 1) {
            uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
            sectors = (c_size + 1) << 10;
        } else {
            uint32_t c_size = ((uint32_t)(csd[6] & 3) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
            uint32_t mult = ((csd[9] & 3) << 1) | (csd[10] >> 7);
            uint32_t rbl = csd[5] & 15;
            sectors = (c_size + 1) << (mult + 2 + rbl - 9);
        }
    }
    deselect();
    spi_set_baudrate(SD_SPI, 25 * 1000 * 1000);
    ok = sectors > 0;
    return ok;
}

bool sd_ready(void) { return ok; }
uint32_t sd_sectors(void) { return sectors; }

bool sd_read(uint32_t lba, uint8_t *buf, uint32_t count) {
    if (!ok || !count) return false;
    uint32_t a = block_addr ? lba : lba * 512;
    bool good = true;
    if (count == 1) {
        good = cmd(17, a) == 0 && rx_block(buf, 512);
    } else if (cmd(18, a) == 0) {
        for (uint32_t i = 0; i < count && good; i++) good = rx_block(buf + 512 * i, 512);
        cmd(12, 0);
    } else {
        good = false;
    }
    deselect();
    return good;
}

bool sd_write(uint32_t lba, const uint8_t *buf, uint32_t count) {
    if (!ok || !count) return false;
    uint32_t a = block_addr ? lba : lba * 512;
    bool good = true;
    if (count == 1) {
        good = cmd(24, a) == 0 && tx_block(buf, 0xFE);
    } else {
        cmd(0x80 | 23, count);
        if (cmd(25, a) == 0) {
            for (uint32_t i = 0; i < count && good; i++) good = tx_block(buf + 512 * i, 0xFC);
            if (!tx_block(NULL, 0xFD)) good = false;
        } else {
            good = false;
        }
    }
    if (!wait_ready(500)) good = false;
    deselect();
    return good;
}

bool sd_sync(void) {
    bool r = select_card();
    deselect();
    return r;
}
