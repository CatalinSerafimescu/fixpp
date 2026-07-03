# Feature Specification: Outbound store-failure disposition — fail-closed on a persistent store

**Feature Branch**: `059-outbound-store-fail-closed`
**Created**: 2026-07-03
**Status**: Draft
**Input**: Cluster-3 flagship hardening S-P1-1 / S-P2-1 (`research/G19-fix-fpml-iso20022/remaining-work/perf-and-hardening-findings.md`; detail in `phases/phase-9/perf-investigation/findings/outbound-store-retention-review.md`).

## Overview

When an engine sends an outbound FIX message it does two things in order: it **retains** a copy of the framed message in its message store (so it can answer a future `ResendRequest`), then it **transmits** the message on the wire. The store keeps its own next-expected sequence counter and only advances it on a fully successful retain.

Today, if that retain fails, the failure is discarded and the message is transmitted anyway. Because the store's counter did not advance while the wire counter did, the store and the wire permanently disagree by one; every subsequent retain then fails the store's own in-order check and is discarded too. **One retain failure silently and permanently freezes all future retention for the life of the session** — the engine keeps advertising that it can resend, but retains nothing past the failure point.

This is a data-integrity defect with two blast radii:

1. **Silent resend loss.** A later peer `ResendRequest` for anything after the frozen point finds nothing retained and is answered with an administrative gap-fill instead of the real application messages — the counterparty silently loses every business message sent after the freeze.
2. **Persistent-store restart desync (worse, currently undocumented).** On a durable (file-backed) store the frozen counter is *also* the on-disk counter. A single transient disk fault (a full filesystem, a transient I/O error) freezes the durable counter below the sequence numbers the peer already saw on the wire. After a restart the engine recovers the too-low counter and re-stamps outbound messages at numbers the peer has already processed, desynchronising the session on reconnect.

The root cause is a **disposition asymmetry**: the engine already treats a durable *counter*-write failure as session-fatal (it disconnects), but treats the durable *frame*-retain failure — the write that is the entire point of the store — as ignore-and-continue. This feature makes the two symmetric for a durable store.

## Clarifications

### Session 2026-07-03

- Q: After a durable-store retain failure fails the session closed and the transient fault later clears, what must an in-process reconnect do (FR-007 / User Story 3)? → A: Reconcile from durable — on the fatal disconnect the session is marked for re-hydration so the next in-process reconnect re-reads the durable store counter and resumes cleanly (reusing the existing rehydrate-from-store machinery). US3 is IN scope; the session recovers with no manual intervention once the fault clears.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A durable store that cannot retain a message fails the session closed, never silently (Priority: P1)

An operator runs an engine backed by a durable, file-based message store — the configuration that promises reliable resend across restarts. The underlying storage suffers a transient fault while the engine is sending (the disk fills, or an I/O error occurs on one write). The engine must not continue as if nothing happened while silently having lost the ability to honour resend requests.

**Why this priority**: This is the only true P1 in the hardening batch. On a durable store the current behaviour turns a *transient* fault into a *permanent, silent* retention hole and a restart-time counterparty desync. Fixing it is the whole feature.

**Independent Test**: Drive a durable store to return one retain failure on a live send, then continue sending, then have the peer request a resend of messages at and after the failure point. Assert that the session failed closed (transitioned to disconnected and did not transmit the un-retained message) rather than silently continuing and later gap-filling real business messages.

**Acceptance Scenarios**:

1. **Given** a session backed by a durable store, **When** an outbound retain fails for a message, **Then** the session transitions to disconnected and the failing message is NOT transmitted on the wire (the peer never sees a sequence number the store could not retain).
2. **Given** the same failure, **When** the engine is restarted and reconnects, **Then** the recovered durable counter and the sequence numbers the peer already saw are consistent (no message is re-stamped at a number the peer already processed) — i.e. no reconnect desync.
3. **Given** a durable store that is retaining normally, **When** messages are sent, **Then** behaviour is byte-for-byte unchanged from today (retain, then transmit; no spurious disconnects).
4. **Given** a durable store, **When** a retain fails, **Then** the retention freeze cascade cannot occur — because the session has failed closed, there are no subsequent silently-discarded retains.

---

### User Story 2 - A volatile in-memory store keeps today's best-effort behaviour (Priority: P2)

An operator runs an engine backed by the volatile in-memory store — the embedded/testing configuration that makes no cross-restart durability promise. A retain failure there (e.g. a bounded store past its configured capacity) must continue to behave exactly as documented today.

**Why this priority**: The fail-closed change must be scoped to durable stores only. Regressing the volatile store's documented "logged-then-proceed" behaviour (tracked limitation L-008-2, status wontfix) would break existing embedded deployments and existing tests. This story exists to pin "no change on the volatile path".

**Independent Test**: Drive a volatile store to return a retain failure and assert the session does NOT disconnect and continues exactly as it does on `main` today (the documented best-effort behaviour).

**Acceptance Scenarios**:

1. **Given** a session backed by a volatile store, **When** an outbound retain fails, **Then** the session behaviour is unchanged from `main` (logged-then-proceed; no new disconnect).
2. **Given** a volatile store, **When** messages are retained normally, **Then** behaviour is unchanged.

---

### User Story 3 - After a transient durable fault clears, a reconnect resumes cleanly (Priority: P2)

The transient fault that caused the fail-closed disconnect (USER STORY 1) clears — the disk is no longer full. The engine reconnects (in-process, without a full restart). The session must resume retaining and sending from a state consistent with the durable store, not wedge into a repeating disconnect because an in-memory counter is stale relative to the durable one.

**Why this priority**: Fail-closed (US1) is safe on its own — worst case without US3 is a repeating fail-closed disconnect once the fault clears, which is an availability problem, not a data-integrity one. US3 upgrades "safe" to "recovers cleanly": the session must not be permanently stuck after the underlying fault is gone.

**Independent Test**: Cause a fail-closed disconnect via one durable retain failure, then clear the fault, then trigger an in-process reconnect, and assert the session resumes sending/retaining consistently (the next retained message uses the sequence number the durable store expects) rather than repeatedly disconnecting.

**Acceptance Scenarios**:

1. **Given** a session that failed closed on a durable retain failure and the fault has since cleared, **When** the session reconnects in-process, **Then** it resumes from the durable store's counter and successfully retains the next message (no repeating disconnect).
2. **Given** the reconnect resumes from the durable counter, **When** the next message is sent, **Then** it is stamped at the sequence number consistent with what the peer last saw and what the store last durably retained (no gap, no reuse of a number the peer already processed).

---

### Edge Cases

- **Retain-before-transmit ordering must hold under the fix.** The fail-closed decision must be made *before* the message is transmitted; a message the store could not retain must never reach the wire. (If it were transmitted first, the peer would see a number the engine cannot resend — the exact desync the feature prevents.)
- **Cancellation is not a store failure.** An in-flight send cancelled during shutdown (the existing operation-aborted path) already disconnects; that behaviour is preserved unchanged and is not conflated with a durable retain failure.
- **Best-effort emit sites.** A few administrative emit paths (e.g. an outbound reject in reply to a malformed `ResendRequest`, and a `Logout` emit) are intentionally best-effort — they currently ignore the emit result. Each such site must be dispositioned explicitly so that a durable-store retain failure still results in the session failing closed on every path where continuing would advertise a retention guarantee the engine can no longer honour.
- **Which store errors are fatal-when-persistent.** Every retain-failure class the store can return on the send path (I/O failure, out-of-order rejection, capacity exhaustion) is treated as fail-closed on a durable store; the classification is by store durability, not by the specific error code.
- **Non-send store failures out of scope.** This feature only changes the disposition of the *outbound retain on the send path*. It does not change resend-read (`retrieve`) error handling, reset handling, or the mid-traversal reset guard (a separate tracked item).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: On a session backed by a **durable/persistent** store, an outbound message-retain failure MUST cause the session to fail closed — transition to disconnected and propagate the failure to the send caller — instead of being discarded.
- **FR-002**: On a fail-closed retain failure, the message whose retain failed MUST NOT be transmitted on the wire (retain-before-transmit ordering: the fail-closed decision precedes transmission).
- **FR-003**: On a session backed by a **volatile/non-persistent** store, an outbound message-retain failure MUST continue to behave exactly as on `main` today (logged-then-proceed; no new disconnect). The documented limitation for the volatile store stands.
- **FR-004**: The fail-closed disposition MUST cover every outbound-retain failure class the store can return on the send path (durable I/O failure, out-of-order rejection, capacity exhaustion), classified by store durability rather than by error code.
- **FR-005**: The existing cancellation (operation-aborted) handling on the send path MUST be preserved unchanged and MUST NOT be reclassified as a durable retain failure.
- **FR-006**: Every send caller that can originate an outbound retain — including the administrative best-effort emit sites — MUST be dispositioned so that a durable-store retain failure reaches a session fail-closed. Any site that is intentionally left best-effort after a durable failure MUST be justified explicitly (a documented reason why continuing is safe on that specific path).
- **FR-007**: After a fail-closed disconnect caused by a durable retain failure, a subsequent in-process reconnect MUST resume outbound sequencing from the **durable store counter**, so that once the underlying fault clears the session recovers cleanly instead of repeatedly disconnecting on a stale in-memory counter.
- **FR-008**: On a persistent store, after the fix there MUST be no state in which the wire counter has permanently advanced past the durable store counter while the session continues in an active/connected state (the "silent retention freeze" state is unreachable).
- **FR-009**: The operator-facing limitations catalogue MUST be amended: the silent-resend-loss limitation (L-008-2) applies ONLY to the volatile store after this change; the persistent-store leg now fails closed. Any related behaviours/limitations rows MUST reflect the new persistent-store disposition.

### Key Entities

- **Message store**: retains outbound framed messages keyed by sequence number and owns its own next-expected counter; may be **durable/persistent** (survives restart, counter is on-disk) or **volatile** (in-memory only). The session already tracks whether its store is persistent.
- **Wire sequence counter**: the outbound sequence number the session stamps on messages and advances per send, independent of the store's counter; the two are intended to stay in lockstep.
- **Send-path retain step** (`store_then_emit`): the single point that retains an outbound frame and then transmits it; the locus of the disposition change.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A discriminating test reproduces the full failure cascade on the pre-fix code (RED): one durable retain failure, subsequent sends, then a peer resend request → post-failure business messages are lost (answered as gap-fill), the durable counter is stuck below the wire counter, and a restart recovers a counter below what the peer already saw.
- **SC-002**: The same test passes on the fixed code (GREEN): the session fails closed at the first durable retain failure, the un-retained message is not transmitted, and on reconnect/restart the durable counter and the peer's last-seen sequence agree.
- **SC-003**: A volatile-store retain failure produces byte-identical behaviour to `main` (no new disconnect; existing volatile-store tests unchanged and passing).
- **SC-004**: After a transient durable fault clears, an in-process reconnect resumes sending/retaining with zero repeating disconnects and no sequence gap or reuse.
- **SC-005**: The full existing session/store test suite passes with no regressions across the sanitizer matrix used by the feature's gating tier.
- **SC-006**: Every `store_then_emit` call site is enumerated and each is confirmed to either fail closed on a durable retain failure or is documented as intentionally best-effort with a justification.

## Assumptions

- **A-1 (durability signal)**: The session already carries a reliable "is my store persistent?" signal (set from the store factory at open time); the fix keys off that existing signal rather than introducing a new configuration surface.
- **A-2 (durable-fault reachability)**: A durable-store retain failure on a live send (a full/faulting log filesystem, a transient I/O error) is an ordinary operational fault for a durable engine, not a contrived one — so the fail-closed disposition matters in production, not only in tests.
- **A-3 (reconnect reconciliation mechanism)**: Per the 2026-07-03 clarification, the required in-session-reconnect behaviour is **reconcile-from-durable** (US3 in scope). FR-007's "resume from the durable counter" is expected to reuse the session's existing rehydrate-from-store machinery (marking the session for re-hydration on the fatal disconnect) rather than introduce a new counter-rewind path; the precise mechanism (e.g. clearing the hydration latch so the next reconnect re-reads the durable counter) is a design (`/plan`) decision.
- **A-4 (no public/wire/error-surface change)**: The change is a disposition of an already-returned error along an existing internal error channel; it introduces no new public API, no new wire behaviour, and no new error code visible to counterparties. Fail-closed reuses the existing session-fatal disconnect path.
- **A-5 (scope)**: Only S-P1-1 + S-P2-1 (the shared-root send-path disposition) are in scope. S-P2-2 (mid-traversal reset guard on the volatile store), S-P3, and unrelated cluster items are explicitly out of scope and remain separate tracker rows.
