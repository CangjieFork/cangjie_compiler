# 用 dev_perf_ci 在 windows_common 上测多种 cjc 编译选项变体的 wall + 峰值内存。
# 变体通过临时改写 windows_common/cjpm.toml 的 compile-option 注入，跑完还原。
# 规则：禁止 .sh；本脚本为 .ps1（与 measure_impl.ps1 同），符合"自动化用脚本但非 sh"。
$ErrorActionPreference = "Continue"
$proj = "E:\Project\CS_Project\2026\ling\windows-cj"
$toml = "$proj\windows_common\cjpm.toml"
$tc   = "dev_perf_ci"
Set-Location $proj
$env:cjHeapSize = "8gb"

$base = '--experimental --incremental-compile -Woff unused'
$variants = @(
    @{ name = "profile"; opt = "$base --profile-compile-time --profile-compile-memory" },
    @{ name = "apc8";    opt = "$base --apc=8" }
)

$orig = Get-Content $toml -Raw
function Set-Opt($optStr) {
    $new = $orig -replace 'compile-option = "[^"]*"', ("compile-option = `"" + $optStr + "`"")
    Set-Content -Path $toml -Value $new -NoNewline
}

try {
    foreach ($v in $variants) {
        Write-Output "==================== variant=$($v.name) ===================="
        Write-Output "opt = $($v.opt)"
        Set-Opt $v.opt
        cjv run $tc cjpm clean 2>&1 | Out-Null
        $log = "$env:TEMP\impl_variant_$($v.name).log"
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process -FilePath "cjv" -ArgumentList @("run", $tc, "cjpm", "build", "-m", "windows_common", "-j", "8") `
            -WorkingDirectory $proj -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
        $peak = 0
        while (-not $p.HasExited) {
            $sum = 0
            Get-Process -Name cjc, cjc-frontend -ErrorAction SilentlyContinue | ForEach-Object { $sum += $_.WorkingSet64 }
            if ($sum -gt $peak) { $peak = $sum }
            Start-Sleep -Milliseconds 400
        }
        $sw.Stop()
        Write-Output ("[$($v.name)] EXIT=$($p.ExitCode)  wall={0:N1}s  peakCjcMem={1:N2}GB" -f $sw.Elapsed.TotalSeconds, ($peak / 1GB))
        Write-Output "--- log tail ---"
        Get-Content $log -Tail 3 -ErrorAction SilentlyContinue
        Get-Content "$log.err" -Tail 4 -ErrorAction SilentlyContinue
    }
}
finally {
    Set-Content -Path $toml -Value $orig -NoNewline
    Write-Output "cjpm.toml restored."
}
