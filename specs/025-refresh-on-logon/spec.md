# Feature Specification: RefreshOnLogon — per-logon re-hydrate of seqnum counters from the store

**Feature Branch**: `025-refresh-on-logon`
**Created**: 2026-06-09
**Status**: Draft
**Input**: User description: "RefreshOnLogon (S-018) — a per-logon re-hydrate config knob riding on the merged 029 hydrate-on-open spine. Add an opt-in, default-OFF `refresh_on_logon` boolean to SessionConfig. When true, the session re-reads BOTH persisted seqnum counters from the MessageStore at EACH logon event, bypassing 029's one-shot INV-H3 latch. Semantic = QF-faithful store-wins (unconditional, can move counters UP or DOWN). Default-off ⇒ byte-identical no-op; non-persistent store ⇒ no-op. Operator opts in accepting active-session reconnect can regress past the INV-H1 lag (documented operator responsibility / backup-standby topology). Catalogue row S-018 [FIX-SL §4.3.12]."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Hot-standby adopts the primary's persisted counters on each logon (Priority: P1)

An operator runs a **backup / hot-standby** session topology: a primary process owns the live FIX session and advances the persisted message store; a standby process is configured identically against the **same persisted store** and is expected to take over on failover. The standby must, on each logon, **adopt whatever sequence numbers the store currently holds** — including following the primary forward (the store advanced while the standby was idle) and following the primary's reset-to-1 backward (the primary performed a sequence reset). The operator enables a per-session knob that **re-reads both persisted counters from the store at every logon** so the standby's in-memory counters always reflect the store. With the knob at its default the session hydrates its counters exactly once at cold open (current 029 behaviour) and never again.

**Why this priority**: This is the requested feature and its canonical, documented use case (QuickFIX "RefreshOnLogon … useful for creating backup systems"). It is independently valuable: it is the only way a standby can stay synchronised with a store that another process is advancing.

**Independent Test**: Configure a persistent-store-backed session with the knob enabled. Advance the store **out of band** (simulating the primary) to counters higher than the session's live in-memory values, drive a logon, and assert both in-memory counters now equal the store's (higher) values. Separately, set the store **below** the live values, drive a logon, and assert the in-memory counters now equal the store's (lower) values (store-wins, can move down). With the knob at default, assert a second logon does **not** re-read the store (counters retain their live values).

**Acceptance Scenarios**:

1. **Given** a persistent-store session with the refresh knob enabled and the store advanced out of band to counters higher than the live in-memory values, **When** a logon occurs, **Then** both in-memory counters are set to the store's (higher) values.
2. **Given** a persistent-store session with the refresh knob enabled and the store set out of band to counters lower than the live in-memory values, **When** a logon occurs, **Then** both in-memory counters are set to the store's (lower) values (store-wins, moves down).
3. **Given** a persistent-store session with the refresh knob at its default (off), **When** a second logon / reconnect occurs, **Then** the store is **not** re-read and the in-memory counters retain their live values (029 one-shot behaviour, unchanged).

---

### User Story 2 - Default-off, byte-identical no-op (Priority: P1)

An existing operator who does not opt in must see **no change whatsoever**. With the knob at its default (off), seqnum hydration behaves exactly as 029 ships today: a single cold-open hydrate, no re-hydrate on any subsequent logon/reconnect, and the outbound wire is unchanged. A session backed by a **non-persistent** store (e.g. an in-memory store) is a no-op even with the knob enabled.

**Why this priority**: Zero regression for every existing session is co-equal with the feature itself — the knob is opt-in and the established 029 one-shot hydrate path must be untouched when the knob is at default.

**Independent Test**: With the default configuration, run the full existing session/establishment/recovery/029-hydrate regression suite and assert every witness remains green; assert the new field defaults to off. Separately, enable the knob on a **non-persistent**-store session and assert no store read occurs and behaviour is unchanged.

**Acceptance Scenarios**:

1. **Given** the default configuration, **When** the full existing session/recovery/hydrate regression suite runs, **Then** every witness remains green and behaviour is byte-identical to the pre-feature baseline.
2. **Given** a freshly default-constructed session configuration, **When** the new field is read, **Then** it holds the off value (refresh disabled).
3. **Given** the refresh knob enabled on a session backed by a non-persistent store, **When** a logon occurs, **Then** no store read is performed and behaviour is byte-identical to the knob-off path (carries over 029 INV-H4).

---

### User Story 3 - Refresh never produces a malformed reset Logon and never masks a peer reset (Priority: P2)

When the refresh knob is enabled, the re-hydrated (possibly non-1) counters MUST compose correctly with the existing logon establishment paths: (a) a session whose effective reset policy forces a reset-to-1 (e.g. `bilateral_strict`, or the `reset_on_logon` knob) MUST NOT emit a Logon that announces `ResetSeqNumFlag(141=Y)` while carrying a non-1 body sequence number (a malformed, self-contradictory Logon); (b) an acceptor that receives a peer **reset** Logon (`34=1, 141=Y`) MUST still honour the peer's reset — the refresh MUST NOT leave the inbound counter at a non-1 store value that would reject the peer's `34=1` as too-low.

**Why this priority**: These are the two correctness interactions with the established 024/013/027 logon FSM. They are not the feature's primary value but the feature is incorrect without them.

**Independent Test**: (a) Enable refresh on a `bilateral_strict` initiator with the store holding non-1 counters; drive the logon and assert the emitted Logon is internally consistent — either reset-to-1 (`141=Y` AND body `34=1`) or no-reset (`141` absent AND body carries the hydrated value), never `141=Y` with a non-1 body. (b) Enable refresh on an acceptor with the store holding non-1 inbound; feed a peer reset Logon `34=1, 141=Y`; assert the acceptor honours the reset (reaches Active, peer `34=1` accepted) rather than rejecting it as too-low.

**Acceptance Scenarios**:

1. **Given** the refresh knob enabled on a `bilateral_strict` session with non-1 persisted counters, **When** it emits a Logon, **Then** the Logon is internally consistent (reset-to-1 with `141=Y`+body `34=1`, OR no-reset with the hydrated body and no `141=Y`) — never `141=Y` with a non-1 body.
2. **Given** the refresh knob enabled on an acceptor with non-1 persisted inbound, **When** a peer reset Logon (`34=1, 141=Y`) arrives, **Then** the peer's reset wins, the session reaches Active, and the peer's `34=1` is accepted (not rejected as too-low).

---

### Edge Cases

- **Store-wins can move counters down** (clarified intent): refresh sets the in-memory counters to the store's values **unconditionally**, which may be lower than the live values. This is deliberate (QuickFIX parity; required so a standby can follow a primary's reset-to-1). It is NOT advance-only / `max`.
- **Active-session reconnect regression is an operator responsibility** (known limitation): on an **active** session (one advancing its own counters), a reconnect refresh can pull a counter **below** the live value past the INV-H1 lower-bound lag window (the deliver-then-persist window, or an un-persisted GapFill jump where the store deliberately lags the manager). The operator opts into this by enabling the knob; the documented guidance is to enable `refresh_on_logon` **only on backup/standby topologies** where another process owns store advancement. This matches QuickFIX, which likewise refreshes unconditionally and documents the feature for backup systems.
- **Default-off ⇒ 029 one-shot unchanged**: with the knob off, the existing 029 cold-open one-shot hydrate (latched, never re-runs on reconnect — INV-H3) is entirely unmodified.
- **Non-persistent store ⇒ no-op**: a session whose store factory reports a non-persistent store performs no store read on refresh (carries over 029 INV-H4), even with the knob enabled.
- **Refresh vs `reset_on_logon` (024)**: when `reset_on_logon` is also enabled, the reset-to-1 takes precedence over the refresh — the session logs on reset to 1 (`141=Y`, counters 1). Refresh's hydrated value is overridden by the stronger reset semantics (QuickFIX `generateLogon` ordering: refresh, then reset, then the reset-flag decision). No wire contradiction arises.
- **Refresh vs `bilateral_strict` policy**: `bilateral_strict` emits `141=Y` on every Logon independent of the reset knobs. When refresh is enabled under `bilateral_strict`, the reset-to-1 semantics of `bilateral_strict` take precedence (the Logon resets to 1: `141=Y`, body `34=1`), so refresh is effectively moot on that path and no self-contradictory Logon is produced (US3 / FR-008).
- **Refresh vs received-141 reset (013/024 acceptor)**: the acceptor's existing received-141 reset (a peer sending `34=1, 141=Y`) wins over the refresh; the inbound seed from the refresh MUST be withheld/overridden on a peer reset Logon so the peer's `34=1` is accepted (this is the same reset-Logon inbound-withhold the 029 cold path already performs — RC-1; FR-009).
- **Outbound construction otherwise unaffected**: apart from sampling the (possibly refreshed) outbound counter into the Logon body, the knob does not change message construction; it governs only the seqnum-counter hydration at logon.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST expose a **per-session configuration knob** (`refresh_on_logon`) controlling whether the persisted seqnum counters are re-read from the message store at each logon, **defaulting to off** — matching QuickFIX `RefreshOnLogon` (default off).
- **FR-002**: When the refresh knob is enabled and the session is backed by a **persistent** store, the session MUST re-read **both** persisted counters (inbound and outbound) from the store **at each logon event** (cold open, and every subsequent reconnect/logon) and set the in-memory counters to the store's values. This bypasses the 029 one-shot latch (INV-H3) — the hydrate runs on every logon, not just the first.
- **FR-003**: The refresh MUST be **store-wins / unconditional**: the in-memory counters are set to the store's values whether that moves them up or down. The system MUST NOT clamp the refresh to advance-only (`max(store, live)`).
- **FR-004**: When the refresh knob is at its default (off), seqnum hydration MUST be byte-identical to current 029 behaviour: a single cold-open one-shot hydrate that never re-runs on a subsequent logon/reconnect (INV-H3 unchanged).
- **FR-005**: When the session is backed by a **non-persistent** store (the store factory reports non-persistent), the refresh MUST be a no-op (no store read) even with the knob enabled — carrying over 029 INV-H4.
- **FR-006**: A store **read failure** during a refresh MUST be handled by the existing 029 hydrate read-failure disposition (fatal disconnect, no partial seed); the refresh path MUST NOT introduce a new error slot or a different failure disposition.
- **FR-007**: The knob MUST govern only the **seqnum-counter hydration at logon**; it MUST NOT introduce any new wire field, MUST NOT change outbound message construction beyond sampling the (possibly refreshed) counters, and MUST NOT alter inbound validation rules.
- **FR-008**: When the refresh knob is enabled, the feature MUST NOT produce an internally inconsistent (malformed) Logon. Specifically, a session whose effective reset policy/knob forces a reset-to-1 (`bilateral_strict`, or `reset_on_logon`) MUST reset to 1 (the reset takes precedence over the refresh), emitting `141=Y` with a body sequence of 1; a session that does NOT reset MUST emit a Logon carrying the hydrated body sequence with `141` absent. The feature MUST NEVER emit `141=Y` alongside a non-1 body sequence. (QuickFIX-faithful: `generateLogon` orders refresh → reset → reset-flag-decision, and only flags reset when counters are 1.)
- **FR-009**: When the refresh knob is enabled on an **acceptor**, a received peer **reset** Logon (`34=1, 141=Y`) MUST still be honoured — the inbound seed produced by the refresh MUST be withheld/overridden on the peer-reset arm so the existing 013/024 received-141 reset establishes `{1,1}` and the peer's `34=1` is accepted (not rejected as too-low). The refresh MUST NOT precede / mask the received-141 reset arm. (This is the same reset-Logon inbound-withhold the 029 cold path already performs — RC-1.)
- **FR-010**: When the refresh knob is at its default (off), the session MUST be byte-for-byte identical to current behaviour on every path — zero regression for existing sessions.
- **FR-011**: The feature MUST NOT introduce a new error slot, a new wire field, a codegen-emitter change, or a C-ABI surface change; it adds one additive `SessionConfig` field (C++-only value type; struct-layout change requiring a normal source rebuild) and reuses the existing 029 hydrate machinery.
- **FR-012**: The feature MUST surface a **feature-catalogue row** for S-018 (RefreshOnLogon) and a behaviors-and-limitations entry documenting the active-session-regression operator responsibility (the standby-only guidance).

### Key Entities *(include if feature involves data)*

- **`refresh_on_logon` config knob**: additive per-session boolean (default off) selecting whether the persisted seqnum counters are re-read from the store at each logon.
- **Persisted seqnum counters**: the inbound and outbound next-expected/next-send values held by the message store; the source the refresh reads (store-wins).
- **In-memory seqnum counters (manager)**: the live next-expected/next-send values the session uses; the target the refresh overwrites.
- **029 hydrate machinery**: the existing cold-open hydrate path (one-shot latch / persistent-store discriminator / read-failure disposition) that the refresh reuses with the one-shot latch bypassed.
- **Effective reset policy/knob**: `bilateral_strict` policy and the `reset_on_logon` (024) knob, whose reset-to-1 takes precedence over the refresh (FR-008).
- **Received-141 reset arm (013/024 acceptor)**: the existing peer-reset path the refresh must not mask (FR-009).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With the refresh knob enabled on a persistent-store session and the store advanced out of band **above** the live values, a logon sets both in-memory counters to the store's higher values (verified by an automated test asserting counter state after logon).
- **SC-002**: With the refresh knob enabled on a persistent-store session and the store set out of band **below** the live values, a logon sets both in-memory counters to the store's lower values — store-wins moves down (verified by an automated test).
- **SC-003**: With the refresh knob at default (off), a second logon/reconnect does **not** re-read the store and the in-memory counters retain their live values (verified by an automated test asserting no store read + retained counters); 100% of existing session/recovery/029-hydrate regression witnesses remain green and behaviour is byte-identical to the pre-feature baseline.
- **SC-004**: With the refresh knob enabled on a **non-persistent**-store session, a logon performs no store read and behaviour is byte-identical to the knob-off path (verified by an automated test counting store reads = 0).
- **SC-005**: With the refresh knob enabled on a `bilateral_strict` session holding non-1 persisted counters, the emitted Logon is internally consistent (reset-to-1: `141=Y` + body `34=1`, OR no-reset: hydrated body + no `141`) and never `141=Y` with a non-1 body (verified by an automated test inspecting the emitted Logon).
- **SC-006**: With the refresh knob enabled on an acceptor holding non-1 persisted inbound, a peer reset Logon (`34=1, 141=Y`) is honoured — the session reaches Active and the peer's `34=1` is accepted (not rejected as too-low) (verified by an automated test).
- **SC-007**: A store read failure during a refresh produces the existing 029 fatal-disconnect disposition with no partial seed (verified by a fault-injection test), introducing no new error slot.

## Assumptions

- **Default off** ⇒ the field defaults to the value that preserves today's (029) behaviour: a single cold-open one-shot hydrate, never re-hydrated. The knob is opt-in; the feature is a byte-identical no-op when unset. This is an EXPLICIT per-field default (no-implicit-default, [const §XII.5]). (Polarity matches the other false-default additive knobs 021/022/024/026/027.)
- **QuickFIX-faithful store-wins (settled — see Input / locked decision D-RoL-1)** — the refresh mirrors QuickFIX-cpp `RefreshOnLogon`: `Session::refresh()` is called unconditionally (when the knob is set) at the connect / receive-Logon / send-Logon points; counters are set to the store's values whether up or down. The documented purpose (QFcpp NEWS) is hot-standby/backup systems. fixpp adopts the same store-wins contract, **including the active-session-regression risk as an operator responsibility** (enable on standby topologies only).
- **Rides on the merged 029 spine** — the bidirectional hydrate-on-open machinery (store→manager `hydrate`, persistent-store discriminator, read-failure disposition, acceptor reset-Logon inbound-withhold) already exists from 029; 025 reuses it, the only delta being that the one-shot latch (INV-H3) is bypassed when the knob is enabled so the hydrate re-runs on each logon.
- **Reset precedence resolves the 141 interaction** — `bilateral_strict` / `reset_on_logon` reset-to-1 takes precedence over the refresh (no malformed Logon, FR-008); the acceptor received-141 reset wins over the refresh inbound seed (FR-009). These compose with the established 013/024 logon FSM rather than changing it.
- **No advance-only clamp** — refresh is store-wins, not `max(store, live)`; clamping would break the standby-follows-primary-reset-to-1 case (DEAD alternative).
- **Inbound-not-persisted is DISCHARGED** — the prior (parked) 025 design's blocker (the store did not track the inbound counter) was closed by 029 (T034); refresh reads a store that now tracks both counters. (Do not re-litigate.)
- **No new wire field, no new error slot, no codegen change, no C-ABI change** — one additive C++-only `SessionConfig` value-type field reusing 029 machinery; struct-layout change requiring a normal source rebuild.
- **Own Gate A** — this slice touches the session logon FSM (seqnum hydration at each logon) and therefore runs its own Gate A design review.

## Normative References

- **QuickFIX `RefreshOnLogon` setting** — the parity authority for FR-001/002/003: when on, the engine reloads the session's sequence numbers from the persisted store whenever the session logs on (`Session::refresh()` called at `setResponder` / `nextLogon` / `generateLogon`); QFcpp NEWS documents the purpose as creating backup systems.
- **`[FIX-SL §4.3.12] Synchronization after successful logon`** — the catalogue-match primary for S-018: re-synchronising sequence state at logon.
- **`[FIX-SL §4.4] Logon process`** — the establishment path the refresh composes with (the `ResetSeqNumFlag(141)` interaction, FR-008/009).
