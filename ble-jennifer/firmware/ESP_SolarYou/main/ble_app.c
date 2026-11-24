#include "ble_app.h"
#include "ble_telemetry.h"
#include "sensor_api.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

/* Keep the latest snapshot fresh enough for the 10 Hz maximum send setting.
 * All I2C reads stay on the existing application loop, never BLE callbacks. */
#define BLE_CAPTURE_INTERVAL_US 100000
#define BLE_POWER_INTERVAL_US 100000
#define BLE_POWER_RETRY_US 1000000
#define BLE_READING_MAX_AGE_US 1000000
static sy_telemetry_sample_t snapshot;
static int64_t last_capture_us;
static int64_t next_power_read_us;
static int64_t last_imu_update_us;
static int64_t last_targets_update_us;
static bool started;

void ble_app_start(void)
{
    esp_err_t result = ble_telemetry_init();
    if (result != ESP_OK) {
        ESP_LOGE("BLE", "BLE unavailable: %s", esp_err_to_name(result));
        return;
    }
    started = true;
    snapshot.captured_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ble_telemetry_publish(&snapshot);
}

void ble_app_update_imu(float pitch, float roll, bool valid)
{
    snapshot.pitch = pitch;
    snapshot.roll = roll;
    snapshot.imu_valid = valid && isfinite(pitch) && isfinite(roll);
    last_imu_update_us = esp_timer_get_time();
}

void ble_app_update_targets(float pitch, float roll, bool valid)
{
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    /* These are the same setpoints already passed to the existing controller. */
    sy_telemetry_set_targets(&snapshot, pitch, roll, valid);
#else
    (void)pitch; (void)roll; (void)valid;
    snapshot.targets_valid = false;
#endif
    last_targets_update_us = esp_timer_get_time();
}

void ble_app_poll(void)
{
    if (!started) return;
    int64_t now = esp_timer_get_time();
    if (now - last_capture_us < BLE_CAPTURE_INTERVAL_US) return;
    if (now >= next_power_read_us) {
        sensor_power_sample_t power = {0};
        snapshot.power_valid = sensor_adapter_read_power(&power) &&
            power.valid && isfinite(power.power_w);
        snapshot.power = power.power_w;
        int64_t completed_us = esp_timer_get_time();
        next_power_read_us = completed_us +
            (snapshot.power_valid ? BLE_POWER_INTERVAL_US : BLE_POWER_RETRY_US);
    }
    /* Power retries must not pause IMU/target delivery or refresh old values. */
    now = esp_timer_get_time();
    if (now - last_imu_update_us > BLE_READING_MAX_AGE_US) snapshot.imu_valid = false;
    if (now - last_targets_update_us > BLE_READING_MAX_AGE_US) snapshot.targets_valid = false;
    last_capture_us = now;
    snapshot.captured_ms = (uint32_t)(now / 1000);
    ble_telemetry_publish(&snapshot);
}
