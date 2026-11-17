@echo off
setlocal

rem Flash bootloader / partition table / app to ESP32-C3 (same offsets as idf.py flash).
rem 三文件分别写入，不触碰 NVS 区：升级刷机保留已存的 WiFi/密钥/服务器配置。
rem （整包 ai-usage-full.bin 从 0x0 连续写入会把 0x9000 起的 NVS 一起抹掉，勿用于升级）
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

set BL=%~dp0..\build\bootloader\bootloader.bin
set PTB=%~dp0..\build\partition_table\partition-table.bin
set APP=%~dp0..\build\ai-usage.bin
if not exist "%APP%" (
  echo [flash] Build artifacts not found: %APP%
  echo         Build first:
  echo         docker run --rm -v "F:\code\ai_passport:/project" -w /project/firmwares/ai-usage espressif/idf:v5.5.3 idf.py build
  exit /b 1
)

echo [flash] Flashing bootloader + partition table + app to %PORT% (NVS kept) ...
python -m esptool --chip esp32c3 --port %PORT% --baud 460800 write-flash 0x0 "%BL%" 0x8000 "%PTB%" 0x10000 "%APP%"
exit /b %ERRORLEVEL%
