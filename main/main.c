#include <stdlib.h>
#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"

#define BOOT_BUTTON GPIO_NUM_0
static const char *TAG = "usb_kbd";

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

/* ----------------------------- Send Keypress ----------------------------- */

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
    // Configure BOOT button as input with pull-up
    const gpio_config_t btn_cfg = {
        .pin_bit_mask = BIT64(BOOT_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
    };

    gpio_config(&btn_cfg);

    const tinyusb_config_t usb_cfg = {
        .string_descriptor = string_desc,
        .string_descriptor_count = sizeof(string_desc)/sizeof(string_desc[0]),
        .configuration_descriptor = cfg_desc,
    };

    tinyusb_driver_install(&usb_cfg);
    ESP_LOGI(TAG, "USB ready");

    while (true) {
        if (tud_mounted() && !gpio_get_level(BOOT_BUTTON)) {
            ESP_LOGI(TAG, "Sent character");
            send_key_A();
            while (!gpio_get_level(BOOT_BUTTON)) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
