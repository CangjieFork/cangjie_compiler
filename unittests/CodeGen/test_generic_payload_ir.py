#!/usr/bin/env python3
"""CodeGen regression checks for generic payload barrier emission.

Usage: test_generic_payload_ir.py <cjc> <output-dir>
The test intentionally inspects product IR emitted by cjc; it does not reimplement
the code-generation logic in the test process.
"""
import pathlib
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
    start = ir.find(needle)
    if start < 0:
        raise AssertionError(f"missing generated function {needle}")
    end = ir.find("\ndefine ", start + 1)
    return ir[start:] if end < 0 else ir[start:end]


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    cjc, out_dir = sys.argv[1], pathlib.Path(sys.argv[2])

    # Structural guardrails pin the product call sites as well as the emitted
    # IR.  Removing either product helper call makes this test fail immediately
    # (before any test-only code could mask the regression).
    dispatcher = (ROOT.parents[2] / "src/CodeGen/Base/IntrinsicsDispatcher.cpp").read_text()
    read_branch = dispatcher.split("if (retTy->IsGeneric())", 1)[1].split("} else {", 1)[0]
    assert "CallGCWriteGenericPayload" in read_branch
    assert "CreateMemCpy" not in read_branch
    emit_package = (ROOT.parents[2] / "src/CodeGen/CJNative/EmitPackageIR.cpp").read_text()
    wrapping_start = emit_package.index("if (auto thisType = CGType::GetOrCreate")
    wrapping_else = emit_package[wrapping_start:].split("/// step4", 1)[0]
    assert "CallGCWriteGenericPayload" in wrapping_else

    read_ir = emit(cjc, ROOT / "GenericPayloadCPointerRead.cj", out_dir)
    read_fn = function_slice(read_ir, "readGeneric")
    assert "llvm.cj.gcwrite.generic.payload" in read_fn
    assert "llvm.memcpy" not in read_fn

    wrapping_ir = emit(cjc, ROOT / "GenericPayloadWithoutTI.cj", out_dir)
    assert "llvm.cj.gcwrite.generic.payload" in wrapping_ir

    # CPointerWrite is intentionally recorded as a pending runtime-helper item;
    # keep a product-IR witness so a future helper change can turn this into a
    # positive barrier assertion without rebuilding a test-only copy.
    emit(cjc, ROOT / "GenericPayloadCPointerWrite.cj", out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
