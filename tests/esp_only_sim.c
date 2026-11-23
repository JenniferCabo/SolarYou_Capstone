/* Link the real ESP-only application with no sensor or motor implementations.
 * Any accidental dependency on those APIs fails to link this test. */
#include "../ESP_SolarYou/main/ESP_SolarYou.c"
#include "../ESP_SolarYou/main/ble_app.c"
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

static int capture_printf(const char *format, ...);
#define printf capture_printf
#include "../ESP_SolarYou/main/esp_only_test.c"
#undef printf

static jmp_buf stop_test;
static int64_t fake_us;
static unsigned assertions, delays, starts, publications, usb_messages, labels;
static sy_telemetry_sample_t latest;
#define CHECK(x) do { assert(x); ++assertions; } while (0)

int64_t esp_timer_get_time(void) { return fake_us; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test"; }
void startup_log(const char *tag, const char *format, ...)
{ (void)tag; if (strstr(format, "generated readings")) ++labels; }
void startup_delay(unsigned ticks)
{
    CHECK(ticks == 20U);
    if (++delays == 61U) longjmp(stop_test, 1);
    fake_us += (int64_t)ticks * 1000;
}
esp_err_t ble_telemetry_init(void) { ++starts; return ESP_OK; }
void ble_telemetry_publish(const sy_telemetry_sample_t *sample)
{ latest = *sample; ++publications; }

static int capture_printf(const char *format, ...)
{
    char line[SY_BLE_JSON_CAPACITY + 10];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    CHECK(strstr(line, "\"simulated\":true") != NULL);
    ++usb_messages;
    return length;
}

int main(void)
{
    if (setjmp(stop_test) == 0) app_main();
    CHECK(starts == 1U && labels == 1U);
    CHECK(publications >= 12U && usb_messages >= 6U);
    CHECK(latest.simulated && latest.imu_valid && latest.power_valid);
    CHECK(!latest.targets_valid);
    CHECK(latest.pitch != 0 && latest.power > 0);
    char json[SY_BLE_JSON_CAPACITY];
    CHECK(sy_telemetry_format(json, sizeof(json), &latest) > 0U);
    CHECK(strstr(json, "\"simulated\":true") != NULL);
    CHECK(strstr(json, "\"pitch_target\":null,\"roll_target\":null") != NULL);
    printf("ESP-only startup: %u assertions passed; no sensor or motor APIs linked\n", assertions);
    return 0;
}
