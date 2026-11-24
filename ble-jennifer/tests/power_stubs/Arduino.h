#ifndef POWER_STUB_ARDUINO_H
#define POWER_STUB_ARDUINO_H

#include <cstddef>
#include <cstdint>
#include "power_io_fake.h"

#define MSBFIRST 1

inline uint32_t millis()
{
    return power_io_fake::state().now_ms;
}

#endif
