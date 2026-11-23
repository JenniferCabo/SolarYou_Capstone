#ifndef POWER_STUB_ADAFRUIT_BUSIO_REGISTER_H
#define POWER_STUB_ADAFRUIT_BUSIO_REGISTER_H

#include "Adafruit_I2CDevice.h"

class Adafruit_BusIO_Register {
public:
    Adafruit_BusIO_Register(Adafruit_I2CDevice *, uint8_t reg, uint8_t, uint8_t)
        : reg_(reg) {}
    bool read(uint16_t *value)
    {
        const auto operation = power_io_fake::perform(power_io_fake::Kind::Read, reg_);
        if (operation.ok) *value = operation.value;
        return operation.ok;
    }
    bool write(uint32_t value, uint8_t)
    {
        return power_io_fake::perform(power_io_fake::Kind::Write, reg_,
                                      static_cast<uint16_t>(value)).ok;
    }

private:
    uint8_t reg_;
};

class Adafruit_BusIO_RegisterBits {
public:
    Adafruit_BusIO_RegisterBits(Adafruit_BusIO_Register *reg, uint8_t bits,
                              uint8_t shift)
        : reg_(reg), bits_(bits), shift_(shift) {}
    bool write(uint32_t value)
    {
        uint16_t original = 0U;
        if (!reg_->read(&original)) return false;
        const uint32_t mask = ((UINT32_C(1) << bits_) - 1U) << shift_;
        return reg_->write((static_cast<uint32_t>(original) & ~mask) |
                           ((value << shift_) & mask), 2U);
    }

private:
    Adafruit_BusIO_Register *reg_;
    uint8_t bits_;
    uint8_t shift_;
};

#endif
