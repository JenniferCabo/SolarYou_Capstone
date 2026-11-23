/* Exercise the real bridge with controlled time and no sensor API linked. */
#include "../ESP_SolarYou/main/ble_app.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int64_t fake_us;
static unsigned assertions, publications, errors;
static esp_err_t init_result;
static sy_telemetry_sample_t latest;
#define CHECK(x) do { assert(x); ++assertions; } while (0)

int64_t esp_timer_get_time(void) { return fake_us; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "fake error"; }
void bridge_log(const char *tag, const char *format, ...)
{ (void)tag; (void)format; ++errors; }
esp_err_t ble_telemetry_init(void) { return init_result; }
void ble_telemetry_publish(const sy_telemetry_sample_t *value)
{ latest = *value; ++publications; }

static void reset_fixture(void)
{
    snapshot = (sy_telemetry_sample_t){0};
    latest = snapshot;
    started = false;
    fake_us = last_capture_us = last_imu_update_us = 0;
    last_power_update_us = last_targets_update_us = 0;
    publications = errors = 0;
    init_result = ESP_OK;
}

static void check_json(const char *text)
{
    char json[SY_BLE_JSON_CAPACITY];
    CHECK(sy_telemetry_format(json, sizeof(json), &latest) != 0U);
    CHECK(strstr(json, text) != NULL);
}

int main(void)
{
    reset_fixture();
    ble_app_poll();
    CHECK(publications == 0U);
    init_result = ESP_FAIL;
    ble_app_start();
    fake_us = 100000;
    ble_app_poll();
    CHECK(!started && publications == 0U && errors == 1U);

    reset_fixture();
    ble_app_start();
    CHECK(started && publications == 1U);
    CHECK(!latest.imu_valid && !latest.power_valid && !latest.targets_valid);
    ble_app_update_imu(1, -2, true);
    ble_app_update_power(0, true); /* Zero watts is a real measurement. */
    ble_app_update_targets(0, 5, true);
    fake_us = 99999;
    ble_app_poll();
    CHECK(publications == 1U);
    fake_us = 100000;
    ble_app_poll();
    CHECK(publications == 2U && latest.captured_ms == 100U);
    check_json("\"pitch\":1.00,\"roll\":-2.00,\"power\":0.00");
#if CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    check_json("\"pitch_target\":0.00,\"roll_target\":5.00");
#else
    check_json("\"pitch_target\":null,\"roll_target\":null");
#endif

    /* New power results propagate without rereading any sensor. */
    fake_us = 200000;
    ble_app_update_power(1.25f, true);
    ble_app_poll();
    CHECK(latest.power_valid && latest.power == 1.25f);
    fake_us = 300000;
    ble_app_update_power(1.25f, false);
    ble_app_poll();
    CHECK(!latest.power_valid);
    check_json("\"power\":null");

    /* Repeated publication cannot keep an old measurement fresh. */
    fake_us = 400000;
    ble_app_update_power(2, true);
    for (fake_us = 400000; fake_us <= 1500000; fake_us += 100000) {
        ble_app_update_imu(3, 4, true);
        ble_app_poll();
    }
    CHECK(latest.imu_valid && !latest.power_valid && !latest.targets_valid);
    check_json("\"pitch\":3.00,\"roll\":4.00,\"power\":null");
    fake_us = 1600000;
    ble_app_update_imu(0, 0, true);
    ble_app_update_power(0, true);
    ble_app_update_targets(0, 0, true);
    ble_app_poll();
    CHECK(latest.imu_valid && latest.power_valid);
    check_json("\"pitch\":0.00,\"roll\":0.00,\"power\":0.00");

    fake_us = 1700000;
    ble_app_update_imu(NAN, 2, true);
    ble_app_update_power(INFINITY, true);
    ble_app_update_targets(2, NAN, true);
    ble_app_poll();
    CHECK(!latest.imu_valid && !latest.power_valid && !latest.targets_valid);
    check_json("\"pitch\":null,\"roll\":null,\"power\":null");
    fake_us = 3000000;
    ble_app_poll();
    CHECK(!latest.imu_valid && !latest.power_valid && !latest.targets_valid);
    printf("BLE bridge: %u assertions passed\n", assertions);
    return 0;
}
