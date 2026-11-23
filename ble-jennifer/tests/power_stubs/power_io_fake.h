#ifndef POWER_IO_FAKE_H
#define POWER_IO_FAKE_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

// Script the BusIO boundary while compiling the real INA driver and wrapper.
// Failed reads deliberately leave the caller's destination untouched.
namespace power_io_fake {
enum class Kind { Begin, Read, Write };

struct Operation {
    Kind kind;
    uint8_t reg;
    bool ok;
    uint16_t value;
    uint16_t timeout_ms;
};

struct State {
    std::deque<Operation> expected;
    std::vector<Operation> observed;
    std::vector<uint16_t> timeout_changes;
    uint16_t timeout_ms = 250U;
    uint32_t now_ms = 0U;
    bool bus_begin_ok = true;
    unsigned bus_begin_calls = 0U;
};

inline State &state()
{
    static State fixture;
    return fixture;
}

inline void reset()
{
    state() = State();
}

inline void clearOperations()
{
    if (!state().expected.empty()) {
        std::fputs("Unconsumed power I/O expectations\n", stderr);
        std::abort();
    }
    state().observed.clear();
    state().timeout_changes.clear();
}

inline void expect(Kind kind, uint8_t reg, bool ok, uint16_t value = 0U)
{
    state().expected.push_back({kind, reg, ok, value, 0U});
}

inline Operation perform(Kind kind, uint8_t reg, uint16_t written = 0U)
{
    State &fixture = state();
    if (fixture.expected.empty()) {
        std::fputs("Unexpected power I/O after scripted operations\n", stderr);
        std::abort();
    }
    const Operation operation = fixture.expected.front();
    fixture.expected.pop_front();
    if (operation.kind != kind || operation.reg != reg ||
        (kind == Kind::Write && operation.value != written)) {
        std::fprintf(stderr, "Unexpected power I/O kind/register/value (%u/%u/%u)\n",
                     static_cast<unsigned>(kind), static_cast<unsigned>(reg),
                     static_cast<unsigned>(written));
        std::abort();
    }
    fixture.observed.push_back({kind, reg, operation.ok, operation.value,
                               fixture.timeout_ms});
    return operation;
}
}

#endif
