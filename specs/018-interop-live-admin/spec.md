# Feature Specification: Live Session-Admin Interop Round-Trips (gap-fill G1)

**Feature Branch**: `018-interop-live-admin`  
**Created**: 2026-06-03  
**Status**: Draft  
**Input**: User description: "Live session-admin interop round-trips against QuickFIX-J (gap-fill G1). Extend the in-repo interop test harness so the live (paired-counterparty) cells exercise and assert REAL bidirectional FIX 4.4 session-admin traffic over the established TLS session — beyond the Logon/Logout handshake the existing session-layer badge already covers."

## Context

The session-layer FIX 4.4 interop **badge is already minted** (parent harness P0–P6 complete, 2026-06-02; `BADGE.md`, 32/32 live cells GREEN over TLS, both roles, vs QuickFIX-cpp v1.16.0 / QuickFIX-J 3.0.1). Those live cells today drive fixpp **to `Active` and capture only the Logon/Logout handshake** via the counterparty engine-log seam. This feature closes gap-fill rung **G1** (per `INTEROP-016-ROADMAP.md` and user direction 2026-06-02): make the live cells exercise and assert **real bidirectional session-admin traffic on the established session**, proving the admin behaviours that shipped in 005/013/S-023 interoperate with a live independent engine — not just against in-repo parity witnesses.

This is a **tests/harness-only** feature; **no new production library behaviour is expected** (the FSM/liveness/recovery admin paths shipped in 005/013/S-023). The public post-015 surface has no admin-originate API (`Session::send()` is app-payload-only) and no inbound-frame observer, so the work extends the gtest interop fixture to **induce** fixpp-originated admin frames (via inbound silence / FSM transitions, with engine-chosen ids) and **await/observe** admin frames on the live session — capturing richer golden transcripts that include these admin frames.

## Clarifications

### Session 2026-06-03

- Q: For G1, which fixpp role(s) should each admin scenario be asserted against live QuickFIX-J? → A: Both roles — each scenario runs with fixpp as initiator AND as acceptor (matches the badge's both-role scope).
- Q: Should the recovery scenario (US3) also assert fixpp answering a counterparty ResendRequest (outbound replay / SequenceReset-GapFill), or only fixpp detecting an inbound gap? → A: Both directions — inbound-gap-detect (fixpp issues ResendRequest) AND outbound-answer (fixpp replays / GapFills via 013's `build_sequence_reset_gapfill`).
- Q: Pin the idle Heartbeat-cadence (US2) assertion thresholds. → A: Negotiate `HeartBtInt(108)=1`s; assert ≥3 unsolicited heartbeats observed in EACH direction over a ~5s window, ±1-beat tolerance, no `TestRequest` triggered.

### Session 2026-06-03 (Gate A round 1)

- Q: The public post-015 surface has no admin-originate API and no inbound-frame observer — how is the fixpp-originated `TestRequest` driven and correlated? → A: It is **induced by inbound silence** (the liveness loop emits a `TestRequest` with an **engine-chosen** `112`); the round-trip is correlated by the `112` **observed in the golden**, not a fixture-chosen id (US1 / FR-001 / FR-006). (RC#1)
- Q: Does the 016 P4 normalizer's default tag set work for G1? → A: No. The shipped default canonicalizes `{9,10,34,52,60,112,122}`, which drops the very tags G1 asserts (`112`/`34`/`122`/`123`). G1 supplies an explicit `{52,10}` admin profile into `diff_transcripts`'s `excluded_tags` parameter (no library change) (FR-007). (RC#2)
- Q: Can fixpp originate a deliberately malformed admin frame so the peer rejects it (US4 reverse leg)? → A: No — there is no such API. Both US4 directions are exercised by the parent **corrupting a frame in flight at the proxy layer**; fixpp never emits malformed bytes (US4 / FR-005). (New-2)
- Q: The inbound-gap and outbound-answer induction mechanisms can yield different golden frame sets — how is determinism preserved? → A: **One** induction mechanism is pinned **per cell** (drop-a-frame for `recovery_inbound`; QFJ-restart-resend for `recovery_outbound`); the golden captures only the pinned mechanism's frames (research R3/R6, parent contract §3/§4). (New-3)
- Q: Is the outbound-seqnum witness on the public surface? → A: No — it reads `seqnum_mgr_test_access()`, a pre-existing tests-only seam (used by the 016 `testrequest_echo` cell), NOT public API and NOT a new seam; named explicitly so the "public surface only" framing isn't overstated (descriptor contract rule 3). (New-4)

### User Story 1 - Bidirectional TestRequest → Heartbeat liveness (Priority: P1)

A maintainer running the release-prep interop matrix needs proof that a live QuickFIX-J peer answers fixpp's `TestRequest` correctly and vice versa, on an already-established session. The public post-015 surface has **no admin-originate API** (`Session::send()` is app-payload-only), so fixpp's `TestRequest(35=1)` is **induced** — the harness holds the link inbound-silent until fixpp's liveness loop emits a `TestRequest` carrying an **engine-chosen** `TestReqID(112)`; the peer must answer with `Heartbeat(35=0)` echoing that same `112` (correlated by the **observed** `112` value, not a fixture-chosen id). The reverse direction (QFJ-originated `TestRequest` → fixpp `Heartbeat` with matching `112`) must also hold.

**Why this priority**: Lowest-complexity real admin round-trip with no sequence manipulation; it is the foundational proof that the established-session admin path interoperates and de-risks the fixture extension (induce/await on the live session) that every later story reuses. A viable MVP on its own.

**Independent Test**: Drive an existing live cell to `Active`, hold the link inbound-silent so fixpp's liveness loop emits a `TestRequest` over the live link, and assert the captured counterparty transcript shows a `Heartbeat` echoing the **engine-chosen** `112` observed on fixpp's `TestRequest`; then exercise the reverse with the counterparty originating the `TestRequest`. Delivers the first asserted bidirectional admin exchange.

**Acceptance Scenarios**:

1. **Given** an established fixpp↔QuickFIX-J live session at `Active`, **When** the link is held inbound-silent so fixpp's liveness loop emits a `TestRequest` with an engine-chosen `112=<observed>`, **Then** the wire transcript shows the peer's `Heartbeat(35=0)` carrying that same `112=<observed>` (correlated by the observed value) and the session stays `Active`.
2. **Given** an established session at `Active`, **When** the QuickFIX-J peer sends a `TestRequest` with `112=ID-B`, **Then** fixpp emits a `Heartbeat(35=0)` carrying `112=ID-B` that crosses the wire and the session stays `Active`.
3. **Given** the round-trip completed, **When** the transcript is compared against the golden under the admin normalization profile (canonicalize `52=`/`10=` only — `112` is matched verbatim), **Then** the `TestRequest`/`Heartbeat` admin frames are present and the echoed `112` matches.

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

A maintainer needs proof that a malformed/invalid admin frame received over the live session elicits a session-level `Reject(35=3)` from **fixpp** and that the session **survives** the reject (no disconnect). The load-bearing assertion is the inbound→react direction: the parent corrupts a counterparty frame in flight at the proxy layer, the malformed frame reaches fixpp, and fixpp emits the `Reject` (shipped path S-007). fixpp has **no public API to originate a deliberately malformed admin frame**, so the symmetric "peer rejects a fixpp-originated malformed frame" leg is exercised the same way — the parent corrupts a fixpp→peer frame in flight at the proxy layer; fixpp itself never deliberately emits malformed bytes.

**Why this priority**: Valuable robustness proof but the narrowest — it is a single non-fatal error round-trip, depends on the parent proxy injecting a controlled malformed admin frame on the live link, and is the least divergence-prone of the four. P3.

**Independent Test**: Have the parent corrupt an admin frame in flight at the proxy layer (a controlled defect the receiving engine rejects per `[FIX-SL §4.5.4]`), and assert the receiver's `Reject(35=3)` carrying the reference fields crosses the wire while the session remains `Active`.

**Acceptance Scenarios**:

1. **Given** an established live session, **When** a malformed admin message is sent over the live link, **Then** the receiving engine emits `Reject(35=3)` carrying `RefSeqNum(45)` and an appropriate `SessionRejectReason(373)`/`RefTagID(371)` and the session stays `Active` (no disconnect).
2. **Given** the reject crossed the wire, **When** subsequent heartbeats are observed, **Then** the session continues normal admin exchange, confirming the reject was non-fatal.

---

### Edge Cases

- **Counterparty unavailable**: when no live QuickFIX-J peer is present, every G1 cell MUST `skip:counterparty-unavailable` (FR-023 contract), exactly like the existing badge cells — never silently pass.
- **Heartbeat/observation timing flake**: the idle-cadence and recovery windows must tolerate scheduling jitter without becoming flaky; observation windows carry an explicit tolerance and an internal self-deadline so a missing frame fails fast rather than hanging the run.
- **Recovery ambiguity**: QuickFIX-J may answer a `ResendRequest` with either real message replay (`43=Y`/`122=`) or `SequenceReset-GapFill(123=Y)` depending on what is in its store; the assertion must accept either spec-legal reply shape.
- **Reject vs disconnect divergence**: if a peer disconnects rather than rejecting a given malformed input, that is a divergence to record (known-limitation row), not a fixpp defect — the scenario asserts fixpp's own behaviour and records the peer's.
- **TestReqID correlation**: each `TestRequest` carries a distinct `112` (engine-chosen for the fixpp-originated leg, QFJ-chosen for the reverse) so an echoed `Heartbeat` is unambiguously correlated to its prompt by the **observed** `112` value, not by a fixture-chosen id.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The live interop harness MUST exercise a bidirectional `TestRequest(35=1, 112=)` → `Heartbeat(35=0, 112=)` exchange on an established session and assert the echoed `112` matches, in both directions. The fixpp-originated leg is **induced by inbound silence** (the liveness loop emits a `TestRequest` with an **engine-chosen** `112`); the assertion correlates the answering `Heartbeat` against the `112` **observed in the golden**, not against a fixture-chosen id. The counterparty-originated leg uses a QFJ-chosen `112`. (The echoed `112` is assertable because the admin normalization profile does NOT canonicalize `112` — see FR-007.)
- **FR-002**: The harness MUST assert idle-state unsolicited `Heartbeat(35=0)` cadence in both directions against `HeartBtInt(108)=1`s, observing **at least 3** heartbeats in EACH direction over a **~5s** window (**±1-beat** tolerance) and asserting **no `TestRequest`** in the window. Because the liveness loop emits an inbound-silence `TestRequest` only after a `heartbt_int` grace window elapses with **no** inbound frame, the "no `TestRequest`" invariant holds iff the peer's heartbeats keep the inbound stream non-silent within that grace window; the cadence cell MUST therefore size its negotiated interval / grace relationship so a punctual peer heartbeat (within the ±1-beat tolerance) cannot falsify the invariant by tripping a spurious `TestRequest`.
- **FR-003**: The harness MUST induce a genuine inbound sequence gap on the live session and assert fixpp emits `ResendRequest(35=2)` with correct `BeginSeqNo(7)`/`EndSeqNo(16)`.
- **FR-004**: The harness MUST assert fixpp correctly processes the counterparty's recovery reply — accepting either real replay (`PossDup 43=Y`/`OrigSendingTime 122=`) or `SequenceReset-GapFill(35=4, 123=Y)` — and resynchronises to `Active` with the expected inbound sequence and no loss of the preserved prefix.
- **FR-004a**: The harness MUST also exercise the **outbound** recovery half: drive QuickFIX-J to issue a `ResendRequest(35=2)` against fixpp and assert fixpp answers with a spec-legal replay (`PossDup 43=Y`/`OrigSendingTime 122=`) and/or `SequenceReset-GapFill(35=4, 123=Y)` (013 `build_sequence_reset_gapfill`), after which QuickFIX-J resynchronises to `Active`.
- **FR-005**: The harness MUST exercise a session-level `Reject(35=3)` round-trip over the live link (malformed admin input injected by the parent proxy in flight — fixpp never originates malformed bytes) and assert the reject carries the reference fields and the session survives (remains `Active`, no disconnect).
- **FR-005a**: Every G1 scenario (FR-001..FR-005) MUST be asserted for **both fixpp roles** — once with fixpp as the initiator (vs a QuickFIX-J acceptor) and once with fixpp as the acceptor (vs a QuickFIX-J initiator), consistent with the existing badge's both-role scope.
- **FR-006**: The gtest interop fixture MUST be able to **observe** (via the golden engine-log seam) and **await** (via fixpp-state witnesses + self-deadline) the admin messages of the established live session, correlating by message type and key tags. fixpp-**originated** admin frames are NOT chosen by the fixture — they are FSM/liveness-induced (e.g. an inbound-silence `TestRequest` with an engine-chosen `112`, a gap-driven `ResendRequest`, a recovery `SequenceReset-GapFill`) and observed by their golden value. There is **no admin-originate API** on the post-015 surface (`Session::send()` is app-payload-only) and **no new production library behaviour** is introduced.
- **FR-007**: Each G1 scenario MUST capture richer **golden transcripts** that include the new admin frames, captured at first paired run via the existing engine-log seam (no MITM). The golden comparison MUST use the existing 016 `diff_transcripts(...)` utility with an **explicit G1 admin normalization profile that excludes only `{52, 10}`** (canonicalize `52=` SendingTime and `10=` CheckSum only), passed into `diff_transcripts`'s `excluded_tags` parameter. The default 016 tag set (`{9,10,34,52,60,112,122}`) MUST NOT be used for G1 cells: it drops `112`/`34`/`122`/`123`, which are exactly the tags G1 asserts (echo correlation, seqnum, OrigSendingTime/replay evidence). This needs **no library change** — the `excluded_tags` parameter already exists; G1 supplies the `{52,10}` set explicitly.
- **FR-008**: Each G1 scenario MUST emit `cell_results.yaml` rows that round-trip through the **same** in-repo `interop_cell_results_schema_check` rules used by the existing matrix, and integrate with the release-prep tier gate (not per-PR). The emitted G1 cell ids MUST be added to the in-repo `EXPECTED_IDS` frozenset (`cell_results_schema_check_test.py:132`) so that the `test_per_cell_completeness_no_silent_absence` assertion enforces **exact-set equality** (both `missing` AND `unexpected` must be empty) — a subset-only check would allow silent row deletion (per `[[feedback_completeness_gate_exact_set_not_subset]]`).
- **FR-009**: When no live counterparty is available, every G1 cell MUST resolve to `skip:counterparty-unavailable`; a live `skip`/`fail` on a G1 cell MUST make the cell ineligible for the green tally, consistent with FR-023/SC-001 of the existing harness.
- **FR-010**: Every G1 live-I/O assertion MUST carry an internal self-deadline so a missing or late frame fails the cell deterministically rather than hanging the test run. Per-cell default guidance: `testrequest_echo` cells — **10 s** (covers inbound-silence wait + echo round-trip); `idle_cadence` cells — **10 s** (covers the ~5 s observation window + tolerance); `recovery_inbound` and `recovery_outbound` cells — **30 s** (covers reconnect/re-logon cycle); `session_reject` cells — **10 s** (covers single round-trip). Each cell MAY use a tighter deadline where operationally justified; NO cell may rely on `ioc.run()` to terminate (the `[[feedback_fail_placeholder_red_test]]` constraint).
- **FR-011**: The feature MUST update the interop documentation surface (corpus/scenario index, `INTEROP-016-ROADMAP.md` G1 status, and the badge known-limitations/scope notes as warranted) to reflect the newly asserted admin coverage, and MUST NOT overstate the badge scope (session-admin, still not application-message interop).

### Key Entities *(include if feature involves data)*

- **Live admin scenario cell**: a named release-prep interop cell that drives an established fixpp↔QuickFIX-J session and asserts a specific admin round-trip; attributes: cell id, role (initiator/acceptor), direction(s), scenario group (TestRequest / cadence / recovery / reject), expected wire frames, status (`pass`/`fail`/`skip:<reason>`/`known-limitation:<tracking>`/`n/a`, the 016 `cell_results_schema_check` enum).
- **Admin round-trip assertion**: the send/await unit on the live session — originating frame, awaited reply, correlation key (e.g. `112`, `45`, seqnum range), self-deadline, tolerance.
- **Golden admin transcript**: the captured, canonicalised counterparty `toAdmin`/`fromAdmin` frame sequence for a cell, including the new admin frames; the drift gate compares against it.
- **Cell result row**: the `cell_results.yaml` entry validated by the in-repo schema-check (reused, not re-implemented).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All **five** G1 scenario groups (`testrequest_echo`, `idle_cadence`, `recovery_inbound`, `recovery_outbound`, `session_reject`) run as live cells — for **both fixpp roles** (initiator and acceptor) — and are GREEN end-to-end against a live QuickFIX-J 3.0.1 peer over `one_way_ca` TLS. Recovery is modelled as two separate groups (`recovery_inbound`: fixpp detects gap + issues ResendRequest; `recovery_outbound`: fixpp answers a QFJ ResendRequest), consistent with `data-model.md` E1 and `contracts/admin-scenario-descriptor.md` rule 8.
- **SC-002**: 100% of G1 live cells resolve to exactly one of `pass` / `skip:counterparty-unavailable` / `n/a` — never a silent pass — and the schema-check validates the emitted rows with zero errors.
- **SC-003**: For the recovery scenario, fixpp returns to `Active` with the expected inbound sequence number and zero loss of the preserved message prefix in 100% of runs where the counterparty replies with a spec-legal recovery shape.
- **SC-004**: Every G1 cell either produces a verified golden transcript (a deliberate single-tag mutation makes the drift gate bite) or is explicitly recorded as a known-limitation with rationale; no G1 cell ships without one of these. The gate-bite canary MUST mutate a tag the admin normalization profile actually **compares** (e.g. `112`, `34`, `7`, `16`, `122`, or `123`) — never one of the canonicalized `{52, 10}` tags, which the diff ignores and which would yield a false non-biting "pass".
- **SC-005**: The release-prep matrix run including G1 completes deterministically (no hangs, no flakes across a configured repeat count) within a quantified budget: the G1 admin cells add ≈ (**5 scenario groups × 2 roles = 10 base cells**, each bounded by its self-deadline per FR-010) to the 016 matrix; the dominant per-cell wall time is the US2 ~10 s self-deadline (cadence) and the US3 recovery ~30 s self-deadline (reconnect cycles). The repeat count is fixed at **3** (this spec is authoritative for the figure; the `INTEROP-016-ROADMAP.md` carries no numeric flake-gate/repeat figure to inherit). The G1 increment to the release-prep matrix wall-time budget MUST stay bounded by the sum of the per-cell self-deadlines stated above (no unbounded waits). No cell may rely on `ioc.run()` to terminate (FR-010).
- **SC-006**: The badge/known-limitations documentation accurately reflects that session-admin round-trips (beyond handshake) are now asserted, while continuing to disclaim application-message interop.

## Normative References

Per `[const §VI.5]`, these are the coverage-index entries that inform this spec; every G1 assertion reconciles to these, not to a reference engine (engine-drift rule, `[const §VI]`):

- **`[FIX-SL §4.5.1]`** FIX connection keep-alive (Heartbeat) — idle-cadence cell (US2 / FR-002).
- **`[FIX-SL §4.5.5]`** Test Request processing — TestRequest→Heartbeat echo (US1 / FR-001).
- **`[FIX-SL §4.5.4]`** Rejecting invalid messages (Reject 35=3) — session-level reject survival (US4 / FR-005).
- **`[FIX-SL §4.8.2]`** Request retransmission of messages (ResendRequest) — recovery inbound-detect + outbound-answer (US3 / FR-003 / FR-004 / FR-004a).
- **`[FIX-SL §4.8.5]`** Gap fill process (SequenceReset-GapFill) and **`[FIX-SL §4.8.6]`** Sequence reset — recovery reply shapes (US3 / FR-004 / FR-004a; 013 `build_sequence_reset_gapfill`).
- **`[FIX-SL §4.5.3]`** Missing sequence number (gap detection → ResendRequest) — inbound-gap induction + `BeginSeqNo(7)`/`EndSeqNo(16)` assertion (US3.i / FR-003).
- **`[const §VI]`** Spec-coverage / engine-drift rule — every assertion reconciles to the FIX spec, never to a single engine (FR-011, R8).
- **`[const §VI.5]`** Normative-references obligation — this section satisfies it for the `/specify` artifact.
- **`[const §IX.2]`** Tier-1 sanitizer matrix (ASan/UBSan/TSan) — the interop ctest runs fixpp under sanitizers against live admin traffic (plan IX.2; discharges the 016 verify-YELLOW waiver).
- **`[const §XV.18]`** No research/decision content in-repo — orchestration stays in the gitignored parent `phase-9-harness/`.
- **`[const §XVII.1]`** Gate A blocks `/tasks` and re-triggers on any production-surface escape — the R-prod escape hatch cites it.

## Assumptions

- The runtime engine's shipped FSM/liveness/recovery paths (005/013/S-023) **induce** every fixpp-originated admin frame the cells need — inbound-silence `TestRequest`, unsolicited Heartbeats, gap-driven `ResendRequest`, recovery `SequenceReset-GapFill` — observed by their golden value. There is **no admin-originate API** and **no new production library behaviour** is required: the fixture never chooses a `TestReqID` and never originates a malformed frame (malformed input is parent-proxy-injected in flight). If a scenario proves un-drivable without a production change, that change is out of this feature's scope and is surfaced as a finding (it would re-trigger Gate A).
- The counterparty is **live QuickFIX-J 3.0.1** over `one_way_ca` TLS; QuickFIX-cpp and Fix8 are not required for G1 (Fix8 stays corpus-only per FR-009 of 016; QFcpp pairing optional, not required for G1 sign-off).
- Wire visibility uses the existing **engine-log seam** (counterparty `toAdmin`/`fromAdmin`), consistent with the resolved P4 decision (b) — byte-exact MITM capture is not required.
- The matrix runs at **release-prep tier** only, not per-PR; the per-PR `interop-smoke.yml` is unchanged.
- The existing parent-harness rendezvous, port-lease, TLS fixtures, golden-capture tooling, and `cell_results.yaml` schema-check are reused; this feature extends them rather than re-implementing.
- Recovery dialogue can be driven from the harness in both directions without modifying fixpp production code, with **one induction mechanism pinned per cell** (New-3): `recovery_inbound` withholds/drops a single counterparty frame so fixpp sees a higher-than-expected `MsgSeqNum` (capturing fixpp's `ResendRequest` then QFJ's reply); `recovery_outbound` drives QuickFIX-J to issue a `ResendRequest` against fixpp via a QFJ restart/reconnect that expects a lower sequence. The two mechanisms yield different golden frame sets, so a cell does not mix them; the golden captures only the pinned mechanism's frames. If either proves un-drivable at the harness level, that gap is surfaced as a finding rather than met with a production change.
- Application/business messages (G2, `NewOrderSingle→ExecutionReport`, the `[const §VII.6]` residual), FIXT.1.1/5.0SP2, and mutual mTLS are **out of scope**.
