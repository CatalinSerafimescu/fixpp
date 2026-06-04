# Quickstart: Inbound PossDup / OrigSendingTime Handling

**Feature**: 021-inbound-possdup-origsendingtime | **Date**: 2026-06-04

How to exercise and verify the feature. All commands run with cwd in the library submodule (`research/G19-fix-fpml-iso20022/library`); the parent harness lives in `phase-9-harness/`.

## 1. Unit witnesses (RED-first, per arm)

Two files per the tasks.md split — `tests/session/test_inbound_poss_dup_tolerance.cpp` (US1, Arms A/B) and `tests/session/test_inbound_poss_dup_validation.cpp` (US2, Arms C/D/E) — one test per disposition row (data-model §1):

- **Arm A admin-ignore**: feed a too-low admin frame with `43=Y` + valid `122`; assert state stays `Active`, `seqnum_mgr_` expected unchanged, no Logout/Reject emitted (this tolerated arm is intentionally silent — assert *absence* of any wire/event emission, no positive event expected per FR-009).
- **Arm A app-drop (default)**: too-low app frame, `redeliver_poss_dup=false`; assert no `Application::fromApp` call, no advance, `Active` (tolerated arm, silent).
- **Arm A app-redeliver (opt-in)**: same frame, `redeliver_poss_dup=true`; assert exactly one `fromApp` call flagged possible-duplicate, still no advance.
- **Arm B regression pin**: too-low frame **without** `43=Y`; assert `→Disconnected` with **NO Logout wire frame** emitted (byte-identical to pre-feature `session.cpp:1860-1862` — `record_state_transition_` only). Pin the *absence* of a Logout frame.
- **Arm C**: `43=Y`, no `122`; assert `Reject(35=3)` with `371=122`, `373=1`, session `Active`.
- **Arm D**: `43=Y`, `122 > 52`; assert `Reject(35=3)` `371=122`, `373=10` + `Logout` + `Disconnected`. Plus a boundary test `122 == 52` → **not** Arm D.
- **Arm C at-expected**: `34==expected`, `43=Y`, **no** `122`; assert the same Arm-C `Reject(35=3)` `371=122`, `373=1` fires (validation is seqnum-independent — at-expected is NOT exempt).
- **Arm D at-expected**: `34==expected`, `43=Y`, `122 > 52`; assert the Arm-D `Reject(373=10)` + `Logout` + `Disconnected`.
- **Arm E**: `35=4` + `43=Y` with no `122`; assert it is **not** rejected (routes to the existing reset path).

> FR-008 send-path strip is **DEFERRED** out of this slice (opaque-send hardening — see spec Clarifications Gate A round 1); no send-path unit witness here.

Run (debug):
```bash
cmake --build build/debug --target fixpp_session_tests -j2
ctest --test-dir build/debug -R inbound_poss_dup --output-on-failure
```

Alloc discipline (no heap on the inbound path):
```bash
# counting_resource witness asserts 0 allocations on the new disposition arm
ctest --test-dir build/debug -R inbound_poss_dup -V | grep -i alloc
```

## 2. Live interop cells (skip-without-counterparty)

Extend the 018 admin-interop fixture + parent harness with PossDup-replay cells (both roles, QFJ + QFcpp):

- **PossDup replay survives**: drive a real ResendRequest→replay so the counterparty re-sends an already-seen message with `43=Y`; assert the fixpp session stays `Active` and does not Logout (SC-001).
- **Malformed dup rejected, survives**: feed `43=Y` without `122`; assert a session-level Reject crosses the wire and the session survives, matching QFJ + QFcpp (SC-002).

```bash
# parent harness (with counterparties available)
cd ../phase-9-harness
python tools/run_interop_cell.py --list | grep poss_dup
python tools/run_interop_cell.py <poss_dup_cell> --out results/
```

## 3. Sanitizers (Tier-1, one preset at a time per the build-resource cap)

```bash
for p in asan ubsan tsan; do
  cmake --build build/$p --target fixpp_session_tests -j2
  ctest --test-dir build/$p -R "inbound_poss_dup|interop" --output-on-failure
done
```

## 4. Success-criteria mapping
- **SC-001** ← Arm A live replay-survives cell (full gate); the Arm-A unit witnesses (`_tolerance.cpp`) locally smoke the behavior when no counterparty is present.
- **SC-002** ← Arm C live malformed-dup cell, both engines (full gate); the Arm-C/D unit witnesses (`_validation.cpp`) locally smoke the reject behavior.
- **SC-003** ← Arm B unit regression pin + existing seqnum-too-low tests still green.
- **SC-004** ← both-role QFJ + QFcpp live cells green under normal + sanitizer builds.
