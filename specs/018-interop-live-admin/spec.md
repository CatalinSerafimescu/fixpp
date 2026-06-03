# Feature Specification: Live Session-Admin Interop Round-Trips (gap-fill G1)

**Feature Branch**: `018-interop-live-admin`  
**Created**: 2026-06-03  
**Status**: Draft  
**Input**: User description: "Live session-admin interop round-trips against QuickFIX-J (gap-fill G1). Extend the in-repo interop test harness so the live (paired-counterparty) cells exercise and assert REAL bidirectional FIX 4.4 session-admin traffic over the established TLS session — beyond the Logon/Logout handshake the existing session-layer badge already covers."

## Context

The session-layer FIX 4.4 interop **badge is already minted** (parent harness P0–P6 complete, 2026-06-02; `BADGE.md`, 32/32 live cells GREEN over TLS, both roles, vs QuickFIX-cpp v1.16.0 / QuickFIX-J 3.0.1). Those live cells today drive fixpp **to `Active` and capture only the Logon/Logout handshake** via the counterparty engine-log seam. This feature closes gap-fill rung **G1** (per `INTEROP-016-ROADMAP.md` and user direction 2026-06-02): make the live cells exercise and assert **real bidirectional session-admin traffic on the established session**, proving the admin behaviours that shipped in 005/013/S-023 interoperate with a live independent engine — not just against in-repo parity witnesses.

This is a **tests/harness-only** feature. The runtime engine already exposes the admin send path (post-015); no new production library behaviour is expected. The work extends the gtest interop fixture to send and await specific admin messages on the live session and captures richer golden transcripts that include these admin frames.

## Clarifications

### Session 2026-06-03

- Q: For G1, which fixpp role(s) should each admin scenario be asserted against live QuickFIX-J? → A: Both roles — each scenario runs with fixpp as initiator AND as acceptor (matches the badge's both-role scope).
- Q: Should the recovery scenario (US3) also assert fixpp answering a counterparty ResendRequest (outbound replay / SequenceReset-GapFill), or only fixpp detecting an inbound gap? → A: Both directions — inbound-gap-detect (fixpp issues ResendRequest) AND outbound-answer (fixpp replays / GapFills via 013's `build_sequence_reset_gapfill`).
- Q: Pin the idle Heartbeat-cadence (US2) assertion thresholds. → A: Negotiate `HeartBtInt(108)=1`s; assert ≥3 unsolicited heartbeats observed in EACH direction over a ~5s window, ±1-beat tolerance, no `TestRequest` triggered.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Bidirectional TestRequest → Heartbeat liveness (Priority: P1)

A maintainer running the release-prep interop matrix needs proof that a live QuickFIX-J peer answers fixpp's `TestRequest` correctly and vice versa, on an already-established session. fixpp sends `TestRequest(35=1)` carrying a `TestReqID(112)`; the peer must answer with `Heartbeat(35=0)` echoing the same `112`. The reverse direction (QFJ-originated `TestRequest` → fixpp `Heartbeat` with matching `112`) must also hold.

**Why this priority**: Lowest-complexity real admin round-trip with no sequence manipulation; it is the foundational proof that the established-session admin path interoperates and de-risks the fixture extension (send/await on the live session) that every later story reuses. A viable MVP on its own.

**Independent Test**: Drive an existing live cell to `Active`, have the fixture send `TestRequest(112=<id>)` over the live link, and assert the captured counterparty transcript shows a `Heartbeat` with `112=<id>`; then exercise the reverse with the counterparty originating the `TestRequest`. Delivers the first asserted bidirectional admin exchange.

**Acceptance Scenarios**:

1. **Given** an established fixpp↔QuickFIX-J live session at `Active`, **When** fixpp sends `TestRequest` with `112=ID-A`, **Then** the wire transcript shows the peer's `Heartbeat(35=0)` carrying `112=ID-A` and the session stays `Active`.
2. **Given** an established session at `Active`, **When** the QuickFIX-J peer sends a `TestRequest` with `112=ID-B`, **Then** fixpp emits a `Heartbeat(35=0)` carrying `112=ID-B` that crosses the wire and the session stays `Active`.
3. **Given** the round-trip completed, **When** the transcript is compared against the golden, **Then** the `TestRequest`/`Heartbeat` admin frames are present and match (modulo the canonicalised `52=`/`10=` fields).

---

### User Story 2 - Idle Heartbeat cadence (Priority: P2)

A maintainer needs proof that, with a low negotiated `HeartBtInt`, both engines emit unsolicited heartbeats at the agreed cadence over the live link, so liveness keep-alive interoperates without traffic.

**Why this priority**: Builds directly on the US1 send/await fixture but adds time-window observation; validates the negotiated-`108` cadence behaviour against a live peer. Independent and demonstrable, but lower urgency than the explicit request/response of US1.

**Independent Test**: Negotiate `HeartBtInt(108)=1`s on the live cell, hold the session idle for a ~5s window, and assert at least 3 heartbeats are observed crossing the wire in **each** direction (±1-beat tolerance), with no `TestRequest` triggered.

**Acceptance Scenarios**:

1. **Given** a live session negotiated with `HeartBtInt=1`s, **When** the session is held idle for a ~5s observation window, **Then** at least 3 `Heartbeat(35=0)` frames from fixpp and at least 3 from QuickFIX-J are observed within the window (±1-beat tolerance) and no `TestRequest` is triggered.
2. **Given** the idle window elapses, **When** the cadence is measured, **Then** the inter-heartbeat interval matches the negotiated `108=1`s within the tolerance band and the session remains `Active`.

---

### User Story 3 - ResendRequest / SequenceReset-GapFill recovery dialogue (Priority: P1)

A maintainer needs proof that fixpp's recovery sub-protocol (shipped in 013/S-023) interoperates with a live QuickFIX-J peer in **both directions**: (i) when a real inbound sequence gap occurs, fixpp detects it, drives the recovery dialogue, and resynchronises to `Active`; and (ii) when QuickFIX-J issues a `ResendRequest` against fixpp, fixpp answers correctly by replaying its sent messages and/or emitting `SequenceReset-GapFill(35=4,123=Y)` (013's `build_sequence_reset_gapfill` outbound path), and the peer resynchronises.

**Why this priority**: This is the highest-value correctness scenario — recovery is the most divergence-prone area across engines (cf. the Fix8 vs QuickFIX GapFill divergence already in the thorny corpus). Proving both the inbound-detect and outbound-answer halves live against QuickFIX-J is the strongest interop signal in G1. P1 alongside US1 because it exercises the core seqnum-recovery promise the badge scope names.

**Independent Test**: (i) Induce a genuine inbound gap on the live session (withhold/drop a counterparty message so fixpp sees `MsgSeqNum` higher than expected), then assert fixpp emits `ResendRequest(35=2)`, the peer replays with `PossDup(43=Y)`/`OrigSendingTime(122=)` or `SequenceReset-GapFill(35=4,123=Y)`, and fixpp returns to `Active`. (ii) Drive QuickFIX-J to issue a `ResendRequest` against fixpp (e.g. QFJ restarts/reconnects expecting a lower sequence), then assert fixpp answers with a spec-legal replay and/or `SequenceReset-GapFill` and QFJ resynchronises to `Active`.

**Acceptance Scenarios**:

1. **Given** an established live session, **When** a real inbound sequence gap is induced (a counterparty message is withheld so the next received `MsgSeqNum` exceeds expected), **Then** fixpp emits `ResendRequest(35=2)` with the correct `BeginSeqNo`/`EndSeqNo` over the wire.
2. **Given** fixpp has issued the `ResendRequest`, **When** QuickFIX-J replays the gap as administrative `SequenceReset-GapFill(35=4,123=Y)` and/or `PossDup(43=Y)` application/admin messages, **Then** fixpp applies them and resynchronises its expected inbound sequence.
3. **Given** an established live session, **When** QuickFIX-J issues a `ResendRequest(35=2)` against fixpp for a prior range, **Then** fixpp answers with a spec-legal replay (`PossDup 43=Y`/`OrigSendingTime 122=`) and/or `SequenceReset-GapFill(35=4,123=Y)` over the wire and QuickFIX-J resynchronises.
4. **Given** either recovery dialogue completes, **When** the session state is observed, **Then** both peers are back at `Active` with no data loss on the preserved prefix and the session continues to exchange heartbeats.

---

### User Story 4 - Session-level Reject(35=3) over the live link (Priority: P3)

A maintainer needs proof that a malformed/invalid admin message sent over the live session elicits a session-level `Reject(35=3)` from the peer (or from fixpp for a peer-originated malformed message) and that the session **survives** the reject (no disconnect).

**Why this priority**: Valuable robustness proof but the narrowest — it is a single non-fatal error round-trip, depends on being able to inject a controlled malformed admin frame on the live link, and is the least divergence-prone of the four. P3.

**Independent Test**: Inject a malformed admin message over the live session (a controlled defect the peer rejects per `[FIX-SL §4.5.4]`), and assert the peer's `Reject(35=3)` carrying the reference fields crosses the wire while the session remains `Active`.

**Acceptance Scenarios**:

1. **Given** an established live session, **When** a malformed admin message is sent over the live link, **Then** the receiving engine emits `Reject(35=3)` carrying `RefSeqNum(45)` and an appropriate `SessionRejectReason(373)`/`RefTagID(371)` and the session stays `Active` (no disconnect).
2. **Given** the reject crossed the wire, **When** subsequent heartbeats are observed, **Then** the session continues normal admin exchange, confirming the reject was non-fatal.

---

### Edge Cases

- **Counterparty unavailable**: when no live QuickFIX-J peer is present, every G1 cell MUST `skip:counterparty-unavailable` (FR-023 contract), exactly like the existing badge cells — never silently pass.
- **Heartbeat/observation timing flake**: the idle-cadence and recovery windows must tolerate scheduling jitter without becoming flaky; observation windows carry an explicit tolerance and an internal self-deadline so a missing frame fails fast rather than hanging the run.
- **Recovery ambiguity**: QuickFIX-J may answer a `ResendRequest` with either real message replay (`43=Y`/`122=`) or `SequenceReset-GapFill(123=Y)` depending on what is in its store; the assertion must accept either spec-legal reply shape.
- **Reject vs disconnect divergence**: if a peer disconnects rather than rejecting a given malformed input, that is a divergence to record (known-limitation row), not a fixpp defect — the scenario asserts fixpp's own behaviour and records the peer's.
- **TestReqID uniqueness**: each `TestRequest` uses a unique `112` so an echoed `Heartbeat` is unambiguously correlated to its prompt.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The live interop harness MUST exercise a bidirectional `TestRequest(35=1, 112=)` → `Heartbeat(35=0, 112=)` exchange on an established session and assert the echoed `112` matches, in both directions (fixpp-originated and counterparty-originated).
- **FR-002**: The harness MUST assert idle-state unsolicited `Heartbeat(35=0)` cadence in both directions against `HeartBtInt(108)=1`s, observing **at least 3** heartbeats in EACH direction over a **~5s** window (**±1-beat** tolerance) without inducing a `TestRequest`.
- **FR-003**: The harness MUST induce a genuine inbound sequence gap on the live session and assert fixpp emits `ResendRequest(35=2)` with correct `BeginSeqNo(7)`/`EndSeqNo(16)`.
- **FR-004**: The harness MUST assert fixpp correctly processes the counterparty's recovery reply — accepting either real replay (`PossDup 43=Y`/`OrigSendingTime 122=`) or `SequenceReset-GapFill(35=4, 123=Y)` — and resynchronises to `Active` with the expected inbound sequence and no loss of the preserved prefix.
- **FR-004a**: The harness MUST also exercise the **outbound** recovery half: drive QuickFIX-J to issue a `ResendRequest(35=2)` against fixpp and assert fixpp answers with a spec-legal replay (`PossDup 43=Y`/`OrigSendingTime 122=`) and/or `SequenceReset-GapFill(35=4, 123=Y)` (013 `build_sequence_reset_gapfill`), after which QuickFIX-J resynchronises to `Active`.
- **FR-005**: The harness MUST exercise a session-level `Reject(35=3)` round-trip over the live link (malformed admin input) and assert the reject carries the reference fields and the session survives (remains `Active`, no disconnect).
- **FR-005a**: Every G1 scenario (FR-001..FR-005) MUST be asserted for **both fixpp roles** — once with fixpp as the initiator (vs a QuickFIX-J acceptor) and once with fixpp as the acceptor (vs a QuickFIX-J initiator), consistent with the existing badge's both-role scope.
- **FR-006**: The gtest interop fixture MUST be able to **send** specific admin messages on, and **await** specific admin messages from, the established live session (correlating by message type and key tags) using the runtime engine's existing post-015 send path — with no new production library behaviour.
- **FR-007**: Each G1 scenario MUST capture richer **golden transcripts** that include the new admin frames, captured at first paired run via the existing engine-log seam (no MITM), and the golden comparison MUST canonicalise only volatile fields (`52=`/`10=`) consistent with the existing P4 normalizer.
- **FR-008**: Each G1 scenario MUST emit `cell_results.yaml` rows that round-trip through the **same** in-repo `interop_cell_results_schema_check` rules used by the existing matrix, and integrate with the release-prep tier gate (not per-PR).
- **FR-009**: When no live counterparty is available, every G1 cell MUST resolve to `skip:counterparty-unavailable`; a live `skip`/`fail` on a G1 cell MUST make the cell ineligible for the green tally, consistent with FR-023/SC-001 of the existing harness.
- **FR-010**: Every G1 live-I/O assertion MUST carry an internal self-deadline so a missing or late frame fails the cell deterministically rather than hanging the test run.
- **FR-011**: The feature MUST update the interop documentation surface (corpus/scenario index, `INTEROP-016-ROADMAP.md` G1 status, and the badge known-limitations/scope notes as warranted) to reflect the newly asserted admin coverage, and MUST NOT overstate the badge scope (session-admin, still not application-message interop).

### Key Entities *(include if feature involves data)*

- **Live admin scenario cell**: a named release-prep interop cell that drives an established fixpp↔QuickFIX-J session and asserts a specific admin round-trip; attributes: cell id, role (initiator/acceptor), direction(s), scenario group (TestRequest / cadence / recovery / reject), expected wire frames, status (`pass`/`skip`/`fail`/`n-a`).
- **Admin round-trip assertion**: the send/await unit on the live session — originating frame, awaited reply, correlation key (e.g. `112`, `45`, seqnum range), self-deadline, tolerance.
- **Golden admin transcript**: the captured, canonicalised counterparty `toAdmin`/`fromAdmin` frame sequence for a cell, including the new admin frames; the drift gate compares against it.
- **Cell result row**: the `cell_results.yaml` entry validated by the in-repo schema-check (reused, not re-implemented).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All four G1 scenario groups (TestRequest→Heartbeat both directions, idle cadence both directions, recovery dialogue covering both inbound-detect and outbound-answer, session-level Reject) run as live cells — for **both fixpp roles** (initiator and acceptor) — and are GREEN end-to-end against a live QuickFIX-J 3.0.1 peer over `one_way_ca` TLS.
- **SC-002**: 100% of G1 live cells resolve to exactly one of `pass` / `skip:counterparty-unavailable` / `n/a` — never a silent pass — and the schema-check validates the emitted rows with zero errors.
- **SC-003**: For the recovery scenario, fixpp returns to `Active` with the expected inbound sequence number and zero loss of the preserved message prefix in 100% of runs where the counterparty replies with a spec-legal recovery shape.
- **SC-004**: Every G1 cell either produces a verified golden transcript (drift gate bites on a deliberate mutation) or is explicitly recorded as a known-limitation with rationale; no G1 cell ships without one of these.
- **SC-005**: The release-prep matrix run including G1 completes deterministically (no hangs, no flakes across a configured repeat count) within the existing matrix time budget.
- **SC-006**: The badge/known-limitations documentation accurately reflects that session-admin round-trips (beyond handshake) are now asserted, while continuing to disclaim application-message interop.

## Assumptions

- The runtime engine's post-015 admin send path is sufficient to originate `TestRequest`, drive heartbeats, issue `ResendRequest`, and send a controlled malformed admin frame from the test fixture; **no new production library behaviour** is required (session FSM admin handling shipped in 005/013/S-023). If a scenario proves un-drivable without a production change, that change is out of this feature's scope and is surfaced as a finding (it would re-trigger Gate A).
- The counterparty is **live QuickFIX-J 3.0.1** over `one_way_ca` TLS; QuickFIX-cpp and Fix8 are not required for G1 (Fix8 stays corpus-only per FR-009 of 016; QFcpp pairing optional, not required for G1 sign-off).
- Wire visibility uses the existing **engine-log seam** (counterparty `toAdmin`/`fromAdmin`), consistent with the resolved P4 decision (b) — byte-exact MITM capture is not required.
- The matrix runs at **release-prep tier** only, not per-PR; the per-PR `interop-smoke.yml` is unchanged.
- The existing parent-harness rendezvous, port-lease, TLS fixtures, golden-capture tooling, and `cell_results.yaml` schema-check are reused; this feature extends them rather than re-implementing.
- Recovery dialogue can be driven from the harness in both directions without modifying fixpp production code: the **inbound** gap by withholding/reordering a counterparty frame (so fixpp sees a higher-than-expected `MsgSeqNum`), and the **outbound** answer by driving QuickFIX-J to issue a `ResendRequest` against fixpp (e.g. a QFJ restart/reconnect that expects a lower sequence). If either proves un-drivable at the harness level, that gap is surfaced as a finding rather than met with a production change.
- Application/business messages (G2, `NewOrderSingle→ExecutionReport`, the `[const §VII.6]` residual), FIXT.1.1/5.0SP2, and mutual mTLS are **out of scope**.
