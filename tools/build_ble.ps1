param(
    [ValidateSet('SensorDashboard', 'FullControl', 'EspOnly')][string]$Mode = 'SensorDashboard',
    [switch]$UsbOnly,
    [string]$IdfProfile
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if ($IdfProfile) {
    foreach ($line in (& $IdfProfile -e 6>&1)) {
        if ($line -match '^([A-Z_][A-Z0-9_]*)=(.*)$' -and $Matches[1] -ne 'SYSTEM_PATH') {
            if ($Matches[1] -eq 'PATH') { $env:PATH = $Matches[2] + ';' + $env:PATH }
            else { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
        }
    }
}
if (-not $env:IDF_PATH -or -not $env:IDF_PYTHON_ENV_PATH) {
    throw 'Run in an ESP-IDF 5.5.5 PowerShell terminal, or pass -IdfProfile with your installed environment profile.'
}
$taskTemp = Join-Path $taskRoot 'artifacts/tmp'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP = $taskTemp
$env:TMP = $taskTemp
$env:IDF_CCACHE_ENABLE = '0'
$env:IDF_COMPONENT_CACHE_PATH = Join-Path $taskRoot '.component-cache'
$defaults = @('sdkconfig.defaults')
if ($Mode -eq 'EspOnly' -and $UsbOnly) { throw 'ESP-only Bluetooth testing requires BLE; do not use -UsbOnly.' }
$linkName = 'ble'
if ($UsbOnly) { $linkName = 'usb' }
else { $defaults += 'sdkconfig.ble.defaults' }
if ($Mode -eq 'FullControl') { $defaults += 'sdkconfig.full-control.defaults' }
if ($Mode -eq 'EspOnly') { $defaults += 'sdkconfig.esp-only.defaults' }
$buildName = "build-$linkName-" + $Mode.ToLowerInvariant()
$python = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
Push-Location (Join-Path $taskRoot 'ESP_SolarYou')
try {
    & $python -u (Join-Path $env:IDF_PATH 'tools/idf.py') -B $buildName `
        -D 'CMAKE_NINJA_FORCE_RESPONSE_FILE=ON' -D 'IDF_TARGET=esp32s3' `
        -D "SDKCONFIG=$buildName/sdkconfig" -D "SDKCONFIG_DEFAULTS=$($defaults -join ';')" build
    if ($LASTEXITCODE -ne 0) { throw "Firmware build failed ($LASTEXITCODE)" }
} finally { Pop-Location }
