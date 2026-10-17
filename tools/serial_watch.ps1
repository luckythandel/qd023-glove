param(
  [string]$Port = "COM13",
  [int]$Baud = 115200,
  [int]$Seconds = 900,
  [string]$LogPath = "C:\Users\lucky\Documents\deepseek-harness\default-workspace\serial_capture.log"
)

$ErrorActionPreference = "Continue"
$sp = New-Object System.IO.Ports.SerialPort $Port,$Baud,None,8,One
$sp.ReadTimeout = 500
$sp.NewLine = "`n"
$sp.DtrEnable = $false
$sp.RtsEnable = $false
try {
  $sp.Open()
} catch {
  Write-Output "[FATAL] cannot open $Port : $($_.Exception.Message)"
  exit 1
}
Write-Output "[watching $Port @ $Baud - Ctrl+C / kill job to stop]"
$start = Get-Date
try {
  while (((Get-Date) - $start).TotalSeconds -lt $Seconds) {
    try {
      $line = $sp.ReadLine()
      $ts = (Get-Date).ToString("HH:mm:ss.fff")
      $out = "[$ts] $line"
      Add-Content -Path $LogPath -Value $out -Encoding utf8
      Write-Output $out
    } catch [System.TimeoutException] { }
  }
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
Write-Output "[capture ended]"
