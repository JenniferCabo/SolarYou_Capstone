#include "sensor_api.h"

#include <Arduino.h>
#include <string.h>

#include "sdkconfig.h"
#include "imu_sensor.h"
#include "light_sensor.h"
#include "power_sensor.h"

static bool imuInitialized = false;
static bool imuCalibrated = false;
static bool lightInitialized = false;
static bool lightCalibrated = false;
static bool powerInitialized = false;
static uint32_t snapshotSequence = 0U;

bool sensor_adapter_init(void)
{
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    imuInitialized = imuSensorBegin();
    imuCalibrated = false;
#else
    imuInitialized = false;
    imuCalibrated = false;
#endif

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    lightSensorBegin();
    lightInitialized = true;
    lightCalibrated = false;
#else
    lightInitialized = false;
    lightCalibrated = false;
#endif

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    powerInitialized = powerSensorBegin();
#else
    powerInitialized = false;
#endif

    bool ready = true;
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    ready = ready && imuInitialized;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    ready = ready && lightInitialized;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    ready = ready && powerInitialized;
#endif
    return ready;
}

bool sensor_adapter_capture_neutral(void)
{
    // If calibration fails the sensor is still initialized. We can steady it or handle a reset then try calibration again.
    bool ready = true;
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    imuCalibrated = imuInitialized &&
        imuSensorCaptureNeutral(IMU_NEUTRAL_SAMPLES);
    ready = ready && imuCalibrated;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    lightCalibrated = lightInitialized &&
        lightSensorCaptureNeutral(LIGHT_NEUTRAL_SAMPLES);
    ready = ready && lightCalibrated;
#endif
    return ready;
}

bool sensor_adapter_read(sensor_snapshot_t *snapshot)
{
    if (snapshot == nullptr) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->schema_version = SY_SENSOR_SCHEMA_VERSION;
    snapshot->device_uptime_ms = millis();
    snapshot->sequence = ++snapshotSequence;

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    AxisReading imu = {0.0f, 0.0f, false};
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    LightReading light = {{0, 0, 0, 0}, 0.0f, 0.0f, false};
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    PowerReading power = {0.0f, 0.0f, 0.0f, 0.0f, false};
#endif
    if (imuInitialized && imuCalibrated) {
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
        imuSensorReadCentered(imu);
#endif
    }
    if (lightInitialized && lightCalibrated) {
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
        lightSensorReadCentered(light);
#endif
    }
    if (powerInitialized) {
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
        powerSensorRead(power);
#endif
    }

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    snapshot->imu.valid = imu.valid;
    snapshot->imu.pitch_deg = imu.pitch;
    snapshot->imu.roll_deg = imu.roll;
    imuSensorGetHealth(snapshot->imu.health);
    if ((snapshot->imu.health.active_faults &
         SENSOR_FAULT_CALIBRATION_REQUIRED) != SENSOR_FAULT_NONE) {
        imuCalibrated = false;
    }
#endif

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    snapshot->light.valid = light.valid;
    snapshot->light.pitch_error = light.pitch;
    snapshot->light.roll_error = light.roll;
    snapshot->light.top_left_raw = light.raw.topLeft;
    snapshot->light.top_right_raw = light.raw.topRight;
    snapshot->light.bottom_left_raw = light.raw.bottomLeft;
    snapshot->light.bottom_right_raw = light.raw.bottomRight;
    lightSensorGetHealth(snapshot->light.health);
    if ((snapshot->light.health.active_faults &
         SENSOR_FAULT_CALIBRATION_REQUIRED) != SENSOR_FAULT_NONE) {
        lightCalibrated = false;
    }
#endif

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    snapshot->power.valid = power.valid;
    snapshot->power.voltage_v = power.busVoltageV;
    snapshot->power.current_a = power.currentA;
    snapshot->power.power_w = power.powerW;
    snapshot->power.shunt_voltage_mv = power.shuntVoltageMv;
    powerSensorGetHealth(snapshot->power.health);
#endif

    bool selectedSensorsValid = true;
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    selectedSensorsValid = selectedSensorsValid && snapshot->imu.valid;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    selectedSensorsValid = selectedSensorsValid && snapshot->light.valid;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    selectedSensorsValid = selectedSensorsValid && snapshot->power.valid;
#endif
    snapshot->valid = selectedSensorsValid;
    return snapshot->valid;
}

void sensor_adapter_clear_latched_faults(void)
{
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    imuSensorClearLatchedFaults();
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    lightSensorClearLatchedFaults();
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    powerSensorClearLatchedFaults();
#endif
}
