#!/usr/bin/env python3
"""
Builds nDash for one or all supported boards and packages every build into
release/v<VERSION>/ (VERSION = 1.<YY>.<MMDD> of the build day, written to version.txt)
nDash-v<VERSION>-<BOARD>/ (and a .zip of it). A package holds:

  - the firmware files (bootloader, partition table, otadata, the app, the web interface "www",
    the pictures "media") and install.bat / install.sh, which flash all of them: a new board or
    a change of the partition table. Settings (NVS) stay, uploaded pictures are replaced by the
    built-in ones;
  - update.bat / update.sh: only the app and the web interface (the board keeps its saved
    layout, CAN table, settings and uploaded pictures);
  - ota/: the same app and web images for the /ota page of the dash (no cable needed);
  - manifest.json (version, git commit, build time, console, sizes, sha256), SHA256SUMS.txt, README.md.

Each board is built into its own directory build_release/<board>/ with its own sdkconfig, so the
release never touches (or inherits settings from) the ordinary development build/ directory and
the boards do not rebuild each other. No source file is edited to pick the board.

This script assumes an ESP-IDF environment is already active (idf.py on PATH). It is normally
started through build_release.bat / build_release.sh, which take care of that.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import time
import zipfile
from datetime import datetime
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_DIR = TOOLS_DIR.parent
RELEASE_BUILD_ROOT = PROJECT_DIR / "build_release"

# Vendored standalone esptool.exe (see tools/README.md): copied into every package, install.bat
# and update.bat call it directly, no Python needed on the machine that flashes.
VENDOR_ESPTOOL_EXE = TOOLS_DIR / "vendor" / "esptool.exe"

# Boards that were tried on real hardware: the default release is built for these only.
BOARDS = [
    "WAVESHARE_S3_LCD7",
    "WAVESHARE_S3_LCD5",
]

# Not tried on hardware (the SD pins are not checked against the schematics): built only when asked with --board.
UNTESTED_BOARDS = [
    "JC8048W550C",
    "Sunton_S3_LCD7",
]

# Where the log goes. The Waveshare 7" shares GPIO19/20 between CAN and the native USB, so the
# log of that board is on its UART bridge; the others use the native USB-Serial-JTAG.
CONSOLE = {"WAVESHARE_S3_LCD7": "uart"}

# What a firmware file is called in the package, by its name in the build.
OTA_APP = "nDash.bin"
OTA_DATA = "ota_data_initial.bin"
WWW = "www.bin"
MEDIA = "media.bin"

INSTALL_BAT = """@echo off
REM nDash {version} / {board}: flashes EVERYTHING (bootloader, partition table, app, web interface
REM and the built-in pictures). The settings, layout and CAN table in NVS stay; the pictures
REM uploaded from the web are replaced by the built-in ones. To update only the app and the web
REM interface use update.bat.
REM Self-contained: esptool.exe is next to this file, no Python needed.
setlocal
cd /d "%~dp0"
set PORTARG=
if not "%~1"=="" set PORTARG=-p %~1

esptool.exe --chip {chip} %PORTARG% --baud 460800 ^
  --before {before} --after {after} ^
  write-flash {write_flash_args} ^
{files_bat}

pause
"""

UPDATE_BAT = """@echo off
REM nDash {version} / {board}: flashes only the app and the web interface. The saved layout, CAN
REM table, settings and the uploaded pictures stay. otadata is written too: it makes the board
REM start the app from the first slot, where this file puts it.
REM Self-contained: esptool.exe is next to this file, no Python needed.
setlocal
cd /d "%~dp0"
set PORTARG=
if not "%~1"=="" set PORTARG=-p %~1

esptool.exe --chip {chip} %PORTARG% --baud 460800 ^
  --before {before} --after {after} ^
  write-flash {write_flash_args} ^
{update_bat}

pause
"""

# The shell versions need Python 3; the esptool package is installed with pip on the first run.
SH_HEAD = """#!/usr/bin/env bash
# nDash {version} / {board}: {what}
set -euo pipefail

PORT="${{1:-}}"
if [ -z "$PORT" ]; then
  echo "Usage: $0 <serial-port>   (e.g. $0 /dev/ttyUSB0)"
  exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" && pwd)"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found - install it first (apt install python3 python3-pip / brew install python3)." >&2
  exit 1
fi
if ! python3 -c "import esptool" >/dev/null 2>&1; then
  echo "esptool not found - installing it with pip ..."
  python3 -m pip install --user esptool
fi

python3 -m esptool --chip {chip} -p "$PORT" -b 460800 \\
  --before {before} --after {after} \\
  write-flash {write_flash_args} \\
{files_sh}
"""

README = """# nDash {version} - {board}

Прошивка приборной панели nDash для платы **{board}** (консоль: {console}). Собрана {built} из
коммита `{commit}`.

## Что в папке

| Файл | Для чего |
|---|---|
| `install.bat` / `install.sh` | **Полная прошивка**: загрузчик, таблица разделов, приложение, веб-интерфейс и встроенные картинки. Для новой платы или после смены таблицы разделов. Настройки, раскладка и таблица CAN (NVS) сохраняются, картинки, загруженные через веб, заменяются встроенными |
| `update.bat` / `update.sh` | **Обновление**: только приложение и веб-интерфейс. Раскладка, таблица CAN, настройки и загруженные картинки остаются |
| `ota/` | Те же образы для обновления **без кабеля**: страница `http://192.168.4.1/ota` (точка доступа включается долгим нажатием BOOT) |
| `manifest.json`, `SHA256SUMS.txt` | Версия, коммит, размеры и контрольные суммы |

## Прошивка кабелем

Подключите плату по USB и запустите `install.bat` (или `update.bat`) двойным щелчком: `esptool.exe` лежит рядом,
ставить ничего не нужно. Порт находится сам; если нет - `install.bat COM5`. На Linux/macOS: `./install.sh /dev/ttyUSB0`
(нужен Python 3, пакет `esptool` поставится сам).

## Обновление по Wi-Fi

1. Удерживайте кнопку BOOT 2 секунды: приборка поднимет точку доступа, имя и пароль появятся на экране.
2. Подключитесь и откройте `http://192.168.4.1/ota`.
3. Загрузите `ota/{app_name}` в блок приложения, затем `ota/{www_name}` в блок веб-интерфейса.
   Приложение проверяется при следующем запуске: если новая версия не стартует, плата сама вернётся к прежней.

Образ приложения собран именно для платы **{board}**: прошивка от другой платы включит не те выводы.
"""


def run(cmd, **kwargs):
    print(f"$ {' '.join(map(str, cmd))}")
    # On Windows idf.py has no registered Win32 executable - it only runs through the shell's own
    # file-association/PATHEXT resolution, so CreateProcess cannot launch it directly. The arguments
    # are fixed literals and paths of this repository, never user input.
    subprocess.run(cmd, check=True, cwd=PROJECT_DIR, shell=(sys.platform == "win32"), **kwargs)


def release_version():
    """1.<YY>.<MMDD> of the build day, like the other projects (roundGauge: 0.<YY>.<MMDD>). The major
    number is 1; the date part is written to version.txt on every release build, so the firmware
    (esp_app_get_description()->version, the web) and the package carry the same version."""
    now = datetime.now()
    version = f"1.{now:%y}.{now:%m%d}"
    (PROJECT_DIR / "version.txt").write_text(f"{version}\n")
    return version


def git_commit():
    try:
        out = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=PROJECT_DIR, capture_output=True, text=True, check=True)
        dirty = subprocess.run(["git", "status", "--porcelain"], cwd=PROJECT_DIR, capture_output=True, text=True, check=True).stdout.strip()
        return out.stdout.strip() + ("+dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def copy_retry(src, dst, retries=6, delay_s=0.5):
    # Explorer previews, antivirus scans and indexers hold a file that was just written for a moment:
    # retry instead of aborting the whole release over it.
    for attempt in range(1, retries + 1):
        try:
            shutil.copy2(src, dst)
            return
        except (PermissionError, OSError) as e:
            if attempt == retries:
                raise SystemExit(f"Could not write {dst} - still locked after {retries} attempts ({e}). "
                                 f"Close whatever has it open and run again.")
            print(f"{dst} is locked ({e}), retrying in {delay_s}s ...")
            time.sleep(delay_s)


def partition_sizes():
    """name -> (offset, size) from partitions.csv."""
    out = {}
    for line in (PROJECT_DIR / "partitions.csv").read_text().splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        f = [x.strip() for x in line.split(",")]
        out[f[0]] = (int(f[3], 16), int(f[4], 16))
    return out


def check_sizes(build_dir, flasher_args):
    """The images must fit their partitions (the linker checks the app, the others are not checked)."""
    parts = partition_sizes()
    files = {Path(p).name: off for off, p in flasher_args["flash_files"].items()}
    for name, part in ((OTA_APP, "ota_0"), (WWW, "www"), (MEDIA, "media")):
        if name not in files:
            raise SystemExit(f"{name} is not among the flash files of the build: check CMakeLists.txt and partitions.csv")
        offset, size = parts[part]
        if int(files[name], 16) != offset:
            raise SystemExit(f"{name} is flashed at {files[name]} but partition {part} is at {hex(offset)}")
        path = next(build_dir / p for p in flasher_args["flash_files"].values() if Path(p).name == name)
        if path.stat().st_size > size:
            raise SystemExit(f"{name} ({path.stat().st_size} bytes) does not fit the partition {part} ({size} bytes)")


def asm_lines(pairs, bat):
    """[(offset, file name)] -> the continuation lines of the esptool command for .bat or .sh."""
    out = []
    for i, (off, name) in enumerate(pairs):
        last = i == len(pairs) - 1
        cont = "" if last else (" ^" if bat else " \\")  # line continuation (no backslash allowed inside an f-string before Python 3.12)
        out.append(f'  {off} "%~dp0{name}"{cont}' if bat else f'  {off} "$SCRIPT_DIR/{name}"{cont}')
    return "\n".join(out) + "\n"


def write_text(path, text, executable=False):
    path.write_text(text, newline="\r\n" if path.suffix == ".bat" else "\n")  # .bat needs CRLF, the rest LF
    if executable:
        path.chmod(0o755)


def package(board, version, build_dir, make_zip):
    if not VENDOR_ESPTOOL_EXE.is_file():
        raise SystemExit(f"Missing {VENDOR_ESPTOOL_EXE}: releases are packed with a self-contained esptool.exe "
                         f"(see tools/README.md).")
    flasher_args = json.loads((build_dir / "flasher_args.json").read_text())
    check_sizes(build_dir, flasher_args)

    settings = flasher_args["extra_esptool_args"]
    common = {"version": version, "board": board, "chip": settings["chip"], "before": settings["before"],
              "after": settings["after"], "write_flash_args": " ".join(flasher_args["write_flash_args"])}

    out_dir = PROJECT_DIR / "release" / f"v{version}" / f"nDash-v{version}-{board}"
    out_dir.mkdir(parents=True, exist_ok=True)

    # the firmware files, flat in the package folder
    pairs = sorted(((off, Path(p).name) for off, p in flasher_args["flash_files"].items()), key=lambda x: int(x[0], 16))
    for off, rel in flasher_args["flash_files"].items():
        copy_retry(build_dir / rel, out_dir / Path(rel).name)
    update_pairs = [(o, n) for o, n in pairs if n in (OTA_DATA, OTA_APP, WWW)]

    copy_retry(VENDOR_ESPTOOL_EXE, out_dir / "esptool.exe")
    write_text(out_dir / "install.bat", INSTALL_BAT.format(**common, files_bat=asm_lines(pairs, True)))
    write_text(out_dir / "update.bat", UPDATE_BAT.format(**common, update_bat=asm_lines(update_pairs, True)))
    write_text(out_dir / "install.sh", SH_HEAD.format(**common, what="flashes everything (bootloader, partition table, app, web, pictures).",
                                                       files_sh=asm_lines(pairs, False)), executable=True)
    write_text(out_dir / "update.sh", SH_HEAD.format(**common, what="flashes only the app and the web interface.",
                                                      files_sh=asm_lines(update_pairs, False)), executable=True)

    # the same images for the /ota page
    ota_dir = out_dir / "ota"
    ota_dir.mkdir(exist_ok=True)
    app_name, www_name = f"nDash-v{version}-{board}.bin", f"www-v{version}.bin"
    copy_retry(out_dir / OTA_APP, ota_dir / app_name)
    copy_retry(out_dir / WWW, ota_dir / www_name)

    built = datetime.now().strftime("%Y-%m-%d %H:%M")
    commit = git_commit()
    write_text(out_dir / "README.md", README.format(version=version, board=board, console=CONSOLE.get(board, "usb"), built=built,
                                                    commit=commit, app_name=app_name, www_name=www_name))

    files = sorted(p for p in out_dir.rglob("*") if p.is_file() and p.name not in ("manifest.json", "SHA256SUMS.txt"))
    sums = {str(p.relative_to(out_dir)).replace("\\", "/"): (p.stat().st_size, sha256(p)) for p in files}
    manifest = {"name": "nDash", "version": version, "board": board, "git": commit, "built": built,
                "console": CONSOLE.get(board, "usb"), "flash": {off: name for off, name in pairs},
                "files": {k: {"size": v[0], "sha256": v[1]} for k, v in sums.items()}}
    write_text(out_dir / "manifest.json", json.dumps(manifest, indent=2) + "\n")
    write_text(out_dir / "SHA256SUMS.txt", "".join(f"{v[1]} *{k}\n" for k, v in sums.items()))

    if make_zip:
        zip_path = out_dir.parent / f"{out_dir.name}.zip"
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for p in sorted(out_dir.rglob("*")):
                if p.is_file():
                    z.write(p, Path(out_dir.name) / p.relative_to(out_dir))
        print(f"Zip: {zip_path} ({zip_path.stat().st_size // 1024} KB)")
    print(f"Packaged: {out_dir}")


def build_one(board, version, skip_build, make_zip, clean):
    print(f"\n=== {board} ===")
    build_dir = RELEASE_BUILD_ROOT / board
    if clean and build_dir.exists():
        shutil.rmtree(build_dir, ignore_errors=True)
    if not skip_build:
        run([
            "idf.py",
            "-B", str(build_dir),
            "-D", f"SDKCONFIG={build_dir / 'sdkconfig'}",  # not the sdkconfig of the development build
            "-D", f"NDASH_BOARD={board}",
            "-D", f"NDASH_CONSOLE={CONSOLE.get(board, 'usb')}",
            "-D", "NDASH_UI_SHOW_FPS=0",       # no FPS overlay in a release
            "-D", "NDASH_LOG_LEVEL=warn",      # a quiet console: the log is written by polling and costs CPU
            "-D", "NDASH_DEMO=0",              # real CAN data, not the sweeping demo
            "-D", "NDASH_LAYOUT_BUILTIN=0",    # the saved layout is used
            "build",
        ])
    elif not (build_dir / "flasher_args.json").is_file():
        raise SystemExit(f"--skip-build, but {build_dir} has no build")
    package(board, version, build_dir, make_zip)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--board", action="append", choices=BOARDS + UNTESTED_BOARDS,
                        help="build only this board (repeat for several); default: the boards tried on hardware: " + ", ".join(BOARDS))
    parser.add_argument("--skip-build", action="store_true", help="only package the builds that are already in build_release/")
    parser.add_argument("--no-zip", action="store_true", help="do not make a .zip of every package")
    parser.add_argument("--clean", action="store_true", help="delete the build directory of a board before building it")
    args = parser.parse_args()

    version = release_version()
    boards = args.board or BOARDS
    if git_commit().endswith("+dirty"):
        print("Warning: the working tree has uncommitted changes, the release says so in manifest.json (git: ...+dirty).")

    for board in boards:
        build_one(board, version, args.skip_build, not args.no_zip, args.clean)

    print(f"\nDone. Release output: release/v{version}/")


if __name__ == "__main__":
    sys.exit(main())
