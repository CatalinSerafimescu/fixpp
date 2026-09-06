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
                         fixpp::core::Clock&          clock,      // #377
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
| P1 | `transport` is connected and, on a TLS profile, post-`async_handshake` | caller (`engine.cpp:753-763`) |
| P2 | `buf` is **empty** on entry | caller default-constructs it at `engine.cpp:788` (`std::vector<std::byte> frame_buf;`; `:789` only reserves) and passes it at `:792-793`. This is what makes the old loop-top budget check unreachable, and what anchors the clamp proof's base case. |
| P3 | `1 <= max_bytes < SIZE_MAX` | caller passes the `kFirstFrameMaxBytes = 4096` constant (`engine.cpp:781`), guarded by a `static_assert` at the same site. The upper bound is what makes P5/I6's derived `max_bytes + 1` capacity representable without wrapping — `carry{max_bytes + 1, ...}` (`read_first_frame_bounded.hpp:105`) and `room = (max_bytes + 1) - buf.size()` (`:113`) both wrap at `SIZE_MAX` otherwise (Gate B PR #239 finding B2). |
| P4 | the calling coroutine's cancellation state admits `total` | `run_accept_loop` resets to `enable_total_cancellation()` at `engine.cpp:594`. **Necessary, not sufficient — noted at Gate A round 2.** Admitting `total` at the top of the chain says nothing about whether it is honoured at the bottom: on TLS it is accepted at every hop and then dropped inside the SSL composed op. The sufficient condition is P6. **A TEST caller must establish P4 for itself — added at Gate A round 3 (C2).** `co_spawn` builds its initial state with the **terminal-only** ctor (`asio/impl/co_spawn.hpp:336` → `asio/cancellation_state.hpp:88-100`) and forwards the type verbatim (`co_spawn.hpp:260-263`), so a cell that `co_spawn`s this function directly and emits `total` has that signal die **before** the function is entered — this precondition unmet, and the failure indistinguishable from a defect in the code under test. Every such cell MUST interpose an outer wrapper coroutine whose first statement is the reset, mirroring what `run_accept_loop` does in production (SC-018 clause 4a; research §D-6.12a). |
| P6 | (TLS only) `asio_tls_transport::async_read_some` maps its outgoing cancellation to `terminal` | the transport itself (FR-018 / INV-L6), at `src/transport/asio_tls_transport.cpp:1134`. **NEW at Gate A round 2.** Listed as a precondition of the postconditions rather than a caller obligation, because the caller **cannot** discharge it: `reset_cancellation_state` replaces the single bottom-frame state and the last reset wins (`asio/impl/awaitable.hpp:726-732`), so a call-site wrapper is clobbered by the transport's own reset. Without P6 the `stop()` behaviour stated under *"Failure — transport errors"* does not hold, and this function does not return at all. No equivalent precondition exists for the plain transport. |
| P5 | the framer's carry buffer is constructed with capacity `>= max_bytes + 1` | the implementation itself (`read_first_frame_bounded.hpp:105`), as a value **derived from** the clamp bound. Not a caller obligation, but stated as a precondition of the postconditions below: without it S4/F1 do not hold. See I6. |

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

**S5 is the assertable form of SC-006** (clarification G-2). Observed by draining the executor to
completion after the call returns and requiring the transport to have recorded **zero** `cancel()`
calls. The previously-stated postcondition — a live `Session`'s read pump surviving — is the
*consequence* S5 guarantees, not something the direct-helper target can express.

### Failure — `wire_frame_too_large`

| # | Condition | Before | After |
|---|---|---|---|
| F1 | `buf.size() > max_bytes` **and** no complete frame extractable | `buf.size() >= max_bytes`, checked **before** framing | strict `>`, checked **after** framing, at one program point (FR-001/FR-003/FR-007) |
| F2a | the framer's `parse_frame` rejects a *declared* BodyLength or computed frame length above `Framer::cfg_.max_frame_bytes` (`src/wire/framer.cpp:120-121`, `:129-130`, `:179-180`) | ✔ | ✔ — a **distinct cause** sharing the same code; preserved deliberately, not merged |
| F2b | `pmr_carry_buffer::append` overflows the framer's carry capacity (`src/wire/framer.cpp:199-201`, `:246-248`) | ✔ reachable, and — at capacity `max_bytes` — it **pre-empts F1 on every over-budget input** | **unreachable on this path**, because the carry capacity now equals the cumulative bound (P5 / I6) |

**F1's reachability depends on P5, and that is the point.** With the carry capacity at `max_bytes`,
F2b fires one step before F1 can ever be evaluated — so FR-007's "single budget decision point" would
be dead code in its new position, exactly the defect FR-007 exists to remove. With capacity
`max_bytes + 1`, a cumulative-4097 feed succeeds, produces no frame, and reaches F1. The trace is in
research §D-1a; the source measurement is in
`research/reviews/orchestrator_088-firstframe-budget-timer-lifetime_gate_a_r1_measurements.md`.
F2 therefore **splits** rather than disappears: F2a keeps the cause distinct, F2b goes away, and F1
becomes the sole budget-attributable source of this code on this path (clarification G-3).

### Failure — `transport_handshake_timeout`

| # | Condition | Before | After |
|---|---|---|---|
| T1 | the deadline elapses before a complete frame | via a flag observed **between** reads, plus `transport.cancel()` from the timer callback aborting the in-flight read | the deadline is an **arm of the join**; when it is recorded first (`order[0] == 1`) this error is returned directly. *(Wording tightened at Gate A round 4: **not** "winning the race" — on this path the deadline arm is the only one that completes, and `wait_for_one_success` then cancels the read arm, so the outcome is determined, not raced. The word is avoided deliberately; round 4 withdrew "race" framing elsewhere in favour of expiry ordering. The genuinely undetermined case is the **simultaneous** cancellation of both arms — see the transport-errors block above.)* |
| T2 | the in-flight read is aborted on expiry, not merely flagged | ✔ (the 015 `/simplify` Q-2 requirement) | ✔ — **preserved and strengthened**: cancellation is emitted by the join, and the read arm is awaited to completion before this returns |

### Failure — transport errors

Propagated verbatim: `transport_read_cancelled`, `transport_read_eof`, `transport_read_error`,
`transport_read_truncated`, `transport_already_closed`, `transport_read_in_progress`. **No mapping
changes for transport-originated errors.** A `stop()`-induced abort still surfaces as
`transport_read_cancelled`.

**Amended at Gate A round 2 — that last sentence is a PRECONDITION, not a free consequence, and on
TLS it was false.** *"A `stop()`-induced abort surfaces as `transport_read_cancelled`"* holds only if
the `total` actually reaches the underlying operation. It does on plain TCP (the socket read op
honours `terminal|partial|total`, `asio/detail/reactive_socket_service_base.hpp:716-725`). It does
**not** on TLS under the one-argument cancellation reset: the `total` is relayed unchanged (a filter
is a mask, `asio/cancellation_state.hpp:31-39`) and then discarded by the SSL composed op's
terminal-only inner state (`asio/ssl/detail/io.hpp:100-106` →
`asio/detail/base_from_cancellation_state.hpp:44-48` → `cancellation_state.hpp:88-100`). Under the
joined form that does not produce a *different* error — **it produces no completion at all**: the read
arm never retires, the join never finishes, and this function **never returns**. The sentence is true
as written **only once FR-018 is delivered** (INV-L6), and it is the caller-visible statement that
SC-018's cell T6 pins. See research §D-2a.

> **Further corrected at Gate A round 4 — even WITH FR-018 the exact value is not guaranteed, and
> this contract was the last place still asserting it.** A `stop()`-induced `total` reaches **both**
> arms of the join, and `await_deadline` absorbs the cancellation and **completes normally**
> (research §D-2; since #377 by a `catch` pair rather than `redirect_error`, because
> `Clock::sleep_until` returns an awaitable and no `redirect_error` token applies to it — the
> requirement that neither arm throws is unchanged, since `outcome.index()` is the sole
> discriminator),
> so both arms complete and the returned value is decided entirely by `order[0]`
> (`asio/experimental/awaitable_operators.hpp:352-357` vs `:363-367`): index 0 ⇒
> `transport_read_cancelled`, index 1 ⇒ `transport_handshake_timeout`. Research §D-6.10a establishes
> that this ordering is **not derivable** in the simultaneous-cancellation case, which is exactly why
> round 4 widened **T2a** and **SC-018 clause 4c** from an exact value to a **cancellation-attributable
> set**. The contract must say the same thing. **The guaranteed property is that the call returns
> promptly with a cancellation-attributable failure** — `transport_read_cancelled` **or**
> `transport_handshake_timeout`, and never a success, `transport_already_closed` or
> `transport_read_eof`. `transport_read_cancelled` is the **expected** value and the one a plain-TCP
> path or an un-joined read will produce; it is **not** a contractual guarantee under the join. See
> also *"What callers may NOT assume"* below.

**Corrected at Gate A round 1.** This block previously added *"which is how the caller distinguishes
it from a deadline (`engine.cpp:415-421`)"*. **The caller does not distinguish.**
`engine.cpp:794-797` is `if (!read_r.has_value()) { transport->close(); continue; }` — every error
takes the identical arm. No assertion is added for a distinction nothing consumes; the sentence is
simply removed.

**One mapping DOES change, on the deadline path.** Stated as a table so Gate B does not have to infer
it:

| Situation | Before | After |
|---|---|---|
| deadline expires with a read **in flight** | the timer callback calls `transport.cancel()`, the read completes `operation_aborted`, and the error is propagated verbatim ⇒ **`transport_read_cancelled`** (`engine.cpp:394-397` then `:415-421`; the code's own comment at `:416-419` concedes *"The specific code is unobservable here"*) | the deadline arm wins the join ⇒ **`transport_handshake_timeout`**, returned directly |
| deadline expires **between** reads | `transport_handshake_timeout` (`:454`) | `transport_handshake_timeout` (unchanged) |

The post-fix behaviour is the *more* honest of the two — the pre-fix code documented the timeout and
returned the cancellation — and no caller or test observes the difference. Recorded, not asserted.

---

## Invariants over the call

| # | Invariant | Status |
|---|---|---|
| I1a | Bytes read from the peer before the decision `<= max_bytes + 1` | **NEW** (SC-013 bound 1) |
| I1b | Logical `buf.size() <= max_bytes + 1` | **NEW and tighter** (SC-013 bound 2). **Corrected at Gate A round 1: pre-fix the logical cap was 8191, not `max_bytes`** — `engine.cpp:408` rejects only *before* the read, `:413-414` requests the full 4096, and `:424` inserts before `:426` rejects, so pre-fix `buf.size()` peaks at `4095 + 4096`. The clamp is still a genuine tightening, just from 8191. A naive frame-before-budget would have made it `max_bytes + 4096`. |
| I1c | Peak **resident** bytes attributable to the peer = `buf` + `carry` + `read_buf` ≈ 4097 + 4097 + 4096 ≈ **12 KiB**, plus `std::vector` capacity slack | **NEW** (SC-013 bound 3). The first draft stated I1 over `buf` alone; an invariant written over one of three co-resident buffers is what let the carry-capacity collision go unnoticed. |
| I2 | Bytes requested per read `>= 1` — never a zero-length read; and no read *completes* with 0 bytes and no error | NEW; the request side follows from I3 (research D-1's inductive proof). **The completion side is discharged by the CLAMP ALONE** — `room >= 1 ⇒ want >= 1`, so the request is never empty — and **not** by a transport-contract claim. *(Corrected at Gate A round 4: this cell previously rested on "the transports' `eof` mapping", which is the auxiliary defence research §D-1 **withdrew**, and it carried no qualifier. Unqualified the claim is **false**: for a **zero-length** request asio succeeds with zero bytes — `asio/detail/impl/socket_ops.ipp:890-895`, reached via `all_empty(buffers)` at `asio/detail/reactive_socket_service_base.hpp:426-428`.)* For a **non-empty** request no production transport completes with 0 bytes and no error (`include/fixpp/transport/transport.hpp:101-104`; EOF/error mapping at `asio_plain_transport.cpp:224-233` and `asio_tls_transport.cpp:1167-1182` — **not** `:1162-1163`, which is the *initiation*). |
| I3 | Exactly one budget decision point, at the foot of the loop body, **and it is reachable** | NEW (FR-007); it is what makes I2 provable, and I6 is what makes it reachable |
| I4 | `Engine::stop()`'s `total` aborts the call promptly, **on both transports** | **held before only in a weak sense, BROKEN by the round-1 fix, restored by FR-018.** Requires **two** things, not one: *(a)* the deadline arm resets its cancellation filter (research D-2), and *(b)* the TLS transport's read installs an **OUT map** so the `total` is forwarded as `terminal` and actually aborts the SSL read (research §D-2a / INV-L6 / FR-018). With (a) alone the call **never returns** under `stop()` on TLS, and `stop()`'s own deadline-less join (`src/session/engine.cpp:1273-1284`) hangs with it. Pre-fix the abort on TLS came 5 s late via the cancellation-immune deadline lambda, so *"promptly"* was already false there; with (b) it is true for the first time. FR-015 + **SC-018** pin it; cell **T6** is the only one that can. *(Corrected at Gate A round 2.)* |
| I5 | The framer is fed only newly-read bytes, never the whole `buf` | unchanged (F-015-001, `read_first_frame_bounded.hpp:141-144,148-149`) |
| I6 | `carry.capacity() >= max_bytes + 1` — the carry capacity is **derived from** I1b's bound, not an independent constant | **NEW** (P5 / FR-013). Violated by the pre-round-1 design (`engine.cpp:402` builds it at `max_bytes`), which made S4 and F1 undeliverable — see the F1/F2 note above. |
| I7 | The deadline is resolved once, before the loop, to an absolute instant; the deadline arm never moves it | **NEW** (FR-017), **and since #377 it is STRUCTURAL rather than a rule.** Held pre-fix by the loop's shape; put at risk by the join, which turns the deadline into a per-iteration `co_await`. It was then carried by a prohibition on `await_deadline` (*"MUST NOT call `expires_after`"*) — a rule the next editor had to read and honour. `await_deadline` now takes an absolute `fixpp::core::steady_time_point`, so re-sleeping to the same instant is idempotent and the arm **cannot** move the deadline. The mutant moved up one level, to the line computing `abs_deadline`; cell **B6** kills it there (measured post-port: `wire_frame_too_large`, `buf.size() == 201`). |

---

## What callers may NOT assume

- **Not** that `buf.size() <= max_bytes` on return. It may be `max_bytes + 1`. The sole caller reads
  only `buf[0..len)` and treats the rest as surplus, so it is unaffected — but a future caller that
  sized a buffer on `max_bytes` would be wrong.
- **Not** that a complete frame implies an under-budget peer. Under the delivered invariant a frame
  can be returned at cumulative `max_bytes + 1`. This is the intended behaviour (FR-002), and it is
  what witness B2 pins.
- **Not** that `wire_frame_too_large` means "budget exceeded" specifically — F2a shares the code.
- **Not** which of the two cancellation-attributable errors an `Engine::stop()` produces. *(Added at
  Gate A round 4.)* A `total` reaches **both** arms of the join and both complete, so the returned
  value is decided by `order[0]` (`asio/experimental/awaitable_operators.hpp:352-357` vs `:363-367`)
  — an ordering research §D-6.10a shows is **not derivable**. The caller may rely on *"returns
  promptly with a cancellation-attributable failure"*; it may **not** rely on
  `transport_read_cancelled` specifically. Today's sole caller is unaffected — `engine.cpp:794-797`
  takes the identical `close(); continue;` arm for every error — and **that is what keeps this a
  documentation obligation rather than a defect**. A future caller that branched on the distinction
  would be relying on scheduler internals.

---

## Transport-side contract delta (FR-014)

`asio_plain_transport` and `asio_tls_transport` gain no **public** method and no observable behaviour
change on any success or failure path. The delta is negative in behaviour: **a connect or handshake
timeout that has already expired can no longer cancel a socket whose operation subsequently
succeeded, nor a socket belonging to a later attempt, nor touch the transport at all once it has been
destroyed** (INV-L4 / INV-L5). No error code, no signature, no public-header-visible type changes.

**Internal surface delta, corrected at Gate A round 1** — the first draft said *"one private
`std::uint64_t` member is added to each class"*, `plan.md` said *"2 production headers gain one member
each"*, and `plan.md`'s Structure Decision described three members. All three were re-derived once the
mechanism changed (research §D-4.1):

| Per class | Added |
|---|---|
| `asio_plain_transport` | one `std::shared_ptr<timer_epoch_state>` member; one user-provided destructor (body retires the epochs); one `timer_epochs()` const accessor |
| `asio_tls_transport` | the same three |

Two members in total, one per class; the *counters* inside the shared state are one per timer (plain:
`connect`; TLS: `connect` + `handshake`). All of it lives in `src/transport/asio_plain_transport.hpp`
and `src/transport/asio_tls_transport.hpp` — **internal headers**; `include/fixpp/transport/` contains
no such files. `src/` is not an installed include root, and that is what preserves SC-010/SC-017.

## Transport-side contract delta (FR-018) — *added at Gate A round 2*

A **second, independent** transport-side delta, on a different site and mechanism from FR-014's:

| | |
|---|---|
| **Site** | `asio_tls_transport::async_read_some`, the cancellation reset at `src/transport/asio_tls_transport.cpp:1134` — **TLS only** |
| **Change** | one-argument `reset_cancellation_state(enable_total_cancellation())` → **two-argument**, with an OUT filter mapping any non-`none` cancellation to `terminal`. Mirrors `:918-933` (`async_connect`, 016 T008) |
| **Observable delta** | `asio_tls_transport::async_read_some` now completes with `transport_read_cancelled` when its coroutine is cancelled with `cancellation_type::total`, **as `include/fixpp/transport/transport.hpp:105-106` already documents that it does**. Today it does not: the signal is dropped inside the SSL composed op and the read continues indefinitely |
| **Surface delta** | **none.** No member, no method, no signature, no error code, no header change. One changed argument list and its rationale comment |
| **Blast radius** | the **two** production callers of `Transport::async_read_some` — `read_first_frame_bounded.hpp:122` (this contract) and `engine.cpp:460` (the session read pump). The pump's own termination comment (`engine.cpp:383-389`) already contracts for this behaviour; INV-4a is dispositioned **SAFE BY STRAND CONSTRUCTION** (research §D-2a.7) |
| **Not applied to** | `asio_plain_transport` — its socket read op honours `total` natively (`asio/detail/reactive_socket_service_base.hpp:716-725`), where `total`'s no-side-effects semantics are genuinely available. The asymmetry is deliberate |

This delta is **negative in surface and positive in behaviour**: nothing a caller can name changes,
and a cancellation contract that was documented-but-false becomes true.
