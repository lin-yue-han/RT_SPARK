param(
    [string]$Port = "COM6",
    [Parameter(Mandatory = $true)][string]$Ssid,
    [Parameter(Mandatory = $true)][string]$Password,
    [string]$BridgeHost = "192.168.43.128",
    [int]$BridgePort = 8090
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Python = Join-Path $Root ".venv\Scripts\python.exe"

if (-not (Test-Path $Python)) {
    throw "Python venv not found: $Python"
}

$ConfigJson = @{
    ssid = $Ssid
    pass = $Password
    host = $BridgeHost
    port = $BridgePort
} | ConvertTo-Json -Compress

$TempFile = Join-Path $Root "rt_spark_config.json"
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($TempFile, $ConfigJson, $Utf8NoBom)

try {
    & $Python -m mpremote connect $Port fs cp $TempFile ":rt_spark_config.json"
    & $Python -m mpremote connect $Port reset
    Write-Host "ESP32 Wi-Fi config written. It will connect to $BridgeHost`:$BridgePort after reboot."
} finally {
    Remove-Item -LiteralPath $TempFile -Force -ErrorAction SilentlyContinue
}
