# CardDic

CardDic is an offline English-to-Chinese dictionary for M5Stack Cardputer-Adv.

It uses a microSD card for the dictionary database. The firmware uses the built-in M5GFX Simplified Chinese font so common Chinese characters do not depend on an SD font file.

## Hardware target

- M5Stack Cardputer-Adv
- ESP32-S3FN8
- 240x135 LCD
- Built-in keyboard
- FAT32 microSD card

## Build firmware

Install PlatformIO, then run:

```powershell
$env:PLATFORMIO_SETTING_ENABLE_PROXY_STRICT_SSL="false"
pio run
```

Upload:

```powershell
pio run -t upload
```

Build release binaries:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_release.ps1
```

Outputs:

- `dist/carddic-cardputer-adv-app.bin`: PlatformIO app image, flashed by `pio run -t upload`.
- `dist/carddic-cardputer-adv-merged.bin`: merged image for one-shot flashing at offset `0x0`.

Flash merged image with esptool:

```powershell
python %USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --baud 1500000 write_flash 0x0 dist\carddic-cardputer-adv-merged.bin
```

If PlatformIO package downloads fail with `HTTPClientError` or `SSLEOFError`, set `PLATFORMIO_SETTING_ENABLE_PROXY_STRICT_SSL=false`. This matches PlatformIO's documented proxy/SSL setting and is useful on networks with TLS interception or unstable mirrors.

## Build dictionary data

Download and build the full MIT-licensed ECDICT English-Chinese dictionary:

```powershell
powershell -ExecutionPolicy Bypass -File tools\download_ecdict.ps1
```

Or create a UTF-8 CSV with these columns:

```text
word,phonetic,translation,example_en,example_zh
```

Generate the SD card files:

```powershell
python tools\build_dictionary.py sdcard\carddic\sample.csv -o sdcard\carddic
```

When importing ECDICT, `tools\download_ecdict.ps1` also uses `sdcard\carddic\sample.csv` as an example-sentence supplement, so words such as `apple`, `book`, and `dictionary` include bilingual examples in the generated database.

Copy the generated dictionary files to a FAT32 microSD card:

```text
/carddic/index.bin
/carddic/entries.dat
/carddic/prefix.bin
/carddic/ipa.bff
```

`font.vlw` is no longer required. `ipa.bff` is a small runtime font for phonetic symbols, so IPA is rendered as-is instead of being replaced with ASCII approximations.

Rebuild the IPA font from a local TrueType font if needed:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_ipa_font.ps1
```

## Controls

- Type letters to search immediately.
- `;` moves upward through results or detail text.
- `.` moves downward through results or detail text.
- Hardware arrow HID keys are also accepted when the keyboard firmware reports them.
- Enter opens the selected entry.
- Del deletes one character in search.
- Ctrl/Fn clears search or returns from detail.

## Dictionary format

`index.bin` contains a fixed-size sorted index. `entries.dat` contains UTF-8 entry bodies with word, phonetic, Chinese translation, English example, and Chinese example translation.

The downloader uses ECDICT from <https://github.com/skywind3000/ECDICT> under its MIT license. ECDICT provides word, phonetic, and translation data; the firmware and file format also support example sentences when imported from a CSV that includes `example_en` and `example_zh`.
