#include "ble_protocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks = 0U;
#define CHECK(expression) do { assert(expression); ++checks; } while (0)

static void self_test(void)
{
    unsigned hz = 0U;
    const char *valid[] = {"{\"set_rate_hz\":1}", "{\"set_rate_hz\":2}",
        "{\"set_rate_hz\":5}", " { \"set_rate_hz\" : 10 } \n"};
    for (size_t i = 0U; i < sizeof(valid) / sizeof(valid[0]); ++i)
        CHECK(sy_ble_parse_rate_command((const uint8_t *)valid[i], strlen(valid[i]), &hz));
    const char *bad[] = {"", "{}", "{\"set_rate_hz\":0}", "{\"set_rate_hz\":3}",
        "{\"set_rate_hz\":true}", "{\"set_rate_hz\":5.0}", "{\"set_rate_hz\":-1}",
        "{\"set_rate_hz\":05}", "{\"set_rate_hz\":100}", "{\"set_rate_hz\":5}x",
        "{\"set_rate_hz\":5,\"extra\":1}", "{\"set_rate_hz\":\"5\"}"};
    for (size_t i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(!sy_ble_parse_rate_command((const uint8_t *)bad[i], strlen(bad[i]), &hz));
    CHECK(!sy_ble_parse_rate_command(NULL, 3U, &hz));
    sy_telemetry_sample_t sample = {.pitch = NAN, .roll = INFINITY, .power = 0,
        .imu_valid = true, .power_valid = true, .captured_ms = 9000U};
    char json[SY_BLE_JSON_CAPACITY];
    size_t n = sy_telemetry_format(json, sizeof(json), &sample);
    CHECK(n > 0U && strstr(json, "\"pitch\":null") != NULL);
    CHECK(strstr(json, "\"roll\":null") != NULL);
    CHECK(strstr(json, "\"power\":0.00") != NULL);
    CHECK(strcmp(json, "{\"pitch\":null,\"roll\":null,\"power\":0.00,\"pitch_target\":null,\"roll_target\":null}") == 0);
    CHECK(strstr(json, "captured_ms") == NULL && strstr(json, "uptime_ms") == NULL);
    CHECK(strstr(json, "attitude_ref") == NULL && strstr(json, "sequence") == NULL);
    sy_telemetry_set_targets(&sample, 15.0f, -4.0f, true);
    CHECK(sample.targets_valid);
    CHECK(sy_telemetry_format(json, sizeof(json), &sample) > 0U);
    CHECK(strstr(json, "\"pitch_target\":15.00,\"roll_target\":-4.00") != NULL);
    sy_telemetry_set_targets(&sample, 0.0f, 0.0f, true);
    CHECK(sample.targets_valid);
    CHECK(sy_telemetry_format(json, sizeof(json), &sample) > 0U);
    CHECK(strstr(json, "\"pitch_target\":0.00,\"roll_target\":0.00") != NULL);
    sy_telemetry_set_targets(&sample, 15.0f, -4.0f, false);
    CHECK(!sample.targets_valid);
    CHECK(sy_telemetry_format(json, sizeof(json), &sample) > 0U);
    CHECK(strstr(json, "\"pitch_target\":null,\"roll_target\":null") != NULL);
    sy_telemetry_set_targets(&sample, NAN, 4.0f, true);
    CHECK(!sample.targets_valid);
    sy_telemetry_set_targets(&sample, 4.0f, INFINITY, true);
    CHECK(!sample.targets_valid);
    sy_telemetry_set_targets(&sample, 1000001.0f, 2.0f, true);
    CHECK(!sample.targets_valid);
    sy_telemetry_set_targets(&sample, 1.0f, -1000001.0f, true);
    CHECK(!sample.targets_valid);
    CHECK(sy_telemetry_format(json, 3U, &sample) == 0U && json[0] == '\0');
    n = sy_telemetry_format(json, sizeof(json), &sample);
    const size_t limits[] = {20U, 21U, 50U, 182U, 512U};
    for (size_t k = 0U; k < sizeof(limits) / sizeof(limits[0]); ++k) {
        size_t count = sy_ble_fragment_count(n, limits[k]);
        CHECK(count > 0U);
        uint8_t restored[SY_BLE_JSON_CAPACITY] = {0}; size_t used = 0U;
        for (size_t i = 0U; i < count; ++i) {
            uint8_t packet[512];
            size_t bytes = sy_ble_fragment((const uint8_t *)json, n, 65535U, limits[k], i, packet, sizeof(packet));
            CHECK(bytes > 0U && bytes <= limits[k]);
            size_t header = count == 1U ? 0U : SY_BLE_FRAGMENT_HEADER;
            if (header != 0U) CHECK(packet[0] == 0x1e && packet[2] == 255U && packet[3] == 255U);
            memcpy(restored + used, packet + header, bytes - header); used += bytes - header;
        }
        CHECK(used == n && memcmp(restored, json, n) == 0);
    }
    CHECK(sy_ble_fragment_count(n, 6U) == 0U);
    CHECK(sy_ble_fragment_count(0U, 20U) == 0U);
    CHECK(sy_ble_fragment_count(SY_BLE_JSON_CAPACITY, 20U) == 0U);
    sy_ble_session_t s; sy_ble_session_init(&s);
    CHECK(!sy_ble_session_due(&s, 0U));
    sy_ble_session_connect(&s); CHECK(!sy_ble_session_due(&s, 0U));
    sy_ble_session_subscribe(&s, true, 0U);
    CHECK(sy_ble_session_due(&s, 0U)); CHECK(!sy_ble_session_due(&s, 199U));
    CHECK(sy_ble_session_due(&s, 200U));
    CHECK(sy_ble_session_set_rate(&s, 2U, 201U)); CHECK(sy_ble_session_due(&s, 201U));
    CHECK(!sy_ble_session_due(&s, 700U)); CHECK(sy_ble_session_due(&s, 701U));
    CHECK(!sy_ble_session_set_rate(&s, 0U, 701U));
    uint32_t generation = s.generation;
    sy_ble_session_disconnect(&s); CHECK(!sy_ble_session_due(&s, 5000U));
    sy_ble_session_connect(&s); CHECK(s.generation != generation && s.mtu == 23U && s.rate_hz == 2U);
    CHECK(!sy_ble_session_due(&s, 5000U));
    sy_ble_session_subscribe(&s, true, UINT32_MAX - 100U);
    CHECK(sy_ble_session_due(&s, UINT32_MAX - 100U));
    CHECK(!sy_ble_session_due(&s, 398U)); CHECK(sy_ble_session_due(&s, 399U));
    printf("%u C protocol/session assertions passed\n", checks);
}

static void packets(const char *json, size_t length, uint16_t sequence, size_t limit, uint32_t now)
{
    size_t count = sy_ble_fragment_count(length, limit);
    for (size_t i = 0U; i < count; ++i) {
        uint8_t packet[512];
        size_t n = sy_ble_fragment((const uint8_t *)json, length, sequence, limit, i, packet, sizeof(packet));
        assert(n > 0U && n <= limit);
        printf("{\"type\":\"notification\",\"time_ms\":%lu,\"limit\":%lu,\"hex\":\"",
               (unsigned long)now, (unsigned long)limit);
        for (size_t j = 0U; j < n; ++j) printf("%02x", (unsigned)packet[j]);
        puts("\"}");
    }
}

static void trace(void)
{
    sy_ble_session_t s; sy_ble_session_init(&s);
    uint16_t message = 0U;
    sy_telemetry_sample_t sample = {0};
    for (uint32_t now = 0U; now <= 5000U; now += 10U) {
        if (now == 0U || now == 2600U) {
            sy_ble_session_connect(&s);
            printf("{\"type\":\"connect\",\"time_ms\":%lu}\n", (unsigned long)now);
        }
        if (now == 0U || now == 2700U) sy_ble_session_subscribe(&s, true, now);
        if (now == 2000U) {
            sy_ble_session_disconnect(&s);
            puts("{\"type\":\"disconnect\",\"time_ms\":2000}");
        }
        if (now == 3000U) s.mtu = 185U;
        if (now == 1000U || now == 3500U) {
            const char *command = now == 1000U ? "{\"set_rate_hz\":2}" : "{\"set_rate_hz\":10}";
            unsigned rate = 0U;
            assert(sy_ble_parse_rate_command((const uint8_t *)command, strlen(command), &rate));
            assert(sy_ble_session_set_rate(&s, rate, now));
            char reply[24]; size_t n = sy_ble_rate_reply(reply, sizeof(reply), rate);
            packets(reply, n, message++, (size_t)s.mtu - 3U, now);
        }
        if (now % 100U == 0U) {
            sample.pitch = 5.0f * sinf((float)now / 1000.0f);
            sample.roll = -sample.pitch / 2.0f;
            sample.imu_valid = now < 4000U || now >= 4500U;
            sample.power_valid = now >= 1000U;
            sample.power = 1.25f;
            const bool targets_ready = now >= 500U && !(now >= 1500U && now < 1800U);
            sy_telemetry_set_targets(&sample, now >= 4500U ? 0.0f : 2.0f,
                                     now >= 4500U ? 0.0f : -1.0f, targets_ready);
            sample.captured_ms = now;
        }
        if (sy_ble_session_due(&s, now)) {
            char json[SY_BLE_JSON_CAPACITY]; size_t n = sy_telemetry_format(json, sizeof(json), &sample);
            assert(n > 0U); packets(json, n, message++, (size_t)s.mtu - 3U, now);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--trace") == 0) trace();
    else self_test();
    return 0;
}
