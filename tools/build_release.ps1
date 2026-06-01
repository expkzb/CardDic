$ErrorActionPreference = "Stop"

$env:PLATFORMIO_SETTING_ENABLE_PROXY_STRICT_SSL = "false"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root ".pio\build\m5stack-cardputer"
$distDir = Join-Path $root "dist"
$pio = Join-Path $root ".venv\Scripts\pio.exe"
$python = Join-Path $root ".venv\Scripts\python.exe"
$esptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
$bootApp = Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"

if (!(Test-Path $pio)) {
    throw "PlatformIO not found at $pio. Create .venv and install platformio first."
}

& $pio run

New-Item -ItemType Directory -Force $distDir | Out-Null
Copy-Item (Join-Path $buildDir "firmware.bin") (Join-Path $distDir "carddic-cardputer-adv-app.bin") -Force

& $python $esptool --chip esp32s3 merge_bin `
    -o (Join-Path $distDir "carddic-cardputer-adv-merged.bin") `
    --flash_mode dio --flash_freq 80m --flash_size 8MB `
    0x0000 (Join-Path $buildDir "bootloader.bin") `
    0x8000 (Join-Path $buildDir "partitions.bin") `
    0xe000 $bootApp `
    0x10000 (Join-Path $buildDir "firmware.bin")
