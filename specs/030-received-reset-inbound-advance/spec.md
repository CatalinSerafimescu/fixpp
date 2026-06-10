# Feature Specification: Received-Reset Inbound Advance Correction

**Feature Branch**: `030-received-reset-inbound-advance`
**Created**: 2026-06-10
**Status**: Draft
**Input**: User description: "received-reset inbound advance correction (acceptor 141=Y off-by-one)"

## Context & Problem Statement

This is a **conformance defect** fix, discovered while running a live interop acceptor cell of the fixpp engine against QuickFIX-cpp and QuickFIX-J (the reference engines already in production use).

When a peer initiates a sequence-number reset by sending a `Logon` with `ResetSeqNumFlag(141=Y)` — and the local `reset_on_logon` knob is **OFF** (the "received-141" path, owned by feature 013/024) — the engine:

1. Consumes the peer Logon, which is itself stamped `MsgSeqNum(34)=1`. The inbound sequence check advances next-expected-inbound to **2**.
2. Then performs a durable reset that re-bases **both** the inbound and outbound counters back to `1`.

Step 2 unconditionally clobbers the inbound advance from step 1. The net result is that next-expected-inbound is left at **1** instead of **2**.

**Observable harm:** the peer's next genuine message arrives at `MsgSeqNum=2`, which the engine now reads as *higher than expected* (gap detected) → it emits a **spurious `ResendRequest`**, forcing an unnecessary recovery round-trip on every peer-initiated reset. Additionally, when feature 027's next-expected advertisement is enabled, the acceptor's reply Logon advertises `NextExpectedMsgSeqNum(789)=1` instead of `2`.

**Oracle / correct behavior:** QuickFIX-cpp and QuickFIX-J treat a `141=Y` Logon as a normal in-sequence message at seq 1 — after consuming it, the next expected inbound is **2**. fixpp's own `reset_on_logon=true` knob path already produces 2; **only** the received-141 (knob-off) path is wrong.

**Root cause of the regression:** in feature 024 the reset was deliberately placed *after* the inbound check to preserve byte-identity of the **outbound reply** (the reply Logon must be stamped seq 1). That rationale was correct *for the outbound side* but was over-applied: it conflated "outbound reply MsgSeqNum = 1" with "inbound next-expected = 1". Those two counters are independent; only the outbound one should be re-based to 1.

## Clarifications

### Session 2026-06-10

No user-facing design decisions were open — the corrected behavior is fully determined by the conformance oracle. The one flagged assumption ("a `141=Y` Logon carries `MsgSeqNum=1`, and consuming it advances next-expected-inbound to 2") was **grounded by a reference-engine source sweep** rather than a user question:

- Q: Is the `141=Y` Logon's `MsgSeqNum=1` (and next-expected-inbound = 2 after consuming it) the spec-mandated / reference-engine behavior, or merely a fixpp convention? → A: **Reference-engine-confirmed; treat next-expected-inbound = 2 as authoritative.**
  - **QuickFIX-cpp** `Session.cpp::nextLogon`: on received reset, `m_state.reset()` rebases both counters to 1 (line ~206) *before* consuming the Logon; then because `resetSeqNumFlag` is set, the too-high branch is skipped and `incrNextTargetMsgSeqNum()` advances target 1→2 (line ~265). The 789 path adds +1 with the comment "incoming Logon did not increment the target SeqNum yet" (line ~710).
  - **QuickFIX-J** `Session.java::nextLogon`: explicitly *infers* `ResetSeqNumFlag` when `MsgSeqNum == 1` — "Inferring ResetSeqNumFlag as sequence number is 1 in response to reset request" (lines 2202-2204), directly corroborating the `MsgSeqNum=1` assumption. `resetState()` rebases to 1 (line 2215); the in-sequence branch then `incrNextTargetMsgSeqNum()` 1→2 (line 2303); the 789 advertisement computes `nextTarget(1) + 1 = 2` ("we always send 2 ... we haven't inc'd for current message yet +1", lines 2269-2278).
  - **Order is the crux**: both engines reset-*then*-increment, so the consumed reset Logon is accounted for (net = 2). fixpp increments-*then*-resets with no re-increment (net = 1) — that is the defect. The fix restores the post-reset inbound to 2, reproducing the engines' net result.

This converts the spec's flagged assumption to a grounded fact; the assumption is no longer "to be confirmed".

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Acceptor accepts the peer's post-reset traffic without a spurious resend (Priority: P1)

An operator runs a fixpp acceptor against a counterparty (e.g. QuickFIX) that initiates a session reset mid-relationship by sending `Logon(141=Y)`. After the reset handshake completes, the counterparty sends its first business/admin message at `MsgSeqNum=2`. The fixpp acceptor must accept it in-sequence and continue the session normally.

**Why this priority**: This is the entire feature. The current defect injects a spurious `ResendRequest` into every peer-initiated reset, causing an unnecessary recovery round-trip and divergence from the reference engines. It is the user-visible conformance failure that motivated the work.

**Independent Test**: Drive a received-141 Logon into an acceptor with `reset_on_logon=false`, then feed the peer's next message at seq 2; assert no `ResendRequest` is emitted and the message is delivered in-sequence. Confirmed end-to-end by re-running the live acceptor interop cell against QuickFIX-cpp/J.

**Acceptance Scenarios**:

1. **Given** an established fixpp acceptor session with `reset_on_logon=false`, **When** the peer sends `Logon(34=1, 141=Y)` followed by an in-sequence message at `34=2`, **Then** the acceptor accepts both with no `ResendRequest` and next-expected-inbound becomes 3.
2. **Given** the same reset, **When** the post-reset state settles, **Then** next-expected-inbound is 2 immediately after the Logon is consumed (matching QuickFIX and the `reset_on_logon=true` knob path).

---

### User Story 2 - Acceptor advertises the correct next-expected after a received reset (Priority: P2)

When feature 027 (NextExpectedMsgSeqNum advertisement) is enabled, a fixpp acceptor replying to a `Logon(141=Y)` must advertise `789=2` (the genuinely next-expected inbound after consuming the seq-1 reset Logon), not `789=1`.

**Why this priority**: Same root cause as US1, but a distinct wire-observable symptom. It only manifests when the 027 knob is on, so it is secondary to the core counter correctness, but it must be fixed in the same change because both read the same corrected counter.

**Independent Test**: Enable 027 advertisement on an acceptor, drive a received-141 Logon, and assert the reply Logon carries `789=2` while its own `MsgSeqNum` stays `1`.

**Acceptance Scenarios**:

1. **Given** an acceptor with 027 advertisement enabled and `reset_on_logon=false`, **When** it replies to a peer `Logon(34=1, 141=Y)`, **Then** the reply Logon has `MsgSeqNum=1` AND `NextExpectedMsgSeqNum(789)=2`.

---

### Edge Cases

- **`reset_on_logon=true` knob path**: MUST be unchanged — it already yields next-expected-inbound = 2. The fix touches only the received-141 (knob-off) arm.
- **Reset Logon not actually consumed**: If the inbound check did not advance for the Logon (no consumed in-sequence reset Logon), the inbound restore MUST NOT fire — there is nothing to restore.
- **Non-persistent store**: behavior must hold with an in-memory store; the inbound restore is a manager-only operation and does not require a persistent store.
- **`bilateral_strict` policy**: the received-141 path interacts with the reset policy (013/024); the corrected counter must hold across the policy matrix (bilateral-strict / bilateral-lenient / unilateral).
- **Durable lower-bound (INV-H1)**: after the fix the durable store still holds the re-based value (1) while the in-memory manager holds 2; the store must remain ≤ manager. The restore must be manager-only (no over-persist), consistent with the established lower-bound invariant.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: After an acceptor consumes a peer `Logon(141=Y)` on the received-141 path (with `reset_on_logon=false`), next-expected-inbound MUST be the reset base + 1 (i.e. 2), reflecting that the reset Logon was an in-sequence message at seq 1 that has been consumed.
- **FR-002**: Following such a reset, the peer's next in-sequence message (at `MsgSeqNum=2`) MUST be accepted and delivered in-sequence, with **no** `ResendRequest` emitted.
- **FR-003**: The acceptor's reply Logon `MsgSeqNum` MUST remain byte-identical to current behavior (seq 1) — the outbound counter stays re-based to the reset base. *(Paired with FR-004: the reply's MsgSeqNum is unchanged; only its 789 content changes.)*
- **FR-004**: When feature 027 next-expected advertisement is enabled, the reply Logon's `NextExpectedMsgSeqNum(789)` MUST advertise 2 (corrected from the current 1). *(This is an intentional wire-content change, not a byte-identity violation — see FR-003.)*
- **FR-005**: The durable message store MUST remain ≤ the in-memory manager value after the correction (INV-H1 lower-bound preserved). The inbound restore MUST be manager-only and MUST NOT durably over-persist.
- **FR-006**: The `reset_on_logon=true` knob path MUST be unchanged by this feature (it is already correct).
- **FR-007**: The inbound restore MUST be guarded on the reset Logon having actually been consumed by the inbound check; it MUST NOT fire on a path where no in-sequence reset Logon was consumed.
- **FR-008**: Behavior MUST hold across the established reset-policy matrix (bilateral-strict / bilateral-lenient / unilateral) and for both persistent and non-persistent stores.

### Discriminating Witness

The single test that proves the fix (and that no weaker proxy can pass) asserts the **triple** after a received-141 reset with 027 advertisement enabled:

> `next_inbound == 2` **AND** `reply.MsgSeqNum == 1` **AND** `reply.789 == 2`

A test asserting only "next_inbound == 2" would miss the outbound/789 coupling; a test asserting only the reply fields would miss the counter; both clauses are required.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Re-running the live acceptor interop cell against **both** QuickFIX-cpp and QuickFIX-J, after a peer `141=Y` reset, results in the session reaching Active with **zero** `ResendRequest` emitted by fixpp. *(This is the true close-out — the defect was found live and must be confirmed closed live.)*
- **SC-002**: The discriminating triple witness passes.
- **SC-003**: The 5 pre-existing tests across features 013/024/027/029 that currently pin the off-by-one value are updated to the corrected expectations, and the full 6-preset verify matrix (incl. all sanitizers) is green.
- **SC-004**: No change to any non-received-141 path: the `reset_on_logon=true` knob path and all steady-state sequencing remain byte-identical (verified by the unchanged remainder of the regression suite).

## Known Blast Radius (pins of the defect to correct)

Five existing tests currently **pin the defective value** (next-expected-inbound = 1, or `789=1`). These are stale pins of the off-by-one and must flip to the corrected value; each must be individually re-read to confirm it pins *this* case and not a distinct one:

1. `reset_seqnum_policy_matrix` — `Bilateral{Strict,Lenient}` / `Unilateral_Acceptor_CountersResetToOne` (×3): `next_inbound` 1 → 2.
2. `persistent_seqnum_hydrate` — `Acceptor_ResetLogon_InboundSeedWithheld_NoTooLowFatal` (×1).
3. `next_expected` — `Reset.AcceptorReplyReceived141_Advertises1` (×1): advertise `789` 1 → 2 (and rename to match).

## Assumptions

- **QuickFIX-cpp/J are the conformance oracle**, grounded in the source already inspected during root-cause analysis. They (and the FIX session protocol) treat the `141=Y` Logon as a consumed in-sequence message at seq 1.
- A `Logon` carrying `141=Y` is sent with `MsgSeqNum=1`, and consuming it advances next-expected-inbound to 2. **Confirmed** by the reference-engine sweep in Clarifications (QuickFIX-cpp `nextLogon` reset-then-increment; QuickFIX-J lines 2202-2204 explicitly infer the reset from `MsgSeqNum==1`).
- The fix rides on the existing 013/024 received-141 machinery and the 029 persistence spine; no new configuration knob is introduced.
- The acceptor role is where this is observable (initiator-side received-141 follows the same code but the live finding and primary scenario are acceptor-side).
