# Implementation Plan: Inbound PossDup / OrigSendingTime Handling (S-010, first G3 slice)

**Branch**: `021-inbound-possdup-origsendingtime` | **Date**: 2026-06-04 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/021-inbound-possdup-origsendingtime/spec.md`

## Summary

Add the **inbound receive side** of possible-duplicate handling to the FIX 4.4 session layer so fixpp interops cleanly when a live counterparty (QuickFIX-cpp / QuickFIX-J) replays admin or already-seen messages during recovery. Today a too-low inbound message (`MsgSeqNum < expected`) is **unconditionally fatal** (`session_seqnum_too_low=69` → `Disconnected`, `session.cpp:1849-1863`); this feature makes that arm **PossDup-aware**: a too-low message bearing `PossDupFlag(43)=Y` is treated as a possible duplicate and the session survives.

**Key design grounding (CodeGraph sweep of the two live interop targets, in [research.md](./research.md)):** QuickFIX-cpp v1.16.0 (`Session::doPossDup`/`doTargetTooLow`) and QuickFIX-J 3.0.1 (`Session.validatePossDup`/`doTargetTooLow`) are **identical** on four of the five arms (B/C/D/E below) and on the no-seqnum-advance rule; they **diverge only** on (1) whether a validated application duplicate is redelivered to the app callback, and (2) the `AllowPossDup` send-path default. Both divergences are resolved in the spec's Clarifications (configurable, default drop; and QFJ-style strip-by-default).

The five disposition arms (all on the inbound too-low / PossDup path):

| Arm | Condition | Disposition | Both engines |
|-----|-----------|-------------|--------------|
| **A** | too-low + `43=Y` + valid `122` | tolerate; **no seqnum advance**; admin→ignore, app→drop(default)/redeliver(opt-in); **no disconnect** | agree (modulo divergence (1)) |
| **B** | too-low + no `43=Y` | `Logout("MsgSeqNum too low, expecting X but received Y")` + disconnect — **preserved unchanged** | agree |
| **C** | `43=Y` + `122` **missing** | `Reject(35=3)` reason **`RequiredTagMissing(1)`**, `RefTagID=122`; **survive** | agree |
| **D** | `43=Y` + `122` **>** `52` (strict) | `Reject(35=3)` reason **`SendingTimeAccuracyProblem(10)`** + `Logout` + disconnect | agree |
| **E** | `SequenceReset(35=4)` + `43=Y` | **exempt** from the `122` requirement; existing gap-fill/reset path handles it | agree |

**This is a real production-behavior change** to the proven 005/013/S-023 inbound dispatch path **and** (for FR-008) the 019/020 plain-send path — so it carries a genuine Gate B (surfaced in Complexity Tracking). It is **bounded**: it reuses existing machinery throughout — `build_reject` (`admin_messages.cpp:540`, already takes `RefSeqNum/RefTagID/RefMsgType/SessionRejectReason`; reasons 1 and 10 both already in use), the existing `Logout`+`Disconnected` emit pattern, `seqnum_mgr_`, the 019 `Application::fromApp` redeliver path, and the `SessionConfig` knob pattern (cf. `reconnect_policy`). No new infrastructure, **no new persistent dedup store**, no codegen change, no new `error::core` enum slot.

## Technical Context

**Language/Version**: C++23 (Clang; coroutines, `std::expected`) — [const §II]
**Primary Dependencies**: existing `session::Session` inbound dispatch + `scan_frame_header`, `seqnum_mgr_`, `build_reject`/admin builders, 019 `Application` (`fromApp`/`fromAdmin`), `SessionConfig`, `core::{error,expected_t}` — no new third-party deps
**Storage**: N/A — "already applied" is derived from the existing expected-inbound-seqnum state (store-replay model, no reorder queue, no new dedup store)
**Testing**: GoogleTest + GoogleMock; sanitizers ASan/UBSan/TSan; coverage llvm-cov; live interop ctest cells (skip-without-counterparty), extending the 018 admin-interop fixture — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1); Windows/MSVC (Tier 2)
**Project Type**: single C++ library (`fixpp`) + tests-only interop-harness extension (parent `phase-9-harness/`)
**Performance Goals**: inbound dispatch path stays allocation-disciplined — the PossDup disposition writes any `Reject`/`Logout` into a stack buffer via the existing admin builders (no heap; `counting_resource` witness on the new arm), consistent with the I-7 "no heap on inbound-dispatch path" rule
**Constraints**: new branch is `noexcept`/`expected_t` house style; no `std::mutex` in awaitable headers ([const §XV.9] — the disposition lives in `session.cpp`, not an awaitable header, and adds no new include into `session.hpp`'s awaitable closure); `43`/`52`/`122` compared as raw `string_view`/parsed-time, no locale; reuses `stamp_sending_time`/`parse` time machinery
**Scale/Scope**: 1 inbound disposition branch (5 arms, 4 reuse existing emits) + 1 `scan_frame_header` field (tag 122 capture) + 2 `SessionConfig` knobs (`allow_poss_dup` send default-strip; `redeliver_poss_dup` inbound app default-drop) + 1 plain-send strip point + live interop cells (QFJ + QFcpp, both roles) + unit FSM/seqnum tests. No FIXT/5.0SP2 (G4), no other G3 knobs.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed — design adds no new violation).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **S-010** (inbound PossDup/OrigSendingTime) + the **AllowPossDup send knob** (QFJ ×4) flip `backlog→done`; `NextExpectedMsgSeqNum(789)`, FIXT/5.0SP2, other G3 knobs stay deferred. Normative refs in research.md. Exact catalogue/coverage-index delta below (written before `/speckit-tasks`). | ⚠ RESOLVED (S-010 + AllowPossDup → done; delta specified) |
| **VII** Testing/TDD | every arm (A admin-ignore / A app-drop / A app-redeliver / B fatal-preserved / C required-tag / D accuracy+logout / E reset-exempt) lands RED-first as a unit test; live cells red-first; GoogleTest | ✅ planned |
| **VII.6** Interop | extends the live QFJ/QFcpp both-role matrix with PossDup replay cells (SC-004) | ✅ planned |
| **VIII.5** Allocator | disposition emits via existing stack-buffer admin builders (no heap on inbound path); `counting_resource` witness on the new arm | ✅ by design |
| **IX.1** Coverage | ≥95/85 on the touched `session.cpp` dispatch region + new branch; arms C/D are genuine error paths ⇒ tested; arm B regression-pinned | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the inbound-path change + interop ctest (018 discipline) | ✅ planned |
| **X** ABI | no C-ABI surface; `SessionConfig` knobs are C++ struct fields (additive, default-valued); no new `error` enum slot (no `fixpp_error_t` change) | ✅ N/A / additive |
| **XI.4** Threading | the **app-redeliver** arm calls `Application::fromApp` from the inbound-dispatch path — the same engine-`exec_` single-thread-confinement context 019/020 already use for inbound app delivery (L-019-3); **no new concurrency surface**, no off-strand call. Redeliver reuses the exact 020 inbound `fromApp` invocation site. | ✅ PASS (reuses 019/020 inbound delivery context) |
| **XIV.2** Pluggable ≤5 pure-virtual | no new pluggable interface; knobs are plain `SessionConfig` fields, disposition is internal | ✅ N/A |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | disposition + header-field capture live in `session.cpp`; the only header touch is two additive `SessionConfig` POD fields (`session_config.hpp` is not an awaitable closure header) — verify no new include drags a mutex into `session.hpp` (Tier-1 unfiltered / `-L sync` per the §XV.9 watch-item) | ✅ PASS (watch-item flagged for verify) |
| **XV.15** No app-message drop | **Arm A app-drop is NOT a backpressure drop** — it is a *protocol* duplicate-discard (the message was already processed once; `MsgSeqNum < expected` proves it). No session/app message is silently lost from the sequence contract; the seqnum contract is exactly preserved (no advance, no gap). Documented as a B-/L- catalogue entry to distinguish it from §XV.15. | ✅ PASS (protocol-dup, not queue-drop) |
| **XVI.3** /clarify before /plan | session-FSM trigger → `/speckit-clarify` Session 2026-06-04 (2 axes: app-dup disposition, AllowPossDup default) ✅ | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⚠ Gate A PENDING |

**Result**: PASS to proceed. Two production-behavior changes (the inbound too-low PossDup branch, and the plain-send strip knob) are surfaced in Complexity Tracking for Gate-A scrutiny; both reuse proven machinery and add no new infrastructure. No unjustified violations.

**Exact §VI delta (written before `/speckit-tasks`):**
- `spec/feature-catalogue.md`: **S-010** (inbound PossDup/OrigSendingTime tolerance + validation) `backlog → done`, cite 021 + the arm table; **AllowPossDup send-path knob** row (QFJ-sourced ×4) `backlog → done`, cite FR-008. Append the two new B-/L- entries (below).
- `spec/coverage-index.md`: against the session-recovery rows for S-010, replace the prior "explicitly out of scope (parity Bucket 3)" note with a `done` reference to 021; record `NextExpectedMsgSeqNum(789)` + remaining G3 config knobs as the still-deferred set.
- `spec/behaviors-and-limitations.md`: add **B-021-1** (inbound PossDup tolerance: too-low+`43=Y` survives, no seqnum advance; arms C/D reject reasons), **L-021-1** (app duplicate default-drop vs opt-in redeliver; admin always ignored — distinguish from §XV.15 backpressure drop), **L-021-2** (`AllowPossDup` default strips caller-supplied `43`/`122` on plain send; auto-resend always re-adds).

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
├── session.cpp          # PRIMARY: inbound too-low arm (1849-1863) → PossDup-aware branch;
│                        #          scan_frame_header + FrameHeader → capture tag 122;
│                        #          plain-send strip point for FR-008 (auto-resend ~2115 unchanged)
├── session_config.hpp   # +2 additive POD knobs: allow_poss_dup, redeliver_poss_dup
└── admin_messages.{hpp,cpp}  # REUSE build_reject (reasons 1 + 10 already used) — no change expected

tests/session/
├── test_inbound_poss_dup.cpp        # NEW: arms A(admin/app×knob)/B/C/D/E unit witnesses (RED-first)
└── (existing seqnum/reject tests)   # arm-B regression pin (no-PossDup too-low still fatal)

tests/interop/                        # extend 018 admin fixture with PossDup replay cells
phase-9-harness/                      # parent: live PossDup-replay cells (QFJ/QFcpp, both roles)
```

**Structure Decision**: Single-library change centred on `src/session/session.cpp` (the inbound dispatch coroutine) plus two additive `SessionConfig` fields. No new modules, files-of-record, or public C surface. The interop witnesses extend the existing 018/020 fixture and the parent harness rather than introducing a new test tier.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| Inbound too-low arm becomes PossDup-aware (`session.cpp:1849-1863`) | The entire feature; without it any counterparty replay kills the session (S-010) | Modifies the **proven 005/013/S-023 production inbound FSM**; arm B (fatal too-low) must be preserved byte-identically while arm A is inserted ahead of it — a mis-ordering would either re-break recovery or fail-open. RED-pin arm B + RED-prove arms A/C/D/E. |
| Plain-send strips caller-supplied `43`/`122` by default (FR-008) | Agreed scope (AllowPossDup knob); corrects the spec's earlier wrong "retain" default to match QFJ | Touches the **019/020 production send path**. The automatic resend/replay path (`~session.cpp:2115`) must remain unaffected (always re-adds `43`/`122`) — a half-restructure that also stripped the resend path would break recovery wire-conformance ([[feedback_half_restructure_symmetric_api]]). Pin both: plain-send strips, auto-resend retains. |

No 4th-project / repository-pattern / speculative-abstraction violations. The two rows above are *behavior* changes on hot production paths, justified by the feature itself and bounded by reusing existing emit/builder machinery.
