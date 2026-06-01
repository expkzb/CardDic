param(
    [string]$FontPath = "C:\Windows\Fonts\segoeui.ttf",
    [string]$OutputPath = "sdcard\carddic\ipa.bff"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $FontPath)) {
    throw "Font not found: $FontPath"
}

$outDir = Split-Path -Parent $OutputPath
if ($outDir) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

npx.cmd --yes lv_font_conv@1.5.3 `
    --size 16 `
    --bpp 1 `
    --format bin `
    --font $FontPath `
    -r 0x20-0x7E,0xA0-0xFF,0x250-0x2FF,0x370-0x52F `
    -o $OutputPath

Get-Item $OutputPath
