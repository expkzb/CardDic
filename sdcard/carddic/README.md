# CardDic SD card files

Prepare a FAT32 microSD card with this directory:

```text
/carddic/
  index.bin
  entries.dat
  prefix.bin
  ipa.bff
```

Build the full MIT-licensed ECDICT dictionary:

```powershell
powershell -ExecutionPolicy Bypass -File tools\download_ecdict.ps1
```

Build dictionary files from your own UTF-8 CSV:

```powershell
python tools\build_dictionary.py sdcard\carddic\sample.csv -o sdcard\carddic
```

CSV columns:

```text
word,phonetic,translation,example_en,example_zh
```

Notes:

- `word` is indexed case-insensitively and must fit in 31 UTF-8 bytes.
- `translation` is required and is shown on the detail page.
- Duplicate words keep the first row and skip later rows.
- `font.vlw` is not required by current firmware.
- `ipa.bff` is required for phonetic symbols. It is generated from Segoe UI and covers ASCII, IPA extensions, Greek symbols, and Cyrillic `ә`.
- ECDICT data is downloaded from <https://github.com/skywind3000/ECDICT> under its MIT license.
- Commercial dictionaries are not included. Import only data you have the right to use.
