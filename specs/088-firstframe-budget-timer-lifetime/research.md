# Research: 088 — first-frame budget boundary + deadline-timer handler lifetime

**Feature**: `088-firstframe-budget-timer-lifetime` · **Date**: 2026-08-04 · **Spec**: [spec.md](./spec.md)

Every decision below is grounded in a source read performed for this feature — the asio behaviours
are quoted from the **pinned** `asio/1.38.0` headers in the Conan cache
(`conanfile.py:67`), not from documentation or memory. Where a claim is load-bearing the file:line is
given so Gate A can re-derive it.

> **Seven decisions are already LOCKED** by user decision at `/specify` (Q1–Q3) and `/clarify`
> (C1–C4), recorded in `spec.md` §Clarifications. This document does not re-open them; it resolves
> the residual *design* questions those decisions handed to `/plan`.

---

## D-1 — The rewritten `read_first_frame_bounded` loop

**Decision.** One loop body, in this exact order:

```
loop:
  1. room  = max_bytes + 1 - buf.size()          // C1 clamp; proof below
     want  = min(read_buf.size(), room)
  2. n     = co_await <joined read | deadline>   // D-2
     - deadline arm won  -> transport_handshake_timeout
     - read arm failed   -> propagate its error
  3. buf.insert(read_buf[0..n))
  4. feed_r = framer.feed(read_buf[0..n), carry, out_frames)   // FRAME FIRST
     - feed error        -> propagate (wire_frame_too_large stays distinct)
     - frame present     -> co_return out_frames[0].bytes().size()   // FRAME WINS
  5. if (buf.size() > max_bytes)                 // SINGLE budget decision, strict >
       co_return wire_frame_too_large
```

**Rationale.**

- Step 4 before step 5 is FR-002 (Q1). Step 5's strict `>` is FR-001 (Q1).
- Step 5 sits at the **foot** of the body, past the frame-found return — FR-007 requires exactly one
  budget decision point, and this placement is what makes the clamp safe (see the proof).
- The loop-top duplicate at today's `engine.cpp:408-411` is **deleted**, not relocated. It is
  unreachable today (the sole caller at `:857-858` passes an empty `buf`) and would be redundant
  with step 5 in any case.
- The `while (!timed_out)` condition disappears with the flag itself (D-2): the deadline is no longer
  observed between reads, it is an arm of the join. The loop becomes `for (;;)`, and every exit is a
  `co_return`.

**Clamp proof (FR-013's obligation — `room` never underflows and is never 0).**

- *Entry*: the sole caller passes an empty `buf` (`engine.cpp:857-858`), so `buf.size() == 0` and
  `room == max_bytes + 1 ≥ 1`. ✔
- *Inductive step*: control reaches step 1 of iteration `k+1` only by falling off step 5 of iteration
  `k`, which requires `buf.size() <= max_bytes`. Therefore
  `room = max_bytes + 1 - buf.size() >= 1`. ✔ — unsigned subtraction never wraps, and `want >= 1`,
  so no zero-length read and no spin.
- *Tightest case*: `buf.size() == max_bytes` ⇒ `room == 1` ⇒ the read requests exactly one byte. This
  is the case the spec's Edge Cases call out, and it is a pinned test cell (see D-6).
- *Bound*: after step 3 of any iteration, `buf.size() <= max_bytes + 1`, because
  `n <= want <= room = max_bytes + 1 - buf.size()`. **SC-013's `max_bytes + 1` is therefore an
  invariant of the clamp, not an aspiration.**

**Alternatives rejected.** Keeping the loop-top check and making it `>` too (two decision points that
must be kept in agreement — FR-007 exists to forbid exactly this); clamping to `max_bytes - buf.size()`
(would make the over-budget condition unreachable, so `wire_frame_too_large` could never fire —
a silent removal of FR-003).

---

## D-2 — The joined read/deadline form, and why the timer arm needs a wrapper

**Decision.** Replace the `timer.async_wait(<lambda>)` + `bool timed_out` + `timer.cancel()`-on-every-path
shape with:

```cpp
using namespace asio::experimental::awaitable_operators;

auto outcome = co_await (
      transport.async_read_some(std::span{read_buf.data(), want})
   || await_deadline(timer)                       // NOT timer.async_wait(use_awaitable) — see below
);
if (outcome.index() == 1) co_return std::unexpected(error::transport_handshake_timeout);
auto read_r = std::get<0>(std::move(outcome));
```

**Why the join closes both legs.** `operator||` expands to
`make_parallel_group(co_spawn(ex, t, deferred), co_spawn(ex, u, deferred)).async_wait(wait_for_one_success(), deferred)`
(`asio/experimental/awaitable_operators.hpp:258-264`). `parallel_group::async_wait` completes only
when **every** operation has finished. So the coroutine cannot resume — and therefore cannot destroy
its frame — while either arm is outstanding. FR-005's "no handler may execute against a destroyed
frame" becomes *structural*: there is no handler left to strand. FR-006 follows for free — the
deadline arm has retired before `run_accept_loop` can reach the
`attach_accepted_transport` move at `engine.cpp:922`, so no late `cancel()` can reach a live
`Session`'s transport.

**The trap, and the reason `await_deadline` is a wrapper rather than a bare `async_wait`.**
`cancellation_state`'s default `InFilter` is `enable_terminal_cancellation`
(`asio/cancellation_state.hpp:199-201`), and `co_spawn` gives each `||` arm its own
`cancellation_state`. `Engine::stop()` emits `cancellation_type::total`. **A bare
`timer.async_wait(asio::use_awaitable)` arm would therefore silently swallow `stop()`'s signal** —
precisely the failure this codebase already documents as MANDATORY to avoid:

> `engine.cpp:307-309` — *"EVERY co_spawned loop MUST reset_cancellation_state(total) as its first
> step or stop()'s total-cancel is swallowed silently (co_spawn defaults to terminal-only)."*
> `[[feedback_asio_cospawn_total_cancellation_default]]`

So the deadline arm is a one-line coroutine that resets first:

```cpp
asio::awaitable<void> await_deadline(asio::steady_timer& t) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    co_await t.async_wait(asio::use_awaitable);
}
```

`basic_waitable_timer::async_wait` supports `terminal`, `partial` **and** `total`
(`asio/basic_waitable_timer.hpp:575-583`), so once the filter admits `total` the abort is real.

**The read arm needs no wrapper.** Both `asio_plain_transport::async_read_some`
(`src/transport/asio_plain_transport.cpp:191-197`) and `asio_tls_transport::async_read_some`
(`src/transport/asio_tls_transport.cpp:1129-1134`) open with
`co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation())` as their
first statement, so the `co_spawn` frame's default filter is replaced before any signal can arrive.
The underlying operations honour it: `ssl::stream::async_read_some` supports `terminal`/`partial`
(`asio/ssl/stream.hpp:843-849`) and `basic_stream_socket::async_read_some` supports
`terminal`/`partial`/`total`. Both map `operation_aborted` → `transport_read_cancelled`.

**Alternatives rejected.** Shared-owned state captured by value (leaves FR-006 open — see
spec §Clarifications Q2); shared state plus a `retired` suppression flag (two mechanisms, and the
flag inherits the same-drain ordering question it was meant to remove).

---

## D-3 — Is the `wait_for_one_success` "winner errored" caveat reachable?

**Question handed to `/plan`.** `wait_for_one_success`'s disposition overload returns
`cancellation_type::none` when the completing operation carries an error
(`asio/experimental/cancellation_condition.hpp:87-91`); its non-disposition overloads and its
default return `cancellation_type::all` (`:67-68`, `:73-84`). If the read arm "errors", the deadline
arm is not cancelled and the group waits for it — up to the full 5 s.

**Finding: NOT reachable on any path this feature creates.** The disposition here is the
`std::exception_ptr` in the `co_spawn(..., deferred)` completion signature — it is non-null only if
the arm's coroutine **throws**. Our read arm cannot:

- `asio_{plain,tls}_transport::async_read_some` returns every failure as
  `std::unexpected{core::error::...}` — `transport_already_closed`, `transport_read_in_progress`,
  `transport_read_cancelled`, `transport_read_eof`, `transport_read_truncated`,
  `transport_read_error`. There is no `throw` in either body.
- The underlying asio call uses `asio::redirect_error(asio::use_awaitable, ec)`
  (`asio_plain_transport.cpp:220-221`, `asio_tls_transport.cpp:1162-1163`), which converts the error
  into `ec` instead of throwing.

A *transport-level* read failure therefore arrives as a **successful** completion carrying an
`expected_t` in the error state — `exception_ptr` is null, `wait_for_one_success` returns
`cancellation_type::all`, and the deadline arm is cancelled immediately. **The normal failure path
is unaffected.**

**Residual, recorded rather than dismissed.** A genuinely thrown exception (e.g. `std::bad_alloc`
from a coroutine-frame allocation) would leave the deadline arm running to its natural expiry. The
consequence is bounded — the accept slot is reclaimed up to 5 s later than it is today, no leak, no
UAF — and it is strictly better than the pre-fix behaviour on the same input, which strands a handler
instead. **No mitigation is added**; adding one would mean second-guessing `wait_for_one_success`'s
contract for a path that cannot fire. This paragraph exists so Gate A sees it was analysed, not
missed.

---

## D-4 — Per-site mechanism for the three transport timer sites (FR-014)

The spec explicitly does **not** mandate `||` here (C4/FR-014). These sites capture `this`, so there
is no dangle leg; only the late-`socket_.cancel()`-after-success leg applies.

**Decision: an attempt-epoch guard, not a join, at all three sites.**

```cpp
const std::uint64_t epoch = ++timer_epoch_;              // member, strand-confined
timer.async_wait([this, epoch](asio::error_code ec) {
    if (ec || epoch != timer_epoch_) return;             // stale expiry -> no-op
    asio::error_code ignored;
    socket_.cancel(ignored);
});
...
++timer_epoch_;                                          // retire before returning
timer.cancel();
```

**Rationale — why not `||` here.**

1. `src/transport/asio_plain_transport.cpp:137-144` (and the TLS twin at
   `src/transport/asio_tls_transport.cpp:918-933`) installs an **OUT cancellation filter** that maps
   any accepted cancellation to `terminal` for the forwarded child op, deliberately, so
   `Engine::stop()`'s `total` can abort an in-flight `async_connect` that would otherwise ignore it
   (016 T008 — the TLS comment spells the whole rationale out at `:918-929`, including that
   slot-level assignment was tried and is unsafe here). Wrapping that op in a `||` inserts a `co_spawn` frame
   between the filter and the operation, so the filter would have to be re-established inside the
   arm. That is a re-plumb of working, tested, deliberately-shaped cancellation code — for a defect
   whose only leg is a stale callback.
2. There is no lifetime problem to solve: the handler captures `this`, and the transport outlives it.
   A join buys nothing the epoch does not.
3. The epoch is strand-confined (these are single-strand transports; `read_in_flight_` is already a
   plain `bool` member guarded the same way — `asio_plain_transport.cpp:206`), so it needs no atomic
   and adds no synchronisation.

**Rationale — why an epoch rather than a plain `bool retired_`.** A `bool` is ambiguous across
successive connect attempts on a reconnecting transport: attempt *N*'s stale handler could observe
attempt *N+1*'s freshly-cleared flag and cancel a socket that is legitimately in use. The monotonic
counter makes "which attempt armed me" explicit, which is the actual question the handler must
answer.

**Sites and their retire points:**

| Site | Operation guarded | Retire (`++timer_epoch_`) immediately before |
|---|---|---|
| `src/transport/asio_plain_transport.cpp:130` | `async_connect` (connect timeout) | the existing `timer.cancel()` after `async_connect` returns |
| `src/transport/asio_tls_transport.cpp:910` | `async_connect` (connect timeout) | the existing `timer.cancel()` after `async_connect` returns |
| `src/transport/asio_tls_transport.cpp:1032` | `async_handshake` (handshake timeout) | the existing `timer.cancel()` at `:1045` |

Each transport gets **one** `timer_epoch_` member; the TLS transport's connect and handshake timers
are sequential, never concurrent, so they can share it.

**Alternatives rejected.** `||` at all three (item 1 above); `||` at the two connect sites only
(inconsistent mechanism for one defect class, and the handshake site is the one whose composed
`ssl::stream` op is least amenable); leaving them and filing an issue (rejected at C4/Q3).

---

## D-5 — The internal header (FR-016) and its build wiring

**Decision.** Move the whole definition of `read_first_frame_bounded` into a new
`src/session/read_first_frame_bounded.hpp` as an **`inline` function template-free header**, and have
`src/session/engine.cpp` `#include "session/read_first_frame_bounded.hpp"`.

**Rationale — the precedent is exact and already load-bearing in this same file.**
`src/session/scan_first_frame_ids.hpp` was created for precisely this purpose (040 US2 Phase 4), and
`engine.cpp:360-362` records why: *"moved from anonymous namespace to enable direct unit testing."*
Its test target is wired at `tests/session/CMakeLists.txt:722-728`:

```cmake
add_threading_test(session_scan_first_frame_ids_overflow scan_first_frame_ids_overflow_test.cpp)
target_include_directories(session_scan_first_frame_ids_overflow PRIVATE "${CMAKE_SOURCE_DIR}/src")
```

The new tests follow that shape verbatim — `${CMAKE_SOURCE_DIR}/src` on the include path, the header
included as `"session/read_first_frame_bounded.hpp"`, and **no link against the engine object**,
because the function is `inline` in the header. `tests/session/CMakeLists.txt:721` even records the
conclusion this feature needs: *"No FIXPP_TEST_HOOKS needed — the function is inline in the internal
header."*

**Why `inline` in the header rather than a declaration + definition in `engine.cpp`.** A declaration
would force every witness TU to link `engine.cpp`'s object, dragging in the whole `Engine`, its
listener/registry machinery, and the OpenSSL surface — for a function whose entire dependency set is
`Transport&`, `Framer`, `pmr_carry_buffer` and a `steady_timer`. The inline header keeps the witness
binaries small and the failure modes local.

**Install-set impact: none, by construction.** `src/` is not an installed include root — only
`include/` is. SC-017 is therefore satisfied without touching any `install()` rule, which is exactly
why C3 chose this location over `include/fixpp/session/detail/`.

**Alternatives rejected.** A `FIXPP_TEST_HOOKS`-gated seam in the accept path (adds a
production-compiled branch; rejected at C3); `include/fixpp/session/detail/` (would make SC-017 real
install-list work instead of a tautology).

---

## D-6 — Test plan: what each witness must be shaped like, and how it is proven RED

FR-010 requires every pin to be **demonstrated RED against pre-fix source**, and the spec's
discrimination note (§SC-012) warns that the obvious boundary test is green under both the delivered
fix and the rejected one.

**RED-proof method — one mechanism for all cells, stated once.** The pre-fix source is `main`'s
`read_first_frame_bounded`. Because D-5 makes it an inline header, the RED run is:
`git show main:src/session/engine.cpp` → extract the pre-fix body into a scratch copy of the new
header → build the witness target against it → record the failure output. This is a *source* A/B on
one function, not a branch checkout, so it cannot be contaminated by stale objects
(`[[feedback_stale_build_objects_false_green_masks_pins]]`). Each cell records its own RED output in
the verify record.

| Cell | Shape | Kills | RED pre-fix because |
|---|---|---|---|
| **B1** (SC-001) | single delivery, cumulative **exactly** `max_bytes`, complete Logon at its head | `>=` | pre-fix rejects at `==` |
| **B2** (SC-012) | **fragmented**: 1000 B, then 3097 B, Logon ends at byte 3500, cumulative 4097 | budget-before-frame | pre-fix rejects at 4097 before feeding; **also fails under the rejected comparison-only fix** — this is the only cell that discriminates |
| **B3** (SC-002) | B1 plus surplus | surplus-carry regression at the boundary | returned length must be 3500, not 4096 |
| **B4** (SC-003) | over-budget, **no** complete frame ever | over-relaxation of FR-003 | must still be `wire_frame_too_large` |
| **B5** (edge) | `buf.size() == max_bytes` on entry to the clamp | clamp off-by-one / zero-length read | a `room == 0` bug spins or asserts |
| **T1** (SC-005/006) | same-drain: deadline `0 ms`, mock transport read completes in the same drain, hand-driven `io_context` | stranded handler | ASan write-to-freed on `timed_out`; session torn down |
| **T2** (SC-015) | `Engine::stop()` mid-read | `total` swallowed by the `||` arm (D-2's trap) | with a bare `async_wait` arm, stop does not abort |
| **T3–T5** (SC-014) | same-drain expiry/success at each of the three transport sites | late `socket_.cancel()` | first post-success operation is cancelled |

**Determinism (SC-016).** T1 uses the D-5 header, a `MockTransport` (`FIXPP_ALLOW_MOCK_TRANSPORT`,
`include/fixpp/transport/test/mock_transport.hpp` — whose contract already binds it to honour
cancellation rather than short-circuit it) and a **hand-driven** `io_context`: arm the timer with a
`0 ms` expiry and script the mock's read to complete without latency, so both completions are ready
**before** the first `poll()`. The ordering under test is constructed, never awaited. No witness in
this feature calls `run_for` with a timing margin.

**T2 must be shown non-vacuous.** A `stop()` test that never actually catches a read in flight
asserts nothing (`[[feedback_ci_gate_observes_not_asserts_witness_skips_into_green]]`). The witness
asserts positively that the read was in flight at the moment `stop()` was issued — via the mock's
scripted read latency plus an observed pre-stop state — and that the coroutine returned
`transport_read_cancelled` rather than `transport_handshake_timeout`.

**FR-011 guard.** `tests/session/engine_firstframe_test.cpp`'s existing over-budget witness uses a
`budget + 1` payload and must pass **unmodified**. Its payload never completes a frame, so under the
new ordering it still reaches step 5 and still trips the budget. If it needs editing, the fix has
overreached — that is the check, not a formality.

---

## D-7 — Allocation discipline (Article VIII §5)

**Finding: not engaged, and the position is not new.** Article VIII §5 bans `new`/`delete` "between
parse and the `fromApp` callback" — the established-session read pump. `read_first_frame_bounded`
runs **once per accepted connection**, before any `Session` exists, and already allocates on that
path today: `pmr_carry_buffer carry{max_bytes, std::pmr::new_delete_resource()}`
(`src/session/engine.cpp:401`). The two `co_spawn` frames the `||` adds are the same order of cost as
the buffer already allocated three lines earlier, once per connection.

**Recorded for Gate A anyway**, because "adds allocations to a path" reads badly out of context: the
delta is **two coroutine frames per accepted connection**, on a path that already allocates, that is
not the hot path, and that is bounded by the accept rate. `bench/` is unaffected — no benchmark
covers the accept path, and Article VIII §3 ("no perf change without a benchmark") is not triggered
because this is not a perf change.

---

## D-8 — Comment corrections (FR-008), enumerated

Three production comments state the pre-fix contract and become false with this change:

| Location | Current claim | Becomes |
|---|---|---|
| `src/session/engine.cpp:373-375` | *"returns when >= 1 complete FIX frame is present … OR when the byte budget is exceeded"* — and the ordering it implies | the delivered order: a complete frame wins over the budget; the budget fires only when no frame is extractable |
| `src/session/engine.cpp:853` | *"4096 bytes max (covers any valid FIX Logon message)"* | true only of a Logon in isolation; must state the coalescing boundary and the `max_bytes + 1` clamp bound |
| `src/session/engine.cpp:390-393` | the Q-2 rationale for `transport.cancel()` from the timer callback | the deadline is now an arm of a join, not a flag-plus-cancel; the Q-2 *requirement* (cancel the in-flight read, do not poll between reads) is **preserved** and must be re-stated, not deleted |

The third matters most: 015's `/simplify` Q-2 rejected between-reads flag polling, and FR-004 carries
that forward. The join satisfies Q-2 more strongly than the flag did (the read is aborted by
cancellation, and the arm is joined), but a reader of the new code must be able to see that the old
requirement was honoured rather than forgotten.
