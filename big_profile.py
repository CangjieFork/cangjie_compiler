#!/usr/bin/env python3
import sys, os, math
sys.argv = ["x", "/root/cj_build/cangjie_compiler/output/bin/cjc-frontend",
            "/root/cj_build/cangjie_compiler/output"]
sys.path.insert(0, "/root/cj_build")
import profile_passes as pp
os.environ["CANGJIE_HOME"] = "/root/cj_build/cangjie_compiler/output"

sizes = [400, 800, 1600, 3200]
data = {}; rsss = {}
for n in sizes:
    flat, rss, rc = pp.run_once(n)
    data[n] = flat; rsss[n] = rss
    print("n=%5d rc=%d peakRSS=%.0fMB" % (n, rc, rss), flush=True)

keys = sorted(data[3200].keys(), key=lambda k: -data[3200].get(k, 0))
hdr = "%-50s %7s %7s %7s %7s %5s" % ("pass", "n400", "n800", "n1600", "n3200", "exp")
print("\n" + hdr)
for k in keys[:20]:
    v = [data[n].get(k, 0) for n in sizes]
    exp = (math.log2(v[-1] / v[0]) / 3.0) if (v[0] > 3 and v[-1] > 3) else 0
    print("%-50s %7d %7d %7d %7d %5.2f" % (k, v[0], v[1], v[2], v[3], exp))

r = [rsss[n] for n in sizes]
print("\npeakRSS " + "/".join("%.0f" % x for x in r) + "MB  exp=%.2f" % (math.log2(r[-1] / r[0]) / 3.0))
