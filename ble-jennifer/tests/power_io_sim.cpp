// Runs the production INA driver, power wrapper, health, and electrical math.
// The fake BusIO boundary checks order, values, and each transaction's timeout;
// this is an I/O failure model, not a measurement of ESP hardware timing.
#include <Adafruit_INA219.h>
#include "power_sensor.h"
#include "power_io_fake.h"
#include <cassert>
#include <cmath>
#include <cstdio>

TwoWire Wire;
static unsigned assertions;
#define CHECK(condition) do { assert(condition); ++assertions; } while (0)
namespace fake = power_io_fake;
using fake::Kind;

bool sensorBusBegin()
{
    ++fake::state().bus_begin_calls;
    return fake::state().bus_begin_ok;
}

static void expectCalibration(uint16_t calibration = 4096U,
                              uint16_t config = 0x399fU)
{
    fake::expect(Kind::Write, INA219_REG_CALIBRATION, true, calibration);
    fake::expect(Kind::Write, INA219_REG_CONFIG, true, config);
}

static void expectDriverBegin()
{
    fake::expect(Kind::Begin, 0U, true);
    expectCalibration();
}

static void checkConsumed(std::size_t expected_count)
{
    CHECK(fake::state().expected.empty());
    CHECK(fake::state().observed.size() == expected_count);
}

static void driverBegin(Adafruit_INA219 &driver)
{
    expectDriverBegin();
    CHECK(driver.begin(&Wire));
    CHECK(driver.success());
    checkConsumed(3U);
    fake::clearOperations();
}

static void checkTimeoutRestored(uint16_t original)
{
    CHECK(Wire.getTimeOut() == original);
    CHECK(fake::state().timeout_changes.size() == 2U);
    CHECK(fake::state().timeout_changes[0] == 2U);
    CHECK(fake::state().timeout_changes[1] == original);
    for (const auto &operation : fake::state().observed) {
        CHECK(operation.timeout_ms == 2U);
    }
}

static void checkDriverInitialization()
{
    for (unsigned stage = 0U; stage < 3U; ++stage) {
        fake::reset();
        Adafruit_INA219 driver;
        CHECK(!driver.success());
        fake::expect(Kind::Begin, 0U, stage != 0U);
        if (stage >= 1U) {
            fake::expect(Kind::Write, INA219_REG_CALIBRATION, stage != 1U, 4096U);
        }
        if (stage >= 2U) {
            fake::expect(Kind::Write, INA219_REG_CONFIG, false, 0x399fU);
        }
        CHECK(!driver.begin(&Wire));
        CHECK(!driver.success());
        checkConsumed(static_cast<std::size_t>(stage + 1U));
        fake::clearOperations();
        driverBegin(driver); // A subsequent initialization can recover.
    }
}

static void checkDriverGetters()
{
    fake::reset();
    Adafruit_INA219 driver;
    driverBegin(driver);
    fake::expect(Kind::Read, INA219_REG_BUSVOLTAGE, false);
    CHECK(driver.getBusVoltage_V() == 0.0f);
    CHECK(!driver.success());
    fake::expect(Kind::Read, INA219_REG_SHUNTVOLTAGE, false);
    CHECK(driver.getShuntVoltage_mV() == 0.0f);
    CHECK(!driver.success());
    checkConsumed(2U);
    fake::clearOperations();

    // Both getters must preserve a failed calibration write and skip the read.
    for (unsigned power = 0U; power < 2U; ++power) {
        fake::expect(Kind::Write, INA219_REG_CALIBRATION, false, 4096U);
        CHECK((power != 0U ? driver.getPower_mW() : driver.getCurrent_mA()) == 0.0f);
        CHECK(!driver.success());
        checkConsumed(1U);
        fake::clearOperations();
        fake::expect(Kind::Write, INA219_REG_CALIBRATION, true, 4096U);
        fake::expect(Kind::Read, power != 0U ? INA219_REG_POWER : INA219_REG_CURRENT,
                     false);
        CHECK((power != 0U ? driver.getPower_mW() : driver.getCurrent_mA()) == 0.0f);
        CHECK(!driver.success());
        checkConsumed(2U);
        fake::clearOperations();
        fake::expect(Kind::Write, INA219_REG_CALIBRATION, true, 4096U);
        fake::expect(Kind::Read, power != 0U ? INA219_REG_POWER : INA219_REG_CURRENT,
                     true, 250U);
        CHECK((power != 0U ? driver.getPower_mW() : driver.getCurrent_mA()) ==
              (power != 0U ? 500.0f : 25.0f));
        CHECK(driver.success());
        checkConsumed(2U);
        fake::clearOperations();
    }

    fake::expect(Kind::Read, INA219_REG_BUSVOLTAGE, true, 24000U);
    CHECK(std::fabs(driver.getBusVoltage_V() - 12.0f) < 0.0001f);
    CHECK(driver.success());
    fake::expect(Kind::Read, INA219_REG_SHUNTVOLTAGE, true, 0xff9cU);
    CHECK(driver.getShuntVoltage_mV() == -1.0f);
    CHECK(driver.success());
    checkConsumed(2U);
}

static void checkCalibrationModes()
{
    typedef void (Adafruit_INA219::*Calibration)();
    const Calibration modes[] = {&Adafruit_INA219::setCalibration_32V_2A,
                                 &Adafruit_INA219::setCalibration_32V_1A,
                                 &Adafruit_INA219::setCalibration_16V_400mA};
    const uint16_t calibrations[] = {4096U, 10240U, 8192U};
    const uint16_t configs[] = {0x399fU, 0x399fU, 0x019fU};
    const float currents[] = {100.0f, 40.0f, 50.0f};
    const float powers[] = {2000.0f, 800.0f, 1000.0f};
    for (unsigned mode = 0U; mode < 3U; ++mode) {
        fake::reset();
        Adafruit_INA219 driver;
        driverBegin(driver);
        fake::expect(Kind::Write, INA219_REG_CALIBRATION, false, calibrations[mode]);
        (driver.*modes[mode])();
        CHECK(!driver.success());
        checkConsumed(1U);
        fake::clearOperations();
        fake::expect(Kind::Write, INA219_REG_CALIBRATION, true, calibrations[mode]);
        fake::expect(Kind::Write, INA219_REG_CONFIG, false, configs[mode]);
        (driver.*modes[mode])();
        CHECK(!driver.success());
        checkConsumed(2U);
        fake::clearOperations();
        expectCalibration(calibrations[mode], configs[mode]);
        (driver.*modes[mode])();
        CHECK(driver.success());
        fake::expect(Kind::Write, INA219_REG_CALIBRATION, true, calibrations[mode]);
        fake::expect(Kind::Read, INA219_REG_CURRENT, true, 1000U);
        CHECK(driver.getCurrent_mA() == currents[mode]);
        CHECK(driver.success());
        fake::expect(Kind::Write, INA219_REG_CALIBRATION, true, calibrations[mode]);
        fake::expect(Kind::Read, INA219_REG_POWER, true, 1000U);
        CHECK(driver.getPower_mW() == powers[mode]);
        CHECK(driver.success());
        checkConsumed(6U);
    }
}

static void expectWrapperBegin()
{
    expectDriverBegin();
    expectCalibration();
}

static void wrapperBegin()
{
    fake::reset();
    expectWrapperBegin();
    CHECK(powerSensorBegin());
    checkConsumed(5U);
    CHECK(fake::state().bus_begin_calls == 1U);
    for (const auto &operation : fake::state().observed) {
        CHECK(operation.timeout_ms == 250U);
    }
    CHECK(fake::state().timeout_changes.empty());
    sensor_health_t health;
    powerSensorGetHealth(health);
    CHECK(health.state == SENSOR_STATE_VALID && !health.has_sample);
    fake::clearOperations();
}

static void expectWrapperRead(uint16_t bus = 24000U, uint16_t shunt = 200U,
                              uint16_t current = 5000U)
{
    fake::expect(Kind::Read, INA219_REG_BUSVOLTAGE, true, bus);
    fake::expect(Kind::Read, INA219_REG_SHUNTVOLTAGE, true, shunt);
    fake::expect(Kind::Write, INA219_REG_CALIBRATION, true, 4096U);
    fake::expect(Kind::Read, INA219_REG_CURRENT, true, current);
}

static void checkUninitializedRead()
{
    const uint16_t original = Wire.getTimeOut();
    PowerReading reading = {5.0f, 5.0f, 5.0f, 5.0f, true};
    CHECK(!powerSensorRead(reading));
    CHECK(!reading.valid && reading.busVoltageV == 0.0f &&
          reading.shuntVoltageMv == 0.0f && reading.currentA == 0.0f &&
          reading.powerW == 0.0f);
    CHECK(Wire.getTimeOut() == original);
    CHECK(fake::state().timeout_changes.empty());
    sensor_health_t health;
    powerSensorGetHealth(health);
    CHECK(health.state == SENSOR_STATE_FAULT && !health.has_sample);
    CHECK((health.active_faults & SENSOR_FAULT_COMMUNICATION_ERROR) != 0U);
}

static void checkWrapperInitialization()
{
    fake::reset();
    checkUninitializedRead();
    checkConsumed(0U);
    fake::reset();
    fake::state().bus_begin_ok = false;
    CHECK(!powerSensorBegin());
    CHECK(fake::state().bus_begin_calls == 1U);
    checkUninitializedRead();
    checkConsumed(0U);
    for (unsigned stage = 0U; stage < 5U; ++stage) {
        fake::reset();
        fake::expect(Kind::Begin, 0U, stage != 0U);
        for (unsigned operation = 1U; operation <= stage; ++operation) {
            const bool calibration = operation % 2U != 0U;
            fake::expect(Kind::Write,
                         calibration ? INA219_REG_CALIBRATION : INA219_REG_CONFIG,
                         operation != stage, calibration ? 4096U : 0x399fU);
        }
        CHECK(!powerSensorBegin());
        CHECK(fake::state().bus_begin_calls == 1U);
        checkUninitializedRead();
        checkConsumed(static_cast<std::size_t>(stage + 1U));
    }
    wrapperBegin(); // Setup can recover after every initialization failure.
}

static void recordGoodSample()
{
    fake::state().now_ms = 100U;
    fake::state().timeout_ms = 137U;
    expectWrapperRead();
    PowerReading reading;
    CHECK(powerSensorRead(reading));
    CHECK(reading.valid && reading.busVoltageV == 12.0f &&
          reading.shuntVoltageMv == 2.0f && reading.currentA == 0.5f &&
          reading.powerW == 6.0f);
    checkConsumed(4U);
    checkTimeoutRestored(137U);
    sensor_health_t health;
    powerSensorGetHealth(health);
    CHECK(health.has_sample && health.sample_time_ms == 100U &&
          health.sequence == 1U && health.age_ms == 0U);
    fake::clearOperations();
}

static void checkWrapperFailureAndRecovery()
{
    const uint16_t previous_timeouts[] = {250U, 37U, 0U, UINT16_MAX};
    for (unsigned stage = 0U; stage < 4U; ++stage) {
        wrapperBegin();
        recordGoodSample();
        fake::state().timeout_ms = previous_timeouts[stage];
        fake::state().now_ms = 200U + stage;
        fake::expect(Kind::Read, INA219_REG_BUSVOLTAGE, stage != 0U, 24000U);
        if (stage >= 1U) {
            fake::expect(Kind::Read, INA219_REG_SHUNTVOLTAGE, stage != 1U, 200U);
        }
        if (stage >= 2U) {
            fake::expect(Kind::Write, INA219_REG_CALIBRATION, stage != 2U, 4096U);
        }
        if (stage >= 3U) {
            fake::expect(Kind::Read, INA219_REG_CURRENT, false);
        }
        PowerReading reading = {5.0f, 5.0f, 5.0f, 5.0f, true};
        CHECK(!powerSensorRead(reading));
        CHECK(!reading.valid);
        CHECK(reading.currentA == 0.0f && reading.powerW == 0.0f);
        checkConsumed(static_cast<std::size_t>(stage + 1U));
        checkTimeoutRestored(previous_timeouts[stage]);
        sensor_health_t health;
        powerSensorGetHealth(health);
        CHECK(health.state == SENSOR_STATE_FAULT);
        CHECK(health.active_faults == SENSOR_FAULT_COMMUNICATION_ERROR);
        CHECK(health.has_sample && health.sample_time_ms == 100U &&
              health.sequence == 1U && health.age_ms == 100U + stage);
        fake::clearOperations();

        // A genuine all-zero measurement recovers and remains valid.
        fake::state().now_ms = 300U + stage;
        expectWrapperRead(0U, 0U, 0U);
        CHECK(powerSensorRead(reading));
        CHECK(reading.valid && reading.busVoltageV == 0.0f &&
              reading.shuntVoltageMv == 0.0f && reading.currentA == 0.0f &&
              reading.powerW == 0.0f);
        checkConsumed(4U);
        checkTimeoutRestored(previous_timeouts[stage]);
        powerSensorGetHealth(health);
        CHECK(health.state == SENSOR_STATE_VALID && health.active_faults == SENSOR_FAULT_NONE);
        CHECK(health.sample_time_ms == 300U + stage && health.sequence == 2U && health.age_ms == 0U);
        CHECK((health.latched_faults & SENSOR_FAULT_COMMUNICATION_ERROR) != 0U);
        powerSensorClearLatchedFaults();
        powerSensorGetHealth(health);
        CHECK(health.latched_faults == SENSOR_FAULT_NONE);
    }
}

static void checkStaleAndRangeFailures()
{
    wrapperBegin();
    recordGoodSample();
    fake::state().now_ms = 601U;
    fake::expect(Kind::Read, INA219_REG_BUSVOLTAGE, false);
    PowerReading reading;
    CHECK(!powerSensorRead(reading));
    checkConsumed(1U);
    checkTimeoutRestored(137U);
    sensor_health_t health;
    powerSensorGetHealth(health);
    CHECK(health.active_faults == (SENSOR_FAULT_COMMUNICATION_ERROR | SENSOR_FAULT_STALE_DATA));
    CHECK(health.sample_time_ms == 100U && health.age_ms == 501U && health.sequence == 1U);

    const uint16_t buses[] = {54000U, 24000U, 24000U};
    const uint16_t currents[] = {5000U, 21000U, 65336U}; // 27 V; 2.1 A; -0.02 A.
    for (unsigned sample = 0U; sample < 3U; ++sample) {
        fake::clearOperations();
        fake::state().now_ms = 602U + sample;
        expectWrapperRead(buses[sample], 200U, currents[sample]);
        CHECK(!powerSensorRead(reading));
        CHECK(!reading.valid);
        checkConsumed(4U);
        checkTimeoutRestored(137U);
        powerSensorGetHealth(health);
        const sensor_fault_flags_t expected = sample < 2U
            ? SENSOR_FAULT_OUT_OF_RANGE : SENSOR_FAULT_UNEXPECTED_REVERSE_CURRENT;
        CHECK(health.active_faults == (expected | SENSOR_FAULT_STALE_DATA));
        CHECK(health.sample_time_ms == 100U && health.sequence == 1U);
    }
}

int main()
{
    checkDriverInitialization();
    checkDriverGetters();
    checkCalibrationModes();
    checkWrapperInitialization();
    checkWrapperFailureAndRecovery();
    checkStaleAndRangeFailures();
    std::printf("%u power I/O assertions passed (real INA/wrapper; fake BusIO/Wire)\n",
                assertions);
    return 0;
}
