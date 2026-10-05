#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"

/* Matches the C3 Super Mini wiring to the ICC SWD pads. */
#define PIN_SWCLK  GPIO_NUM_6
#define PIN_SWDIO  GPIO_NUM_7
#define PIN_NRESET GPIO_NUM_10

static int swdio_output = 1;

static void swdio_drive(int output)
{
    gpio_set_direction(PIN_SWDIO, output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
    swdio_output = output;
}

static void swd_levels(int swclk, int swdio)
{
    if (swdio_output) {
        gpio_set_level(PIN_SWDIO, swdio);
    }
    gpio_set_level(PIN_SWCLK, swclk);
}

static void nreset_set(int asserted)
{
    if (asserted) {
        gpio_set_direction(PIN_NRESET, GPIO_MODE_OUTPUT);
        gpio_set_level(PIN_NRESET, 0);
    } else {
        gpio_set_direction(PIN_NRESET, GPIO_MODE_INPUT);
        gpio_set_pull_mode(PIN_NRESET, GPIO_PULLUP_ONLY);
    }
}

static int swdio_level(void)
{
    return gpio_get_level(PIN_SWDIO) ? 1 : 0;
}

static void reply(char value)
{
    usb_serial_jtag_write_bytes(&value, 1, portMAX_DELAY);
}

static void handle(uint8_t command)
{
    switch (command) {
    case 'd': swd_levels(0, 0); break;
    case 'e': swd_levels(0, 1); break;
    case 'f': swd_levels(1, 0); break;
    case 'g': swd_levels(1, 1); break;
    case 'O': swdio_drive(1); break;
    case 'o': swdio_drive(0); break;
    case 'c':
    case 'R':
        reply(swdio_level() ? '1' : '0');
        break;
    case 'r': nreset_set(0); break;
    case 's':
    case 'u': nreset_set(1); break;
    case 't': break;
    case 'Z': esp_rom_delay_us(1000); break;
    case 'z': esp_rom_delay_us(1); break;
    case 'Q':
    case 'B':
    case 'b':
        break;
    default:
        if (command >= '0' && command <= '7') {
            swd_levels((command & 0x4) ? 1 : 0, (command & 0x2) ? 1 : 0);
        }
        break;
    }
}

void app_main(void)
{
    gpio_config_t swclk = {
        .pin_bit_mask = 1ULL << PIN_SWCLK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&swclk);
    gpio_set_level(PIN_SWCLK, 1);

    gpio_config_t swdio = {
        .pin_bit_mask = 1ULL << PIN_SWDIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&swdio);
    gpio_set_level(PIN_SWDIO, 1);

    gpio_config_t nreset = {
        .pin_bit_mask = 1ULL << PIN_NRESET,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&nreset);

    usb_serial_jtag_driver_config_t usb = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 256,
    };
    usb_serial_jtag_driver_install(&usb);

    uint8_t buffer[256];
    while (1) {
        int count = usb_serial_jtag_read_bytes(buffer, sizeof(buffer), portMAX_DELAY);
        for (int i = 0; i < count; i++) {
            handle(buffer[i]);
        }
    }
}
