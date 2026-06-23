#!/usr/bin/env bash
# run_coverage.sh — local mirror of the tier1.yml coverage job.
# Must be run from the library submodule root.
# Usage: bash tools/run_coverage.sh
set -euo pipefail

BUILDDIR="$(pwd)/build/linux-clang-coverage"
PROFDIR="$BUILDDIR/profiles"
PROFDATA="$BUILDDIR/coverage.profdata"
LCOV="$BUILDDIR/coverage.lcov"
LLVM_PROFDATA="${LLVM_PROFDATA:-llvm-profdata}"
LLVM_COV="${LLVM_COV:-llvm-cov}"

echo "=== Step 1: clean old profiles ==="
rm -f "$PROFDIR"/*.profraw
mkdir -p "$PROFDIR"

echo "=== Step 2: run ctest with per-pid profraw files ==="
LLVM_PROFILE_FILE="$PROFDIR/default-%p.profraw" \
  ctest --preset linux-clang-coverage --output-on-failure 2>&1 | tee /tmp/ctest-coverage.log || true

echo "=== Step 3: merge profiles ==="
profraw_files=("$PROFDIR"/*.profraw)
if [ ${#profraw_files[@]} -eq 0 ] || [ ! -f "${profraw_files[0]}" ]; then
  echo "ERROR: no .profraw files in $PROFDIR" >&2
  exit 1
fi
"$LLVM_PROFDATA" merge -sparse "$PROFDIR"/*.profraw -o "$PROFDATA"
echo "Merged ${#profraw_files[@]} profraw files -> $PROFDATA"

echo "=== Step 4: export lcov ==="
OBJECTS=""
for b in "$BUILDDIR/bin/"*; do
  [ -f "$b" ] && [ -x "$b" ] || continue
  [ "$(basename "$b")" = fixpp_core_tests ] && continue
  OBJECTS="$OBJECTS -object $b"
done

# shellcheck disable=SC2086
"$LLVM_COV" export \
  --format=lcov \
  --instr-profile="$PROFDATA" \
  "$BUILDDIR/bin/fixpp_core_tests" \
  $OBJECTS \
  include src \
  > "$LCOV"
echo "LCOV report written to $LCOV"

echo "=== Step 5: summary ==="
# Use lcov --summary if available, else parse the .info manually
if command -v lcov &>/dev/null; then
  lcov --summary "$LCOV" 2>&1 || true
fi
echo "Done."
