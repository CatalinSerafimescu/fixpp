# Data model: 105 transport and liveness

**Feature**: [spec.md](spec.md) · **Decisions**: [research.md](research.md) · **Contract**:
[contracts/transport-liveness-105.md](contracts/transport-liveness-105.md)

Phase 1. State and entities this feature adds, changes or removes. Names are proposals for the implementer; the
observables are fixed by the contract, the names are not. No member is described by its size or offset: re-derive
the layout from the header.

## E-1. Ending class

A `Disconnected` write is classed by the **root** it runs under (research D1, "The census, by execution
context"). The class fixes who closes the connection and when.

| Class | Roots | Who closes | When |
|---|---|---|---|
| **P — pump frame** | `run_read_pump` (every frame, the acceptor's first frame included) | the pump: `stop_pump` → `close(terminal)` | after the frame's arm returns: after its reply, persist and receipt reset unit |
| **D — detached root** | `run_liveness_loop`, `run_peer_logout_bound_`, `run_ending_logout_bound_` (E-3) | `close_transport_`, spawned at the root's exit; on an Engine session the pump then runs `close(terminal)` | at the root's exit when no frame is inside an arm; otherwise the pump, after that arm |
| **A — application call** | `Session::send` and `Engine::send`'s session body (`send_body_`, `send_deferred_`), `Session::drive_reconnect`, `Session::open` (a direct initiator's Logon) | as class D | as class D |
| **W — ending Logout** | any of the above, where the ending writes a Logout (R-1, every R-2 Logout, the replies to the peer's Logout; E-3) | as E-3's "Who closes" column: the pump after the response, the bound's close frame at its expiry (E-3 rows 10, 11), at the write limit (row 4) and at its failure exit (row 16), the closer for `close()`, `Engine::stop()` and the drain | at the peer's Logout, at the bound, or at the write limit; at the bound's failure it closes when the bound fails, not at its deadline, by the close its phase's deadline makes (row 16) |
| **X — excluded** | `Session::close` and `run_logout_phase1` under it; `drain_for_reconnect_` under `drive_reconnect` | the close or drain that owns it, as today | as today (B-103-9's bounded waits kept) |

A write reached from several roots takes the class of the root it runs under at that moment. The census table in
tasks.md lists each write site with every root that reaches it.

## E-2. The connection-end latch (`Session`, private)

| Field | Type | Written by | Read by |
|---|---|---|---|
| `reconnect_drain_in_flight_` | `bool` | `drive_reconnect`, around its `drain_for_reconnect_` call only | `owe_connection_end_()`, the bound's live predicate (E-3) |
| `connection_end_owed_` | `std::optional<std::uint32_t>`: the `conn_gen_` the close is owed on | set: `owe_connection_end_()` only; cleared: `take_connection_end` (pump), `release_connection_end_` after its spawn or its fallback close, the ending bound at its expiry's spawn (E-3 rows 10, 11) and at its failure exit's (row 16), `close()` at its commit of `closing`, `drain_for_reconnect_` | the pump after each frame; each class D and A root at its exit; the ending bound at its expiry |
| `connection_close_counter_` | `std::shared_ptr<std::atomic<int>>` | the close frame's join parameter (+1 at its construction, −1 at its destruction) | `close()` and `drain_for_reconnect_` (join by post, twice: before their own transport close, and again after they join the ending bound's counter; I-4); `~Session` (assert 0) |

**The one setter**, `owe_connection_end_() noexcept`: sets `connection_end_owed_ = conn_gen_` when
`state_ == lifecycle::open && !reconnect_drain_in_flight_` and **no wait armed before Active is open**
(`!(ending_wait_.phase == armed && !ending_wait_.from_active)`), else does nothing. A wait armed after Active does
not refuse it: the call ends that wait (`phase = resolved`) and owes (Gate A round 2, root cause #1; E-3). Its
callers (a census row and a mutant each):
1. `record_state_transition_`, on a transition **into** `Disconnected` from another state only, using the prior
   state the function captures at its entry. The call sits in the existing `new_state == fsm_state::Disconnected`
   block beside `wake_peer_logout_bound_()`, **above** the `engine_.application == nullptr` return, so a session
   with no application owes its close too (093 placed `reached_active_` above that return for the same reason). A
   write of `Disconnected` over `Disconnected` owes nothing. The order within the edge: the owe (which, during a wait
   armed after Active, sets `phase = resolved`) and `wake_peer_logout_bound_()` run first, synchronously, then
   `onLogout` fires. So an `onLogout` callback that calls `close()` finds the latch set and any wait armed after Active
   resolved; `close()` clears the latch at its commit of `closing` and owns the one close (C-1.a; the cell
   `ConnectionClose105.OnLogoutCallsClose_OneClose`).
2. The bound at its expiry, or at its failure exit, for a wait armed before Active (the session is `Disconnected`
   already; E-3 rows 11 and 16).
3. The pre-Active response, the peer's Logout during a wait armed before Active (E-3 row 9).

**The close frame.** `release_connection_end_() noexcept` decides synchronously, on the strand: the latch is set,
its generation is `conn_gen_`, `state_ == lifecycle::open`, `inbound_depth_ == 0`, and no ending Logout is being
written (`ending_wait_.phase != writing`; E-3, "Other roots during `writing`"). It then constructs the
frame's join (`connection_close_join`, the shape of `peer_logout_bound_join`: it increments the counter at
construction and decrements it at destruction, so a frame that is never published releases it), takes
`live_transport_shared_()`, and spawns the static coroutine `close_transport_(join, std::shared_ptr<Transport>)` on
`exec_.underlying()` (for an Engine session the session strand, `SessionEntry::session_strand`, on which the pump, the
liveness loop and the transport's operations run; for a direct Session the executor the caller drives the Session on)
inside a `try`, and clears the latch after `co_spawn` returns. **A spawn that throws fails
closed** (Gate A round 3, root cause #1; clarification D-g): the `catch (...)` calls the synchronous
`Transport::close()` of the transport it took, on the strand, and clears the latch; the join it constructed is
destroyed unpublished, so the counter stays balanced and the peer still observes EOF with no later call. On the plain
transport that is the same close. On TLS `asio_tls_transport::close` sends close_notify only when no SSL operation
is suspended and otherwise closes the socket without it, so with the pump parked in a read the close is abortive
(L-105-h). The join-as-parameter half follows `on_inbound_active_too_high_logout_`'s spawn; the `catch` half does
not: that precedent restores the mark and rethrows (`throw;`), while this `catch` must not rethrow (the function is
`noexcept`). The liveness loop's spawn sites, whose `fetch_add` before a bare `co_spawn` leaks the count on a throw,
are not copied. The frame awaits `close_async()` on the transport it holds and touches no member of `Session`. It
is joined by `close()` and `drain_for_reconnect_` (I-4), and `~Session` asserts its counter is zero, so a caller
that keeps B-103-13's precondition (`close()` before the Session is destroyed) never destroys a Session with the
frame running; the frame widens no lifetime precondition.

The same frame, with the same join and the same fallback, is spawned by the ending bound at each of its deadlines
(E-3 rows 4, 10 and 11), and at its failure exit (row 16, which closes when the bound fails, not at its deadline, by the close its phase's deadline makes), testing
neither `inbound_depth_` nor the `writing` deferral. At the write limit (row 4) it
tests only the generation and a live transport, because the arm in flight is the one suspended in the write. At the
bound's expiry (rows 10 and 11) it tests the generation, a live transport and `state_ == lifecycle::open`, then
clears the latch. These are I-2's exceptions.

**Invariants.**
- I-1. At most one consumer acts per set: every consumer tests and clears on the session strand.
- I-2. A class D or A consumer acts only when `inbound_depth_ == 0`, tested at the spawn, not when the frame runs.
  That is enough because a frame that enters after the edge finds the session `Disconnected` with no wait armed and
  is drained by the `Disconnected` row, with no store work. **The exception: the deadlines of E-3** (the write limit, row 4, and
  the bound's expiry, rows 10 and 11; Gate A round 3, root cause #2; and the bound's failure exit, row 16, which
  makes its phase's deadline close when the bound fails, not at the deadline; Gate A loop 2 round 1, root cause #1): each closes the transport while an arm may be
  suspended, in an ending Logout's write, behind it at `write_gate_`, or (rows 10, 11) in a non-ending write such
  as a resend answer during the wait. No store work is cut by them: a store call completes before its frame's transmit (durable-before-transmit, `store_then_emit`'s I-3),
  and the receipt reset unit is store-only; the transport close only makes the pending writes complete with an
  error. A root that is not a deadline (E-3 row 12) gets no such exception: while an arm is blocked in a non-ending
  write it defers to that arm, and the connection closes when that write returns (L-105-f). A deferral to the pump is
  consumed on every pump exit, not only after a returned frame: `on_inbound_frame` reports an arm's failure by its
  result, never by a throw past the pump, and every exit of `run_read_pump` (its error return, `expire()`, the
  over-limit frame, a read ended by cancellation or by the transport's close) runs `close(terminal)`, which clears the
  latch at its commit of `closing` and closes the transport (re-derive in `run_read_pump`: every `co_return` follows
  `stop_pump`, `expire()` or `frame_too_large`).
- I-3. The close frame's only effect is `close_async()` on the transport taken at the spawn; it never reads `this`.
- I-4. `close()` never runs its own `close_async()` while a close frame spawned before its first join is in flight
  (the first join). A write-limit frame (E-3 row 4) can still be spawned after that join, while `close()` waits for
  the ending bound; if it is running at `close()`'s own `close_async()`, one of the two calls is subsumed
  (`asio_tls_transport::close_async` sets `state_ = closed` before it suspends and returns at once once it is
  closed, "Idempotent, exactly like close()"). `close()` and `drain_for_reconnect_` therefore join
  `connection_close_counter_` a second time, after the ending bound's counter: the bound spawns any row-4 or row-16 frame
  before it exits, so the second join collects it, and `~Session`'s assert holds after `close()` (Gate A round 3,
  N-3). `Engine::stop()` reaches every Session through `close()`: its step 2 dispatches the transport's close, its
  step 4 runs `Session::close(terminal)` on the session strand after the loops' join (the step list of `Engine::stop`
  in `src/session/engine.cpp`), so these joins and `~Session`'s assertions hold for every Engine session, a C-ABI one
  included (`fixpp_engine_destroy` drives `Engine::stop()`; contract C-9). `~Session`'s assertions are Debug-only; in
  a release build the guarantee is B-103-13's precondition (spec FR-016).
- I-5. **One effective close per connection.** The latch is owed at most once per connection: the edge owes only on
  a transition into `Disconnected` (once per connection), and the wait's two direct callers fire only for a wait
  armed before Active, where no such transition follows. A later `close_async()` on the same transport (the pump's
  `close(terminal)` after a write-limit, bound-expiry, failure-exit or fallback close, `Engine::stop()`'s step 2) is subsumed, as
  F-5 describes; after the synchronous fallback (E-2) the transport is closed too, and both calls are idempotent. This is
  a census row, not a cell: every `Disconnected`-over-`Disconnected` write runs in the synchronous chain of the
  first transition, before any consumer, so no peer-visible observable distinguishes it (Gate A round 2, N-3). Its re-derivation recipe is tasks.md T006's:
  each such write is recorded as a census row with its synchronous chain.
- I-6. **At least once on an Engine session.** Every set is consumed by a consumer that runs after it: the pump after
  the frame or on its exit (I-2), the root's own release at its exit (class D and A), the bound at its expiry or failure
  exit (E-3), `close()` at its commit of `closing`, or the drain at its entry. A deferral hands the latch to a consumer
  that tests it later: `inbound_depth_ != 0` to the pump after that arm (I-2), `writing` to the writer's root after the
  write (E-3, "Other roots during `writing`"), each bounded by the write limit or the arm's return. The census table of
  tasks.md (T006) is the control: every setter path's row names the consumer that runs after it.
- I-7. **The generation.** `conn_gen_` is the existing per-Session connection counter, incremented in
  `install_reconnected_transport`, the one place a Session's connection is replaced. The latch stores it at the set;
  `release_connection_end_`, the bound's spawns (rows 4, 10, 11, 16) and the bound's live predicate compare it. A stale
  close cannot reach a new connection: a set of connection n is refused by the comparison once n + 1 is installed, and
  a close frame of connection n already spawned holds connection n's transport by its `shared_ptr` and closes only that
  one. `drain_for_reconnect_` joins both counters before `install_reconnected_transport` runs, so no frame of
  connection n is in flight at the increment, and no cell is owed for the stale case.
- I-8. **The io_context.** Both frames run on the session's executor, so the io_context outlives them only by the
  preconditions that already hold: `close()` (or `Engine::stop()`) joins both frames before the Session is destroyed
  (B-103-13), and a Session holding a strand does not outlive its io_context. Destroying the io_context while either
  frame is suspended is not supported; no cell does it.

**Session-engine access.** `session_engine_access::take_connection_end(Session&) noexcept -> bool` (test and clear;
it returns false and leaves the latch set while an ending Logout is being written), for the pump; `session_engine_access::ended(Session const&) noexcept -> bool` (`state() == Disconnected`), for the
pump's `expire()` (E-5); `session_engine_access::set_establishment_deadline(Session&, fixpp::core::steady_time_point)
noexcept` (one store), for the pump's hand-off of its establishment deadline (E-3, "The establishment cap"; E-5).

## E-3. The ending Logout: one state machine for the write limit, the R-2 wait, the bound and the latch

Gate A round 2 (root causes #1 and #2 of the round-2 review; ruling D-e). Every Logout fixpp writes to end a session
goes through this machine: R-1's (`run_liveness_loop`), every R-2 Logout (`send_ending_logout_`, before and after
Active), and the replies to the peer's Logout (`on_inbound_active_logout_`, `acknowledge_peer_logout_`). The entry
point is the write point each Logout leaf already has: `inbound_logout_leaf_` after Active, at the place where it
claims the Logout owner `logout_in_flight_` (before its first suspension; a leaf that finds the owner set writes
nothing and enters nothing here), and before Active the build-and-emit of `refuse_logon_with_logout_` and
`inbound_logon_sent_logout_leaf_`. `close()`'s own Logout (`run_logout_phase1`) is class X and keeps
`close_grace` (B-348-1).

| Field | Type | Meaning |
|---|---|---|
| `ending_wait_.phase` | enum `{ none, writing, armed, resolved }` (one byte) | `writing`: an ending Logout's write is in flight and limited; `armed`: the R-2 wait |
| `ending_wait_.kind` | enum `{ r1, r2, reply }` (one byte) | which ending entered `writing`; only `r2` can reach `armed` |
| `ending_wait_.entered_from` | `fsm_state` (one byte) | the FSM state when the write started; leaving `writing` arms a wait only if the session is still in it |
| `ending_wait_.from_active` | `bool` | entered after Active (the session stays Active through an `r2` wait) or before it (the refusal records `Disconnected`) |
| `ending_wait_.cut` | `bool` | set by the bound when it closes the transport during `writing` (row 4, and its failure exit from `writing`, row 16), before it spawns the close frame; cleared on entering `writing`. "Leaving `writing`" tests it: a cut write arms no wait, whatever the emit reported, because the write's success completion can already be queued when the bound acts (plain TCP: the reactor completed the write while the strand ran the bound, and asio does not withdraw a queued completion), or, on TLS, can arrive while `close_async` yields in its quiesce loop (Gate A loop 2 round 1, root cause #1; loop 2 round 2, N-1). Without it the leaf would arm a wait with no bound running |
| `ending_wait_.deadline` | `fixpp::core::steady_time_point` | `deadline_after(now, logout_disconnect_timeout_ms)` on `effective_clock_`, read **once, at the start of the write** (the owner claim, before the number assignment, the store call and the gate acquire); it bounds the write and then the wait: one budget, as `close_async`'s one `close_deadline` bounds its whole call. Because `store_then_emit`'s Step 1 (the store) runs before `live_write_serialized_` acquires `write_gate_`, the budget covers the store too: a persistent store slower than the budget makes the Logout "not sent", and a write that completes late leaves the peer only the rest of the budget to answer (disclosed, B-105-2; Gate A round 3, N-2) |
| `establishment_cap_` | `std::optional<fixpp::core::steady_time_point>`, on `engine_.clock` | the connection's establishment deadline, handed over once by an Engine pump (`session_engine_access::set_establishment_deadline`); empty on a direct Session; reset at every connection start (row 17) |
| `ending_logout_wake_` | the shape of `peer_logout_wake_` | published by the bound while it waits |
| `ending_logout_bound_counter_` | `std::shared_ptr<std::atomic<int>>` | held by the bound's join parameter (`ending_logout_bound_join`, the shape of `peer_logout_bound_join`); joined by `close()` and `drain_for_reconnect_` after their transport close; asserted 0 by `~Session` |

**Entering `writing`** (all three kinds): only when a clock is present and `logout_disconnect_timeout_ms > 0`; then
`phase = writing`, `cut = false`, the deadline is read, and the bound `run_ending_logout_bound_(join, gen)` is spawned **before the
write**. The instant is the same step at every entry point: after Active, the leaf's claim of `logout_in_flight_`
(R-1 from `run_liveness_loop`, R-2 from `send_ending_logout_`, the replies from `on_inbound_active_logout_` and
`acknowledge_peer_logout_`, all through `inbound_logout_leaf_`), before its number peek, its store call and its gate
acquire; before Active, the start of `refuse_logon_with_logout_`'s or `inbound_logon_sent_logout_leaf_`'s
build-and-emit, before its number assignment and store call. The bound is spawned in `on_inbound_active_too_high_logout_`'s shape: the join is the coroutine's parameter, and a `catch (...)`
around `co_spawn` sets `phase = none` and does not rethrow (the precedent's `catch` rethrows; this one must not).
**A spawn that throws fails closed** (Gate A round 3, root cause #1; clarification D-g; row 2a): no Logout is
written. The leaf takes the path it takes for a Logout that cannot be built (`inbound_logout_leaf_`'s "A frame that
cannot be built is not sent, and the leaf returns success"; before Active the build-and-emit is skipped), and the
site records `Disconnected` as it does for a Logout not sent, so the edge owes the close and the root's consumer
closes at once. Nothing was written, so nothing needs a limit.
With timeout 0 or no clock nothing is entered: the write is unbounded, as before 105, and no wait follows (C-2 "No
wait is armed", C-11 L-105-f). A 0 ms limit is not used as "close at once", unlike `close()`'s `close_grace`,
because it would cut every R-1 Logout.

**LogonReceived never enters `writing`** (spec clarification "Session 2026-11-21 (analyze re-run)", which corrects the
earlier text naming LogonReceived beside Active). The peer's Logout in LogonReceived reaches `on_inbound_active_logout_`,
which writes no reply there, so no bound is spawned and no write limit applies. The arm still runs its persist, its
`Disconnected` write and, under `reset_on_logout`, the 104 receipt reset (its `pending_receipt_owner` is declared for
both states). The session is in LogonReceived at that frame only after a Logon arm's supersede return, while `close()`
or `Engine::stop()` is already under way, and that closer owns the connection's close (contract C-1 row 3). Re-check
recipe, in `src/session/session.cpp` and `include/fixpp/session/session.hpp`: read the `fsm_state_ == fsm_state::Active`
test that encloses the `inbound_logout_leaf_` call in `on_inbound_active_logout_`; read the leaf's own entry test
(its state test, which returns success without writing); then, in the acceptor Logon arm, read each return taken after
`logon_arm_superseded_(fsm_state::LogonReceived)` holds, each `disconnect_unless_superseded_(fsm_state::LogonReceived)`
return (which writes nothing when superseded), and the return on `*h789 != logon_789_outcome::in_sync_continue` against
`logon_789_outcome`'s Outcomes comment, keeping the outcomes that write no state. Those returns are the ones that leave
`fsm_state_` at LogonReceived; each is taken only when `logon_arm_superseded_` holds, and with `fsm_state_` still LogonReceived only
its `closing` and `engine_stop_requested_` terms can make it hold. A change to any of these tests re-opens this paragraph.

**The establishment cap** (Gate A round 3, root cause #2). Before Active on an Engine session
(`establishment_cap_` set and `!from_active`) the bound has two instants, each on its own clock: `deadline` on
`effective_clock_`, and `*establishment_cap_` on `engine_.clock`, the clock the pump reads the establishment deadline
on (`SessionConfig::clock_override` moves `effective_clock_` but not that deadline: `logon_timeout_ms`'s comment in
`session_config.hpp`, "SessionConfig::clock_override does not move it"). It races `detail::await_deadline` on each, and
its deadline has passed when either has. With no override the two are one clock and this is the earlier of the two
instants. **The condition the two arms rely on** (Gate A loop 2 round 1, root cause #2): the cap's arm and the
bound's arm (and the bound's arm beside a parked liveness sleep, or beside the pump's own establishment race) are
independent sleeps, even when the two clocks are one object. On `system_clock_source` that holds because an Engine
session's coroutines run on the type-erased session strand (`SessionEntry::session_strand`, an
`asio::any_io_executor`), and `sleep_until` takes the per-session `SessionTimerSlot` only when
`awaiter_exec.target<session_executor>()` succeeds, an exact-type test; on any other executor each call builds its
own timer. The bound inherits that executor: it is spawned on `co_await asio::this_coro::executor` of the
coroutine that enters `writing`, as `on_inbound_active_too_high_logout_`'s bound is, never on
`Session::executor()`. Re-derive before coding: the `target<session_executor>()` test in
`system_clock_source::sleep_until`, and the executor `Engine::start` spawns `run_accept_loop` and
`run_connect_loop` on. `mock_clock` keeps one waiter per sleep, each with its own cancellation handler, so the
cells see the same independence. A direct driver that spawns a Session's coroutines on `sess.executor()` itself
reaches the slot path, where concurrent sleeps of one Session share one timer; inferred from the code, the same
sharing reaches 103's `run_peer_logout_bound_` and `close()`'s wait on `main`, so it is `system_clock_source`'s
debt and a follow-up issue (plan, "Follow-ups to file before Gate B"), not 105's. Registering one `await_deadline`
at the earlier instant when the two clocks are one object would save one allocation; it is not taken, because it is
not a correctness fix and it adds a second code path with its own cell. So row 4 closes a refusal Logout blocked in its write, queued behind another write, or held in its store
call at the establishment deadline when that comes first, and row 11 closes a wait still armed then. This is needed
because `run_read_pump` tests `expired()` only at its loop head and before each delivery, and races the deadline
only against `async_read_some`: while the refusal's arm is suspended inside `on_inbound_frame`, the pump cannot
observe the deadline. No establishment-timeout event fires at that close: the refusal records `Disconnected` when
the leaf returns, the pump consumes the latch after the arm returns and before it next tests `expired()`, and
`expire()` tests `ended` (E-5). With timeout 0 or no clock nothing enters `writing`, so a blocked refusal Logout is
unbounded before Active too, as on `main` (L-105-f).

**Leaving `writing`**, when the leaf returns (on the strand, with no suspension before the next step). A wait is armed
only here, so only when the bound exists:
- `r2`, the Logout transmitted (D2 item 1's out-parameter), `!cut`, no pending too-high mark claimed by this ending (N-5;
  D2 item 1), and `fsm_state_ == entered_from` (no other root ended the session during the write): `phase = armed`, and the bound is woken, so it rebuilds its
  race in `armed` (a re-test). After Active the session stays Active; before Active the refusal's own
  `Disconnected` write follows (row 6).
- every other case (`r1`, `reply`, an `r2` that was not transmitted, cut by the bound (`cut` set), that claimed a pending
  mark, or whose session another root ended during the write): `phase = none`, the bound is woken, and the site
  records `Disconnected` as it does today (over `Disconnected`, it owes nothing), so the edge owes the close (E-2).

**Other roots during `writing`: the latch defers to the writer.** While `phase == writing`, the two latch consumers
leave the latch set: the pump's after-frame test (`take_connection_end` is not called) and `release_connection_end_`
(it returns without spawning). So a `Disconnected` another root writes during the write (the peer's in-sequence
Logout answered while R-1's Logout is in flight, whose leaf writes nothing because the owner is set; an arm's internal
failure during the too-high bound's acknowledgement) owes the close but does not cut the Logout. The writer's root
consumes it when the write has returned: every root that writes an ending Logout is a consumer (the pump for a frame
arm, the liveness loop for R-1, `run_peer_logout_bound_` for its acknowledgement, E-1). The deferral is bounded by the
write limit. With timeout 0 or no clock nothing enters `writing`, so nothing defers, as on `main`. **The pump's error
close does not defer**: an arm that returns an error runs `stop_pump` → `close(terminal)` at once, as on `main`, and
that close can cut a Logout in flight from another root (disclosed in L-105-f's row).

**The bound's loop.** It publishes its wake, then loops: test the live predicate; if live and the clock is before
the deadline, race the wake against `detail::await_deadline(*effective_clock_, deadline)` (and, under the establishment cap,
against `detail::await_deadline(*engine_.clock, *establishment_cap_)`). It reads the completion by arm: the wake's
arm is a re-test; a deadline arm is a re-test when that arm's own clock has reached its instant (the loop head then
finds the deadline passed and takes the expiry exit). A deadline arm that completed while its own clock still reads
before its instant can only be an exception `detail::await_deadline` absorbed (its `catch (...)` completes the arm;
a `cancel_sleeps()` sweep makes it sleep again, and a join cancel shows as another arm's index), so it is the
failure exit below, not a re-test: re-racing would return at once and spin the strand until the instant under a
lasting allocation failure (Gate A loop 2 round 1, N-1). A wake is a re-test, never an exit by itself. The live predicate, by phase:
- `writing`: `phase == writing` and `conn_gen_ == gen` (asserted). Nothing else ends the limit early. A terminal
  `close()` (or an escalation) and `Engine::stop()` close the transport themselves (`close()`'s `close_async`,
  stop's step 2 `close_async`), which returns the write and so leaves `writing` (row 5). A graceful `close()` does
  not: its phase 1 poll-joins the Logout owner raced only against `close_grace`, an `asio::steady_timer` on wall
  time armed at the `close()` call, and makes its direct `Transport::close()` only when that timer wins.
  `drain_for_reconnect_` waits for quiescence against its own wall-clock bound and force-closes with
  `Transport::close()` only there. Under those two the write ends at row 4's bound or at that wall-clock timer,
  whichever comes first, and row 4 is the only session-clock bound on it (row 5c).
- `armed` after Active: `phase == armed`, `conn_gen_ == gen`, `!engine_stop_requested_`, `!reconnect_drain_in_flight_`,
  `fsm_state_ == Active`. A graceful `close()` leaves the session Active while it poll-joins the Logout owner, so the
  bound runs on (rows 13, 14). Unlike `run_peer_logout_bound_`'s `live()` it does not test `closing_or_stopped_()`.
- `armed` before Active: `phase == armed`, `conn_gen_ == gen`, `state_ == lifecycle::open`,
  `!reconnect_drain_in_flight_`. Any `close()` there is terminal in effect, since `close()`'s Logout phase runs only
  from Active or LogonReceived. The refusal's own `Disconnected` edge wakes the bound and the predicate holds, so it
  waits on (row 6).

**The bound's exits** (Gate A loop 2 round 1, root cause #1: one failure exit, routed through the expiry exit).
Every exit clears its wake. There are three, and each is total over the phase it finds:

| Exit | Taken when | `writing` | `armed`, live predicate true | `armed`, live predicate false; `none`; `resolved` |
|---|---|---|---|---|
| **expiry** | the loop head finds the deadline, or the establishment cap, passed | row 4: `cut` set, then the close frame (E-2) tested on the generation and a live transport only; `phase` left for the leaf to clear when it returns | `resolved` first; after Active `Disconnected` recorded (row 10, owes; row 14 under a graceful `close()`, no owe); before Active `owe_connection_end_()` called directly, after `resolved` (row 11; in the other order the setter refuses, a wait armed before Active being open); then the close frame tested on the generation, a live transport and `state_ == lifecycle::open`, and the latch cleared. Under `closing` the frame's `lifecycle` test fails and `close()` owns the close (row 14) | not reached: the live predicate is tested first |
| **failure** | the bound's `catch (...)`, or a deadline arm that completed before its instant (above) | as the expiry exit | as the expiry exit | as the ordinary exit |
| **ordinary** | the live predicate false, or `phase` no longer `writing` | — | — | an exit that finds `phase == armed` sets `resolved` first; then `release_connection_end_()` |

So an expiry, and a failure, closes whatever arm is in flight: the arm resumes with a write error and the pump's
later `close(terminal)` is subsumed. The failure exit never hands its close to `release_connection_end_()`, which
refuses while `inbound_depth_ != 0`: from `writing` the writer is usually a pump arm, and with the bound gone
nothing else limits that write (Gate A loop 2 round 1, Codex #2). Every step of the expiry exit is `noexcept`
(`record_state_transition_`, `owe_connection_end_()`, the close frame's spawn with its D-g fallback), so the
failure exit cannot itself fail open. The fallback's `Transport::close()` result is discarded, as `close()`'s grace
discards its own direct close's: a transport whose close reports an error is not closed a second way, and the
pump's later `close(terminal)` on it is subsumed (contract C-8.6).

**Cancellation.** Neither new spawn binds a cancellation slot: each is `co_spawn(..., asio::detached)`, as
`on_inbound_active_too_high_logout_`'s bound is, and runs on its own awaitable thread. So `root_cancel_` reaches
neither, the bound's `||` race changes only the bound's own thread's cancellation filter, and `close_async`'s
`disable_cancellation` stays on the close frame's thread (F-6); the liveness loop's and the pump's filters are not
touched. W-13 and W-14 end the wait by a wake, never by cancelling the bound: the wake completes the race's wake arm,
the `||` operator then cancels the deadline arm as in `run_peer_logout_bound_`, and that completion is read by its arm
index as a re-test ("The bound's loop"). This is what makes contract C-2 W-9's "at the failing root's exit" hold for
this root (row 16).

**The bound's wake sources:** the `Disconnected` edge; the leaf's return from `writing`; `close()` at its first
commit of `closing` (beside its `claim_peer_logout_none_()`); `drain_for_reconnect_` at its entry; the response
(rows 8, 9). `Engine::stop()` is not a separate source: its step 1 sets `engine_stop_requested_` (`note_engine_stop_`),
and it reaches the bound through `Session::close(terminal)` at its step 4, whose commit of `closing` and whose
`Disconnected` write (`close()` records it before its phase 2 when the session is not already `Disconnected`) wake it;
after Active the re-test then fails on `!engine_stop_requested_` and on `fsm_state_ == Active`, before Active on
`state_ == lifecycle::open` (row 15). Between steps 1 and 4 the bound runs on to its own instant at most; a wake there
already finds the flag set.

**Transitions.** "Owes" means `owe_connection_end_()` sets the latch (E-2); "no owe" means its predicate refuses.

| # | FSM, lifecycle, phase | Event | Phase after | Latch | Bound | Who closes |
|---|---|---|---|---|---|---|
| 1 | any, open, `none` | an ending Logout's write starts, clock present, timeout > 0 | `writing` (kind, `from_active`, deadline) | none | spawned before the write | row 3, 4 or 5 |
| 2 | any, open, `none` | an ending Logout's write starts with timeout 0 or no clock | `none` | the site's `Disconnected` after the write: owes | none | the root's consumer after the write (E-1); the write is unbounded (L-105-f) |
| 2a | any, open, `none` | an ending Logout's write starts, clock present, timeout > 0, and the bound's spawn throws | `none` | the site's `Disconnected`, the Logout not written: owes | none | the root's consumer, at once (E-1); no Logout is written (clarification D-g) |
| 3 | any, any, `writing` | the write returns before the deadline | `armed` (an `r2` transmitted, not cut, no mark claimed) or `none` (every other case) | `none`: the site's `Disconnected` owes; `armed` after Active: none; `armed` before Active: row 6 | `armed`: woken, runs on (a re-test); `none`: woken, exits | `armed`: rows 8 to 15; `none`: the root's consumer (R-1 at the loop's exit, a reply after its reset unit) |
| 4 | any, any, `writing` | the deadline passes, or before Active on an Engine session the establishment cap, the write not returned (the Logout in its store call, blocked in `async_write` or queued at `write_gate_`) | `writing` until the leaf returns, then `none` (row 3: `cut` set) | the site's `Disconnected` after the leaf returns: owes; its consumer finds the transport closed | sets `cut`, spawns the close frame (E-2) on the transport (its spawn failing: the synchronous close, E-2), then exits | the bound's close frame: `close_async()`; on TLS a close_notify that cannot be written falls back to the abortive close after `tls_close_timeout` (C-8.5, L-105-g) |
| 5 | any, any, `writing` | `close(terminal)`, an escalation, `Engine::stop()`; a graceful `close()` before Active (its phase 1 does not run there) | `writing` until the leaf returns | as row 3 or 4, whichever ends first | runs on until the leaf returns | the closer's own `close_async()` returns the write; `close()` joins the bound, then the close frame's counter again (E-2 I-4) |
| 5c | Active, `closing` (graceful) or the drain flag, `writing` | `close(graceful)` after Active, `drive_reconnect`'s drain | `writing` until the leaf returns | as row 3 or 4 | runs on until the leaf returns | row 4's close frame at the bound, or the closer's direct `Transport::close()` when its wall-clock timer (`close_grace`; the drain's bound) wins first; the closer then proceeds and joins as row 5 |
| 5a | any, open, `writing` | another root writes `Disconnected` (the peer's Logout answered while the owner is set; an internal failure) | `writing` until the leaf returns, then `none` (`fsm_state_` is no longer `entered_from`) | that transition owes; both consumers defer while `writing` | runs on until the leaf returns | the writer's root, after the write: the Logout is complete on the wire, then EOF |
| 5b | any, open, `writing` | an arm returns an error (the pump's `!r`) | — | — | — | the pump's `close(terminal)` at once, as on `main`; it can cut the Logout in flight. That close is row 5 for the write: `cut` is not set, because the closer owns the close, and no wait survives it: `close()` commits `closing` and records `Disconnected` (wake sources), so a wait the leaf arms from a write the close did not cut fails its live predicate (`fsm_state_ == Active` after Active, `lifecycle::open` before it) and the bound exits by row 15 |
| 6 | NotConnected or LogonSent, open, `armed` | the refusal's own `Disconnected` write | `armed` | no owe (a wait armed before Active) | woken; its predicate holds; waits on | rows 9, 11, 15 |
| 7 | Active, open, `armed` | a second ending from a peer frame (C-2 W-8): `send_ending_logout_` or `end_session_silently_` | `armed` | none (nothing recorded) | runs on | — |
| 8 | Active, open, `armed` | the peer's Logout (W-1) | `resolved`, at the response arm's entry | the arm's `Disconnected` owes | woken, exits | the pump, after the arm returns (reset unit first) |
| 9 | Disconnected, open, `armed` (before Active) | the peer's Logout (W-1) | `resolved` | `owe_connection_end_()` called directly | woken, exits | the pump, after the frame |
| 10 | Active, open, `armed` | the bound expires | `resolved`, then `Disconnected` | that transition owes; the bound clears it at its spawn | exits | the bound's close frame, at once, whatever arm is in flight (a resend answer blocked in its write included; E-2) |
| 11 | Disconnected, open, `armed` (before Active) | the bound expires, or on an Engine session the establishment cap passes | `resolved` | `owe_connection_end_()` called directly; the bound clears it at its spawn | exits | the bound's close frame, at once (E-2) |
| 12 | Active, open, `armed` | **any other `Disconnected` write**, from any root (an arm's internal failure, W-9; a send admitted before the Logout whose store or emit fails; a liveness emit admitted before it that fails; the too-high Logout bound's handler) | `resolved`, by the edge's call | owes | woken, exits | the pump on the error or on the latch, whichever it tests first; on a non-pump root, its own release at its exit, deferred to the pump while an arm is inside one (an arm blocked in a non-ending write closes when that write returns, L-105-f); on a direct Session, L-105-b |
| 13 | Active, `closing` (graceful), `armed` | the peer's Logout | `resolved` | no owe (`closing`) | woken, exits | `close()`, released from its poll-join |
| 14 | Active, `closing` (graceful), `armed` | the bound expires | `resolved`, then `Disconnected` | no owe | exits | `close()`, released from its poll-join |
| 15 | any, `closing` (terminal or escalated) or the stop flag or the drain, `armed` | `close(terminal)`, `Engine::stop()`, an escalation, `drive_reconnect`'s drain | `resolved` at the bound's exit | none (`closing` or the drain flag) | woken, exits; the closer joins it | the closer, as today |
| 16 | any, any, any | the bound fails: its own `catch (...)`, or a deadline arm that completed while that arm's clock reads before its instant (`detail::await_deadline`'s absorbed exception) | as the expiry exit of the phase it finds ("The bound's exits"): `writing` with `cut` set until the leaf returns, then `none`; `armed` → `resolved` | as row 4 (`writing`), row 10, 11 or 14 (`armed`, live), or the ordinary exit (otherwise) | exits | as row 4 from `writing`; as rows 10, 11 or 14 from `armed`; the closer (row 15) or `release_connection_end_()` otherwise; never `release_connection_end_()` from `writing` or a live `armed` |
| 17 | any, any, `resolved` or `none` | the next connection (`install_reconnected_transport`, `attach_accepted_transport`) | `none` (asserted `resolved` or `none` there); `establishment_cap_` reset | — | — | — |

**Totality.** The table is total over the events that change `phase`. In `none` only rows 1, 2, 2a and 17 act; every
other event leaves `phase` at `none` and is today's behaviour. In `writing`, rows 3 to 5c and 16: a peer frame the pump
delivers while another root writes is processed by its arm (a `Disconnected` it records is row 5a; a second ending is
absorbed, `ending_in_progress_()` holding during `writing` after Active), a frame cannot arrive while the writer is a
pump arm (the pump is serial), and a reconnect cannot (row 17's `install_reconnected_transport` runs only after
`drain_for_reconnect_`, row 5c, has waited for the leaf's return). In `armed`, rows 6 to 16 and contract C-2's W rows.
In `resolved`, only row 17 acts: every other event finds the bound exiting and the latch's consumer or the closer
already acting.

**The order before Active** (rows 1, 3, 6): enter `writing` and spawn the bound; the Logout's write; `armed`; the
refusal's own `Disconnected` write. Arming after that write would let the edge owe the close and end the wait before
it began (a mutant). The bound is already running when the edge fires, which is safe because its wake is a re-test.

**The invariant that makes the latch total by construction.** The absorbed set is defined by **cause**, never by
return value: only a peer-frame condition W-8 names (through `end_session_silently_`) and a further ending Logout
(through `send_ending_logout_`) are absorbed, and only while an ending is in progress after Active
(`ending_in_progress_()` = `phase == writing` with `from_active`, or `phase == armed` with `from_active`). Every
other `Disconnected` write reaches the edge, and the edge, during a wait armed after Active, ends the wait and owes
(row 12). The one write the owe refuses is the refusal's own write during a wait armed before Active (row 6), which
is the write the suppression exists for. The armed test lives in `owe_connection_end_()` and in the two absorbing
helpers; `record_state_transition_`'s `Disconnected` write also serves `close()` and the drain, which the latch's
`lifecycle` and drain-flag terms exclude.

**During `armed`:** the rows of contract C-2 "While the wait is armed" (W-1 to W-15).

## E-4. Liveness timing (`run_liveness_loop`, locals) and the TestReqID rule

| Quantity | Value | Arithmetic |
|---|---|---|
| H | `cfg_.heartbeat_interval.value_or(kDefaultHeartBtInt)` (whole seconds) | as today |
| T | `cfg_.test_request_threshold` when set, else `default_test_request_threshold(H)` = H × 1200 ms | saturating at `milliseconds::max()` |
| `tr_sent_at` | the clock's reading when the pending TestRequest was emitted (a loop local) | — |
| "inbound since the TestRequest" | `last_inbound_steady_ > tr_sent_at` | — |
| Heartbeat instant | `last_outbound_steady_ + H`, **eligible** when no TestRequest is pending, or one is pending and an inbound frame arrived since it | as today |
| TestRequest instant | `deadline_after(last_inbound_steady_, T)`, eligible under the same condition | saturating |
| Give-up instant | `deadline_after(deadline_after(deadline_after(last_inbound_steady_, T), T), std::chrono::nanoseconds{1})`, **eligible** when a TestRequest is pending and no inbound frame arrived since it | three saturating steps, no product; the guard is the third (`deadline_after`'s template takes nanoseconds, since `steady_time_point` is `std::chrono::steady_clock`'s and its static_asserts require a whole multiple of its tick) |
| Gap deadline (unchanged, B-537-2) | `detail::resend_gap_deadline(started_at, H)` | as today |

**One sleep per iteration** (Gate A round 2, N-2; the round-1 design's separate grace sleep is gone; the sleep set
restated in Gate A round 3, root cause #3). At its top the loop re-reads `last_inbound_steady_` and
`last_outbound_steady_` and sleeps until the earliest instant of the **sleep set**: the Heartbeat instant when it is
eligible **or not yet reached**, the TestRequest instant when eligible, and the give-up instant when eligible. The
Heartbeat instant is in the set before it is eligible because a frame can make it eligible during the sleep, and
nothing on the inbound path wakes the sleep (the per-frame refresh writes `last_inbound_steady_` only; the loop
suspends only in `effective_clock_->sleep_until`). An eligible instant already reached makes the sleep return at
once. **The spin trap:** a Heartbeat instant that has been reached while ineligible is dropped from the set; kept, it
would make every sleep return at once. On waking the loop tests eligibility again and acts on every eligible instant
the clock has reached: the give-up (C-3), else the TestRequest (its TestReqID becomes the pending one; `tr_sent_at`
is the clock's reading), else the Heartbeat. D-536-2's state test and filter reset stay before every sleep.

So a frame that does not answer the TestRequest makes the Heartbeat eligible again (QuickFIX C++: `Session::verify`
runs `testRequest(0)` on every accepted frame, and `needHeartbeat` is `(now - lastSentTime()) >= heartBtInt() &&
!testRequest()`): a frame before the Heartbeat instant has the Heartbeat sent at that instant; a frame after it has
the Heartbeat sent at the loop's next wake, the stale give-up instant at the latest, so at most max(0, T − H) after
the frame (the TestRequest goes T after the last frame before it, the Heartbeat instant is H after the TestRequest,
the give-up 2T after that frame; 0.2 × HeartBtInt at the default T, 2 s at the cells' H = 10 s). QuickFIX C++ sends it at its next `Session::next()` tick,
which `ThreadedSocketConnection` polls every second (`int timeout = 1000;`). No inbound wake is added: the delay
disconnects nothing, and the peer's own TestRequest is answered by the pump. A further T of silence sends the next
TestRequest. With inbound silent since the TestRequest, nothing but the give-up is eligible, so no Heartbeat is sent
(FR-010, C-6.3).

**One predicate**, `liveness_may_originate_()` = `fsm_state_ == fsm_state::Active && ending_wait_.phase == none`:
the loop condition and every test after a suspension (after the sleep, after each emit). It is keyed on the
ending's state, not on `logout_in_flight_`, because `run_logout_phase1` sets that flag before it records
`LogoutSent`. It does not block R-1: the loop tests it after its sleep, before it acts; the give-up then writes the
slot-74 record, enters `writing` at the leaf (kind `r1`), and, when the leaf returns (`phase` back to `none`), records
`Disconnected`, so the post-emit test fails on `fsm_state_` and the loop exits to its release (research D3). The arming adds no wake: the predicate is what keeps the loop from originating, and `close()` and the
drain join the loop as today (research D2).

The loop is off when H ≤ 0 (owner ruling on SI-2, 2026-11-20).

**The TestReqID rule** (ruling D-d; research D5, F-15). One predicate, `classify_test_req_id_(id)`, used by both
comparison sites (the in-sequence Heartbeat arm of `on_inbound_active_` and `answer_pending_test_request_`, which the
too-high Heartbeat route and #582's knob-off route call), and only while a TestRequest is pending, as today:

| 112 on the Heartbeat | Outcome |
|---|---|
| absent, or equal to `pending_test_req_id_` | **answers**: the pending TestRequest is cleared. "Absent" is the header scan's empty TestReqID, as today; whether a present but empty `112=` is a header fault or reads as absent is the scan's rule, unchanged by 105 (the fuzz seeds include it, tasks.md T085) |
| `TR` followed by the canonical decimal (no sign, no leading zero, fits `std::uint32_t`) of an n with `(n − floor − 1) mod 2³² < (next − floor) mod 2³²`, other than the pending one | **earlier**: a TestRequest fixpp sent on this connection; neither answers nor ends; the frame refreshes liveness as every accepted frame does |
| anything else | **mismatch**: ends the session silently, `session_testreqid_mismatch`, as today |

`next` is `next_test_request_id_` (the per-Session counter the loop increments for each TestReqID, never reset in
`src/`); `floor` is a new `std::uint32_t test_request_id_floor_`, set to `next_test_request_id_` at every connection
start (`open()`, `install_reconnected_transport`, `attach_accepted_transport`). Each start runs with no inbound frame
inside an arm, so no classification straddles the reset: `open()` and `attach_accepted_transport` run before the
pump's first delivery, and `install_reconnected_transport` runs after `drain_for_reconnect_` has quiesced the inbound
chain (B-103-9). Memory is those two integers. The
window `(floor, next]` is modular, so it is wrap-safe while fewer than 2³² TestRequests have been issued on one
connection. After 2³² TestRequests on one connection the window no longer identifies the sent set: at exactly 2³²,
`next == floor` and no value reads as earlier; past it, the window is the modular remainder (Gate A round 3,
Codex #7). Unreachable in practice (2³² TestRequests at least T apart), and disclosed in B-105-5 as that condition.
The window test is a free `constexpr` function of `(n, floor, next)` in `src/session/read_first_frame_bounded.hpp`
(beside `deadline_after`, `resend_gap_deadline` and the default-threshold helper), test-visible; `constexpr` cells pin
that function at `UINT32_MAX`, across the wrap, and at one full cycle (guards, each with its mutant: a non-modular
comparison; the full-cycle case dropped, `next == floor` read as a full window; Gate A loop 2 round 2, Codex #3).
The parse-and-classify step is a second free `constexpr` function in the same header,
`detail::is_earlier_test_req_id(std::string_view id, std::uint32_t floor, std::uint32_t next)`: true when `id` is `TR`
followed by the canonical decimal of an n that fits `std::uint32_t` and the window function holds for `(n, floor,
next)` (orchestrator decision on `/speckit-analyze` C1). `classify_test_req_id_` answers for an absent 112 or
`pending_test_req_id_`, and otherwise calls it (true: earlier; false: mismatch); the fuzz harness
`tests/fuzz/fuzz_session_recovery_admin_parse.cpp` calls it directly, so the parse is fuzzed without a pending
TestRequest (plan Constitution Check VII §7). `constexpr` guard cells pin it on a canonical earlier id, a leading zero,
a sign, a number past `std::uint32_t` and `TR` alone, each with its mutant.
`answer_pending_test_request_` returns false only for **mismatch**.

**Validation of a set T** (`Session::open`, `Engine::register_session`, the TOML mapper): `T > 0`. No other bound.

**Removed:** `Session::unanswered_tr_` and its writes.

## E-5. The pump's establishment test

`establishing()` is unchanged. `expire()` calls `note_establishment_timeout` only when `!ended(session)`, and
then `stop_pump()` either way: a session that has recorded its own ending (a refusal, or a pre-Active R-2 wait
still open at the deadline) is closed at the deadline with no event and no record.

`run_read_pump` gains `std::span<const std::byte> first_frame`, delivered before `initial_bytes` under the same
disposition (FR-006). Before its first delivery it hands `establish_deadline`, when it holds one, to the Session
(`session_engine_access::set_establishment_deadline`, one store), so that the ending machine can close a pre-Active
ending Logout at that deadline while the pump is suspended in the arm (E-3, "The establishment cap").

## E-6. Pre-session reject reason (`src/session/engine.cpp`, anonymous namespace)

`enum class pre_session_reject : std::uint8_t`, logged by value and by a fixed name:

| Value | Name in the record | `run_accept_loop` branch | CompIDs read | `error` |
|---|---|---|---|---|
| `not_tls_transport` | `not_tls` | the accepted transport is not a `TlsTransport` on a TLS profile | no | 0 |
| `tls_handshake_failed` | `tls_handshake` | `async_handshake` failed | no | the handshake's error |
| `no_establishment_time` | `no_time_left` | no establishment time left after the handshake | no | 0 |
| `first_frame_read_failed` | `first_frame` | `read_first_frame_bounded` failed (timeout, over budget, an over-limit frame, a read error) | no | the read's error |
| `first_frame_without_ids` | `no_ids` | `scan_first_frame_ids` found no BeginString or no CompIDs | what was read | 0 |
| `compid_mismatch` | `no_session` | the reversed SessionId does not match the entry | yes | 0 |

Re-derive the branch list before coding: every `(void)transport->close(); continue;` in `run_accept_loop` before
`open()` is one row; a branch with no row is a defect of this table.

## E-7. The pre-session log gate (hoisted rate gate)

| Entity | Where | Fields |
|---|---|---|
| `detail::log_rate_gate` | `include/fixpp/session/detail/log_rate_gate.hpp` (a new installed implementation header: `session.hpp` declares two gates; the status of `detail/replay_gate.hpp`, contract C-9) | `logged` (`bool`), `next` (`fixpp::core::steady_time_point`), `unlogged` (`std::uint64_t`, a count no flood reaches the range of) (Session's struct, moved; layout unchanged; `Session` keeps a member alias) |
| `detail::admit_log_record(gate, now, window, occurrences)` | same, inline | returns the suppressed count since the last record and starts the next window, or counts and returns `nullopt` |
| `Session::admit_log_record_` | unchanged signature | forwards with `effective_clock_` and `max(HeartBtInt, 1 s)` |
| the accept loop's gate | a local of `run_accept_loop` | one per entry; clock `engine_cfg.clock`; window `max(entry's HeartBtInt or the default, 1 s)` |

## E-8. Log records added (each registered in `src/log/format_registry.cpp`)

Each is a `FIXPP_SLOG` record: the producer stores the format's CRC32 id and its arguments and the drain thread
formats it (`src/log/format_registry.cpp`'s header), so no synchronous I/O runs on the session strand (Article XV §5).
The registry is the drain's only source of the format text, so each record cell, which asserts the rendered record
through a capturing sink, fails on a format that is not registered; the registry's duplicate-id `static_assert` checks
collisions.

| Writer | Level | Format | Rate |
|---|---|---|---|
| `run_liveness_loop` (R-1), written **before** the Logout's write (ruling D-e), so a write that never completes cannot lose it | warn | `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}` | once per connection; a connection reaches it only after an accepted Logon and 2T of inbound silence, so a flapping peer yields at most one per reconnect cycle, and no gate is added (as `note_establishment_timeout_`) |
| `log_pre_session_reject` (E); the BeginString and CompIDs with each byte outside printable ASCII (0x20 to 0x7E) replaced by `?` before `ArgValue::from_inline`'s 15-byte copy (contract C-7.1, research D8) | warn | `acceptor connection refused before a session: reason={} begin_string={} sender_comp_id={} target_comp_id={} error={} suppressed_since_last={}` | E-7's gate |
| `run_accept_loop`'s listener catch | error | `acceptor listener could not bind: begin_string={} sender_comp_id={} target_comp_id={} error={}` | one error record per start of the entry's accept loop (the record is in the listener's catch, which ends that loop; contract C-7.3) |

## E-9. `ReconnectFsm` after FR-013

Removed: `attempt_index_`, `heartbeat_interval_`, `logout_disconnect_timeout_`, `heartbeat_timer_`,
`test_request_timer_`, `logout_timer_`, `last_outbound_testreqid_`, `drive_logout`, every other stub with no
caller (`run_heartbeat_cadence`, `run_inbound_liveness_watch`, `validate_inbound_heartbeat_testreqid`,
`process_inbound_sequence_reset`, `reply_to_inbound_resend_request`, as the caller census of research D7 confirms at
the implementation head; owner ruling on SI-1, 2026-11-20), and the constructor's `heartbeat_interval` and
`logout_disconnect_timeout` parameters.
