# Contract: inbound frame dispositions (093)

This contract builds on 092's `contracts/unparseable-frame-disposition.md`, whose C-2 rows for faulty
frames stand except where C-2 below changes them. Requirement IDs are spec.md's. C-1, C-4, C-6 and C-7
were re-derived at Gate A round 1. C-1 W-2, C-3 I-5, C-4, C-6, C-7 and C-8 were revised at round 2
(plan.md `## Gate A`).

## C-1: Framing on the session's inbound path (FR-001, FR-002, FR-008, FR-013, FR-052)

**Configuration.** Two Framers run with `resync_on_garble = true` and `max_frame_bytes = L`:
- the read pump's, over the carry the Session allocated at `open()` (L plus one read; E-2);
- the acceptor's first-frame read's, over its existing `max_bytes + 1` carry. L is known before the
  Session exists, because it is computed from the registry entry's `SessionConfig` (E-2's
  `inbound_limit_for`).

**Outcome at a candidate frame start.** The first column is what the bytes at the candidate hold:

| Bytes at the candidate | Framer outcome | Pump action |
|---|---|---|
| a well-formed frame of ≤ L bytes | frame | deliver to `on_inbound_frame` |
| a BodyLength over L, or a frame length over L (both known before the CheckSum is read) | `wire_frame_too_large` | **close** (`close(terminal)`), with a log record carrying the kind and L (no `SessionEvent`: E-5 adds none for it; plan OD-24) |
| carry overflow | `wire_frame_too_large` | **close**, as above |
| not `8=` at a frame boundary (leading junk) | garble, kind `wire_framing_resync` | counted in the feed's summary; search resumes |
| a BeginString value longer than the BeginString cap (W-2), with no SOH, at any candidate (a frame boundary included) | garble, kind `wire_framing_resync` | as above |
| a bad, missing or non-digit 9, a BodyLength digit run longer than the digit cap (W-2), or `10=` not at the counted offset (a wrong count, or CheckSum not last) | garble, kind `wire_invalid_body_length` | as above |
| `10=` at the counted offset but its value not 3 digits, or not SOH-terminated | garble, kind `wire_checksum_mismatch` | as above |
| a structurally complete frame whose CheckSum value is wrong | garble, kind `wire_checksum_mismatch`, **extending through that frame's own end** | as above |
| a partial frame (BodyLength not yet satisfied) | none | read more |

"Structurally complete" means that `8=`, `9=<digits>`, `10=` at the counted offset, three digits and a
trailing SOH are all present.

In the pump, the carry-overflow row is unreachable once the carry is L + R (plan OD-23). A pending
candidate is at most L bytes, because its header is capped and an over-L BodyLength is refused before the
CheckSum, and one read adds at most R. The row stands for the Framer's own contract, where Framer-level
cells drive it.

The close rows apply at every candidate, including one the resync search found. An over-L BodyLength
inside a garbled region therefore closes; it is not disregarded (FR-013 prevails over FR-001). In the
pump, no guard or handler reads any field of a frame refused this way. The Framer itself reads only
the framing fields that give the frame's length.

**Resync rule.**
- **Frame start.** At a frame boundary (the first byte the Framer has received, or the first byte
  after a complete frame), framing is as today: `8=` followed by any BeginString value. After a
  garble, the next frame start is the next occurrence of `8=FIX`, whatever byte precedes it,
  searched from the byte after the garble's first byte, or, for a structurally complete frame whose
  CheckSum value is wrong, from the byte after its own end (Extent, below). The byte after such a
  garble is therefore a search position, not a frame boundary, and the bytes the search passes over
  belong to that garble's region. `FIX` is the prefix every supported profile
  identifier shares. The Framer reads those three BeginString bytes during a search, and no others.
- **Extent.** A garble runs from its candidate start to the next frame start, or through its own end
  for a structurally complete frame whose CheckSum value is wrong. A well-formed frame lying wholly
  after a garble is never lost. A frame lying inside the extent of such a wrong-CheckSum frame is part
  of that garble. Each failed candidate, and each run of leading junk at a frame boundary, is one
  region: a candidate the search finds right after another region opens its own region when it fails,
  so two adjacent garbles count two.
- **State across feeds.** The Framer keeps one flag, `searching`, and nothing else besides the carry.
  When a feed ends during a search, the region stays open, the next feed continues it, and it is not
  counted again. The carry retains only the trailing bytes that are a proper prefix of `8=FIX`, at most
  four. The frames produced and the regions counted therefore do not depend on how the stream is
  segmented. The per-call summaries do, and so do the events built from them (FR-002, FR-003).
- **Ordering.** A call that has produced a frame stops before resolving a later garble, or a later
  `wire_frame_too_large` outcome (plan.md OD-20), and leaves it for the next call. Garbles reported by a call therefore precede every frame the same call produces.
- **Reporting.** Each call resets and then fills one `garble_summary` (E-1): the number of garbled
  regions it opened, the first one's kind, and the bytes it discarded. Bytes discarded while
  continuing a region opened by an earlier call add to `discarded` with `regions == 0`.

**Bounded work (FR-052, FR-002).**
- **W-1.** The carry is compacted (its consumed prefix erased) only when the incoming bytes would not
  fit after the bytes it already holds. A carry-only feed, which is the pump's drain, never compacts.
  The unconsumed bytes are at most one pending candidate, which is at most L bytes, and the carry holds
  L plus one read. So each compaction moves at most L bytes and follows at least one read's worth of
  appended bytes since the previous one, however the peer segments the stream: one-byte reads
  included.
- **W-2.** In resync mode the header is read with bounded work at every candidate, a frame boundary
  included. Two caps bound the bytes read, and both cap the **encoded** length, not the value.
  - **The BeginString cap** is `Framer::Config::max_begin_string_bytes`. Its default is the length of
    the longest supported profile identifier. The session's two Framers set it to the larger of that
    and the configured `begin_string`'s length (plan.md OD-16), so a session configured with a longer
    BeginString still frames its own frames. A longer value with no SOH is a garble (the §4.5.2
    criterion "not a defined profile identifier").
  - **The digit cap** bounds the BodyLength digit run, leading zeros included. It is a named constant,
    `kBodyLengthDigitCap`. Its conditions: a floor, at least the decimal width of the largest L plus an
    allowance for the zero padding a counterparty legitimately sends (FIX `int` permits leading zeros);
    and a ceiling, three times the decimal width of the largest L (262144), so that the cap's term in
    the bound below stays a small constant. This is the one place the ceiling is stated. The
    allowance is derived at implementation by research R-2's recipe and recorded there, never in a
    comment. A longer run is a garble of kind `wire_invalid_body_length`, not a close, because its
    bytes are not over L. Within the cap, the value is read digit by digit and refused as over L as
    soon as it exceeds L.
  - Without the caps, a boundary `8=` with no SOH, or a `9=` followed by an unbounded run of zeros,
    would be rescanned from the candidate's start on every feed. A persisted scan cursor was rejected,
    because it breaks the single-flag state rule above.
- **W-3.** A CheckSum is summed only over a structurally complete candidate, and such a candidate is
  then consumed whole, either as a frame or as a garble. Summed regions are therefore disjoint.
- **W-4.** Nothing is allocated. The scan runs in place over the carry and the incoming bytes.

**The bound.** Let R be the pump's read size. Amortised over any segmentation of the stream, the
Framer's work per received byte is at most a constant times (1 + L ÷ R + the BeginString cap + the
digit cap):
- W-1 contributes L ÷ R;
- W-2's rescan of a pending candidate's header on each feed contributes the two caps;
- W-3 contributes a constant, because each byte is summed at most once.

The counted-work instrument charges one unit per byte read, summed or moved. The constant is derived
from W-1 to W-3 at implementation and recorded with its derivation in research R-2. It is the value
Q-4's cells and `fuzz_wire_framer`'s resync arm assert against, and quickstart §2's work-bound
mutants (compaction, the caps, the nested sums) must exceed it.

The BeginString cap depends on the configured `begin_string`, so the bound does too. The configured
length is bounded on every path where the pump runs over peer bytes, by three things 093 leaves as
they are (the Logon buffer size, the first-frame budget, and `SessionId` equality), and not by L:
- **Initiator.** The pump starts only after the Logon is sent. `build_logon` writes it into a buffer
  of `Session::kMaxMaskableLogonBytes`, and a BeginString too long to fit fails the build, so the role
  loop returns before the pump.
- **Acceptor.** A connection reaches a Session only if its first frame carries the configured
  BeginString (`SessionId`'s defaulted equality over all three fields), and that frame must fit in the
  first-frame read's `kFirstFrameMaxBytes`, which also bounds that read's total input. After `open()`
  the reply Logon is built into a buffer of the same Logon size; a build that fails leaves the session
  Disconnected while the pump still runs, with the configured length already bounded by the
  first-frame budget.

Re-derive both values from `include/fixpp/session/session.hpp` and `run_accept_loop` in
`src/session/engine.cpp`, and the equality from `include/fixpp/session/engine.hpp`. If a change lets a
session reach the pump with a configured BeginString not bounded by one of them, this bound no longer
holds and the cap needs its own ceiling. Quickstart §1 has the adversarial cells, and §2 has the
mutants and the counted-work instrument.

**Every other Framer caller** (reify, the re-framing parse helpers, fuzz, benches, tests) keeps
`resync_on_garble = false` and is unchanged byte for byte. That includes the order of the
`frame_len > max_frame_bytes` check. Its move ahead of the CheckSum, and the caps of W-2, apply only
with the flag on (plan.md OD-4).

## C-2: Disposition at `on_inbound_frame`, in this order, in each arm (FR-003, FR-004, FR-005, FR-020, FR-030)

| Step | Condition | Disposition | States |
|---|---|---|---|
| 1 | the scan's `msg_type_is_third == false`, faulty or not | **disregard as garbled**: counted, evented and logged as one garble of kind `wire_header_out_of_order` (FR-003). Then return success: no Reject, NextNumIn kept, no liveness refresh | every state except Disconnected |
| 2 | the arm's superseded predicate (`closing`, or `Engine::stop()`'s engine-stop flag, or a moved FSM state) and state ∈ {NotConnected, LogonSent} (plan OD-28) | return success with no effect (FR-030, FR-041) | NotConnected, LogonSent |
| 3 | the scan found a fault | 092's C-2 rows, except D-8, which step 1 makes unreachable | as in 092 |
| 4 | otherwise | LogonReceived and Active: refresh inbound liveness (FR-020), then the existing guards and handlers. NotConnected, LogonSent and LogoutSent: the existing arm | each arm |

- Step 1 comes before step 2 because garbled-frame accounting is not an arm effect (FR-030's
  carve-out; plan.md OD-7).
- Frames over L never reach `on_inbound_frame` (C-1). The Active-only advertised-MaxMessageSize check
  above the switch is deleted, because C-1 replaces it.
- `validate_inbound_messages = true` does not change step 1. The validator's Step 0 (373=14 for this
  shape) becomes unreachable from the session.
- Before Active, step 1 is a disregard, where 092's D-1 and D-2 were refusals. C-4 bounds the wait it
  creates.
- Disconnected stays "ignore every frame", which is both 092's and today's rule. A frame there is not
  scanned, so a criterion-3 garble in Disconnected is not counted. A Framer garble there is counted,
  evented and logged: the pump runs while the transport is open, whatever the FSM state, and accounts
  every feed's summary (C-1, E-4). That difference is by design: only the arm reads field 3.
- Step 1 also takes 092's D-9 frames (LogoutSent) whose third field is not 35. They stay disregarded,
  as D-9 says, and are now counted, evented and logged as garbles. D-3 to D-7 need field 3 to be 35,
  so step 1 never takes them, and they stand unchanged.

## C-3: Parse capacity (FR-010 to FR-015)

- **I-1.** For every frame the pump admits (C-1: ≤ L and well framed), every inbound parse site's
  `Parser::parse` succeeds without `out_of_memory` and without `err_offset_table_full`, on every lane.
  The sites are the validate gate in three arms and dispatch at seven sites. Re-derive them with 092
  contract C-6's command.
- **I-2.** The parse's resource is a fresh `monotonic_buffer_resource` per call over the session's
  B(L) span. Its upstream is a spill witness, so a spill is detectable on every lane, MSVC debug
  included.
- **I-3.** No inbound parse nests inside another. Callbacks are synchronous, and the callback scope
  asserts it.
- **I-4.** 092's late-parse close (`close_on_late_parse_failure_`) stays as a defence. Its reachability
  is shown only through `session_test_access` (plan OD-24): the `LateSite_*` cells lower the session's
  entry cap (`lower_inbound_entry_cap`), which fails the parse on every lane; Q-15 shrinks the buffer's
  bytes (`shrink_parse_buffer`), which fails the parse only where the spill witness is null, and on
  MSVC debug records the spill instead.
- **I-5.** Lazy reads inside a callback draw on `kCallbackReadHeadroom`, in the same span. Exhausting
  it in a read that reports a status (every row below except the C cursor shells) never ends the
  session. The C cursor shells are the exception: an exhaustion there ends the session, or the
  process terminates, as it does today (L-17, fixpp#541). The reports below hold on the lanes where
  the spill witness (I-2) is null; on MSVC debug it forwards to the heap and records, so a read past
  the headroom succeeds and the spill is recorded instead. `kCallbackReadHeadroom` must meet the sizing
  condition data-model E-2 states, checked at implementation; L-17 records any frame for which it
  fails. What the failed read reports depends on the API's declaration, and 093
  changes only the `unknown_fields()` row. Re-derive the rows from the declarations in
  `include/fixpp/wire/offset_table.hpp` and `include/fixpp/wire/parser.hpp`, and from the group getters
  in `src/capi/message_read.cpp`.

  | Lazy read | Failure on exhaustion | 093 |
  |---|---|---|
  | `OffsetTable::group_slices()`, and the typed `group<>()` over it | an empty span, indistinguishable from an absent group. The public wrapper discards the internal status | unchanged |
  | `OffsetTable::nested_group_slices()`, and `fixpp_group_get_nested_group` | `alloc_failed`, which the C getter returns as `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` | unchanged |
  | `fixpp_msg_get_group` | it calls the degrading `group_slices()`, so exhaustion is reported as `FIXPP_ERR_TYPE_MISMATCH`, a misreport | unchanged; disclosed (L-5) |
  | the C cursor shells that `fixpp_msg_get_group` and `fixpp_group_get_nested_group` allocate from the parse arena | the allocation has no catch, so a `bad_alloc` escapes the C function | unchanged; disclosed (L-17), fixpp#541 (unconfirmed; reproduce first) |
  | `MessageView::unknown_fields()` | **today:** it is `noexcept` and pushes into a pmr vector over the parse resource, whose upstream is null on every lane except MSVC debug, so a `bad_alloc` reaches `std::terminate` (fixpp#540, unconfirmed). **093:** an internal catch, a body-only change that keeps `noexcept`. It clears the partial list, marks the list built so later calls return the same empty view, and returns an empty view | changed (FR-015) |

  An empty `unknown_fields()` view after exhaustion cannot be told from a frame with no unknown
  fields (L-5).
- **I-6.** The admin and outbound parse sites keep their stack arenas, and 093 does not change them.
  Their size is not derived from what those sites parse. A dense outbound body near `Session::send`'s
  body-buffer size can need more offset-table space than the stack arena holds. Those sites ignore
  `parse_failed`, so the send callback is then skipped without notice. This is pre-existing and
  outside #515, which is inbound-only. It is disclosed (L-14) and filed as a follow-up. Research R-3
  has the derivation.

## C-4: Establishment timeout (FR-006, FR-007)

Let T be `logon_timeout_ms`. The establishment deadline is absolute, measured on `engine_cfg.clock`.
It ignores `SessionConfig::clock_override`, because phase (a) begins before a Session exists. A
mock-clock cell therefore drives `engine_cfg.clock`. `close()`'s wait (C-6) runs on the Session's
`effective_clock_`, which differs whenever an override is set, so the two bounds are independent.

**Expiry is a checked invariant, not the outcome of a race.** At every loop head of the read pump
before the first Active, the deadline is tested as `steady_now() >= deadline`:
- before each read;
- before each frame is delivered to `on_inbound_frame`, in the read loop's drain and in the drain of
  the first-frame surplus (`initial_bytes`) alike.

A read that blocks is raced against `await_deadline`, but the race only wakes the blocked read. When
both arms are ready, the order asio completes them in decides nothing, because the next loop-head test
sees the passed deadline. A peer that keeps the socket readable past T is therefore closed at the
first loop head after T: no frame is delivered once the deadline has passed, and the overrun is at most
the processing of the one frame already in delivery.

These loop heads are the pump's (phase (b)). Phase (a)'s first-frame read keeps its own read loop,
which is unchanged. A tie there cannot outrun the bound, because its byte budget, which counts
discarded bytes, caps the number of iterations.

**Which first-frame bound fires first.** The read feeds the Framer before it tests the budget (088's
frame-first order), so a BodyLength over L is refused in the feed that carries its digits, before
the budget fills. A frame that is too long for the budget is refused by the budget or by the read's
`max_bytes + 1` carry. Each closes the raw transport silently (L-6), so the order changes only when
the close happens.

| Role and phase | Clock starts | What bounds it | On expiry or over-budget | Observable |
|---|---|---|---|---|
| **(a) acceptor, pre-Session**: from accept until a first frame whose CompIDs match the entry | accept | the TLS handshake bound, unchanged and not shortened; then the first-frame read, bounded by `min(5 s, deadline − now)` and by its unchanged 4096-byte budget, which also counts discarded bytes. When `deadline − now` is not positive after the handshake, the accept loop closes the transport without issuing the read. The relative-to-absolute conversion inside `read_first_frame_bounded` is unchanged | the raw transport is closed. No Session exists, so there is no `close()`, no SessionEvent and no log | the peer's connection closes; nothing is recorded (L-6) |
| **(b) acceptor, Session pre-Active**: from the first frame's delivery until the first Active | accept (the same deadline) | the loop-head test; a blocked read is raced against the deadline | `session_event_establishment_timeout`, a log, then `close(terminal)` | the event; the transport closes; the FSM ends in Disconnected |
| **(b) initiator, pre-Active** | `drive_reconnect` returns, with the Logon sent | as above | as above | as above |

- **On TLS, phase (a) lasts at most `max(T, the handshake bound)`.** The handshake bound is fixed per
  listener and 093 does not shorten it, so with T below it a stalled handshake ends at the handshake
  bound, not at T. That is not a hang.

- **Which phase a garbage-only peer meets.** On the acceptor, a peer that sends only garbage never
  leaves phase (a). It is closed at the byte budget or at `min(5 s, T)`, whichever comes first, not
  at T. A peer reaches phase (b) only with a first frame whose CompIDs match: a refused Logon, or a
  non-Logon frame, either of which leaves the session pre-Active with its transport open. Every
  initiator connection is in phase (b).
- **What the deadline races.** Phase (b) races only the waits for peer bytes. The local suspensions
  inside `on_inbound_frame` (store hydrate, store reset, the Logon reply write) are not raced. The
  condition: a peer cannot hold them open. Before Active the outbound volume is the Logon reply plus at
  most a refusal, far below a socket send buffer, so a peer that stops reading cannot hold the write.
  A store operation that never completes hangs the session in any state, as it does today.
- **Sleep cancellation.** The race uses `await_deadline`, which re-arms when a clock-wide
  `cancel_sleeps()` wakes it early.
- **`Engine::stop()` during phase (b), with no reset unit in flight.** Nothing runs on an empty
  cancellation slot (only the unit's store operation does, C-6). Stop's
  step 1 cancels the pump, which is bound to the session's slot, and its step 2 closes the transport,
  so the pump's read ends and the pump closes the session as it does today on a failed read. The
  deadline adds nothing to that path: `session_event_establishment_timeout` is recorded only if a
  loop-head test saw the deadline pass before stop, so a cell must not assert either outcome for a
  stop that coincides with T.
- **After Active.** The deadline has no effect after the first Active, which the dedicated
  `reached_active_` latch records (E-6). A session with no application attached is therefore not
  closed while Active (quickstart Q-36). Phase (b) also covers a session already refused into
  Disconnected whose transport is still open (#534's pre-Active half).
- **Allocation.** The race may allocate before the first Active. Whether it does, and how much, is
  measured in the verify record, not asserted here (L-13). After the first Active the pump reads
  without the race, and that is FR-052's no-new-allocation scope (plan.md OD-22).
- **Zero refusal.** `logon_timeout_ms` defaults to 10000. Zero is refused by `register_session`, by
  `open()`, by the TOML loader and by the C setter.

## C-5: Liveness (FR-020, FR-021)

- `last_inbound_steady_` has one per-frame writer, step 4 of C-2. The seeds at `open()` and at entering
  Active stay.
- Garbled frames (C-1, and C-2 step 1) and faulty frames (C-2 step 3) never write it.

## C-6: The 141=Y reset unit, `close()` and `Engine::stop()` (FR-040 to FR-042)

> **Erratum (2026-10-04, plan.md OD-25; it supersedes the shield mechanism below).** The in-place disable at
> step 2 does not shield the store operation. `fixpp::sync::async_mutex::async_lock()` resets the awaitable
> thread's cancellation state to terminal-only after every acquisition, and enables total cancellation during
> a contended wait (`include/fixpp/core/sync/async_mutex.hpp`). `Engine::stop()` emits only total. So:
> - after the unit's first seqnum-manager lock, the disable has already been replaced;
> - a store operation that waits on a contended lock can be cancelled, whatever the disable says.
>
> The unit therefore runs **only `store_->reset_to(in, out)` through `co_spawn` with a token bound to an
> empty cancellation slot**. The spawned thread has no parent slot, so no emission reaches any operation the
> store runs, for every store kind.
> *Refined by OD-26:* immunity to stop's total emit comes from the separate awaitable thread (its slot forwards
> only through the arm's filter, which is terminal-only after the first lock). The empty slot extends it to every
> emission kind. Mutant M2a (drop the binding) is GREEN; M2b (inline) is RED. Steps up to the store operation stay inline with no suspension (L-518-1's
> uncontended grant). The step-2 disable is removed. The step-5 restore stays, because it drops any
> cancellation recorded on the arm's thread while the unit ran. The engine-stop flag (OD-15) is unchanged.
> Where the text below says "the shield", read "the empty-slot store operation".
>
> Every suspension in a Logon arm is followed by the predicate before the next effect. **That includes the
> suspensions inside `honor_peer_next_expected_` and `replay_outbound_range_` (the 789 path), and the store
> await inside the reply Logon's `store_then_emit` before its transport write** (OD-25).

**Store operation.** `MessageStore::reset_to(next_in, next_out)` has the precondition `next_in,
next_out ∈ {1, 2}`. Any other value is refused with `session_invalid_argument` and changes nothing. The
default body is `reset()` and then one `next_seqnum(dir, true)` for each target that is 2.
`MemoryStore` and `FileStore` override it as one operation under their writer lock.

**The unit, both roles, in this order:**
1. Compute the targets `(in, out)` (research R-6).
2. Shield: `co_await this_coro::reset_cancellation_state(disable_cancellation{})`. asio keeps
   cancellation state per awaitable thread, not per frame (re-derive in asio's
   `impl/awaitable.hpp`: `reset_cancellation_state` writes the bottom frame's state). The disable
   therefore covers the arm and the pump until step 5 restores it. That is why the restore is
   explicit, and why the shield is not put inside a helper coroutine.
3. Set the manager: `reset_to_one()`, `set_next_inbound(in)`, `set_next_outbound(out)`, capturing
   the first error and **not returning on it**. Each is an `async_lock` that grants inline on its
   uncontended fast path, so nothing between step 2 and step 4 suspends. The condition is L-518-1's: a
   change that makes that grant post, or that lets another holder contend for the seqnum mutex during
   a Logon, breaks it. With the shield in place, none of these awaits throws on a pending cancellation.
4. Only if step 3 succeeded: `reset_unit_in_flight_ = true; r = co_await store_->reset_to(in, out);`,
   then clear the flag and signal the unit's completion (E-10).
5. Restore: `co_await this_coro::reset_cancellation_state(enable_total_cancellation())`. Steps 3 and 4
   have no return path, so the restore always runs.
6. The existing dispositions on the captured results: a manager error, or else `r`. Their early
   returns (`Disconnected` and an error) now come after the restore.
7. The existing `logon_arm_superseded` check, which now also tests the engine-stop flag (below).

This runs on every store, volatile ones included (OD-9).

**What the shield means.**
- A cancellation emitted before or during steps 2 to 5 is not observed by the unit, which runs to
  completion.
- After the restore the pump's state is fresh, so an emission made during the unit is not replayed.
  Without more, the arm would then run its post-unit effects after `Engine::stop()`'s step 1 has run
  on the session's strand: the reset event, `toAdmin`, the reply write, the Active transition and
  `onLogon`. On the initiator, unless it honours a NextExpectedMsgSeqNum(789), nothing between the
  unit and Active writes to the socket, so stop's transport close does not prevent it (research R-9).
- **The engine-stop flag orders the arm after stop's step 1 on the session's strand** (plan.md OD-15).
  Once stop's step-1 handler has run on a session's strand, every later predicate check on that
  strand sees the flag. Effects the arm runs on that strand before the handler are ordered before it,
  as for a session that reached Active just before step 1 reached its strand, and `stop()`'s normal
  sequence (the emit, step 2's transport close, the join, then `close()`) handles that session. The
  guarantee is strand-ordered, not real-time: no flag read by the arm without mutual exclusion against
  `stop()` can give more (OD-15). It holds on the condition that every suspension in a Logon arm is
  followed by the predicate before the next effect (#518's discipline). Re-derive it transitively, not
  from the arm's own `co_await`s: read each Logon arm and every `Session` coroutine it awaits, down to
  their effects, and classify each suspension by what follows it (the evidence file's recipe,
  "`logon_arm_superseded_` sites, transitively"; plan OD-26 and its Disconnected-write census). A read
  of the arm's `co_await` sites against `grep -n logon_arm_superseded src/session/session.cpp` alone
  cannot see a suspension inside an awaited coroutine.
  `Engine::stop()`'s step 1 reads each
  entry's `session` on the control strand, where step 1 runs. Inside the `co_spawn` it already posts
  to the session strand, and before the `emit`, it sets the Session's `engine_stop_requested_` through
  `session_engine_access`. A null session has nothing to set, on the condition data-model E-13 states:
  both role loops publish `entry.session` before any frame is delivered, and a publish refused because
  stop began delivers nothing. `logon_arm_superseded` tests
  `state_ == closing`, the new flag, and the FSM state. So once the flag is set, the check in step 7 stops the arm before any
  event, callback, write or Active transition, and every other site of the predicate stops it too.
  Re-derive the sites with `grep -n logon_arm_superseded src/session/session.cpp`. The flag is only read
  inside the predicate, so it adds no return path between steps 3 and 5, and the restore still always
  runs.
- The pump learns of `Engine::stop()` through stop's step 2, which closes the transport, so its next
  read fails. It learns of `close()` through step 7's check.
- `Engine::stop()`'s join (its step 3) therefore waits for an in-flight unit's store operation, which
  is not cancellable (L-12).

**`close()`:**
- `close()` waits only when it is about to issue its teardown reset and `reset_unit_in_flight_` is set.
  The wait is event-driven. It awaits the unit's completion signal raced against
  `await_deadline(*effective_clock_, now + logon_timeout_ms)`, which re-arms on a clock-wide sweep
  (plan.md OD-1).
- On expiry it records `session_event_close_reset_wait_expired` and proceeds.
- `close()` issues no teardown reset and does not wait when neither `reset_on_disconnect` nor
  (`reset_on_logout` after a Logout) holds. The manager was already set in step 3, so the seqnum drain
  can no longer strand the restore.
- `teardown_reset_done_` keeps only its role as `close()`'s single-fire latch. The in-unit
  `teardown_reset_done_` stops are removed.
- *Note (2026-10-06, plan OD-29):* a `close(terminal)` that arrives while a `close(graceful)` is in flight
  escalates it: it ends the graceful close's grace wait, or keeps its phase 1 from starting, so the
  transport closes at once, and both callers get the in-flight result. This narrows `[2d §4.7]`'s and
  `[2d §6.5]`'s "Idempotency" bullets. The wait above is unchanged: an escalated close still runs it before
  its teardown reset. A graceful close on a closing session still joins with no side effect.

**Outcomes, for both roles, when `close()` or `Engine::stop()` begins at any point of the unit:**

| Teardown reset configured | Final durable (next-in, next-out) | Holds for |
|---|---|---|
| no | (in, out) of the unit: in = 2 when the Logon advanced; out = 2 on the initiator's own reset | every store |
| yes | (1, 1) | every store while the wait does not expire. After expiry: an overriding store whose `reset_to` holds a FIFO writer lock across the operation, because the teardown reset then queues behind it. A default-body store after expiry is not covered (L-4) |

**"Any point of the unit" reduces to one interleaving point, on three conditions.** `close()` and
stop's step-1 handler run on the session strand, so they can begin inside the unit only where the
unit suspends. The conditions (step 2 is removed, plan OD-25): step 5's `reset_cancellation_state`
completes without suspending (re-derive in asio's `impl/awaitable.hpp`); step 3 grants inline
(L-518-1's condition, above); and steps 5 to 7 hold no other `co_await` (re-derive with `grep -n
"logon_arm_superseded\|co_await" src/session/session.cpp`). Then the one point is step 4's await of
the `co_spawn`ed `reset_to` (`Session::run_reset_unit_`), which is where the cells hold the store
(quickstart Q-23, Q-26). A `close()` or stop that begins before step 3 meets the arm's earlier `logon_arm_superseded` check, on the #518 condition
above, so the unit is not entered and the table above does not apply.

`MemoryStore` meets the FIFO condition (a leading post, then its mutex). For `FileStore` the condition
is its writer `async_mutex`, and a quickstart cell measures it rather than assuming it.

**Atomicity.** Only `MemoryStore` and `FileStore` are atomic. A non-overriding store meets the table,
but a crash inside its default body can leave the intermediate state (L-3).

## C-7: Public surface and versioning (FR-007, FR-051)

**BREAKING placement rule (`[const §X.7]`).** A C-ABI behaviour change is marked BREAKING in the doc
block of each C declaration through which it is observed. `version.h`'s history block is headed
BREAKING and points to those declarations, and details an effect only where no declaration carries it. Where a change falsifies a sentence 092 put in a C-ABI 1.10 clause, that
sentence is amended in place and the amendment is marked with the new MINOR. The same holds for a
sentence any earlier MINOR put there: 091's 1.9 FR-020 sentence (a Logon with a malformed Length+Data
count "is now refused") is amended in place, because a Logon whose third field is not MsgType(35) is
now disregarded before it is interpreted (C-2 step 1). The amended 1.10
bullets are named below by their opening text, not by line number. Re-derive the sites of every
earlier MINOR's sentences with `grep -n "C-ABI 1\.[0-9]" include/fix/c_api/session.h`, and read each
earlier MINOR's history entry in `include/fix/c_api/version.h`.

**The observer set.** `version.h`'s 1.10 history entry names the calls whose result depends on the
session being logged on: `fixpp_session_is_established`, `fixpp_session_close`, `fixpp_session_send`,
`fixpp_session_register_callback` and `fixpp_session_register_send_callback`. This contract calls them
**the five observers**. A change that keeps a session up that used to end, or ends one that used to
stay up, changes the result of each. So every BREAKING row below (rows 1 to 6) is marked on all five,
unless the row writes a result-based reason for leaving one out. Re-derive the set from the 1.10
entry, not from this paragraph.

**Rows 1, 3, 5 and 6 amend no 1.10 sentence.** None describes their old outcomes: the garble close,
the carry refusal near 64 KiB, the pre-Active connection left open, and the TestRequest sent while
the peer's well-formed frames took an early return that did not refresh liveness. The 1.10 bullet on a heartbeat interval speaks of faulty frames only and stays true
(FR-021). Re-derive with the grep above and with the 1.10 entry in `version.h`.

**Rows.** The column headings are abbreviated: "C declarations (BREAKING / amended 1.10)", "Kind and
why", and "Golden / freeze".

| # | Change | C++ declaration | C declarations | Python | TOML | B&L | Kind and why | Golden / freeze | Cell |
|---|---|---|---|---|---|---|---|---|---|
| 1 | A Framer-detected garbled frame is disregarded, not session-ending. That includes two shapes that were framed before and are now garbled at framing (C-1 W-2): a BeginString value longer than the BeginString cap, which was handled as a mismatch, and a BodyLength digit run longer than the digit cap, which was accepted | `Framer::Config::resync_on_garble`, `Framer::Config::max_begin_string_bytes`, `garble_summary`, `last_garbles()`, `kBodyLengthDigitCap` (added; Q-4 is its witness); `friend struct framer_test_access;` and the private counted-work counters (added, unconditional, always compiled; they change `sizeof(Framer)`, so the change is source-compatible but not layout-neutral, and nothing a strict caller observes, data-model E-1) | BREAKING on the five observers | through C | — | B row; `L-004-4` closed | BREAKING: a session that ended stays established | none | TC 2d/2m/3b/3c/3e; Q-4 (digit cap); Q-10; Q-37 |
| 2 | A 35-not-third frame is disregarded in every state except Disconnected (which ignores every frame, C-2), in both validation modes | — | BREAKING on the five observers. Amend the 1.10 text beginning "a Logon carrying a malformed tag" at all five sites: `close`'s paragraph and the bullet lists on `is_established`, `fixpp_session_send`, `register_callback` and `fixpp_session_register_send_callback`. Such a Logon is now disregarded when its third field is not MsgType(35). The 1.10 bullet beginning "on an established session, a faulty frame whose fault comes before its MsgSeqNum(34)" stays true, so it is not amended: such a frame is still disregarded without advancing, one whose third field is not MsgType(35) now by C-2 step 1 and one whose third field is 35 by 092's D-7 at step 3, as before, and the gap handling behind its "session ends" consequence is unchanged | through C | — | B-005-7 narrowed | BREAKING: a frame that was processed, or a Logon that was refused, is now disregarded | none | TC 2t; D-8 cells; Q-37 |
| 3 | A frame over L closes in every state, the acceptor's first frame included. A frame of at most L is admitted however the stream is segmented, where today the 64 KiB carry refuses some frames near it by segmentation (research R-3) | — | BREAKING on the five observers | through C | — | B row; 070 exemption reversed | BREAKING | none | over-L cells; Q-37 |
| 4 | A late parse failure is unreachable for an admitted frame | — | BREAKING on the five observers. Amend the 1.10 bullet beginning "on an established session, a frame the header scan finds fault-free but the session cannot parse for dispatch" on `is_established`, `fixpp_session_send`, `register_callback` and `fixpp_session_register_send_callback` | through C | — | `L-092-6` updated | BREAKING (a documented effect no longer occurs for an admitted frame) | none | dense-L; FR-014 defence; Q-37 |
| 5 | A pre-Active connection closes at the establishment deadline (C-4 phase b), including one refused into Disconnected | `SessionConfig::logon_timeout_ms` (added) | BREAKING on the five observers (a slow peer's session no longer establishes) | through C | — | B row | BREAKING | none | timeout cells; Q-37 |
| 6 | Liveness refreshes on more frames, so a TestRequest that used to be sent is not | — | BREAKING on the five observers (a session that ended on an unanswered TestRequest stays up) | through C | — | B row | BREAKING | none | SC-005 cells; Q-37 |
> C-7 rows 7 and 8 also extend the Python import-surface golden (`bindings/python/tests/wheel/test_import_surface.py`, `EXPECTED_PUBLIC_SURFACE`) with `session_config_set_logon_timeout_ms` and `session_garbled_frame_count` (added at implementation, Phase 8, plan OD-27).
| 7 | Establishment timeout setter | — | `fixpp_session_config_set_logon_timeout_ms` (added; refuses null and zero; reentrancy "single-thread") | automatic | — | — | MINOR | golden + `session.h` freeze hash | setter cells, C and Python |
| 8 | Garbled-frame counter | `Session::garbled_frame_count()` (added) | `fixpp_session_garbled_frame_count` (added; refuses null handle and null `out`; 0 before the session exists; reentrancy "thread-safe") | `%apply` OUTPUT typemap; a GIL-table row | — | — | MINOR | golden + freeze hash | getter cells, C and Python, both wheel lanes |
| 9 | TOML key | — | — | — | `logon_timeout_ms`, a bare integer of milliseconds. Each refusal is a `LoadDiagnostic` on the key: a non-integer with `reason_class::malformed_value` (present but wrong type, as `src/config/logger_resolver.cpp`'s integer keys refuse one), and a value ≤ 0 or one above `UINT32_MAX` with `reason_class::out_of_range`. `logout_disconnect_timeout_ms`'s mapper is the shape only for the integer read: it accepts 0, and it ignores a non-integer without a diagnostic | — | additive | none | TOML cells |
| 10 | Config refusals: zero timeout; advertised 383 outside [4096, 262144] | `Engine::register_session` and `Session::open()` | — (C cannot set 383; C's zero is refused at the setter) | — | — | B row | C++ only; not C-ABI | none | `register_session` and `open()` cells |
| 11 | `SessionEvent` alternatives | `session_event_garbled_frame`, `session_event_establishment_timeout`, `session_event_close_reset_wait_expired` (appended) | — | — | — | — | C++ source change, not C-ABI: a `std::visit` over `SessionEvent` with no default arm stops compiling until it handles the three. 092 declined an alternative for that reason; 093's three carry the events FR-003, FR-006 and FR-041 require, as #424 appended one for its own. Declared in the B&L delta and the PR description's C++ deltas; T012 re-derives the in-repo visits | none | event cells |
| 12 | Parse reserve argument | a public `Parser::parse(frame, mr, OffsetTable::Config, std::size_t reserve_entries)` overload; a public `OffsetTable` constructor overload taking the same reserve, which passes it to the private `build`; a private tagged `MessageView` constructor between them (`Parser` is already its friend). Existing overloads are unchanged | — | — | — | — | additive | none | dense-L |
| 13 | Store reset unit | `MessageStore::reset_to` (non-pure virtual; precondition `{1, 2}`) | — | — | — | L-3, L-4 | additive. The vtable changes, so a rebuild is needed (C++ only) | none | C-6 cells |
| 14 | Engine access seam | `friend struct session_engine_access;` in `session.hpp`, defined under `src/session/`, never installed. It carries what data-model E-11 lists: `inbound_limit()`, `has_reached_active()`, `note_garbles_()` (the summary intake), `note_establishment_timeout_()`, `note_engine_stop_()` (the engine-stop flag's setter) and the borrowed carry | — | — | — | — | additive. The Session's underscore hooks stay private | none | build |
| 15 | Arena requirements | `SessionConfig::framer_carry_arena` must hold L + one read + `kContainerSlack`. The Session's arena must hold B(L) per `Session`, and the engine builds one `Session` per connection. Both are allocated at `open()`, and a failure is an `open()` error, not a `std::terminate`. The carry's path needs no public change: `open()` allocates the block inside a `try`, builds a `monotonic_buffer_resource` over it whose upstream is the spill witness, and builds `pmr_carry_buffer` over that resource, so the `noexcept` constructor's reserve is served from the block (E-2) | — | — | — | B row (cost formula) | behaviour, C++ only | none | `open()` with a bounded arena |
| 16 | #523: a closing session's NotConnected and LogonSent arms act on nothing | — | — | — | — | B row | **Not BREAKING**, following B-518-1's owner ruling ("the old outcomes were the defect"), of which #523 and #524 are the follow-ups | none | #523 cells |
| 17 | #524: the reset unit is one store operation, run on an empty cancellation slot (plan OD-25, OD-26), and stopped by the engine-stop flag after `Engine::stop()`'s step 1 has run on the session's strand | — | — | — | — | `L-518-1` updated | **Not BREAKING**, on the same ruling | none | #524 cells |
| 18 | `MessageView::unknown_fields()` returns an empty view on arena exhaustion instead of reaching `std::terminate` (fixpp#540) | `MessageView::unknown_fields()` (body only; still `noexcept`) | — (the C ABI does not expose it) | — | — | L-5 | behaviour, C++ only | none | Q-32 |

**Version.** One MINOR bump. Its `version.h` history entry is headed BREAKING, as 1.10's is, names
rows 1 to 6 in one summary line each with a pointer to their declarations, and lists rows 7 and 8 as
additions. By the placement rule it details an effect only where no declaration carries it; rows 1 to
6 are each carried by the five observers, so the entry details none of them. Re-derive the current
MINOR from `include/fix/c_api/version.h`.
`gh release list --exclude-drafts` must be empty at implementation. The PR description and the B&L
delta carry the same BREAKING list.

## C-8: Disclosed limitations (B&L L-rows)

- **L-1.** BodyLength too large but ≤ L: the Framer waits for the counted bytes, swallowing any frame the
  peer sends meanwhile. When the count can be checked, the candidate is a garble and framing resumes inside
  it, so the swallowed frames are delivered late. Before Active, the establishment deadline bounds the
  wait. In Active with HeartBtInt > 0, the TestRequest reply is trapped behind the stall, so the liveness
  loop takes the FSM to Disconnected, and the transport stays open until the peer closes it, a later
  over-L BodyLength closes it, or `close()` / `Engine::stop()` runs (fixpp#534). With HeartBtInt = 0 only
  the bytes arriving, the peer, or `close()` / `Engine::stop()` end it. A carry overflow cannot end it:
  it is unreachable in the pump (OD-23; re-derived at T105 as B&L `L-093-1`).
- **L-2.** The event carries the failure kind, not the §4.5.2 criterion. TC 3e's "not last" reports as
  BodyLength.
- **L-3.** A custom store that does not override `reset_to` has no crash atomicity.
- **L-4.** `close()`'s wait can expire. For a default-body store whose unit is still running, the
  teardown reset can then interleave with it, and the (1, 1) row is not guaranteed.
- **L-5.** A callback-time lazy read beyond the headroom that reports a status fails that read, never
  the session; the C cursor shells are the exception (L-17). On MSVC debug the spill witness forwards,
  so the read succeeds and the spill is recorded. Its report is per API (C-3 I-5): `group_slices()` gives an empty span, indistinguishable from an absent group;
  `fixpp_msg_get_group` reports `FIXPP_ERR_TYPE_MISMATCH`; the nested getter reports
  `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`; `unknown_fields()` gives an empty view, indistinguishable from a
  frame with no unknown fields.
- **L-6.** Before a Session exists, a connection on the acceptor is not observed. Its garbles are not
  counted, and its close is silent (no event, no log): no out-of-session log site exists in `src/`. A
  connection that yields a Session hands its first-frame garbles to it as one summary.
- **L-7.** There is no resend-loop guard for a garbled frame retransmitted identically (§4.5.2's
  recommendation; as in L-092-1). Its outcome is the one L-092-1 records for a disregarded faulty
  frame, because the session sees the same missing number either way: bounded within one session by
  that trace, and repeated across Engine restarts (the engine builds one Session per entry per
  `Engine::start()`, data-model E-4).
- **L-8.** A complete, well-formed frame embedded in a garbled region's data, if the resync search
  finds it, is framed and delivered like any other frame. The session's guards then apply to it. For
  example, an embedded copy of an earlier message has a MsgSeqNum below NextNumIn; unless it is a
  Heartbeat or carries PossDupFlag(43)=Y, the too-low guard ends the session.
- **L-9.** The 16-slot event ring can evict older events under a garble flood. The counter is the
  durable signal.
- **L-10.** Only C++ can set 383. C, Python and TOML sessions use L = 64 KiB.
- **L-11.** Garble logging is rate-bounded (FR-003) to one record per `max(HeartBtInt, 1 s)`. Within an
  interval, garbles after the first are counted in the next record and not logged one by one. If the
  logger's queue is full, its `drop_newest` policy drops records; the counter stays exact.
- **L-12.** `Engine::stop()` waits for an in-flight reset unit's store operation, which no cancellation
  reaches: it runs on an empty cancellation slot (plan OD-25, OD-26). A store operation that never completes hangs `stop()`, as `close()`'s teardown
  reset already does.
- **L-13.** Before the first Active, the deadline race may allocate. Whether it does, and how much, is
  measured in the verify record (FR-052's no-new-allocation scope starts at the first Active).
- **L-14.** The admin and outbound parse sites' stack arena is not derived (C-3 I-6). This is
  pre-existing and filed as a follow-up.
- **L-15.** A frame lying inside a structurally complete frame whose CheckSum value is wrong is
  discarded with it. ResendRequest recovers it.
- **L-16.** The resync search after a garble looks for `8=FIX`. A session configured with a BeginString
  that does not begin with `FIX` frames its frames at a frame boundary as today, but after a garble it
  finds no next frame start, so it discards every later byte until the establishment deadline or the
  liveness loop ends it. No supported profile has such a BeginString.
- **L-17.** The C cursor shells that `fixpp_msg_get_group` and `fixpp_group_get_nested_group` allocate
  from the parse arena have no catch, so exhausting the headroom there lets a `bad_alloc` escape the C
  function. Neither function is `noexcept`. Where the unwind reaches the callback guard, the session
  ends with `app_callback_threw`; where the C caller's frame cannot be unwound, the process
  terminates. Either way the session ends or the process terminates. This is pre-existing on main,
  found by code reading at Gate A round 2, and filed as fixpp#541 (unconfirmed; reproduce first;
  batch B28). 093 does not fix it (plan.md OD-19). If data-model E-2's `kCallbackReadHeadroom`
  condition fails at implementation, this row also records that 093's up-front reserve leaves less
  callback room than the base for the frames where it fails.
- **L-18.** Through the C ABI, an `open()` allocation failure (the carry block or B(L);
  `core::error::out_of_memory`, plan OD-23) is visible only as a session that is never published:
  `fixpp_session_close` returns `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`. The error code is not carried,
  because `Session::open()` runs in the engine's role loops, which return on error, not on
  `fixpp_session_open`'s path (plan OD-27).
- **L-19.** These C-7 clauses are witnessed through C++ only, not through the C API. Re-derive the list: run
  `capi_inbound_frame_dispositions_test --gtest_list_tests --gtest_filter='CapiC7Witness.*:CapiInboundFrameDispositions*'`,
  build the C-7 row × role × state matrix of the C cells it lists, and diff that matrix's complement against
  this list. The rule "a C-ABI effect witnessed only through C++ is unwitnessed" applies to them, so they are
  disclosed rather than claimed:
  - row 1: a garble in LogoutSent; row 2: the count of a frame in LogoutSent whose third field is not MsgType(35)
    (092's D-9 frames of that shape, disregarded and counted). From C, a session is in LogoutSent only while
    `fixpp_session_close(graceful)` blocks its caller, so a C cell must make that call from a helper thread and
    drive the peer meanwhile, as `CapiC7Witness.Row3_AnOverLimitBodyLengthInLogoutSentEndsTheBlockingClose` does
    for an over-limit frame (plan OD-29); neither the garble cell nor the getter cell is driven that way. For the
    count, a C cell must also poll the getter from a further thread while that call blocks: the close invalidates
    the handle when it returns, after which `fixpp_session_garbled_frame_count` returns `FIXPP_ERR_INVALID_HANDLE`,
    and the disregarded frame draws no reply the reader could synchronise on, since the LogoutSent arm answers no
    inbound frame and the getter's value is not ordered with the rest of the session's state;
  - row 1: a garble in Disconnected (no C-observable effect beyond the counter);
  - row 6's liveness classes that are a single frame or sit behind a configuration the C ABI cannot set: one
    too-high frame, a Reset-mode SequenceReset, the validate Reject (validation cannot be enabled through C),
    and the knob-off path;
  - the Python getter's `uint64_t` mapping on the installed-wheel and Windows lanes, which only CI runs.
