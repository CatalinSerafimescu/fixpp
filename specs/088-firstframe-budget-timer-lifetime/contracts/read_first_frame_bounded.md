# Contract: `read_first_frame_bounded`

**Feature**: `088-firstframe-budget-timer-lifetime` · **Date**: 2026-08-04

This is an **internal** contract. `read_first_frame_bounded` is not public API and is not installed;
it becomes reachable from tests via `src/session/read_first_frame_bounded.hpp` (FR-016 / research
D-5), mirroring `src/session/scan_first_frame_ids.hpp`. The public surface delta of this feature is
empty (FR-012 / SC-010 / SC-017).

The contract is stated as **before → after** because this feature changes behaviour that already
shipped, and Gate A's question is precisely which parts moved.

---

## Signature

```cpp
namespace fixpp::session::detail {

// Reads raw bytes from an accepted, post-handshake transport into `buf`, bounded by a byte
// budget and a deadline. Returns the EXACT length of the first complete FIX frame on success.
[[nodiscard]] inline asio::awaitable<fixpp::core::expected_t<std::size_t>>
read_first_frame_bounded(fixpp::transport::Transport& transport,
                         std::vector<std::byte>&      buf,
                         std::chrono::milliseconds    deadline,
                         std::size_t                  max_bytes);

}  // namespace fixpp::session::detail
```

**Unchanged**: name, parameters, return type, namespace-visible behaviour to the caller on the
success path (the returned value is still the first frame's exact length, and `buf` still holds the
raw bytes in arrival order so `buf[0..len)` is that frame byte-for-byte).

**Changed**: the enclosing namespace (`detail`, from TU-local) and the linkage (`inline` in a header,
from a definition in `engine.cpp`). Neither is observable to the sole production caller.

---

## Preconditions

| # | Precondition | Enforced by |
|---|---|---|
| P1 | `transport` is connected and, on a TLS profile, post-`async_handshake` | caller (`engine.cpp:838-850`) |
| P2 | `buf` is **empty** on entry | caller (`engine.cpp:857-858`); this is what makes the old loop-top budget check unreachable, and what anchors the clamp proof's base case |
| P3 | `max_bytes >= 1` | caller passes the `kFirstFrameMaxBytes = 4096` constant |
| P4 | the calling coroutine's cancellation state admits `total` | `run_accept_loop` resets to `enable_total_cancellation()` at `engine.cpp:675-676` |

---

## Postconditions

### Success — `expected_t` holds `len`

| # | Postcondition | Before | After |
|---|---|---|---|
| S1 | `len` is the exact byte length of the first complete frame | ✔ | ✔ (unchanged) |
| S2 | `buf[0..len)` is that frame, byte-for-byte | ✔ | ✔ (unchanged) |
| S3 | `buf[len..]` is surplus, carried into the read pump | ✔ | ✔ — but **less surplus near the boundary**, because the clamp stops reading at `max_bytes + 1`. Pinned by witness B3. |
| S4 | Success is returned whenever a complete frame is extractable, **regardless of `buf.size()`** | ✘ **rejected first at `>= max_bytes`** | ✔ (FR-002 / INV-B2) |
| S5 | No completion handler armed by this call is outstanding when it returns | ✘ **the deadline handler may be queued** | ✔ structurally — `parallel_group::async_wait` retires every arm (FR-005 / INV-L1) |

### Failure — `wire_frame_too_large`

| # | Condition | Before | After |
|---|---|---|---|
| F1 | `buf.size() > max_bytes` **and** no complete frame extractable | `buf.size() >= max_bytes`, checked **before** framing | strict `>`, checked **after** framing, at one program point (FR-001/FR-003/FR-007) |
| F2 | the framer itself rejects an over-capacity frame | ✔ | ✔ — a **distinct cause** sharing the same code; preserved deliberately, not merged |

### Failure — `transport_handshake_timeout`

| # | Condition | Before | After |
|---|---|---|---|
| T1 | the deadline elapses before a complete frame | via a flag observed **between** reads, plus `transport.cancel()` from the timer callback aborting the in-flight read | the deadline is an **arm of the join**; winning the race returns this error directly |
| T2 | the in-flight read is aborted on expiry, not merely flagged | ✔ (the 015 `/simplify` Q-2 requirement) | ✔ — **preserved and strengthened**: cancellation is emitted by the join, and the read arm is awaited to completion before this returns |

### Failure — transport errors

Propagated verbatim: `transport_read_cancelled`, `transport_read_eof`, `transport_read_error`,
`transport_read_truncated`, `transport_already_closed`, `transport_read_in_progress`. **No mapping
changes.** In particular a `stop()`-induced abort still surfaces as `transport_read_cancelled`, which
is how the caller distinguishes it from a deadline (`engine.cpp:415-421`).

---

## Invariants over the call

| # | Invariant | Status |
|---|---|---|
| I1 | Peak bytes buffered `<= max_bytes + 1` | **NEW and tighter** (SC-013). Pre-fix the cap was `max_bytes`; a naive frame-before-budget would have made it `max_bytes + 4096`. |
| I2 | Bytes requested per read `>= 1` — never a zero-length read | NEW; follows from I3 (research D-1's inductive proof) |
| I3 | Exactly one budget decision point, at the foot of the loop body | NEW (FR-007); it is what makes I2 provable |
| I4 | `Engine::stop()`'s `total` aborts the call promptly | **held before, at risk from the fix** — requires the deadline arm to reset its cancellation filter (research D-2). FR-015 pins it. |
| I5 | The framer is fed only newly-read bytes, never the whole `buf` | unchanged (F-015-001, `engine.cpp:432-437`) |

---

## What callers may NOT assume

- **Not** that `buf.size() <= max_bytes` on return. It may be `max_bytes + 1`. The sole caller reads
  only `buf[0..len)` and treats the rest as surplus, so it is unaffected — but a future caller that
  sized a buffer on `max_bytes` would be wrong.
- **Not** that a complete frame implies an under-budget peer. Under the delivered invariant a frame
  can be returned at cumulative `max_bytes + 1`. This is the intended behaviour (FR-002), and it is
  what witness B2 pins.
- **Not** that `wire_frame_too_large` means "budget exceeded" specifically — F2 shares the code.

---

## Transport-side contract delta (FR-014)

`asio_plain_transport` and `asio_tls_transport` gain no method and no observable behaviour change on
any success or failure path. The only delta is negative: **a connect or handshake timeout that has
already expired can no longer cancel a socket whose operation subsequently succeeded, nor a socket
belonging to a later attempt** (INV-L4). No error code, no signature, no header-visible type changes;
one private `std::uint64_t` member is added to each class.
