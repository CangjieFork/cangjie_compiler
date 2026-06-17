# 仅用 --profile-compile-time 抓 windows_common 各包(尤其 impl)的 per-phase 耗时。
# cjHeapSize 提到 16gb 规避 cjpm 自身 GC 的 "mutator list lock timeout"。
$ErrorActionPreference = "Continue"
$proj = "E:\Project\CS_Project\2026\ling\windows-cj"
$toml = "$proj\windows_common\cjpm.toml"
$tc   = "dev_perf_ci"
Set-Location $proj
$env:cjHeapSize = "16gb"

$orig = Get-Content $toml -Raw
$opt  = '--experimental --incremental-compile -Woff unused --profile-compile-time'
try {
    $new = $orig -replace 'compile-option = "[^"]*"', ("compile-option = `"" + $opt + "`"")
    Set-Content -Path $toml -Value $new -NoNewline
    cjv run $tc cjpm clean 2>&1 | Out-Null
    $log = "$env:TEMP\impl_profile.log"
    cjv run $tc cjpm build -m windows_common -j 8 > $log 2> "$log.err"
    Write-Output "EXIT=$LASTEXITCODE  log=$log"
    Write-Output "--- err tail ---"
    Get-Content "$log.err" -Tail 5 -ErrorAction SilentlyContinue
}
finally {
    Set-Content -Path $toml -Value $orig -NoNewline
    Write-Output "restored."
}
