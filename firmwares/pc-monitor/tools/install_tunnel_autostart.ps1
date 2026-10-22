# install_tunnel_autostart.ps1 - zero-touch launcher for the pc-monitor SSH
# reverse tunnel (remote TRAE hooks -> local bridge).
#
# Registers a Scheduled Task that runs tools/pcmon_tunnel.ps1 (hidden) at
# user logon. The wrapper reconnects forever, so once this task exists the
# remote badge page works without any manual ssh command.
#
# Prerequisite: key-based ssh auth to the remote must work non-interactively;
# the installer checks this and prints the one-time fix if it does not.
#
# Usage (no admin required for a per-user logon task):
#   powershell -ExecutionPolicy Bypass -File tools\install_tunnel_autostart.ps1 -Remote user@host
#   powershell -ExecutionPolicy Bypass -File tools\install_tunnel_autostart.ps1 -Remove
#
# This script changes the local machine (creates/removes a Scheduled Task).
# Review it before running.

param(
    [Parameter(Mandatory=$true)]
    [string]$Remote,
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$taskName = "FoloToy PcMon Tunnel"

if ($Remove) {
    $existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($existing) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        Write-Host "Removed scheduled task '$taskName'."
    } else {
        Write-Host "Scheduled task '$taskName' was not present."
    }
    # Best effort: kill any leftover tunnel ssh process.
    Get-CimInstance Win32_Process -Filter "Name='ssh.exe'" |
        Where-Object { $_.CommandLine -match '-R 8765:' } |
        ForEach-Object {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
            Write-Host "Killed leftover ssh tunnel (PID $($_.ProcessId))."
        }
    return
}

$scriptPath = Join-Path $PSScriptRoot "pcmon_tunnel.ps1"
if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Tunnel wrapper script not found next to this installer: $scriptPath"
}

# The tunnel runs unattended, so key auth must already work without prompting.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& ssh.exe -o BatchMode=yes -o ConnectTimeout=8 $Remote "true" 2>$null
$sshOk = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $prevEap
if (-not $sshOk) {
    Write-Host "Passwordless ssh to $Remote does not work yet."
    Write-Host "Run this once (it asks for the remote password), then re-run this installer:"
    Write-Host ""
    Write-Host "  type `$env:USERPROFILE\.ssh\id_rsa.pub | ssh $Remote `"mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys`""
    Write-Host ""
    exit 1
}

# Launch through conhost --headless so no console window pops up on the desktop.
$action = New-ScheduledTaskAction `
    -Execute "conhost.exe" `
    -Argument "--headless powershell.exe -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$scriptPath`" -Remote $Remote"
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
    -Description "Keeps the SSH reverse tunnel (remote 8765 -> local bridge) alive for remote TRAE hooks." `
    -Force | Out-Null

Write-Host "Installed scheduled task '$taskName'."
Write-Host "  remote : $Remote"
Write-Host "  script : $scriptPath"
Write-Host "It will start at your next logon. Start it now with:"
Write-Host "  Start-ScheduledTask -TaskName '$taskName'"
