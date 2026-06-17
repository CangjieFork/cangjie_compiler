#!/usr/bin/env python3
import sys, os, math
sys.argv = ["x", "/root/cj_build/cangjie_compiler/output/bin/cjc-frontend",
            "/root/cj_build/cangjie_compiler/output"]
sys.path.insert(0, "/root/cj_build")
import profile_passes as pp
os.environ["CANGJIE_HOME"] = "/root/cj_build/cangjie_compiler/output"

sizes = [400, 800, 1600, 3200]
data = {}
for n in sizes:
    flat, rss, rc = pp.run_once(n)
    data[n] = flat
    print("n=%5d rc=%d" % (n, rc), flush=True)

clu_keys = sorted([k for k in data[3200] if k.startswith("CLU.")],
                  key=lambda k: -data[3200].get(k, 0))
print("\n%-40s %7s %7s %7s %7s %5s" % ("CLU sub-check", "n400", "n800", "n1600", "n3200", "exp"))
for k in clu_keys:
    v = [data[n].get(k, 0) for n in sizes]
    exp = (math.log2(v[-1] / v[0]) / 3.0) if (v[0] > 2 and v[-1] > 2) else 0
    print("%-40s %7d %7d %7d %7d %5.2f" % (k.replace("CLU.", ""), v[0], v[1], v[2], v[3], exp))
