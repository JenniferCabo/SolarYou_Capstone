# SolarYou sensor tests (ESP-IDF 5.5.5)

This is the sensor-test part of SolarYou that I've been working on. For now, I'm checking the sensors and the calculations by themselves.

What's in here right now:

- four light sensors that calculate pitch and roll tracking errors
- a BNO085 that gives pitch and roll angles centered around the starting position
- a INA219 that gives voltage, current, and power
- checks for bad or old readings, plus health states and fault flags
- C-compatible functions Manuel can call from the team's ESP-IDF code
- a draft JSON message Zack can use while we agree on the dashboard data

This uses ESP-IDF 5.5.5 with Arduino Core 3.3.11. I still need the Arduino
side for the Adafruit sensor libraries, but the public sensor functions can
be called from C code.


## Hardware

I'm specifically targeting the Heltec WiFi LoRa 32 V4.3 with the ESP32-S3R2 and 16 MB of flash. PSRAM is turned off because these tests don't need it.

| Connection | GPIO or address |
| --- | --- |
| I2C SDA | GPIO47 |
| I2C SCL | GPIO48 |
| BNO085 | `0x4A` |
| INA219 | `0x40` with A0/A1 open |
| Light top-left | GPIO3 |
| Light top-right | GPIO4 |
| Light bottom-left | GPIO5 |
| Light bottom-right | GPIO6 |

GPIO3 is still a maybe because it affects how the esp starts up. I still need to try a cold boot with that light sensor dark, bright, and disconnected. If the board has trouble booting, the fallback is an external ADC.

## Build and run on the board

First, install and activate ESP-IDF 5.5.5. Then open a terminal in this folder and run:

```powershell
idf.py --version
.\scripts\fetch_arduino_libraries.ps1
idf.py set-target esp32s3
idf.py build
```

To put the firmware on the board, replace `COM_PORT` with the port your computer shows (for example, `COM10` if that is the current port):

```powershell
idf.py -p COM_PORT flash monitor
```

While it starts, keep the BNO085 still and level and give all four light sensors about the same amount of light. That is when the code sets its neutral reference. Press `Ctrl+]` to leave the monitor.

The generated build folders and downloaded libraries are left out of Git by `.gitignore`.

## Run the simulations

There are four Wokwi options: one for each sensor group and one with everything together. A single sensor build only starts that sensor path. The combined one runs all three. They use the same public functions and shared calculations.

| Mode | Build command | Diagram |
| --- | --- | --- |
| All sensors | `.\scripts\build_wokwi.ps1` | Root `diagram.json` |
| Light only | `.\scripts\build_wokwi.ps1 -Mode light` | `simulations/light_only/diagram.json` |
| IMU only | `.\scripts\build_wokwi.ps1 -Mode imu` | `simulations/imu_only/diagram.json` |
| Power only | `.\scripts\build_wokwi.ps1 -Mode power` | `simulations/power_only/diagram.json` |

```powershell
.\scripts\build_wokwi.ps1
```

After the build finishes, open the matching diagram and choose Wokwi: Start Simulator. Check that the terminal shows the right `VALIDATION MODE` before you start moving the controls.

The code checks the sensors every 100 ms. It normally prints a short update once a second, plus an update right away if a sensor's status changes. I
left the full nested JSON off in the terminal so it doesn't scroll too fast. If you need to see it, change `SY_CONSOLE_PRINT_JSON` to `1` in
`component_config.h` and rebuild.

Wokwi was used only to check the code's behavior. It DOES NOT prove the real sensors, wiring, or power setup work.

The [simulation guide](docs/SIMULATION_GUIDE.md) shows what to adjust and what you should see.

## Public sensor functions

The functions Manuel would call are in `components/sensor_service/include/sensor_api.h`:

```c
bool sensor_adapter_init(void);
bool sensor_adapter_capture_neutral(void);
bool sensor_adapter_read(sensor_snapshot_t *snapshot);
void sensor_adapter_clear_latched_faults(void);
```

- `sensor_adapter_init()` starts the sensors.
- `sensor_adapter_capture_neutral()` saves the current IMU position and light balance as zero.
- `sensor_adapter_read()` gives back one snapshot with the IMU, light, power, and their validity and health information.
- `sensor_adapter_clear_latched_faults()` clears old fault history but does not hide a fault that is still happening.

That snapshot can be turned into JSON with:

```c
sensor_json_result_t sensor_snapshot_to_json(
    const sensor_snapshot_t *snapshot,
    char *output,
    size_t output_capacity,
    size_t *bytes_written);
```

This function only creates the JSON text. It does not send it anywhere, the full program will need a separate Wi-Fi/telemetry part for that.

The JSON field names are still a draft. The [snapshot contract](docs/SNAPSHOT_CONTRACT.md) explains the current shape, and the [example JSON](docs/draft_sensor_message.json) shows one message.

## Team integration

The idea is basically for Manuel to add this sensor component to the main ESP-IDF project and call `sensor_adapter_read()` from the team's sensor task. The
resulting snapshot can go to the control code and to whatever sends data out.

Zack can use a JSON copy once we agree on the final field names and how to send it. The serial monitor printout is just for testing, it is not the dashboard connection.

## Current status

The physical target and Wokwi firmware both build. The light, IMU, power, fault, snapshot, and JSON logic are in place. The C header and JSON behavior
also have desktop tests.

What I still need to check or finish:

- finish the physical BNO085, light-array, and INA219 test records
- repeat the GPIO3 cold-boot test
- adjust the light thresholds using real measurements
- compare INA219 readings with a multimeter and the panel ratings
- test resets, disconnects, reconnects, and longer runs
- agree on the final JSON field names with the team
- help fix any sensor side API or data format issues that come up during Manuel and Zack’s integration

The [fault-code guide](docs/FAULT_CODES.md) explains the current states and fault values.
