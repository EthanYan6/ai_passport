@echo off
setlocal

rem Flash build\pc-monitor-full.bin to ESP32-C3 at 0x0.
rem Usage: flash.cmd [COMx]   (auto-detects USB-Serial-JTAG port VID 303A when omitted)

set PORT=%~1
if "%PORT%"=="" (
  for /f "delims=" %%P in ('powershell -NoProfile -Command "Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match 'VID_303A' -and $_.Name -match 'COM\d+' } | ForEach-Object { if ($_.Name -match 'COM\d+') { $Matches[0] } } | Select-Object -First 1"') do set PORT=%%P
)
if "%PORT%"=="" (
  echo [flash] No ESP32-C3 USB-Serial-JTAG port found ^(VID 303A^).
  echo         Power on the device, connect it via a data-capable USB cable,
  echo         or pass the port explicitly: %~nx0 COM5
  exit /b 1
)

set BIN=%~dp0..\build\pc-monitor-full.bin
if not exist "%BIN%" (
  echo [flash] Image not found: %BIN%
  echo         Build and merge first:
  echo         docker run --rm -v "D:\k1\ai_passport:/project" -w /project/firmwares/pc-monitor espressif/idf:v5.5.3 idf.py build
  echo         docker run --rm -v "D:\k1\ai_passport:/project" -w /project/firmwares/pc-monitor espressif/idf:v5.5.3 idf.py merge-bin -o pc-monitor-full.bin
  exit /b 1
)

echo [flash] Flashing %BIN% to %PORT% at 0x0 ...
python -m esptool --chip esp32c3 --port %PORT% --baud 460800 write-flash 0x0 "%BIN%"
exit /b %ERRORLEVEL%
