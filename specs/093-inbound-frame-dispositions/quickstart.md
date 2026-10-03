# Quickstart: validating 093-inbound-frame-dispositions

This file is a validation guide, not an implementation. Every behaviour cell is written first and shown
either RED on the base (the merge base tasks.md T002 records after the rebase onto `origin/main`;
`00c1f720` at spec time), or RED on its named
mutant. A cell of the second kind shows "—" in its RED column, with the mutant and the instrument that
observes the effect. A regression guard is green on the base by design and names the mutant that turns
it RED. §4 maps every FR and SC to its contract clause and its cells.

## 0. Before any production change

1. **Take the timing baseline before the first edit.** Run a paired base-vs-branch bench of
   `bin/on_inbound_frame_bench`, once with `validate_inbound_messages` false and once with it true
   (`[const §VIII.2]`), and of `bin/framer_bench`'s `BM_Framer_Feed_NoCarry` (default `Config`).
   `on_inbound_frame_bench` bypasses the Framer, so the base's `BM_Framer_Feed_NoCarry` is the
   reference for two `framer_bench` pairings at the final head (§3):
   - against the candidate's default row, `BM_Framer_Feed_NoCarry` (the strict path and the
     always-compiled counted-work counters);
   - against the candidate's `BM_Framer_Feed_NoCarry_Resync` (tasks.md T018a; resync on, the pump's
     limit and BeginString cap), compared across row names because that row cannot exist on the
     base. This is the delta the pump's production path pays.
   - The base worktree path is padded to the branch path's length, because an embedded source-dir literal
     shifts the code layout.
   - The bench is not paired in CI (`bench/ci-suite.txt`), so this is the gate.
2. **Measure the parse-arena ceiling on base, per lane.** Find the field count at which a dense frame
   fails today, using `tests/support/pmr_allocation_tracking_resource.hpp`. That number is the RED
   evidence for SC-004. It is recorded in `research.md`, never in a comment.
3. **Census of non-canonical test builders.** In a scratch copy only (the research R-8 recipe), add a
   temporary `std::abort()` on a fault-free `!msg_type_is_third` and run the whole suite unfiltered,
   including the loopback, C-ABI and Python round-trips. Every abort is a test or builder to fix before
   FR-004 lands.
4. **Reproduce fixpp#540 on base** (Q-32's first half, FR-015). Record the death test's output in the
   evidence file before the catch is written. Its outcome decides whether the PR settles #540 (SC-008).

## 1. Behaviour cells (contract C-1 to C-6)

| # | Cell group | Contract | RED on base because |
|---|---|---|---|
| Q-1 | TC 2020 2d, 2m, 2t, 3b, 3c, 3e (Active): disregard and continue; NextNumIn kept; the counter reads 1 through C++ and C; one `session_event_garbled_frame` with the row's kind and the discarded bytes; one log record with that kind. Kinds: 2d and 3c `wire_framing_resync`; 2m `wire_invalid_body_length`; 3b `wire_checksum_mismatch`; 3e's CheckSum-not-last shape `wire_invalid_body_length` (L-2), and its not-three-digits and not-SOH-terminated shapes `wire_checksum_mismatch`; 2t `wire_header_out_of_order`. Where a numbered frame is lost (2m, 2t, 3b, 3e), the next good frame draws a ResendRequest whose BeginSeqNo(7) is the lost number. In 2d and 3c the junk precedes a good frame that is delivered, so no number is lost and no ResendRequest is sent | C-1, C-2, FR-003 | the session closes (2d/2m/3b/3c/3e), or the frame is processed (2t) |
| Q-2 | Resync recovery: good ‖ garbage ‖ good in one write, with garbage that does **not** end in SOH (`XYZ8=FIX…`); a truncated frame followed by a good frame; each split at every byte boundary (segmentation independence); a garbage-only buffer is consumed in finite steps | C-1 | the session closes |
| Q-3 | Resync extent: a wrong-CheckSum frame with a well-formed frame inside its extent is one garble, through its own end; a well-formed frame embedded after a malformed candidate is delivered (L-8) | C-1, L-8, L-15 | the session closes |
| Q-4 | Bounded work, using the counted-work instrument (§2), under the bound C-1 states. Each shape is fed in 4096-byte reads and again one byte per read: `8=␁` triples behind a failed large candidate; about L bytes of small valid frames behind a failed large candidate, drained one per feed; staggered nested candidates whose BodyLengths share one `10=`; `8=FIX` repeated with no SOH; a boundary `8=` with no SOH; `8=FIX.4.4␁9=` followed by a run of zeros with no SOH. The digit cap's pair: a valid frame whose BodyLength is zero-padded to exactly `kBodyLengthDigitCap` digits is framed and delivered, and one padded to the cap plus one is a garble of kind `wire_invalid_body_length`, each fed whole and one byte per read | C-1 W-1 to W-4 | the cap-plus-one frame is accepted today (that pair only). The rest: — (new bound; each mutant in §2 is RED, observed by the counted-work instrument) |
| Q-5 | Garble accounting across feeds: a region split across reads counts once; a summary with `regions == 0` emits no event; garbles before a frame in one feed precede it; each failed candidate is its own region, so two adjacent garbles count two; after a wrong-CheckSum frame the next byte is a search position, not a frame boundary, so wrong-CheckSum ‖ junk ‖ good counts one region and the good frame is framed (C-1 Frame start, Extent); logging is at most one record per `max(HeartBtInt, 1 s)`, with the suppressed count, including a session with HeartBtInt = 0 under a sustained garbage stream | C-1, FR-003, E-12 | the session closes |
| Q-6 | Over-L: a frame of L+1 bytes, one of L+1 bytes with a bad CheckSum, and an over-L BodyLength at a candidate the resync search found all close, with no guard or handler reached; the acceptor's first frame over L is refused | C-1, FR-013 | the bad-CheckSum variant would be disregarded without the reorder (mutant) |
| Q-7 | Strict callers are unchanged: with `resync_on_garble = false`, an over-max frame with a bad CheckSum still reports `wire_checksum_mismatch`, and `8=` with no SOH is still partial | C-1, OD-4 | — (regression guard; mutant: apply the reorder with resync off; observed by the returned error kind) |
| Q-8 | Before Active, garbled and 35-not-third frames are disregarded (acceptor first frame, NotConnected, LogonSent, LogonReceived, LogoutSent). In LogoutSent a frame whose third field is not 35 (092's D-9) is also counted, evented and logged. In Disconnected a Framer garble is counted, evented and logged (the pump still runs), while a 35-not-third frame is not scanned and not counted (regression guard; mutant in §2) | C-2 | close, or refusal; for D-9, the count and event (compile-RED on the base, where the frame is disregarded uncounted) |
| Q-9 | A 35-not-third frame after `close()` began, in NotConnected or LogonSent, is counted and evented, with no other effect | C-2 steps 1–2, FR-030 | it is processed |
| Q-10 | BeginString mismatch, each side of the W-2 cap. A value within the cap keeps today's handling: Disconnected with no Logout in Active, refusal before Active, transport close on the acceptor's first frame. A longer value is a garble, disregarded and counted. A session configured with a BeginString longer than the longest supported identifier frames and processes its own frames | FR-008, C-1 W-2 | the longer value is handled as a mismatch (that side only). The shorter side and the configured-long session are regression guards: a cap that ignores the configured length turns the last RED |
| Q-11 | Dense frame of exactly L bytes, per lane including MSVC debug: parsed and delivered, peak ≤ B(L), no spill | C-3 I-1, I-2 | parse fails (the arena or the entry cap) |
| Q-12 | A frame of L split across reads at every boundary near the carry edge | C-1, C-3 | carry overflow between 61442 and 65536 B today |
| Q-13 | Advertised 383 below 4096 or above 262144, and `logon_timeout_ms == 0`: each refused at `register_session` and at `open()`; L follows 383 when set | FR-006, FR-010 | accepted today |
| Q-14 | `open()` with a bounded carry arena or session arena too small for L: an `open()` error, not `std::terminate`. Run on Linux and on the MSVC sandbox, where the carry's container proxy draws on the block (E-2) | E-2, C-7 row 15 | the carry half: `open()` succeeds today, because the carry is built in the pump, not at `open()` (driving the pump then terminates when the carry arena cannot serve the carry); the session-arena half is accepted today, because no B(L) is allocated |
| Q-15 | Defence: shrink the buffer through `session_test_access`, so the late close fires (092 C-6 disposition) | C-3 I-4 | — (proves the defence is reachable; mutant: delete the late close; observed by the session's state and the close event) |
| Q-16 | Establishment timeout, phase (b), per role. The initiator after its Logon, and the acceptor after a matching first frame that leaves it pre-Active (a refused Logon, a non-Logon frame): garbage-only closes **at** T, not before; a silent peer closes at T; the event is recorded. **Readable across T:** through a transport double whose read completes at initiation, so the read arm wins every race, a peer streaming garbage (and, separately, valid non-Logon frames) past T has no frame delivered after T and is closed at the first loop head after T. The double's stream is finite and ends in EOF, so the mutant fails an assertion rather than spinning. On the mock clock, "at T, not before" means: still open after the clock is advanced to T − 1 ms and the io_context drained, and closed after it is advanced to T and drained. Set from C++, C, Python and TOML. The C and Python arms run on the real-time clock, because the C ABI has no mock clock: T = 500 ms, against an initiator's peer that never answers the Logon, closed at an elapsed time ≥ T from a stamp taken before the engine starts and < 5 s, half the 10 s default, so an ignored setter fails while a slow lane keeps seconds of headroom; before the band is fixed, no other pre-Active close source may fire inside it (tasks.md T090) | C-4 | immediate close (garbage), or never (silent, and the readable-across-T valid-frame stream) |
| Q-17 | Establishment timeout, phase (a): a garbage-only acceptor peer closes at the byte budget or at `min(5 s, T)`, whichever comes first; with T < 5 s the first-frame read ends at T; bytes under the budget, sent slowly, are not closed before then; no event. On TLS with T below the handshake bound: a stalled handshake closes at the handshake bound; a handshake that completes after T (no establishment time left when it ends) closes the transport without reading: the peer sends a valid Logon right after the handshake and observes no Logon reply and the close within the bound (before the 5 s first-frame deadline would elapse from the handshake). Observed at the peer, on real TLS; no production seam | C-4 | close at the first garbled byte. The TLS pair: — (regression guard for the stalled handshake; mutant for the late handshake: delete the post-handshake remaining-time check, so the read is issued on its 5 s bound; observed by the peer receiving a Logon reply) |
| Q-18 | Deadline clock. A clock-wide `cancel_sleeps()` from another session during phase (b) does not end the wait early. With `clock_override` set to a second mock clock, advancing only the override does not expire the deadline, and advancing `engine_cfg.clock` does | C-4 | — (mutants: drop the re-arm; measure the deadline on `effective_clock_`. Observed by the session's state and the transport's open flag at the mock-clock instant) |
| Q-19 | Active-read allocations, a regression witness: the real pump driven past Active under a global `operator new` counter, which is first shown to count a known allocation; zero per Active read after a warm-up read | C-4, FR-052 | — (regression guard. Not claimed to catch a never-disarmed race: asio's recycling allocator can serve its state without `operator new`. Q-36 is the disarm's RED witness) |
| Q-20 | Liveness, one cell per SC-005 class: no TestRequest within the interval of that frame. The interval is HeartBtInt measured from the last refresh: the liveness loop sends its TestRequest once `last_inbound_steady_ + HeartBtInt` is reached, and `test_request_threshold` is not read (fixpp#535; re-derive in `run_liveness_loop`). Each cell sends the class frame at t1, after the last refreshing frame at t0, and asserts on the mock clock that no TestRequest is sent before t1 + HeartBtInt, where the base sends one at t0 + HeartBtInt | C-5 | a TestRequest is sent |
| Q-21 | Liveness, garbled and faulty frames do not refresh (092 FR-018 twins) | C-5 | — (regression guard; mutant: refresh them; observed by the TestRequest in the outbound frames) |
| Q-22 | #523, per role: Logon ‖ dictionary-invalid frame, graceful close during the hydrate or the peer-reset yield; no toAdmin, Reject or state write after close began; the LogoutSent confirmation cell stays green | C-2 step 2 | the Reject reaches toAdmin |
| Q-23 | #524 without a teardown reset, per role: close drains inside the unit, and the durable state equals the unit's targets; the peer's next Logon at 34=2 without 141=Y is accepted (789 off) | C-6 | NextNumIn is stranded at 1 |
| Q-24 | #524 with a teardown reset, per role: final state (1, 1) | C-6 | — (green on base; regression guard; observed by the durable store counters) |
| Q-25 | #524 with a non-overriding store (default body), both teardown settings: the table holds | C-6 | — (mutant: delete `close()`'s wait; observed by the durable store counters) |
| Q-26 | #524 under `Engine::stop()` during the unit, for `MemoryStore`, `FileStore` and a default-body `HookedStore`, with and without a teardown reset: the table holds, and per role no `toAdmin`, no reset event, no `onLogon` and no Active transition is observed after `Engine::stop()`'s step 1 has run on the session's strand (the cell holds the store operation, so the stop handler runs first) | C-6, E-13 | the unit is interrupted, leaving the pre-unit state (expected RED on base for the default body and `FileStore`). The effect assertions: mutant: drop the engine-stop flag from the predicate; observed by the application double's callback log and the event ring |
| Q-27 | #524 with the `close()` wait expiring: the event is recorded and `close()` completes. For `FileStore`, (1, 1) still holds after expiry (FIFO writer lock). Re-arm arm: a clock-wide `cancel_sleeps()` on `effective_clock_` from another session during the wait does not end it; no `session_event_close_reset_wait_expired` is recorded before `effective_clock_` reaches the bound. (`close()`'s own sweep comes before its teardown reset, so only another sweep can reach the wait) | C-6 | — (new event; mutants: expire without recording; wait on a one-shot sleep without `await_deadline`'s re-arm. Observed by the event ring and the durable counters) |
| Q-28 | `reset_to` with a target outside {1, 2} is refused with no effect, on the default body and on both overrides | C-6, E-9 | — (new operation; mutant: accept any target; observed by the return value and the store's counters) |
| Q-29 | `FileStore::reset_to` crash atomicity: fault injection between the temp write and the rename, then a restart, sees either the old or the new counters and never (1, 1) partway | C-6 | — (new operation; mutant in §2; observed by the restarted store's counters) |
| Q-30 | C-ABI setter and getter. The setter refuses a null handle and zero. The getter refuses a null handle and a null `out`, writes 0 before the session exists, and the count after a garble. Python on both wheel lanes | C-7 rows 7–8 | the symbols do not exist |
| Q-31 | TOML: `logon_timeout_ms` accepted as a bare integer; zero, negative, above `UINT32_MAX` and a non-integer each refused with a diagnostic | C-7 row 9 | the key is unrecognised |
| Q-32 | fixpp#540. **First, the reproduction:** a `Parser` over a small `monotonic_buffer_resource` whose upstream is set explicitly to `null_memory_resource`, sized so the parse succeeds and the unknown-field list does not fit; `unknown_fields()` under `EXPECT_DEATH`, run on the base and its output recorded. **The cell:** under `EXPECT_EXIT(..., ExitedWithCode(0), ...)`, so a terminate on the base is a recorded failure and does not abort the suite, the same call returns an empty view and a second call returns the same empty view | C-3 I-5, FR-015 | it terminates, if the reproduction confirms #540. If the reproduction does not terminate, #540 is closed as not a bug and this cell is a regression guard (mutant: remove the catch) |
| Q-33 | Lazy reads at headroom exhaustion inside a callback, per API, through the session over a dense frame: `group_slices()` gives an empty span; `fixpp_group_get_nested_group` returns `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`; `fixpp_msg_get_group` returns `FIXPP_ERR_TYPE_MISMATCH`; `unknown_fields()` gives an empty view; the session stays Active and processes the next frame. These reports hold where the spill witness is null, so these assertions run only on those lanes (on MSVC debug the witness forwards to the heap, and research R-3's "nothing spilled" rule stands for the parse). On MSVC debug the same callback runs with that lane's assertions instead: each read succeeds, the spill witness records the spill, and the session stays Active (FR-011). It has no C cursor shell exhaustion arm: that is L-17, fixpp#541, outside 093 | C-3 I-5, L-5 | the `unknown_fields()` arm terminates, if Q-32's reproduction confirms #540; otherwise it is a regression guard. The other arms: — (regression guards pinning today's reports; observed by each call's result and the session's state). The MSVC-debug branch: the reads are a regression guard, and the spill assertion is compile-RED on the base (no witness); mutant in §2 |
| Q-34 | First-frame summary hand-off: k garbled regions before a matching Logon on the acceptor's first-frame read; after `open()` the counter reads k and exactly one `session_event_garbled_frame` is recorded | FR-003, E-4 | the connection closes at the first garbled byte |
| Q-35 | Default timeout: a `SessionConfig` with no timeout set reads 10000, and a silent peer in phase (b) is closed at 10 s on the mock clock | FR-006, E-7 | the field does not exist |
| Q-37 | C-ABI witnesses for C-7 rows 1 to 6, driven through the C ABI. For each row's trigger, assert afterwards, through C: `fixpp_session_is_established`; the result of `fixpp_session_send`; whether the callbacks registered with `fixpp_session_register_callback` and `fixpp_session_register_send_callback` fire; and the result of `fixpp_session_close`. The triggers: row 1, a garbled frame in Active; row 2, a 35-not-third frame in Active and a 35-not-third Logon; row 3, a frame of 64 KiB split at the carry edge; row 4, a dense frame of 64 KiB; row 5, a pre-Active peer past T; row 6, liveness-only traffic past HeartBtInt with the TestRequest unanswered | C-7 rows 1–6 | row 1, 3, 4 and 6 triggers end the session today, and the branch keeps it up; row 2's frame reaches the receive callback today and its Logon is refused, and the branch disregards both; row 5's session stays up today, and the branch ends it |
| Q-36 | Disarm at the first Active, per role, with **no application attached**: the session reaches Active before T, idles past T with T below HeartBtInt so liveness does not act, and stays Active with its transport open | C-4, E-6, FR-052 | — (green on base, which has no deadline; mutants: never disarm, and disarm on `onLogon_fired_` instead of `reached_active_`; observed by the session's state and the transport's open flag) |

## 2. Instruments: each must be shown able to fail

- Each cell above that claims a RED must be run once on the base, and its failing output recorded in the
  evidence file.
- **Counted-work instrument** (Q-4). A test-only count of the bytes the Framer reads, sums and moves, read
  through a `tests/support/` access seam. It must be shown to report a value over the bound on a mutant
  before its zero-violation result is believed.
- **Mutants**, each RED against at least one named cell:
  - delete the resync (pass `resync_on_garble = false` in the pump) → Q-1, Q-2;
  - restore the SOH-anchored start rule → Q-2 (`XYZ8=FIX…`, truncation);
  - compact on every feed, carry-only included → Q-4 (small frames);
  - compact on every non-empty feed → Q-4 (one-byte reads);
  - sum a wrong-CheckSum frame's nested candidates (do not consume it whole) → Q-4 (shared `10=`), Q-3;
  - uncap the BeginString scan → Q-4 (`8=FIX` with no SOH, and a boundary `8=` with no SOH), Q-10;
  - cap the BeginString at the longest supported identifier, ignoring the configured length → Q-10
    (the configured-long session);
  - uncap the BodyLength digit run → Q-4 (the run of zeros fed one byte per read, and the cap-plus-one
    frame);
  - restore `frame_len > max` after the checksum in resync mode → Q-6;
  - apply the reorder with resync off → Q-7;
  - restore the 16 KiB arena and the default entry cap → Q-11;
  - delete the 35-not-third check → Q-1 (2t), Q-8;
  - run step 1 in Disconnected too → Q-8 (the Disconnected regression guard; observed by the counter);
  - put the FR-030 guard before step 1 → Q-9;
  - restore the old liveness writer → Q-20;
  - delete the FR-030 guard → Q-22;
  - delete `close()`'s wait → Q-25;
  - delete the unit's cancellation shield → Q-26 (durable counters);
  - drop the engine-stop flag from `logon_arm_superseded` → Q-26 (the per-role effect assertions);
  - make `reset_to`'s override non-atomic (reset, then advance) → Q-29;
  - never disarm the deadline race → Q-36;
  - disarm on `onLogon_fired_` instead of `reached_active_` → Q-36;
  - delete the loop-head deadline test, keeping only the race → Q-16 (readable across T). The transport
    double completes its read at initiation, so the read arm wins every race and the mutant is RED
    whatever order asio uses;
  - delete the post-handshake remaining-time check → Q-17 (the late TLS handshake: the peer receives a
    Logon reply);
  - measure the deadline on `effective_clock_` → Q-18;
  - drop the deadline's re-arm → Q-18;
  - make `close()`'s wait a one-shot sleep without `await_deadline`'s re-arm → Q-27 (re-arm arm);
  - delete the 1 s floor of the log rate → Q-5 (HeartBtInt = 0);
  - remove `unknown_fields()`'s catch → Q-32, Q-33;
  - make the spill witness forward without recording → Q-33's MSVC-debug branch (the spill
    assertion; run on the MSVC sandbox, tasks.md T113).
- The spill witness must be shown to record a spill. Feed a frame denser than B(L)'s design point
  through the test-access shrink.
- **Global allocation counter** (Q-19, and the pre-Active measurement of L-13). It counts only when a
  replacement `operator new` is linked into the test binary, and otherwise reads 0 by construction. Show
  it counting a known allocation in the same binary before any zero it reports is believed.
- **Behavioural witnesses for the deadline** (Q-16, Q-36). Each mutant-only deadline cell observes the
  session's state and the transport's open flag at a mock-clock instant, never an allocation count.
- **Durable state** (Q-23 to Q-29). For `FileStore`, reopen the store directory after the session
  ends and read its next-in and next-out. For `MemoryStore` and `HookedStore`, which have no restart,
  read the same store object's counters after `close()` or `Engine::stop()` has returned. Q-23's
  peer-visible half (the next Logon at 34=2 accepted) is the behavioural check on top.
- `HookedStore` forwards `reset_to` and fires its hooks there. Show it with a cell that fails when the
  forwarding is removed. That is its forward mode (Q-23, Q-24). Its default-body mode calls the base
  `MessageStore::reset_to`, so the default body runs over its own `reset()` and `next_seqnum()`: Q-25
  and Q-26 use that mode, and the delete-the-shield mutant is run against it.

## 3. Regression and cost

- The whole ctest suite unfiltered on `linux-clang-debug`, `-asan`, `-ubsan`, `-tsan` and `-release`,
  one preset at a time, each under the 16 GiB cap. Python round-trips on the `-py` lanes. The MSVC
  sandbox (take `.sandbox-lock` first) for the dense-L, spill and getter-typemap cells.
- The C-ABI symbol golden, the freeze hashes, `check_capi_reentrancy.sh`, and the abidiff/`nm` audit.
- Every `ci-script-pins` step, driven from tier1.yml's YAML.
- `check-comment-claims.py --base origin/main`, the line-citation shift audit, `check_brain.py gate`,
  and the external sweep.
- Bench: the paired run from §0 again on the final head, within `[const §VIII.2]`'s +5 % budget per
  case: `on_inbound_frame_bench` with validation off and on, and both `framer_bench` pairings against
  the base's `BM_Framer_Feed_NoCarry` (the candidate's `BM_Framer_Feed_NoCarry`, and the candidate's
  `BM_Framer_Feed_NoCarry_Resync`).
- The fuzz arms: `fuzz_wire_framer` with `resync_on_garble = true` (no crash, no unbounded loop, and the
  counted-work bound of Q-4 asserted per input), and `fuzz_transport_read_path`.

## 4. Traceability: requirement → contract clause → cells

| Requirement | Contract | Cells |
|---|---|---|
| FR-001 | C-1 | Q-1, Q-2, Q-8 |
| FR-002 | C-1 (start rule, extent, W-1 to W-4, the bound) | Q-2, Q-3, Q-4, Q-5 |
| FR-003 | C-1 reporting, C-2 step 1, E-4, E-12 | Q-1 (event, log), Q-5, Q-8 (D-9, Disconnected), Q-9, Q-30, Q-34 |
| FR-004 | C-2 step 1 | Q-1 (2t), Q-8 |
| FR-005 | C-2 step 1, D-8 unreachable | Q-8 |
| FR-006 | C-4 | Q-13, Q-16, Q-17, Q-18, Q-35, Q-36 |
| FR-007 | C-7 rows 7–9 | Q-16 (C and Python setters), Q-30 (setter and getter refusals), Q-31 |
| FR-008 | C-1 W-2 (the BeginString cap's carve-out) | Q-10 |
| FR-010 | C-1 configuration, E-2 | Q-6, Q-12, Q-13, Q-14 |
| FR-011 | C-3 I-1, I-5 | Q-11, Q-33 |
| FR-012 | C-3 I-2 | Q-11, the spill witness |
| FR-013 | C-1 close rows | Q-6, Q-7 |
| FR-014 | C-3 I-4 | Q-15 |
| FR-015 | C-3 I-5 (the `unknown_fields()` row), C-7 row 18 | Q-32, Q-33 |
| FR-020 | C-2 step 4, C-5 | Q-20 |
| FR-021 | C-5 | Q-21 |
| FR-030 | C-2 step 2 | Q-9, Q-22 |
| FR-040 | C-6 store operation | Q-28, Q-29 |
| FR-041 | C-6 unit, the engine-stop flag, `close()` | Q-23 to Q-27 |
| FR-042 | C-6 | the existing test-local `MessageStore` subclasses compile under `-Werror` (re-derive with `git grep -n "public .*MessageStore"`); Q-25 |
| FR-050 | C-8 | the B&L delta, checked at Gate B |
| FR-051 | C-7 | the golden, the freeze hashes, the `version.h` history, the per-declaration BREAKING blocks on the five observers; Q-37 |
| FR-052 | C-1 W-4, C-4 | Q-4, Q-19, Q-36 |
| FR-053 | — | enforced by B25's census, a dependency rather than a 093 cell |
| SC-001 | C-1, C-2 | Q-1 |
| SC-002 | C-1 | Q-2, Q-3, Q-4 |
| SC-003 | C-4 | Q-16, Q-17, Q-36 |
| SC-004 | C-3 | Q-6, Q-11 |
| SC-005 | C-5 | Q-20 |
| SC-006 | C-2, C-6 | Q-22 to Q-27 |
| SC-007 | E-2 | the §0/§3 bench; the per-lane peak of Q-11 |
| SC-008 | C-3 I-5 | Q-32 |
