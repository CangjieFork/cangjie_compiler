# 独立核验 cfg 门控：全 off / Foundation 子集 各 clean 构建一次，计时+rc。最后还原全 off。
$ErrorActionPreference = "Continue"
$proj = "E:\Project\CS_Project\2026\ling\windows-cj"
$tc = "dev_perf_ci"
Set-Location $proj
$env:cjHeapSize = "8gb"

function BuildMeasure($label) {
    cjv run $tc cjpm clean 2>&1 | Out-Null
    $log = "$env:TEMP\vcfg_$label.log"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath "cjv" -ArgumentList @("run",$tc,"cjpm","build","-m","windows_common","-j","8") `
        -WorkingDirectory $proj -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $peak = 0
    while (-not $p.HasExited) {
        $s = 0; Get-Process -Name cjc,cjc-frontend -ErrorAction SilentlyContinue | ForEach-Object { $s += $_.WorkingSet64 }
        if ($s -gt $peak) { $peak = $s }; Start-Sleep -Milliseconds 400
    }
    $sw.Stop()
    Write-Output ("[{0}] rc={1} wall={2:N1}s peakCjc={3:N2}GB" -f $label,$p.ExitCode,$sw.Elapsed.TotalSeconds,($peak/1GB))
    Get-Content "$log.err" -Tail 3 -ErrorAction SilentlyContinue
}

Write-Output "=== 1) 全 off (canonical 当前态) ==="
python tools/select_features.py --none 2>&1 | Out-Null
BuildMeasure "alloff"

Write-Output "=== 2) Windows.Foundation 子集 ==="
python tools/select_features.py --enable Windows.Foundation 2>&1 | Out-Null
BuildMeasure "foundation"

Write-Output "=== 3) Microsoft.UI.Composition 子集(测较大闭包) ==="
python tools/select_features.py --enable Microsoft.UI.Composition --print-closure 2>&1 | Select-Object -First 1
python tools/select_features.py --enable Microsoft.UI.Composition 2>&1 | Out-Null
BuildMeasure "composition"

Write-Output "=== 还原全 off ==="
python tools/select_features.py --none 2>&1 | Out-Null
Write-Output "done"
