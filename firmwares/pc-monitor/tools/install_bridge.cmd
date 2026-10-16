@echo off
REM One-click installer for the FoloToy AI Passport PC status bridge.
REM Double-click this file. It registers a per-user logon Scheduled Task that
REM runs tools\pc_status_bridge.py hidden, so afterwards you only plug in USB.
setlocal

echo Checking Python dependencies (pyserial, psutil)...
python -c "import serial, psutil" 2>nul
if errorlevel 1 (
    echo.
    echo Missing Python packages. Install them once, then run this again:
    echo     pip install pyserial psutil
    pause
    exit /b 1
)

echo Registering the logon task...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_autostart.ps1"
if errorlevel 1 (
    echo.
    echo Installation failed. See the messages above.
    pause
    exit /b 1
)

echo Starting the bridge now...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-ScheduledTask -TaskName 'FoloToy PcMon Bridge'"

echo.
echo Done. The bridge now runs automatically at every logon.
pause
exit /b 0
