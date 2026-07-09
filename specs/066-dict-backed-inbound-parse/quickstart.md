# Quickstart: verify the 066 dictionary-backed inbound parse

All commands run with cwd inside the library submodule
(`research/G19-fix-fpml-iso20022/library`).

## 1. Reproduce the defect through REAL session dispatch (pre-fix, RED)

The critical methodology point: prove it on the SHIPPED path, not a `Parser{dict}` unit parse. Write a session-level witness that:
1. Constructs a `Session` (or the C-ABI engine) with a real FIX44 dictionary.
2. Drives a group-bearing frame through inbound dispatch (`on_inbound_frame` / the C-ABI receive path) — e.g. an `ExecutionReport` with `NoLegs(555)` carrying leg members followed by a trailing outer field.
3. In the application callback, reads the last group instance and queries the trailing outer field.

On the pre-fix dict-free parse this returns `FIXPP_ERR_OK` + a wrong value (extent runs to end-of-message). The witness asserts `TAG_NOT_FOUND` → **RED**. This RED-first proof is mandatory (`[const Art VII §3]`); the existing `Parser<Index>{dict}` tests are already green and cannot demonstrate the shipped-path defect.

## 2. Apply the fix, verify GREEN

After the change (session `inbound_tv_` built at `open()`; `parse_and_dispatch_` uses `Parser{*inbound_tv_}`):

```bash
cmake --preset linux-clang-debug
cmake --build build/linux-clang-debug -j2
# The new 066 witnesses are ctest-labeled `LABELS "066;..."` and their binary
# names all contain the literal substring `066` (session-side `test_066_*`,
# C-ABI `capi_dict066_*`, alloc-guard `dict066_grouped_read_alloc_guard*`) —
# `-R '066'` selects exactly and only that set (verified: 12/12 tests, no
# false negatives from a `-R 'session|...'` name-substring miss, since none
# of the `test_066_*` binary names contain the literal word "session").
ctest --test-dir build/linux-clang-debug -R '066' --output-on-failure
# Then the broader regression sweep (existing session/interop/C-ABI suites,
# unaffected by 066's own naming):
ctest --test-dir build/linux-clang-debug -R 'session|capi|interop' --output-on-failure
```

Expect: the new real-dispatch witnesses PASS (`test_066_group_membership_red`'s `TrailingFieldAbsentFromLastInstance` + `InteriorUndeclaredTagTruncatesInstance`, and their C-ABI mirror `capi_dict066_group_membership_red`: trailing field `TAG_NOT_FOUND` on the last instance; `test_066_scalar_as_group` / `capi_dict066_scalar_as_group`: scalar-as-group `TYPE_MISMATCH`), and the existing session/interop/C-ABI suites remain green (each intended behavior delta an explicit, reviewed edit — no silent breakage).

## 3. Clone / reify identity (FR-007)

Witness: clone an inbound dict-backed message (`fixpp_msg_clone`) and read the same group from the clone — assert the clone's result equals the source's (membership-bounded), not a positional divergence (`tests/capi/dict066_clone_identity_test.cpp::GroupMembershipCloneIdentity.CloneTrailingFieldAbsentFromLastInstance`). Same for a `reify` owning handle (`tests/dictionary/reify_membership_identity_test.cpp::ReifyMembershipIdentity.GroupMembershipSurvivesSourceDestruction`) and the shared T003 accessor it rests on (`tests/wire/message_view_membership_copy_test.cpp`).

## 4. No new global heap; arena fit (SC-004)

```bash
ctest --test-dir build/linux-clang-debug -R 'alloc' --output-on-failure
```

Confirm no new global-heap allocation on the inbound parse+read path (the `table_view` is built once at `open()`; per-message membership + nested sub-views come from the per-message stack arena). Arena fit is **witnessed** (SC-004 / FR-009), because dict-backed nested reads build sub-`OffsetTable`s from the stack arena — a NEW cost the dict-free path never incurred, landing on BOTH arenas:
- a representative group-bearing **app** message parses+reads within `kInboundParseArena` (16 KiB) — no heap fallback, successful read;
- a group-bearing **admin** message parses+reads within the tighter `kAdminParseArena` (8 KiB) — no heap fallback, successful read;
- a **near-cap / headroom probe** (a message approaching the arena bound still succeeds);
- a **pathological deeply-nested** message **fails closed** within the group-depth (`kMaxGroupDepth=16`) / entry caps and the arena — never over-read or corrupt (FR-009).

## 5. C-ABI freeze unchanged (SC-003)

```bash
ctest --test-dir build/linux-clang-debug -R 'abi|freeze' --output-on-failure
```

No delta in the exported-symbol golden / `capi_freeze.sha256` — the change is behavioral.

## 6. Sanitizers (Tier-1 mirror — /speckit-verify)

`/speckit-verify 066-dict-backed-inbound-parse` runs the ASan/UBSan/TSan matrix over the session + C-ABI paths, validating the `inbound_tv_` stable-address binding, the clone-owned `table_view` lifetime, and the reify view lifetime.

## 7. Prerequisite check for 065 (SC-005)

After 066 lands, re-plan 065 and run its real-dispatch C-ABI nested-read witness (issue #179 layout, `555 → 604 ×2 → 687`): RED before 066+065, GREEN after — confirming the dictionary-backed root is the precondition that makes the C-ABI cursor fix actually repair the shipped path.

## 8. T017 — captured group-bearing interop fixture assessment (SC-003)

**Path (b) taken — no captured fixture added; rationale recorded.**

`tests/interop/` (016-interop-harness) has two kinds of drivers, neither of which yields a usable *captured* (static, byte-frozen) group-bearing fixture:

1. `tests/interop/happy/`, `parity/`, `thorny/` — session-layer scenarios (Logon/Heartbeat/TestRequest/ResendRequest/SequenceReset/reject/framing cells). FIX session-admin messages carry no repeating groups, so this tier structurally cannot exercise C1/C3 group-membership behavior. Its captured `.fix` goldens under `tests/interop/happy/golden/` are all session-admin traffic (Logon/Heartbeat/idle-cadence/etc.) — none carry a `NoXXX` repeating group.
2. `tests/interop/test_business_message_interop.cpp` — the one business-message cell (NewOrderSingle→ExecutionReport), but (a) its `ExecutionReport` does not carry a repeating group (no `NoLegs`/`NoAllocs` etc.), and (b) every cell is **live-counterparty-driven**: it `GTEST_SKIP()`s unless a real QuickFIX-J/QuickFIX-cpp counterparty process is reachable via `INTEROP_<TOKEN>_PORT` env vars (orchestration lives in the gitignored parent `../phase-9-harness/`, not in this submodule). No such counterparty is running in this environment, and none of its cells are "captured" (pre-recorded, replayable) fixtures — they require the live process each run.

Searched the parent `phase-9-harness/results/` tree (perf-workload archive) for a prior group-bearing capture: `wl-05-nos-er-medium-groups` (fixpp-tls/-plain, quickfix-cpp, quickfixj) confirms group-bearing NOS/ExecutionReport traffic (with `NoLegs`-style groups) *has* been exchanged live against both reference engines historically — corroborating that such traffic is realistic and QuickFIX-interoperable — but that archive retains only `latency.hgrm`/`summary.json` perf artifacts, not wire-byte captures, so there is nothing there to lift into a correctness fixture either.

Spinning up a live QuickFIX-J/QuickFIX-cpp counterparty to capture a *new* group-bearing `.fix` golden is a live-infra operation (JVM/Maven or native counterparty process bring-up under `phase-9-harness/`) outside this task's "test/fixture/doc additions only" scope and the WSL2 build-resource cap; it is not undertaken here.

**Why this is not a coverage gap**: T004 (`tests/session/test_066_group_membership_red_test.cpp`) and T005 (`tests/capi/dict066_group_membership_red_test.cpp`) already drive a real group-bearing `ExecutionReport`/`NoLegs` frame end-to-end through the actual shipped dispatch path (real `Session::parse_and_dispatch_` and the C-ABI engine-loopback receive callback respectively) and assert the exact membership-bounded contract (C1/C3) this feature restores. These are the "real dispatch" witnesses SC-003 calls out by name; a QuickFIX-counterparty-driven fixture would exercise the identical shipped parse path with the same assertions, wrapped in TLS/live-process transport plumbing that adds no additional coverage of the 066 delta itself (the delta is purely in `parse_and_dispatch_`'s dictionary binding, not in transport/wire framing, which 066 does not touch).
