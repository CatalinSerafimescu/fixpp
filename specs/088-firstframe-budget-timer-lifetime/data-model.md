# Data Model: 088 — bounded first-frame read

**Feature**: `088-firstframe-budget-timer-lifetime` · **Date**: 2026-08-04

There is no persisted data and no new type in this feature. What follows models the **state carried
across one bounded first-frame read** and the **invariants that must hold over it** — because the two
defects are both invariant violations, and the fix is expressible as "restore these invariants".

---

## 1. Entities

### 1.1 `FirstFrameRead` — the per-connection read state (implicit; coroutine locals)

| Field | Type | Lifetime | Role |
|---|---|---|---|
| `buf` | `std::vector<std::byte>&` | owned by `run_accept_loop` (`engine.cpp:857`, passed at `:861-862`) | cumulative raw bytes in arrival order; `buf[0..len)` is byte-for-byte the first frame |
| `carry` | `wire::pmr_carry_buffer` | coroutine frame (`engine.cpp:402`) | the framer's cross-`feed` accumulation. **Capacity `max_bytes + 1` — a value DERIVED from the C1 clamp bound, not an independent constant.** See §1.1a. |
| `framer` | `wire::Framer` | coroutine frame | stateful boundary detector |
| `read_buf` | `std::array<std::byte, 4096>` | coroutine frame | scratch for one `async_read_some`; the clamp caps how much of it is requested, it is not resized (C1's second rejected alternative). **This is deliberate and FR-013 does NOT require otherwise** — *scoped at Gate A round 3 (C12), where FR-013's "every buffer on this path" literally contradicted this row*. FR-013 binds the **cumulative** buffers (`buf`, `carry`), which must hold the whole prefix; a **per-read scratch** need not, because every request is `min(read_buf.size(), room)`. A scratch smaller than the bound costs at most one extra loop iteration, and enlarging it to 4097 would be functionally inert |
| `timer` | `asio::steady_timer` | coroutine frame | the deadline. **Armed exactly once, before the loop, with an absolute expiry** (FR-017 / research D-1b); the join's deadline arm re-awaits it and never re-arms it. |
| ~~`timed_out`~~ | ~~`bool`~~ | ~~coroutine frame~~ | **REMOVED** — it existed only to carry the timer callback's decision back into the loop condition. With the join (D-2) the deadline is an arm of the race, so there is nothing to flag. Its removal is what eliminates the write-to-freed leg at its source rather than making the write safe. |
| `timer_epochs_` | `std::shared_ptr<timer_epoch_state>` | **transport member** (NEW, D-4.1) | which connect/handshake attempt armed the currently-relevant timer. **Held in shared state the handler owns by value, not as a plain member** — see §4. Strand-confined integers, no atomic. |

**Parameters** (unchanged): `transport` (`Transport&`), `deadline` (`5000 ms` in production),
`max_bytes` (`4096` in production). This feature changes neither constant — only when and how they are
consulted. Both are genuine **parameters**, which is what lets the direct-helper witnesses (C3/FR-016)
run at small scales (`max_bytes = 200`, `deadline = 50 ms`) and stay deterministic and fast.

### 1.1a `carry`'s capacity is derived, and the derivation is load-bearing

`Framer::feed` appends every fed byte into `carry` **before any parse** and fails the whole feed on
overflow (`src/wire/framer.cpp:194-201`; `pmr_carry_buffer::append` at
`include/fixpp/wire/framer.hpp:45-51` is a hard, non-reallocating capacity check). So the carry
capacity is an **upper bound on the cumulative bytes the loop may ever hold**, evaluated one step
*before* the accept-path budget.

- **Requirement**: `carry.capacity() >= max(buf.size()) = max_bytes + 1` (INV-B4).
- **Delivered**: exactly `max_bytes + 1`. Every byte in `carry` came from `buf`, and `carry` holds
  only the trailing unconsumed suffix of what has been fed, so `carry.size() <= buf.size()`. The
  capacity is therefore exactly sufficient and never slack.
- **What the pre-round-1 value did**: at capacity `max_bytes` (today's `engine.cpp:402`), the
  cumulative-4097 case fails inside `carry.append` before the frame can be found — so SC-012 is RED
  after the fix as well as before it, and FR-007's budget check is unreachable because the framer
  always rejects one step earlier. Full derivation and traces: research §D-1a.
- **Scope**: this `carry` is a coroutine local of the first-frame read. It is **not** the
  session-lifetime `SessionConfig::framer_carry_arena` the class comment describes
  (`include/fixpp/wire/framer.hpp:22-26`), and the read pump's carry is a separate object with its own
  capacity constant (`engine.cpp:494`). No other consumer is affected.

### 1.2 Outcome

`core::expected_t<std::size_t>` — on success the **exact length of the first frame**, not the number
of bytes buffered. The caller relies on this to deliver `buf[0..len)` to `on_inbound_frame` and carry
`buf[len..]` into the read pump (F-015-002). Unchanged by this feature, and pinned at the boundary by
witness B3 because the clamp changes how much surplus exists there.

Error values, all pre-existing — **no error code is added or removed** (FR-012):

| Value | Raised when |
|---|---|
| `wire_frame_too_large` | budget exceeded with no extractable frame (FR-003), **or** the framer's `parse_frame` rejects a declared BodyLength / computed frame length above `Framer::cfg_.max_frame_bytes` (`src/wire/framer.cpp:120-121`, `:129-130`, `:179-180` — distinct cause, same code, pre-existing, preserved). The framer's third route to this code, a `pmr_carry_buffer::append` overflow (`:199-201`, `:246-248`), becomes **unreachable on this path** once the carry capacity equals the cumulative bound — see §1.1a and clarification G-3. |
| `transport_handshake_timeout` | the deadline arm won the join |
| `transport_read_cancelled` / `_eof` / `_error` / `_truncated` / `transport_already_closed` | propagated verbatim from the transport |

---

## 2. Invariants

Each is stated as it must hold **after** this feature. `INV-B*` are budget/framing; `INV-L*` are
lifetime. The "pre-fix" column is what makes each one a defect rather than a nicety.

| ID | Invariant | Pre-fix status |
|---|---|---|
| **INV-B1** | The connection is closed on the budget **only if** `buf.size() > max_bytes`. Equality never closes. | **VIOLATED** — `>=` at `engine.cpp:408` and `:426` |
| **INV-B2** | If a complete first frame is extractable from the bytes read, it is returned — regardless of `buf.size()`. | **VIOLATED** — the budget check precedes `framer.feed` (`:436`) |
| **INV-B3** | Exactly one program point decides the budget, and it sits **after** the framing step of the same iteration. | **VIOLATED** — two checks, both before the feed; the loop-top one unreachable |
| **INV-B4** | `buf.size() <= max_bytes + 1` at every point after the insert. | not applicable pre-fix (no clamp); becomes SC-013 bound 2 |
| **INV-B5** | The bytes requested per read, `room = max_bytes + 1 - buf.size()`, satisfy `1 <= room <= max_bytes + 1`. Never 0, never wrapped. **And no read may *complete* with 0 bytes and no error.** Discharged by the clamp: `room >= 1 ⇒ want >= 1`, so **the request is never empty**, and no production transport can complete a **non-empty** stream read with zero bytes and no error — `include/fixpp/transport/transport.hpp:101-104`, *"Awaitable completes with byte count (**always > 0 on success** per ASIO async_read_some); EOF → transport_read_eof"*, with the EOF/error mapping at `asio_plain_transport.cpp:224-233` and `asio_tls_transport.cpp:1167-1182`. A witness must not script a zero-byte success on a non-empty request. *(**Qualifier added at Gate A round 4**: unqualified, the claim is **false** — asio succeeds with zero bytes on a **zero-length** request, `asio/detail/impl/socket_ops.ipp:890-895` via `all_empty(buffers)` at `asio/detail/reactive_socket_service_base.hpp:426-428`. Round 3 corrected the citation but not the missing qualifier. Resting the property on the clamp removes the dependency entirely. TLS mapping line numbers also corrected, `:1167-1182`.)* | new; guaranteed by INV-B3's placement (see the proof in research D-1) |
| **INV-B6** | `carry.capacity() >= max_bytes + 1`, i.e. **at least** INV-B4's bound. Every byte the loop buffers passes through `carry` before it can be parsed, so a capacity below the cumulative bound rejects the feed before INV-B2 can be evaluated and before INV-B3's decision point can be reached. | **VIOLATED by the pre-round-1 design** — `engine.cpp:402` builds it at `max_bytes`; see §1.1a and research §D-1a |
| **INV-B7** | The deadline timer is armed exactly once, before the loop, with an absolute expiry; the deadline arm re-awaits it and never calls `expires_after`. | held pre-fix (arming is at `engine.cpp:386`, outside the loop); **at risk from the fix** — the join makes the deadline a per-iteration `co_await`, so a re-arm has an entirely natural place to appear. FR-017 / research D-1b. |
| **INV-L1** | No completion handler armed by this coroutine may run after the coroutine's frame is destroyed. | **VIOLATED** — `timer.cancel()` cannot un-queue a completed handler |
| **INV-L2** | No completion handler armed by this coroutine may call `cancel()` on a transport after ownership has moved to a `Session` (`engine.cpp:922`). | **VIOLATED** — the sharper leg |
| **INV-L3** | `Engine::stop()`'s `cancellation_type::total` aborts an in-flight first-frame read promptly, **on both transports**. | **Corrected at Gate A round 2.** Pre-fix it held only in the weak sense — on **TLS** the `total` never reached the read at all; the read was aborted 5 s later by the cancellation-immune deadline lambda's `transport.cancel()`, so *"promptly"* was already false there. **Broken outright by the round-1 fix**: the join makes the un-abortable read arm block the group, so `stop()` hangs unboundedly (research §D-2a). Restored — and for the first time actually *prompt* on TLS — by **INV-L6**. Pinned by T2a (mock, deadline-arm mutant only), T2b (engine scope) and **T6** (real TLS, the only discriminating cell). |
| **INV-L6** | (TLS transport) A cancellation accepted by `asio_tls_transport::async_read_some`'s coroutine MUST be **forwarded in a type the underlying SSL operation honours** — i.e. mapped to `terminal` on the way out, never relayed as `total`. | **NEW at Gate A round 2** (FR-018 / SC-018). Violated by the one-argument reset at `src/transport/asio_tls_transport.cpp:1134`: `asio::cancellation_filter` is a **mask**, not a map (`asio/cancellation_state.hpp:31-39`), and the one-arg form sets both filters (`:121-126`), so `total` is relayed as `total` and then discarded by the SSL composed op's terminal-only inner state (`asio/ssl/detail/io.hpp:100-106` → `asio/detail/base_from_cancellation_state.hpp:44-48` → `cancellation_state.hpp:88-100`). **Not an invariant of the plain transport**: its socket read op honours `terminal\|partial\|total` natively (`asio/detail/reactive_socket_service_base.hpp:716-725`), so no map is added there and none is wanted. |
| **INV-L4** | (transports) A timeout handler from attempt *N* may not cancel a socket belonging to attempt *N+1*, nor a socket whose operation already succeeded. | **VIOLATED** at all three sites |
| **INV-L5** | (transports) A timeout handler must decide staleness **without dereferencing the transport**, because the transport may already be destroyed when it runs. | **VIOLATED** at all three sites — and the pre-round-1 remedy (a plain `std::uint64_t` member compared inside the handler) would have violated it too. See §4 and the census correction in `spec.md`. |

### 2.1 How the invariants are established

- **INV-B1/B2/B3** — by the loop order in research D-1: feed, frame-wins return, then one strict-`>`
  budget check at the foot of the body.
- **INV-B4/B5** — by the clamp, whose non-underflow and non-zero properties follow inductively from
  INV-B3's placement. This coupling is the reason FR-007 names the placement rather than leaving it
  to taste.
- **INV-B6** — by constructing `carry` with `max_bytes + 1` and commenting the derivation at the
  construction site, so the two numbers cannot drift. **This is the invariant whose absence made
  INV-B2 and INV-B3 undeliverable in the first draft**, and it is why it is stated as an invariant
  rather than left as an implementation detail.
- **INV-B7** — by keeping `expires_after` in the prologue and giving `await_deadline` a documented
  prohibition on re-arming, pinned by cell B6 (research §D-6.1).
- **INV-L1/L2** — structurally, by `parallel_group::async_wait` retiring **every** arm before the
  join completes. There is no surviving handler to constrain, so these stop being invariants that
  code must maintain and become properties of the composition. Observed by the T1 cell as *zero*
  `cancel()` calls on the transport after the call returns and the context is drained (contract S5).
- **INV-L3** — by **two** mechanisms, not one, and the second was missing at round 1. *(a)* The
  **deadline** arm resets to `enable_total_cancellation()` before awaiting — not automatic, since
  asio's default `InFilter` is `enable_terminal_cancellation`
  (`asio/cancellation_state.hpp:199-201`), so the naive form breaks the invariant on that arm.
  *(b)* The **read** arm needs INV-L6: without it the `total` reaches the SSL op as `total` and is
  dropped, so on TLS the arm never retires and the join — and therefore `stop()` — never completes.
  Mechanism (a) alone leaves `stop()` **hanging**, which is why round 1's design was worse than the
  defect it fixed. Both are required; neither substitutes for the other.
- **INV-L6** — by replacing the one-argument reset at `src/transport/asio_tls_transport.cpp:1134`
  with the two-argument form whose OUT filter maps any non-`none` cancellation to `terminal`, exactly
  as the same file's `async_connect` already does at `:918-933` (016 T008). **It cannot be
  established at the call site**: `this_coro::reset_cancellation_state` replaces the single
  bottom-frame cancellation state (`asio/impl/awaitable.hpp:726-732`) and the last reset wins, so the
  transport's own first-statement reset clobbers anything an engine-side arm wrapper installs. The
  invariant therefore belongs to the transport, and the guard against its silent removal is the
  in-source rationale comment FR-018 requires — deleting the OUT filter as "redundant" leaves **every
  test green except T6**.
- **INV-L4** — by the monotonic epoch counter compared inside the handler. A plain `bool` would not
  suffice: attempt *N*'s stale handler could observe attempt *N+1*'s freshly-cleared flag.
- **INV-L5** — by holding the epoch counters in a `std::shared_ptr<timer_epoch_state>` that the
  handler captures **by value**, and by retiring every counter in the transport's **destructor body**.
  Members are destroyed after the body runs, so *guard passed ⇒ the destructor body has not run ⇒
  `this` is alive*. A plain member counter cannot establish this: the comparison itself would read
  through the dangling pointer. See §4 and research §D-4.1.

---

## 3. State transitions — one iteration of the read loop

Prologue, once: `carry{max_bytes + 1}` (INV-B6) · `timer.expires_after(deadline)` (INV-B7).

```
                        ┌──────────────────────────────┐
                        │ room = max_bytes+1-buf.size()│   INV-B5: room >= 1
                        │ want = min(4096, room)       │
                        └──────────────┬───────────────┘
                                       ▼
                     ┌─────────────────────────────────────┐
                     │  co_await ( read(want) || deadline ) │  both arms retire
                     └───────┬──────────────────┬──────────┘  before resume (INV-L1/L2)
                deadline won │                  │ read won
                             ▼                  ▼
             transport_handshake_timeout   read failed? ──yes──► propagate error
                                                │ no
                                                ▼
                                     buf.insert(read_buf[0..n))     INV-B4
                                                │
                                                ▼
                                     framer.feed(new bytes only)
                                       │            │
                            feed error │            │ ok
                                       ▼            ▼
                                  propagate    frame present? ──yes──► co_return frame.size()
                                                    │ no                        (INV-B2)
                                                    ▼
                                        buf.size() > max_bytes? ──yes──► wire_frame_too_large
                                                    │ no                        (INV-B1/B3)
                                                    └──────────► next iteration
```

**Note on the feed argument.** The framer is fed **only the newly-read bytes**, never the whole
`buf` — it accumulates unconsumed bytes in `carry` across calls, so re-feeding would duplicate the
carried prefix and corrupt a fragmented first frame. This is the pre-existing F-015-001 fix
(`engine.cpp:432-437`) and it is **preserved verbatim**; witness B2's fragmented shape exercises
exactly this path at the new boundary, so a regression here surfaces as a failed discrimination cell
rather than silently. It is also *why* INV-B6 is needed: because only the new bytes are fed, the
framer — not the caller — is the party holding the cumulative bytes, in `carry`, and its capacity is
therefore the binding constraint on the cumulative bound.

---

## 4. Transport-site state (D-4)

```
struct timer_epoch_state {          // strand-confined plain integers, no atomics
    std::uint64_t connect{0};
    std::uint64_t handshake{0};     // TLS only
};
std::shared_ptr<timer_epoch_state> timer_epochs_;   // one control block per transport instance

attempt N:                                    handler armed by attempt N (captures the shared_ptr
  epoch = ++timer_epochs_->connect             BY VALUE, and `this` only for socket_):
  timer.async_wait(                              if (ec) return;                       // cancelled
      [this, epochs = timer_epochs_, epoch]{…})  if (epoch != epochs->connect) return; // STALE (INV-L4)
  <operation>                                    socket_.cancel(ignored);              // `this` alive (INV-L5)
  ++timer_epochs_->connect   // retire
  timer.cancel()

~transport() { ++timer_epochs_->connect; ++timer_epochs_->handshake; }   // destructor BODY
```

**Why shared state and not a plain member.** The handler must decide staleness *before* it touches the
transport, because the transport may already be gone: `reconnect_fsm.cpp:250-252` and `:284-286`
destroy a block-scope `std::unique_ptr` on the failure arm, and `engine.cpp:841-844` does the same to
the accept loop's `transport` (INV-L5 and the census correction in `spec.md`). A `std::uint64_t`
*member* would be read through that dangling `this`. The `shared_ptr` captured by value outlives the
transport unconditionally, and the destructor-body retirement makes the implication
*guard passed ⇒ `this` alive* true. The retirement **must** be a destructor-body statement: members
are destroyed after the body, which is what sequences the retirement before `socket_`'s destruction.

**Precondition, stated rather than assumed.** Handler and destructor must run on the same strand. That
holds at all three owners: `reconnect_fsm`'s transport is destroyed inside the FSM coroutine running
on the executor the transport was minted with (`reconnect_fsm.cpp:242`), and the accept loop's
transport is destroyed inside `run_accept_loop` on the session strand it was minted on
(`engine.cpp:674-676`, strand assertion at `:822`). This is the same confinement `read_in_flight_`
already relies on (`src/transport/asio_plain_transport.hpp:45-48`,
`src/transport/asio_tls_transport.hpp:281-285`).

**One counter per timer**, not per transport: the plain transport uses only `connect`;
`asio_tls_transport` uses `connect` **and** `handshake`. The two TLS timers
are in fact strictly ordered today — `async_connect` (`:869`) and `async_handshake` (`:984`) are
awaited sequentially by both drivers (`reconnect_fsm.cpp:250` then `:284`; the accept path uses only
the handshake, `engine.cpp:842`), and `reconnect_fsm` builds a fresh transport per attempt
(`:242-247`) — so a shared counter would be correct. It is split anyway because that correctness rests
on a sequencing property of the *callers* which the transport does not enforce; sharing would make a
future interleaving silently reintroduce this exact defect class (see research §D-4).

**Surface delta, corrected.** One `std::shared_ptr<timer_epoch_state>` member per class (**two members
total, one each** — not "one `std::uint64_t` to each class", and not "three members"), plus one
user-provided destructor per class, plus one `timer_epochs()` const accessor per class used by the
SC-014 cells. All in `src/transport/*.hpp`, which is not an installed include root — that is what
preserves SC-010/SC-017.

Overflow is not a concern at 2⁶⁴ attempts; no wrap-handling is added, and none is needed
(`[[feedback_truncated_timestamp_wrap_false_linearizability_failure]]` is about a 31-bit truncation,
not a 64-bit counter).

**INV-L6 is in the same file and is NOT part of this state — stated so the two are not conflated.**
FR-018 changes the filter argument of the `reset_cancellation_state` call at
`src/transport/asio_tls_transport.cpp:1134`, inside `async_read_some`. It adds **no member, no
counter, no destructor obligation and no accessor**, it does not participate in the epoch protocol
above, and the epoch protocol does not depend on it. The surface delta stated in this section —
**two** members total, one `std::shared_ptr<timer_epoch_state>` per class — is therefore **unchanged
by the round-2 amendment**, and SC-010/SC-017 continue to hold for the same reason (nothing under
`include/` is touched but the test-only mock).
