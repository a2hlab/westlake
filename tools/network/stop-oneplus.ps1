param([Parameter(Mandatory=$true)][string]$Serial, [Parameter(Mandatory=$true)][string]$Adb, [switch]$StopRelay)
$ErrorActionPreference = 'Stop'
$relayExe = Join-Path $PSScriptRoot 'gnirehtet.exe'
$env:ADB = (Resolve-Path $Adb).Path
& $relayExe stop $Serial
if ($LASTEXITCODE -ne 0) { throw 'Could not stop Android VPN client' }
& $env:ADB -s $Serial reverse --remove localabstract:gnirehtet
if ($LASTEXITCODE -ne 0) { Write-Warning 'ADB reverse mapping was already absent or could not be removed' }
if ($StopRelay) {
    # Verify executable identity at the listening port; never trust a stale PID file.
    $listeners = @(Get-NetTCPConnection -State Listen -LocalPort 31416 -ErrorAction SilentlyContinue)
    foreach ($owner in ($listeners.OwningProcess | Select-Object -Unique)) {
        $relayProcess = Get-Process -Id $owner
        if ($relayProcess.Path -ne $relayExe) { throw 'Port 31416 belongs to another program; leaving it running' }
        $relayProcess | Stop-Process
    }
}
