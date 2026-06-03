# Phase 0 Research: Live Session-Admin Interop Round-Trips (gap-fill G1)

**Feature**: `018-interop-live-admin` | **Date**: 2026-06-03
**Input**: [`plan.md`](./plan.md) Technical Context + Constitution-Check ⚠ items.

All decisions below are grounded in the **verified public surface** (`include/fixpp/session/session.hpp`, `engine.hpp`), the **existing 016 harness** (`tests/interop/**`), and the parent `phase-9-harness/INTEROP-016-ROADMAP.md`. No production behaviour is proposed.

---

## R1 — Assertion architecture: golden-based wire assertion vs in-process active driving (HEADLINE)

**Decision**: **Architecture A** — wire-frame round-trips are asserted by diffing **enriched golden transcripts** captured from the QuickFIX-J engine-log seam (`toAdmin`/`fromAdmin`); the in-process driver asserts only fixpp's **own** observable state (`state()`, `fsm_visit_history()`, outbound seqnum delta, bounded `stop_within`). Active manipulation (gap induction, malformed injection, driving QFJ to send admin) is parent-harness capability.

**Rationale**: Verified during planning — the public surface does **not** support the in-process alternative:
- `Session::send(std::span<const std::byte> app_payload)` sends an **application payload** only (auto-assigns `MsgSeqNum`/`SendingTime`, frames a well-formed message). There is **no public API to originate a specific admin message** (a `TestRequest` with a chosen `TestReqID`, a deliberately malformed admin frame). Admin frames are emitted internally by the FSM/liveness loop (`run_liveness_loop`: outbound-idle Heartbeat, inbound-silence TestRequest, recovery ResendRequest/GapFill).
- There is **no public hook to observe a received inbound admin frame's contents**. In-process observation is limited to `state()`, `fsm_visit_history()` (FSM-state membership), and `recent_events()` (`SessionEvent` ring — TLS outcome / credential rotation, not raw admin tags). `on_inbound_frame()` is the engine→session feed, not a test observation callback.
- Clarify decision-b (engine-log seam, no MITM) already fixed wire visibility as golden-based; Architecture A is the consistent, constitution-aligned choice (zero production surface, no new interface).

**Alternatives considered**:
- **Architecture B — in-process active driving via test seams**: add a public admin-originate API + inbound-frame observer (production change → Gate-A scope, contradicts the "no production behaviour" scope), or reach private state (`pending_test_req_id_`) via friend/test seams (banned-pattern risk, divergence from clarify-b). **Rejected.**
- **MITM-terminating proxy** for byte-exact in-flight admin capture: already rejected at 016 P4 decision-1 (more moving parts; byte-exact not required for v1.0). **Rejected.**

**Escape hatch (R-prod, see below)**: if a specific scenario proves un-expressible via A, it is a Gate-A-re-triggering finding, never a silent production change.

---

## R-prod — Production surface (zero, bounded)

**Decision**: **Zero `src/`/`include/` change expected.** The engine send path, liveness loop, recovery sub-protocol (005/013/S-023), and Reject path already shipped; G1 exercises them live. If a scenario cannot be expressed via the public surface + parent orchestration, that is surfaced as an explicit **finding that re-triggers Gate A** (`[const §XVII.1]`); G1 does **not** silently absorb a behaviour change (016 R-parity precedent).

**Rationale**: Distinguishes G1 from 016 (which scoped-in the reconnect-policy field). The clarify "no new production behaviour" scope + the verified-already-shipped admin/recovery paths make zero the honest default. Article IX.1 touched-module gate is therefore **N/A by construction** unless the escape hatch fires.

**Alternatives considered**: scope-in a production admin-originate API up front — rejected (speculative; YAGNI; would expand Gate-A surface for a tests-only feature).

---

## R2 — In-process witness set (what the driver asserts about fixpp)

**Decision**: Per scenario the driver asserts a **minimal, deterministic** fixpp-state witness:
- **US1 (TestRequest→HB)**: fixpp reaches `Active`; on inbound silence the liveness loop emits a TestRequest (outbound seqnum advances); fixpp answers a peer TestRequest with a Heartbeat (seqnum advances) — the `112` echo correlation is the parent golden's job.
- **US2 (cadence)**: fixpp reaches `Active`, stays `Active` across the ~5s idle window, no spurious FSM transition; the per-direction beat count is the parent golden's job.
- **US3.i (inbound gap)**: after the induced gap fixpp's `fsm_visit_history()` shows the recovery transition and returns to `Active`; the ResendRequest frame + correct `BeginSeqNo`/`EndSeqNo` is the parent golden's job.
- **US3.ii (outbound answer, FR-004a)**: when QFJ issues a ResendRequest, fixpp stays `Active` and the outbound replay/GapFill is the parent golden's job.
- **US4 (Reject)**: fixpp stays `Active` (no disconnect) after the malformed input; the `Reject(35=3)` + reference fields is the parent golden's job.

**Rationale**: Plays to the public observation surface (`state()`/`fsm_visit_history()`/`recent_events()`) plus **one pre-existing tests-only seam** — the outbound-seqnum delta is read via `seqnum_mgr_test_access().peek_outbound()` (the 016 `hp_fix44_testrequest_echo_test.cpp` already uses it), NOT public API and NOT a new seam introduced by G1. No new test-access seam is created; the design depends on no private-state friend hook beyond this established one. Keeps in-process assertions deterministic (no flake). Every live-I/O wait uses `InteropEngineFixture::run_until(pred, deadline)` with an internal self-deadline (FR-010, `[[feedback_fail_placeholder_red_test]]`).

**Alternatives considered**: assert echoed `112`/seqnum-range in-process — not possible without an inbound observer (R1). Deferred to golden.

---

## R3 — Gap induction + malformed-frame injection mechanism (parent harness)

**Decision**: All three are **parent-harness capabilities** in `phase-9-harness/`, not in-repo, with **one induction mechanism pinned per cell** (no mixing — New-3):
- **Inbound gap (`recovery_inbound`, US3.i)**: **pinned** to drop/withhold a single QFJ→fixpp frame at the passthrough layer so fixpp observes `MsgSeqNum > expected` → too-high → `ResendRequest`. The "drive QFJ to skip a sequence" alternative is NOT used in this cell: a QFJ send-seqnum skip without a stored message answers the subsequent resend with `SequenceReset-GapFill(123=Y)`, yielding a *different* golden frame set than the withhold-a-frame mechanism; the cell's capture-once golden would otherwise be non-deterministic.
- **Outbound answer (`recovery_outbound`, US3.ii)**: **pinned** to driving QFJ to issue a `ResendRequest` against fixpp = QFJ restart/reconnect expecting a lower inbound sequence (QFJ's standard resend-on-logon behaviour), achievable from the QFJ launcher without bespoke code. See R8 + the parent contract `recovery_outbound` table for the full choreography (store state, `ResetOnLogon=N`, expected `7/16`, per-role).
- **Malformed admin (US4, both directions)**: the parent **corrupts an admin frame in flight at the proxy layer** (a known-bad tag value the receiver rejects per `[FIX-SL §4.5.4]`); fixpp never originates malformed bytes (New-2). For the fixpp-rejects direction the parent corrupts a QFJ→fixpp frame; for the peer-rejects direction it corrupts a fixpp→QFJ frame.

**Rationale**: fixpp cannot force its own inbound to gap, originate a malformed frame, or compel QFJ to resend from the in-process side (R1). The parent passthrough proxy + QFJ launcher (already present per ROADMAP P1/P2) are the natural injection points.

**Alternatives considered**: a test-only transport shim in-repo that drops frames — rejected (re-implements the parent proxy; pulls orchestration into the submodule against the 016 boundary).

---

## R4 — Idle-cadence observation (US2)

**Decision**: Negotiate `HeartBtInt(108)=1`s; the **golden transcript** records the per-direction Heartbeat frames over the ~5s window; the gate asserts **≥3 beats in EACH direction (±1-beat tolerance)** and **no `TestRequest`** (proves true idle keep-alive, not silence-elicitation). In-process: fixpp stays `Active`, no spurious transition.

**Rationale**: matches clarify Q3; the ±1 tolerance + golden-based counting avoids timing flake. The "no TestRequest" assertion distinguishes Heartbeat cadence from the inbound-silence TestRequest path.

**Alternatives considered**: assert exact inter-beat timing in-process — rejected (clock-jitter flake; `[[feedback_stack_use_after_return_local_vs_ci_flake]]` class). Tolerance-bracketed count is robust.

---

## R5 — Both-role cell expansion

**Decision**: Reuse the existing **value-parameterized** driver pattern (`TestWithParam<std::tuple<Counterparty, Role>>`) so each admin cell runs `fixpp-initiator` (vs QFJ acceptor) **and** `fixpp-acceptor` (vs QFJ initiator). For US3.ii (fixpp answers a ResendRequest), the QFJ-initiator-restart mechanism (R3) drives both role orientations.

**Rationale**: matches clarify Q1 + the 016 MATRIX axis (`counterparty × role`); no new pattern. Counterparty fixed to QuickFIX-J 3.0.1 (QFcpp optional, Fix8 corpus-only — plan Assumptions).

**Alternatives considered**: initiator-only (rejected by clarify Q1).

---

## R6 — Golden enrichment + normalizer

**Decision**: Add enriched goldens `happy/golden/HP-*-admin-*.fix` capturing the new admin frames; reuse the 016 `diff_transcripts(...)` utility but pass an **explicit G1 admin normalization profile** that excludes **only `{52, 10}`** (SendingTime + CheckSum) via the function's existing `excluded_tags` parameter (`golden_diff.hpp:44-46`). The **default** 016 tag set is `{9, 10, 34, 52, 60, 112, 122}` (`golden_diff.hpp:36-40`) — it drops `112`/`34`/`122`, exactly the tags G1 asserts (echo correlation, seqnum, OrigSendingTime/replay evidence), and MUST NOT be used for G1. This needs **no library change** — the parameter already exists; G1 is a caller-supplied tag set. A deliberate-mutation negative test on a *compared* tag (`112`/`34`/`7`/`16`/`122`/`123`) must bite. Goldens are captured at **first paired run**, never hand-fabricated (016 T009 rule).

**Rationale**: the admin frames (`112` values, seqnum ranges, `122` replay evidence) are the very subject of G1's assertions; the default 016 profile would mask them. Supplying `{52,10}` explicitly canonicalizes only the genuinely-volatile fields while keeping `diff_transcripts`' semantics identical.

**Alternatives considered**: reuse the 016 *default* tag set unchanged — rejected (it canonicalizes `112`/`34`/`122`, masking FR-001's echo, FR-003's seqnum, and FR-004/004a's `122` replay evidence — the assertions G1 exists to make). A richer normalizer (canonicalize seqnums) — also rejected (same masking).

---

## R7 — `cell_results.yaml` + schema-check integration

**Decision**: Emit G1 cell rows through the **same** in-repo `interop_cell_results_schema_check` rules used by the 016 matrix (imported, not re-implemented); G1 cells join the existing matrix at **release-prep tier** (not per-PR, per 016 ROADMAP decision-2). A live `skip`/`fail` on a G1 cell makes it badge-ineligible (FR-009 / SC-002).

**Rationale**: zero schema divergence; the gate-contract is already tier-aware. The badge known-limitations/scope note is updated to reflect newly-asserted admin coverage **without** overstating scope (still session-admin, not app-message — FR-011).

**Alternatives considered**: a separate G1 results file — rejected (fragments the gate; duplicate schema).

---

## R8 — Reference-engine behaviour sweep (divergence guard, `[[feedback_always_invoke_speckit_clarify]]`)

**Decision**: Pin QuickFIX-J's expected admin behaviour so assertions reconcile to spec, not to one engine:
- **TestRequest→Heartbeat**: QFJ echoes the `TestReqID(112)` in the Heartbeat — standard, no divergence.
- **ResendRequest reply**: QFJ replays application messages with `PossDup(43=Y)`/`OrigSendingTime(122=)` and collapses admin-message gaps into `SequenceReset-GapFill(35=4,123=Y)`. Since G1 exchanges no app messages before the gap, the withheld/missing frames are admin → QFJ answers **predominantly (often exclusively) with GapFill**, so the `(43=Y,122=)` replay arm may **never fire** in a pure-admin G1 gap. The spec **accepts either** reply shape (FR-004/FR-004a), so this is not a divergence — but the `recovery_outbound` golden + contract table (parent contract §4) MUST be written to the GapFill-only outcome as the expected baseline, with the replay arm flagged as the conditional/optional shape (not a required assertion). For the **outbound** half specifically, the precondition that makes a QFJ-issued `ResendRequest` deterministic (preserved store, `ResetOnLogon=N`, expected `7/16`) is pinned in the parent contract `recovery_outbound` table.
- **Reject-vs-disconnect**: some malformed inputs may make a peer disconnect rather than `Reject`. Per spec (`§4.5.4`) a session-level reject is non-fatal; if QFJ disconnects for a given input, that is recorded as a **KNOWN-LIMITATION** (peer divergence), and the cell selects a malformed input that elicits a `Reject` (not a disconnect) for the positive assertion.

**Rationale**: the engine-drift rule (`[const §VI]` reconcile-to-spec) + the thorny-corpus precedent (Fix8-vs-QFC GapFill divergence is corpus-only; Fix8 not live in G1). No new blocking divergence surfaced.

**Alternatives considered**: assert QFJ-exact replay shape — rejected (engine-drift; the spec's either-shape acceptance is correct).

---

## Open items carried to Gate A

- **R1 architecture split** — the headline review item: confirm golden-based assertion + parent-driven manipulation is the right boundary vs any in-process need (escape hatch R-prod).
- **R-prod zero-production claim** — confirm no scenario secretly needs a production admin-originate API.
- **R3 injection mechanisms** — confirm the parent proxy/launcher can drive QFJ-restart-resend + malformed injection without in-repo code.
