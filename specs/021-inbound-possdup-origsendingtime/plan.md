# Implementation Plan: Inbound PossDup / OrigSendingTime Handling (S-010, first G3 slice)

**Branch**: `021-inbound-possdup-origsendingtime` | **Date**: 2026-06-04 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/021-inbound-possdup-origsendingtime/spec.md`

## Summary

Add the **inbound receive side** of possible-duplicate handling to the FIX 4.4 session layer so fixpp interops cleanly when a live counterparty (QuickFIX-cpp / QuickFIX-J) replays admin or already-seen messages during recovery. Today a too-low inbound message (`MsgSeqNum < expected`) is **unconditionally fatal** (`session_seqnum_too_low=69` → `Disconnected`, `session.cpp:1849-1863`); this feature makes that arm **PossDup-aware**: a too-low message bearing `PossDupFlag(43)=Y` is treated as a possible duplicate and the session survives.

**Key design grounding (CodeGraph sweep of the two live interop targets, in [research.md](./research.md)):** QuickFIX-cpp v1.16.0 (`Session::doPossDup`/`doTargetTooLow`) and QuickFIX-J 3.0.1 (`Session.validatePossDup`/`doTargetTooLow`) **agree on arms B/C/D/E's reason codes and terminate/survive disposition** and on the no-seqnum-advance rule; on Arm D's `RefTagID` they differ (QFJ sets `371=122` via `OrigSendingTime.FIELD`, QFcpp omits the field) and **fixpp deliberately chooses QFJ's more specific `371=122`** (see the per-arm table below). They **diverge on three** axes: (1) whether a validated application duplicate is redelivered to the app callback (QFJ drops, QFcpp redelivers — resolved configurable, default drop); (2) **whether PossDup is validated at the expected sequence number** (QFJ validates at-expected `Session.java:1843`, QFcpp validates only via the too-low path — fixpp chooses the QFJ superset: validate ALL `43=Y` non-reset inbound); (3) the send-path `AllowPossDup` default (**DEFERRED** out of this slice — opaque-send hardening required, see FR-008 / research D7).

The five disposition arms — Arms C/D/E are **validation** (Stage 1, run for ALL `43=Y` non-`35=4` inbound, any seqnum); Arms A/B are the **too-low tolerance** (Stage 2, run only when `34 < expected`):

| Arm | Condition | Disposition | Engines |
|-----|-----------|-------------|---------|
| **A** | too-low + `43=Y` + valid `122` | tolerate; **no seqnum advance**; admin→ignore, app→drop(default)/redeliver(opt-in); **no disconnect** | agree (modulo divergence (1)) |
| **B** | too-low + no `43=Y` | **`→Disconnected`, NO Logout wire frame** (current `session.cpp:1860-1862`, `record_state_transition_` only) — **preserved byte-identical** | fixpp current behavior (engines `generateLogout`; not adopted here) |
| **C** | `43=Y` + `122` **missing** (any seqnum) | `Reject(35=3)` `371=122`, `373=1` (**`RequiredTagMissing`**); **survive** | agree on reason; at-expected firing follows QFJ |
| **D** | `43=Y` + `122` **>** `52` (strict, any seqnum) | `Reject(35=3)` `371=122`, `373=10` (**`SendingTimeAccuracyProblem`**) + `Logout` + disconnect | agree on reason; at-expected firing follows QFJ |
| **E** | `SequenceReset(35=4)` + `43=Y` | **exempt** from the `122` requirement; existing gap-fill/reset path handles it | agree |

**This is a real production-behavior change** to the proven 005/013/S-023 inbound dispatch path — so it carries a genuine Gate B (surfaced in Complexity Tracking). The send-path half (FR-008) is **DEFERRED** out of this slice (opaque `send_impl` requires a new boundary-anchored parser — see Complexity Tracking + research D7); this slice is **inbound-only**. It is **bounded**: it reuses existing machinery throughout — `build_reject` (`admin_messages.cpp:540`, emits `45=RefSeqNum`/`371=RefTagID`/`372=RefMsgType`/`373=SessionRejectReason`; reasons 1 and 10 both already in use, the §1699 accuracy path uses the same `Reject→Logout→Disconnect` pattern for Arm D), the existing `→Disconnected` emit, `seqnum_mgr_`, the 019 `Application::fromApp` redeliver path, and the `SessionConfig` knob pattern (cf. `reconnect_policy`). No new infrastructure, **no new persistent dedup store**, no codegen change, no new `error::core` enum slot.

## Technical Context

**Language/Version**: C++23 (Clang; coroutines, `std::expected`) — [const §II]
**Primary Dependencies**: existing `session::Session` inbound dispatch + `scan_frame_header`, `seqnum_mgr_`, `build_reject`/admin builders, 019 `Application` (`fromApp`/`fromAdmin`), `SessionConfig`, `core::{error,expected_t}` — no new third-party deps
**Storage**: N/A — "already applied" is derived from the existing expected-inbound-seqnum state (store-replay model, no reorder queue, no new dedup store)
**Testing**: GoogleTest + GoogleMock; sanitizers ASan/UBSan/TSan; coverage llvm-cov; live interop ctest cells (skip-without-counterparty), extending the 018 admin-interop fixture — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1); Windows/MSVC (Tier 2)
**Project Type**: single C++ library (`fixpp`) + tests-only interop-harness extension (parent `phase-9-harness/`)
**Performance Goals**: inbound dispatch path stays allocation-disciplined — the PossDup disposition writes any `Reject`/`Logout` into a stack buffer via the existing admin builders (no heap; `counting_resource` witness on the new arm), consistent with the I-7 "no heap on inbound-dispatch path" rule
**Constraints**: new branch is `noexcept`/`expected_t` house style; no `std::mutex` in awaitable headers ([const §XV.9] — the disposition lives in `session.cpp`, not an awaitable header, and adds no new include into `session.hpp`'s awaitable closure); `43`/`52`/`122` compared as raw `string_view`/parsed-time, no locale; reuses `stamp_sending_time`/`parse` time machinery
**Scale/Scope**: 1 inbound disposition (two-stage: Stage-1 validation Arms C/D/E run for all `43=Y`; Stage-2 too-low tolerance Arms A/B) + 1 `scan_frame_header` field (tag 122 capture) + **1** `SessionConfig` knob (`redeliver_poss_dup` inbound app default-drop) + live interop cells (QFJ + QFcpp, both roles) + unit FSM/seqnum tests. **FR-008 send-path strip is DEFERRED** (opaque-send hardening — its own future slice). No FIXT/5.0SP2 (G4), no other G3 knobs, no `PossResend(97)`.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed — design adds no new violation).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **S-033** (OrigSendingTime(122) required on PossDup=Y) → `done` (this slice owns the inbound enforcement); **S-010** stays `backlog` with a 021 partial-delivery note (PossDup(43) inbound duplicate semantics delivered; PossResend(97) still deferred). `NextExpectedMsgSeqNum(789)`, FIXT/5.0SP2, other G3 knobs, and the **AllowPossDup send knob** (FR-008, DEFERRED) stay `backlog`. Exact catalogue/coverage-index delta below (written before `/speckit-tasks`; the catalogue rows are **edited at Polish**, not at plan time — per the 020 precedent). | ⚠ RESOLVED (S-033 → done; S-010 partial-backlog; delta specified, applied at Polish before `/speckit-tasks`) |
| **VII** Testing/TDD | every arm (A admin-ignore / A app-drop / A app-redeliver / B fatal-preserved / C required-tag / D accuracy+logout / E reset-exempt) lands RED-first as a unit test; live cells red-first; GoogleTest | ✅ planned |
| **VII.6** Interop | extends the live QFJ/QFcpp both-role matrix with PossDup replay cells (SC-004) | ✅ planned |
| **VIII.5** Allocator | disposition emits via existing stack-buffer admin builders (no heap on inbound path); `counting_resource` witness on the new arm | ✅ by design |
| **IX.1** Coverage | ≥95/85 on the touched `session.cpp` dispatch region + new branch; arms C/D are genuine error paths ⇒ tested; arm B regression-pinned | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the inbound-path change + interop ctest (018 discipline) | ✅ planned |
| **X** ABI | no C-ABI surface; the one `SessionConfig` knob (`redeliver_poss_dup`) is a C++ struct field in the **public** header `include/fixpp/session/session_config.hpp` (additive, default-valued — public-header additive field, no break); no new `error` enum slot (no `fixpp_error_t` change) | ✅ N/A / additive |
| **XI.4** Threading | the **app-redeliver** arm calls `Application::fromApp` from the inbound-dispatch path — the same engine-`exec_` single-thread-confinement context 019/020 already use for inbound app delivery (L-019-3); **no new concurrency surface**, no off-strand call. Redeliver reuses the exact 020 inbound `fromApp` invocation site. | ✅ PASS (reuses 019/020 inbound delivery context) |
| **XIV.2** Pluggable ≤5 pure-virtual | no new pluggable interface; knobs are plain `SessionConfig` fields, disposition is internal | ✅ N/A |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | disposition + header-field capture live in `session.cpp`; the only header touch is one additive `SessionConfig` POD field in `include/fixpp/session/session_config.hpp` (not an awaitable closure header) — verify no new include drags a mutex into `session.hpp` (Tier-1 unfiltered / `-L sync` per the §XV.9 watch-item) | ✅ PASS (watch-item flagged for verify) |
| **XV.15** No app-message drop | **Arm A app-drop is NOT a backpressure drop** — it is a *protocol* duplicate-discard (the message was already processed once; `MsgSeqNum < expected` proves it). No session/app message is silently lost from the sequence contract; the seqnum contract is exactly preserved (no advance, no gap). Two arms are documented in the B-/L- entries to distinguish from §XV.15: the **app** drop (honors `redeliver_poss_dup`) AND the **admin** always-drop (unconditional even when the knob is on) — the asymmetry is operator-visible. | ✅ PASS (protocol-dup, not queue-drop) |
| **XVI.3** /clarify before /plan | session-FSM trigger → `/speckit-clarify` Session 2026-06-04 (2 axes: app-dup disposition, AllowPossDup default) ✅; Gate A round 1 added the validate-all-`43=Y` choice + FR-008 de-scope (spec Clarifications) | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⚠ Gate A PENDING |

**Result**: PASS to proceed. One production-behavior change (the inbound PossDup validation + too-low tolerance branch) is surfaced in Complexity Tracking for Gate-A scrutiny; it reuses proven machinery and adds no new infrastructure. The plain-send strip knob (FR-008) is DEFERRED out of this slice (opaque-send hardening). No unjustified violations.

**Exact §VI delta (written before `/speckit-tasks`):**
- `spec/feature-catalogue.md`:
  - **S-033** (`OrigSendingTime(122)` — required on all PossDupFlag=Y retransmitted messages, `:345`) `backlog → done`, cite 021 (this slice owns the inbound OrigSendingTime-required enforcement — Arms C/D).
  - **S-010** (`PossDupFlag(43) + PossResend(97)` — duplicate detection semantics, `:30`) **stays `backlog`** with a 021 partial-delivery note: *"PossDupFlag(43) inbound session duplicate semantics (tolerate too-low replay, no seqnum advance, Arms A/B) delivered by 021; PossResend(97) application-resend semantics still deferred to a later G3 slice."* (Mirrors how 020 recorded A-001/A-006 as partial-evidence `backlog`.) Do **not** flip S-010 to `done` — PossResend(97) is unimplemented.
  - Note **S-034** (RefTagID(371)/RefMsgType(372) in Reject) is *exercised* by this feature's Arm-C/D rejects (which carry `371=122`); leave its disposition unchanged (the reject merely *carries* 371 — not a standalone close), but cross-reference 021 in a note.
  - The **AllowPossDup send-path knob** row stays `backlog` (FR-008 DEFERRED — not delivered this slice).
  - Append the two new B-/L- entries (below).
- `spec/coverage-index.md`: against the session-recovery rows, mark **S-033** `done` with a 021 reference; keep **S-010** in the deferred set with the PossResend(97)-partial note; record `NextExpectedMsgSeqNum(789)`, remaining G3 config knobs, and the deferred AllowPossDup send knob as the still-deferred set.
- `spec/behaviors-and-limitations.md`: add **B-021-1** (inbound PossDup tolerance: too-low+`43=Y` survives, no seqnum advance; Arms C/D reject reasons `373=1`/`373=10` with `371=122`; validation is seqnum-independent), **L-021-1** (app duplicate default-drop vs opt-in redeliver knob `redeliver_poss_dup` — **and** the *admin always-drop* arm which is unconditional even when the knob is on: document BOTH halves of the asymmetry — distinguish from §XV.15 backpressure drop), **L-021-2** (`AllowPossDup` send-path knob / FR-008 **DEFERRED** — opaque `send_impl` requires a boundary-anchored `43`/`122` excision parser before it can ship; intended default = strip, auto-resend always re-adds).

## Project Structure

### Documentation (this feature)

```text
specs/021-inbound-possdup-origsendingtime/
├── plan.md              # this file
├── research.md          # Phase 0 — engine-grounded disposition decisions (D1..D6)
├── data-model.md        # Phase 1 — disposition decision table + SessionConfig knobs
├── contracts/
│   └── session-possdup.md   # the inbound disposition contract + SessionConfig knob surface
├── quickstart.md        # Phase 1 — how to exercise (unit + live interop cells)
├── checklists/
│   └── requirements.md  # spec-quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 — /speckit-tasks (NOT created here)
```

### Source Code (repository root = library submodule)

```text
src/session/
├── session.cpp          # PRIMARY (behavior site): Stage-1 PossDup validation (Arms C/D/E)
│                        #          for all 43=Y non-35=4 inbound, BEFORE the seqnum disposition;
│                        #          inbound too-low arm (1849-1863) → PossDup-aware (Arm A); Arm B unchanged;
│                        #          scan_frame_header + FrameHeader → capture tag 122
include/fixpp/session/
└── session_config.hpp   # PUBLIC header: +1 additive POD knob: redeliver_poss_dup
                         #   (allow_poss_dup / FR-008 DEFERRED — not added this slice)
src/session/
└── admin_messages.{hpp,cpp}  # REUSE build_reject (371=RefTagID/373=SessionRejectReason; reasons 1 + 10 already used) — no change expected

tests/session/
├── test_inbound_poss_dup.cpp        # NEW: arms A(admin/app×knob)/B/C/D/E unit witnesses (RED-first)
└── (existing seqnum/reject tests)   # arm-B regression pin (no-PossDup too-low still fatal)

tests/interop/                        # extend 018 admin fixture with PossDup replay cells
phase-9-harness/                      # parent: live PossDup-replay cells (QFJ/QFcpp, both roles)
```

**Structure Decision**: Single-library change centred on `src/session/session.cpp` (the inbound dispatch coroutine) plus **one** additive public-header `SessionConfig` field (`redeliver_poss_dup`). No new modules, files-of-record, or public C surface. The interop witnesses extend the existing 018/020 fixture and the parent harness rather than introducing a new test tier.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| Inbound dispatch becomes PossDup-aware: Stage-1 validation (Arms C/D for all `43=Y` non-`35=4`) + Stage-2 too-low Arm A (`session.cpp:1849-1863`) | The entire feature; without it any counterparty replay kills the session (S-010 PossDup half / S-033) | Modifies the **proven 005/013/S-023 production inbound FSM**; Arm B (fatal too-low, `→Disconnected` **no Logout frame**) must be preserved byte-identically while Arm A is inserted ahead of it and validation is inserted before the seqnum gate — a mis-ordering would either re-break recovery or fail-open. RED-pin Arm B (assert *no* Logout frame) + RED-prove Arms A/C/D/E (incl. the at-expected C/D firing). |
| AllowPossDup send-path strip — **DEFERRED** (FR-008 / research D7) | — | **Out of scope this slice.** The plain-`send` path `send_impl` is opaque-payload (copies the business body verbatim; `43`/`122` are not field-parsed), so stripping caller-supplied `43`/`122` requires a **new boundary-anchored excision parser** with a delimiter-injection hostile witness ([[feedback_delimiter_injection_verbatim_field_copy]] — same hazard as 020 RC#1), not a toggle of an existing seam. Split into its own future slice; no send-path change here. |

No 4th-project / repository-pattern / speculative-abstraction violations. The first row is a *behavior* change on a hot production path, justified by the feature itself and bounded by reusing existing emit/builder machinery; the second is explicitly deferred (no production change this slice).

## Gate A

- Round 1 applied 2026-06-04: Codex P1=3 P2=5 P3=1; Opus post-judging P1=5 P2=4 P3=4; rewrite addresses RC#1..RC#5 + New-1/New-2 (FR-008 de-scoped). Reviews: research/reviews/codex_021-inbound-possdup-origsendingtime_gate_a_review.md, research/reviews/opus_021-inbound-possdup-origsendingtime_gate_a_adversarial_review.md.
- Round 2 polish applied 2026-06-04: 4 P3 doc-consistency fixes (spec Edge-Case Logout straggler, plan Arm-D "identical" overstatement, §VI to-apply timing, US3 relocated to deferred). Opus post-judging P1=0 P2=0 (converged). Reviews: codex_..._gate_a_2_review.md, opus_..._gate_a_2_adversarial_review.md.
