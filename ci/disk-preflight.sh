#!/usr/bin/env bash
# ci/disk-preflight.sh — the disk gate every configure/build task in 089 depends
# on (contracts/089-quickfix-interop-conversation/disk-preflight.md).
#
# Usage:
#   ci/disk-preflight.sh --config <normal|asan|ubsan|tsan>
#   ci/disk-preflight.sh --config <c> --bootstrap \
#       --internal-free-kb N --host-growth-kb N --bootstrap-date YYYY-MM-DD
#
# ── Why two predicates, not one ─────────────────────────────────────────────
# `df` inside the WSL2 VHD (`/dev/sdd`) and `df` on the Windows host drive
# backing it (`E:`, a 9p drvfs mount) measure two DIFFERENT quantities: the
# internal reading bounds total data resident at once; the host reading bounds
# net new allocation the VHD may still grow into. They are evaluated
# INDEPENDENTLY (D-1) — gating on their minimum applies a threshold sized for
# one ceiling to the other, which is the exact ENOSPC this gate exists to
# prevent, reproduced by the gate's own arithmetic. See the contract's
# § "Why D-1 is two predicates and not a minimum".
#
# ── The threshold table — T001's measurement, 2026-09-11 ───────────────────
# ⚠️ Measured AS THE MATRIX PERFORMS IT (spec.md Clarifications 2026-09-11):
# INCREMENTALLY, in the existing per-configuration trees, not from clean —
# `cmake --build build/<preset> --target <the 18 interop-driver targets the
# EXISTING cells name>` (T052's four new-cell binaries do not exist yet;
# T052a re-derives once they do, before any later gated build). Method:
# before/peak/after `df -k /` and `df -k /mnt/e`, sampled every 3s during the
# build (peak = the running minimum availability seen, i.e. the largest draw-
# down observed), run under `--bootstrap` (D-9a's only admissible use).
# Non-vacuity of the sampler itself was proven first: a 500 MiB known write
# on each mount moved that mount's reading by exactly 512000 KB before this
# table was trusted.
#
# Measured deltas (KB), this run, this host state:
#   normal (build/linux-clang-debug, already fully built): internal peak
#     4812 (before 95831616 -> min 95826804), host peak 16 (unchanged in
#     practice) — near-zero, as expected for an already-populated tree.
#   asan   (build/linux-clang-asan, already fully built): internal peak
#     10432 (before 93032168 -> min 93021736), host peak 0 — near-zero,
#     despite ninja re-running all 154 steps (a codegen-fingerprint refresh
#     touched generated headers) because every output OVERWRITES a path
#     that already existed; nothing NEW was created.
#   tsan   (build/linux-clang-tsan, already fully built): internal delta
#     73576 (before 93033528 -> after 92959952), host peak 0 — same shape,
#     somewhat larger (a live ccache miss on a couple of TUs), still small.
#   ubsan  (build/linux-clang-ubsan, essentially EMPTY at measurement time —
#     128 objects vs siblings' ~1600, the config this measurement exists to
#     exercise): internal delta 2799200 (before 95831420 -> after 93032220,
#     the after reading, not the 3s-granularity peak, is the true worst case
#     here since the last sample landed slightly before final linking),
#     HOST DELTA 0 (before/peak/after all 17709580) — the whole ~2.67 GiB of
#     new internal data (codegen bootstrap + full core-library rebuild +
#     18 test binaries, all genuinely NEW files in this tree) landed in the
#     already-materialised VHD-on-host extents and cost the host mount
#     NOTHING. No breach of the 3 GiB host floor at any config.
#
# ⚠️ CONDITION THIS TABLE PRESUPPOSES (plan.md § Matrix sequencing / Reclaim
# procedure): asan/ubsan/tsan are ALL reclaimable and the matrix's own order
# (normal → ubsan → asan → tsan) reclaims-then-rebuilds asan and tsan too —
# so "asan/tsan already fully built" is a PROPERTY OF THIS MOMENT (2026-09-11),
# not of those configs. The first build of any of the three reclaimable
# configs AFTER that config has been reclaimed is the EMPTY-TREE shape (same
# as the ubsan measurement below), not the near-zero already-populated shape
# — this table prices asan/tsan for that shape directly (see headroom rule),
# rather than trusting they will always be consulted while still populated.
# `normal` is the one config genuinely exempt: it is NEVER reclaimed (standing
# user rule 2026-09-10), so it is always in the already-populated state this
# table measured.
#
# ── Headroom rule (stated per D-7/D-7a; dated 2026-09-11) ───────────────────
# required_internal_free = max(1.5 x measured_peak_delta_kb, floor_kb).
#   floor_kb = 200 MiB (204800 KB), used ONLY for `normal` (never reclaimed,
#   so always already-populated at consult time) — comfortably above the
#   largest of the already-populated peaks measured this run (73576 KB, tsan)
#   so the threshold is never so small it is trivially satisfied by an
#   almost-full disk (the same vacuous-near-zero failure D-9/A-7 forbid for an
#   UNSET slot, reproduced by a measured-but-negligible one).
#   asan/tsan/ubsan (all reclaimable, per the condition above) instead use the
#   SAME pessimistic figure: 1.5x ubsan's measured empty-tree peak delta
#   (2799200 KB -> 4198800 KB), because any of the three can legitimately be
#   consulted in the empty-tree state the matrix's own reclaim order produces,
#   and this run measured exactly that state only for ubsan. Using the
#   already-populated 200 MiB floor for asan/tsan here would be the vacuous-
#   near-zero failure above, at the config's WORST reachable state rather than
#   its current one — the ENOSPC this gate exists to prevent, reproduced by
#   trusting a moment instead of a condition.
#   ⚠️ Even the ubsan figure this borrows from is itself a PARTIAL-tree
#   measurement (128 objects w/ CMakeCache + conan_toolchain already present
#   at measurement start, not a true `rm -rf build/<preset>` from-scratch
#   build, which additionally pays configure + `conan install`). That
#   additional cost is UNMEASURED here, not assumed zero; it routes to the
#   same T052a re-derivation clause below, same as everything else this table
#   cannot yet certify.
#
# required_host_growth: D-7a requires this be R-1's MEASURED HOST DELTA, not
# derived from or offset by the reuse pool. All four configs measured a host
# delta at or effectively at 0 on THIS run — but per D-7a's own warning, the
# already-materialised reuse pool that made every write land free is a
# BOUND, not a guarantee, and "ext4 does not preferentially allocate into
# already-materialised extents" (plan.md/research.md), so it may simply not
# be realised on a future run with a differently-shaped VHD. Writing the
# measured 0 forward would make the host predicate VACUOUSLY satisfied by
# any host state whatsoever — precisely the A-7 spurious-hit shape, at the
# far more consequential predicate. So required_host_growth uses:
#   - normal: the SAME 200 MiB floor as its required_internal_free — never
#     reclaimed, always overwriting paths that already exist, so its true
#     host-growth exposure is inherently bounded by the same small figure,
#     not by the reuse-pool bound.
#   - asan/ubsan/tsan: the SAME pessimistic empty-tree value as their
#     required_internal_free (not a small floor) — reclaimed-then-rebuilt,
#     any of the three genuinely creates thousands of NEW files in that
#     state, so a reuse-pool failure here translates directly into real host
#     growth up to the full internal delta; research.md's own fallback
#     model is exactly "assume no reuse; host cost = build size", applied
#     here rather than trusting the one favourable measurement, and applied
#     to all three reclaimable configs rather than only the one measured
#     empty this run.
#
# Honestly labelled: measured against the EXISTING cells' 18 interop-driver
# targets, on this host's 2026-09-11 state; asan/tsan's EMPTY-tree figures are
# BORROWED from the ubsan measurement (same reclaimable-config shape), not
# independently measured empty — T052a re-derives once T052's four new-cell
# binaries exist, before any later gated build (D-7/T052a), and MAY also
# re-derive asan/tsan directly empty at that time rather than continuing to
# borrow ubsan's figure.
#
# ── No OTHER figures anywhere in this script ────────────────────────────────
# Every disk figure this bundle carried elsewhere was false within the day it
# was written (plan.md § "Disk preflight"). This script prints LIVE readings
# for every OTHER quantity; the threshold table above is the one place a
# dated, sourced figure belongs (D-7).
#
# ── Testability ──────────────────────────────────────────────────────────────
# Every external input this script reads is overridable from the environment,
# so ci/test-disk-preflight.sh can drive every arm without touching the real
# disk or /proc:
#   FIXPP_DISK_PREFLIGHT_PROC_VERSION   default /proc/version   (D-2)
#   FIXPP_DISK_PREFLIGHT_PROC_MOUNTS    default /proc/mounts    (D-2/D-10)
#   FIXPP_DISK_PREFLIGHT_DF             default df              (the df binary)
#   FIXPP_DISK_PREFLIGHT_BUILD_MOUNT    default /               (build-mount reading)
#   FIXPP_DISK_PREFLIGHT_BUILD_ROOT     default build           (reclaim candidates)
#   FIXPP_DISK_PREFLIGHT_BALLAST_FILES  default the two real reserve-ballast paths (D-8)
#   FIXPP_DISK_PREFLIGHT_REQUIRED_INTERNAL_FREE_KB (+ _DATE) — test-only threshold override
#   FIXPP_DISK_PREFLIGHT_REQUIRED_HOST_GROWTH_KB   (+ _DATE) — test-only threshold override
# ⚠️ A threshold override is NEVER silent: it requires ITS OWN _DATE sibling
# (missing one is itself a D-9 hard error) and is labelled "test-override" on
# every line it governs — same visibility discipline as --bootstrap below.
set -uo pipefail

# ── The threshold table — populated by T001, 2026-09-11 (see the block above)
declare -A INTERNAL_FREE_KB=( [normal]="204800" [asan]="4198800" [ubsan]="4198800" [tsan]="4198800" )
declare -A HOST_GROWTH_KB=(   [normal]="204800" [asan]="4198800" [ubsan]="4198800" [tsan]="4198800" )
declare -A THRESHOLD_DATE=(   [normal]="2026-09-11" [asan]="2026-09-11" [ubsan]="2026-09-11" [tsan]="2026-09-11" )

declare -A CONFIG_DIR=(
  [normal]="linux-clang-debug"
  [asan]="linux-clang-asan"
  [ubsan]="linux-clang-ubsan"
  [tsan]="linux-clang-tsan"
)

PROC_VERSION="${FIXPP_DISK_PREFLIGHT_PROC_VERSION:-/proc/version}"
PROC_MOUNTS="${FIXPP_DISK_PREFLIGHT_PROC_MOUNTS:-/proc/mounts}"
DF_BIN="${FIXPP_DISK_PREFLIGHT_DF:-df}"
BUILD_MOUNT="${FIXPP_DISK_PREFLIGHT_BUILD_MOUNT:-/}"
BUILD_ROOT="${FIXPP_DISK_PREFLIGHT_BUILD_ROOT:-build}"
# plan.md § "Disk preflight" step 2: "locate the host mount via /proc/mounts
# (/mnt/e is a 9p drvfs mount — VERIFIED)". /mnt/e is therefore the EXPECTED
# host mount point, not a blind first-match: a WSL host commonly exposes
# several drive letters as 9p drvfs mounts (C:, D:, E:, F: ...) and only one
# of them backs the VHD this gate cares about. The hint names which mount
# point to check; /proc/mounts is still what VERIFIES it is actually a 9p
# drvfs mount (D-2) rather than trusting the path outright.
HOST_MOUNT_HINT="${FIXPP_DISK_PREFLIGHT_HOST_MOUNT_HINT:-/mnt/e}"
# D-8: the ballast is a one-shot valve on the HOST drive, 5 GiB each
# (plan.md § "Reserve ballast — a one-shot valve"). The expected total is a
# CONSTANT cited from that anchor, never derived from the files' current
# sizes — deriving it from what is present would make a fully-spent valve
# read as "nothing to subtract", which is the fail-open direction.
BALLAST_FILES="${FIXPP_DISK_PREFLIGHT_BALLAST_FILES:-/mnt/e/_wsl-reserve-1.bin /mnt/e/_wsl-reserve-2.bin}"
BALLAST_EXPECTED_KB=$(( 2 * 5 * 1024 * 1024 ))  # 2 files x 5 GiB

# ── T002a: refill target + the refill's own safety floor ───────────────────
# plan.md § "Reserve ballast": "if spent, it must be refilled ... a spent
# valve nobody refills is worse than no valve". BALLAST_PER_FILE_KB is the
# per-file target (5 GiB); BALLAST_REFILL_FLOOR_KB is the floor a refill must
# not push the HOST mount below — refilling into an already-critical host
# would itself reproduce the ENOSPC this valve exists to prevent, so a refill
# is refused (never performed) when it would cross the floor. Both are
# overridable ONLY for ci/test-disk-preflight.sh (never write GiB files in a
# test), and neither override is silent — same discipline as the threshold
# overrides above (labelled "test-override" on every line it governs).
BALLAST_PER_FILE_KB="${FIXPP_DISK_PREFLIGHT_BALLAST_PER_FILE_KB:-$((5*1024*1024))}"
BALLAST_PER_FILE_SRC="embedded (5 GiB, plan.md \"Reserve ballast\")"
[ -n "${FIXPP_DISK_PREFLIGHT_BALLAST_PER_FILE_KB:-}" ] && BALLAST_PER_FILE_SRC="test-override"
BALLAST_REFILL_FLOOR_KB="${FIXPP_DISK_PREFLIGHT_BALLAST_REFILL_FLOOR_KB:-$((3*1024*1024))}"
BALLAST_REFILL_FLOOR_SRC="embedded (3 GiB stop-line)"
[ -n "${FIXPP_DISK_PREFLIGHT_BALLAST_REFILL_FLOOR_KB:-}" ] && BALLAST_REFILL_FLOOR_SRC="test-override"

CONFIG=""
BOOTSTRAP=0
BOOTSTRAP_INTERNAL_KB=""
BOOTSTRAP_HOST_KB=""
BOOTSTRAP_DATE=""
ENSURE_BALLAST=0

usage() {
  echo "usage: $0 --config <normal|asan|ubsan|tsan> [--bootstrap --internal-free-kb N --host-growth-kb N --bootstrap-date YYYY-MM-DD]" >&2
  echo "       $0 --ensure-ballast   # T002a: check + refill the reserve ballast" >&2
  exit 2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --config) CONFIG="${2:-}"; shift 2 ;;
    --bootstrap) BOOTSTRAP=1; shift ;;
    --internal-free-kb) BOOTSTRAP_INTERNAL_KB="${2:-}"; shift 2 ;;
    --host-growth-kb) BOOTSTRAP_HOST_KB="${2:-}"; shift 2 ;;
    --bootstrap-date) BOOTSTRAP_DATE="${2:-}"; shift 2 ;;
    --ensure-ballast) ENSURE_BALLAST=1; shift ;;
    *) echo "usage: unknown argument '$1'" >&2; usage ;;
  esac
done

if [ "$ENSURE_BALLAST" != "1" ]; then
  case "$CONFIG" in
    normal|asan|ubsan|tsan) ;;
    *) echo "usage: --config must be one of normal|asan|ubsan|tsan, got '${CONFIG}'" >&2; usage ;;
  esac
fi

# D-9a bootstrap: admissible ONLY with all three flags present. This script
# cannot police WHO calls it with --bootstrap (that discipline is procedural,
# per the contract's own "made visible in the log rather than left
# inferable"); what it CAN enforce is that bootstrap mode is never reachable
# by accident — every one of the three values is required, and their absence
# is the same D-9 hard error ordinary operation hits.
LINE_PREFIX=""
if [ "$BOOTSTRAP" = "1" ]; then
  if [ -z "$BOOTSTRAP_INTERNAL_KB" ] || [ -z "$BOOTSTRAP_HOST_KB" ] || [ -z "$BOOTSTRAP_DATE" ]; then
    echo "hard_error: --bootstrap requires --internal-free-kb, --host-growth-kb AND --bootstrap-date (D-9a)"
    exit 2
  fi
  # D-9a: the label prints on EVERY output line, so bootstrap use is visible
  # in the log rather than inferable.
  LINE_PREFIX="[BOOTSTRAP: estimated, pending R-1, date=${BOOTSTRAP_DATE}] "
fi

emit() { printf '%s%s\n' "$LINE_PREFIX" "$1"; }

hard_error() {
  emit "hard_error: $1"
  exit 2
}

# ── df wrapper: sets DF_AVAIL_KB / DF_SOURCE, returns 1 on any failure ──────
read_df() {
  local mount="$1" out
  out="$("$DF_BIN" -k --output=avail,source "$mount" 2>/dev/null | tail -n +2)" || return 1
  [ -n "$out" ] || return 1
  DF_AVAIL_KB="$(awk '{print $1}' <<<"$out")"
  DF_SOURCE="$(awk '{print $2}' <<<"$out")"
  [[ "$DF_AVAIL_KB" =~ ^[0-9]+$ ]] || return 1
  return 0
}

is_wsl() {
  [ -r "$PROC_VERSION" ] && grep -qi microsoft "$PROC_VERSION" 2>/dev/null
}

# D-2/D-10: locate the host mount from /proc/mounts — a 9p drvfs mount —
# INDEPENDENTLY of the build mount. No fallback to the build mount lives here;
# the equality check against BUILD_SOURCE (below, in main) is the second half
# of D-10.
resolve_host_mount() {
  [ -r "$PROC_MOUNTS" ] || return 1
  local m
  # $4 is the WSL2 drvfs options field, e.g.
  # "rw,noatime,aname=drvfs;path=E:\;uid=1000;...". drvfs is marked by
  # `aname=drvfs`, not a bare comma-delimited "drvfs" token.
  m="$(awk -v hint="$HOST_MOUNT_HINT" \
       '$2==hint && $3=="9p" && $4 ~ /aname=drvfs/ {print $2; exit}' \
       "$PROC_MOUNTS" 2>/dev/null)"
  [ -n "$m" ] || return 1
  HOST_MOUNT="$m"
  return 0
}

# ── T002a: reserve-ballast check + refill ───────────────────────────────────
# The other half of D-8: D-8 makes the preflight not COUNT the ballast as
# available; this makes it not silently STAY spent. Run as an explicit step
# BEFORE every configure/build this gate governs (never at the end of the
# matrix — plan.md "the valve must be intact for the run that needs it").
#
# Presence is measured by ALLOCATED BLOCKS (`stat -c%b` * 512), never
# apparent size (`stat -c%s`): a sparse file can report the full target via
# %s while holding zero real blocks, which would report "intact" while the
# valve is empty.
allocated_kb() {  # $1=path -> KB of real allocated storage (0 if absent/unreadable)
  local p="$1" blocks
  [ -f "$p" ] || { echo 0; return; }
  blocks="$(stat -c%b "$p" 2>/dev/null)" || { echo 0; return; }
  [[ "$blocks" =~ ^[0-9]+$ ]] || { echo 0; return; }
  echo $(( blocks * 512 / 1024 ))
}

ensure_ballast() {
  if ! is_wsl; then
    # The ballast is a host-mount (E:) mechanism; off WSL there is no host
    # mount to protect and nothing to refill — mirrors D-4's "not applicable",
    # a different case from "unreadable" below.
    emit "ballast_check: not-applicable (not WSL)"
    emit "verdict: proceed"
    return 0
  fi
  if ! resolve_host_mount || ! read_df "$HOST_MOUNT"; then
    # Mirrors D-3: an unresolvable/unreadable host mount is a FAILURE, never
    # a pass — a refill cannot be shown safe without the host reading it
    # would draw down.
    emit "ballast_check: FAILED — host mount unresolvable/unreadable (cannot verify refill safety)"
    emit "verdict: stop"
    return 1
  fi
  local host_avail_kb="$DF_AVAIL_KB"

  # An empty/unset file list is a misconfiguration, not "nothing to refill":
  # the loop below would simply never run and vacuously report "intact",
  # which is a spurious hit on the exact property this check exists to
  # measure (there being real ballast, not merely no ballast to check).
  if [ -z "${BALLAST_FILES// /}" ]; then
    emit "ballast_check: FAILED — no ballast paths configured (T002a)"
    emit "verdict: stop"
    return 1
  fi

  emit "ballast_per_file_kb: ${BALLAST_PER_FILE_KB}"
  emit "ballast_per_file_source: ${BALLAST_PER_FILE_SRC}"
  emit "ballast_refill_floor_kb: ${BALLAST_REFILL_FLOOR_KB}"
  emit "ballast_refill_floor_source: ${BALLAST_REFILL_FLOOR_SRC}"
  emit "host_mount: ${HOST_MOUNT}"
  emit "host_reading_kb: ${host_avail_kb}"

  local need_refill=() f present_kb
  for f in $BALLAST_FILES; do
    present_kb="$(allocated_kb "$f")"
    if [ "$present_kb" -ge "$BALLAST_PER_FILE_KB" ]; then
      emit "ballast_file: ${f} present_kb=${present_kb} status=intact"
    else
      emit "ballast_file: ${f} present_kb=${present_kb} status=short"
      need_refill+=("$f")
    fi
  done

  if [ "${#need_refill[@]}" -eq 0 ]; then
    emit "ballast_status: intact"
    emit "verdict: proceed"
    return 0
  fi

  # Pessimistic, on the same "assume no reuse" posture R-1 takes for the
  # build cost itself: assume each refill costs the FULL per-file target in
  # host growth. No partial refill — either every short file can be safely
  # refilled now, or none is touched and the run STOPs (D-5's rule: never
  # warn-and-continue, applied here to the valve rather than the gate).
  local total_needed_kb=$(( BALLAST_PER_FILE_KB * ${#need_refill[@]} ))
  local projected_kb=$(( host_avail_kb - total_needed_kb ))
  emit "ballast_refill_total_needed_kb: ${total_needed_kb}"
  emit "ballast_refill_projected_host_kb: ${projected_kb}"

  if [ "$projected_kb" -lt "$BALLAST_REFILL_FLOOR_KB" ]; then
    emit "ballast_status: spent-and-unrefillable"
    emit "reason: refilling would leave the host mount below the floor (${BALLAST_REFILL_FLOOR_KB} KB) (T002a)"
    emit "verdict: stop"
    return 1
  fi

  # Real blocks, never sparse: dd writes actual zero bytes throughout (unlike
  # truncate/fallocate over a 9p/drvfs mount, which may leave a hole). dd
  # truncates the destination to the write length by default (no
  # conv=notrunc), so this always lands the file at exactly the rounded
  # target regardless of what was there before.
  local mb_count=$(( (BALLAST_PER_FILE_KB + 1023) / 1024 ))
  for f in "${need_refill[@]}"; do
    if ! dd if=/dev/zero "of=$f" bs=1M "count=$mb_count" status=none 2>/dev/null; then
      emit "ballast_file: ${f} refill_failed"
      emit "ballast_status: spent-and-unrefillable"
      emit "verdict: stop"
      return 1
    fi
    present_kb="$(allocated_kb "$f")"
    if [ "$present_kb" -lt "$BALLAST_PER_FILE_KB" ]; then
      emit "ballast_file: ${f} refill_incomplete present_kb=${present_kb}"
      emit "ballast_status: spent-and-unrefillable"
      emit "verdict: stop"
      return 1
    fi
    emit "ballast_file: ${f} present_kb=${present_kb} status=refilled"
  done

  emit "ballast_status: refilled"
  emit "verdict: proceed"
  return 0
}

if [ "$ENSURE_BALLAST" = "1" ]; then
  ensure_ballast
  exit $?
fi

# ── Threshold resolution — three legal sources, none silent ────────────────
# resolve_threshold PREDICATE(internal|host) -> sets THR_KB THR_DATE THR_SRC
resolve_threshold() {
  local predicate="$1"
  if [ "$BOOTSTRAP" = "1" ]; then
    THR_SRC="estimated, pending R-1"
    THR_DATE="$BOOTSTRAP_DATE"
    if [ "$predicate" = "internal" ]; then THR_KB="$BOOTSTRAP_INTERNAL_KB"; else THR_KB="$BOOTSTRAP_HOST_KB"; fi
    return
  fi
  local ov_val ov_date
  if [ "$predicate" = "internal" ]; then
    ov_val="${FIXPP_DISK_PREFLIGHT_REQUIRED_INTERNAL_FREE_KB:-}"
    ov_date="${FIXPP_DISK_PREFLIGHT_REQUIRED_INTERNAL_FREE_KB_DATE:-}"
  else
    ov_val="${FIXPP_DISK_PREFLIGHT_REQUIRED_HOST_GROWTH_KB:-}"
    ov_date="${FIXPP_DISK_PREFLIGHT_REQUIRED_HOST_GROWTH_KB_DATE:-}"
  fi
  if [ -n "$ov_val" ]; then
    THR_KB="$ov_val"
    THR_DATE="$ov_date"
    THR_SRC="test-override"
    return
  fi
  if [ "$predicate" = "internal" ]; then
    THR_KB="${INTERNAL_FREE_KB[$CONFIG]}"
  else
    THR_KB="${HOST_GROWTH_KB[$CONFIG]}"
  fi
  THR_DATE="${THRESHOLD_DATE[$CONFIG]}"
  THR_SRC="embedded-table"
}

# D-9: unset OR unparseable OR missing its date is a hard error — never 0.
# Validated BEFORE any arithmetic, so an empty/non-numeric value cannot
# silently become 0 in a `(( ))` comparison.
validate_threshold() {
  local predicate="$1" val="$2" date="$3"
  if ! [[ "$val" =~ ^[0-9]+$ ]]; then
    hard_error "threshold_unset_or_unparseable predicate=${predicate} config=${CONFIG} value='${val}' (D-9)"
  fi
  if [ -z "$date" ]; then
    hard_error "threshold_missing_measurement_date predicate=${predicate} config=${CONFIG} (D-9)"
  fi
}

# ── Reclaim candidates — never `normal`, never the config being built ──────
reclaimable_configs() {
  local c dir out=""
  for c in asan ubsan tsan; do
    [ "$c" = "$CONFIG" ] && continue
    dir="${BUILD_ROOT}/${CONFIG_DIR[$c]}"
    if [ -d "$dir" ] && [ -n "$(find "$dir" -mindepth 1 -print -quit 2>/dev/null)" ]; then
      out="${out} ${c}"
    fi
  done
  printf '%s' "${out# }"
}

# ════════════════════════════════════════════════════════════════════════════
# main
# ════════════════════════════════════════════════════════════════════════════

WSL="no"
is_wsl && WSL="yes"

read_df "$BUILD_MOUNT" || hard_error "cannot read build mount '${BUILD_MOUNT}' via df"
BUILD_AVAIL_KB="$DF_AVAIL_KB"
BUILD_SOURCE="$DF_SOURCE"

resolve_threshold internal
validate_threshold internal "$THR_KB" "$THR_DATE"
REQ_INTERNAL_KB="$THR_KB"; REQ_INTERNAL_DATE="$THR_DATE"; REQ_INTERNAL_SRC="$THR_SRC"

INTERNAL_OK=0
[ "$BUILD_AVAIL_KB" -ge "$REQ_INTERNAL_KB" ] && INTERNAL_OK=1

# Computed ONCE and reused for both the verdict decision and the printed
# report — two independent calls could silently disagree if the candidate
# rule ever changes, and the log must say what the decision actually used.
RECLAIM_OUT="$(reclaimable_configs)"

FAILING_PREDICATE="none"
VERDICT=""
REASON=""
HOST_MOUNT_OUT="not-applicable"
HOST_READING_OUT="not-applicable"
REQ_HOST_KB_OUT="not-applicable"
REQ_HOST_SRC_OUT="not-applicable"
REQ_HOST_DATE_OUT="not-applicable"
BALLAST_PRESENT_OUT="not-applicable"
BALLAST_STATUS_OUT="not-applicable"

if [ "$WSL" = "yes" ]; then
  if ! resolve_host_mount; then
    # D-3: on WSL, an absent host mount is a FAILURE, never a pass.
    FAILING_PREDICATE="host"
    REASON="host mount absent (D-3)"
    HOST_MOUNT_OUT="UNREADABLE"
    HOST_READING_OUT="UNREADABLE"
    VERDICT="stop"
  elif ! read_df "$HOST_MOUNT"; then
    # D-3: located but unreadable.
    FAILING_PREDICATE="host"
    REASON="host mount '${HOST_MOUNT}' unreadable via df (D-3)"
    HOST_MOUNT_OUT="$HOST_MOUNT"
    HOST_READING_OUT="UNREADABLE"
    VERDICT="stop"
  else
    HOST_AVAIL_KB_RAW="$DF_AVAIL_KB"
    HOST_SOURCE="$DF_SOURCE"
    HOST_MOUNT_OUT="$HOST_MOUNT"
    if [ "$HOST_SOURCE" = "$BUILD_SOURCE" ]; then
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
      resolve_threshold host
      validate_threshold host "$THR_KB" "$THR_DATE"
      REQ_HOST_KB_OUT="$THR_KB"; REQ_HOST_DATE_OUT="$THR_DATE"; REQ_HOST_SRC_OUT="$THR_SRC"

      # D-8: reserve ballast not counted as available. Subtract whatever the
      # ballast is currently SHORT of its expected 10 GiB from the raw host
      # reading — that is space the valve freed, not space the normal budget
      # owns. This keeps the BUDGET reading flat whether the valve is intact
      # or spent (by design — D-8 only says "not counted as available"); the
      # ballast_status line below makes the spend itself visible, since
      # nothing else on this gate's output would say so (plan.md § "Reserve
      # ballast": "a spent valve nobody refills is worse than no valve").
      present_kb=0
      for f in $BALLAST_FILES; do
        if [ -f "$f" ]; then
          sz="$(stat -c%s "$f" 2>/dev/null || echo 0)"
          present_kb=$(( present_kb + sz / 1024 ))
        fi
      done
      missing_kb=$(( BALLAST_EXPECTED_KB - present_kb ))
      [ "$missing_kb" -lt 0 ] && missing_kb=0
      host_adj_kb=$(( HOST_AVAIL_KB_RAW - missing_kb ))
      [ "$host_adj_kb" -lt 0 ] && host_adj_kb=0
      HOST_READING_OUT="$host_adj_kb"
      BALLAST_PRESENT_OUT="$present_kb"
      if [ "$present_kb" -ge "$BALLAST_EXPECTED_KB" ]; then
        BALLAST_STATUS_OUT="intact"
      elif [ "$present_kb" -le 0 ]; then
        BALLAST_STATUS_OUT="spent"
      else
        BALLAST_STATUS_OUT="partial"
      fi

      HOST_OK=0
      [ "$host_adj_kb" -ge "$REQ_HOST_KB_OUT" ] && HOST_OK=1

      if [ "$INTERNAL_OK" = 1 ] && [ "$HOST_OK" = 1 ]; then
        FAILING_PREDICATE="none"; VERDICT="proceed"
      elif [ "$INTERNAL_OK" = 0 ] && [ "$HOST_OK" = 0 ]; then
        FAILING_PREDICATE="both"; VERDICT="stop"
      elif [ "$HOST_OK" = 0 ]; then
        # 1a: reclaim-first is NEVER offered for a host failure — reclaim
        # returns nothing to the host.
        FAILING_PREDICATE="host"; VERDICT="stop"
      else
        FAILING_PREDICATE="internal"
        if [ -n "$RECLAIM_OUT" ]; then VERDICT="reclaim-first"; else VERDICT="stop"; fi
      fi
    fi
  fi
else
  # D-4: off WSL, the host predicate is SKIPPED — a different case from D-3,
  # on a different code path. required_internal_free alone governs.
  if [ "$INTERNAL_OK" = 1 ]; then
    FAILING_PREDICATE="none"; VERDICT="proceed"
  else
    FAILING_PREDICATE="internal"
    if [ -n "$RECLAIM_OUT" ]; then VERDICT="reclaim-first"; else VERDICT="stop"; fi
  fi
fi

RECLAIM_PRINT="$RECLAIM_OUT"
[ -z "$RECLAIM_PRINT" ] && RECLAIM_PRINT="(none)"

# D-6: print both readings, both required values with their dates, the
# failing predicate and the verdict — on EVERY invocation, pass or fail.
emit "config: ${CONFIG}"
emit "wsl: ${WSL}"
emit "build_mount: ${BUILD_MOUNT}"
emit "internal_reading_kb: ${BUILD_AVAIL_KB}"
emit "required_internal_free_kb: ${REQ_INTERNAL_KB}"
emit "required_internal_free_source: ${REQ_INTERNAL_SRC}"
emit "required_internal_free_date: ${REQ_INTERNAL_DATE}"
emit "host_mount: ${HOST_MOUNT_OUT}"
emit "host_reading_kb: ${HOST_READING_OUT}"
emit "required_host_growth_kb: ${REQ_HOST_KB_OUT}"
emit "required_host_growth_source: ${REQ_HOST_SRC_OUT}"
emit "required_host_growth_date: ${REQ_HOST_DATE_OUT}"
emit "ballast_present_kb: ${BALLAST_PRESENT_OUT}"
emit "ballast_status: ${BALLAST_STATUS_OUT}"
emit "reclaimable_configs: ${RECLAIM_PRINT}"
[ -n "$REASON" ] && emit "reason: ${REASON}"
emit "failing_predicate: ${FAILING_PREDICATE}"
emit "verdict: ${VERDICT}"

case "$VERDICT" in
  proceed) exit 0 ;;
  *) exit 1 ;;
esac
