# install_autostart.ps1 - zero-touch launcher for the FoloToy PC status bridge.
#
# Registers a Scheduled Task that runs tools/pc_status_bridge.py (hidden) at
# user logon. The bridge auto-detects the device and reconnects on its own, so
# once this task exists the only user action is connecting the USB cable.
#
# Usage (no admin required for a per-user logon task):
#   powershell -ExecutionPolicy Bypass -File tools\install_autostart.ps1
#   powershell -ExecutionPolicy Bypass -File tools\install_autostart.ps1 -Remove
#
# This script changes the local machine (creates/removes a Scheduled Task).
# Review it before running.

param(
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$taskName = "FoloToy PcMon Bridge"

if ($Remove) {
    $existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($existing) {
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        Write-Host "Removed scheduled task '$taskName'."
    } else {
        Write-Host "Scheduled task '$taskName' was not present."
    }
    return
}

$scriptPath = Join-Path $PSScriptRoot "pc_status_bridge.py"
if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Bridge script not found next to this installer: $scriptPath"
}

$python = (Get-Command pythonw.exe -ErrorAction SilentlyContinue).Source
if (-not $python) {
    $python = (Get-Command python.exe -ErrorAction SilentlyContinue).Source
}
if (-not $python) {
    throw "Python not found on PATH. Install Python (with pyserial and psutil) first."
}

$action = New-ScheduledTaskAction -Execute $python -Argument "`"$scriptPath`""
$trigger = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -Hidden `
    -RestartCount 3 `
    -RestartInterval (New-TimeSpan -Minutes 1)

Register-ScheduledTask `
    -TaskName $taskName `
    -Action $action `
    -Trigger $trigger `
    -Settings $settings `
    -Description "Runs the FoloToy AI Passport PC status bridge automatically." `
    -Force | Out-Null

Write-Host "Installed scheduled task '$taskName'."
Write-Host "  python : $python"
Write-Host "  script : $scriptPath"
Write-Host "It will start at your next logon. Start it now with:"
Write-Host "  Start-ScheduledTask -TaskName '$taskName'"
