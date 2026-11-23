/* Run the current application scheduler and BLE bridge with fake hardware.
 * Check that USB and BLE share readings and the existing startup policy stays. */
#include "../ESP_SolarYou/main/ESP_SolarYou.c"
#include "ble_protocol.h"
#if CONFIG_SOLARYOU_BLE_TELEMETRY
#include "../ESP_SolarYou/main/ble_app.c"
#endif
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

static jmp_buf stop_loop;
static int64_t fake_us;
static unsigned assertions, loops, controller_starts, sensor_starts;
static unsigned imu_reads, power_reads, usb_messages, ble_starts, ble_messages;
static bool imu_ready, light_ready_for_test, power_ready, calibration_ok, readings_ok;
static esp_err_t controller_result;
static sy_telemetry_sample_t latest_ble;
static system_state_t latest_usb;
#define CHECK(x) do { assert(x); ++assertions; } while (0)

int64_t esp_timer_get_time(void) { return fake_us; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "simulated error"; }
void startup_log(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
void startup_delay(unsigned ticks)
{
    if (ticks == 1U) {
        if (++loops == 27U) longjmp(stop_loop, 1);
        fake_us += 50000;
    } else {
        fake_us += (int64_t)ticks * 1000;
    }
}

esp_err_t ble_telemetry_init(void) { ++ble_starts; return ESP_OK; }
void ble_telemetry_publish(const sy_telemetry_sample_t *value)
{ ++ble_messages; latest_ble = *value; }

bool sensor_adapter_init(void)
{ ++sensor_starts; return imu_ready && light_ready_for_test && power_ready; }
bool sensor_adapter_get_init_status(sensor_init_status_t *status)
{
    *status = (sensor_init_status_t){
        .imu_enabled = true, .imu_initialized = imu_ready,
        .light_enabled = true, .light_initialized = light_ready_for_test,
        .power_enabled = true, .power_initialized = power_ready
    };
    return true;
}
bool sensor_adapter_capture_imu_neutral(void) { return calibration_ok; }
bool sensor_adapter_capture_light_neutral(void) { return true; }
void sensor_adapter_clear_latched_faults(void) {}
bool sensor_adapter_read_imu(sensor_imu_sample_t *sample)
{
    ++imu_reads;
    *sample = (sensor_imu_sample_t){.valid = readings_ok, .pitch_deg = 1, .roll_deg = -2};
    return readings_ok;
}
esp_err_t stabilization_init(pid_controller_t *roll, pid_controller_t *pitch,
                             const stab_config_t *config)
{
    (void)roll; (void)pitch;
    CHECK(config->roll_servo_center == 100.0f && config->pitch_servo_center == 60.0f);
    ++controller_starts;
    return controller_result;
}
esp_err_t stabilization_update(pid_controller_t *roll, pid_controller_t *pitch,
                               const stab_config_t *config, stab_output_t *output,
                               float dt, float roll_target, float pitch_target)
{
    (void)roll; (void)pitch; (void)config; (void)dt;
    (void)roll_target; (void)pitch_target;
    ++imu_reads;
    *output = (stab_output_t){.pitch_imu = 1, .roll_imu = -2};
    return readings_ok ? ESP_OK : ESP_FAIL;
}
esp_err_t light_update(system_state_t *state, const stab_config_t *config, float dt)
{
    (void)config; (void)dt;
    state->pitch_light = 5;
    state->roll_light = -6;
    return readings_ok ? ESP_OK : ESP_FAIL;
}
esp_err_t power_update(system_state_t *state)
{
    ++power_reads;
    state->power = 1.25f;
    state->power_valid = readings_ok && power_ready;
    return state->power_valid ? ESP_OK : ESP_FAIL;
}
void uart_telemetry_send(const system_state_t *state)
{ ++usb_messages; latest_usb = *state; }

static void reset_fixture(void)
{
    fake_us = 0;
    loops = controller_starts = sensor_starts = imu_reads = power_reads = 0;
    usb_messages = ble_starts = ble_messages = 0;
    imu_ready = light_ready_for_test = power_ready = calibration_ok = readings_ok = true;
    controller_result = ESP_OK;
    latest_usb = (system_state_t){0};
    latest_ble = (sy_telemetry_sample_t){0};
#if CONFIG_SOLARYOU_BLE_TELEMETRY
    snapshot = (sy_telemetry_sample_t){0};
    started = false;
    last_capture_us = last_imu_update_us = last_targets_update_us = last_power_update_us = 0;
#endif
}

static void run_application(void)
{ if (setjmp(stop_loop) == 0) app_main(); }

static void check_running(void)
{
    CHECK(sensor_starts == 1U && loops == 27U);
    CHECK(imu_reads == 26U);
    CHECK(power_reads == 6U && usb_messages == power_reads);
#if CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    CHECK(controller_starts == 1U);
#else
    CHECK(controller_starts == 0U);
#endif
#if CONFIG_SOLARYOU_BLE_TELEMETRY
    CHECK(ble_starts == 1U && ble_messages >= 13U);
#else
    CHECK(ble_starts == 0U && ble_messages == 0U);
#endif
}

int main(void)
{
    reset_fixture();
    run_application();
    check_running();
    CHECK(curr_state.mode == SYSTEM_BASIC_CONTROL);
#if CONFIG_SOLARYOU_BLE_TELEMETRY
    CHECK(latest_ble.imu_valid && latest_ble.power_valid);
    CHECK(latest_ble.pitch == latest_usb.pitch_imu && latest_ble.roll == latest_usb.roll_imu);
    CHECK(latest_ble.power == latest_usb.power && latest_usb.power_valid);
#if CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    CHECK(latest_ble.targets_valid && latest_ble.pitch_target == 5 && latest_ble.roll_target == -6);
#else
    CHECK(!latest_ble.targets_valid);
#endif
#endif

    reset_fixture();
    readings_ok = false;
    run_application();
    check_running();
    CHECK(!latest_usb.power_valid);
#if CONFIG_SOLARYOU_BLE_TELEMETRY
    CHECK(!latest_ble.imu_valid && !latest_ble.power_valid && !latest_ble.targets_valid);
#endif

    /* Keep main's existing startup requirements in each runtime mode. */
    reset_fixture();
    power_ready = false;
    run_application();
#if CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    CHECK(curr_state.mode == SYSTEM_ERROR && loops == 0U && ble_starts == 0U);
#else
    check_running();
    CHECK(!latest_usb.power_valid);
#endif
    reset_fixture();
    imu_ready = false;
    run_application();
    CHECK(curr_state.mode == SYSTEM_ERROR && loops == 0U && ble_starts == 0U);
    reset_fixture();
    calibration_ok = false;
    run_application();
    CHECK(curr_state.mode == SYSTEM_ERROR && loops == 0U && ble_starts == 0U);
#if CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    reset_fixture();
    controller_result = ESP_FAIL;
    run_application();
    CHECK(curr_state.mode == SYSTEM_ERROR && sensor_starts == 0U && ble_starts == 0U);
#endif
    printf("Application integration: %u assertions passed\n", assertions);
    return 0;
}
