@echo off
REM Builds and packages an nDash release for all supported boards (or one,
REM with --board NAME). Thin wrapper: activates ESP-IDF if idf.py isn't already
REM on PATH, then delegates the actual work to build_release.py.
REM
REM ESP-IDF is looked for in this order:
REM   1) IDF_PATH
REM   2) idf.currentSetup from .vscode\settings.json (the ESP-IDF the VS Code extension uses)
REM   3) %USERPROFILE%\esp\esp-idf, then every %USERPROFILE%\esp\<version>\esp-idf, newest name first
REM A broken install (export.bat fails, idf.py still missing) is skipped, the next one is tried.
setlocal enabledelayedexpansion

where idf.py >nul 2>nul
if %errorlevel%==0 goto :run

echo idf.py not on PATH - looking for an ESP-IDF install to activate ...

if defined IDF_PATH (
  call :try "%IDF_PATH%"
  if !errorlevel!==0 goto :run
)

set "VSC_IDF="
if exist "%~dp0..\.vscode\settings.json" (
  for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "try { (Get-Content -Raw '%~dp0..\.vscode\settings.json' | ConvertFrom-Json).'idf.currentSetup' } catch {}"`) do set "VSC_IDF=%%P"
)
if defined VSC_IDF (
  call :try "!VSC_IDF!"
  if !errorlevel!==0 goto :run
)

call :try "%USERPROFILE%\esp\esp-idf"
if !errorlevel!==0 goto :run

for /f "delims=" %%D in ('dir /b /ad /o-n "%USERPROFILE%\esp" 2^>nul') do (
  call :try "%USERPROFILE%\esp\%%D\esp-idf"
  if !errorlevel!==0 goto :run
)

echo Could not activate any ESP-IDF install (IDF_PATH, .vscode idf.currentSetup, %USERPROFILE%\esp\).
echo Activate it manually first, e.g.:
echo   %USERPROFILE%\esp\v6.0\esp-idf\export.bat
echo then re-run this script.
pause
exit /b 1

:try
REM %1 = ESP-IDF directory. Returns 0 when export.bat ran and idf.py is now on PATH.
if not exist "%~1\export.bat" exit /b 1
echo Trying ESP-IDF at %~1 ...
REM export.bat picks the Python env by the version of the `python` on PATH (idf6.0_py3.13_env
REM for Python 3.13). If only an env for another Python exists, put that env's python first.
set "IDF_MM="
for %%I in ("%~1\..") do set "IDF_PARENT=%%~fI"
for %%I in ("!IDF_PARENT!") do set "IDF_VERDIR=%%~nxI"
set "IDF_VER=!IDF_VERDIR:v=!"
for /f "tokens=1,2 delims=." %%a in ("!IDF_VER!") do set "IDF_MM=%%a.%%b"
if defined IDF_MM (
  for /d %%E in ("%USERPROFILE%\.espressif\python_env\idf!IDF_MM!_py*_env") do (
    if exist "%%E\Scripts\python.exe" set "PATH=%%E\Scripts;!PATH!"
  )
)
call "%~1\export.bat"
where idf.py >nul 2>nul
exit /b %errorlevel%

:run
python "%~dp0build_release.py" %*
if %errorlevel% neq 0 (
  echo.
  echo Build failed - see output above.
)
pause
