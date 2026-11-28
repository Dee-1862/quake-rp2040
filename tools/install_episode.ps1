# Install one episode of the split game on the FREE-WILi OG, in three
# steps (docs/handoff.md), one CPU in BOOTSEL at a time.
#
#   1. display data, fw.py bootsel --cpu display, then copy qb_ep<N>.uf2 to
#      the RPI-RP2 drive (0x10100000 up, the display app below it is kept)
#   2. the app,      fw.py flash quake --uf2 build\apps\quake\quake_main.uf2
#                    (main, which then sends the display its app and RUN)
#   3. main data,    fw.py flash quake --uf2 qb_ep<N>_main.uf2
#                    (0x10200000 up, the app and the save volume are kept)
#
#   powershell -File tools\install_episode.ps1 -Episode 2
#   powershell -File tools\install_episode.ps1 -Episode 3 -SkipApp   # app already current
#   powershell -File tools\install_episode.ps1 -Episode 2 -Print     # only show the steps
#
# Every image carries qogboot.txt (tools/mkboard.py), so the same app starts
# any episode, and step 2 is needed only when the app changed. The script
# refuses to go on whenever two RPI-RP2 drives are mounted, both RP2040s look
# the same in BOOTSEL, and a copy to the wrong one writes the other chip's
# data over its app. Each step is tried -Retries times. Run it from the repo
# root of the checkout that built the app, with the board on USB and no
# serial console open on its ports.
param(
    [Parameter(Mandatory = $true)][ValidateSet(1, 2, 3)][int]$Episode,
    [string]$ImageDir = "build-board",
    [string]$AppUf2 = "build\apps\quake\quake_main.uf2",
    [switch]$SkipApp,
    [int]$Retries = 3,
    [int]$WaitSec = 90,
    [switch]$Print
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$fw = Join-Path $root "third_party\wiliOGbsp\tools\fw.py"
$display = Join-Path $ImageDir "qb_ep$Episode.uf2"
$main = Join-Path $ImageDir "qb_ep${Episode}_main.uf2"

function Get-BootselDrives {
    # removable drives with the RP2040 boot ROM's INFO_UF2.TXT at the root
    @(Get-CimInstance Win32_LogicalDisk -Filter "DriveType=2" |
        Where-Object { Test-Path (Join-Path ($_.DeviceID + "\") "INFO_UF2.TXT") } |
        ForEach-Object { $_.DeviceID + "\" })
}

function Assert-OneOrNoDrive {
    $d = Get-BootselDrives
    if ($d.Count -gt 1) {
        throw "install_episode: $($d.Count) RPI-RP2 drives are mounted ($($d -join ', ')). Both RP2040s look the same in BOOTSEL, so nothing is copied. Leave only one CPU in BOOTSEL (power the board off and on) and run again."
    }
    return $d
}

function Wait-Drive([bool]$Present, [int]$Seconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $d = Assert-OneOrNoDrive
        if ($Present -and $d.Count -eq 1) { return $d[0] }
        if (-not $Present -and $d.Count -eq 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $null
}

function Invoke-Fw([string[]]$FwArgs) {
    Write-Output ("python " + $fw + " " + ($FwArgs -join " "))
    & python $fw @FwArgs
    return $LASTEXITCODE
}

foreach ($f in @($display, $main) + $(if ($SkipApp) { @() } else { @($AppUf2) })) {
    if (-not (Test-Path $f)) { throw "install_episode: $f not found (docs/handoff.md says how to build it)" }
}
Write-Output "episode $Episode`: display data $display ($((Get-Item $display).Length) B), main data $main ($((Get-Item $main).Length) B)$(if (-not $SkipApp) { ", app $AppUf2" })"
if ($Print) {
    Write-Output "1. python $fw bootsel --cpu display, then copy $display to the RPI-RP2 drive"
    if (-not $SkipApp) { Write-Output "2. python $fw flash quake --uf2 $AppUf2" }
    Write-Output "3. python $fw flash quake --uf2 $main"
    exit 0
}

# 1. display data, first, later main restarts every 20 s until its data is
#    written, and each restart resets the display (docs/handoff.md)
$done = $false
for ($try = 1; $try -le $Retries -and -not $done; $try++) {
    Write-Output "step 1, try $try of $Retries`: display data"
    $d = Assert-OneOrNoDrive
    if ($d.Count -eq 0) {
        if ((Invoke-Fw @("bootsel", "--cpu", "display")) -ne 0) { Start-Sleep -Seconds 5; continue }
        $drive = Wait-Drive $true 30
    } else {
        Write-Output "an RPI-RP2 drive is already at $($d[0]): only if it is the DISPLAY CPU, it is used (Ctrl-C within 5 s otherwise)"
        Start-Sleep -Seconds 5
        $drive = $d[0]
    }
    if (-not $drive) { Write-Output "no RPI-RP2 drive appeared"; continue }
    Assert-OneOrNoDrive | Out-Null
    try {
        Copy-Item $display (Join-Path $drive (Split-Path $display -Leaf)) -Force
    } catch {
        Write-Output "copy stopped: $($_.Exception.Message)"
    }
    # the boot ROM restarts the chip once the whole image is written
    if (Wait-Drive $false $WaitSec) { $done = $true } else { Write-Output "the drive is still there after $WaitSec s" }
}
if (-not $done) { throw "install_episode: step 1 failed $Retries times, nothing else was written" }

# 2. the app on main, which sends the display its app and RUN
if (-not $SkipApp) {
    $done = $false
    for ($try = 1; $try -le $Retries -and -not $done; $try++) {
        Write-Output "step 2, try $try of $Retries`: the app"
        Assert-OneOrNoDrive | Out-Null
        if ((Invoke-Fw @("flash", "quake", "--uf2", $AppUf2)) -eq 0 -and (Wait-Drive $false $WaitSec)) { $done = $true }
        else { Start-Sleep -Seconds 5 }
    }
    if (-not $done) { throw "install_episode: step 2 failed $Retries times. The display data is written, run again with the same episode" }
}

# 3. main's data. Until it is there the new app stops with Sys_Error and
#    restarts every 20 s, fw.py reaches it during one of its USB windows
$done = $false
for ($try = 1; $try -le $Retries -and -not $done; $try++) {
    Write-Output "step 3, try $try of $Retries`: main data"
    Assert-OneOrNoDrive | Out-Null
    if ((Invoke-Fw @("flash", "quake", "--uf2", $main)) -eq 0 -and (Wait-Drive $false $WaitSec)) { $done = $true }
    else { Start-Sleep -Seconds 5 }
}
if (-not $done) { throw "install_episode: step 3 failed $Retries times. Run again with -SkipApp" }
Write-Output "episode $Episode installed: both chips restart into the level the image names (qogboot.txt)"
