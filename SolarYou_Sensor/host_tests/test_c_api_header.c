#include "sensor_api.h"
#include "sensor_json.h"

static void consume_snapshot(const sensor_snapshot_t *snapshot)
{
    if (snapshot != 0 && snapshot->imu.health.state == SENSOR_STATE_VALID) {
        (void)snapshot->imu.pitch_deg;
    }
}

int main(void)
{
    sensor_snapshot_t snapshot = {0};
    char json[SENSOR_JSON_RECOMMENDED_CAPACITY] = {0};
    size_t json_length = 0U;
    consume_snapshot(&snapshot);
    (void)sensor_snapshot_to_json(&snapshot, json, sizeof(json), &json_length);
    return 0;
}
