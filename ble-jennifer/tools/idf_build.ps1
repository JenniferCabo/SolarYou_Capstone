param(
    [ValidateSet('esp32s3','esp32')][string]$Target = 'esp32s3',
    [string]$BuildDir = 'build-ble',
    [string]$Defaults = 'sdkconfig.defaults;sdkconfig.ble.defaults'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskTemp = Join-Path $taskRoot 'tmp'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP = $taskTemp
$env:TMP = $taskTemp
# Read the existing installation's environment without changing its selected IDE profile.
$envLines = & 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1' -e
foreach ($line in $envLines) {
    if ($line -match '^([A-Z_][A-Z0-9_]*)=(.*)$' -and $matches[1] -ne 'SYSTEM_PATH') {
        if ($matches[1] -eq 'PATH') { $env:PATH = $matches[2] + ';' + $env:PATH }
        else { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
$env:IDF_CCACHE_ENABLE = '0'
$env:IDF_PATH = 'C:\Espressif\v5.5.5\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v5.5.5\venv'
$env:ESP_ROM_ELF_DIR = 'C:\Espressif\tools\esp-rom-elfs\20241011'
$env:IDF_COMPONENT_CACHE_PATH = Join-Path $taskRoot '.component-cache'
Push-Location (Join-Path $taskRoot 'firmware\ESP_SolarYou')
try {
    & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" -u "$env:IDF_PATH\tools\idf.py" -B $BuildDir -D "CMAKE_NINJA_FORCE_RESPONSE_FILE=ON" -D "IDF_TARGET=$Target" -D "SDKCONFIG=$BuildDir/sdkconfig" -D "SDKCONFIG_DEFAULTS=$Defaults" build
    exit $LASTEXITCODE
} finally { Pop-Location }
