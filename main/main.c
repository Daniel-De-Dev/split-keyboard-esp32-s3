#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_struct.h"

static const char *TAG = "usb_kbd";

/* ---------------------------------- Pins --------------------------------- */

#define NUM_COLS 7
#define NUM_ROWS 6

static const gpio_num_t row_pins[NUM_ROWS] = {
    GPIO_NUM_9,
    GPIO_NUM_10,
    GPIO_NUM_11,
    GPIO_NUM_12,
    GPIO_NUM_13,
    GPIO_NUM_14
};

static const gpio_num_t col_pins[NUM_COLS] = {
    GPIO_NUM_4,
    GPIO_NUM_5,
    GPIO_NUM_6,
    GPIO_NUM_7,
    GPIO_NUM_15,
    GPIO_NUM_16,
    GPIO_NUM_17
};

/* ------------------------------ Descriptors ------------------------------ */

/**
 * @brief HID report descriptor
 *
 * Decribes the format of reports sendt and recieved.
 *
 * Currently only a single 6-byte keyboard report is exposed
 */
#define REPORT_ID_KEYBOARD 1
static const uint8_t hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD))
};

/**
 * @brief String descriptor
 *
 * Human-readable metadata for the host to display
 */
static const char* string_desc[] = {
    (char[]){ 0x09, 0x04 }, // 0: supported language = English (0x0409)
    "Daniel-De-Dev",                // 1: Manufacturer
    "Split-Keyboard",               // 2: Product
    "123456",                       // 3: Serial
    "Custom-Split-Keyboard",        // 4: Configuration name
};


#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief Configuration descriptor
 */
static const uint8_t cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_desc),
                       0x81, 16, 10),
};

/* ------------------------- TinyUSB HID callbacks ------------------------- */


// Return our report descriptor to the stack
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_desc;
}

// GET_REPORT not supported — return 0 causes stall
uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t,
                               uint8_t*, uint16_t)
{
    return 0;
}


void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t,
                           uint8_t const*, uint16_t) {}

/* --------------------------- Helper Functions ---------------------------- */

// Generate a bitmask represeting a group of pins
static uint64_t make_mask(const gpio_num_t *p, size_t n) {
    uint64_t m = 0;
    for (size_t i = 0; i < n; i++)
        m |= BIT64(p[i]);
    return m;
}

static inline void gpio_set_level_multi(uint64_t mask, uint32_t level) {
    if (level)
        GPIO.out_w1ts = mask;
    else
        GPIO.out_w1tc = mask;
}

// Intilizes the pins based on roles
void matrix_gpio_init(void) {

    // Columns: open-drain outputs, idle released
    uint64_t col_mask = make_mask(col_pins, NUM_COLS);

    gpio_config_t col_io = {
        .pin_bit_mask   = col_mask,
        .mode           = GPIO_MODE_OUTPUT_OD,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&col_io));

    // Columns start released
    gpio_set_level_multi(col_mask, 1);


    // Rows: inputs with internal pull-up
    gpio_config_t row_io = {
        .pin_bit_mask   = make_mask(row_pins, NUM_ROWS),
        .mode           = GPIO_MODE_INPUT,
        .pull_up_en     = GPIO_PULLUP_ENABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&row_io));

    // Todo interrupt init
}

static void send_key_A(void)
{
    uint8_t pressed[6] = { HID_KEY_A };
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, pressed);
    vTaskDelay(pdMS_TO_TICKS(50));
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
}


/* --------------------------------- Main ---------------------------------- */

void app_main(void)
{
    matrix_gpio_init();
    ESP_LOGI(TAG, "Pins Configured");

    const tinyusb_config_t usb_cfg = {
        .string_descriptor = string_desc,
        .string_descriptor_count = sizeof(string_desc)/sizeof(string_desc[0]),
        .configuration_descriptor = cfg_desc,
    };

    tinyusb_driver_install(&usb_cfg);
    ESP_LOGI(TAG, "USB ready");


        



    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
