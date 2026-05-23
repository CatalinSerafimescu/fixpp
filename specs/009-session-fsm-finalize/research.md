# Phase 0 — Research

**Feature:** 009-session-fsm-finalize
**Date:** 2026-05-22
**Status:** complete — zero NEEDS CLARIFICATION unresolved.

## Inheritance from 005

Every design decision binding on this slice is already recorded in [`005-session-establishment-fsm/research.md`](../005-session-establishment-fsm/research.md) D-1..D-13. The 005 Gate A (converged round 3, 2026-05-18, record `library/.specify/decisions/005-session-establishment-fsm-gatea.md`) reviewed those decisions; this slice does not amend any of them.

In particular:

- **D-1** (seqnum_t handoff from 2e to 005) — unchanged; FR-009 of 005 still binds.
- **D-2** (6-state `[FIX-SL §4.10]` FSM, too-high MsgSeqNum = session-fatal, no `RecoveryPending`) — unchanged; the transition matrix in `005/data-model.md:19` is the binding oracle for FR-006 (refused-Logon → Disconnected) of this slice.
- **D-3** (folded `core/` time-helper #4 + Clarification Q3 stale-SendingTime → Reject(10)→Logout→Disconnected; LogonSent special: logout-with-error) — unchanged; the LogonSent-special rule is the binding oracle for FR-009 of this slice.
- **D-10** (Q2 TC split + too-high deferral against the QuickFIX/J acceptance oracle) — unchanged.
- **D-12** (fuzzing N/A; abidiff N/A) — unchanged for this slice (still no parser-touching code, still no C-ABI surface).

## D-1 — `session_role` enum placement (RC#2 / FR-004)

**Decision:** add `enum class session_role : std::uint8_t { initiator = 0, acceptor = 1 };` to `include/fixpp/session/session_config.hpp` and a `session_role role = session_role::initiator;` field on `SessionConfig` (defaulting to `initiator` preserves existing test behavior). `Session::open` branches on `cfg_.role`.

**Rationale:** Opus round-1 triage Fix Queue RC#2 explored two options — (a) `open_initiator()` vs `open_acceptor()` API split, or (b) a `role` field on `SessionConfig`. The triage recommended (b) and the slice adopts it because: (1) `SessionConfig` is already the configuration surface every integrator builds before calling `Session::open` — adding a config field matches the existing mental model; (2) splitting the open API into two named entry points would force every existing test + integration to choose, breaking compile-compatibility on the 25 inherited commits; (3) the role is a property of the *session configuration*, not a verb of the open *call* — there is no scenario where "this session is sometimes an initiator and sometimes an acceptor" is meaningful. The default `initiator` value preserves the existing tests that call `make_initiator_cfg` without an explicit role.

**Alternatives considered:** (a) `open_initiator()` / `open_acceptor()` split — rejected per above. (c) Inferring role from CompID configuration — rejected as too implicit (CompID pairs are direction-agnostic; the spec is explicit that role is a separate degree of freedom).

**Binding citation:** `005/spec.md` FR-002 ("Logon initiation/acceptance both roles"), `005/data-model.md:19` matrix row `NotConnected × inbound Logon (accepted) → LogonReceived`, Opus triage `opus_pr81_1_triage.md` RC#2.

## D-2 — `SeqnumManager::drain` failure policy in `Session::close` (RC#7 / FR-011)

**Decision:** `Session::close` calls `co_await seqnum_mgr_.drain()` during phase 2 (after `root_cancel_.emit(...)` and `trace_slot_.clear()`, before `state_ = closed_drained`). The drain result is consumed but a drain failure does NOT abort the close path — the failure is logged via the existing session-error callback and `close()` still returns successfully. The Session destructor remains safe in all cases (drained-with-failure leaves the mutex in a no-holders-no-waiters state, so the destructor's terminate-precondition is not triggered).

**Rationale:** Opus round-1 triage Fix Queue RC#7 surfaced this as `(void)drain_r; // drain failures: logged-then-proceed`. The triage's reasoning is binding: the documented `async_mutex` teardown precondition (`include/fixpp/core/sync/async_mutex.hpp:155-160`) prohibits destruction with *holders or waiters present*. A drain whose result is failure (e.g. cancellation propagated through pending acquirers) leaves the mutex in a no-holders state — the precondition is satisfied. Aborting close on drain failure would leave the session in a torn-down-but-not-quite state, which is worse for the integrator's recovery logic. The session-error callback is the existing surfacing mechanism (FR-015 from 005).

**Alternatives considered:** (a) abort close on drain failure — rejected per above. (b) Force-drain via `cancel_and_drain()` with a stricter contract — already the underlying primitive Opus references; the visible contract on `SeqnumManager::drain()` already calls into it.

**Binding citation:** `005` `include/fixpp/session/seqnum_manager.hpp:50-53,96-100` (documented drain lifecycle), `include/fixpp/core/sync/async_mutex.hpp:155-160,683-690` (teardown precondition), Opus triage `opus_pr81_1_triage.md` RC#7.

## D-3 — Per-session TestRequest ID counter overflow policy (RC#6 / FR-010)

**Decision:** the TestRequest ID counter is a `std::uint32_t next_test_request_id_ = 0;` member of `Session`. Pre-increment (`++next_test_request_id_`) is used so the first emitted TestReqID is `1`. **Wrap-around at `UINT32_MAX` is acceptable** — within-session uniqueness over a single Session's lifetime is the contract; cross-restart uniqueness is not required, and the wrap-around point is unreachable in any realistic single-session lifetime (`UINT32_MAX ≈ 4.3 billion` TestRequests at HeartBtInt=30s = ~4100 years per session).

**Rationale:** Opus round-1 triage Fix Queue RC#6 was specific about per-session placement but did not constrain the overflow policy. The `[FIX-SL §4.5]` TestReqID spec does not mandate cross-restart uniqueness — it only requires that the responding peer can echo the value back so the requester can correlate the response. Wrap-around within a session that lives 4100+ years is a theoretical concern only; introducing atomic-counter semantics or `std::uint64_t` would be over-engineering per spec.md Assumption "no new contracts" (the slice's discipline).

**Alternatives considered:** (a) `std::uint64_t` for cosmetic future-proofing — rejected (no contract requires it; the existing `uint32_t` matches the prior `static` variable's type). (b) UUID / random-seeded IDs — rejected (echo correlation works just as well with sequential, and sequential is debug-friendlier when reading a TSan / trace log).

**Binding citation:** `005/contracts/admin_messages.hpp` (TestRequest semantics), `[FIX-SL §4.5]` (TestReqID echo correlation), Opus triage `opus_pr81_1_triage.md` RC#6.

## Audit verdict

No NEEDS CLARIFICATION markers remain. The three micro-decisions above are recorded for traceability — none represents a design choice that perturbs the 005 Gate-A-converged design. Inheritance from 005 covers the remaining design space.
