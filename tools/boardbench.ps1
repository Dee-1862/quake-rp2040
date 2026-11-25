# Predicted board speed of timedemo demo3 for this worktree (Agent AS).
#
#   powershell -File tools\boardbench.ps1                  # Fast and Turbo at 300 MHz, LCD by PIO at 60 MHz (Agent AP)
#   powershell -File tools\boardbench.ps1 -Lcd pl022 -Spi 50   # the SPI peripheral at 300 MHz
#   powershell -File tools\boardbench.ps1 -Mhz 250 -Spi 62.5 -Lcd pl022
#   powershell -File tools\boardbench.ps1 -Ref agent-am    # another branch, merged with this tool in a temporary worktree
#   powershell -File tools\boardbench.ps1 -WhatIf "notify rows only=nonotify;spans x0.7=spans*0.7"
#
# Builds quakeog_pc with the board client's render flags and the cost
# model's work counts (-DQOG_PC_BOARDMODEL=ON, its own folder
# build-boardbench), plays timedemo demo3 in Fast (r_fast 1) and Turbo
# (r_fast 2) hidden and silent through tools/pctest.ps1, and hands the
# per-frame counts to tools/boardmodel.py, which prints the board ms per
# phase, the scanout waits and the predicted fps with a 10 to 90% range.
# The calibration (tools/boardmodel_calib.json) is the board's own run of
# de58316. Nothing here touches the board or a COM port.
#
# Needs data\id1\pak0.pak and pak1.pak (registered data) and the
# third_party junction, as every PC build does. A run takes about 3 minutes
# the first time (the build) and about 1 minute after.
param(
    [double]$Mhz = 300,
    [double]$Spi = 60,
    [ValidateSet("pio", "pl022")][string]$Lcd = "pio",
    [int]$Samples = 30,
    [string]$Ref = "",
    [string]$WhatIf = "",       # "label=item,item;label=item": several what-ifs separated by ;
    [switch]$NoBuild,
    [string]$Json = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Invoke-Bench([string]$tree) {
    Push-Location $tree
    try {
        foreach ($f in "data\id1\pak0.pak", "data\id1\pak1.pak") {
            if (-not (Test-Path $f)) { throw "boardbench: $tree\$f is missing (copy the registered paks, never commit them)" }
        }
        if (-not (Test-Path "third_party")) { throw "boardbench: $tree\third_party is missing (make the junction to the main checkout's third_party)" }
        $bdir = "build-boardbench"
        if (-not $NoBuild) {
            if (-not (Test-Path "$bdir\CMakeCache.txt")) {
                cmake -S pc -B $bdir -G "Visual Studio 17 2022" -A Win32 -DQOG_AOT=ON -DQOG_PC_BOARDMODEL=ON | Out-Null
            }
            cmake --build $bdir --config Release --target quakeog_pc | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "boardbench: the build failed (cmake --build $bdir --config Release --target quakeog_pc)" }
        }
        $exe = "$bdir\Release\quakeog_pc.exe"
        if (-not (Test-Path $exe)) { throw "boardbench: no $exe" }
        Copy-Item "$root\tools\boardbench_config.cfg" "$bdir\fixed_config.cfg" -Force
        $logs = @()
        foreach ($m in @(@("fast", 1), @("turbo", 2))) {
            $name = $m[0]; $rf = $m[1]
            Set-Content -Encoding ascii "data\id1\qogtest_bb_$name.cfg" "r_fast $rf`nqog_quitafter 1400`ntimedemo demo3`n"
            $log = Join-Path (Resolve-Path $bdir) "bm_$name.csv"
            $out = Join-Path (Resolve-Path $bdir) "bb_$name.txt"
            if (Test-Path $log) { Remove-Item $log }
            & powershell -NoProfile -File "$tree\tools\pctest.ps1" -Exe $exe -GameArgs "-nosound -bmlog $log +exec qogtest_bb_$name.cfg" `
                -Out $out -Config "$bdir\fixed_config.cfg" -TimeoutSec 300 | Out-Host
            $done = Select-String -Path $out -Pattern "^\s*(\d+) frames\s+([\d.]+) seconds" | Select-Object -First 1
            if (-not $done -or -not (Test-Path $log)) { throw "boardbench: timedemo demo3 did not finish in the $name run (see $out)" }
            $logs += $log
        }
        return $logs
    } finally {
        Pop-Location
    }
}

$tree = $root
$tmp = ""
if ($Ref) {
    # another branch: a temporary worktree next to this one (short path, the
    # engine's paths are at most 128 characters) with this tool merged in
    $tmp = Join-Path (Split-Path -Parent $root) ("qog-bb-" + ($Ref -replace '[^A-Za-z0-9]', ''))
    if (Test-Path $tmp) { throw "boardbench: $tmp exists, remove it first (cmd /c rmdir $tmp\third_party, then git worktree remove --force $tmp)" }
    $me = (git rev-parse HEAD).Trim()
    git worktree add --detach $tmp $Ref | Out-Null
    $hasTool = Test-Path "$tmp\port\pc\boardmodel_pc.c"
    if (-not $hasTool) {
        git -C $tmp merge --no-edit --no-commit $me 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "boardbench: $Ref does not merge with $me cleanly (resolve by hand in $tmp)" }
    }
    $tp = (Get-Item "$root\third_party").Target
    if ($tp -is [array]) { $tp = $tp[0] }
    if (-not $tp) { $tp = "$root\third_party" }
    cmd /c mklink /J "$tmp\third_party" "$tp" | Out-Null
    New-Item -ItemType Directory -Force "$tmp\data\id1" | Out-Null
    Copy-Item "$root\data\id1\pak0.pak", "$root\data\id1\pak1.pak" "$tmp\data\id1\"
    $tree = $tmp
}
try {
    $logs = Invoke-Bench $tree
    $args2 = @("tools\boardmodel.py", "predict", "--calib", "tools\boardmodel_calib.json",
               "--log", $logs[0], "--log", $logs[1], "--names", "Fast,Turbo",
               "--mhz", "$Mhz", "--spi", "$Spi", "--lcd", $Lcd, "--samples", "$Samples")
    foreach ($w in ($WhatIf -split ";")) { if ($w.Trim()) { $args2 += @("--whatif", $w.Trim()) } }
    if ($Json) { $args2 += @("--json", $Json) }
    $label = if ($Ref) { $Ref } else { (git rev-parse --short HEAD).Trim() + " (" + (git rev-parse --abbrev-ref HEAD).Trim() + ")" }
    Write-Output "boardbench: $label, timedemo demo3 on the PC, predicted for the board"
    & python @args2
    if ($LASTEXITCODE -ne 0) { throw "boardbench: tools/boardmodel.py failed" }
} finally {
    if ($tmp -and (Test-Path $tmp)) {
        cmd /c rmdir "$tmp\third_party" | Out-Null
        git worktree remove --force $tmp | Out-Null
    }
}
