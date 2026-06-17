#!/usr/bin/env python3
"""最终合并对照：原始 gitcode 基线(两个 O(N^2) 都在) vs 双修复版。
测全前端 Main Stage 总时长 + CLU + ReportUnusedCode + 峰值 RSS。"""
import sys, os, math
sys.argv = ["x", "x", "x"]
sys.path.insert(0, "/root/cj_build")
import profile_passes as pp
os.environ["CANGJIE_HOME"] = "/root/cj_build/cangjie_compiler/output"

BASELINE = "/root/cj_build/old_bin/cjc-frontend"   # 原始(双 bug)
OPT = "/root/cj_build/cangjie_compiler/output/bin/cjc-frontend"  # 双修复
sizes = [400, 800, 1600, 3200]


def measure(fe):
    pp.FE = fe
    out = {}
    for n in sizes:
        flat, rss, rc = pp.run_once(n)
        out[n] = (flat.get("Main Stage", -1) if isinstance(flat.get("Main Stage"), (int, float)) else
                  sum(v for k, v in flat.items() if k.startswith("Main Stage.")),
                  flat.get("Post TypeCheck.CheckLegalityOfUsage", -1),
                  flat.get("RulesChecking.ReportUnusedCode", -1),
                  rss, rc)
    return out


print("measuring BASELINE (original gitcode, both O(N^2) present)...", flush=True)
b = measure(BASELINE)
print("measuring OPTIMIZED (InitChecker + ReportUnusedCode fixed)...", flush=True)
o = measure(OPT)

print("\n=== Full frontend compile (Main Stage total, ms) ===")
print("%6s %12s %12s %9s" % ("ntypes", "BASELINE", "OPTIMIZED", "speedup"))
for n in sizes:
    bt, ot = b[n][0], o[n][0]
    print("%6d %12d %12d %8.1fx" % (n, bt, ot, bt / ot if ot else 0))

print("\n=== peak RSS (MB) ===")
print("%6s %12s %12s %9s" % ("ntypes", "BASELINE", "OPTIMIZED", "ratio"))
for n in sizes:
    br, orr = b[n][3], o[n][3]
    print("%6d %12.0f %12.0f %8.2fx" % (n, br, orr, br / orr if orr else 0))

print("\n=== 两个被修复 pass 明细 (ms) ===")
print("%6s | CLU base/opt | ReportUnusedCode base/opt" % "ntypes")
for n in sizes:
    print("%6d | %6d / %-6d | %6d / %-6d" % (n, b[n][1], o[n][1], b[n][2], o[n][2]))
