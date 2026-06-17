#!/usr/bin/env python3
"""按"每类实现的接口数"扫描（COM 类实现大量接口的模式），找接口宽度驱动的超线性 pass。
固定类数，变每类实现接口数 W。"""
import sys, os, math, shutil, glob, json, re, subprocess
FE = "/root/cj_build/cangjie_compiler/output/bin/cjc-frontend"
CJ_HOME = "/root/cj_build/cangjie_compiler/output"
WORK = "/root/cj_build/wwork"; OUT = "/root/cj_build/wout"


def gen(nclasses, width, pool, methods_per_iface):
    """pool 个接口；每类实现 width 个接口（各 methods_per_iface 方法）并实现全部方法。"""
    L = ["package synth", ""]
    for i in range(pool):
        L.append(f"public interface I{i} {{")
        for m in range(methods_per_iface):
            L.append(f"    func i{i}m{m}(): Int64")
        L.append("}")
    for c in range(nclasses):
        sel = [(c * 13 + k) % pool for k in range(width)]
        sel = sorted(set(sel))
        ext = " <: " + " & ".join(f"I{s}" for s in sel)
        L.append(f"public class K{c}{ext} {{")
        L.append("    public init() {}")
        for s in sel:
            for m in range(methods_per_iface):
                L.append(f"    public func i{s}m{m}(): Int64 {{ return {s}+{m} }}")
        L.append("}")
    L.append("main(): Int64 { return 0 }")
    return "\n".join(L) + "\n"


def run(src):
    for d in (WORK, OUT):
        if os.path.exists(d): shutil.rmtree(d)
        os.makedirs(d)
    open(os.path.join(WORK, "lib.cj"), "w").write(src)
    env = dict(os.environ); env["CANGJIE_HOME"] = CJ_HOME
    cmd = ["/usr/bin/time", "-v", FE, "-p", WORK, "--output-dir=" + OUT,
           "--experimental", "-Woff", "unused", "--profile-compile-time", "-o=lib.bc"]
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    rss = -1
    for line in p.stderr.splitlines():
        m = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", line)
        if m: rss = int(m.group(1)) / 1024.0
    flat = {}
    pf = glob.glob(os.path.join(OUT, "*.time.prof"))
    if pf:
        def fl(d, pre=""):
            for k, v in d.items():
                if isinstance(v, dict): fl(v, pre + k + ".")
                elif isinstance(v, (int, float)): flat[pre + k] = v
        fl(json.load(open(pf[0])))
    return flat, rss, p.returncode


NCLS = 400
widths = [1, 4, 16, 48]
print("固定 %d 类，变每类接口数 W（每接口2方法，池64）" % NCLS)
data = {}; rsss = {}
for W in widths:
    flat, rss, rc = run(gen(NCLS, W, 64, 2))
    data[W] = flat; rsss[W] = rss
    print("W=%3d rc=%d peakRSS=%.0fMB MainStage=%dms" %
          (W, rc, rss, sum(v for k, v in flat.items() if k.startswith("Main Stage."))), flush=True)
keys = sorted(data[48].keys(), key=lambda k: -data[48].get(k, 0))
print("\n%-50s %6s %6s %6s %6s %5s" % ("pass", "W1", "W4", "W16", "W48", "exp"))
for k in keys[:18]:
    v = [data[W].get(k, 0) for W in widths]
    exp = (math.log2(v[-1] / v[0]) / math.log2(48)) if (v[0] > 3 and v[-1] > 3) else 0
    print("%-50s %6d %6d %6d %6d %5.2f" % (k, v[0], v[1], v[2], v[3], exp))
print("\npeakRSS " + "/".join("%.0f" % rsss[W] for W in widths) + "MB")
