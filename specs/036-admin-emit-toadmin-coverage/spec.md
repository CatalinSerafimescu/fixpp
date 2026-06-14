# Feature Specification: toAdmin/toApp observation coverage for engine-originated Reject and Logout emits

**Feature Branch**: `036-admin-emit-toadmin-coverage`
**Created**: 2026-06-14
**Status**: Draft
**Input**: User description: "Close the toAdmin/toApp observation gap on engine-originated reject/logout emit sites (Fable review F-d, assessment 2.4 DRIFT #1)."

## User Scenarios & Testing *(mandatory)*

Context: a registered `Application` is the host's observation point for the protocol. The engine
contract (FR-008) is that **before transmitting an engine-originated administrative message, the
engine invokes the application's `toAdmin` callback** — so the host can inspect every admin frame
the engine sends, and so a callback that throws is surfaced as an error and tears the session down
deterministically rather than being swallowed. Outbound **application** messages get the parallel
`toApp` callback.

Today that contract is honoured on only **15 of 25** engine-originated admin emit sites. Ten sites
were never wired (a half-restructure: the original wiring covered Logon / Logout / Heartbeat /
TestRequest / ResendRequest / SequenceReset and silently excluded the entire `Reject(35=3)` family;
a later change retrofitted only two new Logout sites; a still-later change wired its own one new
Reject but deliberately stepped *around* the shared reject helper rather than fixing it). The result
is the worst of both worlds — neither "Rejects are exempt from observation" nor "Rejects are
observed" is true.

The ten unobserved sites are the entire engine-originated `Reject(35=3)` family, one initiator
Logon-acknowledgement `Logout`, and one `BusinessMessageReject(35=j)`. Of these, nine are
**administrative** (Reject + Logout) and belong on `toAdmin`; the one `BusinessMessageReject(35=j)`
is an **application** message (it is not in the FIX administrative set A/0/1/2/3/4/5) and belongs on
`toApp` — routing it through `toAdmin` would be a contract violation and a reverse divergence from
reference engines (QuickFIX-cpp/J route outgoing `35=j` through `toApp`). The `35=j` site currently
fires **neither** callback.

This feature closes the gap exhaustively in one pass. On the **no-`Application` and non-veto** paths
it changes only *observation* — the same frames go out on the wire, byte-for-byte, in the same order,
and the only behaviour change is that a registered `Application` now sees them and a throwing callback
now tears the session down. The **one** intentional wire difference is a `BusinessMessageReject(35=j)`
`toApp` veto (`app_do_not_send`), which suppresses that `35=j` (it is not stored or transmitted) while
the inbound durable sequence advance is still persisted.

### User Story 1 - Application observes every engine-originated Reject and Logout (Priority: P1)

A host registers an `Application` to monitor and audit the admin frames its engine emits (for
compliance capture, alerting, or parity with an incumbent engine it is migrating from). Today it
silently misses every engine-originated `Reject(35=3)` and the initiator Logon-ack `Logout`. After
this feature, `toAdmin` fires for **every** engine-originated administrative frame before it is
transmitted.

**Why this priority**: This is the core of the feature — restoring the FR-008 observation contract
to full coverage. Without it the feature delivers nothing.

**Independent Test**: Register an `Application` whose `toAdmin` counts invocations. Drive one
scenario that provokes the engine to emit administrative frames spanning each Reject sub-family
(an inbound-message veto reject, a malformed-field reject, a sequence-reset boundary reject) plus
the initiator Logon-ack `Logout`. Capture the administrative frames on the wire. Assert
`toAdmin_calls == count(administrative frames on the wire)` as **exact-count equality** (not a
subset / "at least one" check). This count fails today (the engine emits the frames but `toAdmin`
is never called for them) and passes only when all nine administrative sites are wired.

**Acceptance Scenarios**:

1. **Given** a registered `Application`, **When** the engine emits an engine-originated `Reject(35=3)` from any sub-family, **Then** `toAdmin` is invoked with that frame before it is transmitted.
2. **Given** a registered `Application`, **When** the initiator emits the Logon-acknowledgement `Logout` (the Logon-ack validation-failure teardown), **Then** `toAdmin` is invoked with that `Logout` before it is transmitted.
3. **Given** a registered `Application` and a scenario emitting N engine-originated administrative frames, **When** the scenario completes, **Then** the number of `toAdmin` invocations equals N exactly (no missed site, no double-fire).
4. **Given** **no** registered `Application`, **When** any of these sites emits, **Then** behaviour is unchanged from today (the frame is transmitted; the callback hook is a no-op).

### User Story 2 - A throwing toAdmin during a Reject/Logout is surfaced, not swallowed (Priority: P1)

A host's `toAdmin` callback throws (a bug, or a deliberate assertion). For the 15 already-wired
sites this is caught, surfaced as `app_callback_threw`, and the session reaches a terminal/Disconnected
state. For the ten unwired sites the callback is **never invoked today**, so the hazard is prospective:
once those sites are wired, a throw must be caught and surfaced rather than escaping the noexcept emit
path — inconsistent and potentially process-terminating if left unguarded. After this feature, a
throwing `toAdmin` on **any** administrative emit site is handled identically: surfaced as
`app_callback_threw` and the session reaches a terminal/Disconnected state.

**Why this priority**: Callback-exception safety is a hard engine invariant; an unguarded throw on
the noexcept emit path is a latent crash. This story makes the new sites match the existing
throw-handling contract exactly.

**Independent Test**: Register an `Application` whose `toAdmin` throws. For each newly-wired
administrative site, drive the scenario that provokes that emit and assert the operation returns
`app_callback_threw` and the session reaches a terminal/Disconnected state (no escape, no swallow).
Note the asymmetry: the admin arm records `Disconnected` directly (`record_state_transition_(Disconnected)`,
matching the 15 wired sites — NOT a `close(terminal)` call); the BMR arm calls `close(close_mode::terminal)`.
`Disconnected` IS the terminal FSM state, so this is benign.

**Acceptance Scenarios**:

1. **Given** a `toAdmin` that throws, **When** an engine-originated `Reject(35=3)` would be emitted, **Then** the operation surfaces `app_callback_threw` and the session reaches a terminal/Disconnected state.
2. **Given** a `toAdmin` that throws, **When** the initiator Logon-ack `Logout` would be emitted, **Then** the operation surfaces `app_callback_threw` and the session reaches a terminal/Disconnected state.

### User Story 3 - BusinessMessageReject is observed via toApp, matching reference engines (Priority: P2)

A host monitoring outbound **application** traffic expects to see the engine's
`BusinessMessageReject(35=j)` on `toApp`, exactly as QuickFIX-cpp/J deliver it. Today the engine's
`35=j` fires neither `toApp` nor `toAdmin`. After this feature, an engine-originated `35=j` fires
`toApp` before transmission.

**Why this priority**: Closes the one application-message half of the gap and removes a
reference-engine parity divergence, but it is a distinct callback from the admin arm and a smaller
surface, so it is sequenced after the administrative coverage.

**Independent Test**: Register an `Application` whose `toApp` counts invocations. Provoke the engine
to emit a `BusinessMessageReject(35=j)` (an inbound application message the engine rejects). Assert
`toApp` was invoked for the `35=j` frame and that this frame is **excluded** from the administrative
`toAdmin` count of User Story 1.

**Acceptance Scenarios**:

1. **Given** a registered `Application`, **When** the engine emits an engine-originated `BusinessMessageReject(35=j)`, **Then** `toApp` is invoked with that frame before it is transmitted and `toAdmin` is **not**.
2. **Given** the exact-count witness of User Story 1, **When** a `35=j` is emitted in the same scenario, **Then** it is counted on the `toApp` side and does not perturb the `toAdmin == administrative-wire-count` equality.

### Edge Cases

- **Shared reject helper, two callers**: the engine-originated Reject emitted by the shared reject helper is reached from two distinct flows (an inbound-message veto reject and a no-`Application`/unknown-message-type reject). Wiring the helper once covers **both** caller flows, but only the app-registered `fromAdmin`-veto caller is the `toAdmin` witness; the no-`Application` unknown-MsgType caller is a structural no-op (reachable only when `application == nullptr`) witnessed as an FR-006 byte-identity no-op, not a `toAdmin` count.
- **Reject paired with a Logout**: two administrative sites emit a Reject immediately followed by a paired Logout that *already* fires `toAdmin`. After the fix both the Reject **and** its paired Logout fire `toAdmin` (two invocations, two wire frames) — the witness must not mistake the already-wired Logout for coverage of the Reject.
- **No Application registered**: every newly-wired site must be a strict no-op when no `Application` is set — no parse, no allocation surprise, identical wire output and ordering.
- **toAdmin / toApp ordering**: the callback fires **before** the frame is stored/transmitted (consistent with the 15 existing sites), so an observer sees the frame before the peer does and a throw prevents transmission of that frame's session continuing.
- **BusinessMessageReject veto**: a `toApp` veto (`app_do_not_send`) on the engine-originated `35=j` suppresses it — the frame is not stored or transmitted and the session stays Active (full `toApp` parity with the originate path and QuickFIX; see Clarifications). Suppressing the reject must **not** suppress the durable processing of the inbound message that triggered it: the rejected inbound message's sequence advance is still persisted (the session must not reprocess that message after a restart). The witness must account for a vetoed `35=j` not appearing on the wire while `toApp` was still invoked once **and** the inbound durable sequence still advanced.
- **No new error/wire surface**: a throwing callback reuses the existing `app_callback_threw` error; no new error slot, wire field, config knob, codegen, or C-ABI change is introduced.

## Clarifications

### Session 2026-06-14

- Q: For the engine-originated `BusinessMessageReject(35=j)` now routed through `toApp`, may a `toApp` **veto** (DoNotSend) suppress the reject, or is `toApp` here observe-and-throw-surface only? → A: **Full `toApp` parity — the veto IS honoured.** A DoNotSend from `toApp` suppresses the `35=j` (the frame is not stored or transmitted) and the session stays Active; a throw is surfaced as `app_callback_threw` and terminally closes the session. This matches QuickFIX-cpp's `sendRaw` exactly (`isAdminMsgType("j")` is false → the app branch wraps `toApp` in `catch (DoNotSend&) → return false`), is consistent with fixpp's own originate-path `toApp` contract (`app_do_not_send` → drop, stay Active), and reuses that existing path rather than adding a new observe-only variant. The inbound sequence number advances independently of suppression (QuickFIX advances it before the reject emit), so a veto causes no protocol desync. Accepted residual: a host's `toApp` can choose to swallow a protocol-mandated reject — the same authority QuickFIX and fixpp's originate path already grant the application over its own message channel.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Before transmitting **any** engine-originated administrative frame — the complete `Reject(35=3)` family and every engine-originated `Logout` — the engine MUST invoke the registered `Application`'s `toAdmin` callback with that frame. This extends the existing FR-008 contract from its current partial coverage to **all** engine-originated administrative emit sites.
- **FR-002**: The administrative sites newly brought under FR-001 are exactly the ten currently-unobserved sites enumerated in assessment 2.4 §2, minus the one application-message site (FR-004): the shared session-reject helper (covering both of its caller flows), the established-session SendingTime-accuracy Reject, the inbound-SequenceReset veto Reject, the two malformed-`OrigSendingTime(122)` Rejects (the 021 Arm C and the 021 RC#1 paths), the 021 Arm D Reject, the inbound-Logout veto Reject, the SequenceReset-`NewSeqNo`-too-low Reject, and the initiator Logon-acknowledgement `Logout`.
- **FR-003**: A `toAdmin` callback that throws during any newly-wired administrative emit MUST be surfaced as `app_callback_threw` and MUST cause the session to reach a terminal/Disconnected state — identical to the throw-handling of the 15 already-wired sites (the admin arm records `Disconnected` directly via `record_state_transition_(Disconnected)`; the BMR arm calls `close(close_mode::terminal)` — `Disconnected` is the terminal FSM state, so the two are functionally equivalent). `toAdmin` remains **inspect-only / not vetoable** for administrative frames (consistent with current behaviour).
- **FR-004**: Before transmitting an engine-originated `BusinessMessageReject(35=j)`, the engine MUST invoke the registered `Application`'s `toApp` callback (NOT `toAdmin`) with that frame, because `35=j` is an application message, not an administrative one. This MUST use the same `toApp` semantics as the originate path: a `toApp` veto (`app_do_not_send`) suppresses the `35=j` (the frame is neither stored nor transmitted) and the session stays Active; a throwing `toApp` is surfaced as `app_callback_threw` and terminally closes the session. (Per Clarifications, the veto is honoured — full `toApp` parity with the originate path and with QuickFIX.)
- **FR-005**: The fix MUST be applied **exhaustively in a single pass** over the full enumerated site list — not incrementally. After this feature, the count of engine-originated administrative emit sites that bypass `toAdmin` MUST be **zero**, and the one application-message reject site MUST route through `toApp`.
- **FR-006**: When **no** `Application` is registered, every newly-wired site MUST be byte-for-byte and ordering-identical to current behaviour — the callback hooks are no-ops and introduce no observable change on the wire, no new allocation, and no reordering.
- **FR-007**: The feature MUST NOT introduce any new wire field, error-code slot, configuration knob, code-generation output, or C-ABI surface. A throwing callback reuses the existing `app_callback_threw` error.
- **FR-008** *(documentation)*: The behaviors-and-limitations catalogue MUST gain a row that scopes **which** engine emits invoke `toAdmin` vs `toApp` (the prior limitation rows describe `toAdmin` as inspect-only but never enumerate coverage). The now-stale in-source comment that warns against routing through the shared reject helper (left by the feature that stepped around it) MUST be inverted to reflect that the helper now fires `toAdmin`. The amendment to FR-008 MUST be recorded in the affected feature's specification anchor without rewriting merged history.

### Key Entities

- **Engine-originated administrative frame**: a `Reject(35=3)`, `Logout(35=5)`, or other admin-set message the engine emits on its own initiative (not relayed from the host); the frames that must fire `toAdmin`.
- **`BusinessMessageReject(35=j)`**: an engine-originated **application** message; the single frame in scope that must fire `toApp` instead of `toAdmin`.
- **`toAdmin` / `toApp` callbacks**: the registered `Application`'s observation hooks for outbound administrative and application frames respectively; the observation contract this feature completes.
- **Exact-count observation witness**: the test invariant asserting `toAdmin_calls == administrative-frames-on-wire` (with the `35=j` counted on the `toApp` side); the regression that pins the coverage closed.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In a scenario that emits N engine-originated administrative frames (spanning each Reject sub-family and the initiator Logon-ack `Logout`), the registered `Application`'s `toAdmin` is invoked **exactly N times** — `toAdmin_calls == administrative-frames-on-wire`, exact-count equality. (Today this equality fails by the ten-site gap.)
- **SC-002**: The number of engine-originated administrative emit sites that transmit without first invoking `toAdmin` is **zero** (verified by enumerating the emit sites against the wiring, not only by a scenario).
- **SC-003**: A `toAdmin` (administrative) or `toApp` (`35=j`) callback that throws on any newly-wired site results in `app_callback_threw` and a terminal/Disconnected session state (the admin arm via `record_state_transition_(Disconnected)`; the BMR arm via `close(close_mode::terminal)`) — 100% of the newly-wired sites match the existing throw-handling contract.
- **SC-004**: An engine-originated `BusinessMessageReject(35=j)` invokes `toApp` exactly once and `toAdmin` zero times; it is excluded from the SC-001 administrative count.
- **SC-005**: With no `Application` registered, the wire output and emit ordering for every affected scenario are **byte-for-byte identical** to the pre-change baseline (the change is observation-only).

## Assumptions

- **`toAdmin` is inspect-only for administrative frames.** The existing contract does not let an `Application` veto an administrative frame; it may inspect, and a throw is surfaced. This feature preserves that — it does not add veto power to the admin arm.
- **The shared reject helper is the right single fix point for its two callers.** Wiring `toAdmin` + throw-handling inside the helper covers both the inbound-veto-reject and the unknown-message-type-reject flows; the remaining sites are inline and wired individually, enumerated from the assessment table so none is missed (the half-pass lesson: incremental wiring costs extra review rounds).
- **`BusinessMessageReject(35=j)` is an application message.** It is outside the FIX administrative set (A/0/1/2/3/4/5); reference engines route outgoing `35=j` through `toApp` **with full veto semantics** (QuickFIX-cpp `sendRaw`'s app branch catches `DoNotSend` and suppresses the frame). Therefore it is routed through `toApp`, not `toAdmin`, reusing the originate-path `toApp` contract (`app_do_not_send` suppresses; throw surfaces) — no new observe-only variant is introduced.
- **Observation is ordered before transmission.** Consistent with the 15 existing sites, the callback fires before the frame is stored/transmitted, so a throw prevents that frame's normal continuation and an observer sees the frame no later than the peer.
- **No reference-engine conformance pressure on the wire.** This is an internal-observation-contract + parity fix; the frames already conform and already go out. The change is invisible to peers and to hosts that register no `Application`.
- **The exact-count witness is the durable regression.** Per the half-restructure lesson, an exact-count (not subset) invariant test pins the coverage so any future emit site added without `toAdmin` fails the count — the same protection used for completeness gates elsewhere in the codebase.
