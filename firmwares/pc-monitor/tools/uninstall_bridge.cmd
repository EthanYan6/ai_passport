@echo off
REM Remove the FoloToy AI Passport PC status bridge autostart task.
REM Double-click this file to stop and unregister it.
setlocal

powershell -NoProfile -ExecutionPolicy Bypass -Command "Stop-ScheduledTask -TaskName 'FoloToy PcMon Bridge' -ErrorAction SilentlyContinue"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_autostart.ps1" -Remove

echo.
pause
exit /b 0
