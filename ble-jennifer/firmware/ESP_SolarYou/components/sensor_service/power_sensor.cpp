#include "power_sensor.h"

#include <Arduino.h>
#include <Adafruit_INA219.h>
#include <Wire.h>
#include <cmath>

#include "component_config.h"
#include "sensor_bus.h"
#include "sensor_math.h"
#include "board_config.h"

static Adafruit_INA219 ina219(POWER_I2C_ADDRESS);
static bool initialized = false;
static sensor_health_t health;

namespace {
// Power reads share Wire with the IMU in the main loop. Restore its timeout
// after each read so the shorter INA transaction wait stays local to power.
class ScopedPowerWireTimeout {
public:
    ScopedPowerWireTimeout() : previousTimeoutMs(Wire.getTimeOut())
    {
        Wire.setTimeOut(2U);
    }

    ~ScopedPowerWireTimeout()
    {
        Wire.setTimeOut(previousTimeoutMs);
    }

private:
    const uint16_t previousTimeoutMs;
};
}

bool powerSensorBegin()
{
    sensorHealthInitialize(&health, SENSOR_STATE_INITIALIZING);
    initialized = sensorBusBegin() && ina219.begin(&Wire);
    if (initialized) {
        ina219.setCalibration_32V_2A();
        initialized = ina219.success();
    }
    if (initialized) {
        sensorHealthSetState(&health, SENSOR_STATE_VALID);
    } else {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_COMMUNICATION_ERROR);
        sensorHealthSetState(&health, SENSOR_STATE_FAULT);
    }
    return initialized;
}

bool powerSensorRead(PowerReading &reading)
{
    reading = {0.0f, 0.0f, 0.0f, 0.0f, false};
    if (!initialized) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_COMMUNICATION_ERROR);
        sensorHealthSetState(&health, SENSOR_STATE_FAULT);
        return false;
    }
    const ScopedPowerWireTimeout timeout;
    reading.busVoltageV = ina219.getBusVoltage_V();
    bool communicationOk = ina219.success();
    if (communicationOk) {
        reading.shuntVoltageMv = ina219.getShuntVoltage_mV();
        communicationOk = ina219.success();
    }
    if (communicationOk) {
        reading.currentA = ina219.getCurrent_mA() / 1000.0f;
        communicationOk = ina219.success();
    }

    sensor_fault_flags_t faults = SENSOR_FAULT_NONE;
    if (!communicationOk) faults |= SENSOR_FAULT_COMMUNICATION_ERROR;
    if (!std::isfinite(reading.busVoltageV) ||
        !std::isfinite(reading.shuntVoltageMv) ||
        !std::isfinite(reading.currentA)) faults |= SENSOR_FAULT_NONFINITE_DATA;
    if (std::isfinite(reading.busVoltageV) &&
        (reading.busVoltageV < 0.0f ||
         reading.busVoltageV > POWER_MAX_BUS_VOLTAGE_V)) {
        faults |= SENSOR_FAULT_OUT_OF_RANGE;
    }
    if (std::isfinite(reading.currentA) &&
        std::fabs(reading.currentA) > POWER_MAX_CURRENT_A) {
        faults |= SENSOR_FAULT_OUT_OF_RANGE;
    }
    if (std::isfinite(reading.currentA) &&
        reading.currentA < POWER_REVERSE_FAULT_A) {
        faults |= SENSOR_FAULT_UNEXPECTED_REVERSE_CURRENT;
    }
    if (!calculateElectricalPower(reading.busVoltageV, reading.currentA,
                                  reading.powerW)) {
        faults |= SENSOR_FAULT_NONFINITE_DATA;
    }
    const uint32_t now = millis();
    if (faults == SENSOR_FAULT_NONE) {
        // Keeps the time of the last good sensor reading. A failed or out of range read shouldn't make old data look new.
        sensorHealthRecordSample(&health, now);
    } else {
        sensorHealthUpdateAge(&health, now);
        if (health.has_sample && health.age_ms > POWER_STALE_TIMEOUT_MS) {
            faults |= SENSOR_FAULT_STALE_DATA;
        }
    }
    sensorHealthSetActiveFaults(&health, faults);
    sensorHealthSetState(&health, faults == SENSOR_FAULT_NONE
        ? SENSOR_STATE_VALID : SENSOR_STATE_FAULT);
    reading.valid = faults == SENSOR_FAULT_NONE;
    return reading.valid;
}

void powerSensorGetHealth(sensor_health_t &output)
{
    sensorHealthUpdateAge(&health, millis());
    output = health;
}

void powerSensorClearLatchedFaults()
{
    sensorHealthClearLatched(&health);
}
