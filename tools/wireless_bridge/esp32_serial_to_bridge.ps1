param(
    [string]$Port = "COM6",
    [int]$Baud = 115200,
    [string]$BridgeHost = "127.0.0.1",
    [int]$BridgePort = 8090
)

$ErrorActionPreference = "Stop"

Write-Host "[ESP32-FWD] Serial $Port @ $Baud -> TCP ${BridgeHost}:${BridgePort}"

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$serial.NewLine = "`n"
$serial.ReadTimeout = 500
$serial.Open()

$tcp = New-Object System.Net.Sockets.TcpClient
$tcp.NoDelay = $true
$tcp.Connect($BridgeHost, $BridgePort)
$stream = $tcp.GetStream()
$writer = New-Object System.IO.StreamWriter($stream, [System.Text.Encoding]::UTF8)
$writer.NewLine = "`n"
$writer.AutoFlush = $true

try {
    while ($true) {
        try {
            $line = $serial.ReadLine().Trim()
        } catch [System.TimeoutException] {
            continue
        }

        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        if ($line.StartsWith("{") -and $line.EndsWith("}")) {
            $writer.WriteLine($line)
            Write-Host "[ESP32-FWD] JSON $line"
        } else {
            $parts = $line -split ','
            if ($parts.Count -ge 6) {
                try {
                    $vertAccel = [double]::Parse($parts[0], [Globalization.CultureInfo]::InvariantCulture)
                    $horizAccel = [double]::Parse($parts[1], [Globalization.CultureInfo]::InvariantCulture)
                    $totalAccel = [double]::Parse($parts[2], [Globalization.CultureInfo]::InvariantCulture)
                    $vertDisp = [double]::Parse($parts[3], [Globalization.CultureInfo]::InvariantCulture)
                    $horizDisp = [double]::Parse($parts[4], [Globalization.CultureInfo]::InvariantCulture)
                    $freqX10 = [double]::Parse($parts[5], [Globalization.CultureInfo]::InvariantCulture)
                    $alarm = 0
                    if ($parts.Count -ge 7) {
                        $alarm = [double]::Parse($parts[6], [Globalization.CultureInfo]::InvariantCulture)
                    }

                    $ts = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
                    $freq = $freqX10 / 10.0
                    $state = "IDLE"
                    $confidence = 0.15
                    if ($alarm -gt 0 -or [Math]::Abs($vertDisp) -gt 50 -or $totalAccel -gt 8) {
                        $state = "SEVERE"
                        $confidence = 0.95
                    } elseif ([Math]::Abs($vertDisp) -gt 15 -or $totalAccel -gt 3) {
                        $state = "MODERATE"
                        $confidence = 0.65
                    } elseif ([Math]::Abs($vertDisp) -gt 5 -or $totalAccel -gt 1) {
                        $state = "LIGHT"
                        $confidence = 0.35
                    }

                    $rawObj = [ordered]@{
                        type = "raw_accel"
                        source = "esp32_bno055"
                        ts = $ts
                        ax = [Math]::Round($horizAccel, 3)
                        ay = 0.0
                        az = [Math]::Round($vertAccel, 3)
                    }
                    $gallopingObj = [ordered]@{
                        type = "galloping"
                        source = "esp32_bno055"
                        ts = $ts
                        state = $state
                        amp_dominant = [Math]::Round([Math]::Abs($vertDisp), 3)
                        amp_x_pp = [Math]::Round([Math]::Abs($horizDisp), 3)
                        amp_y_pp = 0.0
                        amp_z_pp = [Math]::Round([Math]::Abs($vertDisp), 3)
                        displacement_est = [Math]::Round($vertDisp, 3)
                        dominant_freq = [Math]::Round($freq, 3)
                        zero_cross_rate = 0.0
                        rms_accel = [Math]::Round($totalAccel, 3)
                        vibr_energy = [Math]::Round(($totalAccel * $totalAccel), 3)
                        torsion_deg = 0.0
                        confidence = [Math]::Round($confidence, 2)
                    }

                    $raw = $rawObj | ConvertTo-Json -Compress
                    $galloping = $gallopingObj | ConvertTo-Json -Compress

                    $writer.WriteLine($raw)
                    $writer.WriteLine($galloping)
                    Write-Host "[ESP32-FWD] CSV->JSON raw_accel/galloping"
                } catch {
                    Write-Host "[ESP32-FWD] skip: $line"
                }
            } else {
                Write-Host "[ESP32-FWD] skip: $line"
            }
        }
    }
}
finally {
    if ($serial.IsOpen) { $serial.Close() }
    $writer.Dispose()
    $stream.Dispose()
    $tcp.Close()
}
