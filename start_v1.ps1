[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
$mosquittoExe = "C:\Program Files\mosquitto\mosquitto.exe"
$mosquittoConfig = Join-Path $projectRoot "gateway\mosquitto\mosquitto.conf"
$pythonExe = Join-Path $projectRoot ".venv\Scripts\python.exe"
$dashboardScript = Join-Path $projectRoot "gateway\run.py"

function Test-TcpEndpoint {
    param(
        [Parameter(Mandatory)] [string] $HostName,
        [Parameter(Mandatory)] [int] $Port,
        [int] $TimeoutMs = 500
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        return $client.ConnectAsync($HostName, $Port).Wait($TimeoutMs) -and $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Wait-ForEndpoint {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $HostName,
        [Parameter(Mandatory)] [int] $Port
    )

    foreach ($attempt in 1..20) {
        if (Test-TcpEndpoint -HostName $HostName -Port $Port) {
            Write-Host "[online] $Name ($HostName`:$Port)" -ForegroundColor Green
            return $true
        }
        Start-Sleep -Milliseconds 250
    }

    Write-Host "[offline] $Name ($HostName`:$Port)" -ForegroundColor Red
    return $false
}

foreach ($requiredPath in @($mosquittoExe, $mosquittoConfig, $pythonExe, $dashboardScript)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required file not found: $requiredPath"
    }
}

$brokerConfigText = Get-Content -LiteralPath $mosquittoConfig -Raw
$listenerMatch = [regex]::Match(
    $brokerConfigText,
    '(?m)^\s*listener\s+(\d+)\s+(\S+)\s*$'
)
if (-not $listenerMatch.Success) {
    throw "No explicit MQTT listener was found in $mosquittoConfig"
}

$brokerPort = [int] $listenerMatch.Groups[1].Value
$brokerHost = $listenerMatch.Groups[2].Value
$dashboardHost = "127.0.0.1"
$dashboardPort = 8000

if (Test-TcpEndpoint -HostName $brokerHost -Port $brokerPort) {
    Write-Host "MQTT broker is already running."
}
else {
    Write-Host "Starting MQTT broker..."
    Start-Process `
        -FilePath $mosquittoExe `
        -ArgumentList @("-c", $mosquittoConfig, "-v") `
        -WorkingDirectory $projectRoot `
        -WindowStyle Hidden
}

$brokerOnline = Wait-ForEndpoint `
    -Name "MQTT broker" `
    -HostName $brokerHost `
    -Port $brokerPort

if (Test-TcpEndpoint -HostName $dashboardHost -Port $dashboardPort) {
    Write-Host "Dashboard is already running."
}
else {
    Write-Host "Starting dashboard..."
    Start-Process `
        -FilePath $pythonExe `
        -ArgumentList $dashboardScript `
        -WorkingDirectory $projectRoot `
        -WindowStyle Hidden
}

$dashboardOnline = Wait-ForEndpoint `
    -Name "Dashboard" `
    -HostName $dashboardHost `
    -Port $dashboardPort

if ($dashboardOnline) {
    $dashboardUrl = "http://localhost:$dashboardPort"
    try {
        $state = Invoke-RestMethod `
            -UseBasicParsing `
            -Uri "http://$dashboardHost`:$dashboardPort/api/state" `
            -TimeoutSec 5
        Write-Host "Dashboard MQTT: $($state.broker_connected)"
        Write-Host "ESP32 online:  $($state.device_online)"
    }
    catch {
        Write-Warning "Dashboard started, but its state endpoint is not ready yet."
    }
    Write-Host "Open $dashboardUrl" -ForegroundColor Cyan
}

if (-not $brokerOnline -or -not $dashboardOnline) {
    throw "One or more V1 services failed to start. Check the configured LAN IP and installed dependencies."
}
