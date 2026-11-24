/* Run the real application bridge with fake time/BLE and the real sensor API
 * declarations. This is neither an ESP scheduler nor a sensor simulation. */
#include "../firmware/ESP_SolarYou/main/ble_app.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned assertions, publications, power_reads, initializations, logs;
static int64_t fake_us, power_read_duration_us;
static esp_err_t init_result;
static bool power_read_result;
static sensor_power_sample_t power_reading;
static sy_telemetry_sample_t latest;
#define CHECK(x) do { assert(x); ++assertions; } while (0)

const char *esp_err_to_name(esp_err_t result) { (void)result; return "fake failure"; }
int64_t esp_timer_get_time(void) { return fake_us; }
void bridge_log(const char *tag, const char *format, ...) { (void)tag; (void)format; ++logs; }
esp_err_t ble_telemetry_init(void) { ++initializations; return init_result; }
void ble_telemetry_publish(const sy_telemetry_sample_t *sample) { latest = *sample; ++publications; }
bool sensor_adapter_read_power(sensor_power_sample_t *sample)
{
    ++power_reads;
    fake_us += power_read_duration_us;
    *sample = power_reading;
    return power_read_result;
}

static void reset_fixture(void)
{
    snapshot = (sy_telemetry_sample_t){0};
    last_capture_us = 0;
    next_power_read_us = 0;
    last_imu_update_us = last_targets_update_us = 0;
    started = false;
    publications = power_reads = initializations = logs = 0;
    fake_us = 0;
    power_read_duration_us = 0;
    init_result = ESP_OK;
    power_read_result = true;
    power_reading = (sensor_power_sample_t){.valid = true, .power_w = 0};
    latest = (sy_telemetry_sample_t){0};
}

static void check_targets(const char *expected)
{
    char json[SY_BLE_JSON_CAPACITY];
    CHECK(sy_telemetry_format(json, sizeof(json), &latest) > 0U);
    CHECK(strstr(json, expected) != NULL);
}

int main(void)
{
    reset_fixture();
    fake_us = 500000;
    ble_app_poll();
    CHECK(power_reads == 0U && publications == 0U);
    init_result = ESP_FAIL;
    ble_app_start();
    CHECK(initializations == 1U && !started && publications == 0U && logs == 1U);
    ble_app_poll();
    CHECK(power_reads == 0U && publications == 0U);

    reset_fixture();
    ble_app_start();
    CHECK(started && initializations == 1U && publications == 1U);
    CHECK(!latest.imu_valid && !latest.power_valid && !latest.targets_valid);
    CHECK(latest.captured_ms == 0U);
    check_targets("\"pitch_target\":null,\"roll_target\":null");
    CHECK(power_reads == 0U);

    ble_app_update_imu(1, 2, true);
    ble_app_update_imu(3, 4, true); /* Keep only the latest sample. */
    ble_app_update_targets(15, -4, true);
    CHECK(power_reads == 0U && publications == 1U);
    fake_us = 99999;
    ble_app_poll();
    CHECK(power_reads == 0U && publications == 1U);
    fake_us = 100000;
    ble_app_poll();
    CHECK(power_reads == 1U && publications == 2U);
    CHECK(latest.captured_ms == 100U && latest.imu_valid);
    CHECK(latest.pitch == 3 && latest.roll == 4);
    CHECK(latest.power_valid && latest.power == 0);
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    CHECK(latest.targets_valid && latest.pitch_target == 15 && latest.roll_target == -4);
    check_targets("\"pitch_target\":15.00,\"roll_target\":-4.00");
#else
    CHECK(!latest.targets_valid);
    check_targets("\"pitch_target\":null,\"roll_target\":null");
#endif
    fake_us = 199999;
    ble_app_poll();
    CHECK(power_reads == 1U && publications == 2U);

    ble_app_update_imu(3, 4, false);
    ble_app_update_targets(15, -4, false);
    power_reading.valid = false;
    fake_us = 200000;
    ble_app_poll();
    CHECK(!latest.imu_valid && !latest.power_valid && !latest.targets_valid);
    CHECK(power_reads == 2U && publications == 3U);
    char json[SY_BLE_JSON_CAPACITY];
    CHECK(sy_telemetry_format(json, sizeof(json), &latest) > 0U);
    CHECK(strstr(json, "\"pitch\":null,\"roll\":null,\"power\":null") != NULL);
    check_targets("\"pitch_target\":null,\"roll_target\":null");

    power_reading.valid = true;
    power_reading.power_w = 1.25f;
    power_read_result = false; /* API failure overrides an allegedly valid struct. */
    fake_us = 300000;
    ble_app_update_imu(9, 8, true);
    ble_app_update_targets(7, 6, true);
    ble_app_poll();
    CHECK(!latest.power_valid && power_reads == 2U && publications == 4U);
    CHECK(latest.imu_valid && latest.pitch == 9 && latest.roll == 8);
    fake_us = 1200000;
    ble_app_poll();
    CHECK(!latest.power_valid && power_reads == 3U && publications == 5U);

    power_read_result = true;
    fake_us = 2200000;
    ble_app_update_imu(0, 0, true);
    ble_app_update_targets(0, 0, true);
    ble_app_poll();
    CHECK(latest.imu_valid && latest.pitch == 0 && latest.roll == 0);
    CHECK(latest.power_valid && latest.power == 1.25f);
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    CHECK(latest.targets_valid);
    check_targets("\"pitch_target\":0.00,\"roll_target\":0.00");
#else
    CHECK(!latest.targets_valid);
    check_targets("\"pitch_target\":null,\"roll_target\":null");
#endif

    ble_app_update_imu(NAN, 2, true);
    CHECK(!snapshot.imu_valid);
    ble_app_update_imu(1, INFINITY, true);
    CHECK(!snapshot.imu_valid);
    ble_app_update_targets(NAN, 5, true);
    fake_us = 2300000;
    ble_app_poll();
    CHECK(!latest.imu_valid && !latest.targets_valid);
    check_targets("\"pitch_target\":null,\"roll_target\":null");

    fake_us = 3200000; /* A slow main loop publishes one latest sample, no backlog. */
    ble_app_poll();
    CHECK(power_reads == 6U && publications == 8U);
    CHECK(latest.captured_ms == 3200U);

    /* Healthy reads wait 100 ms from completion, even if a read is slow. */
    reset_fixture();
    ble_app_start();
    power_read_duration_us = 200000;
    fake_us = 100000;
    ble_app_update_imu(1, 2, true);
    ble_app_update_targets(8, -4, true);
    ble_app_poll();
    CHECK(fake_us == 300000 && latest.captured_ms == 300U);
    CHECK(power_reads == 1U && publications == 2U);
    fake_us = 399999;
    ble_app_poll();
    CHECK(power_reads == 1U && publications == 2U);
    fake_us = 400000;
    ble_app_update_imu(3, 4, true);
    ble_app_poll();
    CHECK(power_reads == 2U && fake_us == 600000 && latest.captured_ms == 600U);

    /* A failed read backs off for one second; new IMU/target snapshots keep
     * publishing every 100 ms and power stays null during that pause. */
    reset_fixture();
    ble_app_start();
    power_read_duration_us = 200000;
    power_read_result = false;
    fake_us = 100000;
    ble_app_update_imu(1, 2, true);
    ble_app_update_targets(8, -4, true);
    ble_app_poll();
    CHECK(power_reads == 1U && fake_us == 300000 && !latest.power_valid);
    for (fake_us = 400000; fake_us < 1300000; fake_us += 100000) {
        unsigned previous_publications = publications;
        ble_app_update_imu(5, 6, true);
        ble_app_update_targets(7, 8, true);
        ble_app_poll();
        CHECK(power_reads == 1U && publications == previous_publications + 1U);
        CHECK(latest.imu_valid && latest.pitch == 5 && !latest.power_valid);
    }
    power_read_duration_us = 0;
    power_read_result = true;
    power_reading.power_w = 1.25f;
    ble_app_update_imu(9, 10, true);
    ble_app_poll();
    CHECK(power_reads == 2U && latest.power_valid && latest.power == 1.25f);
    CHECK(latest.captured_ms == 1300U && latest.pitch == 9);

    /* Completion stamps may not relabel old IMU/target data as fresh. */
    reset_fixture();
    ble_app_start();
    ble_app_update_imu(1, 2, true);
    ble_app_update_targets(8, -4, true);
    power_read_duration_us = 1500000;
    fake_us = 100000;
    ble_app_poll();
    CHECK(latest.captured_ms == 1600U && latest.power_valid);
    CHECK(!latest.imu_valid && !latest.targets_valid);
    CHECK(sy_telemetry_format(json, sizeof(json), &latest) > 0U);
    CHECK(strstr(json, "\"pitch\":null,\"roll\":null,\"power\":0.00") != NULL);
    check_targets("\"pitch_target\":null,\"roll_target\":null");
    power_read_duration_us = 0;
    fake_us = 1700000;
    ble_app_update_imu(0, 0, true);
    ble_app_update_targets(0, 0, true);
    ble_app_poll();
    CHECK(latest.imu_valid && latest.pitch == 0 && latest.roll == 0);
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    CHECK(latest.targets_valid && latest.pitch_target == 0 && latest.roll_target == 0);
#else
    CHECK(!latest.targets_valid);
#endif

    reset_fixture();
    ble_app_start();
    power_reading.power_w = NAN;
    fake_us = 100000;
    ble_app_poll();
    CHECK(!latest.power_valid);
    fake_us = 200000;
    ble_app_poll();
    CHECK(power_reads == 1U && !latest.power_valid);
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    printf("%u BLE bridge assertions passed (full-control; fake time/BLE/power)\n", assertions);
#else
    printf("%u BLE bridge assertions passed (motor-off; fake time/BLE/power)\n", assertions);
#endif
    return 0;
}
