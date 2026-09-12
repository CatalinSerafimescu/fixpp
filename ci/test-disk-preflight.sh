#!/usr/bin/env bash
# ci/test-disk-preflight.sh — regression pin for ci/disk-preflight.sh (089
# T005/T007), driving the RED arms `contracts/089-quickfix-interop-
# conversation/disk-preflight.md` § "Required RED arms" enumerates.
#
# Every external input ci/disk-preflight.sh reads is overridable from the
# environment (see that script's own header), so every arm here runs against
# SYNTHETIC fixtures — a fake `df`, a fake `/proc/version`, a fake
# `/proc/mounts`, sparse ballast files, and a scratch build-root — never the
# real disk. Buildless: bash + coreutils only.
#
# ⚠️ EVERY RED CELL ASSERTS ITS OWN DIAGNOSTIC TOKEN, not merely a non-zero
# exit (same convention as ci/test-check-wheel-payload.sh, which this contract
# names as precedent). A cell that reddens via a different check than the one
# it names has certified nothing.
#
# ⚠️ A-7 and A-8 are SPURIOUS-HIT arms (FR-018): they must show the guard
# would report PASS for a reason unrelated to what it claims to measure. A
# forced-miss arm alone (proving the guard CAN fire) cannot show that — so
# each runs a MUTANT copy first (proven to apply via a byte-diff against the
# original) and asserts it goes GREEN WRONGLY, then runs the real script on
# the IDENTICAL fixture and asserts it goes RED for the named reason. Mutants
# are applied to a scratch COPY only — the tracked script is never edited.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/disk-preflight.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0

# ── Fixture plumbing ─────────────────────────────────────────────────────────

FAKE_DF="$WORK/fake-df"
cat > "$FAKE_DF" <<'EOF'
#!/usr/bin/env bash
# Stub `df --output=avail,source <mount>`. Looks up the queried mount in the
# table named by FAKE_DF_TABLE ("<mount> <avail_kb> <source>" per line, one
# line per mount). Exits 1 (simulating an unreadable/nonexistent mount) if
# the queried mount has no row.
set -uo pipefail
TABLE="${FAKE_DF_TABLE:?FAKE_DF_TABLE not set}"
mount="${@: -1}"
line="$(awk -v m="$mount" '$1==m {print $2, $3; exit}' "$TABLE")"
[ -n "$line" ] || exit 1
echo "   Avail Source"
echo "$line"
EOF
chmod +x "$FAKE_DF"

mk_df_table() {  # $1=outfile, then triples: mount avail_kb source
  local out="$1"; shift
  : > "$out"
  while [ "$#" -ge 3 ]; do
    printf '%s %s %s\n' "$1" "$2" "$3" >> "$out"
    shift 3
  done
}

mk_wsl_proc_version() { printf 'Linux version 5.15.167.4-microsoft-standard-WSL2 (root@buildhost)\n' > "$1"; }
mk_nonwsl_proc_version() { printf 'Linux version 5.15.0-generic (buildd@lcy02-amd64)\n' > "$1"; }

# $1=outfile $2=drvfs mount point ("" to omit any drvfs line) $3=drvfs source label
mk_proc_mounts() {
  local out="$1" mnt="$2" dev="${3:-E:\\134}"
  {
    echo "/dev/sdd / ext4 rw,relatime 0 0"
    if [ -n "$mnt" ]; then
      printf '%s %s 9p rw,noatime,aname=drvfs;path=E:\\;uid=1000 0 0\n' "$dev" "$mnt"
    fi
  } > "$out"
}

# $1=dir $2=size1(bytes|absent) $3=size2(bytes|absent) — sparse files, no real
# bytes written, so this stays instant regardless of ballast size.
mk_ballast() {
  local dir="$1" s1="$2" s2="$3"
  mkdir -p "$dir"
  rm -f "$dir/b1.bin" "$dir/b2.bin"
  if [ "$s1" != "absent" ]; then : > "$dir/b1.bin"; truncate -s "$s1" "$dir/b1.bin"; fi
  if [ "$s2" != "absent" ]; then : > "$dir/b2.bin"; truncate -s "$s2" "$dir/b2.bin"; fi
}

# Writes REAL (non-sparse) bytes via dd, at KB scale — never GiB. Moved above
# the common fixture defaults so both the shared `run()` harness's ballast
# fixture and the D-8/T002a cells can build real-allocated-blocks ballast
# rather than truncate's sparse holes.
mk_real_file() {  # $1=path $2=size_kb
  mkdir -p "$(dirname "$1")"
  dd if=/dev/zero "of=$1" bs=1K "count=$2" status=none 2>/dev/null
}

# Populate/clear a build-root's reclaim-candidate directories.
# $1=build_root_dir  $2...=populated config names (asan/ubsan/tsan)
mk_build_root() {
  local root="$1"; shift
  rm -rf "$root"
  mkdir -p "$root"
  local c
  for c in "$@"; do
    case "$c" in
      asan) mkdir -p "$root/linux-clang-asan/obj"; : > "$root/linux-clang-asan/obj/x.o" ;;
      ubsan) mkdir -p "$root/linux-clang-ubsan/obj"; : > "$root/linux-clang-ubsan/obj/x.o" ;;
      tsan) mkdir -p "$root/linux-clang-tsan/obj"; : > "$root/linux-clang-tsan/obj/x.o" ;;
    esac
  done
}

# Runs $SUT with the given fixture set. Sets OUT, RC.
#   run <sut> <config-args...>
run() {
  local sut="$1"; shift
  OUT="$(
    FIXPP_DISK_PREFLIGHT_PROC_VERSION="$PV_FILE" \
    FIXPP_DISK_PREFLIGHT_PROC_MOUNTS="$PM_FILE" \
    FIXPP_DISK_PREFLIGHT_DF="$FAKE_DF" \
    FAKE_DF_TABLE="$DF_TABLE" \
    FIXPP_DISK_PREFLIGHT_BUILD_MOUNT="/" \
    FIXPP_DISK_PREFLIGHT_BUILD_ROOT="$BUILD_ROOT" \
    FIXPP_DISK_PREFLIGHT_BALLAST_FILES="$BALLAST_DIR/b1.bin $BALLAST_DIR/b2.bin" \
    FIXPP_DISK_PREFLIGHT_BALLAST_PER_FILE_KB="$BALLAST_PER_FILE_KB_MAIN" \
    FIXPP_DISK_PREFLIGHT_REQUIRED_INTERNAL_FREE_KB="${REQ_INTERNAL:-}" \
    FIXPP_DISK_PREFLIGHT_REQUIRED_INTERNAL_FREE_KB_DATE="${REQ_INTERNAL_DATE:-}" \
    FIXPP_DISK_PREFLIGHT_REQUIRED_HOST_GROWTH_KB="${REQ_HOST:-}" \
    FIXPP_DISK_PREFLIGHT_REQUIRED_HOST_GROWTH_KB_DATE="${REQ_HOST_DATE:-}" \
    "$sut" "$@" 2>&1
  )"; RC=$?
}

# $1=label $2=want_rc("*" for "nonzero") $3=sut $4...=cli args, then set
# WANT_TOKENS (array) and WANT_ABSENT (array, optional) before calling.
cell() {
  local label="$1" want_rc="$2" sut="$3"; shift 3
  run "$sut" "$@"
  if [ "$want_rc" = "nonzero" ]; then
    if [ "$RC" -eq 0 ]; then
      echo "  FAIL $label: want nonzero rc, got 0"; fail=$((fail+1)); print_out; return
    fi
  else
    if [ "$RC" -ne "$want_rc" ]; then
      echo "  FAIL $label: want rc=$want_rc, got rc=$RC"; fail=$((fail+1)); print_out; return
    fi
  fi
  local t
  for t in "${WANT_TOKENS[@]}"; do
    if ! grep -qF -- "$t" <<<"$OUT"; then
      echo "  FAIL $label: expected output to contain '$t'"; fail=$((fail+1)); print_out; return
    fi
  done
  for t in "${WANT_ABSENT[@]:-}"; do
    [ -z "$t" ] && continue
    if grep -qF -- "$t" <<<"$OUT"; then
      echo "  FAIL $label: expected output NOT to contain '$t'"; fail=$((fail+1)); print_out; return
    fi
  done
  echo "  ok   $label (rc=$RC)"
  pass=$((pass+1))
}

print_out() { sed 's/^/         | /' <<<"$OUT" | head -20; }

# Applies a python replacement to a fresh copy of disk-preflight.sh, asserts
# it actually changed the file (cmp), and echoes the mutant's path.
# mutate <id> <python-src-on-stdin>
mutate() {
  local id="$1"
  local mdir="$WORK/mut_$id"
  mkdir -p "$mdir"
  local dst="$mdir/disk-preflight.sh"
  if ! python3 - "$SCRIPT" "$dst" 2>"$mdir/err"; then
    echo "  MUTFAIL $id: python replacement raised (mutation did not apply as expected)" >&2
    cat "$mdir/err" >&2
    fail=$((fail+1))
    return 1
  fi
  if cmp -s "$SCRIPT" "$dst"; then
    echo "  MUTFAIL $id: mutant is byte-identical to the original — mutation did not apply" >&2
    fail=$((fail+1))
    return 1
  fi
  chmod +x "$dst"
  echo "$dst"
}

# ── Common fixture defaults ──────────────────────────────────────────────────
PV_WSL="$WORK/proc-version-wsl"; mk_wsl_proc_version "$PV_WSL"
PV_NONWSL="$WORK/proc-version-nonwsl"; mk_nonwsl_proc_version "$PV_NONWSL"
PM_WITH_HOST="$WORK/proc-mounts-with-host"; mk_proc_mounts "$PM_WITH_HOST" "/mnt/e"
PM_NO_HOST="$WORK/proc-mounts-no-host"; mk_proc_mounts "$PM_NO_HOST" ""
# Scaled-down per-file ballast target for the whole `run()` harness (D-8's
# gate-path check derives its expected TOTAL from this times the file count
# — see disk-preflight.sh's BALLAST_EXPECTED_KB), so the fixtures below write
# real KB-scale bytes rather than sparse GiB holes.
BALLAST_PER_FILE_KB_MAIN=100
BALLAST_DIR="$WORK/ballast"
mk_real_file "$BALLAST_DIR/b1.bin" "$BALLAST_PER_FILE_KB_MAIN"
mk_real_file "$BALLAST_DIR/b2.bin" "$BALLAST_PER_FILE_KB_MAIN"
BUILD_ROOT="$WORK/build"; mk_build_root "$BUILD_ROOT"

# Comfortable-by-default reading table: build mount plenty free, host mount
# plenty free, DIFFERENT source devices (so D-10 never spuriously fires).
mk_comfortable_df() {
  DF_TABLE="$WORK/df-comfortable"
  mk_df_table "$DF_TABLE" \
    "/" "50000000" "/dev/sdd-build" \
    "/mnt/e" "20000000" "/dev/sde-host"
}
mk_comfortable_df

REQ_INTERNAL="1000000"; REQ_INTERNAL_DATE="2026-09-11"
REQ_HOST="1000000"; REQ_HOST_DATE="2026-09-11"

echo "== ci/disk-preflight.sh — RED arm inventory =="

# ── A-2: control, both comfortable — GREEN ──────────────────────────────────
PV_FILE="$PV_WSL"; PM_FILE="$PM_WITH_HOST"
WANT_TOKENS=("verdict: proceed" "failing_predicate: none")
WANT_ABSENT=()
cell "A-2 both-comfortable (control)" 0 "$SCRIPT" --config normal

# ── A-4: control, not WSL, no host mount — GREEN via D-4 ────────────────────
PV_FILE="$PV_NONWSL"; PM_FILE="$PM_NO_HOST"
WANT_TOKENS=("verdict: proceed" "wsl: no" "required_host_growth_kb: not-applicable")
WANT_ABSENT=()
cell "A-4 not-wsl (control, proves D-4 != D-3)" 0 "$SCRIPT" --config normal

# ── A-1: host below threshold, build mount comfortable — RED, host predicate
PV_FILE="$PV_WSL"; PM_FILE="$PM_WITH_HOST"
DF_TABLE="$WORK/df-a1"
mk_df_table "$DF_TABLE" "/" "50000000" "/dev/sdd-build" "/mnt/e" "1" "/dev/sde-host"
WANT_TOKENS=("failing_predicate: host" "verdict: stop")
WANT_ABSENT=("reclaim-first")
cell "A-1 host-below-threshold (independent host predicate)" nonzero "$SCRIPT" --config normal
mk_comfortable_df

# ── A-1a: build mount below threshold, host comfortable — RED, internal
DF_TABLE="$WORK/df-a1a"
mk_df_table "$DF_TABLE" "/" "1" "/dev/sdd-build" "/mnt/e" "20000000" "/dev/sde-host"
WANT_TOKENS=("failing_predicate: internal" "verdict: stop")
WANT_ABSENT=()
cell "A-1a build-mount-below-threshold (independent internal predicate)" nonzero "$SCRIPT" --config normal
mk_comfortable_df

# ── A-3: WSL, host mount unreadable via df (present in /proc/mounts, df fails)
DF_TABLE="$WORK/df-a3"
mk_df_table "$DF_TABLE" "/" "50000000" "/dev/sdd-build"   # no /mnt/e row -> df fails
WANT_TOKENS=("failing_predicate: host" "verdict: stop" "D-3")
WANT_ABSENT=()
cell "A-3 host-mount-unreadable (D-3, not incidental)" nonzero "$SCRIPT" --config normal
mk_comfortable_df

# A-3b (bonus, same D-3 rule, different code path into it): host mount absent
# from /proc/mounts entirely.
PM_FILE="$PM_NO_HOST"
WANT_TOKENS=("failing_predicate: host" "verdict: stop" "D-3")
WANT_ABSENT=()
cell "A-3b host-mount-absent-from-proc-mounts (D-3, sibling path)" nonzero "$SCRIPT" --config normal
PM_FILE="$PM_WITH_HOST"

# ── A-5: internal below threshold, host comfortable, reclaimables present
mk_build_root "$BUILD_ROOT" ubsan tsan
DF_TABLE="$WORK/df-a5"
mk_df_table "$DF_TABLE" "/" "1" "/dev/sdd-build" "/mnt/e" "20000000" "/dev/sde-host"
WANT_TOKENS=("failing_predicate: internal" "verdict: reclaim-first")
WANT_ABSENT=()
cell "A-5 internal-fails-with-reclaimables" nonzero "$SCRIPT" --config asan
mk_comfortable_df

# ── A-5a: HOST below threshold, reclaimables present — must be stop, NEVER
# reclaim-first (reclaim returns nothing to the host).
DF_TABLE="$WORK/df-a5a"
mk_df_table "$DF_TABLE" "/" "50000000" "/dev/sdd-build" "/mnt/e" "1" "/dev/sde-host"
WANT_TOKENS=("failing_predicate: host" "verdict: stop")
WANT_ABSENT=("reclaim-first")
cell "A-5a host-fails-with-reclaimables-never-reclaim-first" nonzero "$SCRIPT" --config asan
mk_comfortable_df

# ── A-6: below threshold, nothing reclaimable — stop
mk_build_root "$BUILD_ROOT"   # empty: no asan/ubsan/tsan populated
DF_TABLE="$WORK/df-a6"
mk_df_table "$DF_TABLE" "/" "1" "/dev/sdd-build" "/mnt/e" "20000000" "/dev/sde-host"
WANT_TOKENS=("failing_predicate: internal" "verdict: stop")
WANT_ABSENT=("reclaim-first")
cell "A-6 nothing-reclaimable" nonzero "$SCRIPT" --config asan
mk_comfortable_df
mk_build_root "$BUILD_ROOT"

# ── The debug-tree control: build/linux-clang-debug is NEVER a reclaim
# candidate — populate ONLY it and confirm the verdict is still stop, not
# reclaim-first, on an internal-only failure.
mk_build_root "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT/linux-clang-debug/obj"; : > "$BUILD_ROOT/linux-clang-debug/obj/x.o"
DF_TABLE="$WORK/df-debug-control"
mk_df_table "$DF_TABLE" "/" "1" "/dev/sdd-build" "/mnt/e" "20000000" "/dev/sde-host"
WANT_TOKENS=("failing_predicate: internal" "verdict: stop" "reclaimable_configs: (none)")
WANT_ABSENT=("reclaim-first")
cell "debug-tree-never-reclaimable (linux-clang-debug populated, still stop)" nonzero "$SCRIPT" --config asan
mk_comfortable_df
mk_build_root "$BUILD_ROOT"

echo
echo "== spurious-hit arms (FR-018): mutant GREEN-wrongly, then real RED ======="

# ── A-7: threshold unset/unparseable, BOTH mounts nearly full — spurious hit.
# ⚠️ Re-aimed 2026-09-11 (T001 landed): the embedded table is now POPULATED
# for all four configs, so "unset" can no longer be reached by leaving the
# override unset and relying on an empty embedded slot — that arm no longer
# exists to exercise. The override mechanism is what now reaches "unparseable"
# (the contract's other named trigger, "unset OR unparseable"): a threshold
# override that IS supplied but is NOT a number. Mutant re-aimed to the
# OVERRIDE branch of resolve_threshold — the exact "defaulting to 0 instead of
# failing validation" shape D-9 exists to forbid, now reachable via the
# override path since the embedded-table path can no longer produce it.
# ⚠️ Disclosure: with all four embedded slots populated, this arm now pins
# D-9's unset/unparseable branch on a route production never takes (only
# ci/test-disk-preflight.sh sets the override env vars) — the branch is
# unreachable on the gate's real --config path until a config's slot is
# unset again. Kept because D-9 still names the trigger and the override
# path is the only route left to exercise it, not because it is reachable
# in production today.
M7="$(mutate M-A7 <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '''  if [ -n "$ov_val" ]; then
    THR_KB="$ov_val"
    THR_DATE="$ov_date"
    THR_SRC="test-override"
    return
  fi
'''
new = '''  if [ -n "$ov_val" ]; then
    if [[ "$ov_val" =~ ^[0-9]+$ ]]; then THR_KB="$ov_val"; else THR_KB=0; fi
    THR_DATE="$ov_date"
    THR_SRC="test-override"
    return
  fi
'''
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PY
)"
if [ -n "$M7" ]; then
  # Both mounts NEARLY FULL — 1 KB avail each — so the finding is stark: a
  # defaulted-0 threshold is satisfied by essentially nothing free. The
  # override VALUE is unparseable ("garbage"); its DATE is a valid, present
  # date — isolating the property under test to the value-regex check alone,
  # not conflated with the separate missing-date hard error.
  DF_TABLE="$WORK/df-a7"
  mk_df_table "$DF_TABLE" "/" "1" "/dev/sdd-build" "/mnt/e" "1" "/dev/sde-host"
  REQ_INTERNAL="garbage"; REQ_INTERNAL_DATE="2026-09-11"
  REQ_HOST="garbage"; REQ_HOST_DATE="2026-09-11"
  WANT_TOKENS=("verdict: proceed")
  WANT_ABSENT=()
  cell "A-7 MUTANT override-unparseable-defaults-to-0 -> spurious PASS" 0 "$M7" --config normal

  WANT_TOKENS=("hard_error:" "threshold_unset_or_unparseable" "D-9")
  WANT_ABSENT=("verdict: proceed")
  cell "A-7 REAL unparseable-override -> hard error (D-9), same fixture" 2 "$SCRIPT" --config normal
  REQ_INTERNAL="1000000"; REQ_INTERNAL_DATE="2026-09-11"; REQ_HOST="1000000"; REQ_HOST_DATE="2026-09-11"
  mk_comfortable_df
fi

# ── A-8: host mount resolves to the SAME DEVICE as the build mount —
# spurious hit. Mutant: deletes the D-10 same-device guard entirely, so the
# host predicate is vacuously satisfied by the build reading.
M8="$(mutate M-A8 <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '''    if [ "$HOST_SOURCE" = "$BUILD_SOURCE" ]; then
      # D-10: the host mount resolved to the SAME DEVICE as the build mount —
      # the host predicate would be vacuously satisfied by the build reading.
      # Compared by device (df `source`), not by mount-path string: two
      # different paths on the same device are exactly as vacuous as the same
      # path twice.
      FAILING_PREDICATE="host"
      REASON="host mount resolved to the build-mount device '${BUILD_SOURCE}' (D-10)"
      HOST_READING_OUT="VACUOUS(=build)"
      VERDICT="stop"
    else
'''
new = "    if false; then\n      :\n    else\n"
assert t.count(old) == 1
open(dst, "w").write(t.replace(old, new))
PY
)"
if [ -n "$M8" ]; then
  # Host mount is a DIFFERENT PATH but the SAME DEVICE as the build mount —
  # exactly what a path-string comparison would miss and a source-field
  # comparison catches.
  DF_TABLE="$WORK/df-a8"
  mk_df_table "$DF_TABLE" "/" "50000000" "/dev/sdd-shared" "/mnt/e" "20000000" "/dev/sdd-shared"
  WANT_TOKENS=("verdict: proceed")
  WANT_ABSENT=()
  cell "A-8 MUTANT host-resolves-to-build-device -> spurious PASS" 0 "$M8" --config normal

  WANT_TOKENS=("reason:" "D-10" "verdict: stop")
  WANT_ABSENT=("verdict: proceed")
  cell "A-8 REAL host-resolves-to-build-device -> RED (D-10), same fixture" nonzero "$SCRIPT" --config normal
  mk_comfortable_df
fi

echo
echo "== T007: D-9a bootstrap cannot bypass the gate in ordinary operation ====="

# ⚠️ Re-expressed 2026-09-11 (T001 landed): the embedded table is now
# POPULATED for all four configs (D-7), so "ordinary operation hard-errors
# unconditionally" is no longer the correct claim — the whole POINT of T001
# landing is that ordinary operation now reaches a REAL verdict. What must
# still hold: (a) that verdict is real and dated, never a silent bypass
# (proven below — no hard_error, a genuine proceed/stop/reclaim-first, and
# the dates/sources printed are the embedded ones, not a stray override);
# (b) the D-9a bootstrap discipline (all three flags required) is unaffected
# (unchanged, still proven below); (c) D-9 still governs a threshold that IS
# present but lacks its measurement date — proven with an explicit override.
unset REQ_INTERNAL REQ_INTERNAL_DATE REQ_HOST REQ_HOST_DATE
for c in normal asan ubsan tsan; do
  WANT_TOKENS=("verdict:" "required_internal_free_source: embedded-table" "required_host_growth_source: embedded-table")
  WANT_ABSENT=("hard_error:")
  cell "T007 ordinary-operation config=$c now reaches a real verdict (T001 landed, D-7)" 0 "$SCRIPT" --config "$c"
done
mk_comfortable_df

# D-9 still fires on a threshold that IS present but carries NO measurement
# date — proven via an explicit override rather than relying on the (now
# populated) embedded table to ever be in that state.
REQ_INTERNAL="1000000"; REQ_INTERNAL_DATE=""
WANT_TOKENS=("hard_error:" "threshold_missing_measurement_date" "D-9")
WANT_ABSENT=("verdict: proceed")
cell "T007 present-value-no-date still hard-errors (D-9)" 2 "$SCRIPT" --config normal
unset REQ_INTERNAL REQ_INTERNAL_DATE REQ_HOST REQ_HOST_DATE

# --bootstrap missing any one of the three required values is ALSO a hard
# error — bootstrap mode cannot be entered by accident.
WANT_TOKENS=("hard_error:" "requires --internal-free-kb")
cell "T007 bootstrap-missing-internal-free-kb" 2 "$SCRIPT" --config normal --bootstrap \
  --host-growth-kb 1000000 --bootstrap-date 2026-09-11

WANT_TOKENS=("hard_error:" "requires --internal-free-kb")
cell "T007 bootstrap-missing-host-growth-kb" 2 "$SCRIPT" --config normal --bootstrap \
  --internal-free-kb 1000000 --bootstrap-date 2026-09-11

WANT_TOKENS=("hard_error:" "requires --internal-free-kb")
cell "T007 bootstrap-missing-bootstrap-date" 2 "$SCRIPT" --config normal --bootstrap \
  --internal-free-kb 1000000 --host-growth-kb 1000000

# --bootstrap WITH all three: succeeds, and the D-9a label is on EVERY line
# (not merely present somewhere) — this is the "made visible in the log"
# mechanism, checked as a literal per-line invariant.
run "$SCRIPT" --config normal --bootstrap --internal-free-kb 1000000 \
  --host-growth-kb 1000000 --bootstrap-date 2026-09-11
if [ "$RC" -ne 0 ]; then
  echo "  FAIL T007 bootstrap-complete: want rc=0, got rc=$RC"; fail=$((fail+1)); print_out
else
  total_lines=$(wc -l <<<"$OUT")
  labeled_lines=$(grep -c '^\[BOOTSTRAP: estimated, pending R-1, date=2026-09-11\] ' <<<"$OUT")
  if [ "$total_lines" -gt 0 ] && [ "$total_lines" = "$labeled_lines" ]; then
    echo "  ok   T007 bootstrap-complete label-on-EVERY-line ($labeled_lines/$total_lines, rc=$RC)"
    pass=$((pass+1))
  else
    echo "  FAIL T007 bootstrap-complete: label on $labeled_lines/$total_lines lines, want all"
    fail=$((fail+1)); print_out
  fi
fi

echo
echo "== D-8 reserve ballast visibility (supplementary — not in the RED-arm table) =="

# Restore a valid, dated threshold override — the T007 block above
# deliberately unset these to prove ordinary operation hard-errors.
REQ_INTERNAL="1000000"; REQ_INTERNAL_DATE="2026-09-11"
REQ_HOST="1000000"; REQ_HOST_DATE="2026-09-11"

# Ballast intact: adjusted host reading equals the raw reading (already
# proven on the real box during manual smoke-testing; pinned here as a
# regression). Ballast SPENT: same adjusted reading (D-8 keeps the normal
# budget flat regardless of spend — plan.md "not counted as available"), but
# ballast_status must say so, which is the visibility half. Real KB-scale
# writes throughout (BALLAST_PER_FILE_KB_MAIN=100 -> EXPECTED_KB=200) — no
# GiB written anywhere in this harness.
mk_real_file "$BALLAST_DIR/b1.bin" "$BALLAST_PER_FILE_KB_MAIN"
mk_real_file "$BALLAST_DIR/b2.bin" "$BALLAST_PER_FILE_KB_MAIN"
DF_TABLE="$WORK/df-ballast-intact"
mk_df_table "$DF_TABLE" "/" "50000000" "/dev/sdd-build" "/mnt/e" "20000000" "/dev/sde-host"
WANT_TOKENS=("ballast_status: intact" "host_reading_kb: 20000000")
WANT_ABSENT=()
cell "D-8 ballast-intact (no adjustment, status visible)" 0 "$SCRIPT" --config normal

# ── Sparse-blindness (escalation #1 from the T001/T002a review): a SPARSE
# file reports the full target via apparent size (`stat -c%s`) though it
# holds ZERO real allocated blocks — the exact hole `allocated_kb()` was
# built to close on the --ensure-ballast path, left open on THIS gate path
# until the fix below. Must be excluded exactly like "spent": ballast_status
# must NOT say intact, and the freed amount must be excluded from
# host_reading_kb identically to the real-spent cell below.
rm -f "$BALLAST_DIR/b1.bin" "$BALLAST_DIR/b2.bin"
truncate -s "$((BALLAST_PER_FILE_KB_MAIN * 1024))" "$BALLAST_DIR/b1.bin"
truncate -s "$((BALLAST_PER_FILE_KB_MAIN * 1024))" "$BALLAST_DIR/b2.bin"
DF_TABLE="$WORK/df-ballast-sparse"
# Raw host avail inflated by the full 200 KB a sparse hole must be treated as
# though it were absent — 20000000 + 200 = 20000200 KB (same math as spent).
mk_df_table "$DF_TABLE" "/" "50000000" "/dev/sdd-build" "/mnt/e" "20000200" "/dev/sde-host"
WANT_TOKENS=("ballast_status: spent" "host_reading_kb: 20000000")
WANT_ABSENT=("ballast_status: intact")
cell "D-8 ballast-sparse-not-real (apparent size only -> must read spent, not intact)" 0 "$SCRIPT" --config normal

mk_ballast "$BALLAST_DIR" absent absent
DF_TABLE="$WORK/df-ballast-spent"
# Raw host avail inflated by the full 200 KB the ballast used to occupy —
# 20000000 + 200 = 20000200 KB.
mk_df_table "$DF_TABLE" "/" "50000000" "/dev/sdd-build" "/mnt/e" "20000200" "/dev/sde-host"
WANT_TOKENS=("ballast_status: spent" "host_reading_kb: 20000000")
WANT_ABSENT=()
cell "D-8 ballast-spent (freed space excluded, status visible)" 0 "$SCRIPT" --config normal
mk_real_file "$BALLAST_DIR/b1.bin" "$BALLAST_PER_FILE_KB_MAIN"
mk_real_file "$BALLAST_DIR/b2.bin" "$BALLAST_PER_FILE_KB_MAIN"
mk_comfortable_df

echo
echo "== T002a: reserve-ballast check + refill =================================="

# T002a fixture plumbing: small per-file KB target so refills write bytes at
# KB scale, never GiB (guardrail). run_ensure invokes disk-preflight.sh with
# the SAME df/proc-version/proc-mounts fixtures as the arms above, plus the
# ballast-specific overrides. Sets OUT, RC.
run_ensure() {  # run_ensure <sut> [extra env assignments as "VAR=val" ...]
  local sut="$1"; shift
  OUT="$(
    FIXPP_DISK_PREFLIGHT_PROC_VERSION="$PV_FILE" \
    FIXPP_DISK_PREFLIGHT_PROC_MOUNTS="$PM_FILE" \
    FIXPP_DISK_PREFLIGHT_DF="$FAKE_DF" \
    FAKE_DF_TABLE="$DF_TABLE" \
    FIXPP_DISK_PREFLIGHT_BUILD_MOUNT="/" \
    FIXPP_DISK_PREFLIGHT_BALLAST_FILES="${BALLAST_TEST_FILES:-}" \
    FIXPP_DISK_PREFLIGHT_BALLAST_PER_FILE_KB="${BALLAST_TEST_PER_FILE_KB:-100}" \
    FIXPP_DISK_PREFLIGHT_BALLAST_REFILL_FLOOR_KB="${BALLAST_TEST_FLOOR_KB:-10}" \
    "$@" \
    "$sut" --ensure-ballast 2>&1
  )"; RC=$?
}

cell_ensure() {  # $1=label $2=want_rc("*" for nonzero) $3=sut, then WANT_TOKENS/WANT_ABSENT
  local label="$1" want_rc="$2" sut="$3"; shift 3
  run_ensure "$sut" "$@"
  if [ "$want_rc" = "nonzero" ]; then
    if [ "$RC" -eq 0 ]; then
      echo "  FAIL $label: want nonzero rc, got 0"; fail=$((fail+1)); print_out; return
    fi
  else
    if [ "$RC" -ne "$want_rc" ]; then
      echo "  FAIL $label: want rc=$want_rc, got rc=$RC"; fail=$((fail+1)); print_out; return
    fi
  fi
  local t
  for t in "${WANT_TOKENS[@]}"; do
    if ! grep -qF -- "$t" <<<"$OUT"; then
      echo "  FAIL $label: expected output to contain '$t'"; fail=$((fail+1)); print_out; return
    fi
  done
  for t in "${WANT_ABSENT[@]:-}"; do
    [ -z "$t" ] && continue
    if grep -qF -- "$t" <<<"$OUT"; then
      echo "  FAIL $label: expected output NOT to contain '$t'"; fail=$((fail+1)); print_out; return
    fi
  done
  echo "  ok   $label (rc=$RC)"
  pass=$((pass+1))
}

T2A_DIR="$WORK/ballast-t002a"
mkdir -p "$T2A_DIR"

PV_FILE="$PV_WSL"; PM_FILE="$PM_WITH_HOST"
mk_comfortable_df   # / plenty free, /mnt/e (host) plenty free, distinct sources

# ── Control: both files already at target, real bytes — intact, no writes ──
mk_real_file "$T2A_DIR/b1.bin" 100
mk_real_file "$T2A_DIR/b2.bin" 100
BALLAST_TEST_FILES="$T2A_DIR/b1.bin $T2A_DIR/b2.bin"
BALLAST_TEST_PER_FILE_KB=100
BALLAST_TEST_FLOOR_KB=10
WANT_TOKENS=("ballast_status: intact" "verdict: proceed")
WANT_ABSENT=("status=refilled" "status=short")
cell_ensure "T002a intact (real blocks at target, no refill attempted)" 0 "$SCRIPT"

# ── Refill success: one file short, host mount has plenty of room ──────────
rm -f "$T2A_DIR/b1.bin"
mk_real_file "$T2A_DIR/b1.bin" 10          # short of the 100 KB target
mk_real_file "$T2A_DIR/b2.bin" 100
WANT_TOKENS=("ballast_status: refilled" "verdict: proceed" "status=refilled")
WANT_ABSENT=()
cell_ensure "T002a refill-success (short file topped up, host has room)" 0 "$SCRIPT"
# The refill must have landed REAL allocated blocks at the target, not a
# hole — assert directly on the file this run just wrote, not on the
# script's own report of itself.
refilled_kb=$(( $(stat -c%b "$T2A_DIR/b1.bin") * 512 / 1024 ))
if [ "$refilled_kb" -ge 100 ]; then
  echo "  ok   T002a refill-lands-real-blocks (allocated_kb=$refilled_kb >= 100)"
  pass=$((pass+1))
else
  echo "  FAIL T002a refill-lands-real-blocks: allocated_kb=$refilled_kb, want >= 100"
  fail=$((fail+1))
fi

# ── STOP: refill would push the host mount below the floor — never refills ─
rm -f "$T2A_DIR/b1.bin" "$T2A_DIR/b2.bin"
mk_real_file "$T2A_DIR/b1.bin" 10
mk_real_file "$T2A_DIR/b2.bin" 10
# Host avail (KB) minus 2*100 KB needed must land BELOW the 10 KB floor:
# 215 - 200 = 15 (comfortable, control) vs 205 - 200 = 5 (below floor).
DF_TABLE="$WORK/df-t002a-tight-host"
mk_df_table "$DF_TABLE" "/" "50000000" "/dev/sdd-build" "/mnt/e" "205" "/dev/sde-host"
WANT_TOKENS=("ballast_status: spent-and-unrefillable" "verdict: stop")
WANT_ABSENT=("status=refilled")
cell_ensure "T002a stop-refill-would-cross-host-floor (never refills)" nonzero "$SCRIPT"
# Confirm the STOP path genuinely never wrote — both files stay short.
b1_kb=$(( $(stat -c%b "$T2A_DIR/b1.bin") * 512 / 1024 ))
if [ "$b1_kb" -lt 100 ]; then
  echo "  ok   T002a stop-path-never-writes (b1 stayed short at ${b1_kb}KB)"
  pass=$((pass+1))
else
  echo "  FAIL T002a stop-path-never-writes: b1 grew to ${b1_kb}KB despite STOP"
  fail=$((fail+1))
fi
mk_comfortable_df

# ── Control: off WSL — not-applicable, proceed (mirrors D-4) ───────────────
PV_FILE="$PV_NONWSL"
WANT_TOKENS=("ballast_check: not-applicable" "verdict: proceed")
WANT_ABSENT=()
cell_ensure "T002a not-wsl (control)" 0 "$SCRIPT"
PV_FILE="$PV_WSL"

# ── Forced miss: host mount unreadable — STOP, never proceed (mirrors D-3) ─
PM_FILE="$PM_NO_HOST"
WANT_TOKENS=("ballast_check: FAILED" "verdict: stop")
WANT_ABSENT=("verdict: proceed")
cell_ensure "T002a host-mount-unreadable (D-3-style, cannot verify refill safety)" nonzero "$SCRIPT"
PM_FILE="$PM_WITH_HOST"

echo
echo "== T002a spurious-hit arms (FR-018) ======================================="

# ── Spurious hit: BALLAST_FILES empty -> loop never runs -> vacuous "intact"
# unless guarded. Mutant deletes the emptiness guard; real script rejects.
M_T2A_EMPTY="$(mutate M-T2A-empty <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '''  if [ -z "${BALLAST_FILES// /}" ]; then
    emit "ballast_check: FAILED — no ballast paths configured (T002a)"
    emit "verdict: stop"
    return 1
  fi

'''
assert t.count(old) == 1
open(dst, "w").write(t.replace(old, ""))
PY
)"
if [ -n "$M_T2A_EMPTY" ]; then
  # A bare "" cannot be distinguished from "unset" through bash's ${VAR:-...}
  # default operator (both trigger the default) — the same reason the
  # threshold overrides above are never expressed that way. A whitespace-only
  # value survives the operator (non-empty string) and still collapses to
  # empty under the check's own ${VAR// /} strip, so it exercises the guard
  # without relying on an unrepresentable "explicitly empty" override.
  BALLAST_TEST_FILES=" "
  WANT_TOKENS=("ballast_status: intact" "verdict: proceed")
  WANT_ABSENT=()
  cell_ensure "T002a MUTANT empty-ballast-files -> spurious intact" 0 "$M_T2A_EMPTY"

  WANT_TOKENS=("ballast_check: FAILED" "no ballast paths configured" "verdict: stop")
  WANT_ABSENT=("verdict: proceed")
  cell_ensure "T002a REAL empty-ballast-files -> RED, same fixture" nonzero "$SCRIPT"
  BALLAST_TEST_FILES="$T2A_DIR/b1.bin $T2A_DIR/b2.bin"
fi

# ── Spurious hit: a SPARSE file reports full apparent size via `stat -c%s`
# while holding zero real blocks. Mutant switches allocated_kb() to %s;
# real script (blocks-based) correctly reports it as short, not intact.
rm -f "$T2A_DIR/sparse1.bin" "$T2A_DIR/sparse2.bin"
truncate -s 100K "$T2A_DIR/sparse1.bin"     # hole only — 0 real blocks
mk_real_file "$T2A_DIR/sparse2.bin" 100
sparse_blocks=$(stat -c%b "$T2A_DIR/sparse1.bin")
if [ "$sparse_blocks" -gt 0 ]; then
  echo "  SKIP T002a sparse-file arms: \$WORK filesystem does not support holes (blocks=$sparse_blocks)"
else
  M_T2A_SPARSE="$(mutate M-T2A-sparse <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '''allocated_kb() {  # $1=path -> KB of real allocated storage (0 if absent/unreadable)
  local p="$1" blocks
  [ -f "$p" ] || { echo 0; return; }
  blocks="$(stat -c%b "$p" 2>/dev/null)" || { echo 0; return; }
  [[ "$blocks" =~ ^[0-9]+$ ]] || { echo 0; return; }
  echo $(( blocks * 512 / 1024 ))
}'''
new = '''allocated_kb() {  # MUTANT: apparent size, not allocated blocks
  local p="$1" sz
  [ -f "$p" ] || { echo 0; return; }
  sz="$(stat -c%s "$p" 2>/dev/null)" || { echo 0; return; }
  [[ "$sz" =~ ^[0-9]+$ ]] || { echo 0; return; }
  echo $(( sz / 1024 ))
}'''
assert t.count(old) == 1
open(dst, "w").write(t.replace(old, new))
PY
)"
  if [ -n "$M_T2A_SPARSE" ]; then
    BALLAST_TEST_FILES="$T2A_DIR/sparse1.bin $T2A_DIR/sparse2.bin"
    WANT_TOKENS=("ballast_status: intact" "verdict: proceed")
    WANT_ABSENT=("status=short")
    cell_ensure "T002a MUTANT apparent-size-sparse-file -> spurious intact" 0 "$M_T2A_SPARSE"

    WANT_TOKENS=("status=short")
    WANT_ABSENT=()
    cell_ensure "T002a REAL blocks-based-sparse-file -> correctly short, same fixture" 0 "$SCRIPT"
    BALLAST_TEST_FILES="$T2A_DIR/b1.bin $T2A_DIR/b2.bin"
  fi
fi
mk_comfortable_df

echo
echo "PASS=$pass FAIL=$fail"
if [ "$fail" -gt 0 ]; then
  echo "ci/test-disk-preflight.sh: FAILED"
  exit 1
fi
echo "ci/test-disk-preflight.sh: PASS ($pass cells)"
