# fixpp#563, fixpp#564 — a send started inside an application callback (B41)

> **Status: v0.7 — Gate A converged at round 3 (2026-10-11); owner-signed-off 2026-10-11, for Gate A and
> for the plan; post-Gate-A `/analyze` applied.** Issue mode: this note is the plan; there is no
> `specs/<id>/` bundle. Branch `563-564-callback-send`. Round 3 ran on v0.5 and, after judging, left no P1
> and no P2 (Codex had raised two P2 findings; both were judged P3). v0.6 applied that round's P3 items,
> except the one the owner declined (Codex F-2). `/analyze` then ran over v0.6, and v0.7 applies its
> findings. Neither v0.6's edits nor v0.7's were reviewed by Codex. Nothing is implemented. The last
> section, "Gate A", has the tallies, the reviews, and how each round and the `/analyze` pass were applied.
>
> **Read, not run, with the exceptions named here.** §1's measurements were run before the design, and the
> record has them. Of the design's own outcomes, what has been run is: §4's searches; the measurements
> of predictions P-4 and P-11 (2026-10-11; both agree); the measurements made on today's code before
> any production edit (2026-10-11), each with its start posted: P-1 for a close started in `toApp`, P-1a,
> P-1b, P-1c, P-2, P-3, P-5 and P-6, and an early reading of P-10 with a plain post; and the measurements
> made during implementation, each on a scratch build of its mechanism: P-7 and the first part of P-10
> (2026-10-11), then the rest of P-10, P-1 for a close started in `toAdmin`, P-8 and P-9 (2026-10-12).
> None disagrees. §3 has each result with its evidence file. The Gate A rounds, their rewrites, and the
> edits of v0.6 and v0.7 ran nothing else. Every other outcome below that depends on the order of
> handlers on the strand, or on what a sanitizer reports, is still a prediction from reading. §3 lists
> each row, under "Predictions to measure first", with the measurement that settles it and what follows
> if it disagrees.
>
> **How this design was reached.** Both issues were re-measured after B35, then widened by measurement
> (`Engine::send`, `toAdmin`, both store kinds, several io threads, a close started from a callback). The
> FIX session-layer documents and the send paths of QuickFIX C++, QuickFIX/J and QuickFIX/Go were read. One
> Fable consult derived the condition in §1; it reported `probable`, so the owner decided (§0).
>
> The records are in the parent repo:
> - `research/G19-fix-fpml-iso20022/decisions/speckit/b41-563-564-measurement.md`: every measurement quoted
>   here, with the cells' output, the engine comparison table and the owner's rulings. "The record" below
>   means this file.
> - `research/G19-fix-fpml-iso20022/research/reviews/fable_b41_callback_send_contract_consult.md`: the consult.
> - `research/G19-fix-fpml-iso20022/research/probes/b41_563_564/`: the deferral-cost probe p1 and its build
>   script; P-4's probe (`p4_post_cancellation.cpp`, `build_p4.sh`) with its output
>   (`p4_post_cancellation.output.txt`); P-11's hook (`p11_hook.patch`) with its runs
>   (`p11_runs.output.txt`); and the temporary cells of the other measured predictions
>   (`p0b_measurements.patch`) with their runs (`p0b_runs.output.txt`).
> - `research/G19-fix-fpml-iso20022/research/reviews/analyze_563_564_callback-send.md`: the `/analyze`
>   report over v0.6, with the orchestrator's dispositions.
>
> The cells are on this branch: `tests/session/test_563_564_callback_send.cpp` (suite `CallbackSend`) and
> `tests/session/test_563_564_engine_callback_send.cpp` (suite `EngineCallbackSend`). They are measurement
> cells today: several are RED or abort by design until this note is implemented (§3 says which way each
> must turn).

## 0. Owner rulings (2026-10-11). Not reopened at Gate A; Gate A reviews whether this note implements them.

- **Principle.** fixpp keeps QuickFIX's observable behaviour, callback semantics included. The internal
  implementation may differ. An earlier fixpp clause that departs from it
  (`specs/019-app-callbacks/spec.md` FR-006, "enqueued behind the current dispatch") is history for this
  decision, not a constraint.
- **R-1 — `Session::send`.** A send on a session waits, by one post onto that session's executor, only
  while that session has `toApp` or `toAdmin` on the stack. Otherwise it runs inline. So a send started in
  `fromApp`, `fromAdmin` or `onLogon` is numbered, and its `toApp` runs, inside the callback, as in the
  three QuickFIX engines. Chosen over deferring from every callback.
- **R-2 — `Engine::send`.** Its control-strand lookup is replaced by the published session snapshot, in
  this PR, so that a call from a session's own callback reaches `Session::send` nested by construction and
  no longer depends on `control_strand_` being idle. Chosen over documenting "nested or deferred" and over
  always deferring.
- **R-3.** The session closed from inside one of its own callbacks is to be measured under a memory
  checker. The existing ASan preset could not reach it (§2.5); the measurement moves to this PR's
  verification.
- **R-4 — `Session::close`.** A close in either mode, started while any callback of that session is on the
  stack, begins after the callback returns. So the close never runs inside the callback, and a requested
  Logout is never emitted inside the caller. The close then meets the arm that called the callback as a
  close from another thread does today: while that arm is suspended, or after it has finished. Chosen over
  a check after each inbound callback, and over leaving the arm to carry on as QuickFIX C++ and J do. It is
  QuickFIX/Go's shape.
  - **Its reading, fixed by the owner after Gate A round 1.** v0.3 added a consequence: "an arm never runs
    on a session that its own callback's `close()` closed". Round 1 showed that one post does not deliver
    it (§2.1a), and the owner withdrew the sentence. The rule is "after the callback returns". Chosen over
    waiting for the whole operation that called the callback, and over a split rule for outbound emits;
    both are in §5.
- **R-5 — `Engine::stop()` started from a callback is out of this PR** (owner, after round 1). The note
  claims nothing about it beyond one effect of OD-3 on that route (§2.1a), and adds no cell for it;
  `ApplicationEngineSend.SendDrainRaceNoUAF`, a shipped cell, is on that route, and §3 describes it as
  what it is. What was read about that route is in
  §2.1a, the limitation is in §4, and the follow-up is fixpp#579. Chosen over a post at `stop()`'s entry
  in this PR.
- **R-6 — store order is a requirement** (owner, after round 1). "Calls to `store()` complete in the order
  they were made" becomes a documented requirement of `MessageStore::store`: a doc comment in the header,
  a B&L row, and a cell whose store completes out of order and shows the consequence (§2.1, §3, §4).
  Chosen over disclosing it as a limitation of send ordering.
- **R-7 — same-thread re-entry is outside the contract** (owner, after round 2). A callback must not run
  the session's own executor. The exposed setup is one: a `direct_executor` session attested over a bare
  `io_context` whose callback runs that `io_context` before it returns. The default mode,
  `per_session_strand`, is immune (§2.1 has the condition). The PR adds one sentence to `session.hpp` and
  to `session_executor.hpp`, widens B&L `L-007-2` from concurrent entry to same-thread re-entry with the
  same disposition, and adds debug assertions on the routes (§2.1 step 4, §2.1a, §2.2). Chosen over
  refusing the send at run time and over a callback-exit latch; both are in §5.
- **OD-6 is withdrawn: this PR declares no C-ABI change and carries no version bump** (owner, after
  round 2). The ruling it replaces ("declared breaking") rested on a sentence of v0.4 that round 2 showed
  to be false by reading: outside the route R-5 sets aside, OD-3's stop check refuses no send that
  today's code admits (§2.4). OD-3 stays,
  as the hit path's replacement for the control-strand read of `stopped_`. The note carries one
  first-step measurement on the base tree (§3, P-11); if it disagrees, the C-ABI question returns to the
  owner before the PR proceeds. It was run on 2026-10-11 and agrees (§3). Chosen over keeping the bump and
  over measuring before deciding.
- **OD-5 — the `Engine::send` bench is in this PR and its manifest row is `paired`** (owner, after
  round 2), isolated from the socket write. A `paired` row cannot be removed afterwards, and CI does not
  compare it in the PR that adds it, so this PR's evidence is a local paired run (§3, "Cost"). The bench
  is single-threaded, because the selection rule in the header of `bench/ci-suite.txt` admits only
  single-threaded, CPU-bound binaries to the manifest, and so to its paired tier; as the PR's gated
  evidence it replaces the foreign-thread probe that the first OD-5 ruling promoted; and that probe is
  kept as a local base-against-candidate measurement, reported in the verification record whichever way
  it falls and not gated (the orchestrator's disposition of 2026-10-11; the owner was told and can
  overrule it). `L-023-2` closes for the send route; its establish-churn half is rewritten and stays open
  (§4). Chosen over an in-tree bench that is not paired, and over accepting two probes as the PR's
  evidence.

The record's sections "Gate A round 1, and three owner rulings from it" and "Gate A round 2, and the
owner rulings from it" have R-4's reading, R-5, R-6, R-7 and the OD-5 and OD-6 rulings as the owner gave
them; the numbers R-5, R-6 and R-7 are this note's. Its section "Gate A round 3: converged, and the
owner's sign-off" has the Gate A sign-off and the plan sign-off (§0a). "B35's ruling R-10" below is a
ruling of another note (`.specify/544-hot-path-zero-alloc.md`), not one of these.

OD-1, OD-3 and OD-4 are not owner rulings. They are the decisions v0.3 left open, which Gate A round 1
settled; §6 states them. OD-1 is the result of a deferred send that is cancelled before it starts
(used in §2.1), OD-3 is the stop check on `Engine::send`'s hit path (§2.3, §2.4), and OD-4 is the
statement that `Engine::send` from a callback of an unpublished session cannot be admitted (§2.3).

## 0a. Constitution triggers

- **`[const §XVII.1]`, Gate A.** The change touches the public C++ behaviour of `Session::send`,
  `Session::close` and `Engine::send`, the cancellation and executor model, and the message store contract
  (R-6). Gate A applied. It ran as the loop the last section records, and it has converged.
- **`[const §XI.7]` and Appendix A, the four controls.** Appendix A's rows this change falls under are
  threading (a new await on two entry points, a new route for `Engine::send`) and error semantics
  (`dispatch_aborted` returned by `Session::send` stops implying `Disconnected`, OD-1). They are discharged
  in issue mode as `.specify/495-493-486-dict-reify-copy.md` §0.2 did:
  - Codex Gate A: converged at round 3, on v0.5 (last section).
  - `/clarify`: by hand. The owner rulings of §0 are its record.
  - `/analyze`: run on 2026-10-11 by the spec-analyzer over v0.6, after Gate A converged and before
    implementation. v0.7 applies its findings.
  - Owner `/plan` sign-off: given 2026-10-11. The owner signed off the converged note at round 3 and
    confirmed the same day that the sign-off covers the plan; the record has both. Asked whether the Gate
    A sign-off also stands as the plan sign-off, the owner answered: "Yes, it covers the plan. Push the
    branch, then proceed with implementation" (the record, "Gate A round 3: converged, and the owner's
    sign-off").
- **`[const §VII.3]`, TDD.** Each cell lands in the PR's tree with its final assertions before the
  production edit it witnesses, and the verification record shows it RED there (an abort or a failed
  assertion on at least one of the two build types). The scratch builds of §3 are never the PR's tree. A
  cell that is green on both build types before the production edit is recorded as a pin of existing
  behaviour, by name.
- **`[const §VIII.5]`, `[const §XV.1]`, `[const §XI.6]`, allocation.** The path those articles scope
  (parse to `fromApp`; parse, validate, dispatch; `MemoryStore::store`) gains no allocation; B35's gates
  are re-run (§3). The deferred path allocates per send: its coroutine frame, the payload copy and, where
  the outer send holds the slot, the fallback leaf's frame (§2.1, "Cost"). Only a send started inside
  `toApp` or `toAdmin` takes it, which is outside `B-544-1`'s "sequential `Session::send`" and inside
  what `L-497-1` leaves open.
- **`[const §VIII.3]`, "No perf change merged without a benchmark in the same PR".** R-2 changes the route
  of every `Engine::send`, and §2.2 adds counter writes to every application callback (§2.1, "Cost", has
  the one statement of how many). The note claims no speed-up. The PR carries a bench for the route, with
  a `paired` manifest row (OD-5), and a local paired run of that bench and of the in-tree benches that
  cover the counters and the depth test (§3, "Cost"). `[const §VIII.2a]` applies to the new row: CI does
  not compare it in this PR, and it cannot be removed afterwards.
- **`[const §X.7]`: no C-ABI change is declared** (OD-6 withdrawn). `fixpp_session_send` reaches
  `Engine::send`. The C ABI's symbols, types and error codes do not change. §2.4 has why no call that
  succeeds today fails afterwards, §4 ("Compatibility") has the statement a consumer reads, and P-11 (§3)
  measured the reading on the base tree and agrees with it. R-1 and R-4 are not reachable through the C
  ABI's blocking calls, which deadlock inside a callback today and still do (`L-054-1`).
- **`[const §IX]`, coverage, sanitizers, static analysis.** §3 has the three sanitizers, the coverage
  measurement with the dispositions it fixes in advance, and the tools Article IX §4 names.
- **`[const §XVII.3]`, independence.** The note's author is the Opus orchestrator. v0.4 and v0.5 were
  written by Gate A's rewrite agent (`[const §XVII.5]`). v0.6's edits were applied after convergence by a
  sub-agent working from the judge's fix shapes, and v0.7's by a sub-agent working from the `/analyze`
  report; neither set was reviewed by Codex (last section). Codex has implemented nothing here, so its
  Gate A review is independent of all of them. If Codex later implements or fixes under escalation, that
  PR's Codex review comes from a separate Codex session.
- **`[const §XVII.7]`, the local build.** Before the PR is opened, the article's cycle (Conan install,
  configure, build, ctest) runs locally on `linux-clang-debug`, and the PR description carries the
  article's one-line confirmation. The owner approves each local build before it runs. §3 names the Linux
  presets its cells and gates run on. The MSVC leg is a local build and run in the sandbox, by
  `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md` in the parent repo: the B41 test binaries
  and every test target the PR touches, on `windows-msvc-debug`, a build with assertions, and on
  `windows-msvc-release`. Its result goes into the verification record.
- No wire format, parser, codegen or security surface changes. The session FSM gains no state; what
  changes is when a `close()` started inside a callback begins.

## 1. What is wrong, and the one condition behind it

Measured at `22702dc9` (the record has each cell's output):

- On a build without `NDEBUG`, a `Session::send`, an `Engine::send` or a `Session::close` started inside
  any application callback aborts the process at the assertion in `callback_dispatch_scope`
  (`include/fixpp/session/session.hpp`). That is fixpp#564; the close case is new.
- On an `NDEBUG` build, a send started inside `toApp` or `toAdmin` runs nested:
  - at an emit that calls the callback before `assign_outbound()`, two frames carry one MsgSeqNum
    (fixpp#563; measured for the application send and for the Heartbeat that answers a TestRequest);
  - at an emit that assigns before the callback, the numbers differ but the nested frame is stored and
    transmitted ahead of the outer one, and a volatile store does not retain it (measured for a session
    Reject); a persistent store refuses it and the session disconnects.
- A send started inside `fromApp` works on an `NDEBUG` build.
- `Engine::send` from a callback runs nested on a single-threaded `io_context`. On three io threads the
  same call is nested or deferred depending on whether `control_strand_` is idle at that instant.
- A graceful `Session::close` started inside `toApp` or `toAdmin` does the same through its Logout: one
  number on two frames at a callback-first site, the Logout ahead of the outer frame at an assign-first
  site.
- On a session with no store the defect needs no suspension at all: the inner frame is transmitted while
  the starting callback is still on the stack.
- With the nested start wrapped in `asio::post`, every failing cell passes, on both store kinds and with no
  store.

**The condition.** An emit is one synchronous stretch of a strand handler: from `peek_outbound()`, through
the frame build, the callback and `assign_outbound()`, to the store call, whose first suspension is a post
in both `MemoryStore::store` and `FileStore::store`. `assign_outbound()` cannot suspend inside that stretch,
because no `SeqnumManager` method holds its mutex across a `co_await`. The order of assignment, the order of
store calls and the order on the wire are therefore the same unless a second send body enters the stretch,
and the only way in through an `Application` callback is a synchronous re-entry through the callback that
the stretch itself calls: `toApp` or `toAdmin`. The inbound callbacks are never inside such a stretch: at
every arm the peek follows the callback.

The stretch also runs code the application supplies through configuration and not through
`Application`: the synchronous part of a custom `MessageStore::store`, and a config-time `transport_send`.
A send started in either re-enters the stretch the same way. This PR does not guard that, today's guard
does not see it either, and nothing in the design depends on it (§2.1a, "Which callbacks count").

One emit has a shorter stretch. The SequenceReset-GapFill of a resend replay (`emit_gapfill_async` in
`replay_outbound_range_`) is numbered with the start of the gap, calls `toAdmin` and transmits; it assigns
nothing and stores nothing. Its NewSeqNo can be a peek taken before the `toAdmin`. The same argument
covers it, with the same scope: between its build and its transmit the only way in for another send body
through an `Application` callback is that `toAdmin` (the paragraph above has what configuration
supplies), so a send that waits for the callback to return is numbered at or after that NewSeqNo and
transmitted after the GapFill (§2.1's table has its row).

So the rule is not about which callback, and not about which emit site. It is: **no emit body on session S
may begin while S has an outbound callback on the stack.** R-1 is that rule for `Session::send`; §2.1a
applies it to the other entry point that begins an emit.

Re-derive the premises, do not trust this paragraph:
- the outbound callback invocations: `grep -n "application->toApp(\|application->toAdmin(" src/session/session.cpp`;
  each is inside a lambda passed to the stack-arena overload of `parse_and_dispatch_`;
- the lock scopes: every `co_await` in `src/session/seqnum_manager.cpp` is a lock acquisition or follows
  the end of its scope;
- the stores' first suspension: `MemoryStore::store` in `include/fixpp/session/memory_store.hpp` and
  `FileStore::store` in `src/session/file_store.cpp`. `FileStore::store` reads its executor before the
  post; that read does not suspend;
- the peek-then-callback order per site: `grep -n "peek_outbound()" src/session/session.cpp`. The search
  is keyed on the call, not on one declaration spelling, so that a peek written as an argument or inside
  a conditional is seen and classified instead of skipped. For each hit that is code and not comment,
  decide whether the value becomes a frame's MsgSeqNum. Where it does, read on to the first of
  `fire_to_admin_(`, `->toApp(` or `assign_outbound()`. Where it does not (an argument of a comparison,
  for example, or a GapFill's NewSeqNo), it is not an emit site;
- the GapFill emit: `emit_gapfill_async` in `src/session/session.cpp`, which calls `fire_to_admin_` and
  then its transmit, with no `assign_outbound()` and no store call between them.

## 2. Design

### 2.1 `Session::send`: one decision point (R-1)

**State.** For R-1 `Session` gains a strand-confined counter, the outbound-callback depth (§2.2 lists
every member the PR adds). It is a plain integer,
not an atomic: it is written and read only on the session's serialisation domain, like `fsm_state_`. It
exists in every build type.

**Writer.** One place: the stack-arena overload of `parse_and_dispatch_`, which is the only route by which
`toApp` and `toAdmin` are invoked (`fire_to_admin_`, `send_impl`, `inbound_business_reject_leaf_`). An RAII
scope increments the depth before the callback and restores the previous value after it, on every exit,
including a throwing callback. The order inside the overload is: its `callback_dispatch_scope`, then
§2.2's assertion on this depth, then the increment, then the callback (§2.2, "The members, and who reads
which"). The inbound overload (`fromApp`, `fromAdmin`) and the `onLogon`, `onLogout`
and `onCreate` sites do not write it. On a build with assertions the `onLogon` and `onLogout` sites read
it (§2.2).

Writing it in the overload, not at each call site, is deliberate: a future emit that calls `toApp` or
`toAdmin` gets the guard without being told about it. The implementer confirms the overload has no other
kind of caller (`grep -n "parse_and_dispatch_(" src/session/session.cpp`, read each).

**Reader.** `Session::send`'s body moves into a private coroutine that takes one argument: whether the
depth test applies. The public `send` becomes a plain function that returns that coroutine's awaitable
with the argument set to "apply", the shape `on_inbound_frame` has today, where it returns its arm's
awaitable; it adds no coroutine frame. The test sits after the Active check and before the frame slot is
taken:

- the argument says "skip", or the depth is zero: nothing changes. The send proceeds inline as today.
- otherwise: the send awaits the deferred path, a separate coroutine, and returns its result. That
  await sits outside the body's existing `try`, so nothing the deferred path returns is converted
  from `operation_aborted` into `Disconnected` there.

**The deferred path**, in order, in one coroutine frame that lives until the send has completed:

1. Copy the payload into storage that frame owns ("The payload", below).
2. Await one `asio::post` onto the session's executor (`asio::deferred`).
3. Apply asio's own cancellation test by hand ("Cancellation at the hop", below). If it holds, return the
   OD-1 error.
4. Assert that the outbound-callback depth is zero.
5. Return `session_invalid_state_for_send` if a `close()` has begun (`state_ == lifecycle::closing`); a
   close that has finished is refused by step 6's Active check, with the same error.
6. Await the private coroutine on the copy, with the argument set to "skip": the Active check, the slot
   holder, and today's conversion for a send in flight, and no depth test.

**When the post does not move the send out of the callback (R-7).** The post is what takes the send off
the callback's stack, and a post onto the session's executor is held back only while the handler that
called the callback is that executor's:

- **`per_session_strand`, the default, is immune where the session's entry points run on its strand,**
  which is where the reentrancy contract in `session.hpp` puts `on_inbound_frame()` and `send()`. The
  session's executor is a strand. While the callback runs in a handler of that strand, a post onto the
  strand waits until the handler returns, and a callback that runs the `io_context` runs other handlers,
  never this strand's.
- **A session under the Engine is immune whatever its `mode`.** `Engine::start()` makes a strand for each
  registered session and spawns the role loop onto it, and `Session::open()` adopts that strand as the
  session's executor (`engine_adopt_strand`) before it looks at `mode`. The C ABI's sessions are the
  Engine's.
- **`direct_executor` attested over an explicit strand is immune** for the same reason, and over a
  one-worker `thread_pool` because a handler cannot run the pool.
- **The exposed setup is one:** a `Session` that an application drives itself, in `direct_executor` mode
  attested over a bare `io_context` executor, with a callback that runs that `io_context` (`poll`,
  `run_one`, `run_for`) before it returns. The posted continuation then runs inside the callback.

The exposed setup is outside the contract: a callback must not run the session's own executor. The
attestation is defined as an executor on which the engine can keep accessing session state "as if it were
running under a strand" (`.specify/2d-threading.md`, the `direct_executor` attestation note), and a strand
never runs a second handler of the session inside the first. The shipped code is already broken there
beyond sends and closes: the callback's own run of the `io_context` can run the liveness loop's emit
inside `toApp`, which is fixpp#563 with no send involved, and can run the driver's next frame delivery
inside `fromApp`. On a build with assertions each of those aborts today at the next callback's
`callback_dispatch_scope`. No sentence names the case today; the PR adds one to `session.hpp` and to
`session_executor.hpp`, and widens `L-007-2` (§4).

A test that drives a raw `Session` from a bare `io_context` handler, as the cell fixture does with
`ExecKind::io_context`, is in the same position if its callback runs the `io_context`: the handler that
holds the callback is not the strand's. That driver has already left the reentrancy contract's
"session-strand" clause for `on_inbound_frame()` and `send()`, so this is not a second exposed setup; it
is outside the contract before R-7. NC-16 and NC-20 are the cells whose callbacks run the `io_context`,
and both use the `direct_executor` fixture; NC-16's fourth arm, the control, runs the same callback on
`ExecKind::strand`.

- **Step 4** is the diagnostic for that setup on a build with assertions. It costs one load.
- **Step 6 skips the depth test** because the alternative is worse. On a build without assertions step 4
  compiles out. A second depth test would find the depth still non-zero and defer again, and a
  continuation that posts itself again each time it runs keeps the callback's `poll()` from ever
  returning. Without the second test the send nests there, which is today's behaviour: undefined under
  `L-007-2`, as it is before this PR. The skip is an argument and not a member, so nothing is left set
  when the send returns early.

Step 5 is there because `fsm_state_` stays Active while a graceful close emits its Logout:
`run_logout_phase1` writes `LogoutSent` after the Logout's `store_then_emit` returns. Without the test, a
send deferred from that Logout's `toAdmin`, or started in the same callback after a graceful `close()`,
would pass step 6's Active check and be transmitted after our own Logout, every time. Two sends that
overlap a graceful close have the same window today, by timing; that window is not changed (§7). The test
reads `close()`'s lifecycle state, so it covers the Logout that `close()` emits. It does not cover the
confirming Logout of a logout the peer started, which is emitted with no `close()` begun (§7).

The deferred path has no Active test of its own. A session that left Active while the send waited is
refused by the Active check in step 6, with the same error.

**Why the wait is long enough.** The posted continuation runs only after the handler that held the
callback has yielded. Where the outer emit first yields depends on the branch `store_then_emit` takes, and
on every branch the outer frame's number is assigned, and its bytes are handed on, before that yield:

| Branch of the outer emit | Its first yield | What precedes that yield |
|---|---|---|
| a shipped store (`MemoryStore`, `FileStore`) | the post that begins the store call | the assign, and the store call itself |
| a `MessageStore` whose `store` completes without yielding, or no store at all (`store_` is null when no factory is configured) | the live write: the write gate if a write is in flight, else the transport's write, which holds the gate | the assign, the completed store call if there is one, and the gate request |
| the same, with a synchronous `transport_send_` | none: the emit completes inside the handler | everything |
| the GapFill of a resend replay (no assign, no store; §1) | the live write, as in the second row, or none with a synchronous `transport_send_` | the gate request, or the whole transmit |
| a failure exit (a veto, a throwing callback, a failed assign) | wherever the exit path yields | the outer frame is abandoned; nothing of it is stored or sent |

So the deferred send peeks after the outer assign; its store call follows the outer one; and its request for
the write gate follows the outer one, because the handler that completes a store goes on to request the gate
before it yields again. A deferred send is then exactly an overlapping send. Three premises carry the rest,
each already relied on by two sends that overlap today:

- **A store completes its `store()` calls in the order they were made.** The interface did not say so
  before this PR: the doc comment of `MessageStore::store` (`include/fixpp/session/message_store.hpp`)
  states the copy and the sequence check and nothing about concurrent calls. Both shipped stores do it, by
  a leading post and then a FIFO writer mutex (`MemoryStore::store`; `FileStore::store` takes its mutex on
  the strand before it offloads). R-6 makes it a requirement on every implementation (§4). A store that
  completes two calls out of order has its two frames transmitted out of order, whether the second send
  was deferred or merely overlapping; a cell shows that (§3). B&L `L-518-1` also names a store-order
  premise, but for `reset_to` against the teardown reset. It does not cover `store()`, and v0.3 was wrong
  to cite it for this.
- The write gate grants in arrival order (`async_mutex`'s FIFO grant).
- An uncontended `assign_outbound()` does not yield (§1).

Measured: `OverlappingSends` and `OverlappingSends_FileStore`, `OverlappingSends_NoStore` and its live-write
twin, and the post controls of the nested cells on both stores and with no store.

**Cancellation at the hop (OD-1).** What asio does, read in asio 1.38.0 (the version `conanfile.py` pins;
`asio/impl/awaitable.hpp`):

- A `co_await` inside an `asio::awaitable` tests for cancellation when the await begins. Both
  `await_transform` overloads, the one for an awaitable and the one for an asynchronous operation, throw
  `operation_aborted` when `throw_if_cancelled` is set and the thread's cancellation state already records
  a cancellation. Nothing tests when the coroutine resumes.
- `asio::post` installs no cancellation handler: neither `asio/post.hpp` nor
  `asio/detail/initiate_post.hpp` uses a cancellation slot. A cancellation emitted while a coroutine waits
  on a post is recorded in the cancellation state, the post completes normally, and the coroutine resumes.
  `MemoryStore::store`'s comment on its own leading post relies on the same pair of facts.
- What is recorded depends on the state's filter. A chain started by a plain `co_spawn` records terminal
  cancellation only: `asio/impl/co_spawn.hpp` builds the chain's state with the one-argument
  `cancellation_state` constructor, whose filter is `enable_terminal_cancellation`
  (`asio/cancellation_state.hpp`). A filter also decides what a chain passes on to a coroutine it spawns
  and awaits, so a `total` emission on a chain with the default filter never reaches an inner body, whatever
  that body enabled. `Engine::send`'s session-strand body enables total, for a caller whose own chain
  passes it. A thread that has taken any `async_mutex` lock is left at terminal only (the fourth part of
  the lock sequence, `include/fixpp/core/sync/async_mutex.hpp`).
- A recorded cancellation can be erased. The state's handler assigns the filtered value of each emission
  to its record; it does not accumulate (`asio/cancellation_state.hpp`, the call operator of the state's
  `impl`). So a recorded `terminal` cancellation is erased when a later emission that the filter reduces
  to nothing, a `total` under the default filter, arrives on the same signal before the coroutine's next
  await. P-4's probe measured it: with a `terminal` and then a `total` emission during the wait, the
  record reads `none` after the post and the next await does not throw (§3).

So a deferred send that is cancelled while it waits does not throw at the post. Left alone it would resume,
and the recorded cancellation would throw at its next await. If that await is inside `Session::send`'s
`try`, the `catch` there writes `Disconnected`, a conversion meant for a send in flight. If it is outside,
the exception leaves a function whose callers expect an `expected_t`. This send has consumed nothing: no
number, no store slot, no frame. So in step 3 the deferred path applies asio's test itself
(`co_await asio::this_coro::throw_if_cancelled()` is set, and
`(co_await asio::this_coro::cancellation_state).cancelled()` is not `none`) and returns the OD-1 error
with the session's state untouched. The lock sequence of `async_mutex` applies the same pair by hand
(`FIXPP_DETAIL_CO_AWAIT_LOCK` in `include/fixpp/core/sync/async_mutex.hpp`). A chain that turned
`throw_if_cancelled` off is therefore not refused at the hop, as asio would not throw for it; NC-4's fifth
arm pins that half of the test. Step 3 reads the record that asio's own test reads, so it also behaves as
asio does where a recorded `terminal` cancellation was erased by a later emission that the filter
dropped: that send is not refused at the hop, and its next await would not have thrown either. NC-4 needs
no arm for it.

- No test is needed before the post. The caller's own await of `Session::send` began with the same test,
  and nothing suspends between that await and the post, so the state cannot have changed in between.
- Between step 3 and step 6's first await nothing suspends, so nothing can be recorded there. A
  cancellation that arrives later meets a send in flight and takes today's conversion.
- The natural implementation, a bare post followed by the send, is the mutant "the cancellation test
  removed" of §3.

This is the hazard that made B35's ruling R-10 reject a session send gate
(`.specify/544-hot-path-zero-alloc.md` §2.5): there the new await sat on every send; here it sits only on a
send started inside an outbound callback. The sequence above was read first and then measured: P-4's
probe agrees with it (§3, prediction P-4).

**The payload.** `Session::send` takes a span, and its doc comment states no lifetime rule. Today the span
is consumed before the send's first suspension: `send_impl` begins with `build_send_frame_`, which copies
it into the frame buffer. So a callback may pass a span over a buffer of its own and start the send
detached. On the deferred path the build runs after the callback has returned, when that buffer may be
gone. So step 1 copies the payload before the post and step 6 sends the copy. The copy's storage comes
from the default allocator and is owned by a local of the deferred path's coroutine: a
`std::vector<std::byte>`, the container `Engine::send` uses for its own copy (§0a has the allocation
articles' disposition). The frame that owns the copy
awaits the inner send, so it outlives it. Decided by the orchestrator at round 1 (the alternative is in
§5). The cost is one allocation for the copy, on the deferred path only; a send that does not wait copies
nothing more than today. `Engine::send` already copies the payload twice before `Session::send` runs
(§2.3), so a send that reaches the deferred path through it is copied a third time (§7).

**The frame slot.** The holder is taken in step 6, after the hop. Which buffer the deferred send gets
depends on where the outer emit is by then (the table above):

- The outer send is suspended in its store or its write: it still holds the slot, and the deferred send
  takes `send_fallback_leaf_`, whose frame owns its buffer and so allocates, as a send nested in `toApp`
  does today.
- The outer send completed inside its handler (no store, synchronous sink): its holder has released the
  slot, and the deferred send takes the session's slot.
- A send deferred from `toAdmin` finds the slot free unless an application send is suspended at that
  moment.

Nothing in the design depends on which buffer it takes.

**Where the hop lives.** In the deferred path's own coroutine, which the send's body awaits only when the
depth test sends it there. On the path that does not wait, the body runs the same awaits in the same order
as today, so the position of its first cancellation check, which B35 measured, does not move. Its frame may
grow by the argument and by the storage one more await expression needs; B35's gates are re-run for that
reason (§3).

**Cost.**

- Every application callback, in every build type: two writes to a plain integer, an increment and a
  decrement of the depth of all callbacks (§2.2). `callback_dispatch_scope` compiles to nothing under
  `NDEBUG` today, so this is new work on the inbound path, `fromApp` included. An outbound callback makes
  two more, for the outbound depth. This is the one statement of the counters' cost; §0a and §4 point here.
- A send that is not started inside an outbound callback: a test of the argument, one integer load and a
  branch.
- A send that is: the deferred path's coroutine frame, one allocation for the payload copy, one post, and
  the fallback leaf's frame where the outer send still holds the slot. Probe p1 measured a post on the
  reply path at +210 to +270 ns on the Engine's session executor type, with no call to `operator new` or
  `aligned_alloc`. p1 had no payload copy in it.

B35's zero-allocation gates must stay green (§3). The counter writes and the depth test are not assumed
free: §3 measures them with the in-tree benches that drive those paths.

**What an application observes.**

| Send started in | Before (`NDEBUG`) | After (every build type) |
|---|---|---|
| `fromApp`, `fromAdmin`, `onLogon` | nested | nested: numbered, and its `toApp` run, inside the callback |
| `toApp`, `toAdmin` | nested; one number on two frames, or frames out of order | starts after the callback returns, over a copy of the payload taken before it returned; numbered after the frame whose callback started it; transmitted after it, on a store that meets R-6's requirement |
| `onLogout`, `onCreate` | refused with `session_invalid_state_for_send`: the session is not Active. `onLogout` runs after the state it leaves Active for has been written, and `onCreate` runs at the end of `open()`, before the session can be Active | unchanged |
| outside any callback | inline | unchanged |

On a build with assertions the left column was an abort in the `fromApp` row and in the `toApp` row. The
`onLogout` row and the last row were as on an `NDEBUG` build: a send that is refused at the Active check,
and a send outside any callback, enter no callback scope. NC-2 pins the `fromAdmin` and `onLogon` cases
of the `fromApp` row. The right column is stated for
every configuration inside the contract. In the exposed setup of R-7, a send started in `toApp` or
`toAdmin` by a callback that then runs the `io_context` nests on a build without assertions, as it does
today.

Two results are new for a send started in `toApp` or `toAdmin`:

- It returns `session_invalid_state_for_send` if, by the time it starts, the session has left Active or a
  `close()` has begun.
- Cancelled before it starts, it returns the OD-1 error, `dispatch_aborted`, and the session stays as it
  was. Until now `dispatch_aborted` from `Session::send` always came with `Disconnected`. After this PR a
  caller reads `state()` to tell the two apart (§4).

### 2.1a Every entry point that can begin an emit

The condition of §1 is about emit bodies, and `Session::send` is not the only way an application starts
one. The public coroutine entry points (`grep -n "asio::awaitable<" include/fixpp/session/session.hpp
include/fixpp/session/engine.hpp`, the public ones), each with what it can begin when an application starts
it from a callback of the same session:

| Entry point | Can it begin an emit inline? | Disposition |
|---|---|---|
| `Session::send` | yes | §2.1 |
| `Engine::send` | only through `Session::send` | §2.3; guarded by §2.1 |
| `Session::close`, graceful | yes: `run_logout_phase1` peeks, calls `toAdmin`, assigns and stores, unless a store's flush hook suspends first | below (R-4) |
| `Session::close`, terminal | no: it emits nothing, but it closes the session inline | below (R-4) |
| `Engine::stop` | no emit | out of this PR (R-5); "`Engine::stop()` started from a callback", below |
| `Session::open`, `Session::drive_reconnect` | the initiator's Logon | engine-driven; not an application entry point from a callback. `open()` is refused on a session that is already open. `drive_reconnect()` has no run-time refusal: it is public only so that `engine.cpp` can reach it, and calling it from anywhere else breaks the engine-only convention its doc comment states |
| `Session::on_inbound_frame` | reply emits | fed by the read pump; an application that feeds frames from inside a callback is outside the contract (contract C-3 I-3). On a build with assertions the frame aborts at its first parse over the session's parse buffer, whichever callback fed it; §2.2's nesting table has the frames whose arm parses nothing there, and its "not detected" rows the ones that pass |

**`Session::close`, both modes (R-4).** `close()` waits by one post onto the session's executor when the
depth of all callbacks of that session is non-zero, and then proceeds as it does today. The check is in
`close()` itself, not at the application's call, so it covers every caller, including the terminal close
that `send_impl` awaits when a nested send's `toApp` throws.

- **Which callbacks count.** The ones that run under `callback_dispatch_scope`: `fromApp`, `fromAdmin`,
  `toApp`, `toAdmin`, `onLogon`, `onLogout` and `onCreate` (`grep -n "callback_dispatch_scope cs"
  src/session/session.cpp`). A function the application supplies through configuration, not through
  `Application` (a logon validator, a store, a transport sink, a log sink), is not under the scope and is
  not counted. Starting a `close()` or a send from one of those is not changed by this PR.
- **Where the wait sits.** After `close()`'s existing
  `reset_cancellation_state(asio::disable_cancellation{})` and before its three-state test of `state_`. So
  the test runs on the state the close finds when it resumes: a close that finds the session already
  closed returns `session_already_closed`, and one that finds a close in flight joins it, as a repeated
  close does today. It follows that the wait is before the `state_ = lifecycle::closing` write.
- **Its shape.** One post, then an assertion that the depth of all callbacks is zero, for the reason of
  §2.1's step 4. Not a loop: in the exposed setup of R-7 a close that waited again each time it found the
  depth non-zero would keep the callback's `poll()` from returning, for step 6's reason. On a build
  without assertions the close runs inside the callback there, which is today's behaviour.

**What one post delivers, and what it does not.** The post is queued while the callback is on the stack.
It runs at the first point where the handler that called the callback yields. That is a position in the
strand's queue. It is not the end of the arm that called the callback. So:

- **The close never runs inside the callback.** Inside the callback, after the start, the session is as it
  was: `onLogout` has not run and no Logout has been emitted.
- **The arm can be suspended when the close runs.** After `fromApp` returns, an Active arm on a persistent
  store awaits `persist_inbound_advance_`, and any arm that answers the frame awaits its reply's emit. The
  close runs during that suspension, and the arm resumes on a closing or closed session. That is the
  position of a close from another thread today, and the session's existing handling of it applies
  (`close()`'s drains of the seqnum manager and the write gate; the Logon arms' superseded check).
- **An outbound emit can be cut.** The close's post is queued inside `toApp` or `toAdmin`. On a shipped
  store the outer emit's first yield after the callback is the post that begins its store call (§2.1's
  first-yield table), and that post queues behind the close's. The outer frame's number is assigned by
  then: before the callback at an assign-first site, after it at a callback-first one. So on a shipped
  store a terminal close runs while the outer frame's store call is suspended, and before the frame is
  transmitted. The table's other branches, a store that completes without yielding and no store, were
  not measured. Measured for a close started in `toApp`, on the shipped stores, with the start posted
  (P-1; §3 names the evidence): the outer frame's store call begins before the posted close runs and
  completes after it; on a live transport the frame is retained under its number and not transmitted,
  and its send returns `dispatch_aborted`.
  - **With `reset_on_disconnect` set** (P-1a, measured; §3 names the evidence): the close is suspended
    in its teardown reset before it has drained the write gate, so the outer frame's write passes the
    gate and reaches a transport that is already closed. On a transport that refuses a write after its
    `close()`, nothing is transmitted and the send returns `dispatch_aborted`; the store ends empty,
    with both next numbers at their initial value. This is stated from the arm whose transport double
    refuses the write. A double whose `close()` does nothing reports the frame as transmitted, which is
    an artifact of the double.
  - A graceful close started there emits the outer frame first, then the Logout (measured with the start
    posted, on the cells' synchronous sink and on the live transport; P-1b).
  - NC-6 is the cell for a terminal close started in `toAdmin`. P-1 was measured there on 2026-10-12, at
    a callback-first site and at an assign-first one, and agrees (§3's P-1 row has it).
- **The close can be late, on a volatile store.** The read pump delivers every complete frame of one read
  with no yield between them, and an Active arm on a volatile store does not suspend after `fromApp`. So
  on a volatile store a `close()` started in `fromApp` for one frame begins only after the later frames
  of the same read have been delivered: their callbacks run, their replies are sent, and a send started
  there is admitted. Today's inline close stops them. Measured on a `MemoryStore` with two frames (P-3;
  §3 names the evidence): the second `fromApp` runs before `onLogout`. On a persistent store the bullet
  does not hold. Measured on a `FileStore` (the same row): the arm suspends in its persist, the close
  runs there, the second `fromApp` does not run, and the second frame's `on_inbound_frame` returns ok on
  the closed session. Disclosed in §4.

The owner chose this reading knowing these costs (§0, R-4). §2.5 has what it means for the state v0.3 said
it removed.

**The two modes.**

- **Graceful.** Measured without the wait: started inside `toApp` or `toAdmin`, its Logout takes the outer
  frame's number at a callback-first site and goes out ahead of the outer frame at an assign-first site,
  and the outer frame is then transmitted after our own Logout. With the start posted, the outer frame goes
  first and the Logout follows, both retained. On a `MemoryStore` the Logout's `toAdmin` runs before the
  outer frame is transmitted, while the outer emit is suspended in its store call; the wire order is
  still the outer frame, then the Logout (P-1b, measured; §3 names the evidence). The wait also keeps the
  Logout's `toAdmin` off the caller's stack in `fromApp`, where order was not at risk.
- **Terminal.** It begins no emit. Without the wait it closes the session under the callback's feet: the
  session is `closed_drained`, and `onLogout` has run, before the call returns (§2.5). With the wait it
  runs after the callback, at the positions above.

**The lifecycle callbacks.** R-4 says "any callback", and these three sit in different places. Each
disposition was read first and then measured with the start posted (P-5; §3 names the evidence and the
routes that were run). The measurement agrees in every arm, and every arm ends closed:

- **`onLogon`** runs inside the Logon arm, at the write of Active and before the arm's persist. A close
  started there begins after `onLogon` returns. Where the arm persists the inbound advance on a persistent
  store, the close runs while that persist is suspended; the arm's next statement is its superseded check
  (`logon_arm_superseded_`), which is already there for a close that ran during the persist, and the arm
  stops without starting the liveness loop. Where the arm does not suspend there (a volatile store, or a
  Logon that skips the persist), it finishes first, liveness loop included. A graceful close then sends its
  Logout after the Logon exchange.
- **`onLogout`** runs inside `record_state_transition_`, on the first transition out of Active. Where a
  `close()` made that transition, a close started in `onLogout` waits and then finds the first one in
  flight or finished: it joins it, or returns `session_already_closed`. A terminal close that joins a
  graceful one escalates it (093 plan OD-29). Where a `Disconnected` write outside `close()` made the
  transition (`Session::send`'s, for one), no close is in flight, and the close started in `onLogout` is
  the session's first.
- **`onCreate`** runs at the end of `open()`, before the role loop connects or publishes. A close started
  there begins after `onCreate` returns. `open()` has no suspension after the callback, so it returns
  success, and the close runs at its caller's first suspension. The session never reached Active, so no
  Logout is sent and `onLogout` does not run. On a raw initiator `open()` transmits the Logon before it
  calls `onCreate` (measured, P-5), so a close started in `onCreate`, in either mode, ends the session
  after a Logon and with no Logout. This disposition is a raw-`Session` one, and NC-11 is its
  cell. Under the Engine an application has nothing to close with in `onCreate`: the callback receives a
  `SessionId`, and `Engine::lookup` finds no session until the role loop has published it, which is after
  `open()` returns.

What the three QuickFIX engines do, read: an application's `logout()` sets a flag (C++, J) or queues a
request (Go), and the engine's own loop generates the Logout later, so it is never emitted inside the
caller. `disconnect()` in C++ and J runs inline, `onLogout` included, and the inbound handler then carries
on with the frame. Go has no inline close: a stop is queued to the session's loop. R-4 follows all three
for a graceful close and follows Go for a terminal one; the departure from C++ and J is that `onLogout`
runs just after the callback, not inside it.

**Cancellation and the wait.** The wait must not be a way to skip the close, and by asio's sequence
(§2.1, "Cancellation at the hop") it cannot be one where it sits:

- `disable_cancellation` is the state's filter from `close()`'s reset on, so a cancellation emitted during
  the wait is not recorded, and the reset built a state that records none. The post has no cancellation
  handler. No lock precedes the wait, so nothing has replaced that filter. The close resumes and runs.
  P-4's probe measured this order on asio alone, in its arm that resets to `disable_cancellation` before
  the post (§3).
- This is the in-place disable, and it is enough here. 093's reset unit could not use it: its store
  operation takes a lock, and the lock sequence replaces the filter, so 093 plan OD-25 moved that operation
  onto its own awaitable thread bound to an empty slot. v0.3 cited OD-25 as the model for this wait; that
  cite pointed at the wrong mechanism and is withdrawn.
- A caller whose chain is already cancelled when it evaluates `co_await close()` throws before `close()`
  is entered. That is so today, with or without the wait, and is not changed. One existing path has it:
  `send_impl`'s `co_await close(terminal)` after a throwing `toApp`, on a chain already cancelled, leaves
  the session `Disconnected` and not closed. Noted in §7.
- **The `io_context` destroyed during the wait.** The frame is destroyed at the post, before the `closing`
  write, so the lifecycle is still open and a later `close()` runs normally. Had the wait been placed after
  that write, the lifecycle would stay `closing` and every later `close()` would join a close that never
  completes. One route differs: where `send_impl` awaits `close(terminal)`, the destroyed chain includes
  `Session::send`'s slot holder, which writes `Session` memory when it is destroyed. That is `L-544-2`'s
  rule (keep the `Session` alive until the send's frame has completed or been destroyed), now reachable
  from a wait that did not exist before (§4).
- **At the send's hop**, for comparison: the slot holder is taken after the hop (§2.1), so a deferred send
  destroyed at its post has written no `Session` memory. Through `Engine::send` the only destructor that
  runs is the send counter's guard, on a counter the frame keeps alive.

**`Engine::stop()` started from a callback (R-5: out of this PR).** v0.3 said this route was covered at
`Session::close`. It is not, and the claim and its cell are removed. What was read (Gate A round 1, finding
F-7; not run):

- `stop()` reaches `Session::close` in its step 4, after it has joined the role loops in step 3. The
  callback that started the stop runs inside a role loop, so it returned long before. R-4's wait never
  fires on this route.
- What reaches the session while the callback is on the stack is steps 1 and 2. `stop()`'s body begins
  with a `co_spawn` onto `control_strand_`, which runs inline when that strand is idle. Step 1 then
  `co_spawn`s onto the session strand, which is running in this thread, so the engine-stop flag is set and
  cancellation is emitted inside the callback. Step 2 `co_spawn`s the transport's close onto the same
  strand. On one io thread, then, the callback's `co_spawn` of `stop()` can return with the session's
  transport already closed.
- On several io threads it depends on whether `control_strand_` is idle at that instant, which is the
  timing dependence R-2 removes for `send` and leaves in place for `stop`.
- **This route is the one on which OD-3 refuses sends that are admitted today.** Where step 1 runs
  inside the callback, as above, the engine-stop flag is set ahead of send bodies that are already
  queued on the session's strand. Today those bodies passed their control-strand step before `stopped_`
  was written, and they run; after this PR the stop check of §2.3 refuses them with
  `session_invalid_state_for_send`. It is a C++ route only. The C ABI reaches `Engine::stop` in
  `fixpp_engine_destroy` alone (`grep -n "stop()" src/capi/*.cpp`), a blocking call that deadlocks inside
  a callback (`L-054-1`).

Re-derive by reading `Engine::stop` in `src/session/engine.cpp` from its first `co_spawn` to its step 4.
The limitation row is in §4; the follow-up issue is fixpp#579. A shipped cell is on this route, found by
reading at round 2: `ApplicationEngineSend.SendDrainRaceNoUAF` (§3).

### 2.2 The debug guard keeps its meaning (fixpp#564)

`callback_dispatch_scope` asserts today that no callback of the session is on the stack when one is
entered, whatever the pair. That one assertion carries four meanings. v0.3 kept the first. v0.4 restored
the second and the third and missed the fourth.

| What the assertion does today | Why it matters | What carries it after this PR |
|---|---|---|
| It detects a second thread entering a session's callbacks: a `direct_executor` attested over an executor that does not serialise. This is the misuse its message names. | It is the debug diagnostic for a broken threading contract. | An **atomic owner token**. Entering a scope compares and swaps the token from "no owner" to the current thread. If the swap fails and the owner is another thread, the assertion fires. If the owner is the current thread, the entry is nested and passes. The scope that installed the token clears it on exit; a nested scope leaves it alone. The swap is the scope's first action and the clear is its last, so the depth is incremented after the swap and decremented before the clear. |
| It does that through an atomic exchange, so the detection is not itself a data race. | In the misuse, two threads read and write the guard's state at once. With a plain depth and a plain owner the second thread can read both as free and pass, and the death cell would be a TSan report on the guard. | The same token. A foreign thread that enters a scope reads and writes only the atomic there, and asserts before the scope touches a plain member. The assertions at the parse sites, which read a depth ahead of a scope, are the stated exception ("The members, and who reads which", below). |
| It makes a nested inbound parse abort. 093 contract C-3 I-3 depends on that ("No inbound parse nests inside another. Callbacks are synchronous, and the callback scope asserts it"), and so does the comment on the inbound overload of `parse_and_dispatch_`: one parse buffer per session is safe because no inbound parse runs inside another. | Without it, a `fromApp` that starts `on_inbound_frame` again rebuilds the parse resource over `inbound_parse_buf_` while the outer callback's `MessageView` still points into it: silent corruption where there was an abort. | **An assertion on the depth of all callbacks at each parse site over the session's buffer.** A parse site is a function that builds a memory resource over `inbound_parse_buf_` (`grep -n "inbound_parse_buf_" src/session/session.cpp`): the inbound overload of `parse_and_dispatch_`, and `validate_inbound_`, which the arms call before dispatch when the validate gate is on. Each asserts, before it builds the resource, that the depth of all callbacks is zero. |
| It makes every other same-thread entry abort: an inbound frame fed from `toApp`, `toAdmin`, `onLogon`, `onLogout` or `onCreate`, an outbound callback entered inside an outbound callback, and `onLogon` or `onLogout` entered inside another callback. | The first is a frame the application fed from inside a callback, which the contract does not allow from any callback (§2.1a); from `toApp` or `toAdmin` the nested arm's replies would begin emits inside the outer emit's stretch. The second is R-1's rule being broken: by the run of the `io_context` that R-7 excludes, or by a future emit that does not go through `Session::send`. The third is the session entering or leaving Active under a callback. The design relies on one case of it, leaving Active inside an inbound callback or `onLogon` (a nested send that disconnects; the table below). Inside `toApp` or `toAdmin` it happens inside an emit's stretch, and the outer emit then carries on; it is reached there only from outside the contract. Those are the routes on which a diagnostic is wanted. | The parse-site assertion of the row above, which reads the depth of all callbacks and so fires whichever callback is on the stack. And **an assertion in the stack-arena overload that the outbound depth is zero.** And **an assertion at the `onLogon` and `onLogout` sites that the outbound depth is zero**, which carries the third inside `toApp` and `toAdmin` only. Each of these two is evaluated after its site's `callback_dispatch_scope` has been constructed, and before the outbound depth is raised or the callback is called ("The members, and who reads which", below). |

**The inbound-parse mark of v0.4 is removed.** It was a member of its own, set by the inbound overload,
whose assertion fired on an inbound parse inside an inbound parse. Two reasons:

- The parse-site assertion implies it. `validate_inbound_` calls no application code, and the inbound
  overload calls application code only through its callback, under `callback_dispatch_scope`. So a parse
  over the
  buffer that begins while another is in progress begins with a callback of the session on the stack,
  and the depth of all callbacks is non-zero there. A mark kept beside the depth assertion would carry
  an assertion that no cell could show to be missing.
- The mark did not cover `validate_inbound_`. A frame fed from `fromApp` that the validate gate rejects
  is parsed over the buffer and never reaches the inbound overload.

**Every nesting on one thread, against the checks.** The "Depths" column gives the depth of all callbacks
and then the outbound depth, as the inner entry finds them. "Lifecycle site" means the scopes at
`onLogon`, `onLogout` and `onCreate`, which carry the token and the depth of all callbacks. The `onLogon`
and `onLogout` sites also assert that the outbound depth is zero; the `onCreate` site has no assertion of
its own. The stack-arena overload's check and the lifecycle sites' check are each evaluated after the
site's scope has been constructed, so on one thread the token has already passed when the depth is
read.

| Inner entry, inside what | Depths | Check at the inner entry | Result |
|---|---|---|---|
| **Legal: the design relies on it** | | | |
| `toApp`, inside `fromApp`, `fromAdmin` or `onLogon`: a send started there nests (R-1) | non-zero, zero | stack-arena overload: the outbound depth is zero | passes |
| `onLogout`, inside `fromApp`, `fromAdmin` or `onLogon`: a nested send wrote `Disconnected` inline (§2.5 lists the routes) | non-zero, zero | lifecycle site: the outbound depth is zero | passes |
| any callback of session B, inside a callback of session A, with no callback of B on the stack | zero, zero: B's own | B's checks read B's members | passes |
| **Outside the contract: must abort** | | | |
| a parse over the buffer, inside `fromApp` or `fromAdmin` (C-3 I-3) | non-zero, zero | parse site: the depth of all callbacks is zero | aborts |
| a parse over the buffer, inside `toApp` or `toAdmin` | non-zero, non-zero | the same | aborts |
| a parse over the buffer, inside `onLogon`, `onLogout` or `onCreate` | non-zero, zero | the same | aborts |
| `toApp` or `toAdmin`, inside `toApp` or `toAdmin` | non-zero, non-zero | stack-arena overload: the outbound depth is zero | aborts |
| `onLogon` or `onLogout`, inside `toApp` or `toAdmin` | non-zero, non-zero | lifecycle site: the outbound depth is zero | aborts |
| any callback, entered from a second thread while a first is inside | any | the token; for an inbound frame the parse-site assertion can fire first | aborts |
| **Outside the contract: not detected** | | | |
| `toApp` or `toAdmin` of an emit that is not a send, inside `fromApp`, `fromAdmin` or a lifecycle callback | non-zero, zero | stack-arena overload: the outbound depth is zero | passes |
| `onLogon` or `onLogout`, inside `fromApp`, `fromAdmin` or a lifecycle callback, on a route other than the legal row's | non-zero, zero | lifecycle site: the outbound depth is zero | passes |

The first "not detected" row has the depths of the first legal row, so no check on these counters can tell
them apart. It is
reached two ways, both outside the contract. One is the engine's own emit under the run of the
`io_context` that R-7 excludes. The other is the reply to a frame fed from inside the callback whose arm
parses nothing over the buffer before it emits: with the validate gate off, a frame above the expected
MsgSeqNum is answered by a ResendRequest and calls no receive callback. Today's assertion aborts both.
Inside `fromApp` or `fromAdmin` neither is inside an emit's stretch, because an inbound callback never is
(§1), so there neither can put one number on two frames. Where a deferred send or a waiting close is also
involved, step 4 or the close's assertion reports the run of the `io_context`.

The second "not detected" row has the depths of the second legal row, and the same holds for it. It too
is reached only from outside the contract. One route is a frame fed from inside the callback whose arm
writes `Disconnected` without a parse over the buffer and without an emit: with the validate gate off, a
frame whose BeginString is not the session's is disposed of that way, and so is one whose CompIDs are
not where the CompID check is on; `onLogout` runs inside the feeding callback. The other is a `Disconnected`
write the engine makes itself under the run of the `io_context` that R-7 excludes: the liveness loop
writes that state directly, without `close()`, so the close's assertion does not see it. Today's
assertion aborts both. Started in `toApp` or `toAdmin`, the
same routes disconnect the session inside an emit's stretch, and the outer emit then carries on. That
half is detected: it is the "must abort" row for `onLogon` and `onLogout`, carried by the lifecycle sites'
assertion. What stays undetected is the half inside an inbound or lifecycle callback; an inbound callback
is never inside an emit's stretch (§1).

The losses in the "not detected" rows are disclosed in the guard's B&L row (§4). A frame
fed from inside a callback whose arm neither parses over the buffer nor calls a callback is not detected
either; it is not detected today.

One nesting is absent from the legal rows on purpose. A nested send whose `toApp` throws does not run
`onLogout` inside the inbound callback after R-4: `send_impl` awaits a terminal close, which waits.

**The assertions' messages.** The token's assertion keeps today's text, which names the cross-thread
misuse. The parse-site assertion, the stack-arena overload's, the lifecycle sites', step 4's and the close's each get a text of
their own, and every death arm in §3 matches the text of the assertion it is written for, so that an arm
cannot pass on another assertion's abort.
`InboundFrameDispositionsI3.AnInboundParseReenteredFromACallbackDies` keeps its meaning. Its nested frame
is in sequence, because the Active arm advances the inbound counter before it calls `fromApp`, and its
fixture leaves the validate gate off, so the nested arm reaches the inbound overload and dies at the
parse-site assertion. The test's expected text, which is today's scope message, is edited to the
parse-site text, and the comment above it with it (§4).

Today's scope also clears the flag unconditionally in its destructor, so a nested scope's exit would clear
the outer scope's entry. The token's rule (only the scope that installed it clears it) replaces that.

**The members, and who reads which.** `Session` gains the owner token in place of `in_dispatch_`, and
two depths. Nothing else: v0.4's mark and its skip flag are both gone. The depths are plain integers,
written and read only on the session's serialisation domain, and maintained in every build type:

- **The depth of all callbacks.** `callback_dispatch_scope` increments it on entry and decrements it on
  exit. `close()` reads it (§2.1a). On a build with assertions the parse sites and the close's wait read
  it too.
- **The depth of outbound callbacks.** Written in the stack-arena overload (§2.1). The send's body reads
  it. On a build with assertions the stack-arena overload's assertion, the assertion at the `onLogon` and
  `onLogout` sites and the deferred path's step 4 read it too.

Neither depth is atomic, and the cross-thread check reads neither. Under the misuse the token detects, two
threads would race on them, as they already race on every strand-confined member of `Session`; a build
with assertions aborts at the token before it reaches them. An atomic depth for `close()` to read was
considered and not taken (§5).

The parse sites remain the stated exception to that last statement: their assertion reads a depth
ahead of a scope. `validate_inbound_` has no scope, and the inbound overload builds its resource before it enters
one, so a foreign thread that delivers a frame reads the depth of all callbacks before it reaches a
token. If it reads non-zero it dies at the parse-site assertion; if it reads zero it dies at the token.

**The stack-arena overload and the `onLogon` and `onLogout` sites are not exceptions.** At each of
them, the depth assertion is evaluated after that site's `callback_dispatch_scope` has been constructed,
and before the outbound depth is raised or the callback is called. So a foreign thread that enters one
of them while a first thread is inside dies at the token before it reads a plain depth. Decided by the
orchestrator on 2026-10-11. The text v0.7 first carried put the stack-arena overload's assertion ahead
of its scope and did not say on which side of the scope the lifecycle sites' assertion sits (the last
section, F1). The decision has had no review.

- **What the source shows.** Each of these sites already constructs its scope immediately before it
  invokes the callback. The stack-arena overload does so after it has framed and parsed the frame over
  its stack arena. `record_state_transition_` does so in its `onLogon` branch and in its `onLogout`
  branch, after the state has been written. Re-derive: `grep -n "callback_dispatch_scope cs"
  src/session/session.cpp`, and read from each hit to the callback it guards.
- **No assertion moves relative to the callback.** At each site the assertion still precedes the
  callback, and in the stack-arena overload it still precedes the raise of the outbound depth. What
  changes is that it follows the scope, and so, in the stack-arena overload, the overload's own parse.
  A frame that fails to parse there returns before the scope and calls no callback, so no nesting the
  assertion exists for is passed over.
- **The order in the stack-arena overload:** the scope, the assertion, the raise of the outbound depth,
  the callback. At the `onLogon` and `onLogout` sites: the scope, the assertion, the callback.

**What compiles out under `NDEBUG`:** the token's operations and every assertion this section, §2.1's
step 4 and §2.1a name. The members exist in every build type, as `in_dispatch_` does today, so the class
has one definition.

**`dispatch_app_callback`.** The guard it carries gets the token's shape. No code in `include/`, `src/`
or `bindings/` calls that function (`grep -rn "dispatch_app_callback" include src bindings` finds its
definition and a comment), so no application callback runs under it, and the depth `close()` reads does
not have to count it. If a caller is added, it must go through `callback_dispatch_scope`.

### 2.3 `Engine::send` through the snapshot (R-2)

Today `Engine::send` enrols in `send_counter_`, re-checks `stopped_`, hops onto `control_strand_` to read
`stopped_` and `registry_`, then hops onto the session strand. Each hop is a `co_spawn`, which starts with
a dispatch: the first runs inline only when the control strand is idle, and that is the timing dependence.

`reader_snapshot_` already publishes `SessionId → shared_ptr<Session>` for every published session, rebuilt
on the control strand after each control-plane mutation and read by `Engine::lookup` without the strand.

**The new path.**

1. Enrol in `send_counter_` and construct the guard, in the outer frame, before any await. Unchanged.
2. Re-check `stopped_` with `seq_cst`; if set, return `session_invalid_state_for_send`. Unchanged.
3. Load `reader_snapshot_` and look the id up in it. The hit path reads the snapshot itself and does not
   call `Engine::lookup`. On a build with assertions `lookup` returns a handle backed by a lease that
   `~Engine` asserts is released, and that lease is not something `stop()` drains. A send frame destroyed
   after the Engine (an `io_context` destroyed later, with sends suspended) would trip that assertion,
   where today's copy from `registry_` does not. The rule has no witness, and the scenario is one
   `~Engine` already forbids (§3).
4. **Hit:** copy the snapshot's plain `shared_ptr<Session>` as the keepalive and `co_spawn` onto that
   session's executor. There: enable total cancellation as today, apply the stop check of OD-3, check
   `state() == Active`, then `co_await session->send(payload)`. The stop check reads the session's engine-stop flag through
   `session_engine_access`, which has the flag's setter today and needs a reader added.
5. **Miss:** take today's control-strand path unchanged. It returns `session_invalid_argument` for an
   unregistered id and `session_invalid_state_for_send` for a registered id with no published session, and
   it finds a session published after the snapshot this call loaded.

**Why step 4 is nested by construction.** A callback of session S runs on S's executor. `co_spawn` onto an
executor that is already running in the calling thread starts its body inline, on one io thread or on many.
From any other thread the same `co_spawn` queues behind S's current handler, which is the serialisation
`Engine::send` promises. Nothing in step 4 consults a second strand.

The claim has a condition on the caller, and the B&L row states it: `Engine::send` is a coroutine, and it
nests when the application starts it from the callback in a way that runs its body at once (a `co_spawn`
onto an executor already running in that thread, the session's own or the `io_context`'s). An application
that posts first, or starts it on a strand of its own, gets a send that begins after the callback. That
send is still ordered.

**Why a miss cannot happen where nesting matters.** A send is admitted only in Active. A session reaches
Active on a frame its read pump delivers, and both role loops publish the session, and republish the
snapshot, before the pump or the first-frame delivery starts. A callback that runs before publication
(`onCreate`, the initiator's Logon `toAdmin`) is before Active, so a send there is refused on either path.
This is OD-4 (§6). §3 pins it with a cell per role (NC-1), because it is a property of two role loops, not
of this function.

**Why the miss path stays.** The snapshot holds only published sessions and `register_session` does not
republish. Telling "registered, not yet published" from "unknown" out of the snapshot would need every
registered id in it and a republish per registration, which is a rebuild of the map per call. The miss path
is cold: a send to a session that is not established.

**The payload.** `Engine::send` copies the caller's span into its frame, so a caller's stack buffer may die
before the send completes, and the session-strand body captures its own copy by value. Both stay as they
are. The body must not hold a view into the outer frame: frames can be destroyed out of order when an
`io_context` is destroyed with sends suspended, which is the class PR #572's Gate B fixed at its miss-path
drains.

**Cost.** One snapshot load and one keepalive copy replace a hop through the engine-wide strand. On the
fallback `atomic_shared_ptr` the load takes a shard lock (`include/fixpp/core/sync/detail/atomic_shared_ptr.hpp`).
This note claims no speed-up. No bench covers `Engine::send` today (B&L `L-023-2`); the PR adds one, and
§3 ("Cost") has what it times and how the difference is measured and reported.

### 2.4 What `stop()` relied on, and what holds it after R-2

023's comments say `Engine::send`'s registry read and `stopped_` check are "serialized with stop()'s
mutations on the control strand". Each guarantee, and its carrier on the hit path:

| Guarantee | Carried today by | Carried on the hit path by |
|---|---|---|
| A send never reads `registry_` while `stop()` clears it | the control-strand hop | the hit path does not read `registry_`; the snapshot is replaced atomically, never mutated |
| A send posted but not yet run is visible to `stop()`'s drain (the Engine outlives it) | the enrolment in the outer frame, before any hop | the same enrolment; unchanged |
| A send that starts after the drain saw zero queues no work | the `seq_cst` re-check after enrolment | the same re-check; unchanged |
| A send suspended on the session strand never outlives the Engine (the Session holds a reference to `EngineConfig`) | the drain before `stop()`'s terminal closes and its clear | the same drain; the guard spans the whole send |
| The Session outlives the send | a `shared_ptr` copied from `registry_` on the control strand | a `shared_ptr` copied from the snapshot, which keeps every published session until `stop()` republishes it empty, after the drain |
| `fsm_state_` is read only on the session strand | the Active check in the session-strand body | the same check; unchanged |
| A send whose session-strand body would start after `stop()`'s step 1 ran on that strand fails | the second `stopped_` read, on the control strand, which refuses every send whose control-strand step follows `stop()`'s write of `stopped_` | OD-3: the engine-stop flag, which step 1 sets on the session's strand and the body reads there |

The first six do not depend on the hop. The seventh did, and v0.4 misdescribed it. Traced through
today's `Engine::send` and `Engine::stop` (read first; P-11 then measured the refusal on the base tree
and agrees, §3):

- `stop()`'s control-strand body writes `stopped_` first, after a cancellation reset that does not
  suspend, and only then runs step 1, which sets the engine-stop flag on each session's strand.
- Today's `Engine::send` reads `stopped_` on the control strand and, in the same handler, spawns its body
  onto the session's strand. Nothing lets `stop()`'s body onto the control strand between the two: the
  one `co_await` between them is `reset_cancellation_state`, which does not suspend, and a `co_spawn`
  begins by dispatching onto its target, so the body is running or queued on the session's strand before
  the control strand is released.
- So a send's control-strand step either precedes `stop()`'s body, and then its session-strand body is
  ahead of step 1 on that strand unless step 1 runs inline there (the end of this section); or it
  follows the write of `stopped_` and is refused, with `session_invalid_state_for_send`.

Today, then, with `stop()` started outside the session's callbacks, no send's session-strand body starts
after step 1 ran on that strand: the control-strand read has refused it first. OD-3 refuses exactly the
sends whose body starts after step 1, with the same error. So on those routes OD-3 refuses nothing that
today's code admits, and the hit path keeps the seventh guarantee by a different carrier.

What R-2 changes is the other direction. A send that passed the admission re-check before `stop()` wrote
`stopped_`, and whose session-strand body is queued ahead of step 1's handler, is admitted on the hit
path. Today that send is refused whenever its control-strand step runs after `stop()`'s body has begun.
So a narrow class of sends that race `stop()` is refused today and can be admitted afterwards. Such a
send runs during teardown as a send does today whose control-strand step preceded `stop()`: it is
enrolled, so `stop()`'s drain waits for it. The Active check does not refuse it. P-11's runs read the
session's FSM state as Active at every reading, from before `stop()` to the seam after `stop()`'s send
drain, which is past steps 1 to 3 (§3). So for a send that has passed its admission re-check, what
refuses it after step 1 on today's code is the control-strand read of `stopped_`, where P-11 observed
the refusal, and not the Active check; on the hit path OD-3's check has that place (the table's last
row). Which side of `stop()` a racing send falls on depends on timing today and still does; the window in
which it is admitted is wider. A caller can observe the widening: a send that races `stop()` may succeed where it
failed before. Under `[const §X.7]` a change is breaking when a call a consumer could already make fails,
or returns a result other than the one documented. Neither holds: the outcome of a send that races
`stop()` depended on timing, and no documentation promises that it fails (re-derive: read the comments on
`fixpp_session_send` in `include/fix/c_api/session.h` and on `fixpp_engine_destroy` in
`include/fix/c_api/engine.h`).

One route is an exception: `stop()` started inside a callback of the session, where step 1 runs ahead of
bodies already queued (§2.1a, R-5). It is the only route on which OD-3 refuses a send that is admitted
today, and the C ABI cannot take it.

`stop()`'s step order, the publish and unpublish sites, the snapshot's publisher and `Engine::lookup` are
not changed.

### 2.5 A session closed from inside one of its own callbacks

Measured on an `NDEBUG` build before this change, with no nested send involved: a terminal
`Session::close` started in `fromApp` or `fromAdmin` runs inline to `closed_drained`, and `onLogout` fires,
before the call returns. The inbound arm then resumes on the closed session: it reports success, or
`session_already_closed` where it goes on to emit. With a FileStore the close reports completion before the
inbound arm has finished. A nested send whose `toApp` throws reaches the same state, because that branch
awaits a terminal close.

**What R-4 changes about that state, and what it leaves.** v0.3 said that R-4 "removes that state at its
source". It does not, and the sentence is withdrawn together with R-4's consequence (§0). Under R-4:

- **Removed:** the close running inside the callback. The callback returns to a session that is as it was
  when the callback started the close, and `onLogout` no longer runs with the callback on the stack.
- **Left:** the arm resumed on a session that closed meanwhile, wherever the arm suspends after its
  callback (§2.1a). On a FileStore the close's completion still precedes `on_inbound_frame`'s, with
  the start posted (P-2, measured; §3 names the evidence), as it did for today's inline close.

What is left is the position of a close that arrives from another thread while an arm is in flight. The
session already has handling for that position: `close()` drains the seqnum manager and the write gate, a
live write that finds the gate drained returns `dispatch_aborted`, and the Logon arms test
`logon_arm_superseded_` after each suspension. R-4 adds no handling for it and claims none. The close
cells of §3 assert what the arm does there, per store kind and per transport, so that the position is
witnessed and not assumed.

**Not a memory-safety defect, as far as reading goes.** Nothing that owns a Session can free it while one
of its inbound arms is on the stack:
- under the Engine, each role loop holds its own `shared_ptr` to the Session (`local_session` in
  `run_accept_loop` and `run_connect_loop`) across `run_read_pump`, and the arm runs inside the pump;
  `stop()` joins the role loops before its terminal closes and before it clears the registry;
- the C ABI's engine shell keeps its sessions for its own lifetime;
- a caller that drives a raw `Session` owns that obligation already (B&L `L-015-4`, `L-544-2`).

So a sanitizer cell would come back clean by construction and decides nothing about lifetime. The
sanitizer runs of §3 stay (R-3), as a net under paths that assertion builds could not reach before §2.2.

**What R-4 does not cover.** A nested send can leave the session `Disconnected` without calling `close()`,
because `Session::send` writes that state inline on some results of its body. Started in an inbound
callback or in `onLogon`, each of the following happens inside the callback: `onLogout` runs there, and
the arm resumes on a disconnected session.

- `assign_outbound()` reports the sequence number overflow. It needs a MsgSeqNum at its maximum.
- On a session with no store, a config-time `transport_send` throws. `store_then_emit` returns
  `dispatch_aborted` without having suspended.
- On a persistent store whose `store` completes without yielding, the store call fails. `send_impl` sets
  `disconnect_required` without having suspended. The shipped stores yield first, so with them the write
  comes after the callback has returned.
- The nested send's own `toApp` returns `dispatch_aborted`. `Session::send` treats that value as a
  converted cancellation and writes `Disconnected`; a comment at that test records it as a known
  residual.
- A cancellation is recorded on the nested send's chain before one of its awaits begins, for instance
  emitted from its own `toApp` on a slot the application bound to the send. That await throws, and
  `Session::send`'s `catch` writes `Disconnected`.

Each is read, not run. They are disclosed (§4), not changed. Re-derive the list by reading
`Session::send` from its `try` to its end: every `record_state_transition_(fsm_state::Disconnected)`
there is one of these routes when the code before it has not suspended.

## 3. Tests and gates

Every cell runs alone (`--gtest_filter`), on a build with assertions and on a build with `NDEBUG`, with
the exceptions the cells' rows name. `linux-clang-debug` and `linux-clang-release` are the presets this
section names for the two, and where a row below names one of them the condition it means is that one:
assertions, or `NDEBUG`. Every cell also passes in a whole-binary run, which is how ctest runs it. A death
arm exists only on a build with assertions, because the assertion it dies at compiles out under `NDEBUG`;
it skips on a build with `NDEBUG`, as the I-3 test does today. NC-16's third arm exists only on a build
with `NDEBUG`. "Classification" is the cells' own
three-way witness: nested, deferred, never ran. A cell that expects one classification fails on either of
the other two.

**Mutant first.** Every cell or arm this revision adds or changes was specified from its mutant: the row
in "Arms that must go RED" states what the wrong implementation does in the cell, and the cell's assertion
is the one that outcome fails. The exceptions are these: controls (NC-9 (c),
NC-16's fourth arm, NC-4's second and fourth arms, NC-7's `total` arm, NC-11's positive-control arm);
arms that pin a disclosed
limitation or consequence as measured (NC-5 (f), NC-12, NC-14); and pins of a property this PR does not
introduce (NC-1, NC-2's `onLogon` arm, NC-18, and NC-17's assertion on the outer frame's bytes, which is
B35's property; NC-17's order assertion is in the first mutant row). Where a mechanism
has no cell that can fail, none is claimed (the list after the mutant table).

**Fixture additions the cells need.** At the commit Gate A reviewed, the fixture of
`test_563_564_callback_send.cpp` left the threading mode at its default and set the heartbeat interval to
zero. A tests-only commit made with the measurements of 2026-10-11, `be46718a`, adds part of what
follows; each addition has a default that leaves every existing cell as it was, and the commit's message
lists them. "On the branch" marks an addition that commit made. The new cells need:

- a threading-mode knob: `direct_executor`, attested, over the bare `io_context` executor (NC-16, NC-20).
  On the branch;
- a heartbeat-interval knob (NC-20, and NC-11's `onLogon` arm on a persistent store). On the branch;
- an `onCreate` hook on the cell application, which had hooks for `onLogon` and `onLogout` and none for
  `onCreate` (NC-11). On the branch;
- a peer-frame builder that takes the BeginString; `peer_frame` writes the session's own (NC-19's
  lifecycle arm, and NC-11's `onLogout` sub-arm that a `Disconnected` write outside `close()` fires). On
  the branch;
- test access to `callback_dispatch_scope`, so that a cell can enter and leave a scope without a session
  doing it (NC-9). It is added to `session_test_access` in `tests/support/session_test_access.hpp`, the
  one file in which that struct is defined (`brain/components/session.md`, "Test access to private
  `Session` state"). Not on the branch;
- the store double of NC-14. It is local to `tests/session/test_563_564_callback_send.cpp`, the file of
  the cell that uses it. `tests/support/hooked_store.hpp` is read first, and its `HookedStore` is used
  instead if it can hold one `store()` call and complete a later one first. Not on the branch.

Also on the branch from that commit, for arms this section already specifies: a role knob (NC-11's
"both roles"); a delivery of several frames by one coroutine with nothing between two frames (NC-12);
the transport kind as an argument of the helper that the inbound close cells share ("The arms every
close cell has"); the value `toApp` returns, set by the cell; and, on the live transport double, a count
of `close()` calls and of the writes begun after the first one, with a setting that makes such a write
fail as a write on a closed socket does. The setting is off by default.

Two facts about the fixture were found while measuring, and each binds the cells below (the record,
"Fixture facts found while measuring"):

- **A default-mode cell with a non-zero heartbeat interval feeds the peer's Logon on the session's own
  executor.** The fixture's `open_to_active()` feeds it on the fixture's executor, and the Logon arm
  spawns the liveness loop on the executor the arm runs on. In the default mode over the bare
  `io_context` the loop then runs off the session's strand. With a zero interval the loop returns at
  once and nothing shows. Measured in the early reading of P-10 (`p0b_runs.output.txt`, under its heading):
  with the Logon fed by `open_to_active()`, the loop's emit ran inside a `toApp` that polls; with the
  Logon fed on the session's executor, it did not. NC-11's `onLogon` arm on a persistent store and that
  arm's control are such cells. No helper for this feed is on the branch.
- **A live-transport cell whose subject is a write that can follow a `close()` uses the double that
  refuses a write after `close()`.** With the setting off, the double's `close()` changes nothing that
  a later write sees, and such a write counts as transmitted (P-1a). The third arm of
  `ToAppStartsClose_Terminal` is such a cell.

One addition is to another file's fixture: `ValidateGateFixture`
(`tests/session/test_validate_gate_inbound.cpp`) has no `Application` today, and NC-19's validate arm needs
one attached. The arm is written in that file, where the fixture is defined.

Death arms use gtest's death tests, which the session tests already use. Where an arm starts threads, it
starts them inside the death statement, and the TSan arm of NC-9 runs in the `threadsafe` death-test style
(`tests/dictionary/orchestra_loader_test.cpp` sets it the same way), so that the child is a fresh process.

**The arms every close cell has.** Round 1 found that the close cells on the branch cannot tell a correct
R-4 from a wrong one. The inbound ones record and print, and assert that the callback ran and that
`on_inbound_frame` completed (`run_callback_starts_close` says of itself "little is asserted"). The
outbound ones pass on a property of the fixture: its config-time sink transmits whatever state the session
is in. So every close cell, existing or new, runs in these arms unless its row fixes the transport or
the store. The close cells are, on the branch, `ToAppStartsClose_*`, `ToAdminStartsClose_*`,
`FromAppStartsClose_*`, `FromAdminStartsClose_Terminal` and `FromAppSendsReply_NestedToAppThrows*`, and,
among the new cells, NC-5, NC-6, NC-7, NC-8, NC-11, NC-12 and NC-13. NC-16's second arm starts a close
too, but it is a death arm and asserts only its death. The arms:

- **Transport:** the synchronous sink (`TransportKind::sync_capture`) and the live transport whose write
  suspends (`TransportKind::live_suspending_write`, the fixture's `SuspendingWriteTransport`).
- **Store:** `MemoryStore` and `FileStore`, and no store where the cell's subject allows it.

And in every arm each of these is an assertion, not a printed line:

- inside the callback, after the start: the session's FSM state and lifecycle, and that neither `onLogout`
  nor a Logout's `toAdmin` has run;
- the order of: the callback's return, `onLogout`, each transmit, the completion of the close, and the
  completion of the operation that called the callback;
- the session's FSM state at each transmit;
- what was transmitted, what the store retained, and the result of each operation.

The fixture sets `heartbeat_interval` to zero, so its liveness loop returns at once and a terminal close in
these cells has no suspension of its own; NC-11's `onLogon` arm on a persistent store sets the interval
through the knob, for the reason its row gives. Under the Engine it has several. NC-13 is the cell where the
transport and the liveness loop are real.

**Predictions to measure first.** Each row is an outcome this note states from reading. What has been
measured was measured on 2026-10-11, or on 2026-10-12 where a bullet says so, and the evidence is in the
parent repo's `research/G19-fix-fpml-iso20022/research/probes/b41_563_564/`:

- **P-4 and P-11.** Both agree. Their rows name their evidence files.
- **P-1 for a close started in `toApp`, P-1a, P-1b, P-1c, P-2, P-3, P-5 and P-6,** on today's code, each
  with its start wrapped in `asio::post`. None disagrees. The temporary cells are
  `p0b_measurements.patch`, which applies on the fixture commit `be46718a`. Every run is in
  `p0b_runs.output.txt`, under a heading that names the prediction. The cells ran on `linux-clang-release`
  and again on `linux-clang-asan`. Each of these rows has a "Measured 2026-10-11" statement in its third
  column, and those files are its evidence.
- **P-10, an early reading,** with a plain post in place of each continuation. It agrees, and its
  row says what it covered. The row's own measurement, on the mechanism, is in the three bullets below.
- **P-7, and P-10 at the guard** (§2.2), on a scratch build of the guard. Both agree. The mechanism and
  the cells are `p2a_scratch_mechanism.patch`; the edit that makes the token a plain member is
  `p2a_plain_token_mutant.patch`. Every run is in `p2a_runs.output.txt`, under a heading that names the
  prediction. P-7 ran on `linux-clang-tsan`, P-10 on `linux-clang-asan`.
- **P-10 at the send's deferred path,** on 2026-10-12, on a scratch build of that path. It agrees. The
  mechanism is `p3a_scratch_mechanism.patch` and the cells are `p3a_measurement.patch`. Every run is in
  `p3a_runs.output.txt`. The cells ran on `linux-clang-asan` and on `linux-clang-release`.
- **P-1 for a close started in `toAdmin`, P-8, P-9, and P-10 at the close's wait,** on 2026-10-12, on a
  scratch build of the wait and of the deferred send's step 5. None disagrees. The mechanism is
  `ph4a_scratch_wait_v2.patch` and `ph4a_scratch_step5_v2.patch` (the record says how they differ from
  the two patches without `_v2`), and the cells are `ph4a_measurement.patch`. Every run is in
  `ph4a_runs.output.txt`, under a heading that names the prediction. The cells ran on
  `linux-clang-release` and on `linux-clang-asan`.

Every row of the table has a measurement, and each row's third column has a "Measured" statement with
its date and its evidence file. The measurements of the last three bullets ran in a scratch copy of the
tree, and not in the pull request's.

A row measured with the start posted keeps its "If it disagrees" column. The post stands in for the
mechanism, and the same outcome is read again on the mechanism itself when that mechanism is
implemented, through the cells' own assertions. The column applies to that reading as well.

| Id | Predicted (read; not run unless the row says "Measured") | Measurement | If it disagrees |
|---|---|---|---|
| P-1 | A terminal close started in `toApp`, with the start posted. On the synchronous sink: the outer frame is transmitted after `onLogout`, with the session `Disconnected` at the transmit; it is retained; its send returns ok. On the live transport: the outer frame is retained under its number and not transmitted; its send returns `dispatch_aborted`. | `CallbackSend.ToAppStartsClose_Terminal` with the close's `co_spawn` wrapped in `asio::post`; read the session state at the transmit event. Then the same with `TransportKind::live_suspending_write`. For NC-6, the same measurement with the close started in `toAdmin`, at a callback-first site and at an assign-first one. **Measured 2026-10-11, for a close started in `toApp`: agrees** (`p0b_runs.output.txt`, heading "T0.5 (P-1)"; cells in `p0b_measurements.patch`). Synchronous sink: the outer frame is transmitted after `onLogout`, with the session `Disconnected`; it is retained; its send returns ok. Live transport: nothing is transmitted, the frame is retained under its number, and its send returns `dispatch_aborted`. Both hold on a `MemoryStore` and on a `FileStore`. Read at the store, through a recording wrapper over the `MemoryStore`: the outer frame's store call begins before the posted close runs and completes after it. **Measured 2026-10-12, for a close started in `toAdmin`: agrees** (`ph4a_runs.output.txt`, heading "T4.3a (P-1 for a terminal close started in toAdmin)"; cells in `ph4a_measurement.patch`). It was made on a scratch build of the wait, at a callback-first site (the Heartbeat reply) and at an assign-first one (the Reject), on a `MemoryStore` and on a `FileStore`. Synchronous sink: the outer admin frame is transmitted after `onLogout`, with the session `Disconnected` at the transmit; it is retained. Live transport, with the transport double's refusal of a write after `close()` off and on: the frame is retained under its number and not transmitted. Two readings this row did not predict. The result of the `on_inbound_frame` that called `toAdmin`: ok at both sites on the synchronous sink; on the live transport, `dispatch_aborted` at the callback-first site and ok at the assign-first one. Store against manager: the outbound numbers agree in every arm; on a `FileStore` the inbound numbers agree, except at the callback-first site on the live transport, where the store is one behind. On a `MemoryStore` the store's inbound reading does not move, so it is no check there. The `toApp` half and P-1a were read again on the mechanism in the same runs, and came out as they were measured with the start posted. | The row for that cell, §2.1a's "An outbound emit can be cut" and the B&L limitation are rewritten to what was measured, and the difference is reported to the owner before the cell is written, because R-4's reading was chosen on this cost. |
| P-1a | The same close with `reset_on_disconnect` set. The close's teardown reset and the outer frame's store call are then both in flight. Their order was not read to a conclusion. | The live-transport arm of P-1 with the knob set; read what the store holds and what the send returns. **Measured 2026-10-11** (`p0b_runs.output.txt`, heading "T0.6 (P-1a)"; cells in `p0b_measurements.patch`). The close is suspended in its teardown reset before it has drained the write gate, so the outer frame's write passes the gate and reaches a transport whose `close()` has already been called. In the arm whose transport double refuses a write after `close()`: nothing is transmitted, and the send returns `dispatch_aborted`; read at the end, the store holds no frame and both next numbers are at their initial value, in the manager and in the store, on a `MemoryStore` and on a `FileStore`. In the arm whose double's `close()` does nothing, the frame is reported as transmitted and the send returns ok. That is an artifact of the double, and no assertion is written from it. | Nothing to disagree with: the arm's assertions are written from the measurement, and the B&L limitation states the outcome. |
| P-1b | A graceful close started in `toApp` or `toAdmin`, on the live transport: the outer frame, then the Logout; both retained; both operations ok. (On the synchronous sink this was measured with the start posted.) | The existing graceful cells with the start posted and the live transport attached. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.7 (P-1b)"; cells in `p0b_measurements.patch`), for a close started in `toApp` and in `toAdmin`, at a callback-first site and at an assign-first one: the outer frame, then the Logout, both retained, both operations ok. On a `MemoryStore` the Logout's `toAdmin` runs before the outer frame is transmitted, while the outer emit is suspended in its store call; the wire order is still the outer frame, then the Logout. | As P-1. |
| P-1c | A terminal close started in `fromAdmin` on a TestRequest. The Heartbeat reply's `toAdmin` runs before the close, on an Active session, with either store kind. On the synchronous sink the Heartbeat is transmitted after `onLogout`; on the live transport it is retained and not transmitted. In the live arm `on_inbound_frame` returns `dispatch_aborted`, and on a persistent store the inbound number is then not persisted; reading had not settled this, and it is stated from the measurement in the next column. | `FromAdminStartsClose_Terminal` with the start posted, in both transport arms. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.8 (P-1c)"; cells in `p0b_measurements.patch`). The Heartbeat's `toAdmin` runs before the close, on an Active session, on a `MemoryStore` and on a `FileStore`. Live arm: nothing is transmitted, the Heartbeat is retained, and `on_inbound_frame` returns `dispatch_aborted`; on a `FileStore` the manager's next inbound number has advanced and the store's has not. | As P-1; `on_inbound_frame`'s result is asserted as measured. |
| P-2 | A terminal close started in `fromApp` on a FileStore: the close's completion still precedes `on_inbound_frame`'s; `on_inbound_frame` returns ok; the store has recorded the inbound number. | `FromAppStartsClose_Terminal_FileStore` with the start posted. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.9 (P-2)"; cells in `p0b_measurements.patch`): the close completes before `on_inbound_frame` does, `on_inbound_frame` returns ok, and the store has recorded the inbound number. | §2.5's "Left" bullet and the cell's order assertion are rewritten to what was measured. |
| P-3 | Two application frames delivered with no yield between them, as `run_read_pump` delivers the frames of one read; a terminal close started in the first `fromApp`; `MemoryStore`. The second `fromApp` runs before `onLogout`. | NC-12's setup with the start posted. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.10 (P-3)"; cells in `p0b_measurements.patch`): on a `MemoryStore` the second `fromApp` runs before `onLogout`. The same setup on a `FileStore`, which this row did not predict: the second `fromApp` does not run. The arm suspends in its persist, the close runs there, and the second frame's `on_inbound_frame` returns ok on the closed session. So the bullet and its B&L sentence hold on a volatile store only, and both are scoped to that. | If the second `fromApp` does not run, the "close can be late" bullet of §2.1a and its B&L sentence are deleted, and NC-12 asserts the measured order. |
| P-4 | asio's sequence at a post. A coroutine awaits `asio::post(ex, asio::deferred)`; a terminal cancellation is emitted on its slot while it waits. The exception surfaces at the coroutine's next await, not at the post. In a second arm the coroutine resets its cancellation state to `asio::disable_cancellation{}` before the post, as `close()` does; a terminal cancellation emitted while it waits surfaces at no later await. In a third arm the reset follows the post; no exception surfaces there either (the placement §3 lists as not witnessed). | A standalone probe beside p1 in the parent repo's `research/probes/b41_563_564/`, built against the pinned asio. **Measured 2026-10-11: agrees.** The probe is `p4_post_cancellation.cpp`, built by `build_p4.sh`; its output is `p4_post_cancellation.output.txt`. First arm (the probe's arms A, whose second await is a post, a timer wait or a nested awaitable): no exception at the post, the record reads `terminal` after it, and the second await throws `operation_aborted`. Second arm (the probe's arm S1): nothing is recorded and no await throws. Third arm (the probe's arms B, a reset to the default filter and a reset to total): the reset clears the record and no await throws. The positive control (a cancellation recorded before the first post throws at that post) and the null control (no emission: nothing recorded, nothing thrown) pass. The probe's other arms (C, D, E, S2, S3, S4) measured §2.1's statements about the filter, about `throw_if_cancelled`, about a chain that awaits a spawned chain, and about a recorded cancellation that a later emission erases; each agrees. | §2.1's "Cancellation at the hop" and §2.1a's "Cancellation and the wait" are wrong. The note returns to Gate A before either wait is implemented. |
| P-5 | The lifecycle dispositions of §2.1a: a close started in `onLogon`, in `onLogout` and in `onCreate`. | NC-11's setups with the start posted. **Measured 2026-10-11: agrees in every arm, and every arm ends closed** (`p0b_runs.output.txt`, heading "T0.11 (P-5)"; cells in `p0b_measurements.patch`). The routes that were run: `onLogon`, in both roles, both close modes and both store kinds; `onLogout` fired by a graceful close, with a terminal close started in it; `onLogout` fired by a `Disconnected` write of `Session::send`, whose `toApp` returned `dispatch_aborted`, with a close of each mode started in it; `onCreate`, in both close modes. The route NC-11 names for the `Disconnected` write outside `close()`, a frame with another BeginString, is not the one that was run; it is measured when the cell is written. On a raw initiator the Logon is transmitted before `onCreate` runs, so a close started there ends the session after a Logon with no Logout. | §2.1a's bullet for that callback and NC-11's assertions are rewritten to what was measured. A disposition that leaves a session open after a close was asked for is reported to the owner. |
| P-6 | `B35SendSlot.Nested_ASendInsideToAppLeavesTheOuterFrameIntact` after R-1: the second send is deferred, starts while the outer send is suspended in its `MemoryStore` call, and takes `send_fallback_leaf_`. | That cell with its nested `co_spawn` wrapped in `asio::post`. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.12 (P-6)"; cell in `p0b_measurements.patch`): the second send is deferred, starts while the outer send is suspended in its store call, and takes the fallback leaf's buffer. | NC-17's assertions follow the measurement; the cell's subject (the outer frame's bytes are what `toApp` saw) does not depend on which buffer the second send takes. |
| P-7 | NC-9's arm (d) under TSan. With the atomic token: the second thread's entry dies at the token's assertion and no ThreadSanitizer report is printed. With the token built as a plain member: ThreadSanitizer prints a data-race report on the token before the assertion fires. The second half rests on ThreadSanitizer's happens-before model as known, not on a run: a relaxed atomic load and store create no ordering between the two threads, where a mutex, a condition variable or a non-relaxed atomic would. | Arm (d) on `linux-clang-tsan`, once on the mechanism and once on a scratch copy with the token made plain. **Measured 2026-10-11: agrees, in both halves** (`p2a_runs.output.txt`, headings "T2.2 (P-7), first half" and "T2.2 (P-7), second half"; mechanism and cells in `p2a_scratch_mechanism.patch`, the plain token in `p2a_plain_token_mutant.patch`). It was made on a scratch build of §2.2's guard. With the atomic token the second thread's entry dies at the token's assertion and no ThreadSanitizer report is printed. With the token built as a plain member ThreadSanitizer prints a data-race report whose frames are the token's compare-and-swap. Each half was run with no `TSAN_OPTIONS` and with the `ctest` environment. On the plain token under `halt_on_error=1` the report ends the process before the assertion, so arm (d) fails there by both clauses of its matcher; with no options it fails by the second clause. | If the plain token draws no report, arm (d) cannot see the difference: it is deleted, its mutant row moves to the list of mutants with no witness, and the token's atomicity is recorded in the verification record as a property carried by reading the diff. If the atomic token draws a report, the two threads share more than the token, or a foreign thread touches a plain member before it asserts; that is a defect in §2.2's design and is fixed there. |
| P-8 | NC-8. Arm 1: a terminal close on the cell fixture, with no send in flight and `reset_on_disconnect` off, completes without suspending, so it has reached `closed_drained` before the waiting close resumes. Read through the places it could suspend: the default `close_async` returns `close()`'s result; both drains post only while the mutex has a holder, a waiter or a resumption in flight; the liveness wait posts only while the loop is alive, and with a zero heartbeat interval it is not. Arm 2: a graceful close that finds another graceful close in flight joins it, and the scripted flush hook has run once when both complete. | The two arms on a scratch build of the wait. **Measured 2026-10-12: agrees, in both arms** (`ph4a_runs.output.txt`, heading "T4.1 (P-8)"; cells in `ph4a_measurement.patch`). Arm 1: the lifecycle reads `closed_drained` at the return of the `co_spawn` that starts the other close, and at the waiting close's resumption; the callback's close returns `session_already_closed`; `onLogout` runs once. The observable is the lifecycle at that return, and not whether the other close's completion handler has run by the resumption: `co_spawn` posts the completion handler whatever the close did, so its position reads asio and not the close. The cell's first clause read the handler, which had not run, and printed a disagreement; the clause was changed to read the lifecycle, with the mechanism and the prediction unchanged, and the first run's lines are kept in the evidence file under the same heading. Arm 2: the lifecycle reads `closing` at the resumption; both closes return ok; the flush hook ran once; one Logout is transmitted. | Arm 1: if the first close does suspend, the waiting close joins it; the arm's assertion is rewritten to arm 2's oracle, and the mutant row names arm 2 alone. Arm 2: the assertion follows the measurement; if a second run of the body cannot be told from a join there either, the mutant has no witness and is listed as such. |
| P-9 | NC-5's two arms added at round 2. (e) A graceful `close()` and then a send started in one `fromApp`, `MemoryStore`: the send nests, its frame is transmitted, then the Logout, both retained, both ok. (f) A logout the peer starts; a send started in the confirming Logout's `toAdmin`: it is deferred, passes step 5 and the Active check, and is numbered and transmitted after our Logout on the synchronous sink. What that send returns was not read to a conclusion. | The two arms on a scratch build of R-1 and R-4. **Measured 2026-10-12: agrees, in (e) and in (f)** (`ph4a_runs.output.txt`, heading "T4.2 (P-9)"; cells in `ph4a_measurement.patch`). (e): the send's `toApp` runs inside `fromApp`; on the wire the send's frame, then the Logout; both retained; both ok. (f): the send is deferred; on the wire our Logout, then the send's frame; both retained. What the send of (f) returns, which this row did not predict: ok, on a `MemoryStore` and on a `FileStore`. On a `MemoryStore` its frame goes out after `onLogout`, with the session `Disconnected`; on a `FileStore` it goes out before `onLogout`, with the session still Active. | (e): the arm asserts the measured order, and §2.1a's "the close never runs inside the callback" is re-read against it. (f): §7's bullet and the B&L sentence are rewritten to what was measured; if the send is refused, the bullet is deleted. |
| P-10 | The setups of NC-16, NC-19 and NC-20. On the `direct_executor` fixture a callback's `poll` runs the deferred send's continuation (first arm), the waiting close's (second arm) and, after the callback has advanced the mock clock by the heartbeat interval, the liveness loop's emit (NC-20), each inside the callback. On a session in the default mode that the fixture drives from a strand of its own (`ExecKind::strand`), over which the session's strand is a second one, the same callback's `poll` does not run the continuation (NC-16's fourth arm). A frame fed from `onLogon` with the expected inbound number reaches the inbound overload (NC-19's `onLogon` arm). A frame with another BeginString, fed from `toApp` on an Active session, reaches the `onLogout` site with no parse over the buffer and no emit before it (NC-19's lifecycle arm). | Those arms on a scratch build, with a recorded line at the point each continuation runs. **Early reading, 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.13 (early reading of P-10)"; cells in `p0b_measurements.patch`). It was taken on today's code, with a plain `asio::post` onto the session's executor in place of each continuation. On the `direct_executor` fixture a callback's `poll` runs the posted handler inside the callback. In the default mode the handler runs after the callback returns, while a handler posted onto the bare `io_context` runs inside it; that last arm is the positive control. The row's own measurement was then made on the mechanism, in three places, each on a scratch build. **Measured 2026-10-11, at the guard: agrees** (`p2a_runs.output.txt`, heading "T2.3 (P-10, the halves of Phase 2)"; mechanism and cells in `p2a_scratch_mechanism.patch`), on `linux-clang-asan`. NC-20: on the `direct_executor` fixture, after `toApp` advances the mock clock by the heartbeat interval and polls, the liveness loop's `toAdmin` enters with `toApp` on the stack, and the process dies at the stack-arena overload's assertion; in the control, on the default strand fixture, the liveness `toAdmin` runs after `toApp` has returned. NC-19's `onLogon` arm: the fed frame reaches the inbound overload and dies at the parse-site assertion, in both roles. NC-19's lifecycle arm: the frame dies at the `onLogout` site's assertion, with no `fromApp`, `toAdmin` or transmit line between the feed and the abort. **Measured 2026-10-12, at the send's deferred path: agrees** (`p3a_runs.output.txt`, heading "T3.2 (P-10, the Phase 3 halves)"; mechanism in `p3a_scratch_mechanism.patch`, cells in `p3a_measurement.patch`), on `linux-clang-asan` and on `linux-clang-release`. On the `direct_executor` fixture the callback's `poll` runs the deferred send's continuation inside the callback: on the build with assertions the process dies at the deferred path's assertion, and on the build without them the send nests. In the default mode over a fixture strand the continuation runs after the callback has returned, whether the outer send was started on the session's executor or on the fixture's strand. A handler posted onto the bare `io_context` ran inside the callback in every run; it is the positive control of the poll. **Measured 2026-10-12, at the close's wait: agrees** (`ph4a_runs.output.txt`, heading "T4.3 (P-10, this phase's half)"; cells in `ph4a_measurement.patch`), on the same two presets. On the `direct_executor` fixture the callback's `poll` runs the waiting close's continuation with `fromApp` on the stack: on the build with assertions the process dies at the close's assertion, and on the build without them the close runs to `closed_drained` and `onLogout` fires inside `fromApp`. | If the continuation does not run inside the callback on the `direct_executor` fixture, the fixture is not the exposed setup and the arms are rebuilt before any death is trusted. If it does run inside the callback on the strand fixture, a strand did not hold back a post made under one of its handlers, the property §2.1's statement also rests on; R-7 was ruled on that statement, so the note returns to the owner. If the lifecycle arm's frame aborts or is disposed of before the `onLogout` site, the arm is rebuilt on a frame that reaches it; if none does, the lifecycle sites' assertion joins the list of mechanisms without a witness. |
| P-11 | OD-6's withdrawal (§2.4). On the base tree, one `Engine::send` parked between its admission re-check and its control-strand `co_spawn` while `stop()` runs its step 1 on the session's strand, then released: it returns `session_invalid_state_for_send`, and nothing of it is transmitted. | A hook at that point in a scratch copy of the base tree (`22702dc9`); the hook is not part of the PR. One send is parked there, `stop()` is run until the engine-stop flag is set on the session, and the send is released. **Measured 2026-10-11: agrees.** The hook and its cells are `p11_hook.patch`, applied to the tree at `53b365c4`, whose `src/` and `include/` are the base tree's (re-derive: `git diff --stat 22702dc9 53b365c4 -- src include`); the runs are `p11_runs.output.txt`. A send released after `stop()`'s step 1 had set the flag on its session returned `session_invalid_state_for_send`: its `toApp` did not run, the outbound number did not move, the store retained nothing of it and the peer received nothing. A send released when `stop()` had reached its send drain had the same outcome. Refusal site, in both: the control-strand read of `stopped_`. Control: a send released before `stop()` was started was sent, retained and received. The session's FSM state read Active at every reading, the one at the seam after `stop()`'s send drain included (§2.4). | If the send is transmitted, §2.4's reading is wrong and a send that succeeds today would be refused by OD-3. The C-ABI question returns to the owner before the PR proceeds. |

**Cells that must turn** (they exist on the branch). The transport and store arms above apply to every
row whose cells start a close, unless the row fixes the transport or the store.

| Cells | Must assert |
|---|---|
| `CallbackSend.ToAppSendsNested*`, `ToAdminStartsSend`, `ToAdminStartsSend_AssignFirstSite`, `…_FileStore`; `EngineCallbackSend.EngineSendFromToApp*` | deferred; distinct consecutive numbers in transmit order; each frame retained under its own number; store and manager agree; both outcomes ok |
| `CallbackSend.FromAppSendsReply`; `EngineCallbackSend.EngineSendFromFromApp*` | nested; pass on the debug build too |
| `EngineCallbackSend.MtEngineSendFromFromApp_Quiet`, `…_BusyControlStrand` | every reply nested, in both arms. **On a build with assertions, a run of `…_BusyControlStrand` counts only with its tally.** Measured 2026-10-11 on today's code (`p0b_runs.output.txt`, the runs of this cell on `linux-clang-asan`): the cell passed in some runs, each with every reply deferred, and aborted at the guard's assertion in the others, before it printed its tally; that assertion fires when a reply's `toApp` enters inside `fromApp`. So a pass there is not evidence that any reply nested. The cell prints, for the replies it drove, how many were nested and how many were deferred. A run counts as GREEN only when that line shows every driven reply nested and none deferred, and the line is recorded with the result. A run counts as RED, for the state before R-2 and for the mutant "the hit path replaced by the control-strand hop", only when the line shows a deferred reply. A run that aborts before it prints the line counts for neither and is run again |
| the `…_NoStore` and `…_NoStore_LiveWrite` nested cells | deferred; distinct consecutive numbers in transmit order |
| `CallbackSend.ToAppStartsClose_Graceful`, `ToAppStartsClose_Graceful_FileStore`, `ToAdminStartsClose_Graceful`, `ToAdminStartsClose_Graceful_AssignFirstSite`, `FromAppStartsClose_Graceful` | inside the callback after the start: Active, lifecycle open, the Logout's `toAdmin` not run. After it: the outer frame first where there is one, then the Logout; distinct consecutive numbers; both retained where there is a store; both operations ok. Live-transport arm: P-1b. Pass on the debug build too |
| `CallbackSend.FromAppStartsClose_Terminal`, `FromAppStartsClose_Terminal_FileStore` | inside `fromApp` after the start: Active, lifecycle open, `onLogout` not run. `onLogout` after `fromApp` returned. The session `closed_drained` at the end and the close's result ok. `MemoryStore`: `on_inbound_frame` completes before `onLogout`. FileStore: the order and results of P-2. Pass on the debug build too |
| `CallbackSend.FromAdminStartsClose_Terminal` | inside `fromAdmin` after the start: as the row above. The Heartbeat reply's `toAdmin` runs on an Active session, on either store kind: the TestRequest sub-arm emits its reply before it persists the inbound advance, so nothing suspends between `fromAdmin`'s return and that `toAdmin`. Then per transport arm: P-1c |
| `CallbackSend.ToAppStartsClose_Terminal` | inside `toApp` after the start: Active, lifecycle open, `onLogout` not run. Then per transport arm: P-1. The synchronous-sink arm asserts the FSM state at the transmit, so that it cannot pass for a reason that does not hold on a live transport. A third arm, the live transport with `reset_on_disconnect` set, asserts what P-1a measured; it uses the transport double that refuses a write after `close()` ("Fixture additions the cells need") |
| `CallbackSend.FromAppSendsReply_NestedToAppThrows`, `…_FileStore`, `…_FromAppRejects` | inside `fromApp` after the start: Active, lifecycle open, the send not completed. `onLogout` after `fromApp` returned. The send returns `app_callback_threw`. The session `closed_drained` at the end. `…_FromAppRejects`: the BusinessMessageReject's callback runs on an Active session, before the close, on either store kind (that sub-arm also emits before it persists); whether its frame is transmitted follows P-1's two arms |
| every control (`Sequential*`, `OverlappingSends*`, `TestRequestThenSend`, `ToAdminStartsSend_AssignFirstSite_Sequential`) | unchanged |

The cells' names and their "expected classification" asserts are re-decided when they are lifted: a cell
named `…Nested` that must report "deferred" is renamed.

**New cells.** The ids are for this note's tables; the cells get their names when they are written.

| Id | Drives | Asserts |
|---|---|---|
| NC-1 | `Engine::send` from `onLogon`, initiator and acceptor | nested, and the send succeeds. Pins §2.3's premise that a session is published before it can be Active |
| NC-2 | `Session::send` from `onLogon` and from `fromAdmin`. The admin message of the `fromAdmin` arm is the implementer's choice; what constrains it is that the session is still Active when its `fromAdmin` runs and stays so, as with the Heartbeat of NC-19's validate arm or the TestRequest of `FromAdminStartsClose_Terminal` | nested |
| NC-3 | a send to session B from a callback of session A, A's `toApp` included, on one io thread with B's strand idle. **Route:** `Engine::send` for B, or a `co_spawn` of B's `send` onto B's executor. Which of the two is the implementer's choice; what constrains it is the cell's fixture (a fixture of raw sessions has no Engine) and that B's send runs on B's executor. Not a direct `co_await` of B's `send` from A's strand, which would run B's send outside B's serialisation domain | B's send is not deferred: its `toApp` runs before A's callback returns |
| NC-4 | a deferred send cancelled while it waits. **Route:** `Session::send` started in `toApp` by a `co_spawn` onto the session's executor whose completion token is bound to a cancellation slot the cell owns (`asio::bind_cancellation_slot`). A detached spawn has no slot, and the cell would measure its own setup. **Emission:** `terminal`, which a plain `co_spawn` chain records, emitted from inside `toApp` after the `co_spawn` has returned, so the send is waiting on its post. Second arm: a `total` emission on the same chain. Third arm: the route through `Engine::send`, started the same way, with a `terminal` emission. Fourth arm: that route with a `total` emission. Fifth arm: the first arm's route with the send awaited from a wrapper coroutine that first turns `throw_if_cancelled` off, the slot bound to the wrapper's `co_spawn`, and a `terminal` emission | first and third arms: the send returns the OD-1 error, the session stays Active, nothing of it is transmitted, no number is consumed. Second and fourth arms: the chain's terminal-only filter drops the emission (in the fourth, at `Engine::send`'s outer frame, before the session-strand body's filter is consulted), and the send completes ok. These two show that the other arms' result comes from a recorded cancellation and not from the setup. Fifth arm: the cancellation is recorded and the send is not refused at the hop: its own `toApp` runs. What it returns after that is asserted as measured |
| NC-5 | (a) to (c): a `close()` and a send started in one `toApp`, in three orders: terminal close then send; graceful close then send; send then graceful close. (d) A send started in the graceful Logout's `toAdmin`. (e) A graceful `close()` and then a send started in one `fromApp`. (f) A logout the peer starts, with a send started in the confirming Logout's `toAdmin` | (a), (b), (d): the send returns `session_invalid_state_for_send`, nothing of it is transmitted, no number is consumed, and after a graceful close the Logout is the last frame transmitted. (c): the send's frame, then the Logout. (e): the close has not begun, so the send is admitted and nests: its `toApp` runs inside `fromApp`; then the order of P-9. (f): the order of P-9; this arm pins a disclosed limitation (§7), not a wanted behaviour |
| NC-6 | a terminal close started in `toAdmin`, at a callback-first site and at an assign-first one | inside `toAdmin` after the start: as `ToAppStartsClose_Terminal` inside `toApp`. Then per transport arm, as that cell: P-1, for the outer admin frame. NC-6's outer operation is `on_inbound_frame`, not a send, so the result of the operation that called `toAdmin`, and whether the store and the manager agree, are asserted as measured (P-1's measurement, run at both sites) |
| NC-7 | a close that waits, in each mode, cancelled meanwhile. The close is started in a callback by a `co_spawn` bound to a slot the cell owns; `terminal` and, in a second arm, `total` are emitted from inside the callback after the `co_spawn` has returned. In the graceful mode the cell ends the close's wait for the peer's Logout as NC-8's second arm does, by advancing the mock clock past `logout_disconnect_timeout_ms` | the session reaches `closed_drained` and the close's result is ok, in both arms |
| NC-8 | a close that waits while another close runs first. Before it runs the `io_context`, the cell queues the delivery of the frame and, behind it, a handler that starts the other close. That handler is then ahead of the post that the callback's close makes. **Arm 1:** the other close is terminal, on the synchronous sink, with no send in flight and `reset_on_disconnect` off, so that it cannot suspend (P-8); the callback's close is terminal. **Arm 2:** both closes are graceful, and the cell installs a counting hook with `set_close_flush_hook`. It ends the first close's wait for the peer's Logout by advancing the mock clock past `logout_disconnect_timeout_ms`, which ends `run_logout_phase1`'s sleep on the session's clock | Arm 1: the callback's close returns exactly `session_already_closed`; `onLogout` runs once; the lifecycle ends `closed_drained`. Arm 2: the callback's close joins the first: both return the same result, the Logout is transmitted once, and the hook has run once |
| NC-9 | the guard, through test access to the scope object, so that the guard's state is the only session state the cell's threads share. Builds with assertions only. (a) A first thread enters one session's scope and stays inside; a second thread enters after the first has told it so through an ordinary handshake (a promise or a condition variable). (b) One thread enters, enters a nested scope and leaves it; a second thread then enters while the outer scope is live. (c) One thread enters a nested scope, leaves both, and enters again. (d) On `linux-clang-tsan`: as (a), but the only ordering between the two threads is a `std::atomic<bool>` that the first stores and the second reads with `memory_order_relaxed`. The first thread stays inside the scope until the process dies. The main thread starts both and then joins them, so the death statement does not return first; a join orders nothing between the two threads | (a) and (b): death at the token's assertion. (c): no assertion; a control. (d): the death's output is matched by a gmock matcher that requires the token assertion's text and the absence of the string `ThreadSanitizer` (`AllOf(HasSubstr(…), Not(HasSubstr("ThreadSanitizer")))`); P-7. **What (a) proves and does not prove:** it proves that a second thread's entry while a first is inside is detected. It does not prove that the detection is free of a data race: the handshake orders the first thread's write before the second thread's read, for ThreadSanitizer as for the hardware, so a token built as a plain member passes (a) on every preset. Arm (d) is the only witness of the token's atomicity |
| NC-10 | the OD-3 check. The cell sets the session's engine-stop flag on the session's strand through `session_engine_access::note_engine_stop`, and then calls `Engine::send` for that session. This is a construction: the order it stands for (a send between its admission re-check and its spawn while `stop()` runs its first step) cannot be produced on demand, because no test hook sits between those two statements. The real interleaving is not driven. The cell calls C++ `Engine::send`, and no C-ABI twin is added: with OD-6 withdrawn the check changes nothing a C-ABI consumer can observe (§2.4), so there is no C-ABI effect to witness | the send returns `session_invalid_state_for_send` and nothing is transmitted |
| NC-11 | a close started in each lifecycle callback. `onLogon`: both roles, both close modes, a persistent and a volatile store; the arm on a persistent store sets a non-zero heartbeat interval through the fixture's knob and feeds the peer's Logon on the session's own executor ("Fixture additions the cells need"), and it has a positive-control arm, the same setup with no close started in `onLogon`. `onLogout`: fired by a graceful close, with a terminal close started in it; and fired by a `Disconnected` write outside `close()`, with a close started in it. **Route of that write:** with the validate gate off, the test body delivers to the Active session a frame whose BeginString is not the session's. The Active arm writes `Disconnected` at its BeginString test, with no `close()` and no emit; it is the frame NC-19's lifecycle arm feeds from `toApp`. `onCreate`: both close modes | inside the callback after the start: the state and lifecycle the callback entered with. Then, per callback, the disposition of §2.1a (P-5): for `onLogon` on a persistent store, no liveness loop is started and `on_inbound_frame` returns ok. The observable for "no liveness loop" is the mock clock's count of parked sleeps (`inflight_count()`), read as `expect_close_from_on_logon_starts_no_liveness` in `tests/session/test_session_plaintext_roundtrip.cpp` reads it: with a non-zero heartbeat interval a started loop parks a sleep while the session is Active, and the arm asserts that none is parked. A loop started on a session that has already left Active ends at its first test of the state and parks nothing, so the reading tells "not started" from "started" only where the session is still Active when the Logon arm resumes; elsewhere the assertion is kept and claims no more than that no sleep is parked. That reading is a zero, so it counts only beside its positive control: in the same setup with no close started, the count of parked sleeps is non-zero while the session is Active. The control is run with the arm, and the arm's zero is not reported without it (added by the orchestrator, 2026-10-11; not reviewed). For `onLogout` inside a graceful close, the terminal close joins it and the grace wait ends; for `onLogout` after a `Disconnected` write outside `close()`, the close started there is the session's first: it joins no other close and does not return `session_already_closed`; for `onCreate`, `open()` returns ok and the session is closed at the end with no `onLogout` |
| NC-12 | two application frames delivered by one coroutine with no yield between them, a terminal close started in the first `fromApp`. First arm: `MemoryStore`. Second arm: `FileStore` | First arm: the order of P-3, the second `fromApp` before `onLogout`. This arm pins a disclosed limitation, not a wanted behaviour. Second arm, as P-3's measurement found it on a persistent store: the second `fromApp` does not run, and the second frame's `on_inbound_frame` returns ok on the closed session. This arm pins where the limitation stops |
| NC-13 | Engine level: two Engines over loopback TCP with a non-zero heartbeat interval. A terminal and a graceful close started in `fromApp`, and in `toApp` of an `Engine::send`. The arm for a terminal close started in `fromApp` also runs on `linux-clang-asan`; it is R-3's measurement | inside the callback after the start: Active, lifecycle open. The session's role loop ends and the Engine stops cleanly afterwards. Graceful from `toApp`: the peer receives the outer frame and then the Logout. Terminal from `toApp`: the peer does not receive the outer frame (P-1). This is the cell in which an owner (the role loop, then `stop()`) frees the Session after the close |
| NC-14 | R-6. A test-double store that holds its first `store()` call and completes the second one first. Two overlapping sends; then the same with the second send started in the first one's `toApp` | the second frame is transmitted before the first, in both shapes. This cell shows the consequence of a store that breaks the requirement, as the B&L row states it; it is not a wanted behaviour |
| NC-15 | the payload copy, on `linux-clang-asan`. `toApp` allocates a buffer on the heap, starts a detached `Session::send` over it, and frees the buffer before it returns. The buffer is on the heap because a buffer on the stack is seen only with `detect_stack_use_after_return`, and the preset sets no `ASAN_OPTIONS` | no ASan report; the transmitted frame carries the payload's bytes |
| NC-16 | the exposed setup of R-7: the fixture's `direct_executor` knob, attested over the bare `io_context` executor. A callback starts a send (first arm, from `toApp`) or a close (second arm, from `fromApp`) and then runs the `io_context` (`poll`) before it returns, so the post's continuation runs inside the callback (P-10). The first two arms run on builds with assertions only. **Third arm,** `linux-clang-release` only: the first arm's setup, with the callback's `poll` loop bounded by a count of handlers. The count is the implementer's choice; what constrains it is that a correct run, whose loop ends when `poll` runs no handler, ends well below it, and that the mutant's run, which does not end by itself, reaches it. This arm pins behaviour outside the contract. It is there only so that the mutant "step 6 defers again" can go RED, and the cell's comment says so. **Fourth arm,** a control on both build types: the first arm's callback on a session in the default mode that the fixture drives from a strand of its own (`ExecKind::strand`). The session's strand is a second strand, made over that one, so the handler that holds the callback is the outer strand's and a post onto the session's strand executes through it. The arm shows a driver on a serialising executor. It is not §2.1's case, entry points on the session's own strand | first arm: death at step 4's assertion. Second arm: death at the close's depth assertion. Third arm: the loop ends below its bound and the send has run nested, as it does today; the cell fails if the bound is hit. Fourth arm: no death; the send is deferred, its `toApp` running after the outer `toApp` returned (P-10) |
| NC-17 | `B35SendSlot.Nested_ASendInsideToAppLeavesTheOuterFrameIntact`, rewritten. Today it asserts that the send started in `toApp` reached its own `toApp` inline, and skips on a build with assertions, citing fixpp#564 | the skip is removed. The second send's `toApp` runs after the outer `toApp` returned. The bytes the outer `toApp`'s view spanned are the bytes stored and transmitted for the outer frame; that assertion is a pin of B35's property, which this PR does not introduce, and the order assertion before it is the one R-1 changes (the first mutant row). Which buffer the second send took: P-6 |
| NC-18 | the miss path during `stop()`: the twin of `V15_SendCounterEnrolledBeforeControlHop_NoUAF`. Many `Engine::send` calls for an unregistered id, and for a registered id whose session is not published, queued while `stop()` runs; then the Engine is destroyed. How many sends and how many io threads is the implementer's choice, taken from V-15, its twin; what constrains it is that the cell claims nothing about any single send. Under ASan and TSan. As in V-15, whether a given send is on the control strand's queue when `stop()` drains depends on timing across several io threads; the cell drives many sends and claims the drain for those that are, not a forced interleaving | `stop()` completes; no sanitizer report; each send returns one of the miss path's two documented errors or `session_invalid_state_for_send` |
| NC-19 | a frame fed from inside a callback, on builds with assertions. The callback starts `on_inbound_frame` by a `co_spawn` that begins inline, as the I-3 test does from `fromApp`. **Dispatch arms,** validate gate off: fed from `toApp`, from `toAdmin` and from `onLogon`, each with an application frame that carries the inbound number the session expects at that moment, so that its arm reaches dispatch. **Validate arm,** on `ValidateGateFixture` (`tests/session/test_validate_gate_inbound.cpp`) with an `Application` attached: `fromAdmin` on a conformant Heartbeat feeds a Heartbeat with a tag the dictionary does not define for it, as `UnexpectedTag_Reason2` does. The validate gate runs before the sequence check, so that frame's number does not matter. **Lifecycle arm,** validate gate off: the `toApp` of a send on an Active session feeds a frame whose BeginString is not the session's. The Active arm tests BeginString before the sequence check, writes `Disconnected` there and emits nothing, so `onLogout` is entered inside `toApp` (P-10) | dispatch and validate arms: death at the parse-site assertion. In the validate arm the nested arm never reaches the inbound overload, so the death can only come from `validate_inbound_`'s assertion. The `onLogon` arm is kept only if its nested frame reaches a parse there (P-10's measurement run records it); the lifecycle scopes' count is also witnessed by NC-11. Lifecycle arm: death at the lifecycle sites' assertion. The nested arm reaches no parse site and enters no outbound callback, so the death can only come from that assertion |
| NC-20 | an outbound callback inside an outbound callback, on builds with assertions: NC-16's `direct_executor` fixture with a non-zero heartbeat interval. A send from the test body; its `toApp` advances the mock clock by the heartbeat interval and then runs the `io_context` (`poll`), so the liveness loop wakes and begins its emit inside `toApp` (P-10) | death at the stack-arena overload's assertion |

Not carried over from v0.3: the cell for `Engine::stop()` started from a callback (R-5); the cell "a
`toAdmin` inside a `toApp`'s deferred window" (its mutant has no path, below); a separate cell for a
graceful close in `fromApp` with no store and with a FileStore, which the store arms of the graceful row
now cover. Not carried over from v0.4: NC-13's arm for a close started in `onCreate` under the Engine. An
application has no handle to start it with (§2.1a), and NC-11 covers `onCreate` on a raw `Session`.

**Arms that must go RED** (each run in a scratch copy, never in the PR's tree). Every row has the trace
from the mutated statement to the assertion that fails. A row for which no trace could be written is not
in the table; those are listed after it.

| Mutant | Fails | Trace |
|---|---|---|
| the depth test removed from the send's body, or the public `send` passing "skip" | the classification assertion of the `toApp` and `toAdmin` send cells; NC-17's order assertion | The send started in the outbound callback runs inline. On a build without assertions its own `toApp` runs with the starting callback on the stack, and the witness records "nested" where "deferred" is asserted; at a callback-first site the "distinct numbers" assertion fails too. On a build with assertions the process aborts first, at the stack-arena overload's assertion, when the nested send reaches its `toApp`. NC-17's second send is started in `toApp` in the same way, so its `toApp` runs before the outer `toApp` has returned, where "after" is asserted |
| the outbound depth counted at the inbound overload too | the classification assertion of `FromAppSendsReply` and `EngineSendFromFromApp*`; NC-2's `fromAdmin` arm; NC-5 (e) | `fromApp` raises the outbound depth, the send started there takes the deferred path, its `toApp` runs after `fromApp` returned, and the witness records "deferred" where "nested" is asserted. The inbound overload calls `fromAdmin` too, so NC-2's `fromAdmin` arm fails the same way. In NC-5 (e) the deferred send then finds the close begun and is refused at step 5, where "admitted" is asserted |
| the cancellation test removed from the deferred path (a bare post, then the send) | NC-4, first and third arms | The recorded cancellation throws at the await of the inner send. With that await outside a `try`, the send completes with an exception where the OD-1 error is asserted. With it inside one that converts as `Session::send`'s does, `Disconnected` is written and "the session stays Active" fails |
| the deferred path's cancellation test reading the recorded cancellation alone, without `throw_if_cancelled` | NC-4, fifth arm | The chain turned throwing off and a `terminal` cancellation is recorded. The test holds on the record alone, and the send returns the OD-1 error at the hop. Its own `toApp` never runs, where the arm asserts that it does |
| the depth kept outside the `Session`, as one counter for the thread or for the engine | NC-3 | A's `toApp` raises the shared depth. B's send, started inline from A's callback, reads it as non-zero and takes the deferred path. B's `toApp` runs after A's callback returned, where "not deferred" is asserted |
| the `closing` test removed from the deferred path | NC-5 (b), graceful close then send | The close's post runs first and its Logout suspends in its store call with `fsm_state_` still Active. The send's post runs, `Session::send`'s Active check passes, and the send's frame is transmitted after the Logout. "Returns `session_invalid_state_for_send`" and "the Logout is the last frame" both fail |
| the payload copy removed | NC-15 | The deferred send's build reads the span after `toApp` freed the buffer: ASan reports a heap use after free |
| the depth-zero assertion removed from the deferred path | NC-16, first arm | The post's continuation runs inside `toApp` and the send goes on nested. It reaches its own `toApp`, where the stack-arena overload's assertion aborts the process with a different text. The arm matches step 4's text, so the death does not satisfy it |
| step 6 of the deferred path passing "apply" | NC-16, third arm | Step 4 is compiled out, the depth is still non-zero, the test applies, and each continuation defers again: the callback's loop hits its bound |
| the close's wait removed | every close cell: "inside the callback after the start, the lifecycle is open" | `close()` runs inline in the callback and writes `closing` before the callback's `co_spawn` returns. With §2.2's token a nested callback no longer aborts on the debug build by itself, so the cell fails at that assertion on both build types. A close inside `toApp` or `toAdmin` aborts first on a build with assertions: a graceful one where its Logout's `toAdmin` meets the stack-arena overload's assertion, a terminal one where `onLogout` meets the lifecycle sites' assertion |
| the close's wait limited to graceful mode | the terminal close cells and `…NestedToAppThrows*`: "inside the callback: Active, `onLogout` not run" | A terminal close runs inline to `Disconnected` and fires `onLogout` with the callback on the stack. Inside `toApp` or `toAdmin`, on a build with assertions, the process aborts there, at the lifecycle sites' assertion |
| the close reading the outbound depth where it must read the depth of all callbacks | `FromAppStartsClose_*`, `FromAdminStartsClose_Terminal`, `…NestedToAppThrows*`: the same assertion | In `fromApp` and `fromAdmin` the outbound depth is zero. In `…NestedToAppThrows*` the nested `toApp`'s scope has exited before `send_impl` awaits the close. The close runs inline in each |
| the depth of all callbacks maintained only on builds with assertions | every close cell, on `linux-clang-release` only: "the lifecycle is open inside the callback" | The scope compiles to nothing under `NDEBUG` today. If the counter goes with it, the depth reads zero in release and the close runs inline |
| the depth of all callbacks set to zero on a scope's exit where it must be decremented | `…NestedToAppThrows*`: "still Active inside `fromApp`" | The nested `toApp`'s scope exits and zeroes the depth while `fromApp` is on the stack. `send_impl`'s close reads zero and runs inline |
| the close's wait placed after the `closing` write | every close cell: "inside the callback after the start, the lifecycle is open" | The write happens before the post, inside the callback |
| the close's wait placed between the three-state test and the `closing` write | NC-8. Arm 1: "returns exactly `session_already_closed`". Arm 2: "the hook has run once" | The waiting close passed the test while the lifecycle was open. Arm 1: the other close completes during the wait; the waiting close then writes `closing` over `closed_drained`, runs the body a second time and returns success. Arm 2: the waiting close resumes at the first close's first suspension, which is after that close called the hook; it replaces the shared result and runs a graceful body of its own, so the hook runs a second time |
| the close's wait placed in front of `close()`'s reset, followed by asio's cancellation test and an early return (the shape the send's hop has) | NC-7, its `terminal` arm: "the session reaches `closed_drained`" | Before the reset the caller's terminal-only filter is in force, so a `terminal` emission is recorded. The test returns early and the close is skipped. The `total` arm stays green under this mutant, because that filter drops it |
| the depth-zero assertion removed from the close's wait | NC-16, second arm | The close's continuation runs inside `fromApp` and the close proceeds there. No other assertion lies on its path: inside `fromApp` the outbound depth is zero, so a Logout's `toAdmin` passes the stack-arena overload, and `onLogout` passes the lifecycle sites' assertion, which reads the same depth. No death where one is expected |
| the hit path replaced by the control-strand hop | `MtEngineSendFromFromApp_BusyControlStrand`: "every reply nested" | With the control strand busy the first hop does not run inline, and the cell reports deferred replies. Measured on today's code, which is this mutant (the record) |
| the OD-3 check removed | NC-10 | The session is Active and its flag is set; without the check the send is admitted and transmitted |
| the token's owner test removed | NC-9 (a) | The second thread enters without an assertion: no death |
| a nested scope's exit clearing the token | NC-9 (b) | The nested exit leaves "no owner" while the outer scope is live. The second thread installs itself and passes: no death |
| the token built as a plain member | NC-9 (d) | The second thread's read of the token has no ordering with the first thread's write that ThreadSanitizer can see. It prints a data-race report on the token, and the arm's matcher rejects output that contains one (P-7). Arms (a) and (b) stay green under this mutant |
| the inbound overload's parse-site assertion removed | `InboundFrameDispositionsI3.AnInboundParseReenteredFromACallbackDies`; NC-19's dispatch arms | The nested arm reaches the inbound overload with a callback on the stack, builds the resource over the buffer and calls the receive callback, whose scope passes the token on the owner thread: no death |
| `validate_inbound_`'s parse-site assertion removed | NC-19's validate arm | The validate gate parses the nested frame over the buffer and rejects it. The arm answers with a Reject, whose `toAdmin` enters the stack-arena overload inside `fromAdmin`, with the outbound depth at zero, and passes. The nested arm never reaches the inbound overload: no death |
| a parse-site assertion reading the outbound depth where it must read the depth of all callbacks | the I-3 test; NC-19's `onLogon` and validate arms | Inside `fromApp`, `fromAdmin` and `onLogon` the outbound depth is zero: no death. NC-19's `toApp` and `toAdmin` arms stay green under this mutant |
| a parse-site assertion that fires only while the inbound overload's own callback is on the stack, which is what v0.4's mark did | NC-19's `toApp`, `toAdmin` and `onLogon` dispatch arms | No inbound callback is on the stack when the nested frame arrives: no death. The I-3 test stays green under this mutant |
| the stack-arena overload's assertion removed | NC-20 | The liveness emit's `toAdmin` enters with `toApp` on the stack and passes the token on the owner thread. The Heartbeat's callback runs inside the outer emit's stretch: no death |
| the lifecycle sites' assertion removed, from both sites or from the `onLogout` site alone | NC-19's lifecycle arm | The nested arm writes `Disconnected` at the BeginString test, and `onLogout` is entered inside `toApp`, where its scope passes the token on the owner thread. The arm parses nothing over the buffer and emits nothing, so no other assertion lies on its path: no death |

Rows v0.3 had, and why they are gone:

- **"The scope's exit setting the [outbound] depth to zero".** With R-1 and R-4 in place, and inside the
  contract, no outbound callback of a session can run while another outbound callback of that session is
  on the stack: a send,
  an `Engine::send` and a close started there all wait. The mutant differs from the correct code only on
  a path that does not exist, so no behavioural cell can fail on it. The row and its cell are deleted.
- **"The Active re-check after the hop removed".** The deferred path no longer has an Active test of its
  own (§2.1); `Session::send`'s check covers it, and removing that check is not a mutant of this PR.
- **"The close's wait made cancellable".** Replaced by the narrower row above. Its other reading, the wait
  moved in front of the reset with no test after it, fails no cell: a cancellation recorded during the wait
  is erased by the reset. That placement is therefore not witnessed. The note still specifies "after the
  reset" (§2.1a), so that the statement which shields the rest of `close()` covers the wait too, and it
  claims no witness for that order.
- **"The guard's restore replaced by a clear", against the nested-then-sequential cell.** A clear never
  causes a spurious assertion in that cell; it causes a missed owner and a missed depth. The two rows
  above that replace it name the cells that observe those. The nested-then-sequential arm stays as
  NC-9 (c), a control.

A row v0.4 had, and why it is gone: **"the inbound-parse mark's assertion removed"**. The mark is removed
(§2.2); the parse-site rows above replace it.

Specified without a witness, and claimed as such:

- **The hit path reading `reader_snapshot_` and not `Engine::lookup`** (§2.3, step 3). The two differ only
  where a send's frame is destroyed after the Engine on a build with assertions. No cell constructs that.
  The lease-counter assertion round 3 offered as a witness is not taken (owner): the two are the same
  program in release, and differ in debug only when an Engine is destroyed while its own `stop()` is
  suspended, which the Engine's destructor already forbids.
- **The lifecycle sites' assertion at the `onLogon` site** (§2.2). NC-19's lifecycle arm reaches the
  `onLogout` site only. An arm for `onLogon` needs a Logon processed while an outbound callback of the
  session is on the stack, and no setup for that was read to a conclusion.
- **The close's wait after `close()`'s reset and not in front of it**, with no test after it: the third
  bullet of the v0.3 list above.
- **The side of its scope on which the stack-arena overload's assertion sits, and the lifecycle sites'**
  (§2.2: after the scope). On one thread both placements die at the same assertion, with the same text.
  NC-9, the cell with two threads, reaches the scope through test access and enters neither site. Listed
  with the orchestrator's decision of 2026-10-11.
- **The default mode's immunity where the session's entry points run on its own strand** (§2.1). NC-16's
  fourth arm is a neighbouring control: its handler is the fixture's outer strand's.
- **The sends R-2 admits that today's control-strand read refuses** (§2.4). Which side of `stop()` a
  racing send falls on is timing; no cell forces it.
- **OD-3's refusal on R-5's route** (§2.1a). The route is out of this PR; fixpp#579 inherits it.
- **The branch "the GapFill of a resend replay" of §2.1's first-yield table.** No cell starts a send in
  that GapFill's `toAdmin`: no setup for a send started inside a resend replay was read to a conclusion.
  The cells that start a send in `toAdmin` do so at the Heartbeat that answers a TestRequest and at a
  session Reject. The row rests on §1's reading of `emit_gapfill_async`.
- **The branch "a failure exit" of the same table:** a veto, a throwing callback or a failed assign of
  the outer emit, after its callback started a send. No cell drives it. The outer frame is abandoned
  there, so nothing of it is stored or sent that the deferred send could be ordered against.

A mutant that stays green is a finding about its cell, and the cell is fixed or deleted before the
verification record's verdict is written.

**Existing tests that pin this path and must stay green**, each with whether it still reaches the code it
was written for:

| Tests | After this PR |
|---|---|
| `InboundFrameDispositionsI3.AnInboundParseReenteredFromACallbackDies` (`tests/session/inbound_frame_dispositions_test.cpp`) | Still dies, now at the inbound overload's parse-site assertion. Its expected text is edited to that assertion's (§2.2, §4) |
| `EngineSessionStrand.V15_SendCounterEnrolledBeforeControlHop_NoUAF` | Stays green and stops driving its path. It sends to an Active, published session, so after R-2 its sends take the hit path and never queue on the control strand. Its subject is restated as sends queued on the session strand when `stop()` drains, and NC-18 is its twin on the path that keeps the control-strand hop |
| `EngineSessionStrand.V16_PostDrainLateSendFastFails_WithoutPosting` | Still reaches its code: it fails at the admission re-check, which is before any lookup and unchanged. It requires exactly `session_invalid_state_for_send` |
| the other send-versus-stop cells and the snapshot readers of `EngineSessionStrand`; its re-entrant sub-cell (V-9, sub-cell (a)) | The implementer classifies each cell by whether its sends target a published session (hit path) or not (miss path) and writes the classification in the verification record. The re-entrant sub-cell posts before it sends, so it still shows a send started after a callback and no longer crosses the control strand; its comments are in §4 |
| `ApplicationOutbound.EnginesSendWithUnknownIdReturnsInvalidArgument`, `EnginesSendOnRegisteredButNotEstablishedSession` | Still reach their code: both take the miss path, whose results they pin |
| `ApplicationEngineSend.SendFromForeignThreadCrossesWire`; the C-ABI send tests | Reach `Engine::send` from a foreign thread, now on the hit path |
| `ApplicationEngineSend.SendDrainRaceNoUAF` | Not a foreign-thread cell: it is on R-5's route. It sends from the thread that runs the `io_context`, and its `toApp` hook starts `engine.stop()` by a plain `co_spawn`, so `stop()`'s first steps run inside `toApp` (§2.1a). By reading, its result does not change: the send's session-strand body has started, and passed the stop check, before the hook runs, and the cell accepts a value, `session_invalid_state_for_send` or `dispatch_aborted`. Its `total` emission is dropped by the chain's terminal-only filter (§2.1), so it does not exercise a cancelled send. Its comments say "posts stop()" and describe the emission as cancelling the send; §4 lists them. fixpp#579 inherits the cell as the existing witness of its route, with both facts |
| `ApplicationEngineSend.ReentrantSendFromToAppNoDeadlock`, `ReentrantSendFromFromAppNoDeadlock`; `ApplicationStrand.ReentrantSendNoDeadlock`; `ApplicationOutbound.ReentrantSendFromCallbackDoesNotDeadlock`; the interop conversation cells; the re-entrant cell of `test_business_messages_roundtrip.cpp` | Unchanged in behaviour. Each posts before it sends, or sends from the test body, or posts a no-op, so none starts a send inside a callback and none exercises this contract. Their comments attribute the deferral to `Engine::send`; §4 lists those comments |
| `ApplicationStrand.NoConcurrentCallbacksForOneSession`; `SeamDirectExecutorReentrancy.CorrectAttestationDoesNotTripGuard`; the `SeamExecutorCompat` cells | Still reach the guard, in its token shape. Serial callbacks must not trip it |
| `B35SendSlot.Nested_ASendInsideToAppLeavesTheOuterFrameIntact` | Does not stay as it is: NC-17 |

**Sanitizers** (`[const §IX.2]`: all three run on the PR).
- TSan: the `EngineSessionStrand` suite, the two `Mt*` cells, NC-9's arm (d) and NC-18. The hit path reads
  the snapshot off the control strand from io threads, which is new for `Engine::send`.
- UBSan: every cell above on `linux-clang-ubsan`. The change adds integer counters, one depth
  incremented and decremented in pairs, the other saved and restored, and no arithmetic on untrusted
  input; a clean run is expected and is stated as what it drove.
- ASan: every cell above; of NC-13, the arm its row names, a terminal close started in `fromApp` (R-3);
  NC-15 and NC-18; and the cells of §2.5
  (`FromAppSendsReply_NestedToAppThrows*`, `FromAppStartsClose_*`, `FromAdminStartsClose_Terminal`), which
  the preset could not reach before §2.2. The disabled control cell in the test file shows the instrument
  reports on this binary. By §2.5 no owner frees the Session in the direct-`Session` cells, so a clean run
  there shows the absence of other faults on these paths and says nothing about lifetime; NC-13 is the
  cell with an owner.
- A clean sanitizer run is stated as what it drove, not as absence.

**Coverage** (`[const §IX.1]`). Measured on `linux-clang-coverage` with fresh per-binary profiles, over
the functions the PR changes in `src/session/session.cpp`, `src/session/engine.cpp`,
`src/session/session_engine_access.hpp` and `include/fixpp/session/session.hpp`, against the article's
line and branch thresholds for the touched
module. Every line or branch the diff adds that stays uncovered gets a disposition in the verification
record: tested, waived with its reason, or escalated to an issue. These dispositions are fixed in advance:

- The half of step 3's test in which a cancellation is recorded and `throw_if_cancelled` is off: tested,
  by NC-4's fifth arm. No arm of v0.4 reached it.
- The failing side of each assertion this note adds: reached only in a death arm's child process. Where
  the coverage run does not collect a child's profile, that side is waived as the violated precondition
  the death arm witnesses, and the record names the arm. The lifecycle sites' assertion at the `onLogon`
  site has no death arm (the list above); its failing side is waived on that statement.

**Static analysis and format** (`[const §IX.4]`): `clang-tidy`, the `clang-format` check, `cppcheck` and
`include-what-you-use`, over the files the PR touches, as `/speckit-verify` runs them.

**B35's gates.** The zero-allocation gates on the Active inbound read and on `Session::send`
(`tests/alloc_guard/`, the `test_544_*` cells, the mallocnesia population check) are re-run. A send they
measure is not started inside an outbound callback, so it does not take the deferred path; it does pay
the depth test, and the inbound read pays the counter writes. One `test_544_*` cell does start a send
inside `toApp` and changes: NC-17.

**Cost (OD-5).** The PR adds a bench, in a commit of its own before any production edit, and its manifest
row, in a later commit after the local run. Its evidence is that local paired run. "This PR's evidence",
below, fixes the order of the steps.

- **The bench.** Source `bench/session/engine_send_bench.cpp`. Target `engine_send_bench`, added in
  `bench/session/CMakeLists.txt` with `add_bench_with_test_include`, the helper that gives a bench the
  library root as an include root; this bench needs it for `tests/support/transport_double.hpp`. Its
  session also needs a dictionary: as the sibling benches that build a session do, the bench takes the
  FIX44 one from `tests/support/fix44_dictionary.hpp`, which needs a `FIXPP_DICT_DATA_DIR` compile
  definition on the target. The binary is `bin/engine_send_bench`.
- **Harness.** One `Engine` on one `io_context` that the bench thread runs, on a mock clock, as
  `session_send_bench.cpp` runs a `Session`. One registered initiator session whose
  `transport_factory_override` is `ScriptedReadTransportFactory`, a plaintext factory (the session takes
  the plaintext security profile that the factory's comment names), over a `ScriptedStream` that scripts
  the peer's Logon and has `hold_open` set, so the read pump waits after the Logon and reads no end of
  stream. `tests/session/engine_establishment_timeout_test.cpp` drives an
  engine-managed session over the same double. The session is published and Active with no socket, no
  TLS and no file.
- **What it times.** Each iteration starts one `Engine::send` on the `io_context` with a completion
  handler and polls until it completes. The payload is a NewOrderSingle body whose first field is not
  MsgType. `build_send_frame_` refuses it at its second test with `app_payload_malformed`, before the
  sequence number is read, the frame is built or any callback runs. The session stays Active, and nothing
  is assigned, stored or written.
- **Why the timed part is the route.** What runs per iteration is `Engine::send`'s own path (the payload
  copies, the enrolment, the admission re-check, the lookup, the spawn onto the session's executor, the
  Active check) and `Session::send` up to the refusal (its Active check, the depth test, the slot
  holder). The lookup is what this PR changes: the control-strand `co_spawn` and the `registry_` find on
  the base tree, the snapshot load and the stop check on the candidate. Nothing of the send path after
  the refusal is there to dilute that difference. One cost from outside the send path is in each
  iteration: the scripted read holds an `asio::steady_timer` while it waits, so the `io_context` has a
  reactor and each poll can include a zero-timeout kernel poll. It is equal on both trees and dilutes
  the difference a little; it is also a system call inside a row whose same-tree spread is checked
  ("The same-tree spread", below). The isolation is from the socket write and from everything else the
  send path does after the route. A send vetoed in `toApp` was the other way to stop before the write;
  it adds the frame build, the outbound parse and the callback to every iteration, which
  `session_send_bench`'s `to_app` case already times through `Session::send`.
- **Reach.** Before the timed loop, one well-formed send must complete with a value and reach the
  stream. After it, every timed send must have returned exactly `app_payload_malformed`, the session must
  still be Active, and the stream must hold no write after that first send's. On this path only
  `build_send_frame_` returns `app_payload_malformed`
  (`grep -n "app_payload_malformed" src/session/*.cpp`), so a send refused earlier, for an unknown id, a
  session that is not Active or a stopped Engine, is a miss. A miss is a `SkipWithError`, never a timing.
- **One thread.** The manifest's selection rule admits single-threaded, CPU-bound binaries only. So the
  send is started from the bench thread, outside any strand's handler, and each `co_spawn` runs as a
  dispatch on that thread. The row times the route's CPU cost. It does not time the wake-up of an io
  thread by a send started on another thread, which is a scheduling measurement; `L-023-2` keeps that
  sentence (§4), and the foreign-thread probe below measures it locally, outside the gate.
- **The manifest row,** in the session block of `bench/ci-suite.txt`, under comment lines that name this
  note and say what the bench times:

  ```
  bin/engine_send_bench                       none:no-gb-baseline-recorded                     paired
  ```

- **This PR's evidence.** CI does not compare a `paired` row that is absent from the merge base's
  manifest: `[const §VIII.2a]` makes it a candidate-only addition, gated on execution and schema and
  left out of the timing comparison for that PR. So the comparison is local: A-B-A-B on
  `linux-clang-release`, compared min per tree. The bench source uses the public API and test-support
  headers that exist at the merge base (`tests/support/transport_double.hpp`,
  `tests/support/fix44_dictionary.hpp`), and `Engine::send`'s signature does not change, so the same file
  and the same target lines build in a base worktree. The order is fixed. (1) One commit, before any
  production edit, adds `bench/session/engine_send_bench.cpp` and its target in
  `bench/session/CMakeLists.txt`, and nothing else: no manifest row, no `MANDATORY_PAIRED` entry. (2) The
  production edits. (3) The local A-B-A-B run, with both same-tree spreads, and the foreign-thread probe
  on both trees ("The foreign-thread probe", below). (4) One commit with the manifest row, the
  `MANDATORY_PAIRED` entry and the erratum in `.specify/ci209-bench-gate.md`. A run at step 1 checks reach
  and gives a first look at the spread; it is not a leg of the comparison. If the bench source changes
  after step 1, step 3 is run again on both trees. The base worktree's path is padded to the candidate
  path's length, because
  a path of another length shifts code layout. The result goes into the verification record whichever
  way it falls; a slowdown beyond the budget of `[const §VIII.2]` needs the acceptance that article
  describes.
- **The local run's command.** Each leg runs the bench binaries with the Google Benchmark flags that
  `ci/run-bench-suite.sh` passes: by hand, or by that script given an explicit manifest as its last
  argument, a scratch file that lists the binaries of this run. The shipped manifest cannot serve: it has
  no row for `engine_send_bench` before step 4 and none for `session_send_bench`. Base and candidate
  alternate. With those flags each leg's JSON holds aggregate rows only, and "min per tree" is, for each
  benchmark, the minimum over a tree's legs of each leg's median `cpu_time`, the statistic `run_paired` in
  `tools/bench_compare.py` takes. `tools/bench_compare.py --paired` itself is not the instrument for this
  comparison: it leaves a `paired` row that is absent from the base manifest out of the timing
  comparison, which is why the comparison is local.
- **The same-tree spread.** The comparator fails a run in which a `paired` row's two measurements of one
  tree differ by more than its noise band, and CI takes no such reading for a row in the PR that adds
  it. So the local run also records the spread between the two base runs and between the two candidate
  runs for `engine_send_bench`, and compares each with the comparator's noise band
  (`PAIRED_NOISE_BAND_PCT` in `tools/bench_compare.py`). If either exceeds half the band, the bench is
  stabilised before the manifest row is committed. If it cannot be, the row's `paired` status returns to
  the owner before merge.
- **The counters and the depth test.** The same local run covers the in-tree benches that drive those
  paths: `bin/on_inbound_frame_bench`, whose NewOrderSingle cases deliver to a counting `fromApp`
  and so pay the counter writes on the inbound read; and `bin/session_send_bench`, whose two cases pay
  the depth test and whose `to_app` case pays an outbound callback's counter writes.
  `on_inbound_frame_bench`'s manifest status does not change; `session_send_bench` has no manifest row
  and is run by hand. These benches are read under the same rule as the route bench: A-B-A-B, min per
  tree, `[const §VIII.2]`'s budget, and the result goes into the verification record whichever way it
  falls. Probe p1 is re-run for the reply path: the nested arm must not be slower than the same arm built
  against the base tree, beyond the probe's own run-to-run spread.
- **The foreign-thread probe.** v0.4's second probe: it times `Engine::send` from a foreign thread, base
  against candidate. The first OD-5 ruling promoted it to a paired bench; the manifest's selection rule
  does not admit a binary of that kind ("One thread", above), so the paired bench above is the PR's gated
  evidence in its place (§0). The probe is kept as a local measurement, at step 3: it is run on the base
  tree and on the candidate, and its result goes into the verification record whichever way it falls. It
  is not gated: it has no manifest row and no pass criterion. Its source goes beside p1 in the parent
  repo's `research/probes/b41_563_564/`, outside the PR's tree.
- **The known cost of the ruling.** A `paired` row cannot be removed or downgraded once merged
  (`[const §VIII.2a]`).

**Local gates before the first labelled push:** every `ci-script-pins` step, the bench suite's own run
over the changed manifest (`ci/run-bench-suite.sh` on the candidate build), every Tier 1 source-audit job
(the `co_spawn` closure audit and its census reconciliation read `engine.cpp`'s spawn sites, which change),
`check_line_citations.py --shift-audit origin/main..HEAD` (023's bundle cites `engine.cpp` by line), and
`check-comment-claims.py`, which is in the parent repo's `.claude/scripts/` and is run with the library
as its `--root`. The `[const §XVII.7]` local build and the MSVC leg are in §0a.

## 4. Disclosures, documentation and catalogue

Round 1 found v0.3's list short, because it was written from the rows its author had in mind. This list
is derived by search. The searches are recorded so that the next round, and the implementer on the day,
can run them again; the table below is what they returned and is the thing to distrust. What round 2
found missing is not reachable by a search for old wording; those rows are in the table, marked "(read)".

**The searches.** Run from the library root, in bash:

```bash
roots="brain .specify spec include src tests bindings"
skip="--exclude=563-564-callback-send.md --exclude=test_563_564_*"
g() { command grep -rnIiE "$1" $roots $skip; }      # the live tree
f() { command grep -rnIiE "$1" specs; }             # the frozen bundles, for the brain flags

# A. The route of Engine::send
g 'two[- ]hop|two .{0,14}hops|(→|->) ?control ?(→|->)|control[- ]strand hop|send.{0,60}control[_ -]strand|control[_ -]strand.{0,60}send|queue up on the control|keepalive from the registry|registry .{0,30}keepalive|posts onto'
# B. A send started inside a callback
g 'behind the current dispatch|enqueued? behind|not\*? run before|runs inline|ran inline|run inline|every send suspends|re-?entrant (send|call|engine)|nested (send|in .?toApp)|send nested|from inside (a |an |the )?(.?toApp|.?fromApp|application callback|callback)'
# C. The guard
g 'callback_dispatch_scope|in_dispatch_|dispatch_guard|concurrent session callback entry|callback scope|T019 note'
# D. The rows and issues this PR changes
g 'L-544-3|L-544-4|B-019-3|B-023-2|L-023-2|#563|#564'
# E. The engine-stop flag and its readers
g 'engine-stop flag|engine_stop_requested_|note_engine_stop'
# F. A close started inside a callback
g 'clos(e|es|ed|ing)[^.]{0,50}(inside|within) (a |an |the |its )?[^.]{0,20}callback|onLogout[^.]{0,60}(inside|inline)|(inside|inline)[^.]{0,60}onLogout'
# The same families over specs/, for the list of brain flags
f 'two[- ]hop|(→|->) ?control ?(→|->)|behind the current dispatch|enqueued? behind|callback scope|callback_dispatch_scope|every cross-thread entry point|dispatch.-that-runs|registry snapshot'
```

Some populations are not reachable by a search for old wording, and are found by reading:

- **The doc comment above each declaration whose behaviour changes.** `grep -n "close(\|> send(\|> store("
  include/fixpp/session/session.hpp include/fixpp/session/engine.hpp
  include/fixpp/session/message_store.hpp`, then read upward from each declaration.
  `include/fixpp/session/application.hpp`'s comments on every callback are read in full.
- **Documents that a new requirement makes incomplete without making any sentence of theirs false.** Read
  outward from the requirement. For R-6: the design document `.specify/api-contract.md` §9 names for
  `MessageStore`. For R-7: every place that defines the `direct_executor` attestation
  (`grep -rnI "already_serialized_executor" include spec .specify brain`, read each).
- **Statements that change because a row moves.** When a B&L row moves to the closed file, search for
  its id and for any range that spans it (`L-544-1`…`L-544-4` is written as a range on a brain page).
- **Statements made false by a result, not by a wording.** The `B35SendSlot` cell that asserts a nested
  send ran inline was found through family B's "ran inline"; any other test that asserts today's nesting
  in `toApp` or `toAdmin` fails when the implementation lands, and the full suite run finds it.

Before trusting an empty result from any family, check that it still reports the site named for it here:
A, `Engine::send`'s doc comment in `include/fixpp/session/engine.hpp`; B, the same comment's "enqueued
behind" sentence; C, the comment on the inbound overload of `parse_and_dispatch_` in
`src/session/session.cpp`; D, `brain/components/session.md`; E, `src/session/session_engine_access.hpp`;
F, the fixpp#518 block comment in `tests/session/test_session_plaintext_roundtrip.cpp`. Once the PR has
rewritten a site, its family is checked against the base tree instead (`git grep` at the merge base).

**What the searches and the reading returned, by file, and the action for each.**

| File | What its hits say | Action |
|---|---|---|
| `include/fixpp/session/engine.hpp` | `Engine::send`'s doc comment: "posts onto the session's strand", "captures a shared_ptr keepalive from the registry", "enqueued behind the current dispatch", "before the first hop" | Rewritten in place to §2.3's path and §2.1's rule |
| `include/fixpp/session/session.hpp` | `close()`'s doc comment, its PRECONDITION paragraph included ("the first close runs to completion … before any second close() call can reach the `closing` branch") | Rewritten in place: a close started in a callback waits (R-4), so another close can run first |
| the same | `send()`'s doc comment and the `send()` line of the reentrancy contract | Rewritten in place: R-1's rule, the payload copy, the two new results |
| the same | the comments on `callback_dispatch_scope` ("zero overhead" in release; "no two callbacks enter concurrently"), on `dispatch_app_callback`'s guard and its lifetime precondition, and on `in_dispatch_` | Rewritten in place to §2.2: the token, the two depths, the assertions, and what compiles out |
| the same | (read) the reentrancy contract says nothing about a callback that runs the session's executor | R-7's sentence added: a callback must not run the session's own executor |
| `include/fixpp/core/session_executor.hpp` | (read) the comment on choosing a threading mode lists "a single-thread `io_context`" as an executor the attestation fits, with no condition | R-7's sentence added there |
| `include/fixpp/session/session_config.hpp`, `include/fixpp/core/error.hpp`, `include/fixpp/core/trace_context.hpp`, `.specify/2l-tap.md`, `.specify/2j-controlplane.md` | (read) they name the attestation knob: when it is required, the error for its absence, an example with it off, and two design documents that mention it in passing | No change: none says what the attested executor may do inside a callback |
| `include/fixpp/session/session.hpp` | the comment on `send_fallback_leaf_` ("a send nested in toApp") and on the engine-stop flag ("`logon_arm_superseded` reads it") | Rewritten in place: a send deferred from `toApp`; a second reader, `Engine::send`'s hit path |
| `include/fixpp/session/message_store.hpp` | `store`'s doc comment says nothing about concurrent calls | R-6's requirement added |
| `include/fixpp/session/application.hpp` | the callbacks' comments say nothing about a send or a close started inside them | A sentence per callback kind added, from §2.1's and §2.1a's tables |
| `include/fix/c_api/session.h` | the blocking thunk "posts onto the same strand" | No change: it describes the C ABI's blocking call, which this PR does not change. The file is not reformatted |
| `src/session/engine.cpp` | the block comment above `Engine::send` ("two-hop design", "session→control→session") and the comments in its body | Rewritten in place. The file's header comment names this note as superseding 023's two-hop send, so a reader who starts at the code is routed here |
| the same | `stop()`'s step-1 comment: the engine-stop flag "which Logon arms test" | Rewritten in place: the second reader |
| `src/session/session.cpp` | the comment on the inbound overload ("callback_dispatch_scope asserts it") | Rewritten in place: it names the parse-site assertion, and `validate_inbound_`'s comment names it too |
| the same | `open()`'s comment at `onCreate` (the scope is dropped "so onLogout … can acquire its own … without hitting the re-entrancy assert") and the "T019 note" comments at the inbound sites | Rewritten in place: a nested scope is legal on one thread; what those sites now rely on is the depth being back to its entry value |
| `src/session/session_engine_access.hpp` | the engine-stop flag "Logon arms test" | Rewritten in place; the reader OD-3 needs is added beside the setter |
| `spec/behaviors-and-limitations.md` | `B-019-3` ("posts onto the per-session strand", "enqueued behind the current dispatch") and `B-023-2` ("`send` traverses caller→control→session") | Each rewritten in place |
| the same | `L-023-2`, whose title is "No dedicated `Engine::send` two-hop / establish-churn perf micro-bench" and whose text defines its subject as "the two-hop send (`caller→control→session`)" | Rewritten in place and kept open, for its establish-churn half only (OD-5). What goes: the two-hop send as a subject, and the sentence that a send micro-bench "would be dominated by TLS-loopback setup". What is added: the send route is measured by `bin/engine_send_bench`, a `paired` row, since this PR, on one thread; the wake-up of an io thread by a send started on another thread is not timed by a gated bench. What remains, as the row's subject and its follow-up: no bench times the snapshot's republish on session establishment and teardown, and the recorded fact that the snapshot atomic is not lock-free on the supported standard libraries (`B-023-3`). Status stays "follow-up" |
| the same | (read) `L-007-2`: a `direct_executor` attested over a non-serialising executor is undefined in release and asserts in debug | Widened in place (R-7): same-thread re-entry, a callback that runs the session's own executor, is named beside concurrent entry, with the same disposition and status |
| the same | `L-050-6`, which lists "`Engine::send`'s control-strand hop and session-strand hop" in the chain a C-ABI send awaits | That clause rewritten in place |
| the same | `L-544-3`, `L-544-4` | Moved to the closed file |
| the same | `L-497-1`: "a send started from inside `toApp`" takes the fallback leaf | That clause rewritten in place: a send deferred from `toApp`, on §2.1's condition |
| the same | `L-544-2`: the `Session` must outlive a send's frame | A sentence added: the rule is now reachable from `close()`'s wait where `send_impl` awaits a terminal close (§2.1a) |
| the same | `L-015-4`: names `dispatch_guard` and `in_dispatch_` | Rewritten in place to the members §2.2 leaves |
| the same | `B-093-11`, its "`Engine::stop()`" bullet: the flag the Logon arms test | A sentence added: the second reader |
| the same | `L-054-1`, `B-050-3`, `B-055-2` | No change: the blocking C-ABI calls from inside a callback still deadlock, and Python's refusal stays |
| `spec/feature-catalogue.md` | 019's row: "`Engine::send` is any-thread (posts onto the executor, registry `shared_ptr<Session>` keepalive)" | Rewritten in place; this PR gets its own entry |
| `brain/components/session.md` | the fixpp#544 section: "every send suspends after its frame is built", "a send started from inside `toApp`, which runs inline", and "Two pre-existing defects this work surfaced, not fixed by it" (`L-544-3`, `L-544-4`) | Rewritten in place. The first was already false for a session with no store and a synchronous sink (measured, §1) |
| the same | "What was rejected": the engine-stop flag is "read only inside `logon_arm_superseded_`" | Rewritten in place: the second reader |
| the same | (read) the fixpp#544 section's pointer to B&L: "`L-544-1`…`L-544-4`; what stays open is `L-497-1`" | Rewritten in place: `L-544-3` and `L-544-4` are in the closed file after this PR |
| the same | — | A new section for this decision: R-1 to R-7, the OD-5 and OD-6 rulings, what was rejected (§5), and the flags below |
| `.specify/2d-threading.md` | "Non-reentrant from inside a callback … the send does *not* run before `fromApp` returns"; and (read) its notes on the `direct_executor` attestation, which say the executor is used "as if it were running under a strand" and name no callback that runs it | An erratum appended at the end, for the first passage and for R-7's sentence. The document is cited by section elsewhere, so nothing is inserted |
| `.specify/2e-msgstore.md` | (read) the design document of `MessageStore`. Its `store` contract says concurrent calls "serialise against the per-instance writer mutex" and that under the single-domain discipline "contention is zero"; the same sentence is in its `MemoryStore` concurrency bullet | An erratum appended at the end: R-6's requirement, and that contention is not zero, because two sends of one session overlap in their store calls. §2.1's "the interface did not say so" is about the header; this document said how the shipped stores behave, not what an implementation must do |
| `.specify/544-hot-path-zero-alloc.md` | §2.5 and its "As built" section: a send started in `toApp` "runs inline"; `Engine::send` "makes two `co_spawn` hops, one through `control_strand_`"; fixpp#563 and fixpp#564 as open follow-ups | One erratum appended at the end, naming those passages by section. The note is a closed PR's plan and is not edited in place |
| `.specify/2i-capi.md`, `.specify/2j-controlplane.md`, `.specify/2m-pybind.md`, `.specify/2f-async-mutex.md` and its draft | C-ABI thunks that post onto the session strand; Python's rules for calls from a callback; the mutex's re-entrant drain | No change: none describes `Engine::send`'s route or a C++ send started in a callback |
| `tests/session/test_544_send_slot_cells.cpp` | the header comment ("runs inline … fixpp#563, out of scope"), the nested cell's skip citing fixpp#564, and its "ran inline" assertions | The cell is rewritten (NC-17) and the comment with it |
| `tests/session/inbound_frame_dispositions_test.cpp` | the I-3 cell's comment ("callback_dispatch_scope asserts that no second callback enters") and its expected message | The comment rewritten in place, and the expected message edited to the parse-site assertion's text (§2.2) |
| `tests/session/test_engine_session_strand.cpp` | V-15's comment and body comments (sends "queue up on the control_strand_", "the full two-hop"); the file header's "(session→control→session)" | Rewritten in place to V-15's restated subject (§3) |
| the same | (read) V-9's comments on its sub-cell (a): "Re-entrant Engine::send from inside fromApp", and that its post mirrors a T012 guarantee | Rewritten in place: the sub-cell posts and then sends to the other session; after R-2 that send takes the hit path |
| the same | lines marked "Pre-T012" or "Historical" | No change: they describe a past state and say so |
| `tests/session/test_application_strand.cpp` | Test 4's prose ("enqueued behind the current dispatch", "The spec says Engine::send posts onto the strand") and the comments that the scope "asserts !in_dispatch_" | Rewritten in place: the cell sends from the test body after the callback; the guard is the token |
| `tests/session/test_application_engine_send.cpp`, `tests/session/test_application_outbound.cpp` | the re-entrant cells' prose ("posted behind the current dispatch", "releases in_dispatch_", "must enqueue behind the current dispatch") | Rewritten in place: the deferral in these cells is the test's own post |
| `tests/session/test_application_engine_send.cpp` | (read) `SendDrainRaceNoUAF`'s comments: its hook "posts stop()", and its `total` emission "cancels Engine::send's inner coroutine" | Rewritten in place to what the cell does (§3): the hook starts `stop()` inline, and the emission is dropped by the chain's filter. The cell's assertions are not changed; fixpp#579 inherits it |
| `spec/coverage-index.md` | 023's entry records "send micro-bench → L-023-2" | That clause gets a pointer in place: the send route's bench was delivered by this PR, and `L-023-2` now tracks establish-churn. This PR gets its own entry |
| `bench/ci-suite.txt`, `bench/session/CMakeLists.txt` | — | The target of §3 ("Cost") and `bench/session/engine_send_bench.cpp`, which is new, in a commit of their own before any production edit; the manifest row in a later commit, after the local run (§3, "This PR's evidence") |
| `.specify/ci209-bench-gate.md` | (read) the bench gate's design record. Its census names the binaries of the paired tier and is already behind the manifest, and its promotion rule asks a binary added to that tier for a history of same-tree spread | An erratum appended at the end, in the commit that adds the manifest row: `engine_send_bench` joins the paired tier as a row born `paired` (`[const §VIII.2a]`, OD-5), on the local spread check of §3 ("Cost") and without that history. The census is re-derived by the commands the record gives, not copied |
| `ci/test-bench-gate.sh` | (read) `MANDATORY_PAIRED`, the floor that keeps a `paired` row of the shipped manifest from being downgraded | `engine_send_bench` is added to the list, in the commit that adds the manifest row |
| `tests/session/test_session_plaintext_roundtrip.cpp` | the fixpp#518 block comment: a close started by `co_spawn` in a callback "would run close() inline inside the callback" | Rewritten in place: after R-4 it would wait. The cells keep their `asio::post` and are unchanged in behaviour |
| `tests/session/test_business_messages_roundtrip.cpp`, `tests/interop/test_business_message_interop.cpp`, `tests/interop/conversation/conv_cell_test.cpp` | "re-entrant `Engine::send` from inside `fromApp`", with the note that the call is hoisted by `asio::post` | No change: each says that it posts |
| `tests/session/test_application_throw.cpp`, `test_direct_executor_reentrancy.cpp`, `test_executor_compat.cpp`, `test_019_application_compile_smoke.cpp`, `test_credential_store_redaction.cpp` | they name `in_dispatch_`, `dispatch_guard` or the scope | Read when §2.2's members are named; a comment that names a member which no longer exists is rewritten in place |
| `tests/session/CMakeLists.txt` | the comments on the two B41 targets ("reproduction", "measurement") | Rewritten when the cells are lifted. Its other hits describe the posting suites above and do not change |
| `tests/session/engine_reset_unit_stop_test.cpp`, `tests/session/inbound_frame_dispositions_test.cpp` (its engine-stop cells), `tests/support/session_test_access.hpp` | they set or read the engine-stop flag through the access classes | No change: none says who else reads the flag |
| `src/session/session.cpp` | "onLogout fires inside record_state_transition_" | No change: still true |
| `bindings/python/fixpp.i`, `include/fixpp/core/sync/async_mutex.hpp`, `tests/sync/`, `tests/wire/validator_per_version_test.cpp`, `tests/session/test_544_session_strand.cpp`, `test_persistent_seqnum_hydrate.cpp`, `test_file_store_offload_thread.cpp`, `test_cancellation_parse_to_fromapp.cpp` | hits on the patterns' words in another sense (the mutex's re-entrant drain, FIX `NoHops`, an executor's `blocking` property, Python's blocking-call rule) | No change |

**Frozen bundles are not edited.** `brain/components/session.md` flags each bundle the last search finds
in `specs/`, naming the passages below. The search also reports those bundles' plans, tasks, checklists
and quickstart, which repeat the same statements; the flag is on the bundle and covers them.

- `specs/019-app-callbacks`: FR-006, its "Re-entrant send from within a callback" edge case, research D6.
- `specs/023-engine-session-strand`: FR-012, contract C-0's "every cross-thread entry point enters the
  control-plane domain", C-2's "never an inline `dispatch`-that-runs", data-model E-0, research D0's send
  route. 023's research weighed "Lock-free registry snapshot for `send`" and deferred it "as a possible
  optimization … revisit only if the bench gate fails"; R-2 adopts it for a different reason, and the page
  says so.
- `specs/093-inbound-frame-dispositions`: contract C-3 I-3 and the research line that "the callback scope
  asserts against nesting". I-3 still holds. Its assertion's carrier changes from the callback scope to
  the depth assertion at the parse sites, and the flag says that.

**B&L, live file: new rows.**

- **`Session::send` started inside a callback (R-1):** the table of §2.1 as an application observes it;
  that the payload is copied when the send is deferred; the two new results, with the sentence that
  `dispatch_aborted` no longer implies `Disconnected` and a caller reads `state()`; and that a send
  started while the session answers the peer's Logout, in the confirming Logout's `toAdmin` or in the
  `fromAdmin` that follows it, is numbered after our Logout (§7), with the outcome written from P-9's
  measurement.
- **`close()` started inside a callback (R-4):** it begins after the callback returns; which callbacks
  count; the departure from QuickFIX C++ and J, that `onLogout` follows the callback.
- **A limitation of that rule**, stated as conditions, with each outcome written from §3's measurement
  and not from this note's prediction:
  - the arm that called the callback can resume on a closing or closed session, wherever it suspends
    after the callback (a persistent store; any arm that answers the frame);
  - a terminal close started in `toApp` or `toAdmin` can leave the outer frame retained and not
    transmitted, its send in error (P-1, measured for `toApp`);
  - with `reset_on_disconnect` set, the outer frame's write can reach a transport that is already
    closed. Where the transport refuses it, nothing is transmitted and the send returns
    `dispatch_aborted`, and the teardown reset leaves the store empty with both next numbers at their
    initial value (P-1a, measured for `toApp`);
  - frames already read with the frame whose callback asked for the close can be processed before the
    close begins, on a volatile store, which does not suspend the arm (P-3, measured on a
    `MemoryStore`). On a persistent store the arm suspends in its persist, the close runs there, and the
    later frames' callbacks do not run (measured on a `FileStore`, the same row);
  - a close started in `onCreate` on an initiator that an application drives itself ends the session
    after its Logon has been transmitted, with no Logout (P-5, measured). Under the Engine an
    application has nothing to close with in `onCreate` (§2.1a);
  - on a TLS transport a write can begin while the transport's `close_async` is suspended; what the
    transport does with it is not measured (§7).
- **`Engine::send` through the snapshot (R-2):** the hit path and the condition on the caller under which
  it nests (§2.3); the cold path and what it returns; OD-3's stop check, which replaces the control-strand
  read and is witnessed by a construction, not by the real interleaving (NC-10); the narrow class of
  sends racing `stop()` that is refused today and can be admitted afterwards (§2.4); and that on the
  fallback `atomic_shared_ptr` every `Engine::send` takes one shard lock for the snapshot load.
- **The guard (§2.2):** which nestings on one thread are legal; that entry from a second thread still
  asserts; that a parse over the session's buffer from inside any callback asserts, an outbound
  callback inside an outbound callback asserts, and `onLogon` or `onLogout` entered inside an outbound
  callback asserts; the nestings that asserted before and are no longer detected, each of them inside an
  inbound or lifecycle callback (§2.2's "not detected" rows); and the counter writes every callback now
  makes in every build type (§2.1, "Cost").
- **`L-007-2`, widened (R-7):** the row of the table above.
- **`MessageStore::store` (R-6):** calls complete in the order they were made; a store that completes two
  calls out of order has their frames transmitted out of order (NC-14). This binds every existing
  implementation of the interface.
- **A limitation: a nested send that disconnects inside an inbound callback or `onLogon`** (§2.5, each
  route it lists).
- **A limitation: `Engine::stop()` started from an application callback** (R-5). On one io thread it can
  close the session's transport before the callback returns; on several it depends on whether the control
  strand is idle. Sends already queued on that session's strand, which today are sent, are refused with
  `session_invalid_state_for_send` after this PR (§2.1a). Status: follow-up, fixpp#579.

`L-023-2` is not a new row: the table above has what goes, what is added and what remains.

**Issues.** The PR closes fixpp#563 and fixpp#564. Their acceptance item 3 (measure `Engine::send`, then
make the `engine.hpp` sentence match) is met by the record and this section. fixpp#579 is the follow-up
for `Engine::stop()` and stays open after this PR; the PR body and the commit messages name it only as a
follow-up.

**Compatibility.**

- `Session`'s layout changes: two depths, and the owner token in place of `in_dispatch_`. A C++ layout
  change before the first release. `Session::send` keeps its signature; it becomes a plain function that
  returns the awaitable of a private coroutine (§2.1).
- `MessageStore::store` gains a documented requirement (R-6). No signature changes.
- The threading contract gains a sentence (R-7). It states what `L-007-2` already made undefined for a
  false attestation, for a case no sentence named.
- The C ABI's symbols, types and error codes do not change, and no call that succeeds today fails
  afterwards (OD-6 withdrawn). The reason is §2.4's last row. Outside the route R-5 sets aside, OD-3
  refuses only sends that the control-strand read refuses today, with the same error. The sends it
  refuses and today's code admits are on that route, which the C ABI cannot take. And a send that races
  `fixpp_engine_destroy` from another thread
  has an outcome that depends on timing today and still does; the window in which it is admitted is
  wider. A consumer can observe that widening: such a send may succeed where it failed before. Under
  `[const §X.7]` a change is breaking when a call a consumer could already make fails, or returns a
  result other than the one documented. Neither holds: the outcome of a send that races
  `fixpp_engine_destroy` depended on timing, and no documentation promises that it fails (re-derive: read
  the comments on `fixpp_session_send` in `include/fix/c_api/session.h` and on `fixpp_engine_destroy` in
  `include/fix/c_api/engine.h`). No version bump, and no BREAKING clause. P-11 measured the reading on
  the base tree, and it agrees (§3).

## 5. Rejected alternatives

- **Defer a send from every callback** (the consult's K1). Sound, and one rule. Rejected by R-1: in
  `fromApp` it changes what a ported application observes (the reply's `toApp` and numbering move out of
  the callback; a reply can be numbered after an engine reply to the same frame) and adds a hop to the
  reply path.
- **Let a send nest in `toApp` and `toAdmin` and repair each emit site.** Restamping the outer frame after
  the callback sends a different MsgSeqNum than the one the callback inspected; assigning before the
  callback makes a `toApp` veto consume a number. Neither restores order at the sites that already assign
  first, where the nested frame reaches the store ahead of the outer one (measured). It would need an
  ordering mechanism at every emit site, and there is no QuickFIX behaviour to keep there: C++ and J put one
  number on two frames and Go deadlocks.
- **Refuse a send started in a callback.** It forbids the reply from `fromApp`, which all three engines
  support and their own examples use.
- **A session send gate** (an `async_mutex` around the whole send). Already rejected by B35's ruling R-10;
  nothing here changes its reasons.
- **Write the depth at each call site instead of in the overload.** An outbound emit added later would
  silently lack it.
- **Reuse `in_dispatch_` as the R-1 predicate.** It is set for inbound callbacks too, which is the K1
  behaviour, and it is a debug check today.
- **Guard `Session::send` only.** The first draft of this note. A graceful `close()` is a second emit body
  and reproduces the defect (measured); making same-thread nesting legal would have turned an abort into a
  silent duplicate on every build type.
- **Leave a terminal `close()` inline** (v0.2 of this note), with a check after each inbound callback, or
  with the arm left to carry on as QuickFIX C++ and J do. Rejected by R-4: the check is needed at every
  Active arm and has to decide what a half-processed frame counts as; carrying on leaves a session that
  reports closed while one of its arms still runs.
- **Make a graceful `close()` wait only inside an outbound callback.** The smallest change that restores
  order; it leaves the Logout's `toAdmin` running inside `fromApp`, which no QuickFIX engine does.
- **`Engine::send`: document "nested or deferred".** Rejected by R-2.
- **`Engine::send`: a post at the first hop**, so it is always deferred. Rejected by R-2: it departs from
  `Session::send` and from QuickFIX, and adds a post to every `Engine::send`.
- **Put every registered id in the snapshot** to drop the miss path. Rejected in §2.3: a republish per
  `register_session`.
- **Publish the snapshot once in `start()`** for the same purpose. It changes what a send before `start()`
  returns for a registered id.

Added at Gate A round 1:

- **R-4 read as "after the operation that called the callback has completed"** (Codex's counter-proposal
  to F-1: track the operation and release the close at a safe boundary). It would deliver v0.3's
  withdrawn consequence. Rejected by the owner: it needs a mechanism at every operation entry
  (`on_inbound_frame`, `Session::send`, the liveness emits, the Logon arms, `close()`'s own Logout); a
  terminal close would wait on the store and transport I/O that a terminal close exists to interrupt, so
  it needs a bound and an escalation on expiry; and the path where `send_impl` itself awaits
  `close(terminal)` would have to hand over instead of waiting on itself.
- **That reading for outbound emits only, and "after the callback returns" for inbound arms.** It would
  spare the frame that is numbered and never sent (P-1). Rejected by the owner: two rules to state and to
  test.
- **A "close requested" mark that the read pump or the Active arm tests before the next frame**, so that
  frames read with the one whose callback asked for the close are not processed (§2.1a, "The close can be
  late"). Not taken: the owner chose R-4's reading with that cost stated, and the mark is a second
  mechanism beside the post. It is a check at a frame boundary, not after a callback, so the rejection of
  "a check after each inbound callback" above does not cover it; it remains available if the limitation
  proves unacceptable in use.
- **A post at `Engine::stop()`'s entry, in this PR**, so that `stop()` never runs inside its caller.
  Rejected by R-5: it changes a function this note otherwise leaves alone. It is fixpp#579's candidate.
- **Disclose store order as a limitation of send ordering** and leave `MessageStore::store`'s contract as
  it is. Rejected by R-6: the ordering promise of §2.1 would then hold for the shipped stores only.
- **Order emission inside `Session`, with tickets or an ordered completion queue**, so that order does not
  depend on the store (Codex's second counter-proposal to F-2). Each send would wait for the previous
  send's store before it may request the write gate. That is the queuing B35's ruling R-10 rejected for a
  session send gate, and it is not adopted without reopening that ruling.
- **A lifetime rule for the payload in the header** ("a payload passed to a send started in `toApp` or
  `toAdmin` must outlive the returned awaitable") in place of the copy. Rejected by the orchestrator: the
  same application code would be correct in `fromApp` and undefined in `toApp`.
- **An atomic depth, read by the guard and by `close()`.** It would make the cross-thread check and the
  R-4 predicate one counter. Not taken: it puts an atomic read-modify-write on every callback of every
  build type, for a reader (`close()`) that is on the session's strand already. The token carries the
  cross-thread meaning with atomic operations on builds with assertions only (§2.2).

Added at Gate A round 2:

- **A callback-exit latch** (Codex's counter-proposal to round 2's F-1): after the post, a send or close
  whose callback is still on the stack suspends on a latch that `callback_dispatch_scope` releases when
  the depth returns to zero. Rejected by R-7, for the triage's four reasons. It is a third suspension,
  released from a destructor that runs between the callback's return and `assign_outbound()` at every
  callback-first site; it cannot resume the waiter inline there, because that begins an emit inside the
  outer stretch, so it has to post, and it needs waiter storage, a cancellation rule and a teardown rule
  on a path that exists only for a contract violation. R-1 as ruled is "by one post". It repairs sends
  and closes only: under a callback that runs the `io_context`, the engine's own emits and a nested
  frame delivery still enter the stretch. And the nearer alternative, deferring again, never lets the
  callback's `poll()` return (§2.1).
- **Refuse at run time:** after the post, a send whose callback is still on the stack returns an error on
  every build type. Rejected by R-7: it is one more result of `Session::send` to document and test; it
  has no answer for `close()`, which cannot be refused without leaving the session open; and the
  engine's own emits under such a callback stay broken.
- **Keep the inbound-parse mark beside the depth assertions.** The depth assertion at the inbound
  overload implies the mark's, so no cell could show the mark's assertion to be missing (§2.2).
- **One assertion at `on_inbound_frame`'s entry in place of the parse sites'.** It would also abort a
  frame fed from inside a callback whose arm parses nothing over the buffer. Not taken: `on_inbound_frame`
  is a plain function that returns its arm's awaitable, so an assertion in it runs when the awaitable is
  made, not when the frame is processed; and an assertion at the arm's first resumption would abort
  frames that pass today's guard, those whose arm calls no callback, and whether a shipped test feeds
  such a frame from inside a callback has not been measured. The parse sites are where the buffer is
  written, which is what the guard protects.
- **Extend step 5 to the confirming Logout,** by reading `logout_seen_`. It would refuse the send
  deferred from that Logout's `toAdmin` and leave the send nested in the same Logout's `fromAdmin`, which
  runs after the Logout was emitted and is not deferred. The case belongs with `Session::send`'s
  admission test, which this PR does not change (§7).
- **A strand-confined flag for step 6's skip** (v0.4). It was read at the depth test, which sits after
  the Active check, so a deferred send refused at that check left it set for the next send. Replaced by
  an argument (§2.1).
- **Isolate the bench by a `toApp` veto.** §3, "Cost", has why the refused payload was taken.
- **Declare OD-3 a breaking C-ABI change** (the first OD-6 ruling). Withdrawn by the owner on the
  corrected premise (§0, §2.4): a BREAKING paragraph would describe a failure no consumer can observe.

## 6. Decisions, all settled

**Settled at round 1.** The triage judged each of v0.3's three; none is reopened unless a later round
finds the judgement wrong. OD-2 is not in the list: it was v0.2's question about an inbound arm whose
callback closed the session, and R-4 settled it at v0.3.

- **OD-1 — the result of a deferred send cancelled before it starts: `dispatch_aborted`, with no state
  change.** Sound, on two conditions that this revision writes in. The result has to be produced by an
  explicit test after the post, because the post does not throw (§2.1, step 3). And the B&L row has to
  say that `dispatch_aborted` from `Session::send` no longer implies `Disconnected` (§4). At the C ABI the
  value maps to `FIXPP_ERR_CANCELLED` (`src/capi/error.cpp`), and the C ABI cannot cancel a send, so
  nothing changes there. A new error value was the alternative; it would touch the error taxonomy and the
  C ABI's translation table.
- **OD-3 — the stop check on the hit path: the session's engine-stop flag, read on the session's strand.**
  Sound as a mechanism. `stop()`'s first step sets the flag on that strand before it emits cancellation,
  so the check is ordered with `stop()` by the strand, not by an atomic read that can go stale between the
  check and the send (the gap 093 plan OD-15 rejected for the same flag). It needs a reader on
  `session_engine_access` (§2.3). By reading it breaks no shipped cell: V-15 accepts any result, and V-16
  fails earlier, at the admission re-check. Its cell is a construction (NC-10). Its `[const §X.7]`
  disposition is settled below (OD-6).
- **OD-4 — `Engine::send` from a callback of an unpublished session cannot be admitted.** Holds for both
  role loops, by reading: each publishes the session before it delivers a frame, a refused publish
  delivers none, and the callbacks that run before publication (`onCreate`, the initiator's Logon
  `toAdmin`) are before Active. Re-derive by reading what follows each `publish_entry` call in
  `run_accept_loop` and `run_connect_loop`. NC-1 pins it per role.

**Settled by the owner after round 2.** The rulings are in §0; each is restated here with where the note
implements it.

- **OD-5 — how `[const §VIII.3]` is met: a bench for `Engine::send` in this PR, with a `paired` manifest
  row.** §3 ("Cost") has the source file, the target, what it times, the isolation, the row, how this
  PR's evidence is produced, and the foreign-thread probe that is kept outside the gate (§0). `L-023-2`
  closes for the send route and stays open for establish-churn (§4).
  The alternative v0.4 listed, two probes accepted as the PR's evidence, is gone.
- **OD-6 — withdrawn: OD-3 is not a breaking change, and this PR declares no C-ABI change.** v0.4 said
  that a send whose session-strand work starts after `stop()`'s first step "can be transmitted today and
  is refused after this PR". By reading, today's control-strand read refuses that send already (§2.4).
  OD-3 stays. P-11 measures the reading as a first implementation step; a transmitted send there returns
  the question to the owner. It was run on 2026-10-11, and no send was transmitted there (§3).
- **Same-thread re-entry (the triage's Q2-1) — outside the contract: R-7.** §2.1 has the setups and the
  contract sentence, §2.2 the assertions, §4 the widened `L-007-2`, and §5 the latch and the run-time
  refusal that were not taken.

No decision is open. `/analyze` ran over v0.6 and the owner's `/plan` sign-off is given (§0a). P-11,
which gated the rest of the implementation, is measured and agrees (§3).

## 7. What this does not decide

- A live send that interleaves with a resend replay. A deferred send can start while
  `replay_outbound_range_` is suspended, as any overlapping send can today. Read, not run; QuickFIX/Go
  added a lock for it. Not changed here; a candidate issue.
- A send that overlaps a graceful close without being deferred. It can be numbered and transmitted after
  our Logout, by timing, because `fsm_state_` stays Active until the Logout's emit returns. §2.1's step 5
  refuses that for a deferred send only. Not changed here for the others.
- A send started while the session answers a logout the peer began. The Active arm emits the confirming
  Logout, then calls `fromAdmin` on the peer's Logout, then persists, and writes `Disconnected` only
  after that; no `close()` has begun, so step 5 does not see it. A send started in the confirming
  Logout's `toAdmin` is deferred and then admitted, and a send started in that `fromAdmin` nests; by
  reading, each is numbered after our Logout and, where it is transmitted, transmitted after it, every
  time. Before this PR, on an `NDEBUG` build, the first took the Logout's own number (fixpp#563) and the
  second behaved as it does now. Refusing both is a change to
  `Session::send`'s admission test, which this PR does not make. NC-5 (f) pins the first as measured
  (P-9), and the R-1 B&L row discloses both.
- What a TLS transport does with a write that begins while its `close_async` is suspended. Not decided by
  reading. Under R-4 that order is the regular one for a terminal close started in `toApp` on a TLS
  session; the cells run over plain transports. By reading, `ApplicationEngineSend.SendDrainRaceNoUAF`
  drives that order over TLS, with an assertion that accepts a value or either of two errors (§3).
- `send_impl`'s `co_await close(terminal)` after a throwing `toApp`, on a chain that is already cancelled:
  the await throws before `close()` is entered, and the session is left `Disconnected` and not closed.
  Read at round 1; it predates this PR and is not changed by it.
- The C ABI's and Python's send from a callback (`L-054-1`, `B-055-2`).
- A session send queue, a bound on overlapping sends, or their back-pressure.
- `Engine::stop()`: neither the function nor what it does when an application starts it from a callback
  (R-5, fixpp#579), beyond the one effect of OD-3 on that route that §2.1a states.
- The further copies of the payload on `Engine::send`'s path: its own copy, then the session-strand
  body's, then the deferred path's when that is taken.
- A gated bench for what `Engine::send` costs across threads, and a bench for establish-churn. The new
  bench times the route on one thread, and the foreign-thread probe is a local measurement that is
  reported and not gated (§3, "Cost"). `L-023-2` stays open for establish-churn; the cross-thread wake-up
  is a sentence in that row, not its subject.
- What a callback that runs the session's own executor gets on a build without assertions (R-7,
  `L-007-2`).
- Whether `register_session` should republish the snapshot; two comments in `engine.cpp` and `engine.hpp`
  disagree with the code about it. Noted, not changed.

## Gate A

- Round 1 applied 2026-10-11: Codex P1=3 P2=4 P3=1; Opus post-judging P1=2 P2=12 P3=5; rewrite addresses root causes 1–5 and the stand-alone items N-2, N-7, N-8, N-9, N-10, N-11, N-12 and F-5. Reviews: research/reviews/codex_563_564_1_callback-send_review.md, research/reviews/opus_563_564_1_callback-send_triage.md (parent repo).
- Round 2 applied 2026-10-11: Codex P1=3 P2=4 P3=1; Opus post-judging P1=0 P2=7 P3=9; rewrite addresses root causes 1–4 and the stand-alone items F-5, F-7, F-8, N2-4, N2-6, N2-7, N2-8 and N2-9. Reviews: research/reviews/codex_563_564_2_callback-send_review.md, research/reviews/opus_563_564_2_callback-send_triage.md (parent repo).
- Round 3 (on v0.5) 2026-10-11: Codex P1=0 P2=2 P3=2; Opus post-judging P1=0 P2=0 P3=9. Converged. Codex's two P2 findings (the owner token's exit order; the snapshot read having no witness) were judged P3. Reviews: research/reviews/codex_563_564_3_callback-send_review.md, research/reviews/opus_563_564_3_callback-send_triage.md (parent repo).
- v0.6 applies the P3 items of round 3 after convergence, except Codex F-2, which the owner declined; these edits were not reviewed. Owner sign-off 2026-10-11.
- The assertion at the `onLogon` and `onLogout` sites that the outbound depth is zero (§2.2) was added after the final review, at the owner's choice. The triage offered it as optional under N3-2. It has had no review: neither reviewer saw its nesting rows, its death arm (NC-19's lifecycle arm) or its mutant row.
- v0.7 (2026-10-11) applies the findings of the `/analyze` pass over v0.6 (report: research/reviews/analyze_563_564_callback-send.md, parent repo) and records the P-4 and P-11 measurements; these edits were not reviewed.
- v0.7 also records, with no change of version, the measurements made on 2026-10-11 before any production edit: P-1 for a close started in `toApp`, P-1a, P-1b, P-1c, P-2, P-3, P-5 and P-6, and an early reading of P-10 (§3 has each with its evidence, research/probes/b41_563_564/p0b_runs.output.txt and p0b_measurements.patch, parent repo). None disagreed. It also records decisions of the orchestrator: at the stack-arena overload and at the `onLogon` and `onLogout` sites the depth assertion is evaluated after the site's scope and before its callback (§2.2), which replaces what the F1 line below says was left, and is listed in §3 as specified without a witness; and NC-11 has a positive-control arm (§3). The passages these changed are in the header, §2.1 ("Writer"), §2.1a, §2.2, §2.5, §3 and §4. None of this was reviewed.
- On 2026-10-12 v0.7 also records, again with no change of version, the measurements made during implementation: P-7, P-10 on the mechanism, P-1 for a close started in `toAdmin`, P-8 and P-9 (§3 has each with its evidence, research/probes/b41_563_564/p2a_runs.output.txt, p3a_runs.output.txt and ph4a_runs.output.txt, parent repo); none disagreed, and none of this was reviewed.

### Round 1 — disagreements

- **F-8 (Codex, P3), "the entry-point census is mislabeled": not applied.** Codex says
  `Session::drive_reconnect` is not public. It is: it is declared in `Session`'s public section in
  `include/fixpp/session/session.hpp`, under a comment that says "Declared public for accessibility from
  engine.cpp's run_connect_loop". §2.1a's table has every public awaitable of the two headers, and its
  disposition column already separates the entry points an application calls from the ones the engine
  drives. The table is unchanged except for the `Engine::stop` row (R-5) and a clause on the
  `on_inbound_frame` row.

Three findings are applied, but not with the fix Codex proposed. The reasons are in §5:

- **F-1:** Codex proposed tracking the operation that called the callback and releasing the close at a
  safe boundary. The owner chose "after the callback returns" (R-4's reading).
- **F-2:** Codex proposed, as one option, tickets or an ordered completion queue inside `Session`. That
  conflicts with B35's ruling R-10. Its other option, a documented requirement, is R-6.
- **F-3:** Codex cited Articles XI and XV of the constitution for "threading state must be race-free".
  Neither says that. The finding stands on the note's own claim that cross-thread entry still asserts,
  and is applied on that ground (§2.2).

### Round 1 — where this revision goes beyond or beside the triage's fix shape

Stated so that round 2 can judge each one:

- **N-3, the send's hop.** The triage asks for a read of the cancellation state after the post. §2.1
  applies asio's whole test there (`throw_if_cancelled` set and a cancellation recorded), so that a chain
  which turned throwing off is treated as asio treats it.
- **N-11.** The triage offers "test `state_ == closing` in the re-check, or disclose it". §2.1 takes the
  test, and drops v0.3's Active re-check from the deferred path, because the deferred path ends by
  awaiting `Session::send`, whose own Active check makes a second one unreachable (a mutant of it could
  not fail).
- **F-3.** The triage's shape is an atomic owner token and an atomic depth. §2.2 uses the token alone
  for the cross-thread check and keeps both depths plain; the reason is in §5.
- **N-2.** The copy lives in the deferred path's frame, and that path ends by awaiting `Session::send` on
  the copy, through an entry that skips the depth test, so the ordinary send's body is not duplicated and
  a send that was not moved out of the callback nests instead of waiting again (§2.1, step 6).
- **Found by §4's searches, in neither review:** `B35SendSlot.Nested_ASendInsideToAppLeavesTheOuterFrameIntact`
  asserts that a send started in `toApp` runs inline and would fail under R-1 (NC-17); the fixpp#518
  block comment in `tests/session/test_session_plaintext_roundtrip.cpp`; `L-497-1`'s and `L-015-4`'s
  clauses; and the release-build cost of the two counters (§2.1, "Cost").

### Round 2 — disagreements

Codex findings and counter-proposals that are not applied as proposed:

- **F-1 (P1), a callback-exit latch: not adopted.** Ruled outside the contract (R-7); the triage judged
  the finding P2 on that ruling. Reasons in §5.
- **F-2 (P1), "state that `L-023-2` is closed": applied in part.** The bench and its `paired` row are in
  §3. The row closes for the send route only; the ruling keeps its establish-churn half open.
- **F-3 (P1), declare the C-ABI break and bump the version: not applied.** The finding took v0.4's
  premise as given; the triage found it false (N2-1) and the owner withdrew OD-6 (§2.4, P-11).
- **F-4 (P2), a test-access counter for single execution of the close body: not added.** NC-8's two arms
  need no new seam.
- **F-5 (P2), a test-only Engine hook that exposes the session in `onCreate`: not added.** Judged P3; the
  arm is deleted (§2.1a).
- **F-6 (P2), a C-ABI witness for the stop check: not added.** There is no C-ABI effect to witness (NC-10).
- **F-7 (P2), a verification matrix: applied as lines in §3,** at the triage's P3.

### Round 2 — where this revision goes beyond or beside the triage's fix shape

Stated so that round 3 can judge each one:

- **N2-2, where the inbound assertion sits.** The triage puts it at the entry of the inbound overload.
  §2.2 puts it at every function that builds a resource over the session's parse buffer, the inbound
  overload and `validate_inbound_`, because the validate gate parses a nested frame over the buffer
  before dispatch and can end the arm before the overload is reached. NC-19's validate arm and its mutant
  row are the witness.
- **N2-2, one case stays undetected** at this round, and §2.2's table says so: an emit that is not a
  send, begun inside an inbound or lifecycle callback. The triage's two assertions do not catch it and
  nor do these. Round 3's N3-2 added a second undetected case (below).
- **F-1, which sessions are exposed.** The triage describes the read pump's next completion running
  inside `fromApp` under `direct_executor`. By source, every session under the Engine adopts a strand the
  Engine made, whatever its `mode` (`Engine::start()` sets `engine_adopt_strand`; `Session::open()` uses
  it first), so the read pump is never in that position. §2.1 states the exposed setup as a `Session` an
  application drives itself, and says why the cell fixture's `ExecKind::io_context` is in the same
  position; NC-16's control arm therefore uses `ExecKind::strand`.
- **N2-4: disclosed, not extended.** The triage offers both; the reason is in §5, and §7 adds the send
  nested in the same Logout's `fromAdmin`.
- **N2-3, NC-9.** Arm (a) keeps an ordinary handshake and is stated as proving detection only. Arm (d)
  is the TSan arm, with a relaxed flag as the only ordering and a matcher that rejects a ThreadSanitizer
  report; P-7 says what follows if a plain token draws no report.
- **F-4, NC-8's second arm.** Both closes are graceful: with a terminal waiting close the mutant's second
  run of the body calls no hook and emits no Logout, and the arm could not fail.
- **F-2, the bench's isolation.** The triage's example is a send that `toApp` vetoes. §3 takes a payload
  that `build_send_frame_` refuses, so that the timed part ends before the frame build, and runs it on
  one thread, as the manifest's selection rule requires.
- **F-7, NC-4's fifth arm** asserts only that the send's `toApp` runs, which is what reading settles.
- **N2-2, NC-20** produces the triage's "timer emit" by advancing the mock clock inside the callback.
- **N2-9, §2.5's second added route.** The triage calls it a nested send on a chain that is already
  cancelled. A chain cancelled before the send's awaitable is awaited throws before `Session::send` is
  entered, so §2.5 states the route as a cancellation recorded before one of the nested send's own awaits
  begins.
- **Not in the triage:** each assertion has a text of its own and each death arm matches it, with the
  I-3 test's expected text edited; NC-3's mutant row; the coverage waiver registered in advance for the
  failing side of each new assertion.

### Round 3 — how each P3 was applied

Edits after convergence, applied by a sub-agent from the judge's fix shapes, one line per finding. None
was reviewed.

- **Codex F-1:** §2.2's token row states the order: the swap is the scope's first action and the clear
  its last.
- **Codex F-2:** the lease-counter assertion is not taken (owner). The snapshot read stays on §3's list
  of mechanisms without a witness, with the triage's reason.
- **Codex F-3:** §2.1a's row keeps the refusal for `open()` and states `drive_reconnect()`'s engine-only
  convention, with no run-time refusal.
- **Codex F-4:** §2.4 and §4 ("Compatibility") say that the widening is observable and is not a breaking
  change. The Compatibility bullet's opening clause is scoped to match: no call that succeeds today
  fails afterwards.
- **N3-1:** §3 ("Cost") has the same-tree spread check against the comparator's noise band. §4's table
  has rows for the bench gate's design record and for `MANDATORY_PAIRED`.
- **N3-2:** §2.2 has the second undetected nesting; §2.1a's row and the guard's B&L row point at the
  "not detected" rows where they named one case. The optional assertion is taken (owner), after a read
  of the routes to the `onLogon` and `onLogout` sites: both assert that the outbound depth is zero.
  NC-19 gains a lifecycle arm, the mutant table a row and P-10 the arm's prediction; the `onLogon`
  site's half has no death arm and is listed as specified without a witness.
- **N3-3:** §3 ("Cost") says that `session_send_bench` has no manifest row, names the dictionary the new
  bench needs, and names the scripted read's timer as a cost inside the timed part.
- **N3-4:** the fixture list gains the `onCreate` hook (NC-11) and the `Application` on
  `ValidateGateFixture` (NC-19), and, for the lifecycle arm, a frame builder that takes the BeginString.
  NC-16's control arm and P-10 say that the fixture's strand is the outer one, not the session's own.
- **N3-5:** §1 scopes "the only way in" to `Application` callbacks and names the store's synchronous
  part and the config-time `transport_send`; §2.1a's bullet covers a send as well as a `close()`.

### v0.7 — how each /analyze finding was applied

Edits applied by a sub-agent from the `/analyze` report and from the orchestrator's dispositions at its
end, one line per finding. None was reviewed. The report's line numbers are v0.6's.

- **A1:** §2.1 ("The payload") says that `Engine::send` copies the payload twice before `Session::send`
  runs and that the deferred path makes a third copy, as §2.3 and §7 have it.
- **A2:** §2.3's step 3 says that the rule has no witness and that `~Engine` already forbids the scenario.
- **A3:** §7 says that `L-023-2` stays open for establish-churn.
- **A4:** §3's UBSan line describes each depth as §2.1 and §2.2 do.
- **B1:** §3 ("Cost") fixes the order: the bench's commit, the production edits, the local run, the
  manifest's commit. §4's rows agree.
- **B2:** a mutant that stays green is settled before the verification record's verdict is written.
- **B3:** NC-11 says "close modes".
- **B4:** §2.1's step 5 names the check that refuses a send after a close has finished.
- **B5:** §3's introduction to the predictions says which measurements run on today's tree and which on
  a scratch build.
- **B6:** §3's first paragraph keys the arms on assertions and on `NDEBUG`, and says that every cell also
  passes in a whole-binary run.
- **B7:** NC-2, NC-3, NC-16 and NC-18 state each open parameter as the implementer's choice, with what
  constrains it.
- **C1:** NC-11 names its observable for "no liveness loop" (the mock clock's parked sleeps, with a
  non-zero heartbeat interval) and the route of the `Disconnected` write outside `close()` (a frame with
  another BeginString). The fixture list follows.
- **C2:** NC-13 names its ASan arm, the terminal close started in `fromApp`; the ASan line agrees.
- **C3:** §3 ("Cost") puts the in-tree benches under the route bench's rule, names the local run's
  command, and says what p1's base is.
- **C4:** NC-6 asserts its outer operation's result, and whether the store and the manager agree, as
  measured.
- **C5:** NC-7 says how its graceful arm ends the wait.
- **C6:** §3 names the close cells and excepts a row that fixes the transport or the store.
- **C7:** the coverage scope includes `src/session/session_engine_access.hpp`.
- **C8:** §2.1's table has a row for `onLogout` and `onCreate`, written from `Session::send`'s Active
  check and from the order of the write and the callback in `record_state_transition_`.
- **C9:** §3 says where NC-9's test access, NC-14's store double and NC-19's validate arm go, and where
  `check-comment-claims.py` is.
- **D1:** the header, §0a and §6 record the Appendix A controls as done, with the owner's words on the
  plan sign-off.
- **D2:** §0a and the first line under "Round 3 — how each P3 was applied" say who applied each revision.
- **D3:** §0a has the `[const §VII.3]` disposition.
- **D4:** the text route. §0a has the allocation disposition, and §2.1 names the copy's storage. Flagged
  to the owner.
- **D5:** §2.4 and §4 ("Compatibility") state the article's conditions. The re-derivation was read
  against the comments in both C-ABI headers before it was written.
- **D6:** §0a's Gate A bullet is in the past tense, and §0a has a bullet for the `[const §XVII.7]` build
  and the MSVC leg.
- **E1:** R-3 is referenced in §2.5, in NC-13's row and on the ASan line.
- **E2:** the text route. The default mode's immunity is listed as specified without a witness, and
  P-10's last column is reworded.
- **E3:** "Mutant first" names every exception, and NC-2's `fromAdmin` arm joins the second mutant row.
- **E4:** the sends R-2 admits, and OD-3's refusal on R-5's route, are listed as specified without a
  witness.
- **E5:** the row of `ToAppStartsClose_Terminal` has P-1a's arm.
- **E6:** the controls row names `ToAdminStartsSend_AssignFirstSite_Sequential`.
- **E7:** §0 says what OD-1, OD-3 and OD-4 are and where each is used; §2.1, §2.1a and §2.3 reference
  NC-2, NC-6, NC-1 and OD-4; §6 says what OD-2 was.
- **F1:** the text route, in part. §2.2 narrows the cross-thread claim: the parse sites and the
  stack-arena overload's entry read a depth before a token is reached. The report's wording that puts
  the stack-arena overload's assertion and the lifecycle sites' after their scopes is not applied: it
  moves an assertion, and the disposition says that none moves. Settled afterwards by the orchestrator
  (the `## Gate A` list): both are evaluated after their scopes and before their callbacks. By source
  each scope is constructed immediately before its callback, so neither assertion moves relative to
  its callback, and §2.2 now says so.
- **F2:** the coverage waiver covers the assertion at the `onLogon` site.
- **F3:** §2.1 says that the lifecycle sites do not write the outbound depth and that the `onLogon` and
  `onLogout` sites read it.
- **F4:** P-4's row has its arms as the probe ran them.
- **F5:** §0's OD-5 bullet says that the paired bench is single-threaded and replaces the foreign-thread
  probe as the gated evidence. §3 ("Cost") keeps that probe as a local step. §4's `L-023-2` text, §6 and
  §7 agree.
- **F6:** §0's R-5 bullet and §4 ("Compatibility") are scoped to the route R-5 sets aside.
- **F7:** §2.1 says that NC-16's fourth arm runs on `ExecKind::strand`.
- **F8:** the header, §0's paragraph on the record, §1's GapFill paragraph, the paragraph under §2.1's
  table, §2.5, §4's limitation bullet, the round-2 line on N2-2 and this section's line on v0.6 are
  corrected.
- **The measurements:** the rows of P-4 and P-11 record each result and its evidence files. The header,
  §0, §0a, §2.1, §2.1a, §2.4, §4 and §6 say what has been run. §2.1 has the cancellation that a later
  emission erases. §2.4 no longer says that the Active check bounds a send admitted during `stop()`.
- **From the task-list derivation:** NC-17's order assertion joins the first mutant row, and its
  assertion on the outer frame's bytes is named a pin. The GapFill and failure-exit branches of §2.1's
  first-yield table are listed as specified without a witness. §4's ambiguous "the same" row names
  `include/fixpp/session/session.hpp`.
