#ifndef POWER_STUB_ADAFRUIT_I2CDEVICE_H
#define POWER_STUB_ADAFRUIT_I2CDEVICE_H

#include "Wire.h"

class Adafruit_I2CDevice {
public:
    Adafruit_I2CDevice(uint8_t, TwoWire *) {}
    bool begin()
    {
        return power_io_fake::perform(power_io_fake::Kind::Begin, 0U).ok;
    }
};

#endif
