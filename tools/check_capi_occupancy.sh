#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/check_capi_occupancy.sh
#
# 049-c-abi-handles-errors (T014, FR-013 / data-model E-6) C-ABI occupancy /
# drift gate. TWO INDEPENDENT CHECKS that measure DIFFERENT quantities and are
# NEVER compared to each other ([2i §4.3] ~line 608 + P2-5; comparing the 2
# published decimal #defines against the 4 decimal source variants would always
# FAIL):
#
#   Check A — the PUBLISHED #define LAYOUT in include/fix/c_api/error.h equals
#             the authoritative [2i §4.3] master values (transcribed below), AND
#             tools/abi_history/error_codes_v1.txt restates the same
#             numeric→symbol bindings with no slot redefinition.
#
#   Check B — the SOURCE-DOMAIN variant-row counts documented in the [2i §4.3]
#             block-count table (.specify/2i-capi.md) equal the expected
#             coalescing-coverage table. This is a re-audit tripwire: if a
#             sibling [2X §6.X] error set grows and 2i §4.3 is updated, the
#             coalescing in src/capi/error.cpp must be re-audited.
#
# Reentrancy-class completeness is a SEPARATE discrete gate
# (tools/check_capi_reentrancy.sh) — NOT folded in here.
#
# Exit codes: 0 — both checks pass; 1 — any mismatch.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Paths default to the repo tree; overridable via env so the SC-004 negative
# fixture (tools/test_capi_occupancy_negative.sh) can point at a mutated copy.
error_h="${FIXPP_ERROR_H:-${repo_root}/include/fix/c_api/error.h}"
audit="${FIXPP_AUDIT:-${repo_root}/tools/abi_history/error_codes_v1.txt}"
capi_doc="${FIXPP_CAPI_DOC:-${repo_root}/.specify/2i-capi.md}"

fail=0
err() { echo "FAIL [check_capi_occupancy] $*" >&2; fail=1; }

[[ -f "$error_h" ]] || { err "missing $error_h"; exit 1; }
[[ -f "$audit"   ]] || { err "missing $audit";   exit 1; }
[[ -f "$capi_doc" ]] || { err "missing $capi_doc"; exit 1; }

# ── Check A — authoritative [2i §4.3] published #define layout ────────────────
# symbol → numeric. The single source of truth in this gate; error.h and the
# audit trail must each match it exactly.
declare -A EXPECTED=(
  [FIXPP_ERR_OK]=0 [FIXPP_ERR_CANCELLED]=1 [FIXPP_ERR_UNKNOWN]=2
  [FIXPP_ERR_NULL_HANDLE]=3 [FIXPP_ERR_INVALID_HANDLE]=4
  [FIXPP_ERR_VERSION_MISMATCH]=5 [FIXPP_ERR_BUFFER_TOO_SMALL]=6
  [FIXPP_ERR_TYPE_MISMATCH]=7 [FIXPP_ERR_TAG_NOT_FOUND]=8
  [FIXPP_ERR_INDEX_OUT_OF_RANGE]=9 [FIXPP_ERR_CAPI_CONFIG_INVALID]=10
  [FIXPP_ERR_WIRE_INVALID_FRAME]=100 [FIXPP_ERR_WIRE_LIMIT_EXCEEDED]=101
  [FIXPP_ERR_WIRE_CONFORMANCE]=102
  [FIXPP_ERR_DICT_CONFIG]=200 [FIXPP_ERR_DICT_LIMIT_EXCEEDED]=201 [FIXPP_ERR_DICT_OOM]=202
  [FIXPP_ERR_THREAD_CONFIG]=300 [FIXPP_ERR_THREAD_SESSION_LIFECYCLE]=301 [FIXPP_ERR_THREAD_RUNTIME]=302
  [FIXPP_ERR_STORE_RUNTIME]=400 [FIXPP_ERR_STORE_CONSISTENCY]=401
  [FIXPP_ERR_STORE_CONFIG]=402 [FIXPP_ERR_STORE_VISITOR]=403
  [FIXPP_ERR_SYNC_RUNTIME]=500
  [FIXPP_ERR_TLS_CONFIG]=600 [FIXPP_ERR_TLS_HANDSHAKE]=601
  [FIXPP_ERR_TLS_PINSET]=602 [FIXPP_ERR_TLS_RUNTIME]=603
  [FIXPP_ERR_TRANSPORT_LIFECYCLE]=700 [FIXPP_ERR_TRANSPORT_IO]=701
  [FIXPP_ERR_TRANSPORT_HANDSHAKE]=702 [FIXPP_ERR_TRANSPORT_CONFIG]=703
  [FIXPP_ERR_DECIMAL_INVALID]=800 [FIXPP_ERR_DECIMAL_PRECISION_LOSS]=801
  [FIXPP_ERR_CTRL_CONFIG]=900 [FIXPP_ERR_CTRL_RUNTIME]=901
  [FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED]=1200 [FIXPP_ERR_BINDING_SUBINTERPRETER]=1201
  [FIXPP_ERR_BINDING_OBJECT_LIFETIME]=1202 [FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH]=1203
  [FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE]=1204
)

# Parse error.h `#define FIXPP_ERR_X ((fixpp_error_t)N)` into a map.
declare -A HEADER=()
while IFS=$' \t' read -r sym val; do
  HEADER["$sym"]="$val"
done < <(grep -oE '#define[[:space:]]+FIXPP_ERR_[A-Z_]+[[:space:]]+\(\(fixpp_error_t\)[0-9]+\)' "$error_h" \
         | sed -E 's/#define[[:space:]]+(FIXPP_ERR_[A-Z_]+)[[:space:]]+\(\(fixpp_error_t\)([0-9]+)\)/\1 \2/')

# Parse audit trail data lines `<num>\t<SYMBOL>\t<minor>` into a map.
declare -A AUDIT=()
while read -r num sym _minor; do
  AUDIT["$sym"]="$num"
done < <(grep -E '^[0-9]+[[:space:]]+FIXPP_ERR_' "$audit")

# A.1 header == expected (exact set + values)
for sym in "${!EXPECTED[@]}"; do
  if [[ -z "${HEADER[$sym]:-}" ]]; then
    err "Check A: error.h missing published code $sym (expected ${EXPECTED[$sym]})"
  elif [[ "${HEADER[$sym]}" != "${EXPECTED[$sym]}" ]]; then
    err "Check A: error.h $sym = ${HEADER[$sym]} but [2i §4.3] master = ${EXPECTED[$sym]} (slot redefinition)"
  fi
done
for sym in "${!HEADER[@]}"; do
  [[ -n "${EXPECTED[$sym]:-}" ]] || err "Check A: error.h defines $sym not in the [2i §4.3] master layout"
done

# A.2 audit trail == expected (no slot redefinition vs the header/master)
for sym in "${!EXPECTED[@]}"; do
  if [[ -z "${AUDIT[$sym]:-}" ]]; then
    err "Check A: error_codes_v1.txt missing published code $sym"
  elif [[ "${AUDIT[$sym]}" != "${EXPECTED[$sym]}" ]]; then
    err "Check A: error_codes_v1.txt $sym = ${AUDIT[$sym]} but master = ${EXPECTED[$sym]} (slot redefinition)"
  fi
done
for sym in "${!AUDIT[@]}"; do
  [[ -n "${EXPECTED[$sym]:-}" ]] || err "Check A: error_codes_v1.txt lists $sym not in the master layout"
done

# ── Check B — source-domain variant-row counts ([2i §4.3] block-count table) ──
# NEVER compared to Check A — measures the C++-side variant counts, not the
# published #define layout. Expected coalescing-coverage table (data-model E-6).
declare -A EXPECT_COUNT=(
  [DECIMAL]=4 [WIRE]=13 [DICT]=20 [THREAD]=9
  [STORE]=10 [SYNC]=4 [TLS]=15 [TRANSPORT]=22
)
# Read the [2i §4.3] block-count rows ONCE, then match per-domain in-memory.
doc_rows="$(grep -E "^\|[[:space:]]*\`?FIXPP_ERR_[A-Z]+_\*\`?[[:space:]]*\|" "$capi_doc" || true)"
for domain in "${!EXPECT_COUNT[@]}"; do
  # Row form: | `FIXPP_ERR_DECIMAL_*` | 2a | 4 | `[2a §7.4]` |  → 3rd column's leading int.
  line="$(grep -E "\`?FIXPP_ERR_${domain}_\*\`?[[:space:]]*\|" <<<"$doc_rows" | head -1 || true)"
  if [[ -z "$line" ]]; then
    err "Check B: no [2i §4.3] block-count row for FIXPP_ERR_${domain}_*"
    continue
  fi
  count="$(echo "$line" | awk -F'|' '{print $4}' | grep -oE '[0-9]+' | head -1)"
  if [[ "$count" != "${EXPECT_COUNT[$domain]}" ]]; then
    err "Check B: [2i §4.3] FIXPP_ERR_${domain}_* count = ${count:-<none>} but coverage table expects ${EXPECT_COUNT[$domain]} — re-audit src/capi/error.cpp coalescing"
  fi
done

if [[ "$fail" -ne 0 ]]; then
  echo "check_capi_occupancy: FAILED" >&2
  exit 1
fi
echo "check_capi_occupancy: OK (Check A: ${#EXPECTED[@]} published codes; Check B: 8 source-domain counts)"
exit 0
