#!/usr/bin/env python3
"""按继承深度扫描，找随深度超线性的 pass（贴近真 impl 的深 COM 继承链）。
固定总类数 ~N，变继承深度 D：N/D 条链，每条 depth D。每类覆写接口方法(vtable)。
用法: python3 deep_profile.py
"""
import sys, os, math, shutil, glob, json, re, subprocess
FE = "/root/cj_build/cangjie_compiler/output/bin/cjc-frontend"
CJ_HOME = "/root/cj_build/cangjie_compiler/output"
WORK = "/root/cj_build/dwork"; OUT = "/root/cj_build/dout"


def gen_deep(total, depth, methods):
    """total 个类，分成 total/depth 条继承链，每条深度 depth。
    根类实现接口 IBase(methods 个方法)，每层 override 之并加自己的方法。"""
    L = ["package synth", ""]
    L.append("public interface IBase {")
    for m in range(methods):
        L.append(f"    func mb{m}(): Int64")
    L.append("}")
    chains = max(total // depth, 1)
    cid = 0
    for c in range(chains):
        for d in range(depth):
            if d == 0:
                L.append(f"public open class C{cid} <: IBase {{")
                L.append("    public init() {}")
                for m in range(methods):
                    L.append(f"    public open func mb{m}(): Int64 {{ return {m} }}")
            else:
                L.append(f"public open class C{cid} <: C{cid-1} {{")
                L.append("    public init() { super() }")
                for m in range(methods):
                    L.append(f"    public open override func mb{m}(): Int64 {{ return super.mb{m}() + {d} }}")
                L.append(f"    public func own{cid}(p: Int64): Int64 {{ var a = p; if (a > 0) {{ return a }}; return 0 }}")
            L.append("}")
            cid += 1
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


TOTAL = 1600
print("固定 total=%d 类，变继承深度 D（每类 methods=4 接口方法 override）" % TOTAL)
depths = [1, 4, 16, 64]
data = {}; rsss = {}
for D in depths:
    flat, rss, rc = run(gen_deep(TOTAL, D, 4))
    data[D] = flat; rsss[D] = rss
    print("D=%3d rc=%d peakRSS=%.0fMB MainStage=%dms" %
          (D, rc, rss, sum(v for k, v in flat.items() if k.startswith("Main Stage."))), flush=True)

keys = sorted(data[64].keys(), key=lambda k: -data[64].get(k, 0))
print("\n%-50s %6s %6s %6s %6s %5s" % ("pass", "D1", "D4", "D16", "D64", "exp"))
for k in keys[:20]:
    v = [data[D].get(k, 0) for D in depths]
    # exp over depth 1->64 = 6 doublings
    exp = (math.log2(v[-1] / v[0]) / 6.0) if (v[0] > 3 and v[-1] > 3) else 0
    print("%-50s %6d %6d %6d %6d %5.2f" % (k, v[0], v[1], v[2], v[3], exp))
print("\npeakRSS " + "/".join("%.0f" % rsss[D] for D in depths) + "MB")
