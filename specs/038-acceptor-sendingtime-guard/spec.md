# Feature Specification: Acceptor inbound-Logon SendingTime guard + session/reconnect hardening riders

**Feature Branch**: `038-acceptor-sendingtime-guard`
**Created**: 2026-06-14
**Status**: Draft
**Input**: User description: "Acceptor inbound-Logon SendingTime guard + two session/reconnect hardening riders (Fable F-f session subset). Three cleanly-separated concern groups, each independently witnessable."

## Overview

This feature closes the session-scoped subset of the Fable **F-f** review tail. It is three cleanly-separated, independently witnessable concern groups:

- **Group 1 (primary)** — apply the inbound-`SendingTime(52)` latency (anti-replay) guard to the **acceptor's first-Logon** path, which today skips it while the initiator's Logon-ack path and every established-session message already enforce it.
- **Group 2 (rider)** — contain a user-supplied callback exception during a reconnect attempt so it degrades gracefully rather than aborting the in-flight reconnect.
- **Group 3 (rider, test-only)** — add the missing session-level negative witnesses for the existing FIXT `DefaultApplVerID(1137)` reject path (no production change).

The groups share the session establishment / reconnect surface but are independent: each can be implemented, tested, and demonstrated on its own.

## Clarifications

### Session 2026-06-14

- Q: For the acceptor first-Logon `SendingTime(52)` guard, how should an absent/empty `52` be dispositioned (given `interpret_logon` does not validate `52`)? → A: **Reason code** — absent/empty, present-but-malformed, and stale all share a single `Reject(SessionRejectReason=10 SendingTimeAccuracy, RefTagID=52)`. One uniform disposition for the same defect; matches `session.cpp` established-Q3 (which already maps empty `52` → reason=10). Diverges from QuickFIX-cpp/QFJ only on the absent-`52` reason code (they use `RequiredTagMissing`=1), an accepted, documented divergence in favour of internal consistency.
- Design (grounded post-clarify, not a user choice): the reject **frame shape** is `Reject(35=3)` + `Disconnected` with **NO Logout** — matching the in-arm pre-establishment sibling (the FIXT `1137` reject at `session.cpp:2102-2136`, which emits `Reject` → `Disconnected`, no Logout), NOT the established-Q3 path (whose Logout is the graceful teardown of an *already-live* session). The same internal-consistency principle that fixes the reason code applies to the shape: pre-establishment first-Logon rejects do not Logout. (QuickFIX-cpp/QFJ `doBadTime` do emit a Logout — a further accepted divergence; the Reject + disconnect is interop-safe, as the existing `1137` path already demonstrates live vs QFcpp/QFJ.)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Acceptor rejects a stale or malformed Logon timestamp (Priority: P1)

A counterparty connects to a fixpp **acceptor** and sends an initial Logon whose `SendingTime(52)` is far outside the configured latency window (e.g. hours stale — a replayed or clock-skewed Logon), or whose `SendingTime(52)` is present but unparseable. Today the acceptor establishes the session regardless. After this change, the acceptor refuses to establish the session: it responds with a SendingTime-accuracy rejection (`Reject 373=10`) and disconnects (no Logout — matching the acceptor's existing pre-establishment `1137` reject shape), instead of completing the handshake.

**Why this priority**: This is the feature's reason to exist — a parity and anti-replay gap on the session-establishment boundary. Both reference engines (QuickFIX-cpp and QuickFIX-J) validate inbound `SendingTime` latency on the Logon for every role; fixpp validates it everywhere *except* the acceptor's first Logon, which is the one place an unauthenticated, not-yet-established peer is first seen.

**Independent Test**: Drive an acceptor session with a controllable clock; submit an inbound Logon whose `52` is outside the latency window and assert the session does not establish and the acceptor emits the SendingTime-accuracy rejection (no Logout) + disconnect. No other group is required.

**Acceptance Scenarios**:

1. **Given** an acceptor awaiting its first Logon and a controllable clock, **When** an inbound Logon arrives carrying a well-formed `SendingTime(52)` whose divergence from the effective clock exceeds the configured maximum latency, **Then** the session does NOT establish, a SendingTime-accuracy rejection (`Reject 35=3, 371=52, 373=10`) is emitted (and observed via `toAdmin`), the transport is disconnected (no Logout — pre-establishment shape), and the inbound sequence number is NOT advanced or persisted.
2. **Given** an acceptor awaiting its first Logon, **When** an inbound Logon arrives carrying a `SendingTime(52)` that is absent/empty OR present-but-unparseable, **Then** the Logon is explicitly rejected with the same SendingTime-accuracy disposition as a stale value (`reason=10`, no Logout) — never silently tolerated as if valid — and the session does NOT establish.
3. **Given** an acceptor awaiting its first Logon and a controllable clock, **When** an inbound Logon arrives carrying a well-formed `SendingTime(52)` within the configured maximum latency window, **Then** the session establishes exactly as it does today (no behavioural change on the conforming path).
4. **Given** an acceptor whose latency checking matches the existing established-session policy, **When** the same conforming and non-conforming Logons of scenarios 1–3 are submitted, **Then** the acceptor's first-Logon decision is consistent with what the established-session path and the initiator Logon-ack path already do for an identical timestamp.

---

### User Story 2 - A throwing credentials-rotated callback does not abort a reconnect (Priority: P2)

An operator registers a `credentials_rotated` notification callback. During an automatic reconnect attempt in which credentials were rotated, the callback is invoked. If that user callback throws, the in-flight reconnect attempt must not be aborted by the exception; the reconnect proceeds (or degrades) per the existing reconnect policy as if the notification had completed.

**Why this priority**: Robustness against third-party callback misbehaviour on the reconnect path. This is a graceful-degradation guarantee, NOT a crash-prevention fix — an exception escaping the reconnect coroutine body is delivered to the coroutine's promise, not to `std::terminate`. It is lower priority than Group 1 because it does not change protocol behaviour and the unguarded case is already non-fatal; it is higher than Group 3 because it is a (small) production behaviour change rather than test-only.

**Independent Test**: Register a `credentials_rotated` callback that throws; drive a reconnect attempt in which credentials are rotated; assert the reconnect attempt completes its normal outcome (proceeds/retries per policy) rather than being abandoned because of the callback exception.

**Acceptance Scenarios**:

1. **Given** a session reconnect attempt that rotated credentials and a registered `credentials_rotated` callback that throws, **When** the reconnect attempt invokes the callback, **Then** the exception is contained at the callback boundary and the reconnect attempt continues to its policy-defined outcome (it is not abandoned by the callback throw).
2. **Given** the same reconnect attempt with a `credentials_rotated` callback that returns normally, **When** the callback is invoked, **Then** behaviour is unchanged from today (the guard is transparent on the non-throwing path).

---

### User Story 3 - The FIXT version-routing rejection is observable in tests (Priority: P3)

When a FIXT acceptor receives a Logon whose `DefaultApplVerID(1137)` is absent or non-conformant, it already rejects (a session-level `Reject` carrying `RefTagID(371)=1137`) and disconnects. This behaviour currently has no session-level negative test witness. This story adds witnesses asserting the observable outcome — the rejection frame on the wire, the application/admin observation of that frame, and the disconnect — without changing any production code.

**Why this priority**: Verification completeness only. The behaviour is already correct by code inspection and fail-closed; the gap is that its analogous siblings (the 031 reject arms) carry observation/throw witnesses and this one does not. Lowest priority because there is no production change.

**Independent Test**: Submit a FIXT Logon with an absent / non-conformant `1137` to an acceptor and assert the reject frame (with `371=1137`) reaches the wire, is observed via the admin/application observation path, and the session disconnects without establishing.

**Acceptance Scenarios**:

1. **Given** a FIXT acceptor awaiting its first Logon, **When** an inbound Logon arrives with a non-conformant `DefaultApplVerID(1137)`, **Then** a session-level `Reject` carrying `RefTagID(371)=1137` is emitted to the wire, the rejection is observable through the admin/application observation path, and the session disconnects without establishing.
2. **Given** a FIXT acceptor awaiting its first Logon, **When** an inbound Logon arrives with `DefaultApplVerID(1137)` absent, **Then** the same observable rejection + disconnect outcome holds.

---

### Edge Cases

- **Future-dated timestamp**: a `SendingTime(52)` that is ahead of the effective clock by more than the maximum latency must be rejected the same as a stale (past) one — the guard tests absolute divergence, not direction.
- **Boundary divergence**: a `SendingTime(52)` whose divergence equals exactly the configured maximum latency is treated as conforming (within window), matching the existing established-session check's boundary semantics.
- **Missing/empty `SendingTime(52)` entirely**: a Logon with no `52` header field (or an empty `52`) is dispositioned by THIS guard as a SendingTime-accuracy reject (`reason=10`), identically to a malformed or stale value — `interpret_logon` does not validate `52`, so there is no upstream required-field rejection on the establishment path (per Clarifications 2026-06-14).
- **Latency checking disabled**: if and only if the existing configuration permits disabling inbound latency checking, the acceptor first-Logon guard honours that toggle identically to the established-session path (no new toggle is introduced).
- **Reconnect callback throws on a non-rotation attempt**: the callback is only invoked when credentials were rotated; a reconnect attempt without rotation never reaches the guarded invocation.

## Requirements *(mandatory)*

### Functional Requirements

**Group 1 — Acceptor inbound-Logon SendingTime guard**

- **FR-001**: The acceptor's first-Logon (pre-established) processing MUST validate the inbound Logon's `SendingTime(52)` latency against the effective clock using the same maximum-latency policy already applied on the established-session path and the initiator Logon-ack path.
- **FR-002**: When the inbound first-Logon `SendingTime(52)` diverges from the effective clock by more than the configured maximum latency (in either direction), the acceptor MUST NOT establish the session; it MUST emit a SendingTime-accuracy rejection (`Reject 35=3`, `RefTagID(371)=52`, `SessionRejectReason(373)=10`), route that frame through the admin-observation (`toAdmin`) path, and disconnect the transport. The reject frame is **not** followed by a Logout — matching the in-arm pre-establishment `1137` reject sibling, NOT the established-session (Q3) path whose Logout is a live-session teardown.
- **FR-003**: When the inbound first-Logon `SendingTime(52)` is absent/empty OR present-but-unparseable, the acceptor MUST reject the Logon with the SAME disposition as FR-002 (`Reject`, `373=10`, `371=52`, no Logout, disconnect) and MUST NOT establish the session. A missing or malformed value MUST NOT be silently treated as valid or skipped. (Per Clarifications 2026-06-14: absent/empty, malformed, and stale share one uniform reason-10 disposition mirroring the established-Q3 reason code; `interpret_logon` does not validate `52`, so this guard is the sole site that dispositions it on the establishment path.)
- **FR-004**: On any first-Logon rejection introduced by FR-002 or FR-003, the inbound sequence number MUST NOT be advanced or persisted (a session that never establishes must not mutate persisted sequence state).
- **FR-005**: A conforming inbound first-Logon (well-formed `SendingTime(52)` within the maximum-latency window) MUST establish the session with behaviour byte-for-byte identical to today's acceptor establishment path (no regression on the conforming path).

**Group 2 — Reconnect credentials-rotated callback guard**

- **FR-006**: When the reconnect path invokes the user-registered `credentials_rotated` notification callback, an exception thrown by that callback MUST be contained at the callback boundary so that it does not abort the in-flight reconnect attempt; the attempt MUST continue to its policy-defined outcome.
- **FR-007**: The callback guard MUST be transparent on the non-throwing path — a callback that returns normally MUST produce behaviour identical to today.

**Group 3 — FIXT 1137 rejection witness (test-only)**

- **FR-008**: The existing FIXT acceptor rejection for an absent or non-conformant `DefaultApplVerID(1137)` MUST be covered by session-level negative witness(es) asserting the observable outcome: the `Reject` frame carrying `RefTagID(371)=1137` on the wire, its observation through the admin/application observation path, and the subsequent disconnect.
- **FR-009**: Group 3 MUST NOT change any production code — it adds test coverage only.

**Scope boundary requirements**

- **FR-010**: This feature MUST NOT introduce any new public wire field, new error code/slot, new configuration option (the existing maximum-latency configuration is reused), code generation change, or C-ABI change.
- **FR-011**: This feature MUST be confined to the session and reconnect surface; the other Fable F-f tail items (wire-parser tag-overflow guard, C-ABI decimal sentinel, coverage-waiver remediation, the no-std-mutex corpus-gate list, and the behaviors-and-limitations doc back-fill) are explicitly out of scope.

### Key Entities

- **Inbound first-Logon `SendingTime(52)`**: the counterparty-supplied timestamp on the establishing Logon; its divergence from the effective clock is the quantity the Group 1 guard evaluates.
- **Maximum-latency policy**: the existing configured tolerance (and its existing enable/disable semantics, if any) that already governs inbound SendingTime checking on the established and initiator paths; reused unchanged.
- **`credentials_rotated` callback**: the user-registered notification invoked on a reconnect attempt that rotated credentials; the Group 2 guard contains exceptions escaping it.
- **FIXT `DefaultApplVerID(1137)` rejection**: the existing acceptor reject path (a `Reject` with `RefTagID(371)=1137`) that Group 3 witnesses.

## Normative References

Per `[const §VI.5]`, the exact coverage-index entries that inform this spec:

- **`[FIX-SL §4.2.3]` Validation of SendingTime(52)** → catalogue **S-019** (MaxLatency / latency check — reject messages with SendingTime too far from wall-clock; shipped by 005). Group 1 extends S-019's latency validation to the acceptor first-Logon (establishment) path, which it currently does not reach. (S-039 / 026 nanosecond precision is adjacent and untouched — the check operates on the parsed instant regardless of precision.)
- **`[FIX-SL §4.5.4]` Rejecting invalid messages (Reject 35=3)** → catalogue **S-007 / S-033** (021 Arm-C/D already emit `Reject(35=3)` carrying `RefTagID(371)=52` + `SessionRejectReason(373)=10` SendingTimeAccuracyProblem; 036 routed the engine `Reject` family through `Application::toAdmin`). Group 1 reuses this exact reject taxonomy + observation contract; Group 3 witnesses the `RefTagID=1137` variant of it.
- **`[FIX-SL §4.4]` Extended features for FIX session and connection initiation** (DefaultApplVerID(1137)) → catalogue **S-020** (FIXT.1.1 version-gating; shipped by 033). Group 3 adds the missing session-level negative witnesses for the existing 033 `1137` reject path (no production change).
- **`[impl]` reconnect callback-exception safety** (relates to **T-040** / 014 `session_event_credentials_rotated` emit on the `ReconnectFsm` reconnect path) — Group 2 is a robustness hardening of an existing in-process emit, backed by a design decision, not a new FIX spec section (`[const §VI.3]`).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An acceptor receiving an inbound first-Logon whose `SendingTime(52)` is outside the maximum-latency window does not establish the session and emits the SendingTime-accuracy rejection (`373=10`, `371=52`, no Logout) + disconnect — demonstrated for both a stale (past) and a future-dated timestamp, driven off a controllable clock.
- **SC-002**: An acceptor receiving an inbound first-Logon whose `SendingTime(52)` is absent/empty OR present-but-malformed explicitly rejects it (`reason=10`) and does not establish the session (no silent tolerance) — demonstrated for both the absent and the malformed case.
- **SC-003**: An acceptor receiving a conforming inbound first-Logon (well-formed `SendingTime(52)` within the window) establishes the session with output identical to the pre-change behaviour — verified by a no-regression check on the conforming establishment path.
- **SC-004**: The FIXT acceptor `DefaultApplVerID(1137)` rejection (absent and non-conformant) is covered by session-level negative witnesses asserting the on-wire reject frame, its observation, and the disconnect — where none existed before.

> Group 2 (FR-006/FR-007) is intentionally carried by unit-test evidence only and has **no** success criterion: it is a marginal graceful-degradation hardening, not a measurable user-facing outcome, and inflating it into an SC would overstate its significance.

## Assumptions

- **Missing `52` is NOT handled upstream** (resolved during clarify, was previously assumed otherwise): `interpret_logon` (the acceptor first-Logon validator) explicitly skips tags 34/52/98, and the steady-state Q3 guard is not reached on the establishment path, so an absent/empty `52` is currently unvalidated at the acceptor first-Logon. Group 1's guard is therefore the sole disposition site and covers absent/empty, malformed, AND stale — all as a uniform `reason=10` reject (per Clarifications 2026-06-14).
- **Inbound `52` is available at the first-Logon decision point** (confirmed during clarify): the established-Q3 guard extracts `52` via `scan_frame_header(frame).sending_time`, and the acceptor first-Logon arm holds the same `frame` (it passes it to `interpret_logon`), so the identical extraction is realizable there — no new plumbing required.
- **Latency policy reuse**: the maximum-latency tolerance (default 120 seconds) and any existing enable/disable semantics are reused exactly as the established-session and initiator paths use them; no new configuration surface is added.
- **Reference-engine grounding**: that the acceptor MUST reject a bad-SendingTime Logon (with SendingTime-accuracy semantics, session not established, processing halted) is taken from QuickFIX-cpp (`Session::next` → `isGoodTime` → `doBadTime`) and QuickFIX-J (`isGoodTime`/`doBadTime`), both of which validate inbound SendingTime latency role-agnostically on the Logon — not derived by symmetry from fixpp's initiator half alone. The **frame shape** (Reject + disconnect, no Logout) follows fixpp's in-arm `1137` pre-establishment reject rather than QuickFIX's `doBadTime` (which adds a Logout); this is the same internal-consistency-over-QF-fidelity choice as the reason-code disposition, and is interop-safe (the `1137` Reject-then-disconnect already runs live vs QFcpp/QFJ).
- **Group 2 mechanism**: an exception escaping the reconnect coroutine body is delivered to the coroutine promise (not `std::terminate`); the guard is a graceful-degradation containment matching the established `authorize_logon` callback-guard shape, distinct from the noexcept-method-calling-user-callback `terminate` class.
- **Test-fixture churn expected**: introducing an acceptor first-Logon latency guard will cause existing acceptor-establishment fixtures that carry a fixed/stale `52` against the test clock to require a controllable/current clock; such fixture updates are expected adjustments, not regressions.
- **Group 3 is additive**: only test artifacts are added; no production source changes for FR-008/FR-009.
