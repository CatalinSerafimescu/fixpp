#!/usr/bin/env bash
# check_capi_freeze.sh — NBC-1 (T017): the C-ABI surface is BYTE-FROZEN.
#
# The 0->1 GA freeze (FR-012 / SC-007) means every C-ABI header is immutable.
# This gate recomputes the SHA-256 of `include/fix/c_api.h` + every header under
# `include/fix/c_api/` and verifies it against the committed manifest
# `tools/capi_freeze.sha256`. It is event-independent (works on push:main where
# there is no PR base to diff against) — "byte-frozen" is an ABSOLUTE baseline,
# not a relative-to-base diff.
#
# ANY content change to a frozen header fails the `sha256sum -c` check; ANY
# added/removed header fails the exact-set check below. Deliberately unfreezing
# the C-ABI (a future MAJOR) is therefore a visible, reviewed edit to the
# manifest in the same PR — which is the point.
#
# Companion to the existing tools/check_capi_occupancy.sh (the C-ABI occupancy
# gate run alongside this one in tier1.yml check-layers / python-wheel).
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
MANIFEST="tools/capi_freeze.sha256"

[ -f "$MANIFEST" ] || { echo "::error::missing C-ABI freeze manifest $MANIFEST"; exit 2; }

# 1) Content freeze: every listed header must hash to its manifest value.
echo "── C-ABI byte-freeze: verifying header checksums ──"
sha256sum -c "$MANIFEST"

# 2) Set freeze: the manifest must enumerate EXACTLY the current header set, so a
#    NEWLY ADDED c_api header (which sha256sum -c would silently ignore) is also
#    caught. Exact-set, not subset — mirrors the import-surface snapshot.
LISTED="$(awk '{print $2}' "$MANIFEST" | sort)"
ACTUAL="$(find include/fix/c_api.h include/fix/c_api -type f -name '*.h' | sort)"
if [ "$LISTED" != "$ACTUAL" ]; then
    echo "::error::C-ABI header SET drift — the frozen manifest no longer matches"
    echo "         the headers on disk (a header was added or removed):"
    diff <(printf '%s\n' "$LISTED") <(printf '%s\n' "$ACTUAL") || true
    exit 1
fi

echo "PASS: C-ABI surface byte-frozen ($(printf '%s\n' "$ACTUAL" | wc -l) headers)."
