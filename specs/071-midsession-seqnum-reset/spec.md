# Feature Specification: Mid-Session Sequence-Number Reset Originator (S-032 residual)

**Feature Branch**: `071-midsession-seqnum-reset`

**Created**: 2026-07-12

**Status**: DEFERRED (not built) — see [DECISION.md](./DECISION.md)

> ⚠️ **This spec describes a CONSIDERED design that was NOT implemented.** After Gate A (2 rounds) established that both viable paths are substantial (in-band = 6 concurrency mechanisms; reconnect-based = requires the unshipped reconnect-after-drop capability), S-032's on-demand mid-session originator was **deferred** (user decision, 2026-07-12). The shipped reset surface (connect-time `reset_on_logon` + received-141) remains the supported way to reset sequence numbers. This bundle is retained as the investigation record; it does NOT describe shipped behavior. See `DECISION.md`.

**Input**: User description: "S-032 residual — standalone initiator-driven mid-session ResetSeqNumFlag(141=Y) sequence-reset originator. On-demand, application-triggered capability for a fixpp initiator to reset its FIX sequence numbers to 1 mid-session WITHOUT dropping the connection, by originating a Logon(141=Y) on the live transport (FIX-SL §4.4.2, 24-hour connectivity). Initiator/originator side only; received-141, connect-time ResetOnLogon, and teardown resets already shipped. Opt-in, inert by default; no C-ABI change."

## Context

fixpp already ships every *reactive* half of `ResetSeqNumFlag(141)` (catalogue entry S-032):

- the acceptor's received-141 inbound arm (a peer's `Logon(141=Y)` resets our counters and we confirm),
- the initiator's Logon-ack arm (when *we* sent `141=Y` at connect and the peer acks),
- connect-time `ResetOnLogon` (reset counters at the start of a fresh connection),
- teardown resets (`reset_on_logout` / `reset_on_disconnect`).

What remains — the "residual" deferred from feature 070 on 2026-07-12 — is the one *proactive, mid-session* case: an application that wants to reset the running sequence numbers to 1 **on demand, while the session is live, without tearing down and re-establishing the transport**. This is the FIX Session Layer §4.4.2 "Using `ResetSeqNumFlag(141)` for 24-hour connectivity" pattern: a long-lived session periodically resets counters (e.g. at a daily rollover) by exchanging a fresh `Logon(141=Y)` in-band, rather than disconnecting.

Interop feasibility of the in-band mechanism was confirmed against the QuickFIX-cpp reference engine (its inbound dispatch accepts a `Logon` on an already-established session and, on `141=Y`, resets sequence numbers to 1 and confirms with its own `Logon` — no disconnect). The reactive counterpart to this originator (a fixpp *acceptor* honoring an inbound mid-session `141=Y` while already active) is a separate concern and is **out of scope** here (see Scope Boundaries).

**Reference-engine survey (clarification input, 2026-07-12).** Neither QuickFIX-cpp nor fix8 *originates* an in-band `Logon(141=Y)` on a live socket. Their "24-hour connectivity" scheduled reset is implemented as **schedule-driven logout + disconnect + reconnect with ResetOnLogon** (QuickFIX `Session::next` → `checkSessionTime` → `reset()` = `generateLogout()`+`disconnect()`+`m_state.reset()`; fix8 `activation_service` schedule flag + `ResetSeqNumFlag=Y` only at `generate_logon`). The in-band live-socket 141=Y appears in the references **only reactively** (a peer accepting a *received* 141=Y). Consequently: (a) the genuinely-missing capability — and the scope of this feature — is the **manual, application-triggered in-band reset**; (b) automatic/scheduled reset is a **separate, deferred decision** and, if pursued, the reference-conformant path is the existing logout+reconnect machinery, not a new in-band timer.

## Clarifications

### Session 2026-07-12

- Q: Trigger surface — API-only method, API + auto config knob, or auto knob only? → A: Manual API only for this feature; auto-scheduling deferred to a separate later decision (reference engines achieve scheduled reset via logout+reconnect, not an in-band timer).
- Q: How is a peer that never confirms the mid-session reset handled? → A: ~~Reuse the existing connect-time logon-response timeout and disposition; no new timeout path or config.~~ **SUPERSEDED by the Gate A round 1 session below** — no such timeout exists in the engine; see the corrected FR-009 / L-071-3.
- Q: What happens if the reset trigger is invoked again while a reset handshake is already outstanding? → A: Refused via the existing active-only guard (during the handshake the session is not Active), returning the non-fatal invalid-state error; no queueing/coalescing.

### Session 2026-07-12 (Gate A round 1 — re-decisions after review)

Gate A round 1 (Codex + Opus adversarial, both source-verified) surfaced four P1s that forced re-deciding several design assumptions. The corrections:

- Q: FR-009 claimed reuse of a "connect-time logon-response timeout" — does such a timeout exist? → A: **No.** Source-verified three ways (no ack timer in `open()`, no read deadline in `run_read_pump`, no liveness loop in the LogonSent window). FR-009 rewritten to document the **actual** behavior: there is **no dedicated logon-response timeout** (mid-session reset inherits the same reality as connect-time logon); a non-confirming peer leaves the session in LogonSent until transport EOF or an application-initiated close. Recorded as limitation **L-071-3**. No timer is added (a real logon-response timer is a separate deferred concern, not in 071's scope).
- Q: Does suppressing `onLogout` for the reset transition risk swallowing a *real* teardown's callback? → A: **Yes** — the coarse "reset in progress" suppression would mute `onLogout` on a failed-reset `LogonSent→Disconnected` edge, so a logged-on session could die silently. The callback model is re-decided (FR-015): across a **successful** reset the application observes **neither** `onLogon` nor `onLogout` (session continuity); across a **failed** reset (non-confirmation → disconnect) the application **MUST** still observe `onLogout` (the session it believed live has died).
- Q: Can the ack arm's `peek_outbound()==2` restore predicate be relied on unchanged (FR-004)? → A: **No** — a non-reset outbound frame in the LogonSent window (e.g. a validation-`Reject` of a malformed first confirming Logon) advances the counter and makes the restore silently skip, producing a duplicate sequence 1. FR-004 is relaxed: the outbound restore MUST key off an **explicit latched fact** from the reset emit path (not the brittle `peek_outbound` inference); the ack arm may be minimally extended for this (it is no longer "reuse strictly unchanged"), and any non-reset emission before a valid confirmation fails the handshake closed rather than silently mis-restoring.
- Q: Is `Active && !is_awaiting_resend()` a sufficient trigger guard? → A: **No** — it omits the initiator-role check (an Active *acceptor* would pass). The guard is `role==initiator && Active && !is_awaiting_resend()` (FR-007). Also the transition to LogonSent must occur **before** the first `co_await` to close the in-flight-`send()` / liveness-emit interleave window (FR-014).
- Q: Does SC-002's "fixpp acceptor" fixture contradict the Scope Boundaries? → A: **Yes** — the fixpp acceptor has no inbound mid-session `141=Y` accept path (explicitly out of scope). SC-002's fixture is restricted to a **conforming peer only** (QuickFIX or a hand-rolled conforming acceptor).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Reset sequence numbers on a live session without reconnecting (Priority: P1)

An application operating a long-lived fixpp **initiator** session against a conforming peer wants to reset both sequence counters to 1 **on demand** (e.g. the application itself decides a daily 24-hour connectivity rollover has arrived) while keeping the underlying connection open, so it avoids the cost and observable churn of a disconnect/reconnect cycle. (The application drives *when*; there is no built-in scheduler — FR-010.)

**Why this priority**: This is the entire feature — the single remaining S-032 behavior. There is no smaller viable slice; without it the application's only option is a full reconnect-with-reset.

**Independent Test**: With a fixpp initiator in the established/active state connected to a conforming peer, invoke the new reset trigger. Observe (a) exactly one `Logon` with `ResetSeqNumFlag=Y` emitted on the existing transport at outbound sequence 1, (b) durable reset of both counters to 1, (c) on the peer's confirming `Logon(141=Y)` the session returns to the established/active state and the next application message carries outbound sequence 2 (no duplicate sequence 1), (d) no disconnect of the transport throughout.

**Acceptance Scenarios**:

1. **Given** an initiator session in the established/active state against a conforming peer, **When** the application invokes the mid-session reset trigger, **Then** the session emits exactly one `Logon(141=Y)` on the existing transport with outbound sequence 1, durably resets outbound and inbound counters to 1, and does not close the transport.
2. **Given** the reset `Logon(141=Y)` has been sent and the session is awaiting confirmation, **When** the peer replies with a confirming `Logon(141=Y)`, **Then** the session returns to the established/active state, next-expected-inbound and next-outbound reflect the post-reset counters (reusing the existing initiator ack behavior), and the next application send carries outbound sequence 2.
3. **Given** the reset trigger has never been invoked on a session, **When** the session runs any already-shipped flow (connect, send, receive, connect-time reset, received-141, teardown reset), **Then** every byte emitted and every disposition taken is identical to today's behavior (the feature is inert until triggered).
4. **Given** an initiator session that is **not** eligible (not active; OR an Active **acceptor**; OR Active but awaiting an inbound-gap resend), **When** the application invokes the mid-session reset trigger, **Then** the trigger is refused with the distinct, non-fatal `session_invalid_state_for_reset` error and the session state is unchanged (no `Logon` emitted, no counters touched).
5. **Given** an application with `onLogon`/`onLogout` observers on an active initiator, **When** a mid-session reset completes **successfully**, **Then** the application observes **neither** `onLogon` nor `onLogout` across the reset; and a subsequent real `close()` still fires `onLogout` exactly once (the fired-once latch was not corrupted).
6. **Given** a mid-session reset handshake is outstanding (session in LogonSent), **When** the reset **fails** (peer never confirms and the transport later EOFs, the durable store reset fails, or the application closes), **Then** the application observes `onLogout` exactly once (the session it believed live has died — no silent death).

### Edge Cases

- **Peer does not confirm**: the peer never replies with a confirming `Logon(141=Y)` after the reset `Logon` is sent. There is **no dedicated logon-response timeout** in the engine (verified — same reality as connect-time logon); the session remains in LogonSent until the transport fails (EOF) or the application closes it. On such a teardown the application still observes `onLogout` (FR-015). Documented as limitation **L-071-3** (see FR-009).
- **Peer rejects the reset**: the peer replies with a `Logout` or a `Logon` that fails validation / policy (e.g. a `bilateral_strict` mismatch). Handled by the existing initiator logon-ack / rejection machinery — this feature adds no new rejection semantics. If a non-reset frame (e.g. a validation-`Reject` of a malformed confirming Logon) is emitted in the LogonSent window, the reset handshake fails closed rather than allowing a mis-restored sequence (FR-004).
- **Durable store failure during reset**: persisting the counter reset fails. Must follow the established reset disposition (fatal-when-persistent, mirroring the shipped reset arms) — the trigger must not invent a new disposition.
- **Trigger invoked twice in quick succession**: a second trigger arrives while a first reset handshake is still outstanding. Because the session is no longer in the established/active state during the handshake, the second trigger is refused by the same active-only guard as FR-007 (returning the non-fatal invalid-state error) — no queueing/coalescing, no overlapping reset logons, and the single-shot latch the ack arm depends on is never re-armed while outstanding.
- **Application send races the reset**: an application `send()` is attempted while the reset handshake is outstanding (session momentarily not in the established/active state). Must follow the existing "send only when active" rule (refused with the existing invalid-state error), not silently reorder around the reset.
- **Clock unresolved at the new emit site**: the new mid-session emit path must guard the sending-time source exactly as the connect-time emitter does (a prior feature's Gate B caught an unguarded clock dereference at a new emit site).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The library MUST provide an application-invokable trigger that, when a fixpp **initiator** session is in the established/active state, initiates a mid-session sequence-number reset by originating a `Logon` carrying `ResetSeqNumFlag=Y` on the **existing** transport (no disconnect/reconnect).
- **FR-002**: On trigger, the session MUST durably reset both the outbound and inbound sequence counters to their minimum (1) using the existing shared durable-reset path, and MUST take the existing reset disposition on store failure (fatal-when-persistent) — no new disposition is introduced.
- **FR-003**: The emitted reset `Logon` MUST carry outbound sequence 1 (consistent with a reset-origin logon), and its `ResetSeqNumFlag=Y` MUST be produced by the existing logon-build path.
- **FR-004**: On the peer's confirming `Logon(141=Y)`, the session MUST complete the handshake through the already-shipped initiator ack arm (the `peer_ack_sent_reset_flag` path, including its inbound/outbound restore and `by_peer_request` classification) — the feature MUST NOT duplicate or fork that logic. The ack arm's **outbound restore** MUST key off an explicit latched fact carried from the reset emit path (that the reset Logon consumed the post-reset outbound sequence), NOT the brittle `peek_outbound()==seqnum_min+1` inference — because a non-reset frame emitted in the LogonSent window (e.g. a validation-`Reject` of a malformed confirming Logon) advances the counter and would make the inference silently skip the restore, duplicating sequence 1. This is a **minimal, additive extension** of the ack arm's restore predicate (the arm is otherwise reused intact); any non-reset outbound emission before a valid confirmation MUST fail the handshake closed rather than mis-restore.
- **FR-005**: After a successful reset handshake the session MUST return to the established/active state and the next outbound application message MUST carry outbound sequence 2 (exactly one post-reset frame at sequence 2, no duplicate sequence 1).
- **FR-006**: The capability MUST be opt-in and inert by default: on any session where the trigger is never invoked, all emitted bytes and all dispositions MUST be identical to current behavior. The shipped reset regression suites (`tests/session/test_reset_on_lifecycle.cpp`, `test_persistent_seqnum_hydrate.cpp`, `test_reset_seqnum_policy_matrix.cpp`) MUST remain green **unchanged**.
- **FR-007**: The trigger MUST be refused — with a distinct, non-fatal error, leaving session state and counters unchanged — unless **all** of: the session's role is **initiator**, the FSM is in the established/active state, and no inbound-gap resend is outstanding (`is_awaiting_resend()` is false). This covers: any non-active state, an Active **acceptor** (an acceptor reaches Active but must not originate a reset Logon), a mid-gap-fill reset (resends ride on Active as a transient flag, so the active check alone does not exclude them), and a second trigger during an outstanding handshake (the session is no longer Active).
- **FR-008**: The feature MUST NOT change the C-ABI (frozen at 1.5.0) and MUST NOT change the Python surface; it adds only a C++ session-level capability. (Adding a `fixpp::core::error` value is a C++ error, not a change to the frozen `fixpp_error_t` C enum.)
- **FR-009**: There is **no dedicated logon-response timeout** in the engine for a non-confirming peer — this was verified against shipped source (initiator `open()` arms no ack timer; the read pump has no read deadline; the liveness loop does not run in the LogonSent window). A peer that never sends a confirming `Logon(141=Y)` therefore leaves the session in LogonSent until the **transport fails (EOF)** or the **application closes** the session — exactly the same disposition the shipped **connect-time** logon already has (this feature introduces no regression relative to it, and adds no timer or config). On any such teardown the application MUST still observe `onLogout` (FR-015). This limitation is recorded as **L-071-3**; adding a real logon-response timer (covering connect-time and mid-session alike) is a separate, deferred concern.
- **FR-010**: The trigger MUST be a single application-invoked session method (on the session's single-writer strand, adjacent to `send()`/`close()`). No configuration knob and no automatic/scheduled reset are in scope for this feature; automatic scheduled reset is a separate, deferred decision (and, per the reference survey, is reference-conformant only via logout+reconnect, not an in-band timer).

### New-Emit-Site Safety Requirements (derived from prior Gate B defects + Gate A round 1)

- **FR-011**: The new mid-session emit path MUST NOT re-run the connect-time one-shot hydrate block (the store-hydrate one-shot latch is designed for a single connect→ack round-trip; a mid-session re-emit must not double-hydrate).
- **FR-012**: The new emit path MUST guard the effective clock source before stamping sending-time, exactly as the connect-time emitter does (avoids the unguarded-clock null-dereference class caught in 070 Gate B).
- **FR-013**: The new emit path MUST sample the post-reset outbound sequence **after** the durable reset (avoids sampling a pre-reset / un-hydrated counter, the wrong-durable-sequence class caught in 070 Gate B).
- **FR-014 (atomic transition boundary — Gate A round 1)**: The trigger MUST transition the FSM out of the established/active state (`Active→LogonSent`) **before its first suspension point (`co_await`)**, so that a `send()` already past its entry guard and the liveness (heartbeat/test-request) loop both observe a non-active state and cannot interleave an outbound frame with, or after, the reset. The durable reset and the wire emission then occur under the non-active state.
- **FR-015 (application callback model — Gate A round 1)**: Across a **successful** mid-session reset the application MUST observe **neither** `onLogon` nor `onLogout` (the session is continuous from the application's perspective). Across a **failed** reset — i.e. any teardown to a terminal state while the reset handshake is outstanding (non-confirmation → transport EOF, store-reset failure, or application close) — the application MUST still observe `onLogout` exactly once (the session it believed live has died). The suppression MUST be scoped to the benign `Active→LogonSent` reset edge and MUST NOT swallow a real teardown's `onLogout`, MUST NOT corrupt the `onLogon`/`onLogout` fired-once latches for later real transitions, and MUST clear its bookkeeping on **every** exit edge (success and failure) independent of whether an application callback object is registered.
- **FR-016 (single liveness loop — Gate A round 1)**: The reset's re-entry to the active state MUST NOT leave more than one liveness (heartbeat/test-request) loop running. Because the loop terminates only lazily on its next state check, the re-entry MUST use a generation/identity guard so any superseded loop exits promptly. This MUST be behavior-preserving for all existing (non-reset) flows.
- **FR-017 (tolerate in-flight peer frames in the reset window — advisor/pre-round-2)**: While a mid-session reset handshake is outstanding (LogonSent with the reset marker set), the session MUST NOT disconnect on a peer's **in-flight liveness admin frame** (`Heartbeat`, `TestRequest`) that arrives before the peer has processed the reset `Logon`. The shipped LogonSent inbound handler transitions **any** non-Logon inbound to Disconnected — correct at connect time, but fatal mid-session because the peer heartbeats continuously, so an unmodified window would drop the connection on a routine heartbeat and violate SC-001. In the reset window these liveness frames MUST be drained/ignored (connection preserved) until the confirming `Logon(141=Y)` arrives; an inbound `Logout` still transitions to terminal (peer declined). In-flight peer **application** frames sent before the reset are part of the sequence history both sides abandon by the §4.4.2 reset contract (the caller quiesces business traffic per Assumptions) — they are dropped-as-abandoned, not a slow-consumer drop (Article XV §15 is about drop-oldest under backpressure, a different mechanism). This tolerance MUST be gated on the reset marker so connect-time LogonSent behavior is byte/disposition-identical. The same handler covers the `co_await` window between the pre-emit transition (FR-014) and the wire emit.

### Scope Boundaries

**In scope**: the initiator/originator side — the application trigger (with the role/state/resend guard, FR-007), the new mid-session emit path (FR-014 atomic-transition boundary), reuse of the existing durable-reset helper, reuse of the initiator ack arm with a **minimal additive extension** to its outbound-restore predicate (FR-004 latched fact), the application-callback model across the reset (FR-015), and a liveness-loop generation guard (FR-016).

**Out of scope (already shipped — not re-specified)**: the acceptor received-141 inbound arm, connect-time `ResetOnLogon`, and teardown resets (`reset_on_logout` / `reset_on_disconnect`). (The initiator Logon-ack arm is reused; its outbound-restore predicate is minimally extended per FR-004 — that extension IS in scope.)

**Explicitly out of scope (separate concern — flagged, not silently pulled in)**: whether a fixpp **acceptor** honors an *inbound* mid-session `Logon(141=Y)` received while already active. This feature is scoped **initiator-only, against a conforming acceptor** (interop validated against QuickFIX). If fixpp-acceptor-to-fixpp-initiator mid-session reset is desired, it is a follow-up feature.

### Key Entities

- **Mid-session reset trigger**: the application-facing entry point that requests an in-band sequence reset on an active initiator session.
- **Reset `Logon` frame**: a `Logon` message with `ResetSeqNumFlag=Y` emitted on the existing transport at outbound sequence 1.
- **Sequence counters**: the durable outbound and inbound next-sequence values, reset to 1 and restored per the existing ack arm.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An application can reset both sequence counters to 1 on a live initiator session and continue exchanging messages on the **same** transport — verified by a test that observes no transport close across the reset and a post-reset application message at outbound sequence 2. The test MUST include a **peer `Heartbeat` (and `TestRequest`) arriving in the LogonSent reset window** and assert the connection is preserved (FR-017), proven RED against a mutant that keeps the shipped disconnect-on-non-Logon disposition.
- **SC-002**: A fixpp initiator's mid-session reset is honored by a **conforming peer** with the initiator returning to the active state — verified by an interop-style test against a conforming acceptor fixture **only** (QuickFIX or a hand-rolled conforming acceptor; NOT the fixpp acceptor, which has no inbound mid-session `141=Y` accept path — that is out of scope) where the peer's confirming `Logon(141=Y)` completes the handshake.
- **SC-003**: With the trigger never invoked, 100% of the shipped reset regression suites (74 tests across the three named files) pass unchanged, and a byte-level comparison of connect/send/receive flows shows zero difference from the pre-feature baseline (inert-by-default).
- **SC-004**: Invoking the trigger from every non-active session state is refused with the distinct non-fatal error and leaves counters and state unchanged — verified across the relevant states.
- **SC-005**: Each of the emit-site / boundary safety requirements (FR-011/012/013/014) has a discriminating regression test proven RED against a mutant that removes the guard, plus a witness that an in-flight `send()` and a liveness emit cannot interleave an outbound frame with the reset (FR-014).
- **SC-006**: No change to the C-ABI or Python surfaces — verified by the existing ABI-hygiene / surface checks staying green.
- **SC-007 (callbacks — FR-015)**: A successful mid-session reset fires **neither** `onLogon` nor `onLogout`; a failed reset (non-confirmation → transport EOF, store-reset failure, or application close during the handshake) fires `onLogout` exactly once. Verified with callback observers, including a follow-up real `close()` proving the fired-once latch was not corrupted. The store-reset-failure failure edge is exercised by fault injection.
- **SC-008 (single liveness loop — FR-016)**: After a successful reset round-trip exactly one liveness loop is active (no doubled heartbeat/test-request cadence over an interval), verified under TSan. The guard is behavior-preserving for existing flows (the shipped reset/reconnect suites stay green).
- **SC-009 (dup-seq closed — FR-004)**: A confirming Logon preceded by a non-reset outbound emission in the window (e.g. a validation-`Reject` of a malformed first Logon) does NOT leave `next_outbound` at 1 — the handshake either restores to 2 via the latched fact or fails closed; never a silent duplicate sequence 1. Proven RED against the `peek_outbound`-inference mutant.

## Assumptions

- **Initiator-only**: "initiator-driven" scopes the sender. The peer is assumed to be a conforming acceptor that honors an in-band `Logon(141=Y)` (QuickFIX-validated). The fixpp acceptor's mid-session-accept path is a separate feature.
- **Eligibility precondition**: the trigger is only valid on an **initiator** session that is fully established AND not awaiting an inbound-gap resend; invoking it otherwise is an application error handled by FR-007's guard (a new dedicated `session_invalid_state_for_reset` error — the existing `session_invalid_state_for_send` is name-scoped to `send` and is not reused).
- **Reuse over re-invention (with one minimal extension)**: the durable reset (`reset_seqnums_to_one_durable`), the initiator ack arm (`peer_ack_sent_reset_flag`), and the logon-build path already exist and are correct; this feature composes them from a new trigger rather than re-implementing reset logic. The **one** exception is the ack arm's outbound-restore predicate, which is minimally extended to key off a latched fact instead of the `peek_outbound` inference (FR-004) — required for correctness, not a re-implementation.
- **Strand discipline**: the trigger executes on the session's single-writer strand, consistent with `send()`/`close()`.
- **Policy interaction**: the existing `reset_seqnum_policy` (bilateral_strict / lenient / unilateral) governs the reset `Logon` and its confirmation exactly as it does at connect time; this feature introduces no new policy.
- **No new wire fields, codegen, or C-ABI/Python surface** beyond the single new C++ trigger (mirrors the additive, opt-in shape of 070's rows). One new **C++** `fixpp::core::error` value (`session_invalid_state_for_reset`) is added — this is not a `fixpp_error_t` (C-ABI) change.

## Limitations (recorded in `spec/behaviors-and-limitations.md` on close-out)

- **L-071-3 — no dedicated logon-response timeout**: a peer that never confirms the mid-session reset leaves the session in LogonSent until transport EOF or an application close (FR-009). This is not a regression — the shipped connect-time logon has the identical (non-)behavior; the engine has no logon-response timer. A real timer covering both paths is a separate deferred concern.
- **L-071-2 — in-flight peer app frames abandoned**: peer **application** frames sent before it processed our reset `Logon` (at old high sequence numbers) are abandoned by the mutual §4.4.2 reset contract (the caller quiesces business traffic before triggering — Assumptions). This is the defined reset semantics, not a slow-consumer drop. (Peer **liveness** frames in the same window are tolerated/drained, not abandoned — FR-017 — so the connection is preserved.) *(Corrected from the earlier characterization as a benign "reordering edge": the un-hardened behavior was a disconnect, now prevented by FR-017.)*
- (**L-071-1 retired**: the peek-window duplicate-sequence risk is now *fixed* by the FR-004 latched-fact predicate + fail-closed handling, not documented as a limitation.)

## Dependencies

- The already-shipped reset machinery in the session layer: the shared durable-reset helper, the initiator Logon-ack arm, the connect-time logon emitter (as the reference for emit-site safety), and the sequence-counter manager.
- The FIX Session Layer §4.4.2 semantics and the QuickFIX-cpp reference behavior (interop parity bar).

## Normative References

Per Article VI §5, the exact coverage-index entries informing this spec:

- **[FIX-SL §4.4.2] Using ResetSeqNumFlag(141) for 24-hour connectivity** — the normative basis for a mid-session sequence reset without reconnecting, including the counter-reset-to-1 and confirming-`Logon(141=Y)` exchange this feature originates; the S-032 catalogue row (`spec/feature-catalogue.md:363`, `spec/coverage-index.md` §4.4.2).
- **[FIX-SL §4.4.3] Using ResetSeqNumFlag(141) during connection establishment** — the already-shipped connect-time counterpart (out of scope; establishes what is NOT re-specified here; `spec/coverage-index.md` §4.4.3, S-032).
