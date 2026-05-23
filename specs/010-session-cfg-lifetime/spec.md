# Feature Specification: 010-session-cfg-lifetime

**Feature Branch**: `010-session-cfg-lifetime`
**Created**: 2026-05-23
**Status**: Draft
**Input**: User description: "010-session-cfg-lifetime — Eliminate the pre-existing 005-baseline ASan stack-use-after-scope on `SessionConfig` lifetime AND close the PR #82 Gate B P2 deferrals bundled at the same touch surface (W-5 primary + F-04 / F-05 / F-06 / F-07 + E1 / F-11 + RC#G mixed-path bookkeeping witnesses)."

## Context

This slice closes the residual P2/P3 waivers recorded against PR #82 Gate B (`library/.specify/decisions/009-session-fsm-finalize-gateb.md`, tracked audit in `phases/phase-4/session/009-session-fsm-finalize.md`). The **W-5 baseline ASan use-after-scope** is a pre-existing 005-baseline defect — `Session` stores `const SessionConfig& cfg_` (declared at `include/fixpp/session/session.hpp:312`) and dereferences it across the FSM long after construction (first site `src/session/session.cpp:116`, `cfg_.executor_override.value_or(engine_.executor)`). Production fixtures construct `SessionConfig` as a local; when the fixture leaves scope the reference dangles. Verified pre-009 via `git show 4e621e1:src/session/session.cpp`. The other deferrals (F-04 / F-05 / F-06 / F-07 + E1 / F-11 / RC#G mixed-path) touch the same files (`src/session/session.cpp`, `include/fixpp/session/session.hpp`, `tests/session/*`) and are bundled here so the refactor and the witness improvements land together.

**Binding 005 design is unchanged.** This slice modifies the implementation of `SessionConfig` ownership and adds tests + one new `session_error` variant; no `spec.md` / `data-model.md` / `contracts/*.hpp` of 005 is amended.

## Clarifications

### Session 2026-05-23

- Q: W-5 — `SessionConfig` lifetime mechanism (FR-001) — by-value (A) / `shared_ptr` (B) / PMR-arena (C) / `[[lifetimebound]]` annotation (D)? → A: **A — by-value member `SessionConfig cfg_;`**. The `Session` constructor copies the caller's `SessionConfig` into a member of the same type; the caller may freely drop their config after construction. No sharing across sessions; each `Session` owns its own snapshot. Multi-session sharing is therefore explicitly NOT a supported scenario; post-construction mutation of the caller-side config has no effect on a constructed `Session`.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - SessionConfig lifetime safety (Priority: P1)

An integrator constructs a `fixpp::session::Session` from a locally-scoped `SessionConfig`, runs the session, then lets the `SessionConfig` leave scope while the `Session` continues to operate (heartbeats, FSM transitions, admin emits). Today this is a stack-use-after-scope: every later access to `cfg_` reads dangling memory. The library MUST retain whatever view of the config it needs, so callers can construct configs as locals without ASan failures.

**Why this priority**: The defect is a memory-safety bug (UAF). It is the only P1 in the slice and the only one blocking removal of the existing ASan-only test skip in `tests/session/CMakeLists.txt` (added in PR #82 as the W-5 carry-forward). The other deferrals are coverage / witness improvements, not correctness bugs.

**Independent Test**: Build the `asan` preset. Run `session_coverage_adversarial` (currently `DISABLED TRUE` under `FIXPP_ENABLE_ASAN`). After this slice lands the skip is removed and the test passes clean under ASan with no `stack-use-after-scope` report.

**Acceptance Scenarios**:

1. **Given** a unit test that constructs `SessionConfig` as a local, passes it into `Session(...)`, and lets the config go out of scope, **When** the session continues to operate (FSM transitions, admin emits, `send(...)` calls), **Then** ASan reports no use-after-scope.
2. **Given** the existing `session_coverage_adversarial` test (14 adversarial-input scenarios), **When** built with `FIXPP_ENABLE_ASAN=ON`, **Then** the test runs and passes — the `set_tests_properties(... DISABLED TRUE)` block in `tests/session/CMakeLists.txt` is removed.
3. **Given** the by-value `SessionConfig cfg_;` mechanism (FR-001, resolved at /clarify), **When** measured against the 005 binding design, **Then** no design anchor is amended; the change is purely implementation-level.

---

### User Story 2 - FSM observability + error precision (Priority: P2)

A test author writing matrix coverage for the session FSM, and an integrator catching `Session::send` errors, both need clearer signals from the implementation. Today `LogonReceived` is a synchronous-transient state (the F1 fix in PR #82 made `NotConnected → LogonReceived → Active` fire in one tick from the acceptor path), so the META-attestation seam cannot directly observe the cell. And `Session::send` returns `session_invalid_logon` when the FSM is in any non-`Active` state, conflating two semantically distinct conditions.

**Why this priority**: P2. The matrix cell is reachable from production today (PR #82 closed RC#B), but the test seam to **observe** it is missing — auditors must infer from downstream behavior. The error variant reuse is a semantic near-fit, not a contract break — a dedicated variant cleans up caller diagnostics without changing the wire.

**Independent Test**: (a) Add a production-shaped test that drives the acceptor reply-Logon path and observes `LogonReceived` via the always-on `Session::fsm_visit_history()` ring-buffer accessor (research D-2). (b) Add a test that calls `Session::send` when the FSM is in `NotConnected`, `LogonSent`, or `Disconnected`, and asserts the new dedicated error variant rather than `session_invalid_logon`.

**Acceptance Scenarios**:

1. **Given** an acceptor receives a valid inbound Logon, **When** the FSM passes through `LogonReceived` on its way to `Active`, **Then** a test can observe the `LogonReceived` state via the new seam.
2. **Given** an integrator calls `Session::send(...)` while the FSM is not in `Active`, **When** the call returns, **Then** the error is a new `session_invalid_state_for_send` variant (or equivalent name decided at /plan), not `session_invalid_logon`.
3. **Given** the existing one `Session::send` site that returns `error::session_invalid_logon` (`src/session/session.cpp:1181` — the analyzer step verified only one site exists, not two as the early draft assumed), **When** the variant rename lands, **Then** any test that asserts against this exact variant at the send path is updated to assert the new variant; the FSM-side Logon-refusal tests that legitimately assert `session_invalid_logon` are NOT touched.

---

### User Story 3 - FSM matrix coverage (Priority: P2)

An auditor reading the 005 design wants one witness assertion per cell of the 6-state × N-event FSM matrix `[FIX-SL §4.10]`, so any regression in the FSM transition function trips CI immediately. Today the matrix is covered by happy-path tests + a handful of negatives + the META-attestation seam — exhaustive per-cell witnesses are missing. Additionally: admin builders read `effective_clock.now()` per message, but existing tests don't advance the clock between two emits, so the per-message-distinct `SendingTime` branch never fires. And the PR #82 round-2 RC#G fix gated admin emits on `assign_outbound` failure at all 8 sites, but the mixed-success-mode permutations at sites 1+2 (Reject-ok / Logout-fail and inverse) were not exhaustively tested.

**Why this priority**: P2. These are coverage gaps in code that is correct on the happy path. Risk = a future regression in an under-tested cell escapes review. Bundled together because they all live in `tests/session/` and ride along on existing fixtures.

**Independent Test**: Three independent ride-alongs. (a) A new test file with one assertion per FSM matrix cell. (b) An augmentation to an existing admin-builder test that calls `clock->advance(...)` between two emits and asserts distinct `SendingTime`. (c) Augmentations to existing admin-emit tests for sites 1+2 that exercise the four mixed-success-mode permutations.

**Acceptance Scenarios**:

1. **Given** the 005 FSM matrix `[FIX-SL §4.10]`, **When** the new per-cell witness file runs, **Then** every cell (6 states × N events) has at least one assertion verifying the expected transition or rejection.
2. **Given** the admin builders for Heartbeat / TestRequest / Logout / Reject, **When** a test calls `clock->advance(...)` between two emits, **Then** the resulting messages carry distinct `SendingTime` values.
3. **Given** an admin emit site that calls both Reject and Logout in the same path (sites 1+2 per the round-2 RC#G locations), **When** the test exercises (Reject-ok, Logout-ok), (Reject-fail, Logout-skip), (Reject-ok, Logout-fail), (Reject-fail, Logout-fail), **Then** the gated-emit behavior matches the documented contract in each cell.

---

### User Story 4 - Initiator transport-throw witness (Priority: P3)

A team auditor wants symmetric coverage between initiator and acceptor open-path transport-throw scenarios. PR #82 round 1 added the acceptor-side witness; the initiator side is missing.

**Why this priority**: P3 polish — `[const §IX.1]` does not require this witness (the production code path is exercised by other tests). Symmetry only.

**Independent Test**: Add one RED test that exercises `transport.send` throwing during initiator Logon emit on `Session::open()` and asserts the documented contract (the throw is converted to the documented error return; FSM ends in the documented state).

**Acceptance Scenarios**:

1. **Given** an initiator session calls `Session::open()`, **When** the transport throws during the outbound Logon emit, **Then** the call returns the documented error and the FSM is in the documented state per the 005 design.

---

### Edge Cases

- **Multiple sessions sharing one `SessionConfig`**: under the by-value mechanism (FR-001), each `Session` ctor takes its own copy of the caller's `SessionConfig`. Multi-session sharing is not a supported scenario — callers wanting two sessions with the same configuration construct two configs (or copy one) and pass each into the respective `Session` ctor; the two `Session`s then evolve independently.
- **`SessionConfig` mutated after `Session` construction**: has no effect on the constructed `Session` (the `Session` holds its own copy). The caller may freely reuse, mutate, or drop their `SessionConfig` after the ctor returns.
- **`effective_clock.now()` returning the same value across two admin emits** (e.g., a stopped manual clock): the per-message-distinct-`SendingTime` test seam must NOT promise distinct `SendingTime` in this case — it asserts only that the branch *can* fire when the clock advances.
- **FSM matrix cells that are unreachable by design** (e.g., events forbidden in `Disconnected`): the per-cell witness must assert the rejection / no-op, not silently skip the cell. Cell count includes negatives.
- **Error variant rename breaking external callers**: this library has no external consumers yet — the variant change is internal. If that changes before merge, the rename becomes a wire-API concern and needs a /clarify on rollout.

## Requirements *(mandatory)*

### Functional Requirements

**SessionConfig lifetime (W-5):**

- **FR-001**: `Session` MUST store its `SessionConfig` by value as a non-const member (`SessionConfig cfg_;`) populated by copy at construction time. The caller's `SessionConfig` may go out of scope or be mutated after the `Session` is constructed without affecting the `Session`'s behavior. The declaration at `include/fixpp/session/session.hpp:312` MUST change from `const SessionConfig& cfg_;` to `SessionConfig cfg_;`. The member is NOT declared `const` — the contract "no code mutates `cfg_` post-ctor" is convention-enforced, not type-enforced, to preserve future refactoring flexibility (e.g. config-hot-reload follow-on slices).
  - **FR-001a (W-5 enabler)**: To make `SessionConfig` copy-constructible, the `SessionConfig::store_factory` member at `include/fixpp/session/session_config.hpp:127` MUST change from `std::unique_ptr<MessageStoreFactory>` to `std::shared_ptr<MessageStoreFactory>`. This is a small 005 design amendment driven by the W-5 fix (the binding 005 design used `unique_ptr` for polymorphic ownership through indirection, NOT to forbid sharing; `MessageStoreFactory` is a stateless interface — only one virtual method `make()` returning a freshly-minted unique `MessageStore` — so factory sharing across Sessions is meaningful and the per-Session uniqueness invariant (one MessageStore per Session) is unaffected). The amendment is documented in the Gate A inheritance addendum (T027b). Call sites that assign `cfg.store_factory = std::make_unique<...>(...)` continue to work via the implicit `unique_ptr<T>&& → shared_ptr<U>` move-conversion.
- **FR-002**: All FSM sites that read `cfg_.*` (executor override, role, BeginString, heartbeat interval, comp IDs, send/recv reset policy, drain policy, persistence, etc.) MUST continue to read consistent values for the lifetime of the `Session`.
- **FR-003**: The `set_tests_properties(session_coverage_adversarial PROPERTIES DISABLED TRUE)` block added in PR #82 under `if(FIXPP_ENABLE_ASAN)` (`tests/session/CMakeLists.txt`) MUST be removed; the test MUST run and pass under ASan.

**FSM observability (F-04) + error precision (F-07 + E1):**

- **FR-004**: A test seam MUST exist that lets a production-shaped test directly observe the `LogonReceived` FSM state during the synchronous-transient acceptor reply-Logon transition. The seam mechanism is the always-on `Session::fsm_visit_history() const noexcept` accessor returning `std::span<const fsm_state>` over a 16-entry ring buffer (research D-2); it MUST be populated synchronously on every `fsm_state_` transition. Production-default behavior is unchanged: the ring buffer is always written but never read in production code; there is no `#ifdef`-gated divergence between production and test builds.
- **FR-005**: `Session::send(...)` MUST return the dedicated `session_invalid_state_for_send` error variant (research D-3, slot 77) when the FSM is not in `Active`, replacing the current reuse of `session_invalid_logon` at `src/session/session.cpp:1181` (one site, /speckit-analyze verified). Any test that asserts against the specific variant at this site MUST be updated; /speckit-analyze verified the existing tests assert `EXPECT_FALSE(has_value())` only at this site, so the expected update count is 0 — the new `session_send_invalid_state_test.cpp` (T013) provides AC3 coverage going forward.

**FSM matrix coverage (F-06) + admin builder coverage (F-05) + RC#G mixed-path (PR #82 round-2 carry-forward):**

- **FR-006**: A new per-cell witness test file MUST cover every cell of the 6-state × N-event FSM matrix `[FIX-SL §4.10]` with at least one assertion per cell (positive transition OR explicit rejection / no-op).
- **FR-007**: An admin-builder coverage test MUST call `clock->advance(...)` between two emits across Heartbeat / TestRequest / Logout / Reject and assert that the two resulting messages carry distinct `SendingTime` values, exercising the per-message-distinct-`SendingTime` branch.
- **FR-008**: At admin emit sites 1+2 (per the PR #82 round-2 RC#G fix locations), tests MUST exercise the four mixed-success-mode permutations (Reject-ok/Logout-ok, Reject-fail/Logout-skip, Reject-ok/Logout-fail, Reject-fail/Logout-fail) and assert the gated-emit behavior matches the documented contract.

**Initiator transport-throw witness (F-11):**

- **FR-009**: A RED test MUST exercise `transport.send` throwing during the initiator Logon emit on `Session::open()` and assert the documented error return and FSM end-state per the 005 design (symmetric to the acceptor witness landed in PR #82 round 1).

**Process / hygiene:**

- **FR-010**: This slice MUST NOT amend `005/spec.md`, `005/data-model.md`, or `005/contracts/*.hpp` — the 005 design is binding and unchanged.
- **FR-011**: `/speckit-verify` MUST be re-run as part of this slice. The verify record SHOULD show coverage envelope on `session.cpp` / `session.hpp` rising organically as new tests are added; W-1..W-4 (the 005 Codecov DA/BRDA carry-forwards) MUST either clear or be re-waived per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` + PR #73 precedent. No dedicated tasks for W-1..W-4.
- **FR-012**: The Gate A decision for 010 MUST be one of (i) Gate A waived (no new design anchors; the W-5 candidate is an implementation choice settled at /clarify), or (ii) Gate A inherited from 005 with a 010-specific addendum recording the W-5 implementation choice. Decision deferred to /plan.

### Key Entities *(include if feature involves data)*

- **`SessionConfig`**: User-facing configuration value type. Pre-010: held as `const SessionConfig&` by `Session` (caller owns; UAF risk). Post-010: copied by value into `Session` at construction; caller's `SessionConfig` lifetime is independent of the `Session`.
- **`Session`**: FSM-bearing session object, today holds `const SessionConfig& cfg_`. Post-010, holds `SessionConfig cfg_` (or `const SessionConfig cfg_`) as a by-value member.
- **`session_error` enum**: existing error code enum (or `std::error_code` category — verify at /plan). Post-010, gains one new variant `session_invalid_state_for_send` (final name at /plan).
- **FSM state set**: `[FIX-SL §4.10]` 6 states (NotConnected / LogonSent / LogonReceived / Active / LogoutSent / Disconnected — exact set from 005 `data-model.md`, confirmed against `include/fixpp/session/session_fsm.hpp:30-46`). Unchanged by 010; new test seam exposes the synchronous-transient `LogonReceived` state.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: ASan runs `session_coverage_adversarial` with the existing 14 adversarial-input scenarios, no `stack-use-after-scope` report, no test skip under `FIXPP_ENABLE_ASAN`.
- **SC-002**: The new per-cell FSM matrix witness file has one passing assertion per externally-injectable cell of the 6-state × 15-event matrix `[FIX-SL §4.10]`. **Coverage accounting** (W3.2 / /simplify B-2+C-3 correction): 49 tests = 44 per-cell witnesses across 5 rows (NotConnected×11 + LogonSent×12 + Active×12 + LogoutSent×4 + Disconnected×5) + 2 LogonReceived-row observability tests (the LogonReceived row is synchronous-transient by 010 D-2 design — per-cell injection is not externally addressable because the state appears for one tick during the acceptor reply-Logon path and transitions to Active before any external observer can act; FR-004 observability via `Session::fsm_visit_history()` subsumes the row) + 3 visit-history housekeeping tests. **W3.3-final / F4 (codex + QuickFIX-cpp + QuickFIX/J survey 2026-05-23):** the Active×DupLogon and Active×OOSA(RR/SeqReset) cells previously asserted impl-reality (stay-Active, 0 Rejects) as a "known 005 spec-vs-impl gap"; that gap is now closed in-slice — `is_session_admin` at `src/session/session.cpp` excludes `"A"`, `"2"`, `"4"` per 005 FR-017 "never silent no-op", routing them through the Reject branch. Tests now assert exactly 1 Reject(35=3) per cell. **TODO(2e-recovery):** when the deferred session-recovery feature lands, the RR and SeqReset cells UPGRADE from Reject → Process (gap-fill via the message store), matching QuickFIX-cpp `Session::nextResendRequest` and QuickFIX/J `Session.nextResendRequest`; the dup-Logon cell stays as Reject per 005's intentional defensive divergence from QuickFIX refresh-on-dup-Logon convention.
- **SC-003**: Coverage envelope on `src/session/session.cpp` and `include/fixpp/session/session.hpp` (DA / BRDA per `[const §IX.1]`) rises measurably vs the 009 baseline (concrete deltas computed by `/speckit-verify` and recorded in `.specify/decisions/010-session-cfg-lifetime-verify.md`).
- **SC-004**: `Session::send` returning when the FSM is not `Active` produces a distinct error variant (`session_invalid_state_for_send` or equivalent) — observable in tests and in caller diagnostics.
- **SC-005**: The four mixed-success-mode permutations at admin emit sites 1+2 each have one passing assertion; the gated-emit contract is exhaustively witnessed.
- **SC-006**: The Heartbeat / TestRequest / Logout / Reject admin builders each have one test that advances the clock between two emits and asserts distinct `SendingTime` per message.
- **SC-007**: Initiator `Session::open()` with a throwing `transport.send` returns the documented error and ends in the documented FSM state — symmetric to the acceptor witness landed in PR #82 round 1.
- **SC-008**: Build under all sanitizer presets (ASan, UBSan, TSan) passes `ctest` clean.
- **SC-009**: PR #82 W-5 waiver row in `.specify/decisions/009-session-fsm-finalize-gateb.md` is annotated CLOSED with a back-link to the 010 PR.

## Assumptions

- **Binding 005 design unchanged.** The 010 slice does not amend `specs/005-session-establishment-fsm/spec.md`, `data-model.md`, or `contracts/*.hpp`. Gate A may waive (single /clarify decision drives implementation; no new design anchors) — final call at /plan.
- **Gate A inheritance from 005.** Per `library/CLAUDE.md` 009 precedent, when no new design anchor is introduced Gate A may carry forward from 005 with a 010-specific addendum. Identical pattern proposed here pending /plan decision.
- **W-1..W-4 carry-forwards auto-revisit at /speckit-verify.** No dedicated tasks; the verify record either clears them organically as the 010 tests raise coverage on `session.cpp`/`session.hpp`, or re-waives them with rationale per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` + PR #73 precedent.
- **No external consumers of the `session_error` enum yet.** Renaming the `session_invalid_logon` reuse at `src/session/session.cpp:1181` (one site, confirmed by /speckit-analyze) to `session_invalid_state_for_send` is an internal change. If this assumption breaks before merge, FR-005 escalates to a wire-API concern needing /clarify.
- **Reference engines (QuickFIX-cpp, Fix8) are not consulted for 010** — the slice is a pure internal refactor + test coverage improvement; the interop matrix work belongs to the separate per-release interop gate.
- **Submodule-scoped.** All changes land in `research/G19-fix-fpml-iso20022/library/` (the Spec-Kit submodule). The parent repo only sees a submodule pointer bump on merge.
- **Branch base.** 010 is rooted on the post-PR-#82-merge `main` of the library submodule (commit `ba2222d`). No 009-branch carry-overs.

## Normative References

Per `[const §VI.5]` (applies per-artifact). All entries below are referenced inline in the FRs and acceptance scenarios above; this section consolidates them.

**FIX specification:**
- `[FIX-SL §4.10]` *FIX Session Layer §4.10 — State transitions* — binding for FR-004 / FR-006 (matrix observability + per-cell witness).
- `[FIX-SL §4.5.4]` *FIX Session Layer §4.5.4 — Rejecting invalid messages* — binding for FR-005 (the `session_invalid_state_for_send` variant is the outbound analogue for FSM-state-mismatch rejects).
- `[FIX-SL §4.3]` *FIX Session Layer §4.3 — Establishing a FIX connection* — binding for FR-009 (initiator open() transport-throw witness on Logon emit).
- `[FIX-TC]` *FIX conformance test corpus (TC-001..TC-017 in-scope subset)* — inherited from 005 / 009 unchanged; not re-asserted by 010.

**Project Phase-2 design anchors:**
- `[2d §4.5]` *2d-threading §4.5 — SessionConfig fields* — binding for FR-001 / FR-002 (the by-value `cfg_` member preserves the per-field semantics defined here).
- `[2e §4.1]` *2e-msgstore §4.1 — MessageStore consumed interface* — inherited from 005; no 010 change.

**Source contracts (binding, 005/009-owned, unchanged):**
- `005/contracts/session.hpp` — `Session` public API (open / send / close / state) — binding shape; 010 adds one observation-only method (`fsm_visit_history()`) without altering the existing surface.
- `005/contracts/session_fsm.hpp` (= `include/fixpp/session/session_fsm.hpp:30-46`) — `fsm_state` enum (6 states) — binding for FR-004 / FR-006.
- `009/contracts/session_role.hpp` — `session_role` enum + `SessionConfig::role` field — binding for FR-001 (the by-value member preserves the role field intact).

**Constitution articles cited inline in plan.md Constitution Check** (Article roman + section arabic): `[const §VI.5]` (this section), `[const §VIII.5]` / `[const §XV.1]` (alloc discipline), `[const §IX.1]` (coverage gate), `[const §IX.2]` (sanitizers), `[const §X.4]` (bounded error variants — slot 77), `[const §XI.4]` (per-session strand), `[const §XVII.1]` (Gate A trigger — inherited from 005 per plan), `[const §XVII.8]` (verify mandatory post-implement).
