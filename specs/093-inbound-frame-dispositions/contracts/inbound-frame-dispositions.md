# Contract: inbound frame dispositions (093)

This contract builds on 092's `contracts/unparseable-frame-disposition.md`, whose C-2 rows for faulty
frames stand except where C-2 below changes them. Requirement IDs are spec.md's.

## C-1: Framing on the session's inbound path (FR-001, FR-002, FR-013)

The read pump's Framer runs with `max_frame_bytes = L` and `resync_on_garble = true`, and a carry of
L + one read. On each `feed`:

| Bytes at the frame start | Framer result | Pump action |
|---|---|---|
| a well-formed frame of ≤ L bytes | frame | deliver to `on_inbound_frame` |
| BodyLength or frame length > L (checked before the CheckSum) | `wire_frame_too_large` | **close** (`close(terminal)`), with an event and a log; no field is read |
| carry overflow | `wire_frame_too_large` | **close**, as above |
| not `8=` (leading junk) | garble `wire_framing_resync` | `note_garbled_frame_`, then re-feed |
| bad, missing or non-digit 9, or `10=` not at the counted offset (wrong count, or CheckSum not last) | garble `wire_invalid_body_length` | as above |
| CheckSum malformed, not 3 digits, not SOH-terminated, or wrong value | garble `wire_checksum_mismatch` | as above |
| a partial frame (BodyLength not yet satisfied) | none | read more |

**Resync rule.** After a garble, framing resumes at the first `8=` after the garbled frame's first byte
that either follows an SOH, or is the first byte after the last complete frame.
- The "previous byte was SOH / inside a garble" bit persists across feeds, so the outcome does not depend
  on segmentation.
- A garble region that spans reads is one garble.
- One `feed` reports at most one garble, ahead of any frame that call produces.
- The search is one forward pass with no allocation.

**Every other Framer caller** (reify, the re-framing parse helpers, fuzz, benches, tests) keeps
`resync_on_garble = false`, so it is unchanged. The exception is the check-order change for frames over
`max_frame_bytes`, which applies to all callers.

## C-2: Disposition at `on_inbound_frame`, in this order, in each arm (FR-004, FR-005, FR-020, FR-030)

| Step | Condition | Disposition | States |
|---|---|---|---|
| 0 | `closing` and state ∈ {NotConnected, LogonSent} | return success, no effect (FR-030) | NotConnected, LogonSent |
| 1 | the scan's `msg_type_is_third == false`, faulty or not | **disregard**: `note_garbled_frame_("35 not third")`, no Reject, NextNumIn kept, no liveness refresh | every state except Disconnected |
| 2 | the scan found a fault | 092's C-2 rows, except that D-8 is now unreachable (step 1 takes it) | as in 092 |
| 3 | otherwise | refresh inbound liveness (FR-020), then the existing guards and handlers | LogonReceived, Active |

- Before step 0, frames over L never reach `on_inbound_frame` (C-1).
- The Active-only advertised-MaxMessageSize check above the switch is deleted. C-1 replaces it.
- `validate_inbound_messages = true` does not change step 1. The validator's Step 0 (373=14 for this
  shape) becomes unreachable from the session.
- Before Active, step 1 is a disregard, as 092's D-1 and D-2 were not. The wait it creates is bounded by
  C-4.
- Disconnected stays "ignore every frame", which is 092's and today's rule.

## C-3: Parse capacity (FR-010 to FR-014)

- **I-1.** For every frame the pump admits (C-1: ≤ L and well framed), every inbound parse site's
  `Parser::parse` succeeds without `out_of_memory` and without `err_offset_table_full`, on every lane.
  The sites are the validate gate in three arms and dispatch at seven sites. Re-derive them with
  092 contract C-6's command.
- **I-2.** The parse's resource is a fresh `monotonic_buffer_resource` per call over the session's
  B(L) span. Its upstream is a spill witness, so a spill is detectable on every lane, MSVC debug
  included.
- **I-3.** No inbound parse nests inside another. Callbacks are synchronous, and the callback scope
  asserts it.
- **I-4.** 092's late-parse close (`close_on_late_parse_failure_`) stays as a defence. Its
  reachability is shown only by a cell that shrinks the buffer through `session_test_access`.
- **I-5.** Lazy reads inside a callback draw on `kCallbackReadHeadroom`. Exhausting it fails that read
  with `out_of_memory` returned to the caller, not a session effect (L-5).
- **I-6.** The admin and outbound parse sites keep their stack arenas. They parse only frames fixpp
  built.

## C-4: Establishment timeout (FR-006, FR-007)

| Role | Clock starts | Bounded reads | On expiry |
|---|---|---|---|
| acceptor | accept | the first-frame read (`min(5 s, remaining)`), then each pump read until first Active | event + log, `close(terminal)` |
| initiator | `drive_reconnect` returns (Logon sent) | each pump read until first Active | event + log, `close(terminal)` |

- The clock is `engine_cfg.clock` through `await_deadline`, which re-arms on a clock-wide sweep.
- The timer never runs after the first Active. It applies to a session already in Disconnected (a
  refused Logon) whose transport is still open.
- `logon_timeout_ms` defaults to 10000. Zero is refused by `open()`, TOML and the C setter.

## C-5: Liveness (FR-020, FR-021)

- `last_inbound_steady_` has one per-frame writer: step 3 of C-2. The seeds at `open()` and at entering
  Active stay.
- Garbled frames (C-1, and C-2 step 1) and faulty frames (C-2 step 2) never write it.

## C-6: The 141=Y reset unit and `close()` (FR-040 to FR-042)

**Unit, both roles:**
1. Compute the targets `(in, out)` (research R-6).
2. `seqnum_mgr_.reset_to_one(); set_next_inbound(in); set_next_outbound(out)`, inline, with no
   suspension in between.
3. `reset_unit_in_flight_ = true; r = co_await store_->reset_to(in, out); reset_unit_in_flight_ = false;`
4. The existing disposition on `r`.
5. The existing `logon_arm_superseded` check.

This runs on every store, volatile ones included.

**`close()`:**
- Before issuing its teardown reset, `close()` waits while `reset_unit_in_flight_`, bounded per plan.md
  OD-1. On expiry it records `session_event_close_reset_wait_expired` and proceeds.
- **Open (research R-9): a terminal `close()` emits total cancellation before this wait.** If that
  cancellation reaches an in-flight default-body `reset_to` between its `reset()` and its advances, the
  "any point of the unit" row below fails for a terminal close on a non-overriding store. The atomic
  overrides are unaffected: they hold the store mutex across the whole unit. R-9 settles it before
  P6.
- `teardown_reset_done_` keeps only its role as the single-fire latch.

**Outcomes, for every store and both roles, when `close()` begins at any point of the unit:**

| Teardown reset configured | Final durable (next-in, next-out) |
|---|---|
| no | (in, out) of the unit: in = 2 when the Logon advanced, and out = 2 on the initiator's own reset |
| yes | (1, 1) |

**Atomicity:** `MemoryStore` and `FileStore` only. A non-overriding store meets the table, but a crash
inside its default body can leave the intermediate state (L-3).

## C-7: Public surface and versioning (FR-007, FR-051)

| Surface | Change | Kind |
|---|---|---|
| C++ `Framer::Config`, `garble_record`, `last_garble()` | added | additive |
| C++ `Framer`, every caller | `frame_len > max` checked before the CheckSum | behaviour (which error wins) |
| C++ `OffsetTable::build` / `Parser::parse` reserve argument | added, default 0 | additive |
| C++ `SessionEvent` | three alternatives appended | additive (the variant's type changes) |
| C++ `SessionConfig::logon_timeout_ms` | added | additive |
| C++ `Session::garbled_frame_count()` | added | additive |
| C++ `MessageStore::reset_to` | non-pure virtual added | additive; the vtable changes, so rebuild |
| C `fixpp_session_config_set_logon_timeout_ms` | added | MINOR |
| C `fixpp_session_garbled_frame_count` | added | MINOR |
| C-ABI observable behaviour | see the next list | **BREAKING** (`[const §X.7]`) |
| Python | the setter is automatic; the getter has an `%apply` typemap and a GIL-table row | additive |
| TOML | the `logon_timeout_ms` key (zero refused) | additive |

The C-ABI behaviour changes, observed through `is_established`, `close`, `send` and the callbacks:
- a garbled frame no longer ends the session;
- a 35-not-third frame is disregarded instead of processed;
- a frame over L closes in every state;
- a pre-Active connection closes at the establishment timeout;
- liveness refreshes on more frames, so a TestRequest that used to be sent is not.

## C-8: Disclosed limitations (B&L L-rows)

- **L-1.** BodyLength too large but ≤ L: the stall lasts until the count can be checked, or the carry
  overflows (close). In Active the TestRequest reply is trapped behind it, so liveness ends the session.
- **L-2.** The event carries the Framer's failure kind, not the §4.5.2 criterion. TC 3e's "not last"
  reports as BodyLength.
- **L-3.** A custom store that does not override `reset_to` has no crash atomicity.
- **L-4.** `close()`'s bounded wait can expire, and then today's interleaving hazard returns for that
  close.
- **L-5.** Callback-time lazy reads beyond the headroom fail that read.
- **L-6.** Garbles on an acceptor connection that never yields a Session are not counted.
- **L-7.** There is no resend-loop guard for a garbled frame retransmitted identically (§4.5.2's
  recommendation; as in L-092-1).
- **L-8.** `8=` with no SOH stays partial until the carry fills, then closes.
- **L-9.** The ring can evict older events under a garble flood. The counter is the durable signal.
- **L-10.** Only C++ can set 383. C, Python and TOML sessions use L = 64 KiB.
