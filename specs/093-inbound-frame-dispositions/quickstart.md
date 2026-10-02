# Quickstart: validating 093-inbound-frame-dispositions

This file is a validation guide, not an implementation. Every behaviour cell is written first and shown
RED on the base (`00c1f720`, or the branch's merge base at implementation), except where SC-006 names a
regression guard that is green on the base by design.

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

| Cell group | Contract | RED on base because |
|---|---|---|
| TC 2020 2d, 2m, 2t, 3b, 3c, 3e (Active): disregard and continue; NextNumIn kept; ResendRequest for the gap; the counter reads 1 through C++ and C | C-1, C-2 | the session closes (2d/2m/3b/3c/3e), or the frame is processed (2t) |
| Resync: good ‖ garbage ‖ good in one write, and the same split at every byte boundary (segmentation independence); a garbage-only buffer is consumed in finite steps | C-1 | the session closes |
| Over-L: a frame of L+1 bytes, and one of L+1 bytes with a bad CheckSum, both close, with no guard or handler reached | C-1, FR-013 | the bad-CheckSum variant would be disregarded without the reorder (mutant) |
| Before Active, garbled and 35-not-third frames are disregarded (acceptor first frame, NotConnected, LogonSent, LogonReceived, LogoutSent) | C-2 | close, or refusal |
| Dense frame of exactly L bytes, per lane including MSVC debug: parsed and delivered, peak ≤ B(L), no spill | C-3 I-1, I-2 | parse fails (the arena or the entry cap) |
| A frame of L split across reads at every boundary near the carry edge | C-1, C-3 | carry overflow between 61442 and 65536 B today |
| Advertised 383 below 4096 or above 262144 is refused at `open()`; L follows 383 when set | FR-010 | accepted today |
| Defence: shrink the buffer through `session_test_access`, so the late close fires (092 C-6 disposition) | C-3 I-4 | — (proves the defence is reachable) |
| Establishment timeout, per role: garbage-only closes **at** T, not before; a silent peer after the Logon closes at T; a pre-Active refused session closes at T; set from C++, C, Python and TOML; zero refused | C-4 | immediate close (garbage), or never (silent) |
| Liveness, one cell per SC-005 class: no TestRequest within the interval of that frame | C-5 | a TestRequest is sent |
| Liveness, garbled and faulty frames do not refresh (FR-018 twins) | C-5 | — (regression guard; inverted twin is RED on a mutant that refreshes them) |
| #523, per role: Logon ‖ dictionary-invalid frame, graceful close during the hydrate or the peer-reset yield; no toAdmin, Reject, event or counter after close began; the LogoutSent confirmation cell stays green | C-2 step 0 | the Reject reaches toAdmin |
| #524 without a teardown reset, per role: close drains inside the unit, and the durable state equals the unit's targets; the peer's next Logon at 34=2 without 141=Y is accepted (789 off) | C-6 | NextNumIn is stranded at 1 |
| #524 with a teardown reset, per role: final state (1, 1) | C-6 | — (green on base; regression guard) |
| #524 with a non-overriding store (default body), both teardown settings: the table holds; deleting `close()`'s wait gives RED | C-6 | — (proves the wait carries it) |
| #524 with the `close()` wait expiring: event recorded, `close()` completes | C-6 | — |
| `FileStore::reset_to` crash atomicity: fault injection between the temp write and the rename, then a restart, sees either the old or the new counters and never (1, 1) partway | C-6 | — (new operation) |

## 2. Instruments: each must be shown able to fail

- Each cell above that claims a RED must be run once on the base, and its failing output recorded in the
  evidence file.
- **Mutants**, each RED against at least one named cell:
  - delete the resync (pass `resync_on_garble = false` in the pump);
  - restore `frame_len > max` after the checksum;
  - restore the 16 KiB arena and the default entry cap;
  - delete the 35-not-third check;
  - restore the old liveness writer;
  - delete the FR-030 guard;
  - delete `close()`'s wait;
  - make `reset_to`'s override non-atomic (reset, then advance).
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
- The fuzz arms: `fuzz_wire_framer` with `resync_on_garble = true` (no crash, no unbounded loop, at most
  one garble per feed), and `fuzz_transport_read_path`.
