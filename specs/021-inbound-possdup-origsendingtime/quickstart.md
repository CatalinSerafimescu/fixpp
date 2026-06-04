# Quickstart: Inbound PossDup / OrigSendingTime Handling

**Feature**: 021-inbound-possdup-origsendingtime | **Date**: 2026-06-04

How to exercise and verify the feature. All commands run with cwd in the library submodule (`research/G19-fix-fpml-iso20022/library`); the parent harness lives in `phase-9-harness/`.

## 1. Unit witnesses (RED-first, per arm)

`tests/session/test_inbound_poss_dup.cpp` — one test per disposition row (data-model §1):

- **Arm A admin-ignore**: feed a too-low admin frame with `43=Y` + valid `122`; assert state stays `Active`, `seqnum_mgr_` expected unchanged, no Logout/Reject emitted.
- **Arm A app-drop (default)**: too-low app frame, `redeliver_poss_dup=false`; assert no `Application::fromApp` call, no advance, `Active`.
- **Arm A app-redeliver (opt-in)**: same frame, `redeliver_poss_dup=true`; assert exactly one `fromApp` call flagged possible-duplicate, still no advance.
- **Arm B regression pin**: too-low frame **without** `43=Y`; assert `Logout` + `Disconnected` (byte-identical to pre-feature behavior).
- **Arm C**: `43=Y`, no `122`; assert `Reject(35=3)` with `380=1`, `373=122`, session `Active`.
- **Arm D**: `43=Y`, `122 > 52`; assert `Reject(35=3)` `380=10` + `Logout` + `Disconnected`. Plus a boundary test `122 == 52` → **not** Arm D.
- **Arm E**: `35=4` + `43=Y` with no `122`; assert it is **not** rejected (routes to the existing reset path).
- **FR-008 send**: plain `send` of a message carrying caller `43`/`122` → stripped by default, retained when `allow_poss_dup=true`; auto-resend path always emits `43`/`122` regardless (resend regression pin).

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
- **SC-001** ← Arm A live replay-survives cell.
- **SC-002** ← Arm C live malformed-dup cell (both engines).
- **SC-003** ← Arm B unit regression pin + existing seqnum-too-low tests still green.
- **SC-004** ← both-role QFJ + QFcpp live cells green under normal + sanitizer builds.
