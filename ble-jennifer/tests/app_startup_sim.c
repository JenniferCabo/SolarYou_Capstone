/* Run the actual application startup with real project types/state reset and
 * fake sensor/controller/ESP calls. No board, scheduler or motor is involved. */
#include "../firmware/ESP_SolarYou/main/ESP_SolarYou.c"
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

static unsigned assertions, sensor_inits, imu_calibrations, light_calibrations;
static unsigned controller_inits, ble_starts, ble_polls, loop_iterations;
static bool all_ready, status_ok, imu_calibration_ok, light_calibration_ok;
static bool power_optional_log, optional_warning;
static esp_err_t controller_result;
static sensor_init_status_t reported_status;
static jmp_buf loop_return;
#define CHECK(x) do { assert(x); ++assertions; } while (0)

const char *esp_err_to_name(esp_err_t error) { (void)error; return "simulated error"; }
int64_t esp_timer_get_time(void) { return 0; }
void startup_log(const char *tag, const char *format, ...)
{
    (void)tag;
    char message[256];
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (strstr(message, "INA219 power sensor:") && strstr(message, "optional")) {
        power_optional_log = true;
    }
    if (strstr(message, "An optional sensor is unavailable") ||
        strstr(message, "INA219 power sensor unavailable")) optional_warning = true;
}
void startup_delay(unsigned ticks)
{
    (void)ticks;
    ++loop_iterations;
    longjmp(loop_return, 1);
}
void ble_app_start(void) { ++ble_starts; }
void ble_app_update_imu(float pitch, float roll, bool valid)
{ (void)pitch; (void)roll; (void)valid; }
void ble_app_update_targets(float pitch, float roll, bool valid)
{ (void)pitch; (void)roll; (void)valid; }
void ble_app_poll(void) { ++ble_polls; }
bool sensor_adapter_init(void) { ++sensor_inits; return all_ready; }
bool sensor_adapter_get_init_status(sensor_init_status_t *status)
{
    *status = reported_status;
    return status_ok;
}
bool sensor_adapter_capture_imu_neutral(void)
{ ++imu_calibrations; return imu_calibration_ok; }
bool sensor_adapter_capture_light_neutral(void)
{ ++light_calibrations; return light_calibration_ok; }
bool sensor_adapter_read_imu(sensor_imu_sample_t *sample)
{ memset(sample, 0, sizeof(*sample)); return false; }
esp_err_t stabilization_init(pid_controller_t *roll, pid_controller_t *pitch,
                             const stab_config_t *config)
{
    assert(roll == &roll_ctlr && pitch == &pitch_ctlr && config != NULL);
    ++controller_inits;
    return controller_result;
}
esp_err_t stabilization_update(pid_controller_t *roll, pid_controller_t *pitch,
                               stab_output_t *output, const float *dt,
                               float roll_target, float pitch_target)
{
    (void)roll; (void)pitch; (void)output; (void)dt;
    (void)roll_target; (void)pitch_target;
    return ESP_FAIL;
}
esp_err_t light_update(system_state_t *state, const stab_config_t *config)
{ (void)state; (void)config; return ESP_FAIL; }

static void reset_fixture(void)
{
    sensor_inits = imu_calibrations = light_calibrations = controller_inits = 0;
    ble_starts = ble_polls = loop_iterations = 0;
    all_ready = status_ok = imu_calibration_ok = light_calibration_ok = true;
    power_optional_log = optional_warning = false;
    controller_result = ESP_OK;
    reported_status = (sensor_init_status_t){
        .imu_enabled = true, .imu_initialized = true,
        .light_enabled = true, .light_initialized = true,
        .power_enabled = true, .power_initialized = true
    };
    memset(&curr_state, 0x42, sizeof(curr_state));
}

static void run_startup(void)
{
    if (setjmp(loop_return) == 0) app_main();
    CHECK(sensor_inits == 1U && ble_starts == 1U);
}

static void check_running(void)
{
    CHECK(curr_state.mode == SYSTEM_BASIC_CONTROL);
    CHECK(loop_iterations == 1U && ble_polls == 1U);
    CHECK(imu_calibrations == 1U);
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    CHECK(controller_inits == 1U);
#else
    CHECK(controller_inits == 0U);
#endif
}

static void check_stopped(unsigned expected_imu_calibrations,
                           unsigned expected_light_calibrations,
                           unsigned expected_controller_inits)
{
    CHECK(curr_state.mode == SYSTEM_ERROR);
    CHECK(loop_iterations == 0U && ble_polls == 0U);
    CHECK(imu_calibrations == expected_imu_calibrations);
    CHECK(light_calibrations == expected_light_calibrations);
    CHECK(controller_inits == expected_controller_inits);
}

int main(void)
{
    /* A configured but missing INA219 must not stop a healthy control path. */
    reset_fixture();
    all_ready = false; reported_status.power_initialized = false;
    run_startup(); check_running();
    CHECK(light_calibrations == 1U);
    CHECK(power_optional_log && optional_warning);

    /* Disabling the optional power path is also a valid configuration. */
    reset_fixture();
    reported_status.power_enabled = false; reported_status.power_initialized = false;
    run_startup(); check_running();
    CHECK(power_optional_log && !optional_warning);

    reset_fixture();
    run_startup(); check_running();
    CHECK(light_calibrations == 1U && !optional_warning);

    reset_fixture();
    all_ready = false; reported_status.imu_initialized = false;
    run_startup(); check_stopped(0, 0, 0);
    reset_fixture();
    all_ready = false; reported_status.imu_enabled = false;
    reported_status.imu_initialized = false;
    run_startup(); check_stopped(0, 0, 0);

    reset_fixture();
    all_ready = false; reported_status.light_initialized = false;
    run_startup();
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    check_stopped(0, 0, 0);
#else
    check_running(); CHECK(light_calibrations == 0U);
#endif
    reset_fixture();
    reported_status.light_enabled = false; reported_status.light_initialized = false;
    run_startup();
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    check_stopped(0, 0, 0);
#else
    check_running(); CHECK(light_calibrations == 0U);
#endif

    reset_fixture();
    status_ok = false;
    run_startup(); check_stopped(0, 0, 0);
    reset_fixture();
    imu_calibration_ok = false;
    run_startup(); check_stopped(1, 0, 0);
    reset_fixture();
    light_calibration_ok = false;
    run_startup();
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    check_stopped(1, 1, 0);
#else
    check_running(); CHECK(light_calibrations == 1U);
#endif
    reset_fixture();
    controller_result = ESP_FAIL;
    run_startup();
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    check_stopped(1, 1, 1);
    printf("%u application startup assertions passed (full-control; fake sensors/ESP)\n", assertions);
#else
    check_running();
    printf("%u application startup assertions passed (motor-off; fake sensors/ESP)\n", assertions);
#endif
    return 0;
}
