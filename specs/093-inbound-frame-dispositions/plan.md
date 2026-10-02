# Implementation Plan: Inbound frames 092 leaves out

**Branch**: `093-inbound-frame-dispositions` | **Date**: 2026-10-02 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/093-inbound-frame-dispositions/spec.md` (clarified
2026-10-02, with the plan-research owner rulings recorded there).

**Issues**: fixpp#514, #515, #516, #523, #524 (batch B22). **Depends on**: fixpp#530 (B25) merging first.

## Summary

This bundle makes five changes to fixpp's inbound path, on one shared review surface: the read pump,
`Session::on_inbound_frame`'s arms, and the 141=Y reset unit.

1. **#514: garbled frames are disregarded, not fatal.** The Framer gains an opt-in resync that the read
   pump and the acceptor's first-frame read turn on.
   - A frame that fails framing is skipped, and framing resumes at the next `8=FIX`, independent of how
     reads are segmented. The Framer's work per byte is bounded.
   - A frame whose third field is not 35 is disregarded in every state and both validation modes.
   - Every disregard is counted exactly, and evented and logged in rate-bounded summaries. The count
     is readable through a new C-ABI getter.
   - A new establishment timeout (`logon_timeout_ms`, 10 s, zero refused, with C-ABI, Python and TOML
     surfaces) bounds the pre-Active waits this creates, in two phases (contract C-4). Before the
     acceptor has a Session, the first-frame read's stricter bounds apply. After that, the pump races
     the deadline and closes the transport on expiry.
2. **#515: every admitted frame parses.** One inbound limit L comes from the config: the advertised
   383, between 4 KiB and 256 KiB, else 64 KiB. It is checked at `register_session` and at `open()`.
   It sizes:
   - both Framer limits;
   - the carry (L plus one read);
   - a per-session parse buffer;
   - the offset-table entry cap.

   The carry and the parse buffer are allocated once at `open()`. All of these are sized for the
   densest legal layout, 3 bytes per field. A frame over L is refused at framing, before its CheckSum,
   with a close.
3. **#516: liveness refreshes on every frame that is neither garbled nor faulty.** It happens at one
   point, right after the fault checks.
4. **#523: a closing session's NotConnected and LogonSent arms act on nothing.**
5. **#524: the 141=Y reset is one store operation.** `MessageStore::reset_to` is a new non-pure virtual,
   which `MemoryStore` and `FileStore` override atomically.
   - The unit shields itself from cancellation, because `Engine::stop()` reaches it before `close()`
     does.
   - It sets the manager first, with no yield.
   - A `close()` that is about to issue a teardown reset waits, event-driven and bounded, for an
     in-flight unit.

C-ABI: one MINOR bump, two symbols, **BREAKING** declared on each affected declaration
(`[const §X.7]`; contract C-7).

## Technical Context

**Language/Version**: C++23 (the project toolchain). No new dependency.

**Primary Dependencies**: standalone asio (coroutines, `parallel_group` for the deadline race), std::pmr.

**Storage**: `MessageStore` implementations: `MemoryStore`, and `FileStore`'s append-only log with
temp-file-and-rename resets.

**Testing**: GoogleTest through ctest; the mock clock for timer and liveness cells; `HookedStore` for
the close-during-suspension cells; the C-ABI and Python round-trips; fuzz harnesses (libFuzzer); the
paired bench.

**Target Platform**: Linux (clang, gcc, libc++, sanitizer lanes) and Windows MSVC (the debug and ASan
sandbox).

**Project Type**: a library with a C ABI and Python bindings.

**Performance Goals**: steady-state inbound throughput unchanged within `[const §VIII.2]`'s +5 % budget.
It is expected to improve slightly, since each frame drops one or two 16 KiB stack memsets.

**Constraints**:
- `[const §VIII.5]`: no heap between parse and callback, and no per-frame allocation.
- Per-session memory rises to the carry (L + 4 KiB) plus B(L), stated as a formula with worked totals
  (owner ruling; research R-3 has the derived totals).

**Scale/Scope**: about 10 source files and about 15 test files. Research R-8 lists the cells that flip.

**Unknowns**: none open after Gate A round 1. R-9's two items are settled by design and witnessed by
cells. Three design values are measured at implementation, not chosen here: `kAlignPad`,
`kCallbackReadHeadroom` and `kContainerSlack` (data-model E-2).

## Constitution Check

*GATE: passed before Phase 0; re-checked below after Phase 1.*

| Article | Requirement | Status |
|---|---|---|
| VII §3–4 TDD | failing test first | Planned. Every quickstart §1 cell is RED-first on the base, except the named regression guards. The RED evidence for SC-004 is the ceiling measured in quickstart §0 |
| VII §5 conformance | the FIX-TC corpus | TC 2d/2m/2t/3b/3c/3e move from diverging to conforming, with cells. TC 2i is unchanged (FR-008), and its Logout gap belongs to #534 |
| VII §7 fuzz | parser-touching code needs a fuzz harness | `fuzz_wire_framer` gains a `resync_on_garble = true` arm asserting termination and contract C-1's counted-work bound per input. `fuzz_transport_read_path` covers the pump |
| VII §8 test grouping | new isolation-safe tests go into a grouped bucket, selected by a label | Planned (a `093` label). The timer and coroutine cells stay standalone |
| VIII §2 perf | paired merge-base A-B-A-B within +5 % | A manual paired run of `on_inbound_frame_bench`, with validation off and on, base path padded (quickstart §0, §3) |
| VIII §5 zero-alloc | no heap between parse and callback | Held for the parse: the buffer is allocated once at `open()`, and the resync scans in place. A spill witness proves there is no hidden heap use on any lane. **Not zero before the first Active.** The establishment deadline race (asio parallel group) allocates its shared state per pre-Active read. That is outside the parse-to-callback window, and FR-052 is scoped to start at the first Active. The pre-Active count is measured (research R-9) and disclosed (L-13). The Active scope is witnessed by Q-19, which drives the real pump past Active, and its never-disarm mutant |
| IX sanitizers / coverage | per-line coverage assessment | The `/speckit-verify` matrix. The resync, deadline and reset_to branches are covered by the cells |
| X §4 append-only enums | `core::error` | No new error code: criterion 3 reuses `wire_header_out_of_order`, and `reset_to` reuses `session_invalid_argument`. `SessionEvent` alternatives are appended |
| X §7 ABI | C-ABI changes versioned | **MINOR bump; BREAKING on each affected declaration**, and in `version.h` only where none carries it. The 1.10 sentences 093 falsifies are amended in place (FR-051, contract C-7). `gh release list --exclude-drafts` must be empty at implementation. The C++ additions are source-compatible; `MessageStore`'s vtable changes, which needs a rebuild |
| XI concurrency | strand discipline, cancellation | The deadline race uses `await_deadline`, whose re-arm handles a clock-wide sweep (#536). The reset unit shields itself, and restores the pump's state explicitly, because asio's cancellation state is per awaitable thread (research R-9). `close()`'s wait is event-driven, with no poll. No new detached coroutine |
| XII security | fail closed | An oversize frame closes before its CheckSum is read. Garbled bytes are never acted on. A disregard before Active is bounded by the timeout, so a hostile peer cannot hold a connection open indefinitely |
| XIV §2 | at most 5 pure virtuals per pluggable interface | `MessageStore` keeps 4; `reset_to` is non-pure |
| XIII §2–3 logging | async logger; `trace_context` in every record | The first session log site uses `FIXPP_SLOG` with the session's `trace_context`, and its format strings are registered in `src/log/format_registry.cpp`. Garble records are rate-bounded, so one peer cannot flood the bounded queue (OD-5) |
| XV banned patterns | — | No `thread_local` buffer (the owner rejected a per-thread arena). No RTTI dispatch for `reset_to` |
| XVI §3–4 | `/clarify`, `/analyze` mandatory | `/clarify` is done (2026-10-02). `/analyze` follows `/speckit-tasks` |
| XVI §6 | the orchestrator does not implement | Implementation goes to `phase-implementer`. Mutation proofs happen in a scratch copy |
| XVII §1 Gate A | parser, session FSM, public C++ API, C ABI, message-store contract | **Required before `/speckit-tasks`** |
| XVII §7 | a local build gate before the PR | Planned. Ask before each build, one preset at a time, under the 16 GiB cap |

**Result: PASS**, with the §X.7 BREAKING declaration as a sanctioned pre-release change.

## Orchestrator decisions (open to Gate A review: NOT owner rulings)

The owner's rulings are in spec.md Clarifications. Each item below is a design choice the orchestrator
made at `/speckit-plan`. Reviewers may challenge any of them, and the owner may overrule.

- **OD-1: `close()`'s wait for an in-flight reset unit** (revised at Gate A round 1).
  - **Bound.** `logon_timeout_ms`. The owner ruled "with timeout" and left the value open.
    `logout_disconnect_timeout_ms` was the first candidate and was rejected: nothing validates it, and
    TOML accepts 0, which would expire the wait at once. `logon_timeout_ms` is never 0, and the unit is
    part of Logon processing. A huge value is the operator's choice (G93-A-11).
  - **When.** Only when `close()` is about to issue its teardown reset and a unit is in flight. Without
    a teardown reset the manager is already set, so no wait is needed.
  - **How.** Event-driven: the unit's completion signal raced against `await_deadline` on
    `effective_clock_`, which re-arms on a clock-wide sweep. The pre-Gate-A post-poll would spin the
    strand for the whole bound (G93-O-07).
  - **On expiry.** `close()` records `session_event_close_reset_wait_expired` and proceeds. An overriding
    store with a FIFO writer lock still lands (1, 1). A default-body store may not (L-4).
- **OD-2: `register_session` and `open()` refuse an advertised MaxMessageSize below 4096** (rationale
  restated at Gate A round 1).
  - It is a new BREAKING refusal for C++ callers.
  - The reason that remains is to fail at configuration rather than at every Logon. An L below the size
    the engine already treats as enough for any valid Logon (the acceptor's first-frame budget) is
    almost surely a misconfigured 383. Without the floor, it would surface only as a loud close at each
    connection.
  - The coverage argument (G93-A-12) no longer depends on the floor. The first-frame Framer is now capped
    at L (OD-11), so B(L) covers every frame admitted anywhere, whatever L is.
  - Alternative: no floor, with that late failure.
- **OD-3: FR-013 reverses 070's pre-establishment exemption** for frames over L. Under R-2 such a frame
  cannot be parsed, but the peer has not seen our 383 before the Logon exchange. Alternative: keep the
  exemption for frames over L but within 64 KiB, at the cost of a second, larger buffer before Active.
- **OD-4: with `resync_on_garble` on, the Framer checks `frame_len > max_frame_bytes` before the
  CheckSum**, so that an over-L frame is never disregarded as garbled (revised at Gate A round 1,
  G93-A-17). Strict callers keep today's order byte for byte. The difference touches only a frame
  within about 30 bytes of the ceiling whose CheckSum is bad, which is already fatal for them. This
  removes the "every caller" behaviour change and its C-7 row.
- **OD-5: the first production log site** (revised at Gate A round 1).
  - It is `FIXPP_SLOG` with the session's `trace_context` (`[const §XIII.3]`), through the logger
    resolved at `open()` (`logger_override`, else the engine's). The format strings are registered in
    `src/log/format_registry.cpp`.
  - Garble records are rate-bounded to one per heartbeat interval, carrying the suppressed count
    (G93-O-08). §4.5.2 says "should log each", and the counter stays exact.
  - Pre-Session closes are not logged, because no out-of-session log site exists in `src/` (L-6).
- **OD-6: the establishment deadline lives in the pump**, not in a detached Session timer (research
  R-4), with the acceptor's first-frame read as its phase (a). It closes a pre-Active connection already
  refused into Disconnected, which belongs to #534.
- **OD-7: garbled frames are counted, evented and logged even after `close()` began** (revised at Gate
  A round 1, G93-O-05). They are a transport observation, so FR-030 carves them out. That covers the
  arm's own 35-not-third disregard, which therefore runs before FR-030's guard (contract C-2 steps 1
  and 2).
- **OD-8: the acceptor's first-frame garbles are handed to the Session after `open()` as one summary**
  (replaced at Gate A round 1).
  - The first-frame read returns a `garble_summary`, and the engine calls `note_garbles_` once through
    `session_engine_access`. There is no per-region replay, whose record list would be about 1365
    entries per accept (G93-A-08).
  - A connection that never yields a Session is not counted (L-6).
  - The counter stays on the Session. Gate A departures, below, say why `SessionEntry` was not chosen.
- **OD-9: `reset_to` gets the true targets on volatile stores too.** This may fix #538, which is
  unconfirmed, as a side effect, so #538's reproduction runs on this branch and on its base.
- **OD-10: the parse reserve is a per-call argument**, a public C++ addition, not an `OffsetTable::Config`
  field (research R-3).
- **OD-11: the Framer's resync shape** (added at Gate A round 1; contract C-1).
  - After a garble, the next start is `8=FIX` anywhere.
  - A structurally complete frame with a wrong CheckSum value is consumed through its own end.
  - In resync mode the header scans are bounded at every candidate, a boundary included. A
    BeginString value longer than the longest supported identifier is therefore a garble, which is
    FR-008's carve-out.
  - The carry compacts only when the incoming bytes would not fit, so a carry-only feed never
    compacts, and the cost does not depend on segmentation.
  - The first-frame Framer is `{max = L, resync on}`.
  - Alternatives and their costs are in research R-2.
- **OD-12: one named engine seam, `session_engine_access`** (added at Gate A round 1, G93-A-13). The
  pump and the accept loop reach the Session's limit, latch, accounting hooks and borrowed carry
  through it. Nothing underscore-suffixed joins the installed `Session` surface.
- **OD-13: the carry is allocated at `open()`**, beside B(L), from `framer_carry_arena` (added at Gate A
  round 1, G93-O-04). An allocation failure becomes an `open()` error instead of a `std::terminate`
  inside `pmr_carry_buffer`'s `noexcept` constructor. The operator-visible arena requirements are in
  C-7 row 15.
- **OD-14: the reset unit's cancellation shield is a disable on the awaitable thread with an explicit
  restore**, not a helper coroutine with its own disable (added at Gate A round 1; contract C-6;
  research R-9). The cost is L-12: `Engine::stop()` waits for the unit's store operation.
  - Alternative: `co_spawn` the store await on the session strand, completing through a token bound to
    an empty cancellation slot. That allocates a frame and leaves the pending cancellation to throw at
    the arm's next await, so it was not chosen.

## What changes for whom

| Who | What changes | Declared where |
|---|---|---|
| The FIX counterparty | A garbled frame (a bad CheckSum or BodyLength, leading junk, 35 not third) is ignored instead of ending the session, and the next frame's gap is recovered by ResendRequest. A frame over our L ends the session in every state, the pre-Logon exemption and the acceptor's first frame included. A pre-Active connection closes at the logon timeout, or earlier at the acceptor's first-frame bounds. No TestRequest is sent while its recovery traffic is flowing | B&L delta; contract C-1, C-2, C-4, C-5 |
| A C-ABI or Python consumer | The session-observable changes in C-7 rows 1 to 6, each marked BREAKING on the declarations C-7 names, plus a timeout setter and a garbled-frame counter | C-ABI MINOR; per-declaration BREAKING blocks; `version.h` history |
| A C++ consumer of `Framer` | A new opt-in config and a summary accessor. With the flag off, nothing changes, the check order included | contract C-1, C-7 |
| A C++ `MessageStore` implementer | A new non-pure `reset_to`, taking targets in {1, 2}. No change is needed. Override it for crash atomicity, and for the (1, 1) outcome after an expired `close()` wait | `message_store.hpp` header comment; B&L L-3, L-4 |
| A C++ consumer of `SessionEvent` | Three new alternatives. A `std::visit` without a default arm must handle them | contract C-7 |
| An operator | Per-session memory is the carry plus B(L); the B&L row gives the formula and two worked totals. `framer_carry_arena` must hold L plus one read, and the session arena must hold B(L) per `Session`. A shortfall is an `open()` error. `logon_timeout_ms` is configurable; zero is refused. `Engine::stop()` waits for an in-flight 141=Y store operation (L-12) | B&L; SC-007; C-7 row 15 |
| A C++ `SessionConfig` user | `advertised_max_message_size` below 4096 or above 262144, and `logon_timeout_ms == 0`, are now refused at `register_session` and at `open()` | B&L |

## Project Structure

### Documentation (this feature)

```text
specs/093-inbound-frame-dispositions/
├── spec.md
├── plan.md               # this file
├── research.md           # R-1 (35-not-third), R-2 … R-9
├── data-model.md         # E-1 … E-12
├── contracts/
│   └── inbound-frame-dispositions.md   # C-1 … C-8
├── quickstart.md         # cells Q-1 … Q-31; §4 traceability
└── checklists/
    └── requirements.md
```

### Source code: the files expected to change (re-derive at implementation)

```text
include/fixpp/wire/framer.hpp          # Config::resync_on_garble, garble_summary, last_garbles()
src/wire/framer.cpp                    # resync scan, searching_ flag, W-1..W-4, frame_len check before CheckSum (resync mode)
include/fixpp/wire/offset_table.hpp    # per-call reserve argument (E-3)
src/wire/offset_table.cpp
include/fixpp/wire/parser.hpp          # reserve pass-through
include/fixpp/session/session.hpp      # L, carry, parse span, counter, garbled_frame_count(), reached_active_, reset_unit_in_flight_, friend session_engine_access
src/session/session_engine_access.hpp  # the engine seam (not installed)
src/session/session.cpp                # open() validation + allocation; C-2 steps 1, 2, 4; liveness writer; reset unit + shield; close() wait; logger resolution
include/fixpp/session/session_config.hpp  # logon_timeout_ms
include/fixpp/session/session_event.hpp   # three alternatives
include/fixpp/session/message_store.hpp   # reset_to (non-pure, {1,2}); header count comments become conditions
include/fixpp/session/memory_store.hpp    # reset_to override
include/fixpp/session/file_store.hpp
src/session/file_store.cpp                # reset_to override (temp + rename with target counters)
src/session/engine.cpp                    # register_session gate; pump: borrowed carry, L-sized Framer, summaries, deadline race; first-frame summary hand-off
src/session/read_first_frame_bounded.hpp  # {max = L, resync} Framer, {offset, len, summary}, bounded by the deadline
src/log/format_registry.cpp               # the garble and timeout format strings
src/config/toml_config_loader.cpp, src/config/scalar_mappers.cpp   # logon_timeout_ms
include/fix/c_api/session.h, src/capi/config.cpp, src/capi/session.cpp, include/fix/c_api/version.h
tests/abi/golden/fixpp_capi_symbols.txt, tools/capi_freeze.sha256
bindings/python/fixpp.i                   # %apply for the getter, GIL table row
tests/support/session_test_access.hpp     # buffer-shrink accessor (FR-014)
tests/support/…                           # counted-work seam for the Framer (quickstart §2)
tests/session/…, tests/wire/…, tests/capi/…, tests/fuzz/fuzz_wire_framer.cpp, bindings/python/tests/…
spec/behaviors-and-limitations.md (+ -closed.md), brain/components/{session,inbound-message-path,wire,message-store-quiescence}.md, brain/log.md
```

**Structure decision**: the existing single-library layout. No new target, except possibly a test bucket
label.

## Implementation phases (for `/speckit-tasks`)

The order follows dependencies. Each phase writes its cells RED first, then the change, then GREEN.

- **P0: baselines and census** (quickstart §0): the paired bench, the per-lane ceiling, and the scratch
  census of non-canonical builders, followed by fixing those builders. No production change.
- **P1: Framer** (C-1):
  - the opt-in resync;
  - the `searching_` flag and held prefix;
  - W-1 to W-4;
  - `last_garbles()`;
  - the resync-mode `frame_len` reorder;
  - the counted-work seam;
  - the Framer unit cells (Q-2 to Q-5, Q-7) with every split;
  - the fuzz arm.

  Every other caller is unchanged with the flag off, and Q-7 guards that.
- **P2: L and parse capacity** (C-3, FR-010 to FR-014):
  - `inbound_limit_for`, with the `register_session` and `open()` validation;
  - the carry and the per-session buffer allocated at `open()`, with the spill witness, and the
    per-call reserve;
  - `max_offset_entries` from L;
  - the 10 late sites moved to the buffer;
  - the dense-L and boundary cells on every lane;
  - FR-014's defence cell, and the re-based `LateSite_*`.
  - `test_066` and `test_070` are updated.
- **P3: pump** (C-1, C-4):
  - `session_engine_access`;
  - the borrowed carry, the L-sized Framer, and the summaries into the Session;
  - logger resolution, the counter, the event and the rate-bounded log;
  - the first-frame read's `{max = L, resync}` Framer and its summary hand-off;
  - the two-phase establishment deadline;
  - the TC, resync, timeout and Active-allocation cells.
  - `FramerFailureClosesEstablishedSession_*` flips.
- **P4: arms** (C-2, C-5): step 1, the 35-not-third disregard, in every arm; step 4, the liveness move;
  the B-005-7, W1 and D-8 cells flip; the SC-005 cells.
- **P5: #523** (C-2 step 2). It lands before P6, because FR-041 depends on it on the initiator.
- **P6: #524** (C-6):
  - `reset_to` with its precondition, and both overrides with crash-injection cells;
  - the unit's shield and manager-first rewrite;
  - `reset_unit_in_flight_`, the completion signal and `close()`'s event-driven wait;
  - `HookedStore` forwarding;
  - the #524 cells: with and without a teardown reset, overriding and default-body stores,
    `Engine::stop()` during the unit, and wait expiry;
  - the dead `teardown_reset_done_` stops removed.
- **P7: public surface** (C-7):
  - `SessionConfig` and TOML;
  - the C setter and getter, the golden, the freeze hashes and reentrancy;
  - the per-declaration BREAKING blocks and the amended 1.10 bullets;
  - `version.h` MINOR and its history;
  - the Python typemap and GIL row, with their cells.
- **P8: docs**:
  - B&L: L-004-4 closed; B-005-7 narrowed; new B and L rows from C-8; L-092-6 and L-518-1 updated,
    including the LogonReceived bullet now by design;
  - brain component pages and the log;
  - header comments naming 093 where a decision is superseded (`L-004-4`'s wontfix, 092's D-8,
    #518's in-unit stops).
- **P9: `/simplify`, then `/speckit-verify`** (the full matrix plus MSVC), then `/gate-b`.

## Post-design Constitution re-check

- **VIII §5** holds only if B(L) truly bounds the parse. The spill witness and the per-lane peak cell make
  that measurable, not assumed. FR-052's Active scope is witnessed by Q-19.
- **XI**: `close()`'s wait is bounded and event-driven, so it cannot add a hang. The unit's shield means
  `Engine::stop()` can wait on a store operation, which is disclosed as L-12 and is no worse than
  `close()`'s unbounded teardown reset. The deadline race runs only before the first Active.
- **X §7**: C-7's matrix places BREAKING per declaration, names the amended 1.10 bullets, and records why
  #523 and #524 are not BREAKING.
- **XII**: in resync mode the reorder means no over-L frame takes the disregard path. The Framer's work
  per byte is bounded (C-1 W-1 to W-4), so a hostile peer cannot amplify CPU through resync.

Re-check result: **PASS.**

## Complexity Tracking

| Item | Why it is needed | Simpler alternative rejected because |
|---|---|---|
| Five issues in one bundle | They share the read pump, `on_inbound_frame`'s arms and the reset unit. #514 needs #515's L for its limit, and #524 needs #523's guard on the initiator | Separate PRs would review the same arms five times and land in a forced order anyway |
| The Framer's resync rules (`searching_`, `8=FIX`, W-1 to W-4) | FR-002 must not depend on segmentation, must not lose the frame after junk or truncation, and must stay linear under hostile input | `8=` after an SOH loses the frame after a region not ending in SOH. `8=` anywhere matches tag suffixes. A re-feed per garble, and compaction on every feed or on every non-empty feed, are quadratic under hostile segmentation (research R-2) |
| `close()`'s bounded wait | The owner ruling makes custom stores safe without breaking them | (a) disclosing the residual was rejected; (c) an "unsupported" error code touches the C-ABI error map |
| The reset unit's cancellation shield | `Engine::stop()` cancels the unit before `close()` runs, leaving the pre-unit state, which is #524's own symptom | Narrowing the outcome table to graceful closes would leave the headline guarantee false on the common shutdown path |

## Gate A

- Round 1 applied 2026-10-02: Codex P1=7 P2=9 P3=2; Opus post-judging P1=2 P2=10 P3=18; rewrite addresses root causes #1 (acceptor pre-Session phase), #2 (Framer resync), #3 (reset unit vs `Engine::stop()` cancellation), #4 (per-declaration surface inventory), #5 (state placement and validation), #6 (B(L) accounting). Reviews: research/reviews/codex_093-inbound-frame-dispositions_gate_a_review.md, research/reviews/opus_093-inbound-frame-dispositions_gate_a_adversarial_review.md.

### Round 1 — how each root cause was addressed

- **#1, the acceptor's pre-Session phase** (G93-A-01, A-02, O-06, the SC-003 part of A-14).
  - Contract C-4 was re-derived as a two-phase table. Phase (a) closes the raw transport silently at
    the first-frame bounds. Phase (b) records the event and calls `close(terminal)` at T.
  - FR-006 was phase-scoped, SC-003 was rewritten per phase, and quickstart Q-16 and Q-17 test each
    phase.
  - The first-frame Framer is capped at L, so FR-013 holds for the first frame (O-06). L-6 discloses
    the silent pre-Session close.
- **#2, the Framer resync** (A-04, O-01, O-08, the reporting halves of A-03 and A-08).
  - C-1 was re-derived: `8=FIX` after a garble, a wrong-CheckSum frame consumed whole, a `searching_`
    flag with at most four held bytes, and a per-feed `garble_summary` with no re-feed.
  - The bounded-work invariants W-1 to W-4 are new:
    - compaction only when the incoming bytes do not fit, which holds under one-byte reads;
    - capped header scans at every candidate, with FR-008's carve-out for an over-long BeginString;
    - disjoint CheckSum sums;
    - no allocation. They are witnessed by Q-4, a counted-work
    instrument and five mutants.
  - FR-002, FR-003 (exact counter, summary events, rate-bounded log) and E-1, E-4, E-5 and E-12 were
    updated to match. The fuzz invariant became the counted-work bound.
- **#3, the reset unit and cancellation** (A-05, A-11, O-07).
  - C-6 was re-derived: a cancellation shield on the awaitable thread with an explicit restore (OD-14),
    the manager set under L-518-1's inline-grant condition, and a `{1, 2}` precondition on `reset_to`
    (A-16).
  - `close()` waits only before a teardown reset, event-driven, against `await_deadline`. The outcome
    table now holds under `Engine::stop()` too, with the expiry outcome stated per store class.
  - The "Open (R-9)" bullet was deleted. R-9 became settled design plus cells Q-26 and Q-27, and L-12
    discloses the cost.
- **#4, the surface inventory** (A-07, A-13, A-17, O-04, the FR-007 and FR-052 rows of A-14).
  - C-7 was re-derived as a 17-row matrix: per-declaration BREAKING placement, the amended 1.10 bullets
    named by their opening text, #523 and #524 not BREAKING under B-518-1's ruling, the
    `session_engine_access` seam (OD-12), the arena requirements (OD-13), and the TOML grammar (A-18).
  - The resync-mode-only reorder (OD-4) removes the "every caller" row.
  - Quickstart §4 is the FR/SC → clause → cell index.
- **#5, state placement and validation** (O-02, O-03, A-08, the arena half of O-04).
  - `register_session` gains the zero-timeout and 383 refusals.
  - The first-frame garbles reach the Session as one summary (OD-8), and the per-region replay is gone.
  - The counter stays on the Session, which is a departure recorded below. The carry and B(L) are
    allocated at `open()`, with failure as an `open()` error.
- **#6, B(L) accounting** (A-09, A-10).
  - B(L) is written over N(L) = ⌊L/3⌋ + 1, plus `kAlignPad`.
  - The cost is the carry plus B(L), with derived worked totals at 64 KiB and 256 KiB in research R-3.
    "6·L" was deleted, and the Clarifications question carries an annotation that points at R-3.
- **The remaining P3s:**
  - A-15 (the stall wording, edge case and L-1);
  - A-19 (C-1 says the Framer reads only the length fields, and no guard or handler reads any);
  - O-05 (C-2 step order, FR-030 carve-out, OD-7);
  - O-09 (FR-050's L-518-1 LogonReceived bullet becomes by design);
  - O-10 (OD-5: `FIXPP_SLOG`, `trace_context`, `format_registry.cpp`);
  - O-11 (C-3 I-6 rationale replaced, L-14, a follow-up to file);
  - A-03 (E-1 and E-5 name `wire_header_out_of_order`);
  - A-12 (OD-2 restated);
  - A-14 (FR-008 regression cell Q-10, the FR-042 witness, the FR-053 dependency).

### Round 1 — disagreements

Each item below is one Opus marked Disagree, in whole or in part. Codex's fix was not applied.

- **G93-A-01 (part).** Codex asked for a watchdog over TLS, hydration, the store reset and the Logon
  write, with stalled-cell tests for each. Not applied. Those suspensions are local and bounded by the
  store and the transport. A peer cannot hold them open, so the cells would test the store and the TLS
  stack, not establishment. FR-006 now states that the deadline races only waits for peer bytes.
- **G93-A-02 (part).** Codex asked to uncharge discarded bytes from the first-frame budget and to
  compact. Not applied. That contradicts the spec's Assumption, and the stricter first-frame bounds
  already satisfy the ruling's "bounded by". SC-003 was made per phase instead.
- **G93-A-03.** Codex asked for a dedicated `garble_kind` enum. Not applied:
  `core::error::wire_header_out_of_order` already exists, so "no new error code" holds.
- **G93-A-05 (manager half).** Codex said manager-first was unimplementable through awaitable manager
  operations and asked for a synchronous manager API. Not applied. The uncontended `async_lock` grants
  inline. C-6 step 3 copies L-518-1's condition for when that stops holding.
- **G93-A-06 (part).** Codex said the Constitution Check's PASS was false and required a per-connection
  watchdog. Not applied. §VIII.5 covers parse to `fromApp`, and the pre-Active race sits before any
  parse and lasts only until the first Active. FR-052 was scoped instead, with Q-19 and its mutant.
- **G93-A-07 (part).** Codex said #523 and #524 are BREAKING. Not applied. B-518-1's owner ruling
  says the old outcomes were the defect, and these issues are its follow-ups (C-7 rows 16 and 17).
- **G93-A-08 (fix shape).** Codex asked for a bounded ordered record list for first-frame garbles. Not
  applied: that is about 1365 records per accept. One summary is handed over instead (OD-8).
- **G93-A-09 (consequence).** Codex said an undersized reserve terminates. Not so:
  `OffsetTable::build` catches `bad_alloc` and reports `out_of_memory`. The formula was corrected
  anyway.
- **G93-A-10 (part).** Codex asked for an engine-level admission calculation. Not applied. The
  reservation is at most one carry plus B(L) per registered session with a connection, which research
  R-3 records.
- **G93-A-11.** Codex asked for a separate, capped shutdown timeout. Not applied. The value is the
  operator's choice, and the wait matters only for default-body stores. The expiry outcome is now
  specified per store class (OD-1).
- **G93-A-12.** Codex said the floor is arbitrary and asked to accept any positive value. The floor
  is kept, with its remaining reason restated (OD-2; see the departures).
- **G93-A-14 (two rows).** Codex listed FR-042 and FR-053 as gaps. They are not. FR-042's witness is
  the existing `-Werror` test-local subclasses, and FR-053 is enforced by B25's census, a dependency.
- **G93-A-15 (fix shape).** Codex asked for a terminal transport close on an unanswered TestRequest.
  Not applied. That is #534, which predates 093. Only the wording was corrected.
- **G93-A-16 (fix shape).** Codex asked for a general exact-set primitive. Not applied. The precondition
  `{1, 2}` matches the only caller.

### Round 1 — departures from the Opus fix shapes (code-verified)

- **G93-O-02 / root cause #5: the counter stays on the Session, not on `SessionEntry`.**
  - The premise "drops to 0 at every reconnect" is not reachable today. `start()` runs once
    (`engine.hpp`'s lifecycle note), both role loops `co_return` after their one connection's pump, and
    `entry.session` is retained for `lookup()`.
  - An entry-level atomic read from any thread would also need a new path that survives `stop()`'s
    `registry_.clear()`. The reader snapshot already gives the Session that path.
  - The replay O-02 wanted gone is gone anyway (OD-8). The condition, and how to re-derive it, are in
    data-model E-4.
- **G93-O-01: the start rule is "`8=FIX` anywhere after a garble" without Opus option (a)'s `␁8=`
  alternative.** Every supported profile begins with `FIX`, so `␁8=` recovers no supported frame that
  `8=FIX` misses. Dropping it also drops the "previous byte was SOH" state.
- **G93-A-04 / root cause #2: Opus's "cursor, compact once per call, one forward pass" is not linear by
  itself.** Three code facts:
  - the pump drains with one carry-only `feed` per frame, so compaction per call still moves the
    remainder once per frame;
  - `parse_frame` sums the CheckSum for every structurally valid candidate, and nested candidates can
    share one `10=`;
  - both header scans use an unbounded `find_soh`.

  Compacting only on non-empty feeds, the first repair tried, is still quadratic when the peer
  sends one-byte reads that each resolve a staggered nested candidate. C-1 W-1 to W-3 close all of
  these: amortized compaction, disjoint sums, and capped scans.
- **G93-A-05 / root cause #3: not a helper coroutine with its own `disable_cancellation`.** In the asio
  this build uses, cancellation state belongs to the awaitable thread's bottom frame. A helper's reset
  would persist into the arm and the pump, and `throw_if_cancelled` throws at the next `co_await` when
  cancellation is pending. The shield is therefore a disable before the manager set and an explicit
  restore after the store await (OD-14; research R-9).
- **G93-O-07: the wait's deadline is `await_deadline` on `effective_clock_`.** Opus likened it to the
  logout grace, but that uses an `asio::steady_timer`, so the likeness is only the `||` shape. The
  re-arm is needed because `close()` itself calls `cancel_sleeps()`, and another session can sweep the
  clock.
- **G93-A-12 / O-06: OD-2's floor no longer rests on the coverage argument.** O-06 is fixed by capping
  the first-frame Framer at L, which by itself makes B(L) cover every admitted frame. The floor is kept
  for its remaining reason: it fails a misconfigured 383 at configuration time.

### Round 1 — items for the owner

None. Every ruling in spec.md `## Clarifications` is implementable as written. Two rulings are extended,
not changed:
- the zero-timeout and 383 refusals are also checked at `register_session`;
- "the log" of the counter-getter ruling is rate-bounded, with the counter kept exact. The ruling
  names no per-frame logging; that wording was the orchestrator's FR-003.

The new trade-off L-12 (`Engine::stop()` waits for a shielded store operation) is an orchestrator
decision (OD-14). It is listed for the owner's awareness, not for a ruling.
