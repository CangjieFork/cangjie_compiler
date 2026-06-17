param(
    [Parameter(Mandatory = $true)][string]$Toolchain,
    [Parameter(Mandatory = $true)][string]$Label
)
# 测量用指定工具链编译 windows_common(=deps + 222k 行 impl 巨石) 的墙钟时间 + cjc 峰值内存。
$ErrorActionPreference = "Continue"
$proj = "E:\Project\CS_Project\2026\ling\windows-cj"
Set-Location $proj
$env:cjHeapSize = "8gb"   # 仅约束 cjpm(仓颉程序)；cjc 是 C++ 不受限

Write-Output "[$Label] toolchain=$Toolchain  cleaning..."
cjv run $Toolchain cjpm clean 2>&1 | Out-Null

$log = "$env:TEMP\impl_build_$Label.log"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath "cjv" -ArgumentList @("run", $Toolchain, "cjpm", "build", "-m", "windows_common", "-j", "8") `
    -WorkingDirectory $proj -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
$peak = 0
while (-not $p.HasExited) {
    $sum = 0
    Get-Process -Name cjc, cjc-frontend -ErrorAction SilentlyContinue | ForEach-Object { $sum += $_.WorkingSet64 }
    if ($sum -gt $peak) { $peak = $sum }
    Start-Sleep -Milliseconds 400
}
$sw.Stop()
$rc = $p.ExitCode
Write-Output ("[$Label] EXIT=$rc  wall={0:N1}s  peakCjcMem={1:N2}GB" -f $sw.Elapsed.TotalSeconds, ($peak / 1GB))
Write-Output "--- last log lines ---"
Get-Content $log -Tail 4 -ErrorAction SilentlyContinue
Get-Content "$log.err" -Tail 6 -ErrorAction SilentlyContinue
