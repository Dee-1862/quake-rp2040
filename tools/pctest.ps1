# Run the PC build for an automated test: no window, no sound from the
# speakers, a known config, and a hard timeout.
#
#   powershell -File tools\pctest.ps1 -Exe build-pc-dev\Release\quakeog_pc.exe `
#       -GameArgs "+exec qogtest_hash.cfg" -Out $env:TEMP\out.txt
#
# -hidden makes the game draw off-screen and mix sound without output
# (port/pc/vid_pc.c, port/pc/snd_pc.c). The fixed config is restored before
# the run, because every run saves its settings and the next would inherit
# them. Never name a PowerShell parameter $args: it is a reserved variable
# and silently arrives empty (docs/lessons.md).
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$GameArgs = "",
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$TimeoutSec = 120,
    [string]$Config = "build-pc-dev\fixed_config.cfg"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (Test-Path $Config) { Copy-Item $Config data\id1\config.cfg -Force }
if (-not $GameArgs.Trim()) { throw "pctest: -GameArgs is empty; the game would never quit" }
$p = Start-Process -FilePath $Exe -ArgumentList "-hidden -nomsgbox $GameArgs" -NoNewWindow -PassThru `
    -RedirectStandardOutput $Out -RedirectStandardError "$Out.err"
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    Stop-Process -Id $p.Id -Force
    Write-Output "pctest: TIMEOUT after $TimeoutSec s ($GameArgs)"
}
if (Test-Path $Config) { Copy-Item $Config data\id1\config.cfg -Force }
