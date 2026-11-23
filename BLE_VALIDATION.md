# BLE test results

I tested BLE with my ESP32-S3 using generated readings because I don't have the
sensors or motors. I also tested USB RESET with the unchanged main firmware
without sensors. Actual sensor readings and full control haven't been tested
on my board.

## Board tests

| Test | Result |
| --- | --- |
| Bluetooth connection and dashboard streaming | Passed with generated readings |
| BLE rate confirmation | Initial 5 Hz confirmed; selected 1 Hz and 10 Hz confirmed |
| BLE reconnect after disconnecting/reconnecting the board | Passed; selected 1 Hz and 10 Hz settings were restored |
| Physical RESET while connected through BLE, basic ESP-only firmware | Reconnected automatically; generated readings resumed and 5 Hz was confirmed again |
| USB streaming | Passed with generated readings |
| Physical RESET while using USB, immediate-start firmware | Reconnected in about 2 seconds; readings resumed |
| Physical RESET while using USB, simulated 30-second startup | USB reconnected in about 2 seconds; readings resumed about 30.6 seconds after disconnect |
| Physical RESET while using USB, unchanged main firmware without sensors | USB reconnected in about 2 seconds; no readings arrived |

For the USB RESET tests, I used Zack's existing serial receiver with automatic
port detection. The board returned on the same port with the same USB identity.
The receiver code was unchanged. The delayed test showed a no-data warning
while waiting, then cleared it automatically when readings started.

The 30-second delay was a temporary test; that simulation isn't included in
this update. The basic ESP-only mode starts streaming without an added delay.
For the unchanged-main test, the early startup messages weren't captured, so
I couldn't confirm whether it stopped at a sensor initialization check.

**I haven't reproduced the team's USB failure on my Windows/ESP32-S3 setup.**
Successful startup with the actual sensors wasn't tested.

## Build and software checks

- The real-sensor, motor-off BLE firmware built successfully for ESP32-S3.
- The basic ESP-only firmware built successfully, as did the temporary delay test.
- The unchanged main firmware built successfully for the USB test.
- Protocol, BLE adapter, application integration, and basic ESP-only checks passed.
  The ESP-only checks link no sensor or motor implementations.
- Seven checks decoding C sender packets with Zack's receiver passed.
- Four ESP-only BLE launcher checks passed.
- Zack's dashboard suite ran 42 tests: 41 passed; one Linux-only check was skipped
  on Windows.
- Full-control and USB-only variants passed compiler checks. Those checks were
  not separate complete firmware builds or physical hardware tests.

The test code is in `tests/`; run it with `.\tools\test_ble.ps1` with GCC
available on PATH. Add `-DashboardPath "path/to/dashboard"` for the C/Python
packet checks.

[setup](BLE_INTEGRATION.md).
