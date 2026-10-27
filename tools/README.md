# tools/

## build_release.bat / build_release.sh / build_release.py

Собирают nDash под все поддерживаемые платы и упаковывают каждую в
`release/v<VERSION>/nDash-v<VERSION>-<ПЛАТА>/` (и в `.zip` рядом).

### Версия

Формат `1.<YY>.<MMDD>` - как в других проектах (roundGauge: `0.<YY>.<MMDD>`): мажорное число 1, затем год из двух
цифр и месяц с днём дня сборки, например `1.26.1022` - 22 октября 2026. Скрипт сам записывает версию в
[`version.txt`](../version.txt) при каждой релизной сборке (руками менять не нужно), оттуда её берут прошивка (экран, веб,
`/api/status`) и имя пакета. Две сборки в один день получают одну версию, их различает коммит в `manifest.json`.

### Что в пакете платы

| Файл | Для чего |
|---|---|
| `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`, `nDash.bin`, `www.bin`, `media.bin` | образы разделов (смещения в `manifest.json`) |
| `install.bat` / `install.sh` | **полная прошивка** всего: новая плата или смена таблицы разделов. NVS (настройки, раскладка, таблицы CAN и стенда) сохраняется; картинки, загруженные через веб, заменяются встроенными |
| `update.bat` / `update.sh` | **обновление**: только приложение и веб-интерфейс (плюс `otadata`, чтобы плата загрузилась из первого слота). Всё сохранённое на плате остаётся |
| `ota/nDash-v<версия>-<плата>.bin`, `ota/www-v<версия>.bin` | те же образы для страницы `/ota` (обновление по Wi-Fi без кабеля) |
| `esptool.exe` | автономный, из `tools/vendor/`: Python на машине прошивающего не нужен |
| `manifest.json`, `SHA256SUMS.txt`, `README.md` | версия, коммит git (`+dirty` - были неподтверждённые изменения), время сборки, консоль, размеры, sha256; инструкция |

`install.sh` / `update.sh` нужен только Python 3 (пакет `esptool` ставится сам при первом запуске).

### Как собирается плата

Каждая плата - своя папка `build_release/<плата>/` и свой `sdkconfig` внутри неё, поэтому обычная сборка
`build/` и общий `sdkconfig` проекта не затрагиваются, а платы не пересобирают друг друга. Параметры релиза:
`NDASH_UI_SHOW_FPS=0` (без счётчика fps), `NDASH_DEMO=0` (настоящие данные CAN), `NDASH_LAYOUT_BUILTIN=0` (используется
сохранённая раскладка). Консоль лога: UART у `WAVESHARE_S3_LCD7` (там CAN делит ножки с USB, `CONSOLE` в скрипте),
у остальных родной USB-Serial-JTAG.

После сборки скрипт проверяет, что образы приложения, веба и картинок влезают в свои разделы по `partitions.csv` и
лежат в `flasher_args.json` по тем же смещениям.

### Использование

Нужна активная среда ESP-IDF (скрипты `.bat`/`.sh` пытаются найти её сами):

```bat
:: Windows (cmd.exe): платы Waveshare (проверенные на железе); JC8048W550C и Sunton - только явно через --board
tools\build_release.bat
:: одна плата, без zip
tools\build_release.bat --board WAVESHARE_S3_LCD7 --no-zip
```

```powershell
# PowerShell
. $env:USERPROFILE\esp\v6.0\esp-idf\export.ps1
python tools\build_release.py
```

```bash
# Linux / macOS
. $HOME/esp/esp-idf/export.sh
./tools/build_release.sh
```

> Под Git Bash/MSYS `export.sh` самого ESP-IDF не работает ("MSys/Mingw is not supported"): используйте
> `build_release.bat` в `cmd.exe` или PowerShell с `export.ps1`.

Параметры: `--board ПЛАТА` (можно несколько раз), `--skip-build` (только упаковать уже собранное в `build_release/`),
`--no-zip`, `--clean` (удалить каталог сборки платы перед сборкой).

### Требование: tools/vendor/esptool.exe

Автономный Windows-бинарник `esptool.exe` в `tools/vendor/esptool.exe` копируется в каждый пакет. Без него пакет не
собирается (принципиально без python-фолбэка для `.bat`). Взять можно из пакета плат ESP32 для Arduino IDE:
`%USERPROFILE%\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\<версия>\esptool.exe` (настоящий автономный
бинарник; `esptool.exe`, который ставит pip в окружение ESP-IDF, без этого окружения не работает).

## webtest/

Мок-сервер платы для отладки веб-интерфейса в обычном браузере без железа - см. [webtest/README.md](webtest/README.md).
