$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root "sdcard\carddic"
$csvPath = Join-Path $outDir "ecdict.csv"
$licensePath = Join-Path $outDir "ECDICT_LICENSE.txt"
$url = "https://raw.githubusercontent.com/skywind3000/ECDICT/master/ecdict.csv"
$licenseUrl = "https://raw.githubusercontent.com/skywind3000/ECDICT/master/LICENSE"

New-Item -ItemType Directory -Force $outDir | Out-Null
curl.exe -L $url -o $csvPath
curl.exe -L $licenseUrl -o $licensePath

python (Join-Path $root "tools\build_dictionary.py") $csvPath -o $outDir --examples (Join-Path $outDir "sample.csv")
