#!/usr/bin/env python3
"""交叉构建 Windows cjc.exe + libs（跳过 cjdb），用 cangjie-build 的 windows-x64 env。
host cjc 已构建(dev_perf, ext4) -> host build 步为增量 no-op。
用法: python3 run_cross_build.py            # 全部步骤
      python3 run_cross_build.py <args...>  # 自定义 build.py 调用
"""
import os, sys, subprocess
sys.path.insert(0, "/root/cangjie-build/src")
from pathlib import Path
from cangjie_build.config import build_config
from cangjie_build.stages._common import merged_env
from cangjie_build.toolchain import mingw

cfg = build_config(
    workspace=Path("/root/cj_build"),
    build_root=Path("/root/cj_build/buildtools"),
    target_key="windows-x64",
    build_type="release",
    cangjie_version="0.0.0-dev",
)
env = dict(os.environ)
env.update(merged_env(cfg))
repo = Path("/root/cj_build/cangjie_compiler")
mingw_path = mingw.install_path(cfg.build_root)
sysroot = f"{mingw_path}/"
toolchain = str(mingw_path / "bin")
VER = "0.0.0-dev"
WT = "windows-x86_64"


def run(args, label):
    print(f"\n===== {label}: build.py {' '.join(args)} =====", flush=True)
    rc = subprocess.call([sys.executable, "build.py", *args], cwd=repo, env=env)
    print(f"===== {label} EXIT {rc} =====", flush=True)
    if rc != 0:
        sys.exit(rc)


if len(sys.argv) > 1:
    run(sys.argv[1:], "custom")
    sys.exit(0)

# 1) host cjc（增量，dev_perf 已建则 no-op）。用 --product cjc 避开 flatbuffers flattests
#    (third_party，不受 src/CMakeLists.txt 的 -Wno-error 覆盖，clang-15 -Werror 编不过)。
run(["build", "-t", "release", "--product", "cjc", "--no-tests", "-v", VER, "-j", "14", "--link-jobs", "4"], "host-cjc")
# 2) windows cjc.exe（无 cjdb），触发 cjnative LLVM 的 windows 交叉构建
run(["build", "-t", "release", "--product", "cjc", "--no-tests", "-v", VER,
     "--target", WT, "--target-sysroot", sysroot, "--target-toolchain", toolchain,
     "-j", "14", "--link-jobs", "4"], "win-cjc")
# 3) windows libs
run(["build", "-t", "release", "--product", "libs", "-v", VER,
     "--target", WT, "--target-sysroot", sysroot, "--target-toolchain", toolchain,
     "-j", "14", "--link-jobs", "4"], "win-libs")
# 4) install
run(["install", "--host", WT], "install-win")
run(["install"], "install-host")
print("\nALL CROSS COMPILER STEPS DONE", flush=True)
