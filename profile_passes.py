#!/usr/bin/env python3
"""跑修复后 cjc-frontend，对一系列规模 dump 全部 pass 计时，找下一个超线性热点。
用法: python3 profile_passes.py <cjc-frontend> <CANGJIE_HOME>
"""
import json, math, os, re, shutil, subprocess, sys, glob

FE = sys.argv[1]
CJ_HOME = sys.argv[2]
WORK = "/root/cj_build/pwork"
OUT = "/root/cj_build/pout"


def gen(ntypes, members, methods, branches, ifaces_per, iface_pool):
    """更贴近真 impl：接口池 + 每类实现多个接口 + 字段 + 多方法 + 终止符。"""
    L = ["package synth", ""]
    for i in range(iface_pool):
        L.append(f"public interface I{i} {{ func mi{i}(): Int64 }}")
    for t in range(ntypes):
        sel = sorted({(t * 7 + k) % iface_pool for k in range(ifaces_per)})
        ext = " <: " + " & ".join(f"I{s}" for s in sel) if sel else ""
        L.append(f"public class T{t}{ext} {{")
        for m in range(members):
            L.append(f"    public var f{m}: Int64 = {m}")
        L.append("    public init() {}")
        for s in sel:
            L.append(f"    public func mi{s}(): Int64 {{ return {s} }}")
        for k in range(methods):
            L.append(f"    public func m{k}(p: Int64): Int64 {{")
            L.append("        var acc: Int64 = p")
            for b in range(branches):
                L.append(f"        if (acc > {b}) {{ return acc + f{b % max(members,1)} }}")
            L.append("        return acc")
            L.append("    }")
        L.append("}")
    L.append("main(): Int64 { return 0 }")
    return "\n".join(L) + "\n"


def flatten(d, prefix=""):
    out = {}
    for k, v in d.items():
        key = f"{prefix}{k}"
        if isinstance(v, dict):
            out.update(flatten(v, key + "."))
        elif isinstance(v, (int, float)):
            out[key] = v
    return out


def run_once(n):
    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    open(os.path.join(WORK, "lib.cj"), "w").write(gen(n, 8, 4, 3, 5, 60))
    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    env = dict(os.environ); env["CANGJIE_HOME"] = CJ_HOME
    cmd = ["/usr/bin/time", "-v", FE, "-p", WORK, "--output-dir=" + OUT,
           "--experimental", "-Woff", "unused", "--profile-compile-time", "-o=lib.bc"]
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    rss = -1
    for line in p.stderr.splitlines():
        m = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", line)
        if m: rss = int(m.group(1)) / 1024.0
    pf = glob.glob(os.path.join(OUT, "*.time.prof"))
    flat = flatten(json.load(open(pf[0]))) if pf else {}
    return flat, rss, p.returncode


def main():
    sizes = [200, 400, 800, 1600]
    data = {}; rsss = {}
    for n in sizes:
        flat, rss, rc = run_once(n)
        data[n] = flat; rsss[n] = rss
        print(f"n={n:5d} rc={rc} peakRSS={rss:.0f}MB total={flat.get('Main Stage',-1)}ms", flush=True)
    # 找叶子 pass(无子项的最深键)中 n=1600 最大的 + 伸缩指数
    keys = sorted(data[1600].keys(), key=lambda k: -data[1600].get(k, 0))
    print(f"\n{'pass':52s} {'n200':>7} {'n400':>7} {'n800':>7} {'n1600':>7}  {'exp':>5}")
    for k in keys[:22]:
        v = [data[n].get(k, 0) for n in sizes]
        # 伸缩指数: log2(v[-1]/v[0]) / log2(1600/200)=3   (1=线性,2=二次)
        exp = (math.log2(v[-1] / v[0]) / 3.0) if (v[0] > 2 and v[-1] > 2) else 0
        print(f"{k:52s} {v[0]:>7} {v[1]:>7} {v[2]:>7} {v[3]:>7}  {exp:>5.2f}")
    print(f"\npeakRSS: " + "  ".join(f"n{n}={rsss[n]:.0f}MB" for n in sizes))
    r = [rsss[n] for n in sizes]
    if r[0] > 0:
        print(f"RSS 伸缩指数: {math.log2(r[-1]/r[0])/3.0:.2f} (1=线性,2=二次)")


if __name__ == "__main__":
    main()
