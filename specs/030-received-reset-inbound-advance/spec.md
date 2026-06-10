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
3. **Given** an acceptor with a **persistent** store and `reset_on_logon=false`, **When** the peer sends `Logon(34=1, 141=Y)` and the durable store reset **fails** (fault-injected), **Then** the session disconnects and the store error propagates, and the persist-to-2 write-through is NOT reached — so no `store > manager` (`durable > in-memory`) durable state is ever observable. *(This is the discriminating witness for FR-010: it falsifies the "INV-H1 holds because the reset is fatal-when-persistent" claim if the reset were ever swallowed on a persistent store.)*

---

### User Story 2 - Initiator accepts post-reset traffic without a spurious resend (Priority: P1)

A fixpp **initiator** completes a Logon handshake in which the peer acks the reset (the peer's Logon-ack carries `141=Y`). This is a **separate code path** from the acceptor (the initiator Logon-ack handler with the `peer_ack_sent_reset_flag` reset, `session.cpp:3119` advance / reset at `:3162`, swallow at `:3167-3169`), but it has the **identical clobber**: `check_inbound` advances next-expected-inbound 1→2 for the consumed reset-ack Logon, then the reset re-bases both counters to 1 with no inbound restore. The arm is reachable (bilateral_strict initiator that sends `141=Y` and a peer that acks `141=Y`; bilateral-lenient / unilateral where the peer initiates the reset), and the harm is the same: the peer's next message at seq 2 reads too-high → spurious `ResendRequest`.

**Why this priority**: Same conformance defect, on a reachable second code path. It must be fixed symmetrically in the same change (per [[feedback_half_restructure_symmetric_api]]) — shipping the acceptor fix alone would knowingly leave a reachable conformance bug on the initiator.

**Independent Test**: Drive an initiator through a reset handshake whose Logon-ack carries `141=Y`, then feed the peer's next message at seq 2; assert next-expected-inbound is 2 immediately after consuming the ack Logon, and that the seq-2 message is accepted with no `ResendRequest`. (The `789` reply clause does NOT apply on this path — the initiator already sent its own Logon before this handler runs and builds no reply Logon here; 789-advertisement is acceptor-reply-specific, see US3. A later initiator re-advertise derives from the same corrected counter, per 027.)

**Acceptance Scenarios**:

1. **Given** an established fixpp initiator handshake with `reset_on_logon=false` whose peer Logon-ack carries `141=Y` (consumed at `34=1`), **Then** next-expected-inbound is 2 immediately after the ack is consumed, and a subsequent peer message at `34=2` is accepted in-sequence with no `ResendRequest`.
2. **Given** an initiator with a **persistent** store whose peer Logon-ack carries `141=Y`, **When** the durable store reset **fails** (fault-injected), **Then** the session disconnects and the store error propagates and the persist-to-2 write-through is NOT reached — no `store > manager` durable state is ever observable (symmetric to US1 scenario 3, FR-010).

---

### User Story 3 - Acceptor advertises the correct next-expected after a received reset (Priority: P2)

When feature 027 (NextExpectedMsgSeqNum advertisement) is enabled, a fixpp acceptor replying to a `Logon(141=Y)` must advertise `789=2` (the genuinely next-expected inbound after consuming the seq-1 reset Logon), not `789=1`.

**Why this priority**: Same root cause as US1, but a distinct wire-observable symptom. It only manifests when the 027 knob is on, so it is secondary to the core counter correctness, but it must be fixed in the same change because both read the same corrected counter.

**Independent Test**: Enable 027 advertisement on an acceptor, drive a received-141 Logon, and assert the reply Logon carries `789=2` while its own `MsgSeqNum` stays `1`.

**Acceptance Scenarios**:

1. **Given** an acceptor with 027 advertisement enabled and `reset_on_logon=false`, **When** it replies to a peer `Logon(34=1, 141=Y)`, **Then** the reply Logon has `MsgSeqNum=1` AND `NextExpectedMsgSeqNum(789)=2`.

---

### Edge Cases

- **`reset_on_logon=true` knob path**: MUST be unchanged — it already yields next-expected-inbound = 2. The fix touches only the received-141 (knob-off) arm.
- **Reset Logon not actually consumed**: If the inbound check did not advance for the Logon (no consumed in-sequence reset Logon), the inbound restore MUST NOT fire — there is nothing to restore.
- **`bilateral_strict` policy**: the received-141 path interacts with the reset policy (013/024); the corrected counter must hold across the policy matrix (bilateral-strict / bilateral-lenient / unilateral), on both the acceptor and the initiator arm.
- **Durable lower-bound (INV-H1, persistent store)**: on a **persistent** store, after the fix both the in-memory manager AND the durable store hold the corrected value 2, so INV-H1 (`store ≤ manager`) holds with **equality** (`store == manager == 2`). This is **not** the 029 over-persist class: the consumed seq-1 reset Logon is a *surviving net-advance*, so persisting 2 is correct write-through (it makes the received-141 Logon persist identically to any other in-sequence Logon, matching QuickFIX FileStore), not a `durable > manager` over-persist with no surviving advance. The equality is **guaranteed**, not asserted on faith: on a persistent store the durable reset is made **fatal** (see the durable-reset-failure edge case), so persist-to-2 only ever runs after a known-good reset — there is no path where it advances a stale store. Leaving the store at 1 (manager-only) would be a half-fix that re-opens the exact T034 inbound-persistence gap 029 closed, localized to this path: on restart hydrate seeds 1, the peer's next non-reset message at seq 2 reads too-high, and the Logon gate has **no** ResendRequest arm → fatal disconnect.
- **Durable reset failure on a persistent store (fatal)**: on a **persistent** store, a received-141 durable reset failure MUST now be **fatal** (disconnect + propagate the store error), NOT swallowed-and-proceed. Rationale: a swallowed (`logged`) reset failure on a persistent store leaves the store at its stale value N while the manager reaches the reset base; the subsequent persist-to-2 write-through would then advance the **stale** store (N→N+1) — for any session that had received messages (N>1) that yields `store > manager` (INV-H1 violation → silent inbound skip on restart, the 029 over-persist harm). Making the reset fatal-when-persistent guarantees the reset succeeded before persist-to-2 runs, so `store == manager == 2` truly holds; a reset failure disconnects (the session re-opens, re-hydrates the stale store, and the peer re-drives the reset — no inconsistent durable state is ever observable). This aligns with 029 D-3 ("inbound-correctness failures are fatal") and the existing fatal reset sites (`session.cpp:682` initiator knob, `:1764` acceptor knob). **This amends the 024 FR-001/C2.6 I-07 contract for the persistent received-141 sub-case** (was logged-then-proceed / stay-Active). Non-persistent stores are unaffected.
- **Non-persistent store**: behavior holds with an in-memory store. The persist-to-2 write-through is a no-op (no durable counter, INV-H4), and the durable reset cannot meaningfully fail, so the reset stays effectively `logged` and a received-141 reset never disconnects on this path. No hydrate-on-restart, hence no over-persist hazard. The 024 stay-Active-under-(non-existent-)failure characterization is **retained** for non-persistent stores.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: After an acceptor consumes a peer `Logon(141=Y)` on the received-141 path (with `reset_on_logon=false`), next-expected-inbound MUST be the reset base + 1 (i.e. 2), reflecting that the reset Logon was an in-sequence message at seq 1 that has been consumed.
- **FR-002**: Following such a reset, the peer's next in-sequence message (at `MsgSeqNum=2`) MUST be accepted and delivered in-sequence, with **no** `ResendRequest` emitted.
- **FR-003**: The acceptor's reply Logon `MsgSeqNum` MUST remain byte-identical to current behavior (seq 1) — the outbound counter stays re-based to the reset base. *(Paired with FR-004: the reply's MsgSeqNum is unchanged; only its 789 content changes.)*
- **FR-004**: When feature 027 next-expected advertisement is enabled, the reply Logon's `NextExpectedMsgSeqNum(789)` MUST advertise 2 (corrected from the current 1). *(This is an intentional wire-content change, not a byte-identity violation — see FR-003.)*
- **FR-005**: On a **persistent** store, the correction MUST restore BOTH the in-memory manager AND the durable store to the reset base + 1 (= 2), giving `store == manager == 2` (INV-H1 `store ≤ manager` holds with equality). This is correct write-through for a surviving net-advance (the consumed seq-1 reset Logon), NOT the 029 over-persist class — it persists the received-141 Logon identically to any other in-sequence Logon. The equality holds **because** the durable reset is fatal-when-persistent (FR-010): persist-to-2 only runs after a known-good reset, so it never advances a stale store. A manager-only restore (store left at 1) is FORBIDDEN: it re-opens the inbound-persistence gap 029 closed and turns a spurious-ResendRequest into a fatal-disconnect-on-restart (the Logon gate has no ResendRequest arm). On a **non-persistent** store the durable write-through is a no-op (INV-H4) and only the in-memory manager is restored.
- **FR-010**: On a **persistent** store, a received-141 durable reset failure MUST be **fatal** — the session disconnects and the store error propagates — on BOTH arms (acceptor + initiator). It MUST NOT be swallowed-and-proceed. This guarantees the reset succeeded before the FR-005 persist-to-2 write-through runs, so no `store > manager` durable state is ever observable. On a **non-persistent** store the reset stays effectively `logged` (it cannot meaningfully fail) and the session does not disconnect on a received-141 reset. This amends the 024 FR-001/C2.6 I-07 logged-then-proceed contract for the persistent received-141 sub-case (was: stay-Active under a swallowed store-reset failure). Implementation: pass `store_is_persistent_ ? reset_disposition::fatal : reset_disposition::logged` to the shared `reset_seqnums_to_one_durable(disposition)` helper on both arms; the initiator arm (currently a hand-rolled `reset_to_one()` + swallowed `(*store_).reset()`) is consolidated onto that same helper for symmetric disposition semantics ([[feedback_half_restructure_symmetric_api]]).
- **FR-006**: The `reset_on_logon=true` knob path MUST be unchanged by this feature (it is already correct).
- **FR-007**: The inbound restore MUST be guarded on the reset Logon having actually been consumed by the inbound check; it MUST NOT fire on a path where no in-sequence reset Logon was consumed.
- **FR-008**: Behavior MUST hold across the established reset-policy matrix (bilateral-strict / bilateral-lenient / unilateral) and for both persistent and non-persistent stores.
- **FR-009**: The correction MUST cover BOTH the acceptor received-141 arm (`NotConnected` Logon handler) AND the **initiator** Logon-ack arm (`peer_ack_sent_reset_flag` reset) — they are separate code paths with the identical clobber, both reachable. The fix MUST be applied symmetrically (restore + persist + the FR-010 fatal-when-persistent disposition on each arm) and witnessed on each role. The initiator witness asserts `next_inbound == 2` + harm-repro (peer seq-2 accepted, no `ResendRequest`); the `789` reply clause is acceptor-reply-specific (the initiator builds no reply Logon on this arm).

### Discriminating Witness

On the **acceptor** arm, the single test that proves the fix (and that no weaker proxy can pass) asserts the **triple** after a received-141 reset with 027 advertisement enabled:

> `next_inbound == 2` **AND** `reply.MsgSeqNum == 1` **AND** `reply.789 == 2`

A test asserting only "next_inbound == 2" would miss the outbound/789 coupling; a test asserting only the reply fields would miss the counter; both clauses are required.

On the **initiator** arm the discriminating witness is `next_inbound == 2` + harm-repro (peer seq-2 accepted, no `ResendRequest`). The `reply.789` clause does not apply: the initiator already sent its Logon before this handler runs and builds no reply Logon on this arm (789-advertisement is acceptor-reply-specific; a later initiator re-advertise derives from the same corrected counter, per 027).

Each arm's witness MUST also assert INV-H1 **directly** on the durable store: `store.durable_inbound == seqnum_min + 1` (== 2), `store == manager` (the 029 W9b proxy-gap lesson — assert the store value, not a manager proxy).

**Fault-injection witness (the FR-010 soundness proof, both arms):** a separate witness per arm with a **persistent** store and a fault-injected store-reset failure (`fail_next_reset()`) on the received-141 path MUST assert (i) the session **Disconnected** + the store error propagated, AND (ii) persist-to-2 was **not** reached, so no `store > manager` (`durable > in-memory`) state is ever observable. This is the witness that makes the "INV-H1 holds because the reset is fatal-when-persistent" claim falsifiable — without it the soundness claim is unwitnessed (the 029 W9b proxy-gap lesson, [[feedback_witness_asserts_named_postcondition_not_proxy]]).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Re-running the live acceptor interop cell against **both** QuickFIX-cpp and QuickFIX-J, after a peer `141=Y` reset, results in the session reaching Active with **zero** `ResendRequest` emitted by fixpp. *(This is the true close-out — the defect was found live and must be confirmed closed live.)*
- **SC-002**: Both role witnesses pass — the acceptor discriminating triple (next_inbound==2 AND reply.MsgSeqNum==1 AND reply.789==2, 027-on) AND the initiator witness (next_inbound==2 + harm-repro). Each asserts INV-H1 directly on the store (`store == manager == 2`). The **fault-injection witness** (persistent store, store-reset failure → Disconnected, persist-to-2 not reached, no `store > manager`) passes on both arms (FR-010 soundness proof).
- **SC-003**: The blast radius is updated in one pass and the full 6-preset verify matrix (incl. all sanitizers) is green: (a) the **6 value-pins** across features 013/024/027/029 (and `reset_seqnum_policy_matrix`) that pin the off-by-one *value* flip to the corrected expectations (one of them — the 029 hydrate W9b — has **two** sub-assertions that both flip: `next_inbound` 1→2 and `durable_inbound` 1→2; another — `BilateralStrict_Initiator_CountersResetToOne` — pins the **initiator** arm and flips `next_inbound` 1→2); (b) the merged 024 *contract* witness (5) `ResetOnLogon_Off_Inbound141_StoreFailure_StillActive` (`tests/session/test_reset_on_lifecycle.cpp:531-558`) is **split** — its factory inherits the default `yields_persistent_store()==true`, so under FR-010 it now asserts **Disconnect** on the persistent path, and a NEW sibling witness with a `yields_persistent_store()==false` factory retains the **stay-Active** characterization for non-persistent stores. (6 value-pins + 1 contract-witness split = 7 pins total.)
- **SC-004**: No change to any non-received-141 path: the `reset_on_logon=true` knob path and all steady-state sequencing remain byte-identical (verified by the unchanged remainder of the regression suite).

## Known Blast Radius (pins of the defect to correct)

Six existing tests currently **pin the defective value** (next-expected-inbound = 1, or `789=1`). These are stale pins of the off-by-one and must flip to the corrected value; each must be individually re-read to confirm it pins *this* case and not a distinct one:

1. `reset_seqnum_policy_matrix` (`tests/session/test_reset_seqnum_policy_matrix.cpp`) — `Bilateral{Strict,Lenient}` / `Unilateral_Acceptor_CountersResetToOne` (×3): `next_inbound` 1 → 2.
2. `persistent_seqnum_hydrate` (`tests/session/test_persistent_seqnum_hydrate.cpp`) — `Acceptor_ResetLogon_InboundSeedWithheld_NoTooLowFatal` / W9b (×1 test, **two** sub-assertions): `next_inbound == 1` (`:1587`) → 2 **AND** `store->durable_inbound == 1` (`:1610`) → 2 (under the corrected persist-to-2 fix both flip; the "reset won over hydrate" comment inverts — the consumed-Logon advance now survives the reset).
3. `next_expected_msgseqnum` (`tests/session/test_next_expected_msgseqnum.cpp`) — `Reset.AcceptorReplyReceived141_Advertises1` (`:1441`, ×1): advertise `789` 1 → 2 (and rename to `_Advertises2`).
4. `reset_seqnum_policy_matrix` (`tests/session/test_reset_seqnum_policy_matrix.cpp`) — `BilateralStrict_Initiator_CountersResetToOne` (`:593-594`, ×1): `next_inbound` 1 → 2. This is the **initiator** off-by-one value-pin — it drives the same `peer_ack_sent_reset_flag` initiator arm that FR-009 corrects, so under 030 its `EXPECT_EQ(next_inbound_unsafe(), seqnum_t{1})` flips to 2 (matching the FR-009 initiator witness for the identical path).

### Contract-amendment pin (a different category from the 6 value-pins)

A **7th** pin is the merged 024 *contract* witness — it pins a behavior, not a counter value, so it is tracked separately from the 6 value-pins above:

5. `reset_on_lifecycle` (`tests/session/test_reset_on_lifecycle.cpp:531-558`) — `ResetOnLogon_Off_Inbound141_StoreFailure_StillActive` injects `fail_next_reset()` on the received-141 path and asserts the session **stays Active** under the 024 FR-001/C2.6 I-07 logged-then-proceed contract. Its `StoreDoubleFactory` does **not** override `yields_persistent_store()`, and the base default is `true` (`include/fixpp/session/message_store_factory.hpp`), so the test runs on a **persistent** store. Under FR-010 the fatal-when-persistent flip changes its outcome Active→Disconnected. **Split into two:** the persistent variant flips to assert **Disconnect** (+ error propagated); a NEW sibling witness with a `yields_persistent_store()==false` factory **retains** the stay-Active characterization for non-persistent stores. This is the **024 FR-001/C2.6 I-07 contract amendment** (persistent received-141 reset failure now disconnects; non-persistent keeps stay-Active).

## Assumptions

- **QuickFIX-cpp/J are the conformance oracle**, grounded in the source already inspected during root-cause analysis. They (and the FIX session protocol) treat the `141=Y` Logon as a consumed in-sequence message at seq 1.
- A `Logon` carrying `141=Y` is sent with `MsgSeqNum=1`, and consuming it advances next-expected-inbound to 2. **Confirmed** by the reference-engine sweep in Clarifications (QuickFIX-cpp `nextLogon` reset-then-increment; QuickFIX-J lines 2202-2204 explicitly infer the reset from `MsgSeqNum==1`).
- The fix rides on the existing 013/024 received-141 machinery and the 029 persistence spine; no new configuration knob is introduced.
- The acceptor and initiator received-141 paths are **separate code paths** (acceptor `NotConnected` Logon handler; initiator `peer_ack_sent_reset_flag` Logon-ack arm) with the **identical clobber**, and **both are reachable** and in scope (FR-009). The live finding and primary scenario are acceptor-side, but the initiator arm is fixed and witnessed symmetrically in the same change.

## Normative References

Per `[const §VI.5]` (refs matching the catalogue rows this fix touches):

- **`[FIX-SL §4.4.2] Using ResetSeqNumFlag(141) for 24-hour connectivity`** — the received-`141=Y` sequence-reset handshake whose post-reset next-expected-inbound this fix corrects (catalogue S-032, `spec/feature-catalogue.md:348`).
- **`[FIX-SL §4.4] Extended features for FIX session initiation`** — the reset-knob / logon-synchronization section the 013/024 received-141 machinery this fix rides on is owned by (catalogue S-017, `spec/feature-catalogue.md:37`).
- **`[FIX-SL §4.4.1] Using NextExpectedMsgSeqNum(789)`** — the 027 next-expected advertisement whose acceptor-reply `789` content corrects 1→2 as a downstream of the restored counter (catalogue S-031, `spec/feature-catalogue.md:347`).
- **`[FIX-SL §4.8.2] Request retransmission of messages`** — the `ResendRequest` (013, S-024) whose spurious emission on the peer's seq-2 message is the observable harm this fix removes.
