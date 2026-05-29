# Reconnect / Session-FSM Lifecycle Requirements Checklist: Live Transport Wiring (014-transport-active-binding)

**Purpose**: Validate that the requirements for the realized initiator reconnect loop — triggering conditions, backoff/cap, reason-agnostic retry-to-cap, cancellation/release, success handoff, and terminal disposition — are complete, unambiguous, consistent, and measurable BEFORE implementation. Tests the requirements, not the FSM behaviour.
**Created**: 2026-05-29
**Feature**: [spec.md](../spec.md) · Gate-A trigger: Session FSM

## Requirement Completeness

- [x] CHK001 Are the conditions that trigger a reconnect attempt enumerated completely (connection drop, handshake failure, transport read/write error)? [Completeness, Spec §FR-001] — PASS: FR-001 says "When an initiator session loses its transport (connection drop, handshake failure, or transport read/write error)." All three trigger classes are enumerated. The reconnect attempt concept in Key Entities further defines the scope.
- [x] CHK002 Does FR-003's "every failed reconnect attempt" explicitly include a **factory `make()` failure** as a counted attempt, or only connect/handshake/authorization? [Completeness, Gap, Spec §FR-003] — SPEC-FIXED: The original FR-003 enumerated "connect or handshake failure" as the reason-agnostic comparison but did not explicitly list factory `make()` failure as a counted attempt. (Contracts C1 and tasks T007 included it, but the normative FR did not.) FR-003 in spec.md was edited to explicitly enumerate: "every failure cause — factory `make()` failure, connection failure, TLS handshake failure, or authorization failure — MUST count as exactly one attempt." Affected: `spec.md:§FR-003`.
- [x] CHK003 Is the requirement that NO production code path may mint a transport and discard it (full stub removal) stated as a verifiable invariant? [Completeness, Spec §FR-005] — PASS: FR-005 explicitly states "The 013 stubbed reconnect-driver hooks MUST be fully realized — no production code path may remain that creates a transport and discards it." This is a verifiable invariant (grep for the stub pattern, and the shipped-reality table documents the exact location at `reconnect_fsm.cpp:53-61`).
- [x] CHK004 Is the meaning of "session resumes" at the 014 boundary specified (produce authorized live transport + re-drive Logon to Active), with the continuous read-pump explicitly carved to 015? [Completeness, Spec §FR-001/§Assumptions/§Out of Scope] — PASS: FR-001 says "hands a live transport to the session." The plan (§Summary item 1) specifies "co_return`s `expected_t<void>{}` and hands `{transport, handshake_result, bound_principal}` to the owning Session through the private `Session::install_reconnected_transport`"; the spec Assumptions say "continuous read-pump / public connect-loop / registry are 015." The Out of Scope section explicitly lists "the acceptor accept→Session-create→byte-feed production path." The 014 boundary is clear.

## Requirement Clarity

- [x] CHK005 Is "one attempt" defined unambiguously so that every failure cause (make/connect/handshake/authorize) counts identically (reason-agnostic)? [Clarity, Spec §FR-003] — PASS: After the SPEC-FIXED to FR-003 (CHK002), the text now explicitly says "every failure cause — factory `make()` failure, connection failure, TLS handshake failure, or authorization failure — MUST count as exactly one attempt." The Key Entities section also defines "Reconnect attempt" as "one governed cycle…every failure cause counts as one attempt." No ambiguity.
- [x] CHK006 Is the backoff schedule + attempt cap clearly delegated to 012's `ReconnectPolicy` (consumed, not redefined) rather than re-specified? [Clarity, Spec §FR-002] — PASS: FR-002 says "The reconnect loop MUST honour the configured `ReconnectPolicy` backoff schedule and attempt cap unchanged (it consumes 012's policy; it does not redefine it)." The Normative References cite 012's `reconnect_policy.hpp`. The plan's Technical Context describes `ReconnectPolicy` fields as "consumed, not redefined." Clear.
- [x] CHK007 Is the terminal outcome at cap exhaustion named precisely (the FSM's terminal disconnected outcome / no infinite retry)? [Clarity, Spec §FR-003] — PASS: FR-003 says "until the cap is reached, at which point the session transitions to the FSM's terminal disconnected outcome (no infinite retry)." The FSM's terminal `Disconnected` state is the named terminal outcome; `fsm_state::Disconnected` is the shipped type. No ambiguity.

## Requirement Consistency

- [x] CHK008 Is the reconnect-path authorize-failure disposition (count one attempt + retry-to-cap, NOT terminal Disconnected) stated as an *intentional* divergence from the inherited 013 open-Logon terminal disposition? [Consistency, Spec §FR-007/§Clarifications Q1] — PASS: FR-007 explicitly states "The *FSM disposition* differs from 013's open-Logon path: on 013's open path an authorize failure drives the terminal `Disconnected` transition (`session.cpp:1004-1005`/`:1799-1800`); on the live **reconnect** path it does **NOT** drive terminal Disconnected — it emits the inherited event/code, releases the in-flight transport, counts as one reconnect attempt, and loops per the backoff schedule (FR-003). Only loop-exhaustion at the cap transitions to terminal Disconnected." The Clarifications Q1 records the design decision. Intentional divergence is clearly stated.
- [x] CHK009 Are cancellation requirements consistent across both `co_await` regions — the in-flight handshake AND the inter-attempt backoff sleep? [Consistency, Spec §FR-004/§Edge Cases] — PASS: FR-004 says "a stop / total-teardown request MUST abort an in-flight attempt promptly and release any partially-constructed transport (no leak, no orphaned socket)." The plan §Constraints states "connect/handshake/backoff sleep all `co_await asio::this_coro::cancellation_state`." The Edge Cases section covers "Cancellation mid-handshake." The plan's Constitution Check notes `[const §XI.2]` end-to-end cancellation with the `enable_total_cancellation()` requirement. Both regions are covered.

## Acceptance Criteria Quality / Measurability

- [x] CHK010 Is the consecutive-attempt count **N** for the no-leak success criterion quantified (SC-004 currently says "across N … attempts" without a minimum)? [Measurability, Gap, Spec §SC-004] — SPEC-FIXED: SC-004 originally said "across N consecutive failed-then-succeeded and cancelled-mid-handshake attempts" without a lower bound on N. Tasks T008 specified N ≥ 3 but the spec SC did not. SC-004 in spec.md was edited to "Across N ≥ 3 consecutive…" to bind the minimum. Affected: `spec.md:§SC-004`.
- [x] CHK011 Is "100% of reachable cases" in SC-001 bounded — is "reachable" defined so the criterion is objectively testable? [Measurability, Ambiguity, Spec §SC-001] — SPEC-FIXED: SC-001 originally said "100% of reachable cases" without defining "reachable." Added a definition: "cases reachable by the loopback-TLS integration test (i.e., where the peer becomes reachable and the handshake succeeds within the configured cap)," with the boundary note that cap-exceeded → terminal Disconnected is SC-002. Affected: `spec.md:§SC-001`.
- [x] CHK012 Is SC-002 ("consumes exactly one attempt; terminates at the cap") expressed against an observable counter / terminal state? [Measurability, Spec §SC-002] — PASS: SC-002 says "Every failed reconnect attempt consumes exactly one attempt; the loop terminates at the configured cap; no infinite retry." The observable counter is the `ReconnectPolicy` attempt count (tracked by the FSM); the terminal state is `fsm_state::Disconnected` (emitted as an observable FSM transition). Tasks T007 maps this to `test_reconnect_backoff_cap.cpp` with deterministic `mock_clock`. Measurable.

## Scenario & Edge-Case Coverage

- [x] CHK013 Are requirements defined for cancellation mid-handshake — prompt abort + release of the partially-constructed transport, no leak / no orphaned socket? [Coverage, Spec §FR-004/§Edge Cases] — PASS: FR-004 says "MUST abort an in-flight attempt promptly and release any partially-constructed transport (no leak, no orphaned socket)." US1 AC3 and the Edge Cases section ("Cancellation mid-handshake: a stop / total-teardown request during the handshake aborts the attempt and releases the in-flight transport without a leak (sanitizer-verified)") cover this. SC-004 is the sanitizer witness.
- [x] CHK014 Is reconnect exclusivity (no new attempt starts while a prior attempt's transport is still in flight) specified as a requirement? [Coverage, Spec §Edge Cases] — PASS: The Edge Cases section says "Reconnect exclusivity: a new attempt does not start while a prior attempt's transport is still in flight." Data-model E-1 states invariant (I-2): "at most one in-flight attempt's transport at a time — guaranteed by the single coroutine." This is specified as both an edge case and an invariant; it is verifiable (the single-coroutine structure enforces it structurally).
- [x] CHK015 Are requirements defined for the persistently-failing-peer flow terminating at the cap (no infinite retry)? [Coverage, Spec §FR-003/US1 AC2] — PASS: FR-003 says "retried per the backoff schedule until the cap is reached, at which point the session transitions to the FSM's terminal disconnected outcome (no infinite retry)." US1 AC2 says "each failed attempt consumes one attempt per the backoff schedule and the loop stops at the configured cap." Edge Cases: "Cap exhausted: the loop terminates at the configured cap in the FSM's terminal disconnected outcome."

## Dependencies & Assumptions

- [x] CHK016 Is the assumption "reconnect is initiator-side; acceptor sessions re-accept (acceptor live path → 015)" documented with its 014 boundary clear? [Assumption, Spec §Assumptions] — PASS: The spec Assumptions section explicitly says "Reconnect is initiator-side — acceptor sessions re-accept rather than driving a reconnect loop; the acceptor live path is part of the 015 engine." The 014/015 boundary is documented in Clarifications Q2 and the Out of Scope section.
- [x] CHK017 Is the dependency on 012's `ReconnectPolicy` (schedule/jitter/`max_attempts`/`delay_for_attempt(n)`) and on ASIO native total-cancellation explicitly cited? [Dependency, Spec §FR-002/§FR-004/§Dependencies] — PASS: FR-002 cites "012's policy" by name. The Dependencies section lists "012 transport surface (`Transport` / `TlsTransport` / `TransportFactory` / `ReconnectPolicy` / `handshake_result.peer_id`)." FR-004 and the plan §Constraints cite `[const §XI.2]` ASIO native cancellation. The Normative References section cites `012 include/fixpp/transport/{tls_transport,transport_factory,reconnect_policy}.hpp`. Both dependencies are explicit.

## Notes

- Check items off as completed: `[x]`; record disposition (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline for the step-9 audit.
- CHK002 and CHK010 are the highest-value gaps (factory-make counting; SC-004 N) — resolve in spec or disposition explicitly.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 14 |
| SPEC-FIXED | 3 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **17** |

### SPEC-FIXED items
- CHK002 — FR-003 in `spec.md` §FR-003 did not explicitly enumerate factory `make()` failure as a counted attempt; edited to list "factory `make()` failure, connection failure, TLS handshake failure, or authorization failure" explicitly; affected: `spec.md:§FR-003`.
- CHK010 — SC-004 in `spec.md` §SC-004 said "across N consecutive" without binding N; edited to "N ≥ 3" matching tasks T008; affected: `spec.md:§SC-004`.
- CHK011 — SC-001 in `spec.md` §SC-001 said "100% of reachable cases" without defining "reachable"; added definition bounding it to the loopback-TLS integration test fixture and cross-referencing SC-002 for the cap-exceeded case; affected: `spec.md:§SC-001`.

### DD-DECIDED items
*(none)*

### WAIVED items
*(none)*

Anchors spot-verified:
- `[const §XI.2]` — resolves in constitution.md Article XI §2 (ASIO native cancellation slots end-to-end).
- `[FIX-SL §4.10]` — external FIX standard; not spot-verified locally.
- `012 include/fixpp/transport/reconnect_policy.hpp` — verified `ReconnectPolicy` exists in the merged tree (plan shipped-reality table; `reconnect_policy.hpp:30`).
