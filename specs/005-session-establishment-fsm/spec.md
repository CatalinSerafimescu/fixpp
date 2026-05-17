# Feature Specification: Session Establishment & FSM Core

**Feature Branch**: `005-session-establishment-fsm`
**Created**: 2026-05-17
**Status**: Draft
**Input**: User description: "session establishment and FSM core — the fixpp::session module's connection-lifecycle slice. Implement the FIX session state machine (S-008) and the admin messages that drive it: Logon 35=A (S-001), Logout 35=5 (S-002), Heartbeat 35=0 (S-003), Test Request 35=1 (S-004), session-level Reject 35=3 (S-007). Include MsgSeqNum(34) sequence-number management (S-009); HeartBtInt(108) negotiation (S-015); SenderCompID(49)/TargetCompID(56) validation (S-016); BeginString(8) FIX.4.x vs FIXT.1.1 version gating (S-020). Fold in the deferred core/ time-helper surface for SendingTime(52) via the core::Clock seam (S-019, NFR-015). Anchors: .specify/2e-msgstore.md v0.4, 2d-threading.md, 2f-async-mutex.md. Deferred: Resend/SequenceReset/gap-fill, MessageStore impls, PossDup, Reset*/RefreshOnLogon, auth, DefaultApplVerID."

> **Authority anchors:** This is the **Phase-4 session-module spec** that several signed-off design docs explicitly forward-reference. Where this spec and an anchor disagree, the anchor wins; an inconsistency is a defect in this spec. Anchors: `.specify/2e-msgstore.md` **v0.4** (the FSM *consumes* the `MessageStore` seam — store impls and gap-fill are NOT in this feature; `seqnum_t` ownership is re-pointed **to this spec**, §3.1 / §10 Q9), `.specify/2d-threading.md` **v0.4** (the `fixpp::core::Clock` seam / NFR-015 — heartbeat, TestRequest, SendingTime, graceful-close timeout read `effective_clock`; coroutine + ASIO-cancellation composition; per-session strand), `.specify/2f-async-mutex.md` (per-session async mutex), and the **FIX Session Layer** standard (`[FIX-SL]`) + **FIX Session Layer Test Cases** (`[FIX-TC]`, conformance corpus TC-001..TC-017, `[const §V.5]`). Catalogue rows owned (in part): **S-001, S-002, S-003, S-004, S-007, S-008, S-009, S-015, S-016, S-019, S-020**, and the **`core/` time-helper surface row #4** (folded in per the 2026-05-17 carry-in resolution — closes `core/` module exit). First feature of the `session/` module.

## Clarifications

### Session 2026-05-17

- Q: When an inbound message arrives with `MsgSeqNum` **higher than expected** (a sequence gap), but ResendRequest / SequenceReset / gap-fill recovery is **explicitly deferred** to a later session feature, what MUST this feature do at the gap? → A: **Option A** — detect the gap, surface it via a documented callback/error, and transition the FSM into an **explicit `RecoveryPending` state** where the out-of-sequence message is held (not processed, not counted) and the session is **not** disconnected; this feature does **not** issue ResendRequest/SequenceReset — the later recovery feature wires only the transitions *out* of `RecoveryPending`. Grounded in the OSS reference survey (2026-05-17, `reference-engines/` @ pinned tags): all 3 engines hold/queue on a gap and none treats it as fatal or advances past it; fix8 already models this as a distinct state (`st_resend_request_sent`), confirming an explicit state is a clean phased-delivery seam consistent with `[FIX-SL §4.8.2]`.
- Q: The conformance corpus `[FIX-TC]` TC-001..TC-017 spans cases that require ResendRequest / SequenceReset / gap-fill (deferred here). Which subset MUST ship green in *this* feature's PR vs. be recorded as deferred-with-traceability? → A: **Option A** — capability-partitioned subset. **In scope (must ship green):** every TC case exercising session establishment, liveness (heartbeat / test request), logout, session-level reject, in-sequence + MsgSeqNum-too-low handling, and SendingTime / CompID / BeginString-version gating. **Deferred-with-traceability** (recorded in `coverage-index.md`, pointing at the named later recovery feature): every TC case requiring ResendRequest / SequenceReset / gap-fill / PossDup-resend / store-recovery. This satisfies `[const §V.5]` via the constitution's explicit-scoped-subset allowance. The concrete TC-### → in/deferred split is fixed at `/speckit-plan` against the `[FIX-TC]` document; the **QuickFIX/J acceptance-definition corpus** (`reference-engines/quickfixj` @ QFJ_RELEASE_3_0_1, `quickfixj-core/src/test/resources/quickfix/test/acceptance/definitions/`, version-partitioned) is the executable oracle the split is mapped onto.
- Q: FR-013/US5/SC-007 said inbound `SendingTime(52)` beyond `MaxLatency` triggers "a defined disposition" without specifying it — what is the disposition? → A: **Option A** — emit `Reject` with `SessionRejectReason=10` (SendingTime accuracy problem) referencing tag 52, then `Logout`, then disconnect; if the offending message is the `Logon` itself, logout-with-error instead (no standalone reject, as no session is yet established). Engine-unanimous (QuickFIX C++/J `doBadTime`, fix8 `BadSendingTime`) and matches `[FIX-SL §4.2.3]`.

## User Scenarios & Testing *(mandatory)*

The "users" of this feature are the downstream layers that compose against the session surface — the transport layer (feeds verified inbound frames in, takes outbound frames out), the C ABI (`fixpp_session_*`), the session tap/log, and transitively the application developer building a FIX engine who supplies a `SessionConfig` and receives `fromAdmin`/`fromApp` callbacks. The session FSM has **no Phase-2 design doc by design** (`[2e §3.1]`: "the FSM itself … is owned by the Phase-4 session-module spec"); this spec defines it.

### User Story 1 - Establish a FIX session via the Logon handshake (Priority: P1)

A counterparty connection is available (a verified byte transport exists). One side initiates by sending `Logon(35=A)`; the other accepts by responding with `Logon`. Both sides validate `BeginString(8)`, `SenderCompID(49)`/`TargetCompID(56)`, and negotiate `HeartBtInt(108)`, after which the session reaches the **Active** state and application messages may flow.

**Why this priority**: This is the single critical-path capability of the module. Without a session reaching Active, no heartbeat, logout, reject, or application traffic is meaningful, and the entire `transport/` module (blocked on `session/`) cannot proceed. Every other story builds on an established session.

**Independent Test**: Drive an initiator FSM and an acceptor FSM against each other through an in-memory transport double; assert both transition `NotConnected → LogonSent/LogonReceived → Active`, the negotiated `HeartBtInt` matches the spec rule, and a `fromAdmin` callback delivers the peer `Logon`. Mismatched `BeginString` or `CompID` rejects the logon and the FSM never reaches Active.

**Acceptance Scenarios**:

1. **Given** a fresh initiator session, **When** it is opened, **Then** it emits a well-formed `Logon` with `MsgSeqNum=1`, the configured `SenderCompID`/`TargetCompID`, the requested `HeartBtInt`, and a `SendingTime(52)` stamped from the effective clock, and the FSM is in `LogonSent`.
2. **Given** an acceptor in `NotConnected` receiving a valid peer `Logon`, **When** it processes it, **Then** it replies with a `Logon` confirming the agreed `HeartBtInt`, transitions to `Active`, and surfaces the peer `Logon` via `fromAdmin`.
3. **Given** an inbound `Logon` whose `BeginString` is not a session-supported FIX version (FIX.4.x vs FIXT.1.1 gating), or whose `SenderCompID`/`TargetCompID` do not match the configured session identity, **When** it is processed, **Then** the logon is refused with a defined session error, no `Logon` confirmation is sent that would imply Active, and the FSM does not enter `Active`.
4. **Given** the very first message on the connection is **not** a `Logon`, **When** it is processed, **Then** the session refuses it per `[FIX-SL §4.3]` and does not enter `Active`.

---

### User Story 2 - Track and enforce message sequence numbers (Priority: P1)

Every inbound and outbound admin message carries `MsgSeqNum(34)`. The session maintains the next-expected inbound number and the next outbound number, increments them under the durable-before-transmit ordering of the `MessageStore` seam, detects in-sequence vs out-of-sequence inbound messages, and treats counter overflow as session-fatal.

**Why this priority**: Sequence-number correctness is the backbone of FIX session integrity (`[FIX-SL §4.1]`) and a precondition for every other admin flow; it is independently testable from message content. This feature also **owns the `seqnum_t` type** that `2e-msgstore` consumed as a placeholder — it must be published here.

**Independent Test**: Feed an ordered stream of admin messages and assert the expected-inbound counter advances by exactly one per accepted message; feed a message with `MsgSeqNum` lower than expected (no `PossDupFlag`) and assert the defined too-low disposition; feed one higher than expected and assert the defined gap disposition (per Clarification Q1); drive the outbound counter to its maximum and assert overflow is session-fatal, not a silent wrap.

**Acceptance Scenarios**:

1. **Given** an Active session with next-expected inbound = N, **When** a message with `MsgSeqNum = N` is accepted, **Then** the expected-inbound counter becomes N+1 and the outbound counter is unaffected.
2. **Given** an Active session, **When** an inbound message has `MsgSeqNum` **lower** than expected and no `PossDupFlag=Y`, **Then** the session treats it as a fatal sequence error per `[FIX-SL §4.5.3]` (logout with text, then disconnect) — PossDup duplicate semantics (S-010) are out of scope and such messages are treated as the no-PossDup case.
3. **Given** an inbound message with `MsgSeqNum` **higher** than expected, **When** it is processed, **Then** the FSM transitions to `RecoveryPending`, the message is held (not processed, not counted), the gap is surfaced via the documented callback/error, and the session is not disconnected; no ResendRequest is issued by this feature (per Clarification Q1).
4. **Given** the outbound `seqnum_t` at its maximum representable value, **When** the next outbound message would be assigned, **Then** the session reports a session-fatal sequence-overflow condition requiring operator intervention and does **not** wrap to zero.
5. **Given** an outbound message, **When** it is sent, **Then** the sequence number is committed to the `MessageStore` seam **before** the frame is handed to transport (durable-before-transmit, `[2e §root cause #1]`); a cancelled transmit MUST NOT leave a persisted-but-unsent gap inconsistent with the contract.

---

### User Story 3 - Keep an established session alive (Heartbeat / Test Request) (Priority: P2)

While Active, the session emits `Heartbeat(35=0)` when the outbound channel has been idle for the negotiated `HeartBtInt`, sends `TestRequest(35=1)` when the inbound channel has been silent beyond the interval, replies to a peer `TestRequest` with a `Heartbeat` echoing the `TestReqID(112)`, and declares the session unhealthy if a `TestRequest` goes unanswered within the grace window. All timing reads the effective clock seam.

**Why this priority**: Liveness keeps an established session usable and is required before production, but a session can be established and demonstrated (US1) without it; it depends on US1 reaching Active. P2.

**Independent Test**: With a mock clock, advance time past `HeartBtInt` with no outbound traffic and assert exactly one `Heartbeat` is emitted; advance past the inbound-silence threshold and assert a `TestRequest` with a unique `TestReqID` is emitted; deliver a peer `TestRequest` and assert the `Heartbeat` reply echoes its `TestReqID`; let the test-request grace window elapse unanswered and assert the defined unhealthy/disconnect disposition.

**Acceptance Scenarios**:

1. **Given** an Active session idle on the outbound channel for `HeartBtInt`, **When** the clock advances past it, **Then** exactly one `Heartbeat` is emitted and the idle timer resets (no heartbeat storm).
2. **Given** an Active session whose inbound channel is silent beyond the configured threshold, **When** the clock advances, **Then** a `TestRequest` with a unique `TestReqID` is emitted.
3. **Given** an inbound `TestRequest` with `TestReqID = X`, **When** it is processed, **Then** a `Heartbeat` with `TestReqID = X` is emitted.
4. **Given** an unanswered `TestRequest` after the grace window, **When** the clock advances, **Then** the session is declared unhealthy per `[FIX-SL §4.5.5]` and transitions toward disconnect.
5. **Given** `HeartBtInt = 0` negotiated at logon, **When** the session is Active, **Then** no heartbeat/test-request timers run (heartbeating disabled) per `[FIX-SL §4.3.4]`.

---

### User Story 4 - Terminate a session cleanly (Logout exchange) (Priority: P2)

Either side initiates an orderly shutdown by sending `Logout(35=5)`; the peer confirms with a `Logout`; the FSM transitions through `LogoutSent`/logout-received to `Disconnected`. A graceful-close attempt is bounded by a timeout drawn from the clock seam, after which the session disconnects regardless.

**Why this priority**: Orderly termination is required for correctness and clean reconnects but is exercised only after establishment; P2.

**Independent Test**: From Active, initiate `Logout`, deliver the peer `Logout` confirmation, and assert the FSM reaches `Disconnected` with counters/state finalized; separately, initiate `Logout` and let the confirmation never arrive — assert the clock-bound graceful-close timeout fires and the session force-disconnects.

**Acceptance Scenarios**:

1. **Given** an Active session, **When** it initiates `Logout`, **Then** it emits a well-formed `Logout`, enters `LogoutSent`, and on receiving the peer `Logout` confirmation transitions to `Disconnected`.
2. **Given** an Active session receiving a peer `Logout`, **When** it processes it, **Then** it replies with a confirming `Logout`, surfaces it via `fromAdmin`, and transitions to `Disconnected`.
3. **Given** an initiated `Logout` whose confirmation never arrives, **When** the graceful-close timeout (clock seam) elapses, **Then** the session force-disconnects to `Disconnected` and does not hang.
4. **Given** a session in any non-Active state, **When** a `Logout` is received, **Then** the FSM follows the `[FIX-SL §4.6]`-defined transition for that state and reaches `Disconnected` without undefined behavior.

---

### User Story 5 - Reject invalid session messages & enforce SendingTime (Priority: P3)

When an inbound admin message is structurally parseable but session-invalid (missing/garbled required session field, value out of range, wrong message type for the state), the session emits `Reject(35=3)` with `RefSeqNum(45)`, `RefTagID(371)`, `RefMsgType(372)`, and a `SessionRejectReason(373)`. Additionally, every inbound message's `SendingTime(52)` is validated against the effective clock within a configured `MaxLatency`, and outbound messages are stamped with a correctly formatted UTC `SendingTime`.

**Why this priority**: Hardens the surface against malformed/hostile and stale traffic. The core establishment + liveness path (US1–US4) is independently valuable and testable without full reject taxonomy; P3. This story exercises the **folded `core/` time-helper #4** (UTC `SendingTime` format/parse + `time_point`/`duration`).

**Independent Test**: Feed admin messages with (a) a missing required session field, (b) an out-of-range value, (c) a message type invalid for the current state; assert each yields a `Reject` with the correct `RefSeqNum`/`RefTagID`/`RefMsgType`/`SessionRejectReason`. Feed a message whose `SendingTime` is older than `MaxLatency` and assert the defined stale-time disposition; assert every emitted message carries a `SendingTime` formatted exactly per the FIX UTC timestamp grammar and that round-tripping it through the parse helper recovers the same instant.

**Acceptance Scenarios**:

1. **Given** an inbound admin message missing a required session field, **When** processed, **Then** a `Reject` is emitted with the correct `RefSeqNum`, `RefTagID`, `RefMsgType`, and a `SessionRejectReason` consistent with `[FIX-SL §4.5.4]`, and the expected-inbound counter still advances per spec.
2. **Given** an inbound message whose `SendingTime(52)` differs from the effective clock by more than the configured `MaxLatency`, **When** processed, **Then** the session emits `Reject`(`SessionRejectReason=10`, ref tag 52) then `Logout` then disconnects — or, if the offending message is the `Logon` itself, logout-with-error with no standalone reject (Clarification Q3, `[FIX-SL §4.2.3]`).
3. **Given** any outbound message, **When** it is serialized, **Then** its `SendingTime(52)` is a UTC timestamp formatted to the exact FIX grammar (`YYYYMMDD-HH:MM:SS(.sss)`), sourced from the effective clock, and parseable back to the same instant by the folded time helper.
4. **Given** a `Reject` is emitted, **When** inspected, **Then** it does not itself trigger an infinite reject loop (a malformed `Reject`/`Logout` is not itself rejected with another `Reject`).

---

### Edge Cases

- **Simultaneous logon**: both sides initiate `Logon` at once — the FSM resolves to a single Active session without deadlock or double-confirm.
- **Duplicate / repeat `Logon`** while already Active — handled per `[FIX-SL §4.3]` (not silently re-establishing).
- **`MsgSeqNum` too low without `PossDupFlag`** → session-fatal logout+disconnect (`[FIX-SL §4.5.3]`); PossDup duplicate handling (S-010) is explicitly out of scope.
- **`MsgSeqNum` too high** (gap) → FSM enters `RecoveryPending`, message held, gap surfaced, session stays connected (Clarification Q1); the recovery mechanism (ResendRequest/SequenceReset) and the exit transitions from `RecoveryPending` are deferred.
- **Garbled message** (parse-valid frame but unintelligible session content — `wire/` already filtered bad `BeginString`/`BodyLength`/`CheckSum`; here: unknown `MsgType`, missing `MsgSeqNum`) → handled per `[FIX-SL §4.5.2]` without advancing into Active.
- **`HeartBtInt` mismatch** between sides at logon → resolved per `[FIX-SL §4.3.4]`.
- **`HeartBtInt = 0`** → heartbeating disabled.
- **Missing or unparseable `SendingTime(52)`** on an inbound admin message → reject per `[FIX-SL §4.5.4]`.
- **Clock moved backward / large skew** between local effective clock and peer `SendingTime` → the Clarification Q3 disposition applies (`Reject` reason 10 → `Logout` → disconnect; Logon→logout-with-error); the session does not crash or assert.
- **`seqnum_t` overflow** (counter at max) → session-fatal, operator intervention required, no wrap (`[2e §6.7 store_seqnum_overflow]`).
- **`Logout` received in `LogonSent`/`LogoutSent`/`NotConnected`** → defined `[FIX-SL §4.6]` transition for that state.
- **Transmit cancelled after durable persist** → no persisted-but-unsent inconsistency with `[2e]`'s durable-before-transmit contract; the cancellation taxonomy is the `[2d §6.5]` one.
- **Out-of-scope admin types received** (`ResendRequest`/`SequenceReset`) — the FSM recognizes them as session admin but the recovery behavior is deferred; receipt is handled by a defined, bounded transition (consistent with the `RecoveryPending` seam, Clarification Q1), never undefined.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST implement a FIX session state machine (S-008) with at least the states `NotConnected → LogonSent → LogonReceived → Active → LogoutSent → Disconnected`, **plus an explicit `RecoveryPending` state** entered from `Active` on a detected sequence gap (per Clarification Q1), and the `[FIX-SL §4.10]` state-matrix transitions among them; every inbound admin message and timer event MUST map to a defined transition (no undefined/implicit transitions). The transitions *out of* `RecoveryPending` (ResendRequest/SequenceReset-driven recovery) are owned by the deferred recovery feature, not 005; 005 MUST define entry into `RecoveryPending`, the held-message behavior, and surfacing, leaving the exit transitions as a documented seam.
- **FR-002**: The system MUST implement `Logon(35=A)` initiation and acceptance (S-001) for both the **initiator** and **acceptor** roles, including `HeartBtInt(108)` negotiation (S-015) per `[FIX-SL §4.3.4]`.
- **FR-003**: The system MUST validate `BeginString(8)` and gate the session to a configured FIX version, distinguishing FIX.4.x from FIXT.1.1 (S-020) per `[FIX-SL §4.2.1]`; a `BeginString` outside the session's configured support MUST refuse the session.
- **FR-004**: The system MUST validate `SenderCompID(49)` and `TargetCompID(56)` against the configured session identity (S-016) per `[FIX-SL §4.2.2]`; a CompID mismatch MUST refuse the logon.
- **FR-005**: The system MUST implement `Logout(35=5)` initiation and acceptance (S-002) per `[FIX-SL §4.6]`, including a graceful-close timeout bounded by the clock seam after which the session force-disconnects.
- **FR-006**: The system MUST implement `Heartbeat(35=0)` emission on outbound idle (S-003) and `TestRequest(35=1)` emission on inbound silence plus `TestReqID(112)` echo on receipt (S-004) per `[FIX-SL §4.5.1]`/`[FIX-SL §4.5.5]`; `HeartBtInt=0` MUST disable heartbeating.
- **FR-007**: The system MUST implement session-level `Reject(35=3)` (S-007) per `[FIX-SL §4.5.4]` carrying `RefSeqNum(45)`, `RefTagID(371)`, `RefMsgType(372)`, and a `SessionRejectReason(373)`; a `Reject`/`Logout` MUST NOT itself be rejected (no reject loop).
- **FR-008**: The system MUST maintain inbound expected and outbound next `MsgSeqNum(34)` counters (S-009) per `[FIX-SL §4.1]`: increment-by-one on accepted/sent admin messages, detect too-low (fatal absent PossDup) and too-high (gap) conditions, and treat counter overflow as session-fatal with no wrap.
- **FR-009**: The system MUST **own and publish `seqnum_t`** (`<fixpp/session/seqnum.hpp>`) — promoting the placeholder `2e-msgstore` consumed (`[2e §3.1 / §10 Q9]`); the type MUST match FIX wire `MsgSeqNum(34)` semantics and overflow MUST surface the `[2e §6.7]` `store_seqnum_overflow` (session-fatal) condition.
- **FR-010**: The system MUST consume the `MessageStore` seam (`[2e §4]` 4-pure-virtual interface + awaitable retrieve visitor) under the durable-before-transmit outbound ordering and the parse→store(inbound)→`fromAdmin`/`fromApp` inbound ordering (`[2e §root cause #1]`); MessageStore **implementations** (in-memory S-012, file S-013), the store **interface definition itself** (owned by 2e/S-011), and resend/gap-fill (S-005/S-006/S-014) are **out of scope** — this feature consumes the seam only.
- **FR-011**: The system MUST source all session time — outbound `SendingTime(52)`, heartbeat/test-request timers, graceful-close timeout — from the effective clock (`effective_clock = SessionConfig::clock_override ?: EngineConfig::clock`, `[2d §7.9]`, NFR-015), never from a direct wall-clock call, so tests can drive time deterministically via the mock clock.
- **FR-012**: The system MUST provide the **folded `core/` time-helper surface row #4** — UTC `time_point`/`duration` types and `utc_time_to_fix_string` / `fix_string_to_utc_time` — used to format and parse `SendingTime(52)` to the exact FIX UTC timestamp grammar (`YYYYMMDD-HH:MM:SS` with optional millisecond/microsecond fraction); a format→parse round trip MUST recover the same instant. This discharges the deferred `core/` module-exit item.
- **FR-013**: The system MUST validate inbound `SendingTime(52)` against the effective clock within a configurable `MaxLatency` (S-019) per `[FIX-SL §4.2.3]`; when the divergence exceeds the bound it MUST emit `Reject` with `SessionRejectReason=10` (SendingTime accuracy problem) referencing tag 52, then `Logout`, then disconnect — except when the offending message is the `Logon` itself, in which case it MUST logout-with-error and emit no standalone reject (Clarification Q3).
- **FR-014**: The session FSM MUST compose via coroutines and ASIO native cancellation slots (`[const §VII.1/§VII.2]`, `[2d]`), run callbacks on the per-session strand by default (`[const §VII.4]`), and use the per-session async mutex for serialized state mutation (`[2f]`); no parallel stop-token abstraction.
- **FR-015**: All session failures MUST be reported through `expected_t<T>` and the `fixpp::core::error` enum (`[arch §5.3]`); the session layer MUST expose no C++ types through the C ABI (`[const §X.2]`). The FSM MUST be `noexcept` across the inbound-process / timer-fire window; a throwing user callback MUST trap rather than propagate.
- **FR-016**: The system MUST surface peer admin and application messages to the user via `fromAdmin`/`fromApp` and accept user output via the documented send path, with the per-session-strand reentrancy contract documented per entry point (`[const §X.5]`).
- **FR-017**: The following are explicitly **out of scope** and MUST NOT be partially implemented in a way that implies support: ResendRequest/SequenceReset/gap-fill (S-005/S-006/S-014/S-023/S-024), MessageStore implementations (S-012/S-013), PossDupFlag/PossResend duplicate semantics (S-010), ResetOnLogon/Logout/Disconnect & RefreshOnLogon (S-017/S-018), EncryptMethod/auth fields/DefaultApplVerID (S-021/S-022/S-025). Receipt of a deferred admin type MUST be a defined, bounded transition consistent with the `RecoveryPending` seam (Clarification Q1), never undefined.
- **FR-018**: The system MUST pass, as green executable `tests/conformance/` scenarios in this PR, the **capability-partitioned in-scope subset** of `[FIX-TC]` TC-001..TC-017 (Clarification Q2): establishment, liveness (heartbeat/test request), logout, session-level reject, in-sequence + MsgSeqNum-too-low, and SendingTime/CompID/version gating. Recovery-dependent cases (ResendRequest/SequenceReset/gap-fill/PossDup-resend/store-recovery) MUST be recorded as deferred-with-traceability in `coverage-index.md` pointing at the named later recovery feature, so `[const §V.5]` is satisfied by an explicit scoped subset. The concrete TC-### split is fixed at `/speckit-plan`; the version-partitioned QuickFIX/J acceptance-definition corpus is the executable oracle the split maps onto.

### Key Entities

- **Session**: one FIX session with one counterparty (one `BeginString` + SenderCompID/TargetCompID identity); owns the FSM state, both sequence counters, and the timer set. Multi-session registry / acceptor demultiplexing is out of scope (one Session models one counterparty pair).
- **Session FSM state**: the `[FIX-SL §4.10]` state value (`NotConnected`/`LogonSent`/`LogonReceived`/`Active`/`LogoutSent`/`Disconnected`) plus the explicit `RecoveryPending` state (Clarification Q1) entered from `Active` on a detected gap; its exit transitions are the deferred recovery feature's seam.
- **Sequence-number state**: next-expected inbound and next outbound `seqnum_t`, advanced under the durable-before-transmit `MessageStore` ordering; overflow session-fatal.
- **`seqnum_t`**: the sequence-number type this feature owns and publishes (FIX `MsgSeqNum(34)` semantics), consumed by `2e-msgstore` and downstream session work.
- **SessionConfig (consumed)**: the session-level frozen-at-open knobs this feature reads — identity (CompIDs, BeginString/version), `HeartBtInt` request, `MaxLatency`, `clock_override`, `store_factory`, strand/lock policy (`[2d §4 SessionConfig]`); this feature consumes the relevant fields, it does not redesign the config shape.
- **Effective clock (consumed)**: `SessionConfig::clock_override ?: EngineConfig::clock` (`[2d]`/NFR-015) — the single time source for SendingTime, heartbeat, test-request, and graceful-close timing; mockable for deterministic tests.
- **MessageStore seam (consumed)**: the `[2e §4]` interface (4 pure-virtual + awaitable retrieve visitor) the FSM calls under durable-before-transmit; impls are not in scope.
- **Admin messages**: `Logon`/`Logout`/`Heartbeat`/`TestRequest`/`Reject` — produced/consumed as wire frames via the merged `wire/` + `dictionary/` surfaces (parsing/serialization is `wire/`'s job; this feature interprets session semantics).
- **Time helper (#4, folded)**: UTC `time_point`/`duration` + `utc_time_to_fix_string`/`fix_string_to_utc_time` for `SendingTime(52)`; closes the `core/` module-exit item.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An initiator session and an acceptor session driven against each other through a transport double reach `Active` via a spec-conformant `Logon` handshake in 100% of well-formed runs across every session-supported FIX version (FIX.4.2/4.4/5.0SP2 + FIXT.1.1), with negotiated `HeartBtInt` and validated CompIDs/BeginString.
- **SC-002**: 100% of a labelled corpus of invalid logons (wrong `BeginString`/version, CompID mismatch, first message not `Logon`) is refused without the FSM ever entering `Active`, with the defined session error — zero false establishments.
- **SC-003**: Across an ordered admin-message stream, the expected-inbound and next-outbound counters advance by exactly one per accepted/sent message with zero drift over a long run; a too-low (no-PossDup) message produces the defined session-fatal disposition and a too-high message transitions the FSM to `RecoveryPending` with the message held and the gap surfaced (Clarification Q1) in 100% of cases; counter overflow is session-fatal with zero observed wrap.
- **SC-004**: With a mock clock, heartbeat emission occurs exactly once per idle `HeartBtInt` window (no storms, no misses) and a silent-inbound `TestRequest` + its `TestReqID` echo behave per `[FIX-SL §4.5]` in 100% of timed scenarios; an unanswered `TestRequest` past the grace window deterministically drives the unhealthy/disconnect disposition.
- **SC-005**: Orderly `Logout` from `Active` (initiated and received) reaches `Disconnected` in 100% of runs; a never-confirmed `Logout` force-disconnects within the clock-bound graceful-close timeout in 100% of runs (no hangs).
- **SC-006**: 100% of a labelled corpus of session-invalid admin messages produces a `Reject` with the correct `RefSeqNum`/`RefTagID`/`RefMsgType`/`SessionRejectReason`, with zero reject loops (a malformed `Reject`/`Logout` never triggers another `Reject`).
- **SC-007**: Every emitted message carries a `SendingTime(52)` matching the exact FIX UTC timestamp grammar; a format→parse round trip through the folded time helper recovers the identical instant for 100% of a representative timestamp corpus (including epoch edges, leap-second-adjacent, sub-second precision); inbound `SendingTime` beyond `MaxLatency` triggers `Reject`(reason 10)→`Logout`→disconnect (Logon→logout-with-error) per Clarification Q3 in 100% of cases.
- **SC-008**: The capability-partitioned in-scope `[FIX-TC]` subset (Clarification Q2 — establishment/liveness/logout/reject/in-seq+too-low/SendingTime-CompID-version) passes green in this PR's `tests/conformance/`; every recovery-dependent TC-### case is recorded in `coverage-index.md` as deferred-with-traceability to the named later recovery feature — zero conformance cases silently unaddressed.
- **SC-009**: The FSM performs zero heap allocation on the steady-state inbound-process and timer-fire paths (allocation confined to session-open and the caller-supplied arenas per `[const §VIII.5]`), verified by an allocation-counting harness; the session surface exposes no C++ type across the C ABI (layering check) and is clean under the sanitizer/fuzz matrix used by merged 001–004.
- **SC-010**: `seqnum_t` is published from `<fixpp/session/seqnum.hpp>` and a build/consumer check confirms `2e-msgstore`'s placeholder now resolves to this owned type — closing the `[2e §10 Q9]` cross-doc handoff and the `core/` time-helper module-exit item in the same PR.

## Assumptions

- This spec is the **Phase-4 session-module spec** that `2e-msgstore` v0.4 (and `2d` for session-scoped clock consumers) forward-reference; design decisions locked there (the `MessageStore` 4-pure-virtual seam + awaitable visitor, durable-before-transmit ordering, the `Clock` 4-pure-virtual seam + `effective_clock` rule, per-session strand default, async mutex on the store-write path) are treated as decided and are not re-opened here.
- **Both initiator and acceptor roles** are in scope (a usable FIX engine needs both); a multi-session registry / acceptor connection demux is **not** (one `Session` = one counterparty pair) — that is a transport/control-plane concern, deferred.
- First-session sequence numbers start at 1; sequence **persistence and recovery** across reconnect (RefreshOnLogon S-018) and reset settings (ResetOn* S-017) are deferred, so this feature assumes a fresh logical sequence per session lifetime and consumes the store seam without requiring a concrete persistent impl (a test double satisfies the seam).
- `wire/` (PR #68) and `dictionary/` (PR #66/#67) are merged and provide framing, parsing, serialization, and typed admin-message access; this feature interprets session **semantics** over those surfaces and does not re-implement wire/dictionary behavior. Field-representation types (`decimal<T>` 2a, `dict::field_traits` 2c) are reused.
- The transport layer (blocked on this module) supplies verified inbound frames and consumes outbound frames; this feature is tested against an in-memory transport double and a mock clock, not a real socket — real-socket integration is `transport/`'s concern.
- `SendingTime` precision defaults to milliseconds (FIX 4.x baseline); microsecond precision is supported by the helper where the negotiated version permits, but no version emits coarser-than-second precision.
- The `MaxLatency` default and the test-request grace-window default follow common FIX-engine practice (order of 120 s, and one heartbeat interval, respectively) unless `/speckit-plan` or `/clarify` pins exact values; they are caller-tunable via `SessionConfig`, not FIX-spec invariants.
- Build/test/toolchain conventions follow the merged 001–004 pattern (same project structure, sanitizer/fuzz/bench/coverage gates, the 95/85 coverage floor per `[const §IX.1]`); specifics are deferred to `/speckit-plan`.
- This is a **non-trivial design** (session FSM + threading/cancellation + error semantics) — `/speckit-clarify` is complete (3 clarifications resolved 2026-05-17, OSS-reference-grounded); **Gate A** is still mandatory before `/speckit-tasks`, per the pipeline.
