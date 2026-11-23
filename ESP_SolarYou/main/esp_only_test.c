#include "esp_only_test.h"
#include "ble_app.h"
#include "ble_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

static const char *TEST_TAG = "ESP_ONLY_TEST";

void esp_only_test_run(void)
{
    ESP_LOGW(TEST_TAG, "ESP-ONLY TEST: generated readings; no sensors or motors are initialized");
    ESP_LOGW(TEST_TAG, "Bluetooth device: %s. Data is simulated, the Bluetooth link is real.",
             SY_BLE_TEST_DEVICE_NAME);
    ble_app_start();
    int64_t last_update = esp_timer_get_time() - 100000;
    int64_t last_usb = esp_timer_get_time() - 200000;
    while (1) {
        int64_t now = esp_timer_get_time();
        if (now - last_update >= 100000) {
            last_update = now;
            /* Bound the phase before converting the uptime to a float. */
            float phase = (float)(now % 60000000) / 1000000.0f;
            sy_telemetry_sample_t sample = {
                .pitch = 8.0f * sinf(phase),
                .roll = 6.0f * cosf(phase),
                .power = 1.5f + 0.5f * sinf(phase),
                .imu_valid = true,
                .power_valid = true,
                .targets_valid = false,
                .simulated = true,
                .captured_ms = (uint32_t)(now / 1000)
            };
            ble_app_update_imu(sample.pitch, sample.roll, true);
            ble_app_update_power(sample.power, true);
            ble_app_update_targets(0, 0, false);
            if (now - last_usb >= 200000) {
                last_usb = now;
                char json[SY_BLE_JSON_CAPACITY];
                if (sy_telemetry_format(json, sizeof(json), &sample) > 0U) {
                    printf("%s\n", json);
                    fflush(stdout);
                }
            }
        }
        ble_app_poll();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
