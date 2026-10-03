# Implementation Plan: Inbound frames 092 leaves out

**Branch**: `093-inbound-frame-dispositions` | **Date**: 2026-10-02 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/093-inbound-frame-dispositions/spec.md` (clarified
2026-10-02, with the plan-research owner rulings recorded there).

**Issues**: fixpp#514, #515, #516, #523, #524, and #540 (unconfirmed, reproduce first; spec FR-015,
SC-008) (batch B22). **Depends on**: fixpp#530 (B25) merging first.

## Summary

This bundle makes five changes to fixpp's inbound path, on one shared review surface: the read pump,
`Session::on_inbound_frame`'s arms, and the 141=Y reset unit.

1. **#514: garbled frames are disregarded, not fatal.** The Framer gains an opt-in resync that the read
   pump and the acceptor's first-frame read turn on.
   - A frame that fails framing is skipped, and framing resumes at the next `8=FIX`, independent of how
     reads are segmented. The Framer's work per byte is bounded.
   - A frame whose third field is not 35 is disregarded in every state except Disconnected (which
     ignores every frame), in both validation modes.
   - Every disregard is counted exactly, and evented and logged in rate-bounded summaries. The count
     is readable through a new C-ABI getter.
   - A new establishment timeout (`logon_timeout_ms`, 10 s, zero refused, with C-ABI, Python and TOML
     surfaces) bounds the pre-Active waits this creates, in two phases (contract C-4). Before the
     acceptor has a Session, the first-frame read's stricter bounds apply. After that, the pump tests
     the deadline at every loop head, races it only to wake a blocked read, and closes the transport on
     expiry.
2. **#515: every admitted frame parses.** One inbound limit L comes from the config: the advertised
   383, between 4 KiB and 256 KiB, else 64 KiB. It is checked at `register_session` and at `open()`.
   It sizes:
   - both Framer limits;
   - the carry (L plus one read);
   - a per-session parse buffer;
   - the offset-table entry cap.

   The carry and the parse buffer are allocated once at `open()`. All of these are sized for the
   densest legal layout, 3 bytes per field. A frame over L is refused at framing, before its CheckSum,
   with a close. A lazy read inside a callback that exhausts the headroom and reports a status fails that
   read, never the session, where the spill witness is null (the C cursor shells are the exception,
   L-17, fixpp#541), and `unknown_fields()` gains the catch that #540 asks for, after its reproduction.
3. **#516: liveness refreshes on every frame that is neither garbled nor faulty.** It happens at one
   point, right after the fault checks.
4. **#523: a closing session's NotConnected and LogonSent arms act on nothing.**
5. **#524: the 141=Y reset is one store operation.** `MessageStore::reset_to` is a new non-pure virtual,
   which `MemoryStore` and `FileStore` override atomically.
   - The unit shields itself from cancellation, because `Engine::stop()` reaches it before `close()`
     does.
   - It sets the manager first, with no yield.
   - A session-side engine-stop flag, which `Engine::stop()` sets before it emits, stops the arm after
     the unit, so no callback, event, write or Active transition follows `Engine::stop()`'s step 1 on
     the session's strand.
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

**Unknowns**: none open after Gate A round 2. R-9's items are settled by design and witnessed by
cells. Four design values are measured or derived at implementation, not chosen here: `kAlignPad`,
`kCallbackReadHeadroom` and `kContainerSlack` (data-model E-2), and `kBodyLengthDigitCap` (research
R-2's recipe). #540's reproduction is run first and decides SC-008's branch.

## Constitution Check

*GATE: passed before Phase 0; re-checked below after Phase 1.*

| Article | Requirement | Status |
|---|---|---|
| VII §3–4 TDD | failing test first | Planned. Every quickstart §1 cell is RED-first on the base, or RED on its named mutant, which the cell's row names together with the instrument that observes the effect. Regression guards say so. The RED evidence for SC-004 is the ceiling measured in quickstart §0, and #540's reproduction runs before its catch |
| VII §5 conformance | the FIX-TC corpus | TC 2d/2m/2t/3b/3c/3e move from diverging to conforming, with cells. TC 2i is unchanged (FR-008), and its Logout gap belongs to #534 |
| VII §7 fuzz | parser-touching code needs a fuzz harness | `fuzz_wire_framer` gains a `resync_on_garble = true` arm asserting termination and contract C-1's counted-work bound per input. `fuzz_transport_read_path` drives `Framer::feed` directly, not the pump (its header says so), so it covers neither the pump's resync nor its deadline; it runs unchanged as a regression target. The pump's paths are witnessed by the quickstart §1 cells |
| VII §8 test grouping | new isolation-safe tests go into a grouped bucket, selected by a label | Planned (a `093` label). The timer and coroutine cells stay standalone |
| VIII §2 perf | paired merge-base A-B-A-B within +5 % | A manual paired run of `on_inbound_frame_bench`, with validation off and on, base path padded (quickstart §0, §3). `on_inbound_frame_bench` bypasses the Framer, so `framer_bench` is paired too: base `BM_Framer_Feed_NoCarry` against the same row on the branch (the strict path and the always-compiled counters), and base `BM_Framer_Feed_NoCarry` against the branch's resync-config row (64 KiB limit, resync on, the BeginString cap set as the pump sets it, the same ~80-byte frame), which is the production path's delta. The resync row cannot exist on the base, so it is added after the Framer change (tasks.md T018a), and the second pairing compares two rows by name. The rows time the clean path only; a garbled stream's cost is bounded by contract C-1 W-1 to W-4 and witnessed by Q-4's counted-work instrument, not timed |
| VIII §5 zero-alloc | no heap between parse and callback | Held for the parse: the buffer is allocated once at `open()`, and the resync scans in place. A spill witness proves there is no hidden heap use on any lane. **Not asserted before the first Active.** The establishment deadline race (asio parallel group) may allocate. That is outside the parse-to-callback window, and FR-052 is scoped to start at the first Active. The pre-Active count is measured with a global `operator new` counter (research R-9) and disclosed (L-13). The disarm at the first Active is witnessed by behaviour (Q-36), and Q-19 is a regression witness over the real pump |
| IX sanitizers / coverage | per-line coverage assessment | The `/speckit-verify` matrix. The resync, deadline and reset_to branches are covered by the cells |
| X §4 append-only enums | `core::error` | No new error code: criterion 3 reuses `wire_header_out_of_order`, and `reset_to` reuses `session_invalid_argument`. `SessionEvent` alternatives are appended |
| X §7 ABI | C-ABI changes versioned | **MINOR bump; BREAKING on each affected declaration**: every BREAKING row is marked on the five observers `version.h`'s 1.10 entry names; `version.h`'s history entry is headed BREAKING with a one-line pointer per row, and details an effect only where no declaration carries it. The 1.10 sentences 093 falsifies are amended in place (FR-051, contract C-7). `gh release list --exclude-drafts` must be empty at implementation. The C++ additions are source-compatible; `MessageStore`'s vtable changes, which needs a rebuild |
| XI concurrency | strand discipline, cancellation | The deadline race uses `await_deadline`, whose re-arm handles a clock-wide sweep (#536). The reset unit shields itself, and restores the pump's state explicitly, because asio's cancellation state is per awaitable thread (research R-9). `close()`'s wait is event-driven, with no poll. No new detached coroutine |
| XII security | fail closed | An oversize frame closes before its CheckSum is read. Garbled bytes are never acted on. A disregard before Active is bounded by the timeout, which is a loop-head check, so a peer that keeps the socket readable cannot outrun it. Both header scans are capped over encoded bytes, so zero padding cannot amplify the rescan. The BeginString cap takes the configured length, which is bounded per role on every path where the pump runs over peer bytes, not by L: on the initiator by the Logon buffer (`Session::kMaxMaskableLogonBytes`), on the acceptor by the first-frame budget (`kFirstFrameMaxBytes`) with `SessionId` equality (contract C-1, The bound; OD-16) |
| XIV §2 | at most 5 pure virtuals per pluggable interface | `MessageStore` keeps 4; `reset_to` is non-pure |
| XIII §2–3 logging | async logger; `trace_context` in every record | The first session log site uses `FIXPP_SLOG` with the session's `trace_context`, and its format strings are registered in `src/log/format_registry.cpp`. Garble records are rate-bounded to one per `max(HeartBtInt, 1 s)`, so a HeartBtInt of 0 still bounds them, and the logger's default `drop_newest` policy keeps a full queue from blocking the strand (OD-5) |
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
    strand for the whole bound (G93-O-07). This runs on the Session's clock, while the establishment
    deadline runs on the engine's (C-4). They differ when `clock_override` is set, and the two bounds
    are independent (revised at Gate A round 2, G93-A-10).
  - **On expiry.** `close()` records `session_event_close_reset_wait_expired` and proceeds. An overriding
    store with a FIFO writer lock still lands (1, 1). A default-body store may not (L-4).
- **OD-2: `register_session` and `open()` refuse an advertised MaxMessageSize below 4096** (rationale
  restated at Gate A round 1).
  - It is a new refusal for C++ callers: a C++ behaviour change, not a C-ABI BREAKING one, because C
    cannot set 383 (FR-051). It is declared in the B&L delta and among the PR description's C++
    deltas (contract C-7 row 10).
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
  - Garble records are rate-bounded to one per `max(HeartBtInt, 1 s)`, carrying the suppressed count
    (G93-O-08; the 1 s floor added at Gate A round 2, G93-A-07, because a HeartBtInt of 0 is legal).
    §4.5.2 says "should log each", and the counter stays exact.
  - Pre-Session closes are not logged, because no out-of-session log site exists in `src/` (L-6).
- **OD-6: the establishment deadline lives in the pump**, not in a detached Session timer (research
  R-4), with the acceptor's first-frame read as its phase (a). It closes a pre-Active connection already
  refused into Disconnected, which belongs to #534.
  - **Expiry is a loop-head check** (revised at Gate A round 2, G93-A-01). The pump tests the deadline
    before each read and before each frame's delivery, and races it only to wake a blocked read, so
    asio's completion order cannot decide expiry.
  - On TLS, phase (a) lasts at most `max(T, the handshake bound)`; the handshake bound is not shortened.
    When no time remains after the handshake, the accept loop closes without a read. The alternative,
    clamping the handshake bound to the remaining time, changes a listener-wide setting per connection
    and was not chosen.
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
  field (research R-3). Revised at Gate A round 2 (G93-A-05): `OffsetTable::build` is private, so the
  reserve goes through a new `Parser::parse` overload, a private tagged `MessageView` constructor and a
  new public `OffsetTable` constructor overload (data-model E-3, C-7 row 12).
- **OD-11: the Framer's resync shape** (added at Gate A round 1; contract C-1).
  - After a garble, the next start is `8=FIX` anywhere.
  - A structurally complete frame with a wrong CheckSum value is consumed through its own end.
  - In resync mode the header scans are bounded at every candidate, a boundary included. A
    BeginString value longer than the BeginString cap is therefore a garble, which is FR-008's
    carve-out, and so is a BodyLength digit run longer than the digit cap (OD-16, OD-17; revised at
    Gate A round 2).
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
  - **The path, named at Gate A round 2** (G93-A-05). `open()` allocates one block inside a `try`,
    builds a `monotonic_buffer_resource` over it with the spill witness as upstream, and builds the
    `pmr_carry_buffer` over that resource, so the constructor's reserve cannot fail and no public
    declaration changes. The block includes `kContainerSlack` for MSVC debug's container proxy
    (data-model E-2).
- **OD-14: the reset unit's cancellation shield is a disable on the awaitable thread with an explicit
  restore**, not a helper coroutine with its own disable (added at Gate A round 1; contract C-6;
  research R-9). The cost is L-12: `Engine::stop()` waits for the unit's store operation.
  - Alternative (rationale replaced at Gate A round 2, G93-A-04): `co_spawn` the store await on the
    session strand, completing through a token bound to an empty cancellation slot. Its pending
    cancellation would throw at the arm's next `co_await`, which is what `Engine::stop()` needs. It was
    still not chosen: the reset event is emitted synchronously between the unit and that next
    suspension, so it needs OD-15's flag anyway, and it allocates a frame per unit besides.
- **OD-15: an engine-stop flag on the Session** (added at Gate A round 2, G93-A-04; data-model E-13).
  - `Engine::stop()`'s step 1 sets it through `session_engine_access`, on the session strand, inside the
    `co_spawn` it already uses for its emit and before the emit. `entry.session` is read on the control
    strand first, and a null session has nothing to set.
  - `logon_arm_superseded` tests it, so once the flag is set, the check right after the restore stops
    the arm before any event, callback, write or Active transition, on both roles.
  - The flag is read only inside the predicate, so the claim that no return path skips the restore is
    unchanged. Re-derive the predicate's sites with `grep -n logon_arm_superseded src/session/session.cpp`
    before relying on that.
  - Alternative: call `close()` from `stop()`'s step 1. That reorders `stop()`'s teardown, which its
    join and registry steps depend on, so it was not chosen.
  - The guarantee is strand-ordered: the arm stops after stop's step 1 has run on the session's strand
    (contract C-6). Alternative (Codex G93-A-02, round 3): an atomic latch set on the control strand
    right after `stopped_` and read with acquire in the predicate. It was not chosen, because it has the
    same check-then-act gap (the arm reads it false, the control strand sets it, the arm runs its
    effects); closing that gap needs mutual exclusion between `stop()` and the arm, which is what
    running step 1 on the session strand already gives.
- **OD-16: the BeginString cap is `max(longest supported identifier, configured begin_string length)`**
  (added at Gate A round 2, G93-O2-02; contract C-1 W-2). A fixed cap would garble every frame of a
  session configured with a longer BeginString, which works today, and leave it to time out with no
  refusal at configuration. Alternative: refuse such a configuration at `register_session`, `open()`
  and the C setter. That is a new C++, C and TOML refusal and a new BREAKING row, so it was not chosen.
  The search prefix `8=FIX` stays, and a configured BeginString not beginning with `FIX` is disclosed
  (L-16).
  - The cap's term is bounded on a condition (added at Gate A round 3, G93-A-03; contract C-1, The
    bound): on every path where the pump runs over peer bytes, the configured BeginString is bounded
    per role. On the initiator it fits the Logon buffer (`Session::kMaxMaskableLogonBytes`); on the
    acceptor it fits the first-frame budget (`kFirstFrameMaxBytes`), with `SessionId` equality. So it is bounded by those, not by L. No clamp is
    applied: clamping the term would garble the first frame of an acceptor whose BeginString exceeds
    the clamp but fits the first-frame budget, closing it before any Session exists.
- **OD-17: the BodyLength digit run is capped in resync mode** (added at Gate A round 2, G93-A-06;
  contract C-1 W-2). A value cap alone leaves a zero-padded run rescanned on every feed. A run over the
  cap is a garble, not a close. The cap is derived by research R-2's recipe so that it admits legitimate
  padding. A frame padded beyond the cap, accepted today, becomes a garble: a BREAKING change listed in
  C-7 row 1. Alternative: a persisted scan cursor, which breaks C-1's single-flag state rule.
- **OD-18: lazy reads report exhaustion as their declarations allow** (added at Gate A round 2,
  G93-A-02; contract C-3 I-5). R-2's owner ruling covers admission and the dense parse, not lazy reads
  inside callbacks, so this is an orchestrator choice.
  - 093 changes only `unknown_fields()`: an internal catch that keeps `noexcept` and returns an empty
    view, after #540's reproduction runs on the base (FR-015).
  - The other reports stay as they are and are disclosed (L-5, L-17). Changing `fixpp_msg_get_group`'s
    misreport, or catching the C cursor shells' allocation, changes C-ABI results. Neither has been
    through Gate A, and the shells' defect is unconfirmed, so it is filed separately (since filed as
    fixpp#541, batch B28; OD-19).
  - Alternatives: new result-bearing `try_*` lazy APIs (Codex G93-A-02 option 1), a public surface with
    its own versioning, for a failure 093 does not cause; or a headroom large enough to make exhaustion
    impossible (option 2), which no number can be, because the C cursor shells allocate per call.
- **OD-19: fixpp#541 (the C cursor shells' uncaught allocation) is not in 093's scope** (added at Gate
  A close-out, round 3, G93-A-01). The defect is pre-existing on main: the shells allocate from the
  base's parse arena with the same missing catch. It is disclosed (contract L-17; FR-011), unconfirmed,
  and filed in batch B28 with a reproduce-first item. 093 does not make it more reachable for the C-ABI
  population on data-model E-2's `kCallbackReadHeadroom` condition. Alternative (Codex G93-A-01,
  round 3; an owner option): catch both shells and return `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` with the
  outputs nulled, reproduce first, with a Q-33 shell arm per getter. It changes a C-ABI result that
  Gate A has not reviewed, so it was not taken. Spec.md's Issues line keeps #540 and does not list
  #541.
- **OD-20: in resync mode, C-1's ordering rule covers `wire_frame_too_large` as well as a garble**
  (added at implementation, T018, 2026-10-03). A call that has produced a frame stops at any non-frame
  outcome at the next candidate and leaves it for the next call. The frame, which precedes the over-L
  bytes in stream order, is delivered, and the next call returns `wire_frame_too_large`, which closes.
  Strict mode keeps today's behaviour: an error discards the call's frames. Moot for the pump, whose
  output span has length 1. A T015 cell witnesses it: a good frame then an over-L BodyLength in one feed.
  Alternative: discard the call's frames on over-L, as strict mode does. Rejected: it drops a well-formed
  frame that arrived before the fault, which C-1's Extent rule forbids for garbles.
- **OD-21: E-12's suppressed count is in garbled regions, the unit of `garbled_frame_count()`** (added
  at implementation, T028, 2026-10-03). A record's suppressed count is the regions counted since the
  previous record that no record named: every region of each rate-suppressed summary, plus the
  triggering summary's own other k − 1 regions. Its record names only the first, by `first_kind`, while
  its `discarded` bytes cover all k. The count resets at each emitted record. Invariant, pinned by T028:
  Σ over emitted records of (1 + suppressed) equals `garbled_frame_count()` at the last record. A
  continuation summary (`regions == 0`) emits no record and adds nothing. Alternative: count only the
  regions of suppressed summaries. Rejected: a logged k > 1 summary would then break the reconciliation
  between the log and the counter.

## What changes for whom

| Who | What changes | Declared where |
|---|---|---|
| The FIX counterparty | A garbled frame (a bad CheckSum or BodyLength, leading junk, 35 not third) is ignored instead of ending the session, and the next frame's gap is recovered by ResendRequest. A BodyLength zero-padded beyond the digit cap is now a garble. A frame over our L ends the session in every state, the pre-Logon exemption and the acceptor's first frame included. A pre-Active connection closes at the logon timeout, or earlier at the acceptor's first-frame bounds. No TestRequest is sent while its recovery traffic is flowing | B&L delta; contract C-1, C-2, C-4, C-5 |
| A C-ABI or Python consumer | The session-observable changes in C-7 rows 1 to 6, each marked BREAKING on the five observers, plus a timeout setter and a garbled-frame counter | C-ABI MINOR; per-declaration BREAKING blocks; `version.h` history |
| A C++ consumer of `Framer` | Two new config members (`resync_on_garble`, `max_begin_string_bytes`) and a summary accessor. With resync off, no `feed` result changes, the check order included. `sizeof(Framer)` grows: source-compatible, not layout-neutral | contract C-1, C-7 row 1 |
| A C++ consumer of `Parser` or `MessageView` | A new `Parser::parse` overload and `OffsetTable` constructor overload carrying a reserve. `unknown_fields()` returns an empty view on arena exhaustion instead of terminating | contract C-3 I-5, C-7 rows 12 and 18 |
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
├── research.md           # R-1 (35-not-third), R-2 … R-10
├── data-model.md         # E-1 … E-13
├── contracts/
│   └── inbound-frame-dispositions.md   # C-1 … C-8
├── quickstart.md         # cells Q-1 … Q-37; §4 traceability
└── checklists/
    ├── abi.md
    ├── nfr.md
    ├── protocol.md
    └── requirements.md
```

### Source code: the files expected to change (re-derive at implementation)

```text
include/fixpp/wire/framer.hpp          # Config::resync_on_garble, Config::max_begin_string_bytes, kBodyLengthDigitCap, garble_summary, last_garbles()
src/wire/framer.cpp                    # resync scan, searching_ flag, W-1..W-4 with both header caps, frame_len check before CheckSum (resync mode)
include/fixpp/wire/offset_table.hpp    # constructor overload carrying the reserve (E-3)
src/wire/offset_table.cpp
include/fixpp/wire/parser.hpp          # Parser::parse reserve overload, private tagged MessageView ctor; unknown_fields() catch (FR-015)
include/fixpp/session/session.hpp      # L, carry block + resource + carry, parse span, counter, garbled_frame_count(), reached_active_, reset_unit_in_flight_, engine_stop_requested_, friend session_engine_access
src/session/session_engine_access.hpp  # the engine seam (not installed)
src/session/session.cpp                # open() validation + allocation; C-2 steps 1, 2, 4; liveness writer; reset unit + shield; superseded predicate + stop flag; close() wait; logger resolution
include/fixpp/session/session_config.hpp  # logon_timeout_ms
include/fixpp/session/session_event.hpp   # three alternatives
include/fixpp/session/message_store.hpp   # reset_to (non-pure, {1,2}); header count comments become conditions
include/fixpp/session/memory_store.hpp    # reset_to override
include/fixpp/session/file_store.hpp
src/session/file_store.cpp                # reset_to override (temp + rename with target counters)
src/session/engine.cpp                    # register_session gate; pump: borrowed carry, L-sized Framer, summaries, loop-head deadline test + race; first-frame summary hand-off and non-positive remainder; stop() step 1 sets the stop flag
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
  - W-1 to W-4, with the BeginString cap and the BodyLength digit cap (the cap derived first by
    research R-2's recipe);
  - `last_garbles()`;
  - the resync-mode `frame_len` reorder;
  - the counted-work seam;
  - the Framer unit cells (Q-2 to Q-5, Q-7) with every split;
  - the fuzz arm.

  Every other caller is unchanged with the flag off, and Q-7 guards that.
- **P2: L and parse capacity** (C-3, FR-010 to FR-014):
  - `inbound_limit_for`, computed and stored at `open()`, with no refusal yet;
  - the per-session buffer allocated at `open()`, with the spill witness, and the per-call reserve
    through the named overloads;
  - #540: its reproduction on base, recorded first, then `unknown_fields()`'s catch and the lazy-read
    cells (Q-32, Q-33);
  - `max_offset_entries` from L;
  - the 10 late sites moved to the buffer;
  - the dense-L and boundary cells on every lane;
  - FR-014's defence cell, and the re-based `LateSite_*`.
  - `test_066` is updated.
- **P3: pump** (C-1, C-4):
  - `session_engine_access`;
  - the resync-mode Framer and the summaries into the Session (the pump keeps its local carry and
    today's limit until the FR-013 group);
  - logger resolution, the counter, the event and the rate-bounded log;
  - the first-frame read's `{max = L, resync}` Framer and its summary hand-off;
  - the two-phase establishment deadline: the loop-head test in both drains, the race that only wakes
    a blocked read, and the accept loop's non-positive remainder;
  - the TC, resync, timeout, disarm and Active-allocation cells.
  - `FramerFailureClosesEstablishedSession_*` flips.
- **The FR-013 group** (C-1 close rows, C-2, E-2; with US3, after P2's buffer, in one commit group):
  - the `register_session` and `open()` refusal of an advertised 383 outside [4096, 262144];
  - the carry (block, resource, carry) allocated at `open()` and sized for L, borrowed by the pump;
  - both Framers' limit set to L, and the Active-only 383 check deleted;
  - their cells (Q-12, Q-13's 383 half, Q-14's carry half, Q-6), and `test_070` re-based.

  These change what a session accepts, so they are held out of P2 and P3: the foundational phase then
  stays behaviour-neutral on the wire, as its checkpoint claims (the analysis finding C2/F1). tasks.md
  T022a, T024 and T055a carry them.
- **P4: arms** (C-2, C-5): step 1, the 35-not-third disregard, in every arm; step 4, the liveness move;
  the B-005-7, W1 and D-8 cells flip; the SC-005 cells.
- **P5: #523** (C-2 step 2). It lands before P6, because FR-041 depends on it on the initiator.
- **P6: #524** (C-6):
  - `reset_to` with its precondition, and both overrides with crash-injection cells;
  - the unit's shield and manager-first rewrite;
  - the engine-stop flag, set in `Engine::stop()`'s step 1 and tested by `logon_arm_superseded`, with its
    sites re-derived after the change;
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
  `close()`'s unbounded teardown reset. The engine-stop flag keeps the shield from letting an arm run
  its callbacks after `Engine::stop()`'s step 1 has run on the session's strand. The deadline race runs only before the first Active, and expiry does not
  depend on its completion order.
- **X §7**: C-7's matrix places BREAKING per declaration, names the amended 1.10 bullets, and records why
  #523 and #524 are not BREAKING.
- **XII**: in resync mode the reorder means no over-L frame takes the disregard path. The Framer's work
  per byte is bounded (C-1 W-1 to W-4, with both header caps over encoded bytes), so a hostile peer
  cannot amplify CPU through resync or through zero padding. The BeginString cap's term holds on C-1's
  condition: the configured BeginString is bounded by the Logon buffer and the first-frame budget,
  not by L.

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

- Round 2 applied 2026-10-02: Codex P1=3 P2=6 P3=2; Opus post-judging P1=0 P2=6 P3=7; rewrite addresses root causes RC-1 (deadline expiry as a checked invariant), RC-2 (failure and allocation claims from the declarations), RC-3 (the shield and `Engine::stop()`), RC-4 (the C column from the 1.10 observer set), RC-5 (bounded work over encoded bytes), RC-6 (each witness's instrument). Reviews: research/reviews/codex_093-inbound-frame-dispositions_gate_a_2_review.md, research/reviews/opus_093-inbound-frame-dispositions_gate_a_2_adversarial_review.md.

### Round 2 — how each root cause was addressed

- **RC-1, the deadline as a checked invariant** (G93-A-01 bullets 1 and 3).
  - Contract C-4 now states expiry as a test of `steady_now() >= deadline` before each read and before
    each frame's delivery, in both drains. The race only wakes a blocked read (FR-006, OD-6, E-6,
    research R-4).
  - Phase (a) on TLS is bounded by `max(T, the handshake bound)`. A non-positive remainder after the
    handshake closes without a read, guarded in the accept loop, so `read_first_frame_bounded`'s
    relative-to-absolute line, where 088's B6 mutant lives, is untouched.
  - Cells: Q-16 gains the readable-across-T cell through a transport double whose read completes at
    initiation, so the loop-head mutant is RED whatever order asio uses. Q-17 gains the TLS pair.
- **RC-2, claims from the declarations** (G93-A-02, G93-A-05).
  - FR-011 and C-3 I-5 now give each lazy read's true report, read from `offset_table.hpp`,
    `parser.hpp` and `src/capi/message_read.cpp` (research R-10, OD-18).
  - `unknown_fields()` gains an internal catch (FR-015, C-7 row 18), after fixpp#540's reproduction runs
    on the base (SC-008, Q-32). Q-33 pins every API's report through the session.
  - E-3 and C-7 row 12 name the reserve's real path: a `Parser::parse` overload, a private tagged
    `MessageView` constructor and a public `OffsetTable` constructor overload. E-2, OD-13 and C-7 row 15
    name the carry's path: a Session-allocated block, a monotonic resource over it, then the carry.
- **RC-3, the shield and `Engine::stop()`** (G93-A-04).
  - The engine-stop flag (OD-15, E-13) is set in `stop()`'s step 1 and tested by
    `logon_arm_superseded`. C-6 records why the restore's fresh state needed it, and that the flag adds
    no return path between the shield and the restore.
  - Skipping a null `entry.session` is safe on a stated condition, verified at `00c1f720`: both role
    loops publish before any frame is delivered, and a publish refused because stop began delivers
    nothing (E-13 has the re-derivation).
  - OD-14's inverted rationale is replaced, not stacked on.
  - Q-26 and SC-006 assert, per role, no `toAdmin`, reset event, `onLogon` or Active after stop began,
    with a mutant that drops the flag. (Narrowed at round 3, G93-A-02: after `Engine::stop()`'s step 1
    has run on the session's strand.)
- **RC-4, the observer set** (G93-A-03). C-7 now defines the five observers from `version.h`'s 1.10
  entry, and rows 1 to 6 mark BREAKING on all five. FR-051's witness, plan X §7 and the "what changes for
  whom" row say the same. Codex's request for C-ABI witness cells is applied as Q-37, one trigger per
  row asserting all five observers through C, so no BREAKING row is witnessed only through C++. Row 3
  now also names the C-observable half of its change: a frame of at most L is admitted however the
  stream is segmented.
- **RC-5, encoded bytes** (G93-A-06). C-1 W-2 caps the BodyLength digit run (`kBodyLengthDigitCap`,
  derived by research R-2's recipe, with a floor that admits padding and a ceiling that keeps its term
  small, OD-17). C-1 states the amortised bound with both caps, and FR-002's
  wording now matches it. Q-4 gains the zero-run shape and the cap and cap-plus-one pair. The
  newly-garbled padded frame is in C-7 row 1.
- **RC-6, each witness's instrument** (G93-O2-01, G93-A-09).
  - The disarm has a behavioural cell, Q-36: no application, Active before T, idle past T, still
    Active.
  - Q-19 becomes a regression witness over a global `operator new` counter that must first count a
    known allocation. L-13, FR-052, C-4 and research R-9 now state the pre-Active allocation as a
    condition to measure, and say why a pmr counter cannot see it.
  - Quickstart's opening rule and the VII §3 row now read "RED on base, or RED on its named mutant". Each
    "—" cell names its mutant and its instrument.
- **The P3s.**
  - G93-A-07: the log rate is `max(HeartBtInt, 1 s)`, with the logger's default drop policy stated (FR-003,
    E-12, OD-5, L-11), and a Q-5 cell at HeartBtInt = 0.
  - G93-A-08: Q-34 (first-frame summary hand-off), Q-35 (the 10 s default), Q-30 (the setter's null
    and zero refusals) and Q-33 (lazy reads) are new or extended, and quickstart §4 cites them.
  - G93-A-10: C-4, FR-006 and research R-4 say the deadline ignores `clock_override`. The false
    equivalence is deleted, and Q-18 gains an override cell.
  - G93-A-11: `checklists/requirements.md` is refreshed.
  - G93-O2-02: the BeginString cap takes the configured length (OD-16, C-1 W-2), and Q-10 gains the
    configured-long session. L-16 discloses the `8=FIX` search prefix.
  - G93-A-05's two declaration names are under RC-2 above.

### Round 2 — disagreements

Each item below is one Opus marked Disagree, in whole or in part. Codex's fix was not applied.

- **G93-A-01 bullet 2 (local suspensions), and its connection-scoped watchdog.** A store that never
  completes hangs the session in every state today, Active included, so it is not
  establishment-specific. Before Active the outbound volume is the Logon reply plus at most a refusal,
  far below a socket send buffer, so a peer that stops reading cannot hold the write open. The four
  stalled-cell tests were not added. Only C-4's sentence "the stores and the transport bound them" was
  replaced by its condition.
- **G93-A-02's option 1 (result-bearing `try_*` lazy APIs) and option 2 (a headroom that makes
  exhaustion impossible).** Option 1 adds a public surface, with its own versioning, for a failure 093
  does not cause. Option 2 is not achievable, because the C cursor shells allocate per call. OD-18
  records the choice.
- **G93-A-05's fix shapes (an `expected` factory, or a throwing `pmr_carry_buffer` constructor).** Both
  change a public declaration. The Session-allocated block needs none (OD-13).
- **G93-A-06's persisted scan cursor.** It breaks C-1's single-flag state rule. The digit cap was chosen
  instead (OD-17).
- **G93-A-08, FR-053.** It is a process rule that B25's census enforces mechanically. Round 1 recorded
  this disagreement, and nothing has changed.
- **G93-A-08, FR-050 and FR-051.** Their witnesses are named mechanical gates: the golden, the freeze
  hashes, the history block and the B&L delta. That is the right shape for a documentation and version
  requirement.
- **G93-A-09, as worded.** Q-19 never claimed RED on base. The defect was the header rule and the VII §3
  row, which are corrected under RC-6.

### Round 2 — departures from the Opus fix shapes (code-verified)

- **RC-2's "nested and C cursors: `alloc_failed`" is true only of the nested getter.**
  `fixpp_msg_get_group` calls the degrading `group_slices()` and reports exhaustion as
  `FIXPP_ERR_TYPE_MISMATCH`. Both C cursor shells are allocated with `new_object` from the parse arena,
  with no catch. C-3 I-5 records each true report. Changing either changes a C-ABI result that Gate A
  has not reviewed, so both are disclosed (L-5, L-17), and the shells' defect is a lead to file,
  unconfirmed, as #540 was (since filed as fixpp#541, B28; OD-19).
- **G93-A-05's carry path over "exactly that block" would terminate on MSVC debug.** MSVC's debug STL
  allocates a container proxy from the allocator when the vector is constructed, so a block of exactly
  L + the read size spills, and a null upstream terminates inside the `noexcept` constructor. The block
  includes `kContainerSlack`, the resource's upstream is the spill witness, and Q-14 runs on the MSVC
  sandbox.
- **RC-1's "after each delivered frame" became "before each frame's delivery".** That is stricter: no
  frame is delivered once the deadline has passed, and the overrun is at most the frame already in
  delivery.
- **G93-O2-02 took the `max(...)` form, not the refusal.** That avoids a new C++, C and TOML refusal and
  a new BREAKING row (OD-16).

### Round 2 — items for the owner

None blocking. Every ruling in spec.md `## Clarifications` is implementable as written, and none was
reinterpreted: RC-1 makes the "bounded by the timeout" ruling deterministic, and R-2's ruling does not
cover lazy reads inside callbacks (OD-18). For the owner's awareness:
- OD-17 makes a BodyLength zero-padded beyond the digit cap a garble. A frame accepted today then
  becomes a garble, and C-7 row 1 marks that BREAKING. The cap's recipe is meant to make the case
  unreachable for real counterparties.
- A new lead, to be filed as an unconfirmed issue with a reproduce-first item: the C cursor shells'
  uncaught allocation (L-17). Since filed as fixpp#541 (B28; OD-19).

- Round 3 (final) reviewed 2026-10-03: Codex P1=2 P2=1 P3=2; Opus post-judging P1=0 P2=0 P3=6 — CONVERGED. Close-out applied the round-3 P3 narrowings (A-01..A-05, O3-01). Reviews: research/reviews/codex_093-inbound-frame-dispositions_gate_a_3_review.md, research/reviews/opus_093-inbound-frame-dispositions_gate_a_3_adversarial_review.md.

### Round 3 — Codex/Opus severity disagreement

Codex held G93-A-01 and G93-A-02 at P1 and G93-A-03 at P2. Opus downgraded each to a wording
over-claim: a normative sentence stated a property stronger than the mechanism delivers, or than the
code bounds, and left out the condition that makes it true. The close-out narrowing removes the
contested MUST under either reading (A-01: status-bearing reads only, the C cursor shells excepted as
L-17 / fixpp#541; A-02: ordered after `Engine::stop()`'s step 1 on the session's strand; A-03: the
configured BeginString bounded by the Logon buffer and the first-frame budget). So no Fable consult was
run (orchestrator decision, per `.claude/CLAUDE.md` "Fable consult": only when running code or a
measurement cannot settle the question).
