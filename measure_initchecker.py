#!/usr/bin/env python3
"""测量 InitChecker 优化对编译 CPU + 内存的影响。
用法: python3 measure_initchecker.py <cjc-frontend> <CANGJIE_HOME> <label>
对一系列 ntypes 生成干净的压测包，跑 frontend(--profile-compile-time)，
采集 Post TypeCheck.CheckLegalityOfUsage(ms) 与进程峰值 RSS(MB)。
"""
import json, os, re, shutil, subprocess, sys, glob

FE = sys.argv[1]
CJ_HOME = sys.argv[2]
LABEL = sys.argv[3] if len(sys.argv) > 3 else "?"
WORK = "/root/cj_build/mwork"
OUT = "/root/cj_build/mout"


def gen(ntypes, members, methods, branches):
    """干净的压测形状：ntypes 个独立类；每类 members 个字段 + methods 个方法；
    每个方法含局部 var + branches 个 if(return) 终止符 + while/break(JUMP 终止符)。
    无继承/无 Exception，保证 0 error，纯压 UpdateScopeStatus 终止符路径。"""
    L = ["package synth", ""]
    for t in range(ntypes):
        L.append(f"public class T{t} {{")
        for m in range(members):
            L.append(f"    public var f{m}: Int64 = {m}")
        L.append("    public init() {}")
        for k in range(methods):
            L.append(f"    public func m{k}(p: Int64): Int64 {{")
            L.append("        var acc: Int64 = p")
            for b in range(branches):
                L.append(f"        if (acc > {b}) {{ return acc + f{b % max(members,1)} }}")
            L.append("        var i: Int64 = 0")
            L.append("        while (i < p) { if (i > 5) { break }; i = i + 1 }")
            L.append("        return acc")
            L.append("    }")
        L.append("}")
    L.append("main(): Int64 { return 0 }")
    return "\n".join(L) + "\n"


def run_once(src):
    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    open(os.path.join(WORK, "lib.cj"), "w").write(src)
    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    env = dict(os.environ)
    env["CANGJIE_HOME"] = CJ_HOME
    cmd = ["/usr/bin/time", "-v", FE, "-p", WORK, "--output-dir=" + OUT,
           "--experimental", "-Woff", "unused", "--profile-compile-time", "-o=lib.bc"]
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    # peak RSS
    rss_kb = -1
    for line in p.stderr.splitlines():
        m = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", line)
        if m:
            rss_kb = int(m.group(1))
    # CLU time
    clu = tc = -1
    pf = glob.glob(os.path.join(OUT, "*.time.prof"))
    if pf:
        d = json.load(open(pf[0]))
        clu = d.get("Post TypeCheck", {}).get("CheckLegalityOfUsage", -1)
        tc = d.get("Semantic", {}).get("TypeCheck", -1)
    errs = p.stdout.count("error:") + sum("error" in l for l in p.stderr.splitlines() if "resident" not in l and "Maximum" not in l)
    return clu, tc, rss_kb / 1024.0, p.returncode, p.stdout


def main():
    print(f"### LABEL={LABEL}  FE={FE}", flush=True)
    print(f"{'ntypes':>7} {'CLU(ms)':>9} {'TypeCheck(ms)':>14} {'peakRSS(MB)':>12} {'rc':>3}  scaling", flush=True)
    prev = None
    for n in [100, 200, 400, 800, 1600]:
        clu, tc, rss, rc, out = run_once(gen(n, 8, 4, 3))
        sc = f"x{clu/prev:.2f}" if (prev and prev > 0 and clu > 0) else ""
        print(f"{n:>7} {clu:>9} {tc:>14} {rss:>12.1f} {rc:>3}  {sc}", flush=True)
        if rc != 0:
            errlines = [l for l in out.splitlines() if "error" in l.lower()][:3]
            print("   ERR:", errlines, flush=True)
        prev = clu


if __name__ == "__main__":
    main()
