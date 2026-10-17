#Requires -Version 5.1
<#
  Open the serial port, optionally send a command character, then log everything.

  Usage:
      powershell -ExecutionPolicy Bypass -File tools\serial_cmd.ps1 -Send d -Seconds 120
      powershell -ExecutionPolicy Bypass -File tools\serial_cmd.ps1 -Send c -Seconds 90
#>
param(
    [string]$Port    = "COM13",
    [int]   $Baud    = 115200,
    [int]   $Seconds = 120,
    [string]$Send    = "",
    [string]$LogPath = "C:\Users\lucky\qd023-glove\logs\serial_cmd.log"
)

$ErrorActionPreference = "Continue"
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, One
$sp.ReadTimeout = 500
$sp.NewLine     = "`n"
$sp.DtrEnable   = $false      # never reset the board from here
$sp.RtsEnable   = $false

Set-Content -Path $LogPath -Value "" -Encoding utf8

try {
    $sp.Open()
} catch {
    Write-Output "[FATAL] cannot open $Port : $($_.Exception.Message)"
    exit 1
}

Start-Sleep -Milliseconds 300
if ($Send.Length -gt 0) {
    $sp.Write($Send)
    Write-Output "[sent '$Send']"
}

$start = Get-Date
try {
    while (((Get-Date) - $start).TotalSeconds -lt $Seconds) {
        try {
            $line = $sp.ReadLine()
            $ts  = (Get-Date).ToString("HH:mm:ss.fff")
            $out = "[$ts] $line"
            Add-Content -Path $LogPath -Value $out -Encoding utf8
            Write-Output $out
        } catch [System.TimeoutException] { }
    }
} finally {
    if ($sp.IsOpen) { $sp.Close() }
}
Write-Output "[capture ended]"
