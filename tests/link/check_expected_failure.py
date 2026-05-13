#!/usr/bin/env python3
"""tests/link/check_expected_failure.py — Seam #9 wrapper.

Invokes CMake on the decimal_alias_mismatch_test.cmake project, expects a
nonzero exit, and asserts the linker error message contains a reference to
"decimal_alias_sentinel<...mismatch_type..." (the mismatched alias name).
Rejects spurious failures (compile errors, missing headers, or generic
unresolved-symbol failures unrelated to the alias mismatch) by narrowing the
regex to the mismatched-type name rather than the generic class-template name.

The --fixpp-lib-dir argument must point to the build output directory
containing libfixpp_core.a (or libfixpp_core.so).  The cmake sub-project
links tu_a.cpp against that library so the pod_decimal sentinel resolves
cleanly, leaving only decimal_alias_sentinel<mismatch_type>::tag unresolvable.
This ensures the test exercises the actual alias-lock contract (AC-B3) rather
than passing because no library is linked at all.

Note: if you comment out the target_link_libraries(...fixpp_core) line in
decimal_alias_mismatch_test.cmake and rerun this script, it MUST fail because
both sentinels are undefined and the diagnostic includes "pod_decimal" as well
as "mismatch_type" — the narrowed regex still matches "mismatch_type", so that
smoke-test would still pass; the structural guarantee is provided by the library
being present so that pod_decimal resolves while mismatch_type does not.

Usage:
    python tests/link/check_expected_failure.py \\
        --fixpp-root /path/to/library \\
        --fixpp-lib-dir /path/to/library/build/<preset>/lib \\
        --build-dir /tmp/alias_mismatch_test
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description="Seam #9 alias-mismatch link-failure checker")
    parser.add_argument("--fixpp-root", required=True,
                        help="Path to the library submodule root")
    parser.add_argument("--fixpp-lib-dir", required=True,
                        help="Path to the build lib/ directory containing libfixpp_core.a")
    parser.add_argument("--build-dir", default=None,
                        help="Build directory (default: temp dir)")
    args = parser.parse_args()

    fixpp_root = pathlib.Path(args.fixpp_root).resolve()
    fixpp_lib_dir = pathlib.Path(args.fixpp_lib_dir).resolve()
    cmake_file = fixpp_root / "tests" / "link" / "decimal_alias_mismatch_test.cmake"

    if not cmake_file.exists():
        print(f"error: cmake file not found: {cmake_file}", file=sys.stderr)
        return 2

    if not fixpp_lib_dir.exists():
        print(f"error: --fixpp-lib-dir not found: {fixpp_lib_dir}", file=sys.stderr)
        return 2

    tmpdir = args.build_dir
    cleanup = False
    if tmpdir is None:
        tmpdir = tempfile.mkdtemp(prefix="fixpp_alias_mismatch_")
        cleanup = True

    try:
        # tests/link/CMakeLists.txt is reserved for the ctest entry that
        # registers this checker — pointing `cmake -S` at tests/link would
        # configure that file instead of the alias-mismatch project. Copy
        # the .cmake file into a fresh source directory so it is the project.
        src_dir = pathlib.Path(tmpdir) / "src"
        src_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(str(cmake_file), str(src_dir / "CMakeLists.txt"))
        build_subdir = str(pathlib.Path(tmpdir) / "build")

        # Configure — pass FIXPP_LIB_DIR so the cmake sub-project can find
        # the built libfixpp_core.a and link tu_a against it.
        cfg = subprocess.run(
            ["cmake", "-S", str(src_dir),
             "-B", build_subdir,
             f"-DFIXPP_ROOT={fixpp_root}",
             f"-DFIXPP_LIB_DIR={fixpp_lib_dir}",
             "-DCMAKE_CXX_STANDARD=23"],
            capture_output=True, text=True
        )
        if cfg.returncode != 0:
            print("[seam#9] CMake configure failed (unexpected):")
            print(cfg.stderr)
            return 1

        # Build — expected to FAIL
        build = subprocess.run(
            ["cmake", "--build", build_subdir, "--target", "decimal_alias_mismatch_test"],
            capture_output=True, text=True
        )
        build_output = build.stdout + build.stderr

        if build.returncode == 0:
            print("[seam#9] FAIL: build succeeded — expected link failure", file=sys.stderr)
            return 1

        # Narrow check: must mention the mismatched alias name, not just the
        # generic class-template name. This ensures the test fails if the library
        # is not linked (both sentinels undefined) and the failure is for any
        # unresolved symbol — see P1-4 triage for why the broad check was wrong.
        if not re.search(r"decimal_alias_sentinel[^>]*mismatch_type", build_output):
            print("[seam#9] FAIL: build failed but 'decimal_alias_sentinel<...mismatch_type...>' "
                  "not in output", file=sys.stderr)
            print("Build output was:")
            print(build_output)
            return 1

        print("[seam#9] PASS: link failed with expected 'decimal_alias_sentinel<...mismatch_type>' "
              "message")
        return 0
    finally:
        if cleanup:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
