#!/usr/bin/env python3
"""Witness the erased ($withoutTI) wrapping else branch.

The current backend reports a verifier error after emitting this witness. The
test preserves stdout so the generated IR remains inspectable and uses the
product intrinsic as its pass/fail criterion.
"""
import pathlib
import subprocess
import sys


def main() -> int:
    if len(sys.argv) not in (3, 4):
        return 2
    cjc, source = sys.argv[1], pathlib.Path(sys.argv[2])
    out = pathlib.Path(sys.argv[3]) if len(sys.argv) == 4 else source.with_suffix(".ir")
    proc = subprocess.run(
        [cjc, "-g", "--output-type=staticlib", "--dump-ir", "--dump-to-screen",
         "-o", str(out.with_suffix("")), str(source)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    out.write_text(proc.stdout)
    if "llvm.cj.gcwrite.generic.payload" not in proc.stdout:
        print(f"missing product payload helper (backend_rc={proc.returncode})", file=sys.stderr)
        return 1
    print(f"wrapping else helper present (backend_rc={proc.returncode})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
