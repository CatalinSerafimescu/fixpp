#!/usr/bin/env bash
# test_broken_wheel_gate.sh — SC-006 / FR-009 named NEGATIVE witness (T019).
#
# Proves the wheel gate goes RED when the bundled dictionary data is missing:
# strip _fixpp_data/FIX44.xml from a COPY of a built wheel, install the mutated
# copy into a throwaway venv, run the locator round-trip, and assert it FAILS.
#
# Discriminating (cf. the project's "witness must discriminate" lessons): this
# script first proves the INTACT wheel's round-trip PASSES (a positive control),
# THEN proves the stripped wheel's round-trip FAILS. A single arm could false-pass
# — the intact arm rules out an unrelated break being the reason the stripped arm
# is red; the stripped arm rules out the gate being vacuously green.
#
# Non-publishing: operates only on throwaway copies under a temp dir; never
# touches wheelhouse/, never uploads. $PYTHON overrides the interpreter
# (default python3) so CI can point it at each matrix interpreter and a local
# run can point it at a `uv`-managed CPython.
set -euo pipefail

WHEEL="${1:?usage: test_broken_wheel_gate.sh <path-to-built-cp310-abi3-wheel>}"
PYTHON="${PYTHON:-python3}"

[ -f "$WHEEL" ] || { echo "::error::wheel not found: $WHEEL"; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
WHEEL="$(cd "$(dirname "$WHEEL")" && pwd)/$(basename "$WHEEL")"  # absolutise

# ── Build a valid wheel COPY with _fixpp_data/FIX44.xml removed ──────────────
# Drop the file AND its RECORD line so the mutated wheel still installs cleanly
# (pip verifies RECORD hashes). The failure must therefore surface at
# locate-time (missing data), not as an install-time RECORD mismatch — that is
# what makes this a *dictionary-data* witness rather than a corrupt-zip witness.
BROKEN="$WORK/$(basename "$WHEEL")"
"$PYTHON" - "$WHEEL" "$BROKEN" <<'PY'
import sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
target = "_fixpp_data/FIX44.xml"
with zipfile.ZipFile(src) as zin:
    names = zin.namelist()
    assert target in names, f"{target} not present in {src} — wheel layout changed?"
    record = next(n for n in names if n.endswith(".dist-info/RECORD"))
    blobs = {n: zin.read(n) for n in names if n != target}
kept = [l for l in blobs[record].decode().splitlines(keepends=True)
        if not l.startswith(target + ",")]
blobs[record] = "".join(kept).encode()
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
    for n, b in blobs.items():
        zout.writestr(n, b)
print(f"stripped {target} from wheel copy")
PY

# ── round-trip probe: import fixpp, resolve FIX44 via the locator, load it ───
PROBE='
import fixpp
with fixpp.dictionary_path("FIX44") as p:
    d = fixpp.dict_load_from_xml(p)
    fixpp.dict_destroy(d)
print("ROUNDTRIP_OK")
'

run_in_clean_venv() {  # <wheel>  -> echoes probe output, returns probe exit code
    local whl="$1" venv="$WORK/venv.$RANDOM"
    "$PYTHON" -m venv "$venv"
    "$venv/bin/pip" install -q "$whl"
    # out-of-repo, PYTHONPATH scrubbed: no source tree can shadow the install
    ( cd "$WORK" && env -u PYTHONPATH "$venv/bin/python" -c "$PROBE" )
}

echo "── positive control: intact wheel round-trip must PASS ──"
if ! run_in_clean_venv "$WHEEL"; then
    echo "::error::positive control FAILED — the intact wheel round-trip did not"
    echo "         succeed, so a red stripped arm would not be attributable to"
    echo "         the missing dictionary. Aborting (non-discriminating)."
    exit 1
fi

echo "── negative witness: FIX44-stripped wheel round-trip must FAIL ──"
if run_in_clean_venv "$BROKEN"; then
    echo "::error::SC-006 VIOLATED — the round-trip SUCCEEDED on a wheel with"
    echo "         _fixpp_data/FIX44.xml removed. A broken artifact would ship."
    exit 1
fi

echo "PASS: broken-wheel gate is RED on the stripped wheel, GREEN on the intact wheel."
