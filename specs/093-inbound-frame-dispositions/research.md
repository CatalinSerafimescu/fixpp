# Research: 093-inbound-frame-dispositions

`/speckit-plan` extends this file. R-1 was done at specify time because the owner made the FR-004
ruling conditional on it.

## R-1: a frame whose third field is not MsgType(35)

**Question.** A correctly framed frame (8= first, 9= second with a correct count, 10= last and correct)
whose third field is not 35, e.g. `8=FIX.4.2|9=..|34=2|35=0|…|10=..|`. Must it be disregarded as
garbled, Rejected with 373=14, accepted, or met with a disconnect? Does the answer differ before and
after Logon?

**Decision.** Disregard it as garbled: no Reject, NextNumIn unchanged, continue. This holds in every
state and in both validation modes (FR-004). It confirms the owner's ruling of 2026-10-02.

### Primary documents

The local copies are under the parent's `research/G19-fix-fpml-iso20022/research/`. Text was extracted
with pypdf; page numbers are the PDF's own.

- **FIX Session Layer 2020 §4.5.2 (pp. 26–27).**
  - The definition: "A message shall be considered garbled if … MsgType(35) is not the third tag in a
    message."
  - The rule: "The receiving FIX session processor must disregard the garbled message and not
    increment NextNumIn."
  - §4.5.4 scopes Reject to a message that is "not garbled".
  - §8.5 StandardHeader (p. 62): "MsgType(35) must be the third field in the message."
  - Code 14 ("Tag specified out of required order") appears only in the code list (p. 80).
- **FIX Session Test Cases 2020.**
  - Scenario 2 row t (p. 11): "BeginString(8), BodyLength(9), and MsgType(35) are not the first three
    fields of message. 1. Consider garbled and ignore message. 2. Do not increment NextNumIn.
    3. Continue accepting messages." It carries no state qualifier.
  - The only 373=14 row is Scenario 14 row g: header, then body, then trailer interleaving. That is a
    different defect.
- **FIX 4.4 with 2003-06-18 errata.**
  - Vol 1, p. 18: "The first three fields in the standard header are BeginString … BodyLength …
    MsgType".
  - Vol 2, p. 30, "What constitutes a garbled message": "MsgType (tag #35) is not the third tag in a
    message."
  - Vol 2, p. 43, rows 2s and 2t: "Consider garbled and ignore message (do not increment inbound
    MsgSeqNum)".
- **FIX 4.2:** not obtained (fixtrading.org did not serve the document). The 2020 Session Layer's
  BeginString row lists FIX.4.2.

### Engines

Shallow clones of each default branch, 2026-10-02: QuickFIX/C++ `386ce46e`, QuickFIX/J `db43e4d4`,
QuickFIX/Go `eff945dd`, QuickFIX/n `03081287`.

| Engine | After Logon | Before Logon | Notes |
|---|---|---|---|
| QuickFIX/C++ | ignored (logged) | **disconnect** (`SocketConnection.cpp` drops the socket when not logged on) | The check is `Message.cpp` `headerOrder[]` and throws `InvalidMessage("Header fields out of order")`. It is skipped when `ValidateLengthAndChecksum=N`. 373=14 comes from `DataDictionary` `hasValidStructure`, which never covers the first three fields |
| QuickFIX/J | ignored (logged) | ignored, unless the frame is itself a Logon (disconnect) | `Message.java` preamble check, guarded by having a data dictionary. `RejectGarbledMessage=Y` (not the default) sends a text-only Reject with no 373 |
| QuickFIX/Go | ignored (`OnEventf`, no state change) | **disconnect** on the acceptor's first frame | `message.go` extracts 8, 9 and 35 in order. There is no toggle |
| QuickFIX/n | ignored | ignored, unless the frame is a Logon (disconnect) | `Message.cs` mirrors C++, and so does its toggle |

The shared acceptance test is `test/definitions/server/fix42/2t_FirstThreeFieldsOutOfOrder.def`. It sends
fixpp#514's exact shape after Logon (`34=3^35=0`), then resends 34=3 as a TestRequest, and expects the
Heartbeat reply at outbound 34=3. So the engine sent nothing in response and did not consume the
number. No repo excludes the test.

### Consequences for the spec

- No source supports 373=14 for this shape. B-005-7's description of a QuickFIX divergence, "QF emits
  373=14", is wrong for field 3. QuickFIX emits 373=14 for header, body and trailer interleaving.
- **Before Logon the engines split.** C++ and Go disconnect, while J and n ignore the frame unless it is
  a Logon. No spec text requires a disconnect. FR-004 and FR-005 disregard the frame, and FR-006's
  establishment timeout bounds the wait. Gate A may revisit this. A disconnect is defensible under TC
  Scenario 2S ("first message received is not a Logon → disconnect"), but it is not mandated.
- **Validation off.** C++, J and n skip the check when validation is off and then accept the frame.
  fixpp has no equivalent toggle for framing checks, and FR-004 applies in both of fixpp's validation
  modes.

---

The sections below were written at `/speckit-plan`, 2026-10-02, from three read-only code surveys at
`00c1f720`. Line numbers are leads at that commit. Re-derive them before citing.

## R-2: Framer resync and garbled-frame accounting (#514; FR-001 to FR-003, FR-008)

**Facts.**
- `parse_frame` (`src/wire/framer.cpp`) checks the shape `8=`…SOH and never reads the BeginString value.
  - Error kinds: `wire_framing_resync` (leading bytes are not `8=`), `wire_invalid_body_length`,
    `wire_checksum_mismatch` and `wire_frame_too_large`.
  - `wire_invalid_body_length` covers a missing, empty or non-digit 9, and also `10=` not being at
    `body_off + BodyLength`. So "CheckSum not last" (TC 3e) reports as a BodyLength failure.
- Every error clears the whole carry (`feed`'s `fail` path). With an `out` span longer than one, an error
  also discards frames already produced in that call. The pump's `out` has length 1.
- `wire_frame_too_large` comes from three places:
  - BodyLength digits that overflow, or a value over `max_frame_bytes`;
  - `frame_len > max`, tested **after** the checksum;
  - carry overflow in `feed`.

  The pump's Framer is default-constructed, with a 256 KiB limit, but its carry is 64 KiB. So the carry
  is what bounds frames today.
- **Callers of `Framer::feed`** (the header is public):
  - the pump (`engine.cpp`, twice);
  - `read_first_frame_bounded`;
  - `parse_and_dispatch_` and `validate_inbound_`, each re-framing one exact frame;
  - `reify.cpp` and the generated reify code;
  - three fuzz harnesses and six benches;
  - about 60 test files (`git grep -n -E "\bFramer\b|\.feed\(" -- tests`).

  No C-ABI or Python caller.
- The pump is co_awaited inline on the session strand, so a synchronous `Session` member called from it
  is race-free.
- `SessionEvent` (`include/fixpp/session/session_event.hpp`) is a variant to which alternatives are only
  appended. No `std::visit` runs over it, and it is not in the C ABI. The ring holds 16 entries.
- **No production log call exists in `src/`.** `SessionConfig::logger_override` is written by the
  resolver and never read by the session.

**Decision.**
- **Resync lives inside the Framer, opt-in**, behind `Framer::Config::resync_on_garble` (default `false`).
  Every other caller keeps its behaviour byte for byte.
- With the flag on, any error other than `wire_frame_too_large` triggers a resync:
  - if frames were already produced in this call, stop and leave the garble for the next call;
  - otherwise record one `garble_record{kind, discarded}` and advance to the next frame start in one
    forward pass. That start is an `8=` that follows an SOH, carried across feeds in one bit, or the
    first byte after a complete frame;
  - then continue.
- Nothing is allocated: the scan runs in place over the carry and the incoming bytes.
- `last_garble()` exposes the record, and each `feed` resets it.
- Callers re-feed while a garble produced no frame. The pump and the first-frame read both need this; the
  first-frame read has no drain loop today.
- **`frame_len > L` is checked as soon as `body_off` is known, before the checksum.** That way an over-L
  frame with a bad checksum closes (FR-013) rather than being disregarded. It changes which error wins
  for every Framer user, so the Framer's own tests are updated deliberately.
- The pump calls `Session::note_garbled_frame_(record)`, a synchronous `noexcept` member. It increments
  an atomic counter, emits `session_event_garbled_frame`, and logs through the logger resolved at
  `open()`: `cfg_.logger_override`, else the engine's.
- `read_first_frame_bounded` returns `{offset, len, garbles}`. The engine replays the garbles into the
  Session after `open()`.

**Alternatives rejected.**
- **Resync in the pump.** It cannot recover frames the Framer has already discarded, and it would
  duplicate the scan in two callers.
- **Stepping one byte per `feed`.** `consume_front` is a front erase, so stepping byte by byte is
  quadratic over a 64 KiB junk carry.
- **Reporting the §4.5.2 criterion.** The Framer cannot tell criterion 2 from criterion 4. The event
  carries the failure kind, and B&L says so.
- **Making the flag the default.** That changes reify, the re-framing parse helpers and the fuzz
  harnesses, which want strict framing.

## R-3: Inbound limit L and parse capacity (#515; FR-010 to FR-014)

**What a parse allocates from its resource.**
- `OffsetTable::build` allocates `entries_` (12 B per field, with every field including 8, 9 and 10) by
  `push_back` with no reserve.
- It allocates `overlay_` (4 B per slot) as one exact `assign` of `overlay_cap_for(n)`: the next power of
  two at or above 1.25n+1, with a minimum of 8.
- Nothing else is allocated during the parse.
- The validator and the header scan allocate nothing per field.
- Lazy reads inside a callback allocate on demand: `group_index_`, group slices, nested tables,
  `unk_items_` and C-ABI cursors.

**Densest field.** `1=<SOH>` is 3 bytes. An empty value is accepted by `build` and by the scan's
`length_data_carry::read_value`, so Nmax(L) = ⌊L/3⌋.

**Ceilings today.**
- `default_max_offset_entries` (`offset_table.hpp`) fails a frame with more fields than the cap on every
  lane. 092's LateSite cells use exactly that trigger (`kLateFillerFields`). So the earlier claim that
  "MSVC debug never fails" is false.
- The arena runs out first on 2× growth, at roughly 512 fields in 16 KiB. That figure is derived, not
  measured. MSVC's 1.5× growth fails at a different count.

**Decision.**
- `open()` computes L: the advertised 383 if set, else 64 KiB. It refuses a value under 4096 or over
  256 KiB with `invalid_session_config`.
- L is stored on the Session. The pump reads it and builds `Framer{.max_frame_bytes = L,
  .resync_on_garble = true}` with a carry of L + the read size.
- The parse buffer B(L) = 12·⌊L/3⌋ + 4·pow2ceil(1.25·⌊L/3⌋+1) + headroom (callback reads) + slack (the
  MSVC-debug container proxies). It is allocated once in `open()` from `session_arena_`.
  - At L = 64 KiB that is about 384 KiB plus headroom.
  - `bad_alloc` is mapped to an `open()` error.
- `parse_and_dispatch_` and `validate_inbound_` build a fresh `monotonic_buffer_resource` over that span
  on each call, which is how they reset today. `validate_inbound_` is `const`, so the span is `mutable`.
- Each parse passes `reserve = min(cap, frame.size()/3 + 1)` as a **call argument**, not a
  `Config` field.
- The session's `OffsetTable::Config::max_offset_entries = ⌊L/3⌋ + 1`.
- Admin and outbound parses stay on their stack arrays, which only parse frames fixpp built.
- Upstream: a spill witness, which records any allocation past B(L). It is `null_memory_resource` on
  every lane except MSVC debug, where it forwards and records. Tests assert that nothing spilled.
- **One buffer is safe.** Validate and dispatch run in sequence and are synchronous. Callbacks are
  synchronous, `send()` and `close()` are awaitables, and the callback scope asserts against nesting.

**Alternatives rejected.**
- **Reserving `field_no` from the scan.** There is no size gain, because the buffer must cover Nmax
  anyway. It also couples two scanners that must agree forever (`offset_table.hpp`'s #389 note).
- **A reserve held in `Config`.** Clones and reifies copy `Config` into a `frame_len + 4096` arena, so the
  reserve would push large clones onto the heap. `Config` is also a public-header type.
- **A per-thread buffer, or lazy growth.** The owner ruled per session (Clarifications). Lazy growth
  allocates on the inbound path.
- **Carry = L.** A frame of exactly L would then depend on how the stream is segmented.

**Memory.**
- Today: about 68 KiB live per connection, plus two transient 16 KiB stack arrays per validated frame.
- After: (L + 4 KiB) carry + B(L), about 6·L + headroom, so about 450 KiB at the default L. B&L states the
  formula, not a number (SC-007).
- Throughput should not get worse: each frame loses a 16 KiB memset (two when validated), and the
  reserve is a pointer bump. `on_inbound_frame_bench` is not paired in CI, so the plan runs a manual
  paired base-vs-branch, with the base worktree path padded to the same length.

**Tests that flip or rot.**
- The ten `LateSite_*_Closes` cells lose their trigger. FR-014's defence cell replaces them: it shrinks
  the buffer through `session_test_access` after `open()`.
- `test_066_arena_fit_test` keeps private copies of 8192 and 16384. Delete it or re-base it.
- `test_070_max_message_size_test`'s pre-establishment exemption reverses (FR-013).
- `engine_readpump_test`'s 128 KiB oversize body still exceeds L.

## R-4: Establishment timeout (#514; FR-006)

**Facts.**
- Initiator: `drive_reconnect` installs the transport, enters LogonSent and emits the Logon. The engine's
  connect loop runs one cycle per call, and connect retries happen inside `drive_reconnect_attempt`
  before install.
- Acceptor: accept, then the TLS handshake (bounded by `tls_handshake_timeout`), then
  `read_first_frame_bounded` (5 s and 4096 B), then `open()`, attach, deliver, pump.
- `close(terminal)`:
  - writes Disconnected;
  - cancels sleeps and emits total on `root_cancel_`;
  - calls `close_async()` on the transport, which **closes the socket**.
- `record_state_transition_` closes nothing (fixpp#534).
- `Clock::cancel_sleeps()` is clock-wide. `await_deadline` (`read_first_frame_bounded.hpp`) already
  re-arms on a spurious sweep. The liveness loop does not (fixpp#536, unconfirmed).

**Decision.** The deadline lives **in the pump**, as an absolute steady time passed into
`run_read_pump`.
- Acceptor: accept time + `logon_timeout`. The first-frame read is bounded by `min(5 s, remaining)`.
- Initiator: computed right after `drive_reconnect` returns.
- Until the session first reaches Active, each read races `async_read_some || await_deadline(clock,
  abs_deadline)` on `engine_cfg.clock`, which is what `effective_clock_` resolves to, so mock-clock cells
  drive it.
- On expiry: `note_establishment_timeout_()` (event + log), then `stop_pump()`, which calls
  `close(terminal)`.
- After Active, the plain read.

**Alternatives rejected.**
- **A detached session timer.** It would need a join counter, and a timer that calls `close()` while
  counted in `liveness_counter_` deadlocks `close()`'s join loop.
- **Arming on every reconnect attempt.** There is no per-attempt Logon to arm for.

**Cost.** One parallel group per read, before Active only. That is outside the steady-state path
(FR-052), and the read-path allocation guard measures `async_read_some` + `feed` directly.

## R-5: Liveness refresh placement (#516; FR-020, FR-021)

- In the shared LogonReceived/Active arm, the order is:
  1. the 383 check (above the switch, Active only);
  2. the scan;
  3. the fault check;
  4. the validate gate;
  5. the BeginString/CompID guard;
  6. the SendingTime guard;
  7. Reset-mode SequenceReset;
  8. Guard 4 (seq 0, too-high, the PossDup Rejects, `check_inbound`, the too-low cases);
  9. GapFill;
  10. Logout;
  11. Reject(35=3);
  12. the only writer, guarded by `fsm_state_ == Active`.
- **Decision.** One unconditional `last_inbound_steady_ = effective_clock_->steady_now()` goes after the
  fault check and after the new 35-not-third check, before the validate gate. The old writer is deleted.
  - TestRequest matching is independent of the refresh. `pending_test_req_id_` is set by the loop and
    cleared only by an inbound Heartbeat, and the grace deadline uses the `inbound_deadline` computed
    before the sleep.
- **Tests.** `run_liveness_cell` in `tests/session/unparseable_frame_disposition_test.cpp`, on the mock
  clock, gets inverted twins. The FR-018 cells stay green and become real witnesses: reverting the
  disposer would now refresh. `heartbeat_testrequest_test.cpp` is a second home.
  - Grep found no cell pinning "too-high does not refresh". So the move is followed by an unfiltered run
    of the whole session suite.

## R-6: A closing session, and the atomic reset unit (#523, #524; FR-030, FR-040 to FR-042)

**#523.**
- `close()` sets `state_ = closing`.
- A terminal close writes Disconnected before it first suspends. A graceful close from NotConnected or
  LogonSent yields only in its flush hook.
- So the window exists only with a store whose flush yields.
- **Decision.** One check above the switch:
  `if (state_ == lifecycle::closing && (fsm_state_ == NotConnected || fsm_state_ == LogonSent)) return
  success;`.
- **Tests.** Extend `LogonCloseDuringSuspension` in `tests/session/test_session_plaintext_roundtrip.cpp`
  (`HookedStore`, `CaseRig`).
  - Coalesce by appending the second frame to the single write of the Logon, or of the Logon reply.
  - The RED observable: `validate_inbound_messages = true` plus a dictionary-invalid second frame, whose
    Reject reaches `toAdmin` after close began (`expect_no_admin_after_close`).
  - The flush's 8 posts are replaced by a bounded hold.

**#524, the unit today.**
- Manager `reset_to_one` (inline), then `store_->reset()` (**yields**), then a check, then manager
  `set_next_inbound(2)` (inline), then `persist_inbound_advance_` (**yields**), then a check.
- The initiator adds the outbound restore and one more yield.
- The restore is lost when `close()`'s `seqnum_mgr_.drain()` has run: `set_next_inbound` then returns
  `session_already_closed` (`L-518-1`).

**Decision (FR-041; owner ruling "close() waits, with a timeout").**
1. Compute the targets:
   - `in = advanced ? 2 : 1`;
   - `out = (initiator && own_reset && n_pre_outbound == 2) ? 2 : 1`.
2. Set the manager inline, with no yield.
3. Set `reset_unit_in_flight_`, `co_await store_->reset_to(in, out)`, then clear the flag.
4. Run the existing superseded check.

`close()` waits for the flag to clear before its teardown reset. It polls the way it already waits for
the liveness counter, bounded by `logout_disconnect_timeout_ms`. On expiry it proceeds and records an
event.
- FIFO store order then gives 1/1 when a teardown reset is configured, and the unit's values when one is
  not, for every store.
- The in-unit `teardown_reset_done_` stops become dead and go. The flag remains as `close()`'s latch.
- The true targets are passed to volatile stores too. That may fix fixpp#538 (unconfirmed), so its
  reproduction runs on this branch and on its base.

**The operation.**
`virtual asio::awaitable<expected_t<void>> reset_to(seqnum_t next_in, seqnum_t next_out) noexcept;` with
a default body of `reset()` and then the advances.
- `MemoryStore`: one critical section that clears and sets both counters, keeping `++generation_`.
- `FileStore`:
  - parametrise `initialise_fresh` (it hard-codes `seqnum_min`), or append a counter record to the temp
    file before the rename;
  - change both the POSIX and the Windows temp branches, Region 3 and the `operation_aborted` catch;
  - restart takes the last counter record, so the rename is the single commit point.

**Subclasses.**
- About 29: two in `include/` and about 27 test-local ones.
- No `reset_to` name exists today. `-Wsuggest-override` is off, and `-Werror` is on with `-Wall -Wextra
  -Wpedantic`.
- A non-pure virtual needs no subclass change. Re-derive the population with `git grep -n "public
  .*MessageStore"`.
- The vtable changes, so code built against the old header must be rebuilt. That is C++ only; the C ABI
  is unaffected.
- `HookedStore` must forward `reset_to` to its inner store and fire its hooks there. Otherwise the
  existing `*StoreEndsAtTeardownReset` cells silently measure the default body.

## R-7: Configuration and C-ABI surface (FR-007, FR-051)

- `SessionConfig::logon_timeout_ms{10000}` sits next to `logout_disconnect_timeout_ms`. That field's
  comment claims a validation nothing performs, so copy neither the pattern nor the comment.
- `open()`'s validation block refuses a zero timeout, and an advertised 383 below 4096 or above 256 KiB.
- **TOML:** add the key to `kRecognized`, with a mapper that requires `v > 0`.
- **The C setter** follows `fixpp_session_config_set_heartbeat_seconds`: null handle → `NULL_HANDLE`, and
  zero → `FIXPP_ERR_CAPI_CONFIG_INVALID`, as `set_begin_string` does on an empty value.
- **The C getter** follows `fixpp_session_is_established`: an out-parameter checked for null, then
  `check_session`. It reads through `engine_->lookup(id)` and reports 0 before the session exists.
- **Gates:**
  - the symbol golden, sorted;
  - `tools/capi_freeze.sha256` for `session.h` and `version.h`;
  - `tools/check_capi_reentrancy.sh`, one token per doc block: "single-thread" on the setter,
    "thread-safe" on the getter.
- **Version:** MINOR +1 (re-derive the current value from `include/fix/c_api/version.h`), with a history
  block that declares BREAKING. No error code is added.
- **Python:**
  - the setter is picked up automatically (`%include "fix/c_api/session.h"`);
  - the getter needs `%apply … *OUTPUT { uint64_t* … }`, tested on both wheel lanes because `uint64_t`
    differs between LP64 and Windows;
  - the getter also needs a row in the GIL table, which states it is exhaustive.

## R-8: Tests that pin the old behaviour (re-derive at implementation)

- `engine_readpump_test.cpp` `FramerFailureClosesEstablishedSession_*`: flips (L-004-4).
  `OverCapacityFrameClosesSession` stays.
- `fix_tc_coverage_gaps_test.cpp` `HeaderFieldsOutOfOrder_MsgTypeNotFirst_Accepted_*`: flips (B-005-7).
- `test_validate_gate_inbound.cpp` W1 (373=14 for 35-not-third): flips to disregard.
- `unparseable_frame_disposition_test.cpp`:
  - `PreActive_D8Logon_*_Refused` and the D-8 cells: flip (FR-005);
  - the `LateSite_*_Closes` cells: re-based (R-3);
  - the liveness pin comment: rewritten as a condition.
- `coverage_adversarial_test.cpp`'s 35-position cells.
- `test_070_max_message_size_test.cpp`: the pre-establishment exemption reverses.
- `test_066_arena_fit_test.cpp`: its private constants go.
- `test_session_plaintext_roundtrip.cpp` `*StoreEndsAtTeardownReset` and the
  `close_from_*_persist` cells: re-derived (R-6).
- **The population no grep can find:** test frame builders that emit non-canonical field order. Recipe:
  1. In a scratch copy, add a temporary `std::abort()` on a fault-free `!msg_type_is_third` in every arm.
  2. Run the whole suite unfiltered, including the loopback, C-ABI and Python round-trips.
  3. Collect every failure.
