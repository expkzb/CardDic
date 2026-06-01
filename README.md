# CardDic

CardDic 是一个给 M5Stack Cardputer-Adv 使用的离线英译中词典。

它把词库放在 microSD 卡上，固件只负责搜索、显示和键盘交互。中文使用 M5GFX 内置简体中文字库，音标使用 SD 卡上的小字体文件 `ipa.bff`，所以可以显示原始 IPA 音标，而不是用 ASCII 符号替代。

## 功能

- 离线英译中查询
- 键盘输入后延迟搜索，减少输入卡顿
- 前缀候选列表
- 详情页显示单词、音标、中文释义、英文例句、中文例句
- 支持长内容分页滚动
- 支持 ECDICT 词库导入
- 支持自备 CSV 词库导入
- 支持 M5Launcher 安装

## 硬件

目标设备是 M5Stack Cardputer-Adv：

- ESP32-S3FN8
- 240x135 LCD
- 56 键键盘
- microSD 卡

旧版 Cardputer 没有专门测试。

## 快速使用

### 0. 下载固件

如果你只是想安装使用，不需要自己编译。直接下载仓库里的：

```text
dist/carddic-cardputer-adv-app.bin
```

这个文件用于 M5Launcher 安装。

如果你想从 `0x0` 整体刷写，可以使用：

```text
dist/carddic-cardputer-adv-merged.bin
```

### 1. 准备 SD 卡

把 microSD 卡格式化为 FAT32，然后准备这个目录：

```text
/carddic/
  index.bin
  entries.dat
  prefix.bin
  ipa.bff
```

其中：

- `index.bin` 是词库索引
- `entries.dat` 是词条正文
- `prefix.bin` 是前缀加速索引
- `ipa.bff` 是音标字体

项目里已经提供 `sdcard/carddic/ipa.bff`。词库文件需要按下面方式生成。

### 2. 生成词库

推荐直接生成 ECDICT 词库：

```powershell
powershell -ExecutionPolicy Bypass -File tools\download_ecdict.ps1
```

生成完成后，把下面文件复制到 SD 卡的 `/carddic/` 目录：

```text
sdcard/carddic/index.bin
sdcard/carddic/entries.dat
sdcard/carddic/prefix.bin
sdcard/carddic/ipa.bff
```

也可以用自己的 UTF-8 CSV：

```powershell
python tools\build_dictionary.py your.csv -o sdcard\carddic
```

CSV 表头必须是：

```text
word,phonetic,translation,example_en,example_zh
```

字段说明：

- `word`: 英文单词，索引时会转小写，最长 31 字节
- `phonetic`: 音标，可为空
- `translation`: 中文释义，必填
- `example_en`: 英文例句，可为空
- `example_zh`: 中文例句翻译，可为空

重复单词只保留第一条。

### 3. 安装固件

如果使用 M5Launcher，安装这个文件：

```text
dist/carddic-cardputer-adv-app.bin
```

注意：当前固件内置较大的中文字体，app 大小约 800KB。M5Launcher 分区需要大于这个大小，通常需要 `0x0d0000` 或更大的 app 分区。如果提示 `need 0x080000`、`need 0x0b0000`、`need 0x0c0000`，本质是当前 app 分区不够，不是 SD 卡文件问题。

如果不用 M5Launcher，可以刷 merged 镜像：

```powershell
python %USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --baud 1500000 write_flash 0x0 dist\carddic-cardputer-adv-merged.bin
```

## 按键

搜索页：

- 直接输入英文字母开始搜索
- `Del` 删除一个字符
- `Esc` / `` ` `` / `Ctrl` / `Fn` 清空输入
- `;` 选择上一个候选
- `.` 选择下一个候选
- `Enter` 打开详情页

详情页：

- `;` 向上滚动
- `.` 向下滚动
- `Esc` / `` ` `` / `Del` / `Ctrl` / `Fn` 返回搜索页

如果设备固件正确上报 HID 上下方向键，上下方向键也会被识别；但不同 Cardputer 键盘固件的映射可能不一致，所以 `;` 和 `.` 是稳定备用键。

## 从源码构建

安装 PlatformIO 后执行：

```powershell
$env:PLATFORMIO_SETTING_ENABLE_PROXY_STRICT_SSL="false"
pio run
```

如果 PlatformIO 下载依赖时遇到 `HTTPClientError`、`SSLEOFError` 或代理 SSL 问题，可以保留上面的环境变量。

构建发布文件：

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_release.ps1
```

输出：

- `dist/carddic-cardputer-adv-app.bin`: M5Launcher / app 分区使用
- `dist/carddic-cardputer-adv-merged.bin`: 从 `0x0` 一次性刷写使用

## 重新生成音标字体

通常不需要重新生成。项目已提供：

```text
sdcard/carddic/ipa.bff
```

如果要重新生成，需要本机有 Node.js 和 `npx`：

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_ipa_font.ps1
```

默认使用 Windows 字体：

```text
C:\Windows\Fonts\segoeui.ttf
```

生成的 `ipa.bff` 覆盖 ASCII、IPA 扩展、希腊符号和西里尔 `ә`。

## 文件说明

```text
src/main.cpp                 固件主程序
platformio.ini               PlatformIO 配置
tools/build_dictionary.py    CSV 转词库工具
tools/download_ecdict.ps1    下载并生成 ECDICT 词库
tools/build_ipa_font.ps1     生成音标字体
tools/build_release.ps1      构建 app.bin 和 merged.bin
sdcard/carddic/sample.csv    小样例词库
sdcard/carddic/ipa.bff       音标字体
```

## 词库和版权

项目不内置完整商业词典。

`tools/download_ecdict.ps1` 会下载 ECDICT：<https://github.com/skywind3000/ECDICT>。ECDICT 使用 MIT License。项目里只保留导入格式、样例数据和生成工具。

导入自己的词库时，请确认你有权使用和分发对应数据。

## 常见问题

### 启动提示缺少文件

检查 SD 卡是否是 FAT32，并确认目录是：

```text
/carddic/index.bin
/carddic/entries.dat
/carddic/prefix.bin
/carddic/ipa.bff
```

路径必须在 SD 卡根目录下，不能是 `/sdcard/carddic/`。

### M5Launcher 提示 need 0x080000 / 0x0b0000 / 0x0c0000

这是 app 分区大小不够。当前固件为了完整中文显示内置了中文字体，所以体积大于 512KB。需要在 M5Launcher 中选择更大的 app 分区，或重新分区。

### 音标显示异常

确认 SD 卡上有新版：

```text
/carddic/ipa.bff
```

如果只更新了固件但没有更新 SD 卡字体文件，音标可能显示不清或缺字。

### 中文缺字

当前中文使用 M5GFX 内置 `efontCN_16`，常用简体中文应能显示。如果仍有缺字，优先检查你使用的是最新固件，而不是旧版 `font.vlw` 构建。
