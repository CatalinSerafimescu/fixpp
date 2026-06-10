# Implementation Plan: Received-Reset Inbound Advance Correction (acceptor 141=Y off-by-one)

**Branch**: `030-received-reset-inbound-advance` | **Date**: 2026-06-10 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/030-received-reset-inbound-advance/spec.md`

## Summary

A **conformance bug fix** on the received-141 path (peer `Logon(141=Y)`, local `reset_on_logon`
knob OFF). Today the engine consumes the peer reset Logon — `check_inbound` advances
next-expected-inbound 1→2 — and then `reset_seqnums_to_one_durable()` unconditionally
re-bases **both** counters back to 1, clobbering that advance. Net next-expected-inbound = 1
instead of 2, so the peer's next genuine message (seq 2) reads too-high → fixpp emits a
**spurious `ResendRequest`**, and (when 027 advertisement is on) the reply Logon advertises
`789=1` instead of `2`.

**The reference engines do it in the opposite order** (grounded in `research.md` /
spec Clarifications): QuickFIX-cpp and QuickFIX-J **reset *then* increment** — `reset()`
rebases to 1, then the consumed in-sequence reset Logon advances target 1→2 (QFcpp
`nextLogon` ~206/265; QFJ 2215/2303), and the 789 advertisement is `nextTarget(1)+1=2`
(QFcpp ~710 "incoming Logon did not increment the target SeqNum yet"; QFJ 2269-2278).
fixpp's own `reset_on_logon=true` knob path already nets 2. **Only the received-141
(knob-off) arm is wrong.**

**Fix (manager-only inbound restore):** after the durable reset on the received-141 arm,
restore the in-memory next-expected-inbound to `seqnum_min + 1` (= 2) via
`SeqnumManager::set_next_inbound`, guarded on the reset Logon having actually been
consumed (`logon_inbound_advanced`). The **outbound** counter stays re-based to
`seqnum_min` so the reply Logon's `MsgSeqNum` is byte-identical (seq 1); only the `789`
*content* corrects 1→2 (027-on). The restore is **manager-only — no durable persist** — so
the store stays at `seqnum_min` ≤ manager (**INV-H1 lower-bound preserved**, 029 spine).

The 024 comment that placed the reset *after* the inbound check (for outbound byte-identity)
was correct *for the outbound side* but over-applied — it conflated "outbound reply = seq 1"
with "inbound next-expected = 1". This fix corrects the inbound side only and leaves the
outbound rebase (and the comment's outbound rationale) intact.

**Scope is tiny (~18 LoC + tests), risk is in the cross-feature pins.** 5 pre-existing tests
across 013/024/027/029 currently **pin the defective value** (next_inbound=1 / `789=1`);
they are stale pins of this off-by-one and must flip to the corrected value, each individually
re-verified as pinning *this* case.

**Provenance:** prototyped + harm-confirmed on the isolated branch
`fix/acceptor-received-141-next-inbound` (manager-only restore; `session_reset_on_lifecycle`
22/22 green; harm reproduced deterministically — peer seq-2 → spurious ResendRequest). This
pipeline re-derives that fix TDD-first with the discriminating triple witness and lands it
properly with the pin updates + full verify matrix + Gate B.

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: `Session::on_inbound_frame` received-141 arm (`session.cpp` ~:1941, immediately after `reset_seqnums_to_one_durable(reset_disposition::logged)` and inside the `peer_sent_reset && !cfg_.reset_on_logon` block); `SeqnumManager::set_next_inbound` (existing, used by 027 behind-side); the `logon_inbound_advanced` local set after `check_inbound` success (~:1798); 027 `honor_peer_next_expected_`/789 advertisement (`next_inbound_unsafe()` read). No new deps, no codegen, no wire field, no new error slot, no new config knob.
**Storage**: the existing `MessageStore`. The restore is **manager-only** — the durable reset already wrote `seqnum_min`; we do NOT re-persist (INV-H1: store ≤ manager). No store schema/interface change.
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov. New + amended witnesses: the discriminating **triple** (next_inbound==2 AND reply.MsgSeqNum==1 AND reply.789==2, 027-on); harm-repro (peer seq-2 accepted, NO ResendRequest); guard (no consumed reset Logon ⇒ no restore); `reset_on_logon=true` knob path unchanged; policy matrix (bilateral-strict/lenient, unilateral); INV-H1 (store==seqnum_min ≤ manager==2 after fix); non-persistent store. Live acceptor interop cell re-run vs QFcpp/QFJ (skip-without-counterparty). — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cell runs vs QFcpp/QFJ in the parent harness.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: one extra in-memory counter set on the received-141 path only (cold/rare); no hot-path or allocation change; default (no reset / knob-off-no-141) paths untouched.
**Constraints**: `noexcept`/`expected_t` preserved; the restore reuses the existing `set_next_inbound` awaitable (no new include into the `session.hpp` closure — [const §XV.9] N/A, confirm at verify); manager-only (no over-persist) preserves INV-H1; `reset_on_logon=true` and all steady-state paths **byte-identical**.
**Scale/Scope**: +1 guarded `set_next_inbound(seqnum_min+1)` call on the received-141 arm (~6 effective LoC + comment); 2 net-new regression tests in `tests/session/test_reset_on_lifecycle.cpp`; 5 cross-feature pin flips. No FSM state, no new store/config surface, no codegen/C-ABI/wire change.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1.*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **No net-new catalogue row** — this corrects behavior already owned by 013/024 (received-141 reset) under existing rows S-005/S-006/S-014/S-024; the 027 row S-031 (789 advertisement) gains a corrected received-141 sub-behavior. Amend those rows' notes to cite 030 as the conformance correction, and add a **behavior** entry (received-141 nets next-expected-inbound=2, reference-engine-conformant) + a stale-pin note. No new normative ref beyond 013/024/027's `[FIX-SL §4.x]` logon/reset/recovery (cite `[FIX-SL §4.3.12]` synchronization-after-logon, `[FIX-SL §4.6]` ResetSeqNumFlag). Exact §VI delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: (1) the discriminating **triple** (027-on); (2) harm-repro — post-141 peer seq-2 accepted in-sequence, **NO ResendRequest** (currently RED: emits one); (3) next_inbound==2 immediately post-Logon (knob-off); (4) guard — no consumed reset Logon ⇒ no restore (no spurious set); (5) `reset_on_logon=true` path unchanged; (6) INV-H1 (durable store==seqnum_min ≤ manager==2); (7) policy matrix + non-persistent store hold. The 2 prototype tests (`ResetOnLogon_Off_Received141_NextInboundIsTwo`, `Received141_PeerNextMsgSeq2_HarmCheck`) seed (2)/(3). | ✅ planned |
| **VII.6** Interop | live acceptor cell: peer `141=Y` reset vs QFcpp/QFJ → session reaches Active, **zero** fixpp ResendRequest, peer seq-2 accepted (SC-001, the true close-out) | ✅ planned |
| **VIII.5** Allocator | restore is a counter set (no container, no frame body); no new allocation; the existing no-heap witnesses on the reset path remain green | ✅ PASS (no new alloc) |
| **IX.1** Coverage | ≥95/85 on the new guarded restore branch (both arms: `logon_inbound_advanced` true→restore, false→skip) + the failure arm of `set_next_inbound` | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the received-141 session path + the amended/new reset tests + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot/wire change; reuses an existing `SeqnumManager` method; source rebuild only | ✅ source rebuild (no surface change) |
| **XI.4** Threading | the restore runs on the existing session strand inside the inbound handler (same strand as the reset it follows); reuses `set_next_inbound`'s `async_mutex`; no new concurrency surface | ✅ PASS |
| **XII.5** No-implicit-default | **no new config flag** — the fix is unconditional on the existing received-141 arm; gated only by the pre-existing `peer_sent_reset && !reset_on_logon` condition and the `logon_inbound_advanced` consume-guard | ✅ PASS (no new knob) |
| **XIV.2** Pluggable ≤5 pure-virtual | no interface touched (`MessageStore` stays at 4; manager-only restore adds no virtual) | ✅ PASS |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | no new include; `set_next_inbound` already in the awaitable corpus | ✅ N/A (confirm at verify) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-10 — no user-facing decisions; the one assumption grounded by the QFcpp/QFJ source sweep (reset-then-increment; QFJ 2202-2204 infers reset from MsgSeqNum==1) | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⏳ PENDING (Gate A next) |

**Result**: PASS to proceed. No new config/ABI/wire/interface surface; INV-H1 preserved
(manager-only); default and `reset_on_logon=true` paths byte-identical. Decisions
**explicitly flagged for Gate A** below.

### Flagged for Gate A

1. **Manager-only restore vs re-persist.** The fix restores only the in-memory manager
   (`set_next_inbound`), leaving the durable store at `seqnum_min`. This holds INV-H1
   (store ≤ manager) and matches 029's lower-bound discipline (under-persist is safe; the
   013 ResendRequest on the next Logon reconciles any residual gap). Alternative: durably
   persist 2 as well. **Rejected** because (a) it would make the store briefly *equal* the
   manager which is fine, but the post-reset durable record is conceptually "reset base = 1";
   persisting 2 mixes the reset semantics with the consume; (b) the 029 precedent
   ([[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]) is that
   over-persist at a multi-exit Logon gate is the dangerous direction. Confirm Gate A agrees
   manager-only is correct (and that a witness asserts `store.durable == seqnum_min` directly,
   not just the manager value — the 029 W9b proxy-gap lesson).

2. **Role scope: acceptor-observable, shared code with initiator.** The received-141 arm is
   shared by both roles, but the live finding + primary scenarios are acceptor-side. Does the
   fix's correctness claim (and a witness) need to cover the **initiator** received-141 path
   too, or is it scoped to acceptor with the initiator path noted as
   structurally-covered-but-unwitnessed? (cf. the 025 L-025-2 symmetric-API
   reachability lesson — claim only what a witness proves;
   [[feedback_symmetric_api_claim_unreachable_arm]].)

3. **Blast-radius pin re-verification.** 5 existing tests pin the defective value. Gate A to
   confirm the plan's commitment to **individually re-read each** and certify it pins *this*
   off-by-one (a justified correction), not a distinct intended behavior — and that the
   `next_expected` rename `AcceptorReplyReceived141_Advertises1 → _Advertises2` is a content
   correction, not a semantic redefinition.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md`: amend the 013/024 received-141 reset rows (S-005/S-006/S-014/S-024) Notes + the 027 row **S-031** to cite `030-received-reset-inbound-advance` as the conformance correction (received-141 nets next-expected-inbound=2 + reply 789=2). No new S-row.
- `spec/coverage-index.md`: add a coverage note mapping the new guarded restore branch ↔ `tests/session/test_reset_on_lifecycle.cpp` (triple + harm-repro + guard) under the existing 024/027 entries.
- `spec/behaviors-and-limitations.md`: add **B-030-1** (received-141 reset advances next-expected-inbound to 2 — reference-engine-conformant; reply MsgSeqNum stays 1, reply 789=2 when 027 on). If the initiator path is left unwitnessed (Gate A decision 2), add **L-030-1** scoping the claim to the acceptor role.

## Project Structure

### Documentation (this feature)

```text
specs/030-received-reset-inbound-advance/
├── plan.md              # this file
├── spec.md              # /speckit-specify + /speckit-clarify output
├── research.md          # Phase 0 (reference-engine grounding + design decisions)
├── quickstart.md        # Phase 1 (how to reproduce harm + verify the fix)
├── checklists/
│   └── requirements.md  # spec quality checklist
└── tasks.md             # /speckit-tasks output (NOT created here)
```

No `data-model.md` or `contracts/` — this feature introduces **no new entities and no new
external interface** (it corrects an internal counter on an existing path). Per the plan
template, those artifacts are skipped as not-applicable.

### Source Code (repository root = library submodule)

```text
src/session/session.cpp            # received-141 arm: + guarded set_next_inbound(seqnum_min+1)
tests/session/test_reset_on_lifecycle.cpp   # + triple, + harm-repro, + guard, + INV-H1 witnesses
tests/session/test_reset_seqnum_policy_matrix.cpp   # pin flips ×3 (next_inbound 1→2)
tests/session/test_persistent_seqnum_hydrate.cpp    # pin flip ×1 (InboundSeedWithheld)
tests/session/test_next_expected.cpp                # pin flip ×1 (Advertises1→2 + rename)
```

**Structure Decision**: surgical edit to the existing received-141 arm in `session.cpp`;
all test work lands in the existing session test files (no new test target). The exact pin
file names/paths are confirmed in `/speckit-tasks` via grep, not assumed here.

## Complexity Tracking

No constitution violations to justify. The only "complexity" is the cross-feature pin flips,
which are tracked as a first-class deliverable (SC-003) with a per-pin re-verification
requirement, not hidden as incidental edits.
