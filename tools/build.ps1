# Build everything needed to put Quake on a FREE-WILi OG, from your own
# data\id1\pak0.pak and pak1.pak.
#
#   powershell -ExecutionPolicy Bypass -File tools\build.ps1
#
# It does the PC build, records which files each level loads, makes the data
# images and builds the firmware. Nothing is written to the board. Install the
# result with tools\install_episode.ps1 (the last lines printed say how).
#
# Output
#   build-pc-dev\   the Windows build of the game
#   build-board\    data images made from your pak files (never share them)
#   build\          the firmware (never share it)
param(
    [ValidateSet(1, 2, 3)][int]$Episode = 1,
    [string]$OutDir = "build-board"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$bspUrl = "https://github.com/freewili/wiliOGbsp"
$bspCommit = "3426c46728361299ab2687a073579ac6fc1c253d"   # the one this was tested with

function Invoke-Step([string]$What, [scriptblock]$Do) {
    Write-Host ""
    Write-Host "== $What" -ForegroundColor Cyan
    $global:LASTEXITCODE = 0
    & $Do
    if ($LASTEXITCODE -ne 0) { throw "build: '$What' failed (exit $LASTEXITCODE)" }
}

# ---- what must be there ---------------------------------------------------
$missing = @()
foreach ($f in "data\id1\pak0.pak", "data\id1\pak1.pak") {
    if (-not (Test-Path $f)) { $missing += "$f (from your own copy of Quake)" }
}
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Get-ChildItem "$env:USERPROFILE\.pico-sdk\cmake\*\bin\cmake.exe" -ErrorAction SilentlyContinue |
        Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $cmake) { $missing += "CMake 3.21 or newer" }
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { $missing += "Python 3" }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $missing += "git" }
foreach ($d in "sdk\2.3.0", "toolchain\15_2_Rel1", "ninja\v1.13.2", "picotool\2.3.0", "tools\2.3.0") {
    if (-not (Test-Path "$env:USERPROFILE\.pico-sdk\$d")) {
        $missing += "$env:USERPROFILE\.pico-sdk\$d (Raspberry Pi Pico VS Code extension)"
    }
}
if ($missing) {
    Write-Host "build: these are missing" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
$pak0 = (Resolve-Path "data\id1\pak0.pak").Path -replace '\\', '/'

# ---- board support package ------------------------------------------------
if (-not (Test-Path "third_party\wiliOGbsp\bsp\CMakeLists.txt")) {
    Invoke-Step "Get the FreeWili board support package" {
        git clone $bspUrl third_party\wiliOGbsp
        if ($LASTEXITCODE -eq 0) { git -C third_party\wiliOGbsp checkout --quiet $bspCommit }
    }
}

# ---- PC build -------------------------------------------------------------
$pcExe = "build-pc-dev\Release\quakeog_pc.exe"
Invoke-Step "Build the PC version" {
    & $cmake -S pc -B build-pc-dev -G "Visual Studio 17 2022" -A Win32 -DQOG_PC_GUNIMAGES=ON
    if ($LASTEXITCODE -eq 0) { & $cmake --build build-pc-dev --config Release --target quakeog_pc }
}

# ---- which files each level loads -----------------------------------------
$maps = switch ($Episode) {
    1 { "start", "e1m1", "e1m2", "e1m3", "e1m4", "e1m5", "e1m6", "e1m7", "e1m8" }
    2 { "start", "e2m1", "e2m2", "e2m3", "e2m4", "e2m5", "e2m6", "e2m7" }
    3 { "start", "e3m1", "e3m2", "e3m3", "e3m4", "e3m5", "e3m6", "e3m7" }
}
Invoke-Step "Record what each level loads" {
    $dir = "build-pc-dev/manifests"
    if (Test-Path $dir) { Remove-Item "$dir\*.txt" }
    New-Item -ItemType Directory -Force $dir | Out-Null
    $cfg = @("volume 0", "host_framerate 0.05", "togglemenu", "wait", "wait")
    foreach ($m in "menu_singleplayer", "menu_load", "menu_save", "menu_multiplayer", "menu_setup",
                   "menu_options", "menu_keys", "menu_video", "help", "menu_quit") {
        $cfg += $m, "wait", "wait"
    }
    $cfg += "togglemenu", "wait", "wait", "qog_manifest $dir/common.txt"
    foreach ($m in $maps) {
        $cfg += "qog_manifest_reset", "map $m"
        $cfg += @("wait") * 60
        $cfg += "qog_manifest $dir/$m.txt"
    }
    $cfg += "qog_quit"
    Set-Content -Path "data\id1\qog_manifest_record.cfg" -Value $cfg -Encoding ascii
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\pctest.ps1 -Exe $pcExe `
        -GameArgs "-nosound +exec qog_manifest_record.cfg" -Out "build-pc-dev\manifest.log" -TimeoutSec 300
    foreach ($m in @("common") + $maps) {
        if (-not (Test-Path "$dir\$m.txt")) { throw "build: no $m.txt was recorded, see build-pc-dev\manifest.log" }
    }
}

# ---- data images ----------------------------------------------------------
Invoke-Step "Make the data images" {
    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $extra = @("--gun-images")
    if ($Episode -eq 1) { $extra += "--demos" }   # the Benchmark menu plays demo3
    python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak `
        --manifests build-pc-dev\manifests --maps $maps --exe $pcExe `
        --stub-maps --main --display-no-sounds --display-no-progs @extra -o "$OutDir\qb_ep$Episode"
}

# ---- firmware -------------------------------------------------------------
Invoke-Step "Build the firmware" {
    $opts = "-DQOG_SPLIT=ON", "-DQOG_AOT=ON", "-DQOG_AOT_PAK=$pak0",
        "-DQOG_SPLIT_MAIN_EMBED=quake_display", "-DQOG_SOUND=ON",
        "-DQOG_CL_HUNK_KB=75", "-DQOG_CL_STACK_KB=32", "-DQOG_SV_HUNK_KB=174", "-DQOG_SV_STACK_KB=21",
        "-DQOG_SYS_KHZ=300000", "-DQOG_SYS_MV=1250", "-DQOG_FLASH_FAST=ON", "-DQOG_FLASH_MAX_KHZ=150000",
        "-DQOG_FAT_FORMAT_IF_MISSING=ON", "-DQOG_BOARD_TURBO=ON", "-DQOG_BOARD_HOTTEXT=ON",
        "-DQOG_BOARD_SCANSIZE=ON", "-DQOG_BOARD_RAMSCAN=ON", "-DQOG_BOARD_GUNIMAGES=ON", "-DQOG_PROF=OFF"
    & $cmake --preset target @opts
    if ($LASTEXITCODE -eq 0) { & $cmake --build build --target quake_main }
}

Write-Host ""
Write-Host "Done. To install on the board (read docs\lessons.md first)" -ForegroundColor Green
Write-Host "  python -m pip install pyserial"
Write-Host "  powershell -ExecutionPolicy Bypass -File tools\install_episode.ps1 -Episode $Episode -ImageDir $OutDir"
