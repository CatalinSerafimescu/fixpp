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
| `buf` | `std::vector<std::byte>&` | owned by `run_accept_loop` (`engine.cpp:857`) | cumulative raw bytes in arrival order; `buf[0..len)` is byte-for-byte the first frame |
| `carry` | `wire::pmr_carry_buffer` | coroutine frame | the framer's cross-`feed` accumulation; capacity `max_bytes` |
| `framer` | `wire::Framer` | coroutine frame | stateful boundary detector |
| `read_buf` | `std::array<std::byte, 4096>` | coroutine frame | scratch for one `async_read_some` |
| `timer` | `asio::steady_timer` | coroutine frame | the deadline |
| ~~`timed_out`~~ | ~~`bool`~~ | ~~coroutine frame~~ | **REMOVED** — it existed only to carry the timer callback's decision back into the loop condition. With the join (D-2) the deadline is an arm of the race, so there is nothing to flag. Its removal is what eliminates the write-to-freed leg at its source rather than making the write safe. |
| `timer_epoch_` | `std::uint64_t` | **transport member** (NEW, D-4) | which connect/handshake attempt armed the currently-relevant timer; strand-confined, no atomic |

**Parameters** (unchanged): `transport` (`Transport&`), `deadline` (`5000 ms`), `max_bytes`
(`4096`). This feature changes neither constant — only when and how they are consulted.

### 1.2 Outcome

`core::expected_t<std::size_t>` — on success the **exact length of the first frame**, not the number
of bytes buffered. The caller relies on this to deliver `buf[0..len)` to `on_inbound_frame` and carry
`buf[len..]` into the read pump (F-015-002). Unchanged by this feature, and pinned at the boundary by
witness B3 because the clamp changes how much surplus exists there.

Error values, all pre-existing — **no error code is added or removed** (FR-012):

| Value | Raised when |
|---|---|
| `wire_frame_too_large` | budget exceeded with no extractable frame (FR-003), **or** the framer rejects an over-capacity frame (distinct cause, same code — pre-existing, preserved) |
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
| **INV-B4** | `buf.size() <= max_bytes + 1` at every point after the insert. | not applicable pre-fix (no clamp); becomes SC-013 |
| **INV-B5** | The bytes requested per read, `room = max_bytes + 1 - buf.size()`, satisfy `1 <= room <= max_bytes + 1`. Never 0, never wrapped. | new; guaranteed by INV-B3's placement (see the proof in research D-1) |
| **INV-L1** | No completion handler armed by this coroutine may run after the coroutine's frame is destroyed. | **VIOLATED** — `timer.cancel()` cannot un-queue a completed handler |
| **INV-L2** | No completion handler armed by this coroutine may call `cancel()` on a transport after ownership has moved to a `Session` (`engine.cpp:922`). | **VIOLATED** — the sharper leg |
| **INV-L3** | `Engine::stop()`'s `cancellation_type::total` aborts an in-flight first-frame read promptly. | held pre-fix; **at risk from the fix** — see research D-2, and it is why the deadline arm is wrapped |
| **INV-L4** | (transports) A timeout handler from attempt *N* may not cancel a socket belonging to attempt *N+1*, nor a socket whose operation already succeeded. | **VIOLATED** at all three sites |

### 2.1 How the invariants are established

- **INV-B1/B2/B3** — by the loop order in research D-1: feed, frame-wins return, then one strict-`>`
  budget check at the foot of the body.
- **INV-B4/B5** — by the clamp, whose non-underflow and non-zero properties follow inductively from
  INV-B3's placement. This coupling is the reason FR-007 names the placement rather than leaving it
  to taste.
- **INV-L1/L2** — structurally, by `parallel_group::async_wait` retiring **every** arm before the
  join completes. There is no surviving handler to constrain, so these stop being invariants that
  code must maintain and become properties of the composition.
- **INV-L3** — by the deadline arm resetting to `enable_total_cancellation()` before awaiting. Not
  automatic: asio's default `InFilter` is `enable_terminal_cancellation`
  (`asio/cancellation_state.hpp:199-201`), so the naive form would break this invariant.
- **INV-L4** — by the monotonic `timer_epoch_` compared inside the handler. A plain `bool` would not
  suffice: attempt *N*'s stale handler could observe attempt *N+1*'s freshly-cleared flag.

---

## 3. State transitions — one iteration of the read loop

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
rather than silently.

---

## 4. Transport-site state (D-4)

```
attempt N:                                  handler armed by attempt N:
  epoch = ++timer_epoch_                      if (ec) return;                  // cancelled
  timer.async_wait([this, epoch]{...})        if (epoch != timer_epoch_) return;  // STALE (INV-L4)
  <operation>                                 socket_.cancel(ignored);
  ++timer_epoch_        // retire
  timer.cancel()
```

`timer_epoch_` is a plain `std::uint64_t` member, mutated only on the transport's strand — the same
confinement `read_in_flight_` already relies on. One member per transport: the TLS transport's
connect and handshake timers are **sequential, never concurrent**, so they share it safely.

Overflow is not a concern at 2⁶⁴ attempts; no wrap-handling is added, and none is needed
(`[[feedback_truncated_timestamp_wrap_false_linearizability_failure]]` is about a 31-bit truncation,
not a 64-bit counter).
