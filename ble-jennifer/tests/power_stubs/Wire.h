#ifndef POWER_STUB_WIRE_H
#define POWER_STUB_WIRE_H

#include "power_io_fake.h"

class TwoWire {
public:
    uint16_t getTimeOut() const { return power_io_fake::state().timeout_ms; }
    void setTimeOut(uint16_t timeout_ms)
    {
        power_io_fake::state().timeout_ms = timeout_ms;
        power_io_fake::state().timeout_changes.push_back(timeout_ms);
    }
};

extern TwoWire Wire;

#endif
