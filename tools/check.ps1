# The check to run before sending a change. It builds the PC version, plays a
# fixed stretch of E1M1 and compares the game state with the known value. Run
# tools\build.ps1 once first, it sets up build-pc-dev.
#
#   powershell -ExecutionPolicy Bypass -File tools\check.ps1
#
# The expected value is for the registered game, pak0.pak version 1.06. It
# covers the game logic, not the picture. Compare screenshots for that.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$expected = "ce86f894f9de"

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Get-ChildItem "$env:USERPROFILE\.pico-sdk\cmake\*\bin\cmake.exe" -ErrorAction SilentlyContinue |
        Select-Object -Last 1 -ExpandProperty FullName
}
if (-not (Test-Path "build-pc-dev\CMakeCache.txt")) { throw "check: run tools\build.ps1 first" }
& $cmake --build build-pc-dev --config Release --target quakeog_pc
if ($LASTEXITCODE -ne 0) { throw "check: the PC build failed" }

$cfg = @("host_framerate 0.05", "map e1m1") + @("wait") * 100 + @("qog_statehash") +
    @("wait") * 300 + @("qog_statehash", "qog_quit")
Set-Content -Path "data\id1\qog_check.cfg" -Value $cfg -Encoding ascii
$out = "build-pc-dev\check.log"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\pctest.ps1 -Exe build-pc-dev\Release\quakeog_pc.exe `
    -GameArgs "-nosound +exec qog_check.cfg" -Out $out -TimeoutSec 300

$text = (Get-Content $out | Where-Object { $_ -match "qog_statehash" } |
    ForEach-Object { $_.TrimEnd() + "`n" }) -join ""
$md5 = [Security.Cryptography.MD5]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($text))
$got = (($md5 | ForEach-Object { $_.ToString("x2") }) -join "").Substring(0, 12)
if ($got -eq $expected) {
    Write-Host "PASS, game state $got" -ForegroundColor Green
} else {
    Write-Host "FAIL, game state $got, expected $expected (see $out)" -ForegroundColor Red
    exit 1
}
