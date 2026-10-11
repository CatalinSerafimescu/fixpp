# fixpp#563, fixpp#564 — a send started inside an application callback (B41)

> **Status: v0.3 — draft for Gate A.** Issue mode: this note is the plan; there is no `specs/<id>/` bundle.
> Branch `563-564-callback-send`. Gate A has not run. v0.2 extends the rule from `Session::send` to every
> entry point that can begin an emit (§2.1a), after a graceful `close()` started inside an outbound callback
> was measured to reproduce the defect. v0.3 records ruling R-4 (every `close()` started inside a callback
> waits), which replaces v0.2's open decisions on the close.
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
>   script.
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
  stack, begins after the callback returns. So an arm never runs on a session that its own callback's
  `close()` closed, and a requested Logout is never emitted inside the caller. Chosen over a check after
  each inbound callback, and over leaving the arm to carry on as QuickFIX C++ and J do. It is QuickFIX/Go's
  shape; the owner preferred the session not to be left half-processed.

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
the frame build, the callback and `assign_outbound()`, to the store call, whose first statement is a post in
both `MemoryStore::store` and `FileStore::store`. `assign_outbound()` cannot suspend inside that stretch,
because no `SeqnumManager` method holds its mutex across a `co_await`. The order of assignment, the order of
store calls and the order on the wire are therefore the same unless a second send body enters the stretch,
and the only way in is a synchronous re-entry through the callback that the stretch itself calls: `toApp` or
`toAdmin`. The inbound callbacks are never inside such a stretch: at every arm the peek follows the
callback.

So the rule is not about which callback, and not about which emit site. It is: **no emit body on session S
may begin while S has an outbound callback on the stack.** R-1 is that rule for `Session::send`; §2.1a
applies it to the other entry point that begins an emit.

Re-derive the premises, do not trust this paragraph:
- the outbound callback invocations: `grep -n "application->toApp(\|application->toAdmin(" src/session/session.cpp`;
  each is inside a lambda passed to the stack-arena overload of `parse_and_dispatch_`;
- the lock scopes: every `co_await` in `src/session/seqnum_manager.cpp` is a lock acquisition or follows
  the end of its scope;
- the stores' first statement: `MemoryStore::store` in `include/fixpp/session/memory_store.hpp` and
  `FileStore::store` in `src/session/file_store.cpp`;
- the peek-then-callback order per site: for each `const seqnum_t … = seqnum_mgr_.peek_outbound();` in
  `session.cpp`, the first of `fire_to_admin_(`, `->toApp(` or `assign_outbound()` that follows.

## 2. Design

### 2.1 `Session::send`: one decision point (R-1)

**State.** `Session` gains one strand-confined counter, the outbound-callback depth. It is a plain integer,
not an atomic: it is written and read only on the session's serialisation domain, like `fsm_state_`. It
exists in every build type.

**Writer.** One place: the stack-arena overload of `parse_and_dispatch_`, which is the only route by which
`toApp` and `toAdmin` are invoked (`fire_to_admin_`, `send_impl`, `inbound_business_reject_leaf_`). An RAII
scope increments the depth before the callback and restores the previous value after it, on every exit,
including a throwing callback. The inbound overload (`fromApp`, `fromAdmin`) and the `onLogon`, `onLogout`
and `onCreate` sites do not touch it.

Writing it in the overload, not at the three call sites, is deliberate: a future emit that calls `toApp` or
`toAdmin` gets the guard without being told about it. The implementer confirms the overload has no other
kind of caller (`grep -n "parse_and_dispatch_(" src/session/session.cpp`, read each).

**Reader.** `Session::send`, after its Active check and before it takes the frame slot:

- depth is zero: nothing changes. The send proceeds inline as today.
- depth is non-zero: the send awaits one `asio::post` onto the session's executor (`asio::deferred`), then
  re-checks `fsm_state_ == Active` and returns `session_invalid_state_for_send` if the session left Active
  while it waited. Then it proceeds as an ordinary send.

**Why the wait is long enough.** The posted continuation runs only after the handler that held the
callback has yielded. Where the outer emit first yields depends on the branch `store_then_emit` takes, and
on every branch the outer frame's number is assigned, and its bytes are handed on, before that yield:

| Branch of the outer emit | Its first yield | What precedes that yield |
|---|---|---|
| a shipped store (`MemoryStore`, `FileStore`) | the post that begins the store call | the assign, and the store call itself |
| a `MessageStore` whose `store` completes without yielding, or no store at all (`store_` is null when no factory is configured) | the live write: the write gate if a write is in flight, else the transport's write, which holds the gate | the assign, the completed store call if there is one, and the gate request |
| the same, with a synchronous `transport_send_` | none: the emit completes inside the handler | everything |
| a failure exit (a veto, a throwing callback, a failed assign) | wherever the exit path yields | the outer frame is abandoned; nothing of it is stored or sent |

So the deferred send peeks after the outer assign; its store call follows the outer one; and its request for
the write gate follows the outer one, because the handler that completes a store goes on to request the gate
before it yields again. A deferred send is then exactly an overlapping send. Three premises carry the rest,
each already relied on by two sends that overlap today: a store completes its calls in the order they were
made (B&L `L-518-1` names this premise for the shipped stores; `FileStore::store` takes its FIFO mutex on
the strand before it offloads); the write gate grants in arrival order (`async_mutex`'s FIFO grant); and an
uncontended `assign_outbound()` does not yield (§1). Measured: `OverlappingSends` and
`OverlappingSends_FileStore`, `OverlappingSends_NoStore` and its live-write twin, and the post controls of
the nested cells on both stores and with no store.

**Cancellation at the hop (OD-1).** The post is a new await, so it is a new point where a cancelled chain
throws. A send that is cancelled while it waits has consumed nothing: no number, no store slot, no frame.
It returns an error and leaves the session's state alone. It must not take the existing
`operation_aborted` → `Disconnected` conversion in `Session::send`, which is for a send that was in
flight. This is the hazard that made ruling R-10 reject a session send gate (`.specify/544-hot-path-zero-alloc.md`
§2.5): there the new await sat on every send; here it sits only on a send started inside an outbound
callback. The error value is OD-1.

**The frame slot.** The holder is taken after the hop. The outer send still holds the slot when a send
deferred from its `toApp` starts, so that send takes `send_fallback_leaf_`, as a send nested in `toApp`
does today. A send deferred from `toAdmin` finds the slot free.

**Where the hop lives.** In its own small coroutine, which `Session::send` awaits only when the depth is
non-zero. `Session::send`'s own frame and the position of its first cancellation check, both of which B35
measured, are then the same as today on the path that does not wait.

**Cost.** A send that is not started inside an outbound callback pays one integer load and a branch. A send
that is pays one post and that coroutine's frame: probe p1 measured a post on the reply path at +210 to
+270 ns on the Engine's session executor type, with no call to `operator new` or `aligned_alloc`. B35's
zero-allocation gates must stay green (§3).

**What an application observes.**

| Send started in | Before (`NDEBUG`) | After (every build type) |
|---|---|---|
| `fromApp`, `fromAdmin`, `onLogon` | nested | nested: numbered, and its `toApp` run, inside the callback |
| `toApp`, `toAdmin` | nested; one number on two frames, or frames out of order | starts after the callback returns; numbered after the frame whose callback started it; transmitted after it |
| outside any callback | inline | unchanged |

On a build with assertions the left column was an abort in every row.

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
| `Engine::stop` | no emit; it reaches `Session::close` in terminal mode | covered at `Session::close`; `stop()` itself is not changed (§7) |
| `Session::open`, `Session::drive_reconnect` | the initiator's Logon | engine-driven; refused or meaningless on a session that is already open; not an application entry point from a callback |
| `Session::on_inbound_frame` | reply emits | fed by the read pump; an application that feeds frames from inside a callback is outside the contract |

**`Session::close`, both modes (R-4).** At its entry, for every caller, `close()` waits by one post onto
the session's executor while the depth of all callbacks of that session is non-zero, and then proceeds as
it does today. The check is in `close()` itself, not at the application's call, so it also covers the
terminal close that `send_impl` awaits when a nested send's `toApp` throws, and the terminal close
`Engine::stop()` reaches when an application starts it from a callback.

What it does for the two modes:

- **Graceful.** Measured without the wait: started inside `toApp` or `toAdmin`, its Logout takes the outer
  frame's number at a callback-first site and goes out ahead of the outer frame at an assign-first site,
  and the outer frame is then transmitted after our own Logout. With the start posted, the outer frame goes
  first and the Logout follows, both retained. The wait also keeps the Logout's `toAdmin` off the caller's
  stack in `fromApp`, where order was not at risk.
- **Terminal.** It begins no emit. Without the wait it closes the session under the callback's feet: the
  session is `closed_drained`, and `onLogout` has run, before the call returns, and the arm that called
  the callback resumes on it (§2.5). With the wait, the close reaches that arm the way a close from another
  thread does: while the arm is suspended, or after it has finished.

What the three QuickFIX engines do, read: an application's `logout()` sets a flag (C++, J) or queues a
request (Go), and the engine's own loop generates the Logout later, so it is never emitted inside the
caller. `disconnect()` in C++ and J runs inline, `onLogout` included, and the inbound handler then carries
on with the frame. Go has no inline close: a stop is queued to the session's loop. R-4 follows all three
for a graceful close and follows Go for a terminal one; the departure from C++ and J is that `onLogout`
runs just after the callback, not inside it.

The wait must not be a way to skip the close. It runs with cancellation shut out, as 093 did for the
store operation of its reset unit (plan OD-25): a close that was asked for still closes. A close that
finds the session already closed when it resumes returns as a repeated close does today.

### 2.2 The debug guard keeps its meaning (fixpp#564)

`callback_dispatch_scope` asserts today that no callback is on the stack when one is entered. That
conflates two things: a second thread entering a session's callbacks, which is the misuse the assertion
names (a `direct_executor` attested over an executor that does not serialise), and a callback entered
inside another on the same thread, which is what a QuickFIX-style reply does. Its destructor also clears
the flag unconditionally, so a nested scope's exit would clear the outer scope's mark.

The guard becomes depth plus owning thread:
- entering with depth zero records the current thread as owner;
- entering with depth non-zero asserts that the current thread is the owner;
- leaving restores what was there on entry; the owner is cleared when the depth returns to zero.

Cross-thread entry still asserts. Same-thread nesting passes. The guard `dispatch_app_callback` carries
gets the same shape. The members exist in every build type, as `in_dispatch_` does today, so the class has
one definition; only the checks compile out under `NDEBUG`.

Two counts, then, both strand-confined plain integers that exist in every build type: the depth of all
callbacks, which the guard's checks read under assertions and `close()` reads always (§2.1a);
and the depth of outbound callbacks, which `Session::send` reads (§2.1). The owner-thread check is the only
part that compiles out.

### 2.3 `Engine::send` through the snapshot (R-2)

Today `Engine::send` enrols in `send_counter_`, re-checks `stopped_`, hops onto `control_strand_` to read
`stopped_` and `registry_`, then hops onto the session strand. Each hop is a `co_spawn`, which starts with
a dispatch: the first runs inline only when the control strand is idle, and that is the timing dependence.

`reader_snapshot_` already publishes `SessionId → shared_ptr<Session>` for every published session, rebuilt
on the control strand after each control-plane mutation and read by `Engine::lookup` without the strand.

**The new path.**

1. Enrol in `send_counter_` and construct the guard, in the outer frame, before any await. Unchanged.
2. Re-check `stopped_` with `seq_cst`; if set, return `session_invalid_state_for_send`. Unchanged.
3. Load the snapshot and look the id up.
4. **Hit:** take the `shared_ptr<Session>` as the keepalive and `co_spawn` onto that session's executor.
   There: enable total cancellation as today, apply the stop check of OD-3, check `state() == Active`,
   then `co_await session->send(payload)`.
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
§3 pins this with a cell per role, because it is a property of two role loops, not of this function.

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
This note claims no speed-up: no bench covers `Engine::send` (B&L `L-023-2`). §3 measures the difference
and reports it.

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
| A send that reaches the control strand after `stop()` began fails | the second `stopped_` read, on the control strand | OD-3 |

The first six do not depend on the hop. The seventh is the one the hop provided, and only when the hop
happened to run in one of `stop()`'s suspension gaps: a send whose control-strand step ran before `stop()`
set the flag already reaches `Session::send` during teardown today. So removing the hop widens a window
that exists; it does not open one. OD-3 closes it more tightly than before.

`stop()`'s step order, the publish and unpublish sites, the snapshot's publisher and `Engine::lookup` are
not changed.

### 2.5 A session closed from inside one of its own callbacks

Measured on an `NDEBUG` build before this change, with no nested send involved: a terminal
`Session::close` started in `fromApp` or `fromAdmin` runs inline to `closed_drained`, and `onLogout` fires,
before the call returns. The inbound arm then resumes on the closed session: it reports success, or
`session_already_closed` where it goes on to emit. With a FileStore the close reports completion before the
inbound arm has finished. A nested send whose `toApp` throws reaches the same state, because that branch
awaits a terminal close.

R-4 removes that state at its source: no `close()` begins while a callback of the session is on the stack,
so the arm that called the callback is never resumed on a session its own callback closed. The close then
meets the arm as any close does that arrives while an arm is in flight, which is the case the session is
already built and tested for (`Session::close`'s drain of the seqnum manager and the write gate, the store
quiescence it waits for, 093's rules for the Logon arms).

**Not a memory-safety defect, as far as reading goes.** Nothing that owns a Session can free it while one
of its inbound arms is on the stack:
- under the Engine, each role loop holds its own `shared_ptr` to the Session (`local_session` in
  `run_accept_loop` and `run_connect_loop`) across `run_read_pump`, and the arm runs inside the pump;
  `stop()` joins the role loops before its terminal closes and before it clears the registry;
- the C ABI's engine shell keeps its sessions for its own lifetime;
- a caller that drives a raw `Session` owns that obligation already (B&L `L-015-4`, `L-544-2`).

So a sanitizer cell would come back clean by construction and decides nothing about lifetime. The
sanitizer runs of §3 stay, as a net under paths that assertion builds could not reach before §2.2.

**What R-4 does not cover.** A nested send can leave the session `Disconnected` without calling `close()`:
`Session::send` writes that state inline when `assign_outbound()` reports the sequence number overflow.
Started in an inbound callback, that happens inside the callback, and the arm resumes on a disconnected
session. It needs a MsgSeqNum at its maximum; it is disclosed (§4), not changed.

## 3. Tests and gates

Every cell runs alone (`--gtest_filter`), on `linux-clang-debug` and `linux-clang-release`. "Classification"
is the cells' own three-way witness: nested, deferred, never ran. A cell that expects one classification
fails on either of the other two.

**Cells that must turn** (they exist on the branch):

| Cells | Must report |
|---|---|
| `CallbackSend.ToAppSendsNested*`, `ToAdminStartsSend`, `ToAdminStartsSend_AssignFirstSite`, `…_FileStore`; `EngineCallbackSend.EngineSendFromToApp*` | deferred; distinct consecutive numbers in transmit order; each frame retained under its own number; store and manager agree; both outcomes ok |
| `CallbackSend.FromAppSendsReply`; `EngineCallbackSend.EngineSendFromFromApp*` | nested; pass on the debug build too |
| `EngineCallbackSend.MtEngineSendFromFromApp_Quiet`, `…_BusyControlStrand` | every reply nested, in both arms |
| `CallbackSend.ToAppStartsClose_Graceful`, `ToAdminStartsClose_Graceful`, `…_AssignFirstSite`, `FromAppStartsClose_Graceful` | the close's Logout after the callback returned: its `toAdmin` not on the starting callback's stack; the outer frame first, then the Logout; distinct consecutive numbers; both retained; pass on the debug build too |
| the `…_NoStore` and `…_NoStore_LiveWrite` nested cells | deferred; distinct consecutive numbers in transmit order |
| `FromAppStartsClose_Terminal*`, `FromAdminStartsClose_Terminal` | inside the callback after the start: the session still Active and open; `onLogout` after the callback returned; the session closed at the end; pass on the debug build too |
| `ToAppStartsClose_Terminal` | the outer frame transmitted and retained, its send ok; then the close |
| `FromAppSendsReply_NestedToAppThrows*` | inside `fromApp` after the start: still Active; `onLogout` after `fromApp` returned; the send returns `app_callback_threw`; the session closed at the end |
| every control (`Sequential*`, `OverlappingSends*`, `TestRequestThenSend`) | unchanged |

The cells' names and their "expected classification" asserts are re-decided when they are lifted: a cell
named `…Nested` that must report "deferred" is renamed.

**New cells.**

- `Engine::send` from `onLogon`, initiator and acceptor: nested, and the send succeeds. This pins the
  premise of §2.3 that a session is published before it can be Active.
- `Session::send` from `onLogon` and from `fromAdmin`: nested.
- A send to session B from a callback of session A, A's `toApp` included: not deferred by A's depth.
- A deferred send cancelled while it waits: returns the OD-1 error, the session stays Active, nothing is
  transmitted, no number is consumed.
- A deferred send whose session leaves Active while it waits: returns `session_invalid_state_for_send`.
- A terminal close started in `toAdmin`, at a callback-first site and at an assign-first one: the outer
  frame is transmitted and retained first, no frame follows the close, and the store and the manager agree.
- A close that waits, in each mode, with the chain cancelled meanwhile: the session still closes.
- A close that waits while a second close, from outside the callback, runs to completion: the waiting one
  returns as a repeated close does.
- `Engine::stop()` started from a callback: it completes, and the callback's arm is not resumed on a
  closed session.
- A graceful close started in `fromApp` on a session with no store, and with a FileStore: the Logout after
  the callback in both (today the first is inline and the second is not).
- The guard: two threads entering one session's callbacks still assert (a death cell); a nested callback
  followed by a second, sequential one does not (the restore-on-exit arm).
- `stop()` against the hit path: the OD-3 cell, a send whose session-strand work starts after `stop()`'s
  first step ran on that strand returns `session_invalid_state_for_send`.

**Arms that must go RED** (each run in a scratch copy, never in the PR's tree):

| Mutant | Must fail |
|---|---|
| the depth check removed from `Session::send` | the `toApp` and `toAdmin` cells, at both kinds of site |
| the depth counted at the inbound overload too | `FromAppSendsReply` and `EngineSendFromFromApp*` (they must report nested) |
| the scope's exit setting the depth to zero | a cell with a `toAdmin` inside a `toApp`'s deferred window; written for this arm |
| the Active re-check after the hop removed | the "leaves Active while it waits" cell |
| the close's wait removed | `ToAppStartsClose_Graceful`, `ToAdminStartsClose_Graceful*`, and the "not on the callback's stack" witness of every `…StartsClose_*` and `…NestedToAppThrows*` cell |
| the close's wait limited to graceful mode | the terminal cells and the `…NestedToAppThrows*` cells |
| the close's wait limited to outbound callbacks | `FromAppStartsClose_*`, `FromAdminStartsClose_Terminal` |
| the close's wait made cancellable | the cancelled-close cells |
| the hop's cancellation routed into the `Disconnected` conversion | the cancelled-deferred-send cell |
| the hit path replaced by the control-strand hop | `MtEngineSendFromFromApp_BusyControlStrand` (it reports deferred replies) |
| the guard's owner check removed | the two-thread death cell |
| the guard's restore replaced by a clear | the nested-then-sequential cell |
| the OD-3 check removed | the OD-3 cell |

A mutant that stays green is a finding about its cell, and the cell is fixed or deleted before sign-off.

**Existing tests that pin this path and must stay green:** the `EngineSessionStrand` suite
(`tests/session/test_engine_session_strand.cpp`: the send-versus-stop cells, the post-drain cell that
requires exactly `session_invalid_state_for_send`, the snapshot readers), the `ApplicationEngineSend`,
`ApplicationOutbound` (the two cells that pin the unknown-id and not-established results) and
`ApplicationStrand` suites, the C-ABI send tests, and the interop conversation cells. The shipped
`Reentrant*NoDeadlock` cells wrap their nested send in `asio::post` and so do not exercise this contract;
they stay as they are.

**Sanitizers.**
- TSan: the `EngineSessionStrand` suite and the two `Mt*` cells. The hit path reads the snapshot off the
  control strand from io threads, which is new for `Engine::send`.
- ASan: every cell above, and the cells of §2.5 (`FromAppSendsReply_NestedToAppThrows*`,
  `FromAppStartsClose_*`, `FromAdminStartsClose_Terminal`), which the preset could not reach before §2.2.
  The disabled control cell in the test file shows the instrument reports on this binary. By §2.5 no owner
  frees the Session in these cells, so a clean run shows the absence of other faults on these paths and
  says nothing about lifetime.
- A clean sanitizer run is stated as what it drove, not as absence.

**B35's gates.** The zero-allocation gates on the Active inbound read and on `Session::send`
(`tests/alloc_guard/`, the `test_544_*` cells, the mallocnesia population check) are re-run. The path
measured by them does not take the hop.

**Cost.** Probe p1 is re-run on the candidate for the reply path: the nested arm must not be slower than
its base beyond the probe's own run-to-run spread. A second probe times `Engine::send` from a foreign
thread, base against candidate, and its result is reported whichever way it falls. The paired bench of
Article VIII §2 runs for the rows that exist.

**Local gates before the first labelled push:** every `ci-script-pins` step, every Tier 1 source-audit job
(the `co_spawn` closure audit and its census reconciliation read `engine.cpp`'s spawn sites, which change),
`check_line_citations.py --shift-audit origin/main..HEAD` (023's bundle cites `engine.cpp` by line), and
`check-comment-claims.py`.

## 4. Disclosures, documentation and catalogue

**B&L, live file.**
- New rows: the `Session::send` contract of R-1 as an application observes it (the table in §2.1); a
  `close()` started in a callback, with the departure from QuickFIX C++ and J that `onLogout` follows the
  callback (§2.1a); the `Engine::send` path of R-2, including the cold path and what it returns; the
  reshaped guard; the residual of §2.5 as a limitation.
- `L-544-3` and `L-544-4` move to the closed file.
- `B-019-3` ("enqueued behind the current dispatch") and `B-023-2` ("`send` traverses
  caller→control→session") describe behaviour this PR changes; each is rewritten in place.
- `L-054-1` stays: the blocking C-ABI send from inside the receive callback still deadlocks, because the
  wrapper blocks the strand on its own completion. Python's refusal (`B-055-2`) stays.
- `L-023-2` stays open unless the second probe is promoted to a bench.

**Headers.** The re-entrancy sentence in `Engine::send`'s doc comment; the `send()` line of the reentrancy
contract in `session.hpp`; the comment on `callback_dispatch_scope`. `engine.cpp`'s header comment names
this note as superseding 023's two-hop send, so a reader who starts at the code is routed here.

**Design documents cited by line elsewhere** are amended by an erratum appended at the end, never by an
insertion: `.specify/2d-threading.md` ("the send does *not* run before `fromApp` returns").

**Frozen bundles are not edited.** `brain/components/session.md` gains a section for this decision and
flags, in place: `specs/019-app-callbacks` (FR-006, its re-entrant-send edge case, research D6);
`specs/023-engine-session-strand` (FR-012, contract C-0's "every cross-thread entry point enters the
control-plane domain", C-2's "never an inline `dispatch`-that-runs", data-model E-0, research D0's send
route). 023's research weighed "a lock-free registry snapshot for `send`" and deferred it "as a possible
optimization … revisit only if the bench gate fails"; R-2 adopts it for a different reason, and the page
says so.

**Issues.** The PR closes fixpp#563 and fixpp#564. Their acceptance item 3 (measure `Engine::send`, then
make the `engine.hpp` sentence match) is met by the record and §4.

**Compatibility.** `Session`'s layout changes (a counter, the guard's members): a C++ layout change before
the first release. The C ABI's symbols, types and error codes do not change.

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
- **A session send gate** (an `async_mutex` around the whole send). Already rejected by ruling R-10 for
  B35; nothing here changes its reasons.
- **Write the depth at the three call sites instead of in the overload.** A fourth outbound emit added
  later would silently lack it.
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

## 6. Open decisions for Gate A and the owner

- **OD-1 — the result of a deferred send cancelled before it starts.** Proposed: `dispatch_aborted`,
  returned from before the existing conversion, with no state change. The alternative is a new error
  value, which touches the error taxonomy and the C ABI's translation table.
- **OD-3 — the stop check on the hit path.** Proposed: in the session-strand body, read the session's
  engine-stop flag, which `stop()`'s first step sets on that strand before it emits cancellation, through
  `session_engine_access`. It is ordered with `stop()` by the strand, not by an atomic read that can go
  stale between the check and the send. Alternatives: read `stopped_` there with `acquire` (the
  check-then-act gap 093's OD-15 rejected for the same flag), or no second check (the Active check and the
  drain already bound what such a send can do).
- **OD-4 — `Engine::send` from a callback of an unpublished session.** By §2.3 it cannot be admitted. If
  Gate A finds a route to Active before publication, the miss path's timing dependence is back for that
  route and R-2 is not met there.

## 7. What this does not decide

- A live send that interleaves with a resend replay. A deferred send can start while
  `replay_outbound_range_` is suspended, as any overlapping send can today. Read, not run; QuickFIX/Go
  added a lock for it. Not changed here; a candidate issue.
- The C ABI's and Python's send from a callback (`L-054-1`, `B-055-2`).
- A session send queue, a bound on overlapping sends, or their back-pressure.
- `Engine::stop()` itself. Started from a callback, its terminal closes wait under R-4; its own steps on
  the control strand are not examined here.
- A bench for `Engine::send` (`L-023-2`).
- Whether `register_session` should republish the snapshot; two comments in `engine.cpp` and `engine.hpp`
  disagree with the code about it. Noted, not changed.

## Gate A

Not run.
