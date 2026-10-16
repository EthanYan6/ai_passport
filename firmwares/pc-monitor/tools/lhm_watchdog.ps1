# LHM Web Server watchdog.
# Probes LibreHardwareMonitor's data.json; if it stops responding (known
# intermittent failure when LHM auto-starts at logon), restarts LHM through
# its scheduled task. Runs elevated via the "FoloToy LHM Watchdog" task.
$Url = 'http://127.0.0.1:8085/data.json'
$LogFile = "$env:LOCALAPPDATA\FoloToy\lhm-watchdog.log"

function Write-Log($msg) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Add-Content -Path $LogFile -Value $line
}

function Test-DataJson {
    try {
        Invoke-WebRequest -Uri $Url -TimeoutSec 3 -UseBasicParsing | Out-Null
        return $true
    } catch {
        return $false
    }
}

if (Test-DataJson) { exit 0 }

# Retry once to avoid restarting on a transient hiccup.
Start-Sleep -Seconds 5
if (Test-DataJson) {
    Write-Log 'data.json recovered on retry; no action'
    exit 0
}

Write-Log 'data.json unreachable; restarting LibreHardwareMonitor'
Stop-Process -Name LibreHardwareMonitor -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
Start-ScheduledTask -TaskName 'LibreHardwareMonitor'
Start-Sleep -Seconds 15

if (Test-DataJson) {
    Write-Log 'restart OK; data.json responding'
    exit 0
}
Write-Log 'restart FAILED; data.json still unreachable'
exit 1
