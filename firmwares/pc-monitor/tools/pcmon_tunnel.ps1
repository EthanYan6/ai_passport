# pcmon_tunnel.ps1 - keepalive wrapper for the pc-monitor SSH reverse tunnel.
#
# The remote TRAE hook (deployed by deploy_remote_hook.sh) sends events to
# 127.0.0.1:8765 ON THE REMOTE HOST. The reverse tunnel forwards that port
# back to the local bridge. This wrapper keeps the tunnel alive: whenever
# ssh exits (network drop, remote reboot, ...), it reconnects after a pause.
#
# Usually launched by the "FoloToy PcMon Tunnel" scheduled task (installed by
# install_tunnel_autostart.ps1); can also be run by hand for debugging:
#   powershell -ExecutionPolicy Bypass -File tools\pcmon_tunnel.ps1 -Remote user@host
#
# Requires key-based ssh auth to the remote (BatchMode is on: no prompts).
# Log: %LOCALAPPDATA%\FoloToy\pcmon-tunnel.log

param(
    [Parameter(Mandatory=$true)]
    [string]$Remote
)

$logDir = Join-Path $env:LOCALAPPDATA "FoloToy"
$logPath = Join-Path $logDir "pcmon-tunnel.log"

New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Write-Log([string]$msg) {
    $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $msg
    try {
        if ((Test-Path $logPath) -and ((Get-Item $logPath).Length -gt 512KB)) {
            Clear-Content $logPath
        }
        Add-Content -Path $logPath -Value $line -Encoding UTF8
    } catch {}
}

Write-Log "tunnel wrapper starting (remote=$Remote)"

while ($true) {
    Write-Log "connecting: ssh -N -R 8765:127.0.0.1:8765 $Remote"
    & ssh.exe -N -T `
        -R 8765:127.0.0.1:8765 `
        -o BatchMode=yes `
        -o ExitOnForwardFailure=yes `
        -o ServerAliveInterval=15 `
        -o ServerAliveCountMax=3 `
        $Remote 2>&1 | ForEach-Object { Write-Log "ssh: $_" }
    Write-Log "ssh exited (code $LASTEXITCODE); reconnecting in 5s"
    Start-Sleep -Seconds 5
}
