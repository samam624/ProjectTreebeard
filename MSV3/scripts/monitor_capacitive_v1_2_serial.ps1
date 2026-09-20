param(
  [string]$Port = "COM3",
  [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"
$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, "None", 8, "One")
$serial.ReadTimeout = 500
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
  $serial.Open()
  Clear-Host
  Write-Host "Capacitive Moisture Sensor v1.2 - LIVE" -ForegroundColor Cyan
  Write-Host "$Port at $Baud baud | lower mV = wetter | Ctrl+C or close to stop" -ForegroundColor Yellow
  Write-Host ""

  while ($true) {
    try {
      $line = $serial.ReadLine()
      Write-Host $line
    }
    catch [System.TimeoutException] {
      # Keep waiting without treating a quiet half-second as a failure.
    }
  }
}
finally {
  if ($serial.IsOpen) {
    $serial.Close()
  }
  $serial.Dispose()
}
