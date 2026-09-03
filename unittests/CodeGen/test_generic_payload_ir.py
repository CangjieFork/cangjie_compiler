#!/usr/bin/env python3
"""CodeGen regression checks for generic payload barrier emission.

Usage: test_generic_payload_ir.py <cjc> <output-dir>
The test intentionally inspects product IR emitted by cjc; it does not reimplement
the code-generation logic in the test process.
"""
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


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    cjc, out_dir = sys.argv[1], pathlib.Path(sys.argv[2])

    read_ir = emit(cjc, ROOT / "GenericPayloadCPointerRead.cj", out_dir)
    read_fn = function_slice(read_ir, "readGeneric")
    # The user generic function must retain the product intrinsic entry point;
    # its helper body is supplied by std.core and is checked transitively by the
    # callee symbol in the same emitted module.
    assert "_CNatXPG_4readHv" in read_fn
    assert "llvm.memcpy" not in read_fn

    wrapping_ir = emit(cjc, ROOT / "GenericPayloadWithoutTI.cj", out_dir)
    wrapping_fn = function_slice(wrapping_ir, "$withoutTI")
    assert "llvm.cj.gcwrite.generic" in wrapping_fn
    assert "llvm.memcpy" not in wrapping_fn

    # CPointerWrite currently has no runtime read-side generic helper.  Keep a
    # product-IR witness of the existing memcpy path so a future runtime change
    # can tighten this assertion without rebuilding a test-only copy.
    write_ir = emit(cjc, ROOT / "GenericPayloadCPointerWrite.cj", out_dir)
    write_fn = function_slice(write_ir, "writeGeneric")
    assert "llvm.memcpy" in write_fn
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
