#!/usr/bin/env python3
"""单二进制全前端测量：Main Stage 总时长 + CLU + ReportUnusedCode + 峰值 RSS。
用法: python3 one_measure.py <cjc-frontend> <label>"""
import sys, os
FE = sys.argv[1]
LABEL = sys.argv[2]
sys.argv = ["x", FE, "/root/cj_build/cangjie_compiler/output"]
sys.path.insert(0, "/root/cj_build")
import profile_passes as pp
os.environ["CANGJIE_HOME"] = "/root/cj_build/cangjie_compiler/output"
pp.FE = FE

print("LABEL=%s FE=%s" % (LABEL, FE), flush=True)
for n in [400, 800, 1600, 3200]:
    flat, rss, rc = pp.run_once(n)
    main_total = sum(v for k, v in flat.items() if k.startswith("Main Stage."))
    clu = flat.get("Post TypeCheck.CheckLegalityOfUsage", -1)
    ruc = flat.get("RulesChecking.ReportUnusedCode", -1)
    print("n=%5d rc=%d MainStage=%7dms CLU=%6dms ReportUnusedCode=%5dms peakRSS=%6.0fMB"
          % (n, rc, main_total, clu, ruc, rss), flush=True)
