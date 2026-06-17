#!/usr/bin/env python3
"""测 AddDependencyImpl 的 O(deps^2)：一个函数引用 N 个全局变量 -> 单 decl 有 N 个依赖。
看 SortGlobalVarDecl 随 N 的伸缩。"""
import sys, os, math, shutil, glob, json, re, subprocess
FE = "/root/cj_build/cangjie_compiler/output/bin/cjc-frontend"
CJ_HOME = "/root/cj_build/cangjie_compiler/output"
WORK = "/root/cj_build/depwork"; OUT = "/root/cj_build/depout"


def gen(n):
    """N 个全局 let + 一个 sink 函数引用全部（产生 N 个依赖）+ 再来一批互相引用的全局。"""
    L = ["package synth", ""]
    for i in range(n):
        L.append(f"let g{i}: Int64 = {i}")
    # sink 函数引用全部 g（单个 decl 的依赖 = N）
    L.append("func sink(): Int64 {")
    L.append("    var s: Int64 = 0")
    for i in range(n):
        L.append(f"    s = s + g{i}")
    L.append("    return s")
    L.append("}")
    L.append("main(): Int64 { return sink() }")
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
    flat = {}
    pf = glob.glob(os.path.join(OUT, "*.time.prof"))
    if pf:
        def fl(d, pre=""):
            for k, v in d.items():
                if isinstance(v, dict): fl(v, pre + k + ".")
                elif isinstance(v, (int, float)): flat[pre + k] = v
        fl(json.load(open(pf[0])))
    return flat, p.returncode


sizes = [500, 1000, 2000, 4000]
data = {}
print("单函数引用 N 个全局变量（单 decl N 依赖）")
for n in sizes:
    flat, rc = run(gen(n))
    data[n] = flat
    print("N=%5d rc=%d SortGlobalVarDecl=%dms AST2CHIR=%dms" %
          (n, rc, flat.get("AST to CHIR Translation.SortGlobalVarDecl", -1),
           flat.get("Main Stage.CHIR", -1)), flush=True)
v = [data[n].get("AST to CHIR Translation.SortGlobalVarDecl", 0) for n in sizes]
if v[0] > 2 and v[-1] > 2:
    print("SortGlobalVarDecl exp(over N, 8x) = %.2f (1=线性,2=二次)" % (math.log2(v[-1] / v[0]) / 3.0))
