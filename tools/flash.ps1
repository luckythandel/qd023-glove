#Requires -Version 5.1
<#
  Compile + upload the QD023 glove firmware.

  Usage:
      powershell -ExecutionPolicy Bypass -File tools\flash.ps1
      powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -SkipCompile
      powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Port COM13

  The ESP32 has no auto-reset through this CH340, so the script retries the
  upload for ~30 s. When it prints "HOLD BOOT", do:
      hold BOOT  ->  tap EN once  ->  release BOOT
#>
param(
    [string]$Port    = "COM13",
    [string]$CliPath = "C:\Users\lucky\arduino-cli\arduino-cli.exe",
    [switch]$SkipCompile
)

$ErrorActionPreference = "Continue"
$sketch = Join-Path (Split-Path $PSScriptRoot -Parent) "firmware\qd023_ble_pc"

if (-not (Test-Path $sketch)) {
    Write-Host "sketch not found: $sketch" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $CliPath)) {
    Write-Host "arduino-cli not found: $CliPath" -ForegroundColor Red
    exit 1
}

if (-not $SkipCompile) {
    Write-Host "== compiling ==" -ForegroundColor Cyan
    & $CliPath compile --fqbn esp32:esp32:esp32 $sketch
    if ($LASTEXITCODE -ne 0) {
        Write-Host "compile FAILED" -ForegroundColor Red
        exit 1
    }
}

# a stale IDE / monitor holding the port makes every attempt fail
Get-Process arduino-cli -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ">>> HOLD BOOT, tap EN, release BOOT  (retrying for ~30s)" -ForegroundColor Yellow
Write-Host ""

for ($i = 1; $i -le 15; $i++) {
    Write-Host "-- attempt $i" -ForegroundColor DarkGray
    $out = & $CliPath upload -p $Port --fqbn "esp32:esp32:esp32:UploadSpeed=115200" $sketch 2>&1
    $out | Select-Object -Last 8

    if ($out -match "Hash of data verified|Hard resetting") {
        Write-Host ""
        Write-Host "FLASH OK - board restarted into calibration" -ForegroundColor Green
        exit 0
    }
    Start-Sleep -Seconds 2
}

Write-Host ""
Write-Host "FLASH FAILED after 15 attempts." -ForegroundColor Red
Write-Host "  - is the BOOT/EN sequence being done while the attempts run?" -ForegroundColor Yellow
Write-Host "  - close the Arduino IDE serial monitor, then retry" -ForegroundColor Yellow
exit 1
