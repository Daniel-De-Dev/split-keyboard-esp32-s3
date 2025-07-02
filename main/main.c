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
#include "rom/ets_sys.h"

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

// Place holder mappings for now
const uint8_t keymap[NUM_ROWS][NUM_COLS] = {
  { HID_KEY_Q,   HID_KEY_W,  HID_KEY_E,  HID_KEY_R,  HID_KEY_T, HID_KEY_Y, HID_KEY_U },
  { HID_KEY_A,   HID_KEY_S,  HID_KEY_D,  HID_KEY_F,  HID_KEY_G, HID_KEY_H, HID_KEY_J },
  { HID_KEY_Z,   HID_KEY_X,  HID_KEY_C,  HID_KEY_V,  HID_KEY_B, HID_KEY_N, HID_KEY_M },
  { HID_KEY_1,   HID_KEY_2,  HID_KEY_3,  HID_KEY_4,  HID_KEY_5, HID_KEY_6, HID_KEY_7 },
  { HID_KEY_SHIFT_LEFT, HID_KEY_SPACE, HID_KEY_ENTER,
    HID_KEY_CONTROL_LEFT,  HID_KEY_ALT_LEFT, HID_KEY_TAB, HID_KEY_BACKSPACE },
  { HID_KEY_ESCAPE, HID_KEY_MINUS, HID_KEY_EQUAL,
        HID_KEY_BRACKET_LEFT, HID_KEY_BRACKET_RIGHT, HID_KEY_SEMICOLON, HID_KEY_APOSTROPHE }
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

/* ------------------------ Scan (& Debounce) Logic ------------------------ */

#define SCAN_PERIOD_US   1000
#define DEBOUNCE_MS      5

/* one byte per row, where each bit is its column */
static uint8_t stable[NUM_ROWS];
static uint8_t raw[NUM_ROWS];
static uint8_t cnt[NUM_ROWS][NUM_COLS];

static inline void col_select(int c) {
    gpio_set_level(col_pins[c], 0);
}
static inline void col_release(int c) {
    gpio_set_level(col_pins[c], 1);
}

static void scan_matrix_once(void) {
    memset(raw, 0, sizeof(raw));

    for (int c = 0; c < NUM_COLS; ++c) {
        col_select(c);
        ets_delay_us(3);
        for (int r = 0; r < NUM_ROWS; ++r)
            if (!gpio_get_level(row_pins[r]))
                raw[r] |= 1 << c;
        col_release(c);
    }
}

static bool debounce(void) {
    bool changed = false;

    for (int r = 0; r < NUM_ROWS; ++r) {
        uint8_t diff = raw[r] ^ stable[r];
        if (!diff) continue;

        for (int c = 0; c < NUM_COLS; ++c) {
            if (!(diff & (1 << c))) { cnt[r][c] = 0; continue; }

            if (++cnt[r][c] >= (DEBOUNCE_MS * 1000 / SCAN_PERIOD_US)) {
                stable[r] ^= 1 << c;                // toggle debounced state
                cnt[r][c] = 0;
                changed = true;
            }
        }
    }
    return changed;
}

void matrix_task(void *arg) {
    while (true) {
        scan_matrix_once();
        if (debounce()) {
            /* build + send HID report only when something changed */
            uint8_t mods = 0, keybuf[6] = {0};
            size_t  idx = 0;

            for (int r = 0; r < NUM_ROWS; ++r)
                for (int c = 0; c < NUM_COLS; ++c)
                    if (stable[r] & (1 << c)) {
                        uint8_t code = keymap[r][c];
                        if (code >= HID_KEY_CONTROL_LEFT && code <= HID_KEY_GUI_RIGHT)
                            mods |= 1 << (code - HID_KEY_CONTROL_LEFT);
                        else if (idx < 6)
                            keybuf[idx++] = code;
                    }

            tud_hid_keyboard_report(REPORT_ID_KEYBOARD,
                                    mods,
                                    keybuf);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
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

    matrix_task(NULL);
}
