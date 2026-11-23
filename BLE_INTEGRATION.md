# BLE integration

I connected my BLE sender to the current main firmware so it can use Zack's
existing dashboard receiver. BLE now uses the readings already collected by
the program, including the existing power reading.

## What changed

- The main loop passes pitch, roll, power, and available controller targets to BLE.
- BLE reuses `power_update()` instead of polling the INA219 separately.
- Invalid readings or readings more than one second old are sent as `null`.
- The existing 1, 2, 5, and 10 Hz rate settings and message format are retained.
  Changing the BLE rate does not change the sensor or control-loop rate.
- I added an ESP-only mode to test USB and Bluetooth without sensors or motors.

The real-sensor startup checks, calibration, and motor-control settings are
unchanged. BLE starts after the required startup checks pass. If a required
sensor fails to initialize or calibrate, startup stops before telemetry begins;
check the USB startup log.

## Build

Use ESP-IDF 5.5.5 with the ESP32-S3 target. From an ESP-IDF PowerShell terminal
at the repository root:

| Mode | Command | Readings / motors |
| --- | --- | --- |
| Sensor/dashboard | `.\tools\build_ble.ps1 -Mode SensorDashboard` | Real sensors; motors off |
| Full control | `.\tools\build_ble.ps1 -Mode FullControl` | Real sensors; motors enabled |
| ESP-only test | `.\tools\build_ble.ps1 -Mode EspOnly` | Generated readings; no sensor or motor initialization |

Each mode has its own build folder under `ESP_SolarYou/`. Use that folder's
generated `flash_args` when flashing. The existing board configuration uses
16 MB flash.

For a real-sensor USB-only build, add `-UsbOnly`. ESP-only mode includes both
USB output and BLE and does not use that flag.

On macOS/Linux, the equivalent sensor/dashboard build from `ESP_SolarYou/` is:

```bash
idf.py -B build-ble-sensordashboard -D IDF_TARGET=esp32s3 \
  -D SDKCONFIG=build-ble-sensordashboard/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ble.defaults' build
```

## Connect Zack's dashboard

The dashboard supports Windows and Linux. Use Python 3 and, for BLE, a working
Bluetooth adapter with Bluetooth turned on. Linux uses the BlueZ Bluetooth
service. The firmware must already be flashed onto the ESP before connecting.

### 1. Get the dashboard

Use the `dashboard/` folder from the team's `zack` branch. Keep it in a separate
checkout from the integrated firmware so switching branches doesn't replace
the firmware files.

If the dashboard isn't already downloaded, these commands work in Windows
PowerShell and a Linux terminal:

```text
git clone --branch zack --single-branch https://github.com/manuelGarciaAcha/SolarYou_Capstone.git SolarYou-dashboard
cd SolarYou-dashboard/dashboard
```

If a dashboard checkout already exists, open a terminal in its `dashboard/`
folder instead. That is the folder containing `app.py` and `requirements.txt`.

### 2. Set up Python

Run the commands for the computer being used. They create a local Python
environment and install Zack's dashboard dependencies. An existing
`config.json` is kept; a new one is copied from the example only if needed.

**Windows — PowerShell, from the dashboard folder:**

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
if (-not (Test-Path config.json)) { Copy-Item config.example.json config.json }
```

If `py` isn't available, use `python -m venv .venv` for the first command.

**Linux — terminal, from the dashboard folder:**

```bash
python3 -m venv .venv
./.venv/bin/python -m pip install -r requirements.txt
if [ ! -f config.json ]; then cp config.example.json config.json; fi
```

The commands below call that environment's Python directly, so there is no
separate activation step. Run them from the same dashboard folder.

### 3. Choose the connection

Run one dashboard at a time. Stop the current dashboard with `Ctrl+C` before
switching to another connection or test.

**Real-sensor firmware over BLE**

Use the integrated sensor/dashboard or full-control firmware with BLE enabled.
In `config.json`, check that `ble_name` is `SolarYou-ASSTP` and the telemetry
and rate UUIDs match `config.example.json`
([protocol](ble-jennifer/BLE_PROTOCOL.md)). Sensor startup must succeed before
this firmware starts advertising. No dashboard patch is needed.

Windows:

```powershell
.\.venv\Scripts\python.exe app.py --source ble
```

Linux:

```bash
./.venv/bin/python app.py --source ble
```

**Basic ESP-only firmware over BLE**

Use this when testing without sensors or motors. Replace the example firmware
path below with the location of the integrated firmware checkout. The file
`tools/run_esp_only_dashboard.py` is in that checkout. Keep the terminal in the
dashboard folder; `--dashboard .` means to use that folder's dashboard.

Windows:

```powershell
.\.venv\Scripts\python.exe "C:\path\to\firmware\tools\run_esp_only_dashboard.py" --dashboard .
```

Linux:

```bash
./.venv/bin/python "/path/to/firmware/tools/run_esp_only_dashboard.py" --dashboard .
```

The launcher selects `SolarYou-ESP-TEST`, labels the page **GENERATED READINGS**,
and disables automatic CSV logging. It keeps the dashboard's `config.json`
unchanged. The ESP generates the readings and sends them over real Bluetooth.

**Basic ESP-only firmware over USB**

Connect the ESP to the computer with a USB data cable. These commands select
automatic serial-port detection and disable automatic CSV logging.

Windows:

```powershell
.\.venv\Scripts\python.exe app.py --source serial --serial-port auto --no-log
```

Linux:

```bash
./.venv/bin/python app.py --source serial --serial-port auto --no-log
```

The USB test uses Zack's unchanged serial receiver. Its readings are generated,
although this command doesn't add the BLE test launcher's page banner. USB and
BLE ESP-only packets include `"simulated":true`.

### 4. Open the dashboard

For any of the connections above, open `http://localhost:5001` in a browser on
the computer running the dashboard. Watch the terminal for the connection
status and the first reading. For a RESET test, leave the dashboard running,
press the ESP's physical RESET/RST button once, and check whether it reconnects
and readings resume.

[Test results](BLE_VALIDATION.md)

Based on `main` at `cb263b8` and the dashboard on `zack` at `79151e6`.
Use this integrated source instead of reapplying the older
`ble-jennifer/firmware.patch`.
