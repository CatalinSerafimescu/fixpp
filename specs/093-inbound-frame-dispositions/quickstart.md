# Quickstart: validating 093-inbound-frame-dispositions

This file is a validation guide, not an implementation. Every behaviour cell is written first and shown
RED on the base (`00c1f720`, or the branch's merge base at implementation), except where SC-006 names a
regression guard that is green on the base by design. §4 maps every FR and SC to its contract clause
and its cells.

## 0. Before any production change

1. **Take the timing baseline before the first edit.** Run a paired base-vs-branch bench of
   `bin/on_inbound_frame_bench`, once with `validate_inbound_messages` false and once with it true
   (`[const §VIII.2]`).
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

## 1. Behaviour cells (contract C-1 to C-6)

| # | Cell group | Contract | RED on base because |
|---|---|---|---|
| Q-1 | TC 2020 2d, 2m, 2t, 3b, 3c, 3e (Active): disregard and continue; NextNumIn kept; ResendRequest for the gap; the counter reads 1 through C++ and C; one `session_event_garbled_frame` with the expected kind and bytes; one log record with that kind | C-1, C-2, FR-003 | the session closes (2d/2m/3b/3c/3e), or the frame is processed (2t) |
| Q-2 | Resync recovery: good ‖ garbage ‖ good in one write, with garbage that does **not** end in SOH (`XYZ8=FIX…`); a truncated frame followed by a good frame; each split at every byte boundary (segmentation independence); a garbage-only buffer is consumed in finite steps | C-1 | the session closes |
| Q-3 | Resync extent: a wrong-CheckSum frame with a well-formed frame inside its extent is one garble, through its own end; a well-formed frame embedded after a malformed candidate is delivered (L-8) | C-1, L-8, L-15 | the session closes |
| Q-4 | Bounded work, using the counted-work instrument (§2), under a bound derived from C-1 W-1 to W-4. Each shape is fed in 4096-byte reads and again one byte per read: `8=␁` triples behind a failed large candidate; about L bytes of small valid frames behind a failed large candidate, drained one per feed; staggered nested candidates whose BodyLengths share one `10=`; `8=FIX` repeated with no SOH; a boundary `8=` with no SOH | C-1 W-1 to W-4 | — (new bound; each mutant in §2 is RED) |
| Q-5 | Garble accounting across feeds: a region split across reads counts once; a summary with `regions == 0` emits no event; garbles before a frame in one feed precede it; logging is at most one record per heartbeat interval, with the suppressed count | C-1, FR-003, E-12 | the session closes |
| Q-6 | Over-L: a frame of L+1 bytes, one of L+1 bytes with a bad CheckSum, and an over-L BodyLength at a candidate the resync search found all close, with no guard or handler reached; the acceptor's first frame over L is refused | C-1, FR-013 | the bad-CheckSum variant would be disregarded without the reorder (mutant) |
| Q-7 | Strict callers are unchanged: with `resync_on_garble = false`, an over-max frame with a bad CheckSum still reports `wire_checksum_mismatch`, and `8=` with no SOH is still partial | C-1, OD-4 | — (regression guard) |
| Q-8 | Before Active, garbled and 35-not-third frames are disregarded (acceptor first frame, NotConnected, LogonSent, LogonReceived, LogoutSent) | C-2 | close, or refusal |
| Q-9 | A 35-not-third frame after `close()` began, in NotConnected or LogonSent, is counted and evented, with no other effect | C-2 steps 1–2, FR-030 | it is processed |
| Q-10 | BeginString mismatch, each side of the W-2 cap. A value no longer than the longest supported identifier keeps today's handling: Disconnected with no Logout in Active, refusal before Active, transport close on the acceptor's first frame. A longer value is a garble, disregarded and counted | FR-008, C-1 W-2 | the longer value is handled as a mismatch (that side only; the shorter side is a regression guard) |
| Q-11 | Dense frame of exactly L bytes, per lane including MSVC debug: parsed and delivered, peak ≤ B(L), no spill | C-3 I-1, I-2 | parse fails (the arena or the entry cap) |
| Q-12 | A frame of L split across reads at every boundary near the carry edge | C-1, C-3 | carry overflow between 61442 and 65536 B today |
| Q-13 | Advertised 383 below 4096 or above 262144, and `logon_timeout_ms == 0`: each refused at `register_session` and at `open()`; L follows 383 when set | FR-006, FR-010 | accepted today |
| Q-14 | `open()` with a bounded carry arena or session arena too small for L: an `open()` error, not `std::terminate` | E-2, C-7 row 15 | — (terminates today when the arena is under 64 KiB) |
| Q-15 | Defence: shrink the buffer through `session_test_access`, so the late close fires (092 C-6 disposition) | C-3 I-4 | — (proves the defence is reachable) |
| Q-16 | Establishment timeout, phase (b), per role. The initiator after its Logon, and the acceptor after a matching first frame that leaves it pre-Active (a refused Logon, a non-Logon frame): garbage-only closes **at** T, not before; a silent peer closes at T; the event is recorded. Set from C++, C, Python and TOML | C-4 | immediate close (garbage), or never (silent) |
| Q-17 | Establishment timeout, phase (a): a garbage-only acceptor peer closes at the byte budget or at `min(5 s, T)`, whichever comes first; with T < 5 s the first-frame read ends at T; bytes under the budget, sent slowly, are not closed before then; no event | C-4 | close at the first garbled byte |
| Q-18 | Deadline re-arm: a clock-wide `cancel_sleeps()` from another session during phase (b) does not end the wait early | C-4 | — (mutant: no re-arm) |
| Q-19 | Active-read allocations: the real pump driven past Active under a counting resource; zero allocations per Active read | C-4, FR-052 | — (mutant: never disarm) |
| Q-20 | Liveness, one cell per SC-005 class: no TestRequest within the interval of that frame | C-5 | a TestRequest is sent |
| Q-21 | Liveness, garbled and faulty frames do not refresh (FR-018 twins) | C-5 | — (regression guard; the inverted twin is RED on a mutant that refreshes them) |
| Q-22 | #523, per role: Logon ‖ dictionary-invalid frame, graceful close during the hydrate or the peer-reset yield; no toAdmin, Reject or state write after close began; the LogoutSent confirmation cell stays green | C-2 step 2 | the Reject reaches toAdmin |
| Q-23 | #524 without a teardown reset, per role: close drains inside the unit, and the durable state equals the unit's targets; the peer's next Logon at 34=2 without 141=Y is accepted (789 off) | C-6 | NextNumIn is stranded at 1 |
| Q-24 | #524 with a teardown reset, per role: final state (1, 1) | C-6 | — (green on base; regression guard) |
| Q-25 | #524 with a non-overriding store (default body), both teardown settings: the table holds; deleting `close()`'s wait gives RED | C-6 | — (proves the wait carries it) |
| Q-26 | #524 under `Engine::stop()` during the unit, for `MemoryStore`, `FileStore` and a default-body `HookedStore`, with and without a teardown reset: the table holds | C-6 | the unit is interrupted, leaving the pre-unit state (expected RED on base for the default body and `FileStore`) |
| Q-27 | #524 with the `close()` wait expiring: the event is recorded and `close()` completes. For `FileStore`, (1, 1) still holds after expiry (FIFO writer lock) | C-6 | — |
| Q-28 | `reset_to` with a target outside {1, 2} is refused with no effect, on the default body and on both overrides | C-6, E-9 | — (new operation) |
| Q-29 | `FileStore::reset_to` crash atomicity: fault injection between the temp write and the rename, then a restart, sees either the old or the new counters and never (1, 1) partway | C-6 | — (new operation) |
| Q-30 | C-ABI getter: null handle and null `out` refused; 0 before the session exists; the count after a garble. Python on both wheel lanes | C-7 rows 7–8 | the symbols do not exist |
| Q-31 | TOML: `logon_timeout_ms` accepted as a bare integer; zero, negative, above `UINT32_MAX` and a non-integer each refused with a diagnostic | C-7 row 9 | the key is unrecognised |

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
  - restore `frame_len > max` after the checksum in resync mode → Q-6;
  - apply the reorder with resync off → Q-7;
  - restore the 16 KiB arena and the default entry cap → Q-11;
  - delete the 35-not-third check → Q-1 (2t), Q-8;
  - put the FR-030 guard before step 1 → Q-9;
  - restore the old liveness writer → Q-20;
  - delete the FR-030 guard → Q-22;
  - delete `close()`'s wait → Q-25;
  - delete the unit's cancellation shield → Q-26;
  - make `reset_to`'s override non-atomic (reset, then advance) → Q-29;
  - never disarm the deadline race → Q-19;
  - drop the deadline's re-arm → Q-18.
- The spill witness must be shown to record a spill. Feed a frame denser than B(L)'s design point
  through the test-access shrink.
- `HookedStore` forwards `reset_to` and fires its hooks there. Show it with a cell that fails when the
  forwarding is removed.

## 3. Regression and cost

- The whole ctest suite unfiltered on `linux-clang-debug`, `-asan`, `-ubsan`, `-tsan` and `-release`,
  one preset at a time, each under the 16 GiB cap. Python round-trips on the `-py` lanes. The MSVC
  sandbox (take `.sandbox-lock` first) for the dense-L, spill and getter-typemap cells.
- The C-ABI symbol golden, the freeze hashes, `check_capi_reentrancy.sh`, and the abidiff/`nm` audit.
- Every `ci-script-pins` step, driven from tier1.yml's YAML.
- `check-comment-claims.py --base origin/main`, the line-citation shift audit, `check_brain.py gate`,
  and the external sweep.
- Bench: the paired run from §0 again on the final head, within `[const §VIII.2]`'s +5 % budget.
- The fuzz arms: `fuzz_wire_framer` with `resync_on_garble = true` (no crash, no unbounded loop, and the
  counted-work bound of Q-4 asserted per input), and `fuzz_transport_read_path`.

## 4. Traceability: requirement → contract clause → cells

| Requirement | Contract | Cells |
|---|---|---|
| FR-001 | C-1 | Q-1, Q-2, Q-8 |
| FR-002 | C-1 (start rule, extent, W-1 to W-4) | Q-2, Q-3, Q-4, Q-5 |
| FR-003 | C-1 reporting, C-2 step 1, E-4, E-12 | Q-1 (event, log), Q-5, Q-9, Q-30 |
| FR-004 | C-2 step 1 | Q-1 (2t), Q-8 |
| FR-005 | C-2 step 1, D-8 unreachable | Q-8 |
| FR-006 | C-4 | Q-13, Q-16, Q-17, Q-18 |
| FR-007 | C-7 rows 7–9 | Q-16 (C and Python setters), Q-30, Q-31 |
| FR-008 | C-1 W-2 (the cap's carve-out) | Q-10 |
| FR-010 | C-1 configuration, E-2 | Q-6, Q-12, Q-13, Q-14 |
| FR-011 | C-3 I-1, I-5 | Q-11 |
| FR-012 | C-3 I-2 | Q-11, the spill witness |
| FR-013 | C-1 close rows | Q-6, Q-7 |
| FR-014 | C-3 I-4 | Q-15 |
| FR-020 | C-2 step 4, C-5 | Q-20 |
| FR-021 | C-5 | Q-21 |
| FR-030 | C-2 step 2 | Q-9, Q-22 |
| FR-040 | C-6 store operation | Q-28, Q-29 |
| FR-041 | C-6 unit, `close()` | Q-23 to Q-27 |
| FR-042 | C-6 | the existing test-local `MessageStore` subclasses compile under `-Werror` (re-derive with `git grep -n "public .*MessageStore"`); Q-25 |
| FR-050 | C-8 | the B&L delta, checked at Gate B |
| FR-051 | C-7 | the golden, the freeze hashes, the `version.h` history, the per-declaration BREAKING blocks |
| FR-052 | C-1 W-4, C-4 | Q-4, Q-19 |
| FR-053 | — | enforced by B25's census, a dependency rather than a 093 cell |
| SC-001 | C-1, C-2 | Q-1 |
| SC-002 | C-1 | Q-2, Q-3, Q-4 |
| SC-003 | C-4 | Q-16, Q-17 |
| SC-004 | C-3 | Q-6, Q-11 |
| SC-005 | C-5 | Q-20 |
| SC-006 | C-2, C-6 | Q-22 to Q-27 |
| SC-007 | E-2 | the §0/§3 bench; the per-lane peak of Q-11 |
