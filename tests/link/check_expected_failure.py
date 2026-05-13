#!/usr/bin/env python3
"""tests/link/check_expected_failure.py — Seam #9 wrapper.

Invokes CMake on the decimal_alias_mismatch_test.cmake project, expects a
nonzero exit, and asserts the linker error message contains "decimal_alias_sentinel".
Rejects spurious failures (compile errors, missing headers) by checking the
substring presence in build output.

Usage:
    python tests/link/check_expected_failure.py \\
        --fixpp-root /path/to/library \\
        --build-dir /tmp/alias_mismatch_test
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description="Seam #9 alias-mismatch link-failure checker")
    parser.add_argument("--fixpp-root", required=True,
                        help="Path to the library submodule root")
    parser.add_argument("--build-dir", default=None,
                        help="Build directory (default: temp dir)")
    args = parser.parse_args()

    fixpp_root = pathlib.Path(args.fixpp_root).resolve()
    cmake_file = fixpp_root / "tests" / "link" / "decimal_alias_mismatch_test.cmake"

    if not cmake_file.exists():
        print(f"error: cmake file not found: {cmake_file}", file=sys.stderr)
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

        # Configure
        cfg = subprocess.run(
            ["cmake", "-S", str(src_dir),
             "-B", build_subdir,
             f"-DFIXPP_ROOT={fixpp_root}",
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

        if "decimal_alias_sentinel" not in build_output:
            print("[seam#9] FAIL: build failed but 'decimal_alias_sentinel' not in output",
                  file=sys.stderr)
            print("Build output was:")
            print(build_output)
            return 1

        print("[seam#9] PASS: link failed with expected 'decimal_alias_sentinel' message")
        return 0
    finally:
        if cleanup:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
