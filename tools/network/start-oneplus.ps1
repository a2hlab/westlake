param([Parameter(Mandatory=$true)][string]$Serial, [Parameter(Mandatory=$true)][string]$Adb, [string]$HostInterface = 'Wi-Fi')
$ErrorActionPreference = 'Stop'
$relayExe = Join-Path $PSScriptRoot 'gnirehtet.exe'
$clientApk = Join-Path $PSScriptRoot 'gnirehtet.apk'
$env:ADB = (Resolve-Path $Adb).Path
if ((Get-FileHash $relayExe -Algorithm SHA256).Hash -ne 'd5daefbb48143fbc2b01f24be79135fc11959fe4553b00f5a454801ed5ff720d') { throw 'Relay hash mismatch' }
if ((Get-FileHash $clientApk -Algorithm SHA256).Hash -ne 'c1ac2b869a48e3c836046aac5a168f3ade510288f3304a87ddb671315c564b9a') { throw 'Client hash mismatch' }
$deviceState = & $env:ADB -s $Serial get-state
if ($LASTEXITCODE -ne 0 -or $deviceState.Trim() -ne 'device') { throw 'Phone is not authorized and connected over ADB' }
$dnsServers = @((Get-DnsClientServerAddress -InterfaceAlias $HostInterface -AddressFamily IPv4).ServerAddresses)
if ($dnsServers.Count -eq 0) { throw "No IPv4 DNS on $HostInterface" }
$listener = @(Get-NetTCPConnection -State Listen -LocalPort 31416 -ErrorAction SilentlyContinue)
if ($listener.Count) {
    foreach ($owner in ($listener.OwningProcess | Select-Object -Unique)) {
        if ((Get-Process -Id $owner).Path -ne $relayExe) { throw 'Port 31416 belongs to another program' }
    }
} else {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $relayProcess = Start-Process -FilePath $relayExe -ArgumentList 'relay' -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput "$PSScriptRoot\relay-$stamp.stdout.log" -RedirectStandardError "$PSScriptRoot\relay-$stamp.stderr.log"
    $relayProcess.Id | Set-Content "$PSScriptRoot\relay.pid"
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        if (Get-NetTCPConnection -State Listen -LocalPort 31416 -ErrorAction SilentlyContinue) { break }
        if ($relayProcess.HasExited) { throw 'Relay exited before listening' }
        Start-Sleep -Milliseconds 250
    }
    if (!(Get-NetTCPConnection -State Listen -LocalPort 31416 -ErrorAction SilentlyContinue)) { throw 'Relay did not start listening' }
}
Push-Location $PSScriptRoot
try {
    & $relayExe start $Serial -d ($dnsServers -join ',')
    if ($LASTEXITCODE -ne 0) { throw 'Could not start Android VPN client' }
} finally { Pop-Location }
Write-Output "USB IPv4 tunnel started for $Serial via $HostInterface; DNS=$($dnsServers -join ',')."
Write-Output 'If Android requests VPN consent, accept it on the phone. Re-run this script after USB reconnect.'
