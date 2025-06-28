#include <stdio.h>

#include "esp_now.h"
#include "tusb_hid_keyboard.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

static void espnow_rx_cb(const esp_now_recv_info_t *info,
                         const uint8_t *data, int len)
{
    // first byte = keycode in HID Usage ID set
    if (len) {
        uint8_t keycode = data[0];
        tud_hid_keyboard_report(0 /*id*/, 0 /*modifiers*/, &keycode);
        tud_hid_keyboard_report(0, 0, NULL);        // release
        ESP_LOGI(TAG, "Forwarded key 0x%02X", keycode);
    }
}

void app_main(void)
{
    tinyusb_config_t tusb_cfg = { .device_descriptor = NULL };  // defaults
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_now_init());
    esp_now_register_recv_cb(espnow_rx_cb);

    ESP_LOGI(TAG, "Ready: acts as USB keyboard, waiting ESPNOW frames");
}
