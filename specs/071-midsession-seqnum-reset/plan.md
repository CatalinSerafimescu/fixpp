# Implementation Plan: Mid-Session Sequence-Number Reset Originator (071)

**Branch**: `071-midsession-seqnum-reset` | **Date**: 2026-07-12 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/071-midsession-seqnum-reset/spec.md`

## Summary

Add a single public C++ initiator capability, `Session::reset_sequence_numbers()`, that resets both FIX sequence counters to 1 mid-session **without dropping the connection** by originating a `Logon(141=Y)` on the live transport (FIX-SL §4.4.2). The trigger durably resets, emits the reset Logon via a dedicated clock-guarded emit path that mirrors the connect-time emitter core (`:818-865`) but omits its one-shot hydrate preamble, transitions Active→LogonSent, and completes on the peer's confirming `Logon(141=Y)` by **reusing the existing `peer_ack_sent_reset_flag` ack arm unchanged** (which restores both counters to 2). Two supporting changes fall out of the FSM re-entry: the transition helper suppresses the spurious onLogout callback+latch for the reset edge, and a liveness-loop generation guard prevents a double heartbeat loop. Opt-in and inert by default (FR-006); no C-ABI/Python change (FR-008). Closes catalogue row **S-032** (residual).

## Technical Context

**Language/Version**: C++23 (Article II §1).

**Primary Dependencies**: ASIO awaitables (Article XI §1); `fixpp::sync::async_mutex` where a mutex is needed in awaitable context (Article XI §3). No new external dependency.

**Storage**: reuses the existing `MessageStore` reset/persist path (`reset_seqnums_to_one_durable`, `persist_{inbound,outbound}_advance_`); no schema change.

**Testing**: GoogleTest (Article VII §1); new witnesses in `tests/session/` grouped per Article VII §8; mutation-proven RED for each guard (SC-005).

**Target Platform**: Linux/Clang primary (Tier 1); MSVC Tier 2. No platform-specific code.

**Project Type**: in-process C++23 library (Article I §2).

**Performance Goals**: emit path is zero-heap (Article VIII §5 / XV §1; extends `ResetKnobs_NoHeapOnResetPath`). The liveness-generation guard adds one integer compare per loop iteration — no measurable perf impact; no bench baseline change expected.

**Constraints**: byte/disposition-identical when the trigger is never invoked (FR-006); no C-ABI/Python surface change (FR-008); reuse the shipped ack arm and durable-reset helper rather than fork them (FR-004).

**Scale/Scope**: single new public method + one dedicated private emit helper + one FSM-helper guard branch + one liveness generation guard + one new `core::error` value. Initiator-only, against a conforming acceptor.

## Constitution Check

*GATE: must pass before Phase 0; re-checked after Phase 1.*

**Appendix A mandatory triggers** — this feature hits **Session FSM** (new Active→LogonSent edge, recovery-adjacent), **public C++ API** (new method), **Threading/concurrency** (liveness-loop generation guard), and **Error semantics** (new `core::error` value). Therefore all four controls are mandatory:
- `/clarify` — **DONE** (spec `## Clarifications`, 2026-07-12; 3 items resolved, reference-survey-driven re-scope to manual API only).
- `/analyze` — **REQUIRED** before `/tasks` (Article XVI §4). Planned as pipeline step 6.
- Codex Gate A — **REQUIRED** before `/tasks` (Article XVII §1). Planned next.
- User `/plan` sign-off — **REQUIRED**.

| Article | Gate | Status / plan |
|---|---|---|
| I §3, VI | Catalogue: closes OFFICIAL row S-032; needs coverage-index + feature-catalogue update; Normative References. | Spec cites [FIX-SL §4.4.2]. Close-out task flips S-032 backlog→done. **PASS (planned)** |
| II | C++23, Clang/GCC/MSVC. | No new-standard-feature risk. **PASS** |
| VII §3-4 | TDD, no code without test; discriminating witnesses. | Every FR/INV has a witness (quickstart §1-6); mutation-proven RED (SC-005). **PASS (planned)** |
| VII §8 | Test grouping by label. | New session witnesses join the session bucket; no-heap/own-main stay standalone. **PASS (planned)** |
| VIII §5, XV §1 | Zero heap on the emit path. | INV-071-5 extends `ResetKnobs_NoHeapOnResetPath`. **PASS (planned)** |
| IX §1-4 | ≥95/85 touched lines; ASan/UBSan/TSan; static analysis. | `/speckit-verify` gate; TSan on the liveness/loop witness. **PASS (planned)** |
| X, IV §2-3 | No C-ABI / Python change. | New value is `core::error`, not `fixpp_error_t`; no `c_api.h` edit. **PASS** |
| XI §3-4 | async_mutex in awaitable ctx; per-session strand. | Trigger runs on the strand; generation guard is a plain member read on the strand (no cross-thread). **PASS (planned)** |
| XV §15 | No silent message loss / seq desync. | The reset abandons unacked history *by contract* (R7) — a defined mutual reset, not a silent drop; guarded against mid-gap-fill (R4). **PASS (planned)** |
| XVI §3-4 | /clarify + /analyze mandatory. | /clarify done; /analyze planned. **PASS (planned)** |
| XVII | Gate A + Gate B + /speckit-verify. | All planned. **PASS (planned)** |

**No violations requiring Complexity Tracking.** The one non-obvious addition — the liveness-loop generation guard — is not gold-plating: it repairs a concrete double-spawn defect the FSM re-entry introduces (R6), and is the minimal correct fix (a bool cannot signal a lazily-waking old loop to exit while state is Active again).

## Project Structure

### Documentation (this feature)

```text
specs/071-midsession-seqnum-reset/
├── plan.md              # This file
├── research.md          # Phase 0 (R0–R9 + consolidated design)
├── data-model.md        # Phase 1 (members, error, FSM edge, invariants)
├── contracts/
│   └── session-api.md   # Phase 1 (reset_sequence_numbers contract)
├── quickstart.md        # Phase 1 (validation scenarios 1–6)
├── checklists/
│   └── requirements.md  # spec quality checklist (16/16)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT yet created)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/session/
├── session.hpp            # + reset_sequence_numbers() decl; + midsession_reset_in_progress_, liveness_gen_ members; + emit_midsession_reset_logon_ decl
include/fixpp/core/
├── error.hpp              # + session_invalid_state_for_reset enum value + to_string case
src/session/
├── session.cpp            # + reset_sequence_numbers() def (guard role+Active+!resend; transition BEFORE first co_await); + emit_midsession_reset_logon_(); record_state_transition_ two-edge callback policy (suppress onLogout on Active→LogonSent, force onLogout on LogonSent→terminal, flag-clear before app==null return); ack-arm minimal additive branch (latched-fact/peek restore-or-fail-closed); LogonSent inbound handler marker-gated tolerance of peer HB/TR (:3731-3778, FR-017); liveness generation guard at spawn sites (:2694/:4148) + loop body (:4769)
tests/session/
├── test_midsession_reset.cpp   # new — US1 happy path, invalid-state refusal, callback symmetry, single-liveness-loop, emit-site mutation proofs
└── (existing reset suites unchanged — regression baseline)
spec/
├── feature-catalogue.md   # S-032 backlog→done (close-out)
├── coverage-index.md      # §4.4.2 row update
└── behaviors-and-limitations.md  # + L-071-3 (no logon-response timeout), L-071-2 (in-flight app abandoned); L-071-1 retired (dup-seq fixed, not a limitation)
```

**Structure Decision**: single-project library layout (Option 1). All changes are in the existing `session/` and `core/` modules; no new module, no new pluggable interface, no build-graph change beyond one new test `.cpp` joining the session bucket.

## Phase sequencing (per `.specify/pipeline.md`)

1. **Gate A** (Codex + Opus adversarial) on this bundle — **next**, before `/tasks`.
2. `/analyze` cross-artifact consistency.
3. `/speckit-tasks` (must emit completeness-audit + catalogue close-out tasks).
4. Checklist audit (step 9).
5. `/speckit-implement` (TDD; ≤ the anti-runaway caps).
6. `/simplify` → `/speckit-verify` (GREEN/YELLOW record).
7. Gate B → merge → close-out (flip S-032, bump submodule pointer, update MEMORY).

## Gate A

- **Round 1 applied 2026-07-12**: Codex P1=3 P2=1 P3=1; Opus post-judging P1=4 P2=2 P3=1 → **not converged; structural**. User chose to re-run `/clarify`+`/plan` (not an in-loop rewrite). Re-decisions applied across spec/research/data-model/contracts:
  - **FR-009**: no logon-response timeout exists (verified 3 ways) — rewrote to document the real behavior (session stays LogonSent until transport EOF / app close; same as connect-time) + limitation **L-071-3**. (Removed a fabricated "reuse the connect-time timeout" claim.)
  - **FR-015** (new): two-edge callback model — success reset fires neither onLogon/onLogout; failed reset (LogonSent→terminal) **forces** onLogout once (fixes the Gate A New-P1 silent-death; the coarse flag would have muted onLogout because `was_active` is false on that edge). Flag cleared on every edge, before the `application==nullptr` early return.
  - **FR-014** (new): transition Active→LogonSent **before the first `co_await`** to close the in-flight-send / liveness interleave window (Gate A P1-1).
  - **FR-004** (relaxed): ack-arm outbound restore keys off a latched fact (mid-session marker + `peek==2` check → restore-or-fail-closed), not the brittle `peek_outbound` inference — fixes the reachable dup-seq bug (Gate A P1-2) that was wrongly filed as limitation L-071-1 (now retired). "Reuse strictly unchanged" was unsatisfiable; the arm is now minimally, additively extended.
  - **FR-007**: guard gains the `role==initiator` conjunct (Gate A P2 — an Active acceptor would otherwise pass).
  - **SC-002**: fixture restricted to a conforming peer only (Gate A New-P2 — the fixpp acceptor mid-session-accept path is out of scope).
  - Root causes addressed: (1) atomic transition boundary + callback model now specified (FR-014/FR-015); (2) reuse-first framing reconciled with the code (FR-004 relaxed, FR-009 corrected).
  - Reviews: `research/reviews/codex_071-midsession-seqnum-reset_gate_a_review.md`, `research/reviews/opus_071-midsession-seqnum-reset_gate_a_adversarial_review.md`.
  - **FR-017** (added pre-round-2, advisor-surfaced): the inbound twin of FR-014 — the LogonSent reset window must tolerate in-flight peer liveness frames (Heartbeat/TestRequest) instead of the shipped `:3776` disconnect-on-non-Logon; otherwise a routine peer heartbeat drops the connection mid-reset and falsifies SC-001. Marker-gated (connect-time unchanged). L-071-2 re-characterized accordingly.
- **Round 2**: pending (fresh Codex + Opus pass on the revised bundle).

## Complexity Tracking

*No constitution violations to justify.* The feature is additive and reuse-first: it forks no reset logic (reuses `reset_seqnums_to_one_durable` + the `peer_ack_sent_reset_flag` arm), adds no wire/ABI surface, and the two "extra" changes (onLogout suppression, liveness generation guard) are defect-avoidance forced by the new FSM re-entry, each the minimal correct form.
