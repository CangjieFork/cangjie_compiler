#!/usr/bin/env python3
"""CodeGen regression checks for generic payload barrier emission.

Usage: test_generic_payload_ir.py <cjc> <output-dir>
The test inspects product IR from the given cjc; it does not reimplement
code generation. The CPointer.read(idx) pass/fail pin is the product
intrinsic in the std.core user function slice, not the wrapper symbol alone.
"""
import os
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parent


def emit(cjc: str, source: pathlib.Path, out_dir: pathlib.Path) -> str:
    out_dir.mkdir(parents=True, exist_ok=True)
    output = out_dir / source.stem
    proc = subprocess.run(
        [cjc, "-g", "--output-type=staticlib", "--dump-ir", "--dump-to-screen", "-o", str(output), str(source)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if proc.returncode != 0:
        raise AssertionError(f"{source.name}: cjc failed ({proc.returncode})\n{proc.stdout}")
    (out_dir / f"{source.stem}.ir").write_text(proc.stdout)
    return proc.stdout


def function_slice(ir: str, needle: str) -> str:
    matches = list(re.finditer(r"^define .*" + re.escape(needle), ir, re.MULTILINE))
    start = matches[0].start() if matches else -1
    if start < 0:
        raise AssertionError(f"missing generated function {needle}")
    end = ir.find("\ndefine ", start + 1)
    return ir[start:] if end < 0 else ir[start:end]


def core_ir(cjc: str) -> str:
    home = pathlib.Path(os.environ.get("CANGJIE_HOME", pathlib.Path(cjc).resolve().parents[1]))
    candidates = [
        home / "modules/linux_x86_64_cjnative_bc/std/libstd.core.bc",
        home / "modules/linux_x86_64_cjnative/std/libstd.core.bc",
    ]
    bitcode = next((path for path in candidates if path.is_file()), None)
    if bitcode is None:
        raise AssertionError(f"missing libstd.core.bc below {home}")
    llvm_dis = os.environ.get("LLVM_DIS", "llvm-dis")
    proc = subprocess.run(
        [llvm_dis, str(bitcode), "-o", "-"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if proc.returncode != 0:
        raise AssertionError(f"llvm-dis failed ({proc.returncode})\n{proc.stdout}")
    return proc.stdout


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    cjc, out_dir = sys.argv[1], pathlib.Path(sys.argv[2])

    read_ir = emit(cjc, ROOT / "GenericPayloadCPointerRead.cj", out_dir)
    read_fn = function_slice(read_ir, "readGeneric")
    assert "_CNatXPG_4readHv" in read_fn
    assert "llvm.memcpy" not in read_fn

    indexed_read_fn = function_slice(core_ir(cjc), "_CNatXPG_4readHl")
    if "llvm.cj.gcwrite.generic.payload" not in indexed_read_fn:
        raise AssertionError("missing product payload helper (CPointer.read(idx) user function)")
    if "llvm.memcpy" in indexed_read_fn:
        raise AssertionError("CPointer.read(idx) user function retained llvm.memcpy")

    wrapping_ir = emit(cjc, ROOT / "GenericPayloadWithoutTI.cj", out_dir)
    wrapping_fn = function_slice(wrapping_ir, "$withoutTI")
    assert "llvm.cj.gcwrite.generic" in wrapping_fn
    assert "llvm.memcpy" not in wrapping_fn

    write_ir = emit(cjc, ROOT / "GenericPayloadCPointerWrite.cj", out_dir)
    write_fn = function_slice(write_ir, "writeGeneric")
    assert "_CNatXPG_5write" in write_fn
    assert "llvm.cj.gcwrite.generic.payload" not in write_fn
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
