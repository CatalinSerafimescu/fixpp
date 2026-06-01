# Feature Specification: Interop Harness (Per-Release Interop Gate)

**Feature Branch**: `016-interop-harness`  
**Created**: 2026-06-01  
**Status**: Draft  
**Input**: User description: "interop-harness"

## Context *(non-normative)*

This feature realizes the Phase-9 per-release interop gate (`phases/phase-9.md`), re-baselined post-015 (`phases/phase-9/REFRESH-2026-06-01-post-015.md`). It bundles three disciplines whose investigation is already complete:

- **9.B** — happy-path interop matrix (fixpp as live System-Under-Test against mature reference engines).
- **9.C** — thorny-issues corpus (upstream-bug replay as an append-only regression suite).
- **9.G** — reference unit-test parity (mirror what the reference suites assert; deliverable `phases/phase-9/unit-test-parity-matrix.md` is done — this feature closes its GAP rows).

The prerequisites are met: the session FSM (005), TLS policy (011), transport (012), per-session live wiring (013/014), and the public runtime engine (015) have all shipped, so an end-to-end fixpp session is now drivable against a live counterparty. A self-test harness already exists (`phase-9-harness/`, reference-vs-reference); this feature pairs fixpp into it as the SUT.

## Clarifications

### Session 2026-06-01

- Q: What is the committed library deliverable vs. what stays in the gitignored parent harness? → A: Library commits the SUT-side artifacts (fixpp scenario drivers, golden transcripts, parity unit tests) under `tests/interop/`; the cross-engine fork-exec orchestration and counterparty-engine clones stay in the parent `phase-9-harness/`.
- Q: Is the thorny-issues corpus a pre-existing input, or built by this feature? → A: It does not exist yet; this feature performs the full v1.0 sweep (pre-seeded list + last-2-years closed-bug tail + ALL open issues) across QuickFIX-cpp, QuickFIX-J, and Fix8, and consolidates it.
- Q: Is TLS interop in the v1.0 gate scope? → A: Yes — the 4 TLS-logon cells activated by the post-015 refresh are gated at v1.0 (plain TCP and TLS-logon both gated); mutual-certificate (client-cert) mTLS interop is a v1.1 reach.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Happy-path session interop against live reference engines (Priority: P1)

A release engineer needs verifiable evidence that fixpp speaks the FIX session protocol correctly against the two most-deployed open-source FIX engines. They run fixpp's session loop against QuickFIX-cpp and QuickFIX-J, in both initiator and acceptor roles, on FIX 4.4 and FIX 5.0 SP2, exercising the full session-admin layer (Logon, Heartbeat active+idle, TestRequest, Reject, sequence-number management, ResendRequest/SequenceReset recovery, Logout, abnormal disconnect + reconnect). Each scenario's wire dialogue is captured and diffed against a per-scenario golden transcript.

**Why this priority**: This is the headline interop evidence — the "Interop verified against QuickFIX-cpp / QuickFIX-J" badge. Without it there is no release gate. It is the minimum viable product: a single role/engine/version cell already delivers standalone value (proof fixpp interoperates at all).

**Independent Test**: Stand up one reference engine on a TCP port with a known config, drive a fixpp session on the opposite side, capture both wire streams, and confirm the session reaches the expected FSM end-state on both sides with the expected sequence-number deltas — with no business messages involved.

**Acceptance Scenarios**:

1. **Given** QuickFIX-cpp configured as an acceptor on FIX 4.4, **When** fixpp connects as initiator and completes Logon → idle Heartbeat → TestRequest/Heartbeat → Logout, **Then** both sides end in the disconnected state, the captured wire matches the golden transcript (modulo timestamps/seqnums), and no unexpected administrative messages were exchanged.
2. **Given** fixpp configured as an acceptor on FIX 5.0 SP2, **When** QuickFIX-J connects as initiator and completes the same session-admin chain, **Then** the same end-state and golden-diff invariants hold from the acceptor perspective.
3. **Given** a session where fixpp's expected inbound sequence number is lower than the counterparty's next outbound (a gap), **When** the counterparty's higher-numbered message arrives, **Then** fixpp drives the spec-conformant recovery exchange (ResendRequest / SequenceReset-GapFill) and the session resynchronizes without a fatal disconnect.
4. **Given** an established session that is abruptly disconnected, **When** the side configured to reconnect re-establishes with `ResetSeqNumFlag=N`, **Then** sequence continuity is preserved per the spec and the golden transcript confirms no spurious reset.
5. **Given** a business-message cell of the matrix (e.g., NewOrderSingle → ExecutionReport), **When** the matrix is enumerated, **Then** that cell is present but explicitly tagged deferred (`deferred:app-messages`) with a rationale, not executed.

---

### User Story 2 - Thorny-issues corpus replay (Priority: P1)

A maintainer wants fixpp to be probed against the real interop failures that mature engines have hit in production over ~15–20 years. They sweep the issue trackers of QuickFIX-cpp, QuickFIX-J, and Fix8 for closed-with-fix and still-open bugs touching wire/session/persistence behavior, convert each into a fixpp test scenario reproducing the failing condition, and run fixpp against every scenario. The corpus is append-only across releases and each scenario links to its upstream provenance.

**Why this priority**: The corpus converts "fixpp should handle this by construction" into measured evidence and documented limitations. It is the differentiator no other open FIX engine has, and a P1 corpus failure blocks the release tag identically to a happy-path failure.

**Independent Test**: Take one categorized upstream bug (e.g., a SequenceReset/GapFill edge), encode its triggering message sequence and expected fixpp behavior, run it, and confirm fixpp either handles it per spec or fails in a way that is recorded as a known limitation with a tracking issue.

**Acceptance Scenarios**:

1. **Given** a closed upstream bug classified P1, **When** the corpus scenario drives its failing message sequence at fixpp, **Then** fixpp produces the spec-conformant outcome and the scenario passes.
2. **Given** an open upstream bug classified `watch:P1`, **When** the scenario runs and fixpp behaves correctly per spec while upstream is still broken, **Then** the scenario passes and the divergence is surfaced as a positive differentiator in the release notes.
3. **Given** a corpus scenario that fixpp does not pass, **When** the release is being prepared, **Then** the tag is blocked until the behavior is fixed OR the scenario is recorded as a documented known-limitation with an open tracking issue.
4. **Given** a prior release's corpus, **When** a new release sweep runs, **Then** the new scenarios are added to the existing corpus (append-only) and no prior scenario is removed.

---

### User Story 3 - Reference unit-test parity GAP closure (Priority: P2)

A maintainer wants assurance that every behavior the reference engines assert in their own unit tests is also asserted by fixpp. Using the completed behavior-parity audit (`unit-test-parity-matrix.md`), they close each row dispositioned GAP — writing the equivalent fixpp unit test (and implementing the behavior if it is genuinely missing) — while leaving COVERED and N/A rows untouched with their recorded rationale.

**Why this priority**: This is a standing quality discipline rather than live interop, so it is independent of counterparty availability and lower-risk than the live matrix; but it is the audit that answers "do we test everything they test." The audit is read-only and done; this feature is the closure of its open GAP rows.

**Independent Test**: Pick a GAP row, confirm fixpp lacks an equivalent assertion, add the fixpp unit test (and any missing behavior), and confirm the new test passes and the matrix row flips GAP → COVERED with a citation.

**Acceptance Scenarios**:

1. **Given** a parity row dispositioned GAP, **When** the corresponding fixpp unit test is added and passes, **Then** the matrix row is updated to COVERED with a pointer to the new test.
2. **Given** a parity row dispositioned N/A (architecture-specific to a reference engine), **When** the matrix is reviewed, **Then** the row is left unchanged and its rationale remains auditable.
3. **Given** a GAP row whose behavior is genuinely absent from fixpp, **When** closure requires implementation, **Then** the behavior is implemented and covered, or the gap is explicitly deferred with a tracking entry.

---

### User Story 4 - Release-gate enforcement, CI tiering, and sanitizer discipline (Priority: P2)

A release engineer needs the interop work wired as an actual gate. Every scenario in the happy-path matrix and the thorny corpus runs across the normal build and under sanitizers (TSan and combined ASan/UBSan); a sanitizer-only failure blocks the tag exactly as a normal-build failure does. The full matrix runs at release-prep and blocks the GA tag; a single lightweight smoke scenario runs per pull request to catch obvious regressions cheaply.

**Why this priority**: The disciplines in US1–US3 only have teeth if they are enforced. Sanitizer coverage is the strongest signal interop produces — live counterparty traffic is the only practical way to drive fixpp's concurrent session/store/transport paths under realistic timing.

**Independent Test**: Run the full matrix under TSan against a live counterparty and confirm a deliberately-introduced data race is caught and blocks the gate; run the per-PR smoke scenario and confirm it completes within the PR-latency budget on the normal build only.

**Acceptance Scenarios**:

1. **Given** the full interop matrix at release-prep, **When** any scenario fails on any of the three build configurations (normal, TSan, ASan/UBSan), **Then** the GA tag is blocked.
2. **Given** a scenario that passes on the normal build but trips TSan, **When** the gate evaluates it, **Then** it is treated as a real failure (fix or written-justification suppression — never silently ignored).
3. **Given** a pull request touching the transport or session surface, **When** CI runs, **Then** the lightweight smoke scenario runs on the normal build only and gates the PR.
4. **Given** the counterparty binaries, **When** scenarios run under sanitizers, **Then** only fixpp's process is sanitizer-instrumented; the counterparty runs as its unmodified production binary.

---

### User Story 5 - Interop badge and transcript artifacts (Priority: P3)

An adopter evaluating fixpp wants reassurance it interoperates with the engines they already run. At GA, the release notes carry an "Interop verified against QuickFIX-cpp <ver> / QuickFIX-J <ver>" badge linking to the captured wire transcripts and the corpus results.

**Why this priority**: This is the outward-facing payoff, but it depends entirely on US1/US2 producing green results first, so it is last.

**Independent Test**: After a green full-matrix run, confirm the release artifacts include per-scenario wire transcripts and a badge naming the exact engine versions and commits that were paired against.

**Acceptance Scenarios**:

1. **Given** a green full matrix and corpus run, **When** the release is published, **Then** the release notes include the interop badge naming exact counterparty versions and link to the archived transcripts.
2. **Given** any documented known-limitation from the corpus, **When** the release notes are assembled, **Then** each limitation is listed with its tracking issue.

---

### Edge Cases

- **Counterparty silence or divergence from spec**: when the two reference engines disagree, or one is silent on a control/observability surface, the assertion reconciles against the official FIX specification — never against whichever engine "happens to do X". A both-engines-deviate-from-spec case is parked as an open question referencing the spec section, not pinned to an engine.
- **Sequence-number reset mismatch**: one side resets while the other does not expect it — multiple valid resync paths exist in the spec; the scenario asserts the spec-mandated path.
- **Repeating-group nesting tolerance**: engines interpret group-order and first-tag-after-NumInGroup rules differently; golden transcripts assert the spec behavior, not the lenient engine's.
- **UTCTimestamp precision negotiation** (millis vs micros vs nanos) varying by counterparty config.
- **Unknown/custom tags** (5000+ range): fixpp's reject path must be wire-conformant, not interop on the field itself.
- **Fix8 as counterparty**: corpus-only at v1.0; its happy-path cells are placeholder rows tagged for a later live-disposition revisit, never populated.
- **Counterparty binary unavailable in CI** (e.g., upstream-dormant Fix8): the live matrix degrades gracefully — its scenarios are skipped-with-reason, not silently passed.
- **Recovery cells**: too-high-seqnum recovery now exercises real ResendRequest/SequenceReset behavior (013), not the pre-013 fatal-disconnect path; stale Phase-9 "fatal seqnum" notes do not apply.

## Requirements *(mandatory)*

### Functional Requirements

**Happy-path matrix (US1)**

- **FR-001**: The harness MUST pair fixpp as the System-Under-Test against QuickFIX-cpp and QuickFIX-J as live counterparties.
- **FR-002**: The harness MUST exercise both roles for each live counterparty: fixpp-initiator ↔ counterparty-acceptor AND fixpp-acceptor ↔ counterparty-initiator.
- **FR-003**: The harness MUST cover FIX 4.4 and FIX 5.0 SP2 at minimum.
- **FR-004**: The happy-path matrix MUST exercise the session-admin layer: Logon, Heartbeat (active and idle), TestRequest, Reject, sequence-number management, ResendRequest, SequenceReset (GapFill and Reset modes), Logout, and abnormal disconnect + reconnect with `ResetSeqNumFlag=N`.
- **FR-005**: The happy-path matrix MUST be scoped session-only for v1.0: business-message cells (e.g., NewOrderSingle → ExecutionReport) MUST be present in the matrix but tagged `deferred:app-messages` with a rationale, and MUST NOT be executed.
- **FR-006**: Each happy-path scenario MUST capture the byte-level wire dialogue of both sides and diff it against a per-scenario golden transcript, with a normalization layer that excludes timestamps, sequence numbers, and other non-deterministic fields from the diff.
- **FR-007**: Each happy-path scenario MUST assert the session FSM end-state on both sides and the sequence-number deltas, in addition to the wire diff.
- **FR-008**: Every matrix axis (counterparty × role × FIX-version × session-event-chain) MUST be covered at least once; any skipped cell MUST carry an explicit deferred-to-vN.x note with rationale.
- **FR-009**: Fix8 MUST be treated as corpus-only at v1.0; its happy-path cells MUST be placeholder rows tagged for a later live-disposition revisit and MUST NOT be populated.
- **FR-025**: The v1.0 matrix MUST gate both plain-TCP and TLS-logon session interop — the 4 TLS-logon cells activated by the post-015 refresh are in scope. Mutual-certificate (client-cert) mTLS interop cells MAY be deferred to v1.1 and, if deferred, MUST carry an explicit deferred-to-v1.1 note.

**Thorny corpus (US2)**

- **FR-010**: This feature MUST perform the v1.0 thorny-issues sweep itself (the corpus does not pre-exist) and consolidate it: the pre-seeded list + the last-2-years closed-with-fix tail + ALL open issues across QuickFIX-cpp, QuickFIX-J, and Fix8 that touch wire / session / persistence behavior.
- **FR-011**: Each corpus scenario MUST record its upstream provenance (engine, issue/PR reference, state) so the motivating bug is traceable.
- **FR-012**: Corpus scenarios MUST be bucketed by priority: closed-bug scenarios as P1 (release-blocking) / P2 / P3; open-issue scenarios as `watch:P1` / `watch:P2` / `watch:info`.
- **FR-013**: The corpus MUST be append-only across releases — a new release sweep adds scenarios and never removes prior ones.
- **FR-014**: A failing P1 or `watch:P1` corpus scenario MUST block the release tag until the behavior is fixed OR the scenario is recorded as a documented known-limitation with an open tracking issue.
- **FR-015**: Corpus scenarios where fixpp behaves correctly per spec while upstream remains buggy MUST be identifiable as positive differentiators for the release notes.

**Unit-test parity (US3)**

- **FR-016**: The feature MUST close every reference-unit-test-parity row dispositioned GAP by adding the equivalent fixpp unit test, implementing the behavior if it is genuinely missing, or explicitly deferring it with a tracking entry.
- **FR-017**: Closed GAP rows MUST be updated to COVERED with a citation to the fixpp test that covers them; N/A rows MUST be left unchanged with their recorded rationale.

**Reconciliation principle (all disciplines)**

- **FR-018**: Every assertion MUST reconcile against the official FIX specification (FIX 4.4 / FIXT.1.1 / FIX 5.0 SP2), NOT against QuickFIX-cpp or QuickFIX-J behavior; when the reference engines disagree or are silent, the spec mandate is encoded and any both-engines-deviate case is parked as an open question citing the spec section.

**Gate, CI tiering, sanitizers (US4)**

- **FR-019**: Every happy-path and thorny-corpus scenario MUST run across three build configurations: normal (release-mode), TSan, and combined ASan/UBSan.
- **FR-020**: A sanitizer-only failure MUST block the release tag identically to a normal-build failure; the only acceptable resolutions are a fix or a written-justification suppression.
- **FR-021**: Only fixpp's process MUST be sanitizer-instrumented; the counterparty MUST run as its unmodified production binary.
- **FR-022**: The full matrix + corpus MUST run at release-prep and block the GA tag; a single lightweight smoke scenario MUST run per pull request (normal build only) touching the transport/session surface.
- **FR-023**: When a live counterparty binary is unavailable in a given environment, its scenarios MUST be skipped-with-recorded-reason, never silently reported as passed.

**Badge / artifacts (US5)**

- **FR-024**: At GA, the release notes MUST carry an interop badge naming the exact counterparty versions/commits paired against, linking to the archived wire transcripts and corpus results, and listing every documented known-limitation with its tracking issue.

**Deliverable boundary**

- **FR-026**: The committed library deliverable MUST be the SUT-side artifacts under `tests/interop/`: the fixpp scenario drivers, the per-scenario golden transcripts, and the parity unit tests. The cross-engine fork-exec orchestration and the counterparty-engine clones/builds MUST remain in the gitignored parent harness (`phase-9-harness/`) and MUST NOT be vendored into the submodule.

### Key Entities

- **Scenario**: a single named, reproducible interop test (happy-path cell or corpus item) with pre-conditions, a driven message/event sequence, and pass/fail criteria.
- **Happy-path matrix cell**: a scenario keyed by (counterparty, role, FIX version, session-event chain); business-message cells exist but are deferred.
- **Corpus scenario**: a scenario derived from an upstream bug, carrying provenance (engine, issue ref, state) and a priority bucket.
- **Parity row**: one reference-engine unit-test behavior with a disposition (COVERED / GAP / N/A) and a fixpp-test citation or closure task.
- **Golden transcript**: the expected byte-level wire dialogue for a scenario, compared via a normalization layer.
- **Counterparty**: a reference FIX engine (QuickFIX-cpp, QuickFIX-J live; Fix8 corpus-only) with a build/spawn recipe and a v1.0 disposition.
- **Interop badge**: the release-notes artifact attesting verified interop against named counterparty versions, linking transcripts + corpus results.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The happy-path matrix passes for both live counterparties (QuickFIX-cpp and QuickFIX-J), in both initiator and acceptor roles, across FIX 4.4 and FIX 5.0 SP2, for the full session-admin event set including the 4 TLS-logon cells — 100% of non-deferred cells green.
- **SC-002**: 100% of P1 and `watch:P1` corpus scenarios either pass or are recorded as documented known-limitations with open tracking issues; zero P1 failures are silently unaddressed at the GA tag.
- **SC-003**: 100% of parity rows dispositioned GAP are closed (COVERED with a citation) or explicitly deferred with a tracking entry; no GAP row is left undispositioned.
- **SC-004**: The full matrix + corpus pass under all three build configurations (normal, TSan, ASan/UBSan) with zero sanitizer findings other than written-justification suppressions.
- **SC-005**: The per-PR smoke scenario completes within the project's PR-latency budget on the normal build and reliably catches an introduced wire regression.
- **SC-006**: Every executed scenario reconciles its assertions to a cited FIX specification section, with zero assertions justified by "because engine X does it that way".
- **SC-007**: The published release notes carry the interop badge with exact counterparty versions and links to archived transcripts and the documented-limitations list.

## Normative References

The harness reconciles every assertion against these (FR-018). Per `[const §VI.5]`, these are the coverage-index entries that inform this spec:

- **`[FIX-TC]`** — FIX Session Layer Test Cases (TC-001..TC-017). The conformance oracle the harness reconciles to; the engine-drift rule (FR-018) resolves reference-engine disagreement against these + the session-layer spec, never against an engine.
- **`[FIX-SL §4.4]`** — message recovery: `ResendRequest(35=2)`, `SequenceReset(35=4)` GapFill/Reset modes, `ResetSeqNumFlag(141)` (US1 acceptance 3/4, recovery cells; informed by 013).
- **`[FIX-SL §6.2]`** — session-level sequence number management and gap detection (US1 acceptance 3, seqnum-delta assertions).
- **`[FIX-SL §5.3.x]`** — administrative messages: Logon, Logout, Heartbeat, TestRequest, Reject (US1 session-admin event set, FR-004).
- **`[FIXS §3.2]`, `[FIXS §3.4]`** — FIX-over-TLS session profile + certificate validation (FR-025 TLS-logon cells; inherited from 011/012).
- **`[FIX50SP2 §3.1]`** — FIX 5.0 SP2 / FIXT.1.1 session routing (FR-003 FIX-version axis).
- **`[const §VII.6]`** — the v1.0 interop requirement against an independent FIX implementation (QuickFIX); see the Constitution-tension note in `plan.md` re: its `NewOrderSingle → ExecutionReport` clause vs. the session-only scope.
- **`[const §IX.2]`** — Tier-1 sanitizer matrix (ASan/UBSan/TSan), the basis for FR-019/FR-020.

## Assumptions

- **Scope is session-only for v1.0** (matrix option (a) per the post-015 refresh and user decision #8): business-message families are deferred until a real counterparty engagement requires them; the headline interop evidence is the FIX session layer.
- **Live counterparties at v1.0 are QuickFIX-cpp and QuickFIX-J**; Fix8 is corpus-only at v1.0 (its live disposition is revisited later based on corpus findings).
- **The transport dependency is satisfied**: TCP/TLS transport (012) and per-session live wiring + public runtime engine (013/014/015) have shipped, so an end-to-end fixpp session is drivable against a live counterparty without bundling additional transport work into this feature.
- **9.B and 9.G inputs exist; 9.C does not**: the re-baselined happy-path matrix (`cross-communication-test-plan.md`) and the parity matrix (`unit-test-parity-matrix.md`) already exist as research inputs, and the pre-9.F gap-closure slate (S-021 EncryptMethod, S-023 reset confirmation, integer-overflow bounds) has landed. The thorny-issues corpus does NOT yet exist — this feature performs its full v1.0 sweep + consolidation (see FR-010).
- **A reference-vs-reference self-test harness already exists** (`phase-9-harness/`); this feature pairs fixpp into it as the SUT and re-anchors each converted assertion against the FIX spec.
- **TLS posture at v1.0**: both plain TCP and TLS-logon session interop gate v1.0 (the 4 TLS-logon cells were activated by the post-015 refresh now that TLS policy (011) and TLS transport (012) shipped); mutual-certificate (client-cert) mTLS interop is the v1.1 reach (FR-025).
- **MSan is out of scope** for the sanitizer matrix (third-party dependency surface is not MSan-instrumented); TSan and combined ASan/UBSan are the gate.
- **Counterparty binaries are managed outside the library repo** (the reference engines are gitignored clones / CI setup-step builds); their exact build/spawn management is an implementation detail for the planning phase.
