# Research: 088 — first-frame budget boundary + deadline-timer handler lifetime

**Feature**: `088-firstframe-budget-timer-lifetime` · **Date**: 2026-08-04 (amended 2026-08-05, Gate A round 2) · **Spec**: [spec.md](./spec.md)

Every decision below is grounded in a source read performed for this feature — the asio behaviours
are quoted from the **pinned** `asio/1.38.0` headers in the Conan cache
(`conanfile.py:67`), not from documentation or memory. Where a claim is load-bearing the file:line is
given so Gate A can re-derive it.

> **Seven decisions are already LOCKED** by user decision at `/specify` (Q1–Q3) and `/clarify`
> (C1–C4), recorded in `spec.md` §Clarifications. This document does not re-open them; it resolves
> the residual *design* questions those decisions handed to `/plan`.
>
> **Amended at Gate A round 2 (2026-08-05) — §D-2a is new and it is the round's headline.** Q2's
> locked decision (the join) is **retained**, but D-2's claim that *"the read arm needs no wrapper"*
> is **false on TLS**: `Engine::stop()`'s `total` is silently discarded inside asio's SSL composed
> operation, so the join never retires and `stop()` hangs unboundedly on the default accept path —
> strictly worse than the defect being fixed. Q2 is amended to **(b+)**, adding **FR-018** (a
> transport-side OUT-mapping cancellation reset, mirroring the same file's `async_connect` precedent)
> and **SC-018** (a real-TLS witness, because every mock-driven cell is structurally blind to this).
> The superseded paragraphs are marked in place, not deleted. Sections touched: **D-2** (superseded
> block), **D-2a** (new), **D-6.1** (cell T6), **D-6.9**, **D-6.10** (new), **D-7** (frame count,
> (b+)'s zero cost, Article VIII §3 re-derived), **D-8** and **D-9** (count guards).
>
> **Amended again at Gate A round 3 (2026-08-05) — the WITNESS PLAN, not the design.** No locked
> decision and no design decision changes: the join, the epoch mechanism, the internal header and
> FR-018's OUT map all stand, and Article VIII §3's disposition was independently upheld on all three
> legs. What round 3 found is **three more instances of the class rounds 1 and 2 each found once — a
> witness that cannot fail for the reason it names** — arriving by three different mechanisms: the
> construction **did not exist** (B2/B5/B6), the signal **never arrived** (T2a/T6), and the mutant
> **did not change what was asserted** (the `bare deadline arm` column). Sections touched: **D-2** (a
> round-1/round-2 *severity overstatement* corrected), **D-2a.7/.8** (the INV-4a teardown leg
> **audited**, not deferred), **D-5** (T6's missing label row), **D-6.1** (six cell rows), **D-6.2**
> (the S5 **proxy**), **D-6.7**, **D-6.8** (the matrix re-derived — **five** of ten columns had no
> valid RED), **D-6.10** (assertions rewritten), **D-6.10a / D-6.11 / D-6.12 / D-6.13** (all new),
> **D-9** (mechanisms **5 and 6** added; *"No `Script` field is added"* **overturned**), and the
> `pmr_carry_buffer` census category named.

---

## D-1 — The rewritten `read_first_frame_bounded` loop

**Decision.** One prologue statement and one loop body, in this exact order:

```
  0. carry = pmr_carry_buffer{max_bytes + 1, ...}   // DERIVED from the C1 clamp bound — see D-1a
     timer.expires_after(deadline)                  // ARMED ONCE, absolute expiry — see D-1b
loop:
  1. room  = max_bytes + 1 - buf.size()          // C1 clamp; proof below
     want  = min(read_buf.size(), room)
  2. n     = co_await <joined read | deadline>   // D-2
     - deadline arm won  -> transport_handshake_timeout
     - read arm failed   -> propagate its error
  3. buf.insert(read_buf[0..n))
  4. feed_r = framer.feed(read_buf[0..n), carry, out_frames)   // FRAME FIRST
     - feed error        -> propagate (wire_frame_too_large stays distinct — D-1a)
     - frame present     -> co_return out_frames[0].bytes().size()   // FRAME WINS
  5. if (buf.size() > max_bytes)                 // SINGLE budget decision, strict >
       co_return wire_frame_too_large
```

**Rationale.**

- Step 4 before step 5 is FR-002 (Q1). Step 5's strict `>` is FR-001 (Q1).
- Step 5 sits at the **foot** of the body, past the frame-found return — FR-007 requires exactly one
  budget decision point, and this placement is what makes the clamp safe (see the proof).
- The loop-top duplicate at today's `engine.cpp:408-411` is **deleted**, not relocated. It is
  unreachable today — the sole caller default-constructs `frame_buf` at `engine.cpp:857`
  (`std::vector<std::byte> frame_buf;`, reserve-only at `:858`) and passes it at the call,
  `engine.cpp:861-862` — and it would be redundant with step 5 in any case.
- The `while (!timed_out)` condition disappears with the flag itself (D-2): the deadline is no longer
  observed between reads, it is an arm of the join. The loop becomes `for (;;)`, and every exit is a
  `co_return`.

**Clamp proof (FR-013's obligation — `room` never underflows and is never 0).**

- *Entry*: the sole caller passes a default-constructed `buf` (`engine.cpp:857`, passed at
  `:861-862`), so `buf.size() == 0` and `room == max_bytes + 1 ≥ 1`. ✔
- *Inductive step*: control reaches step 1 of iteration `k+1` only by falling off step 5 of iteration
  `k`, which requires `buf.size() <= max_bytes`. Therefore
  `room = max_bytes + 1 - buf.size() >= 1`. ✔ — unsigned subtraction never wraps, and `want >= 1`,
  so no zero-length read and no spin.
- *Tightest case*: `buf.size() == max_bytes` ⇒ `room == 1` ⇒ the read requests exactly one byte. This
  is the case the spec's Edge Cases call out, and it is a pinned test cell (see D-6).
- *Bound*: after step 3 of any iteration, `buf.size() <= max_bytes + 1`, because
  `n <= want <= room = max_bytes + 1 - buf.size()`. **SC-013's `max_bytes + 1` is therefore an
  invariant of the clamp, not an aspiration.**
- *Completion side, `n == 0`* — the induction above bounds `want`, not the **completion**. A read that
  completes with `n == 0` and **no error** would leave `buf` unchanged, feed an empty span, and
  re-enter the loop identically: the same spin the `room == 0` case would cause, reached by a
  different route. **The property that closes this is the clamp itself, not a transport contract:**
  `room >= 1 ⇒ want >= 1`, so **the request is never empty**, and a non-empty stream read cannot
  succeed with zero bytes (`include/fixpp/transport/transport.hpp:101-104`; EOF and error mapping at
  `asio_plain_transport.cpp:224-233` and `asio_tls_transport.cpp:1167-1182`). **A `mock_transport`
  *can* be scripted to produce a zero-byte success, so B5's script MUST NOT** — a cell that scripts
  `n == 0` on a **non-empty** request tests the harness, not the clamp.

  > **Two corrections at Gate A round 4, both of which this clause previously got wrong in the
  > design's favour.**
  >
  > **(a) The "not producible by either production transport" claim was UNQUALIFIED and is FALSE for
  > a zero-length request.** asio treats an all-empty stream read as a **no-op that succeeds**:
  > `asio/detail/impl/socket_ops.ipp:890-895` — *"A request to read 0 bytes on a stream is a no-op"* →
  > `asio::error::clear(ec); return 0;` — reached because `async_receive` passes
  > `buffer_sequence_adapter<...>::all_empty(buffers)` as the operation's no-op flag
  > (`asio/detail/reactive_socket_service_base.hpp:426-428`). So
  > `asio_plain_transport::async_read_some(std::span<std::byte>{})` **does** return a successful
  > zero-byte completion. The TLS path is *unverified* rather than false — `asio/ssl/detail/impl/engine.ipp`
  > carries no zero-length guard — which is no better as a proof. **The qualifier "non-empty request"
  > is now carried everywhere the claim appears**, and the no-spin property is rested on the clamp,
  > which needs no transport contract at all. *(Note this also means the round-3 "correction" of this
  > citation was itself incomplete: it substituted `transport.hpp:101-104`, a sentence true only for
  > non-empty requests, without the qualifier.)* The earlier citation of
  > `asio_tls_transport.cpp:1162-1163` is dropped: that is the **initiation** of
  > `ssl_stream_->async_read_some`; the mapping is at `:1167-1182`.
  >
  > **(b) The parenthetical claiming the deadline bounds this spin was a FALSE SAFETY CLAIM, it sat
  > inside the design's own no-spin proof, and it was wrong in the DIRECTION OF THE FIX.** It read:
  > *"Pre-fix `while (!timed_out)` and the new `for (;;)` are **equally exposed** and each iteration
  > awaits, so the deadline still bounds it at 5 s."* Both halves fail:
  >
  > - *"each iteration awaits, so the deadline still bounds it"* conflates **awaiting** with **being
  >   able to lose the race**. The loop **does** yield — the join is per-iteration, so `operator||`
  >   `co_spawn`s the read arm fresh each time (`asio/experimental/awaitable_operators.hpp:343-347`)
  >   and its completion is posted — but **yielding is not sufficient**: the arms are launched in
  >   index order (`asio/experimental/impl/parallel_group.hpp:376-380`) with the read at index 0, the
  >   per-arm handler writes `completion_order_[completed_++] = I` as its first statement
  >   (`:205-206`), and a zero-length read completes **during its own launch**, so arm 0's
  >   continuation is enqueued before arm 1's wait is even initiated. `order[0] == 0` **on every
  >   iteration** ⇒ the deadline branch is never taken. Derived in full at D-6.11.
  > - *"equally exposed"* is **false, and false in the design's favour.** Pre-fix, the deadline was a
  >   **separate posted handler** setting `timed_out` (`src/session/engine.cpp:394-399`), tested at the
  >   loop top by `while (!timed_out)` — so a yielding pre-fix loop **did** exit at 5 s. The join
  >   replaces that flag with a race the read arm always wins here. **The fix therefore REMOVES a
  >   bound the pre-fix shape had**, on this path. Stating the two shapes as equally exposed hid a
  >   regression rather than describing one.
  >
  > **Both auxiliary defences in this clause are withdrawn — (a)'s transport-contract claim and (b)'s
  > deadline claim — and the no-spin proof rests on the CLAMP ALONE.** That is not a retreat; it is
  > **strictly stronger**. `room >= 1 ⇒ want >= 1` is a property of the delivered code, independent of
  > any transport's zero-length behaviour, of any completion's timing, and of the scheduler. The two
  > withdrawn clauses were both conditional on things the bundle did not control and, as it turned
  > out, had wrong. (The same defect appears one layer down in B5's round-3 construction; see
  > D-6.11.)

**Alternatives rejected.** Keeping the loop-top check and making it `>` too (two decision points that
must be kept in agreement — FR-007 exists to forbid exactly this); clamping to `max_bytes - buf.size()`
(would make the over-budget condition unreachable, so `wire_frame_too_large` could never fire —
a silent removal of FR-003).

---

## D-1a — The carry buffer's capacity is DERIVED from the C1 clamp, not a constant

**This section is a Gate A round-1 re-derivation.** The first draft of D-1 chose the cumulative bound
(`max_bytes + 1`) without reading the collaborator that already enforces a bound one byte below it.
The number was recorded in two places in this bundle and never put side by side. It is set out here in
full so the two can never drift apart again.

**The collaborator.** `engine.cpp:402` constructs the framer's carry buffer at capacity `max_bytes`:

```cpp
fixpp::wire::pmr_carry_buffer carry{max_bytes, std::pmr::new_delete_resource()};
```

and `pmr_carry_buffer::append` (`include/fixpp/wire/framer.hpp:45-51`) is a hard, non-reallocating
capacity check:

```cpp
[[nodiscard]] bool append(std::span<const std::byte> in) noexcept {
    if (buf_.size() + in.size() > cap_) { return false; }
```

`Framer::feed` appends to `carry` **before any parse** and fails the whole feed on overflow
(`src/wire/framer.cpp:194-201`):

```cpp
if (!carry.empty()) {
    if (!carry.append(incoming)) {
        carry.clear(); pending_ = 0;
        return fail<std::span<frame_view>>(core::error::wire_frame_too_large);
    }
    source = carry.bytes();
```

**What that does to the delivered design at capacity `max_bytes`.** Both consequences are confirmed by
source read, not argument (`research/reviews/orchestrator_088-firstframe-budget-timer-lifetime_gate_a_r1_measurements.md`):

1. **The discriminating cell B2 is RED after the fix as well as before it.** Trace B2's own shape
   (1000 B, then 3097 B, Logon ending at byte 3500, cumulative 4097):
   - feed #1 — `carry` empty ⇒ no append, `source = incoming`; parse is `partial`; the trailing
     1000 B are appended to the now-cleared `carry` (`framer.cpp:243-249`), `pending_ = 1000`.
   - feed #2 — `carry` non-empty ⇒ `carry.append(3097)` ⇒ `1000 + 3097 = 4097 > 4096` ⇒
     **`wire_frame_too_large`, returned with no parse attempted.** The Logon at byte 3500 is never
     looked for.
2. **FR-007's single budget decision point is unreachable.** On the no-frame path `carry` accumulates
   monotonically (`framer.cpp:239-241`: `if (using_carry) { pending_ = trailing; return …; }`, offset
   stays 0), so `carry.size() == buf.size() - n` and the append is always `4097 > 4096`. Every
   over-budget rejection would come from the framer, one step before `buf.size() > max_bytes` can
   fire — i.e. FR-007 would replace one dead budget check with another.

**Why it was missed, and why the asymmetry is lethal.** B1 (cumulative exactly 4096) passes at either
capacity: a single 4096-byte delivery leaves `carry` empty and takes the `source = incoming` path with
no append at all, and a fragmented 4096 never exceeds 4096 either. **The easy cell is green and the
discriminating cell is red** — the
`[[feedback_timing_band_witness_range_admits_the_mutant_it_claims_to_kill]]` shape.

**Decision.** The carry capacity becomes **`max_bytes + 1`**, stated as a value *derived from the C1
clamp bound*, never as an independent constant:

```cpp
// Capacity is DERIVED from the C1 clamp bound (FR-013 / SC-013): the loop can buffer at most
// max_bytes + 1 bytes, and Framer::feed appends every fed byte into `carry` before parsing, so a
// capacity below that bound rejects the feed before the frame can be found. Keep the two equal.
fixpp::wire::pmr_carry_buffer carry{max_bytes + 1, std::pmr::new_delete_resource()};
```

**Sufficiency proof, valid for any number of feeds.** Every byte the framer ever holds in `carry` came
from `buf`, and D-1's clamp proves `buf.size() <= max_bytes + 1` at every point. The mechanism that
makes *"`carry` holds only the trailing unconsumed suffix"* true across **more than two** feeds — the
case the round-1 measurement record explicitly left untraced — is the `pending_` bookkeeping at the
head of `feed` (`src/wire/framer.cpp:189-191`):

```cpp
if (pending_ < carry.size()) {
    carry.consume_front(carry.size() - pending_);
}
```

`pending_` is set to `trailing` on every exit path (`:239-241` on the carry path, `:246-249` on the
non-carry path), i.e. to the number of bytes *not yet consumed as a frame*. So each `feed` first drops
everything the previous call consumed, leaving `carry.size() == pending_ <= ` the bytes fed so far.
Therefore `carry.size() <= buf.size() <= max_bytes + 1` **at the start and end of every feed,
inductively, for any feed count** — not just the two-feed shape B2 traces. Capacity `max_bytes + 1` is
**exactly** sufficient and never slack.

**Census — no other consumer of `pmr_carry_buffer` is affected** (the round-1 measurement record left
this open; `rg -n pmr_carry_buffer src include tests bench` settles it rather than asserting it). Every
construction site passes its **own** capacity argument, computed locally; there is no shared constant
to move. In `src/` there are **five** construction sites, of which **four are framing sites**:
`engine.cpp:402` (this one, first-frame read — the only one this feature touches), `engine.cpp:494`
(the read pump's, at `kReadPumpCarryCapacity`), and `session.cpp:318` and `:1962` (both at
`carry_store.size()`); the fifth, `dictionary/reify.cpp:118`, is **not a framing site** — it
constructs at capacity `0` and never feeds a wire framer. *(Ambiguity corrected at Gate A round 3
(C13). The previous wording read "exactly four … **plus** `reify.cpp:118`", which is arithmetically
defensible only if the reader supplies the category the sentence never named — while the sentence's
own scope word is "In `src/`", which `reify.cpp` is in. **The defect was the unnamed category, not
the count**: the four are the framing sites, and that is now said. `reify.cpp:118` is untouched by
this feature either way.)* All remaining hits are `tests/` and `bench/` sites constructing their own,
and the declaration itself.
`engine.cpp:402`'s object is a **coroutine local** of the function this feature rewrites, so changing
its argument is invisible to every one of them.

**Re-trace of B2 at capacity 4097.** feed #2 appends `1000 + 3097 = 4097 <= 4097` ⇒ `source` is the
4097-byte carry ⇒ `parse_frame` returns `complete` at `frame_len == 3500` ⇒ `produced == 1` ⇒ loop
breaks on `produced == out.size()` (`out_frames` has size 1, `engine.cpp:403`) ⇒ `trailing == 597`,
`pending_ = 597`, one frame returned. **B2 is now GREEN post-fix and RED pre-fix.**

**Re-trace of the no-frame path at capacity 4097.** Read 4096 (`want = min(4096, 4097)`), feed:
`carry` empty ⇒ parse partial ⇒ `carry.append(4096) <= 4097` ✔. `buf.size() == 4096`, not `> 4096` ⇒
next iteration, `room == 1`, read 1 byte, `buf.size() == 4097`; feed: `carry.append(1)` ⇒
`4097 <= 4097` ✔, still no frame ⇒ **step 5 fires** with `4097 > 4096`. **F1 is reachable.**

**Blast radius: one coroutine local.** This `carry` is constructed inside
`read_first_frame_bounded` and dies with it. It is *not* the session-lifetime
`SessionConfig::framer_carry_arena` the class comment describes
(`include/fixpp/wire/framer.hpp:22-26`); the read pump's own carry is a separate object at
`engine.cpp:494` with its own capacity constant. No other consumer of `pmr_carry_buffer` is affected.

**What this does to F2 — split, not deleted.** The contract's F2 ("the framer itself rejects an
over-capacity frame") has two sub-causes, and the change kills exactly one:

| Sub-cause | Where | After this change |
|---|---|---|
| **F2a** — `parse_frame` rejects a *declared* BodyLength or computed `frame_len` above `Framer::cfg_.max_frame_bytes` (default 256 KiB, `framer.hpp:21-22`) | `framer.cpp:120-121`, `:129-130`, `:179-180` | **still reachable** — a peer can declare `9=999999` inside the first 4097 bytes |
| **F2b** — `pmr_carry_buffer::append` overflow | `framer.cpp:199-201`, `:246-248` | **unreachable on this path**, by the sufficiency proof above |

So F2 stays a distinct cause and is not merged into F1 — but the *budget-attributable* source of
`wire_frame_too_large` on this path is now F1 alone, which is what FR-007 wanted and what the
capacity collision was silently preventing. This is recorded as Gate A round-1 clarification **G-3**
in `spec.md`.

---

## D-1b — The deadline timer is armed ONCE, before the loop

**Decision, and an invariant `/speckit-tasks` must not violate.** `timer.expires_after(deadline)` is
executed **once**, in the prologue (today: `engine.cpp:386`), and `await_deadline` (D-2) re-awaits
**the same already-armed timer**. `await_deadline` MUST NOT call `expires_after`.

**Why this needs saying.** Pre-fix the timer is armed once and the flag it sets is read by the loop
condition, so there is no per-iteration arming site to get wrong. Under the join the deadline is
re-**awaited** every iteration, and `await_deadline(asio::steady_timer& t)` reads as a per-iteration
call taking a timer by reference — an implementer has an entirely natural place to move
`expires_after` next to the `async_wait`. That **resets the 5000 ms deadline on every completed
read**: a peer that drips one byte every 4 s stays in the pre-session window forever. The byte budget
would still bound it (4097 bytes ⇒ at most 4097 reads), but the *deadline* — the protection FR-014 and
FR-004 name, and which US3 acceptance scenario 2 and SC-004 pin — would be gone, silently, with every
existing test green: the #232 deadline witness (`PostHandshakeStallClosedByFirstFrameDeadline`) sends
nothing, so it never completes a read and never re-arms.

The behaviour the spec requires at `spec.md` Edge Cases ("Deadline expires *between* reads with no
read outstanding — must still return `transport_handshake_timeout`, unchanged") is preserved **only
because** `expires_after` fixes an absolute expiry point and `async_wait` on an already-expired timer
completes immediately. That is the mechanism, and it is now stated rather than assumed.

**Witness.** Cell B6 (D-6): a mock scripted to deliver **one byte per read** past the deadline must
still return `transport_handshake_timeout`. Against a re-arming mutant that cell runs to the byte
budget and returns `wire_frame_too_large` instead. This is D-2's trap's twin, and unlike D-2 it was
named nowhere in the first draft.

---

## D-2 — The joined read/deadline form, and why the timer arm needs a wrapper

**Decision.** Replace the `timer.async_wait(<lambda>)` + `bool timed_out` + `timer.cancel()`-on-every-path
shape with:

```cpp
using namespace asio::experimental::awaitable_operators;

// `timer` was armed ONCE before the loop (D-1b). await_deadline re-awaits it; it never re-arms.
auto outcome = co_await (
      transport.async_read_some(std::span{read_buf.data(), want})
   || await_deadline(timer)                       // NOT timer.async_wait(use_awaitable) — see below
);
if (outcome.index() == 1) co_return std::unexpected(error::transport_handshake_timeout);
auto read_r = std::get<0>(std::move(outcome));
```

**`outcome.index()` is the sole discriminator, and that is only sound because neither arm throws.**
The overload selected is `awaitable<T, Executor> || awaitable<void, Executor>`
(`asio/experimental/awaitable_operators.hpp:337-373`), returning
`std::variant<T, std::monostate>`. Its body branches on `order[0]` **and** on the two
`std::exception_ptr`s:

- `order[0] == 0` (read arm succeeded first) and `!ex0` ⇒ `in_place_index<0>` carrying the read
  result — and `ex1` is **discarded entirely** (`:352-356`).
- `order[0] == 1` (deadline arm first) and `!ex1` ⇒ `in_place_index<1>` (`:364-366`).
- If the *winning* arm carried an exception the overload returns the **other** index (`:357-359`,
  `:367-370`) — i.e. a throw silently re-labels which arm is reported to be the winner.

With D-3's `redirect_error` both `ex0` and `ex1` are always null, so `index()` is exactly "which arm
won" with no aliasing. **Without it, a throw on the read arm surfaces to the caller as
`transport_handshake_timeout`.** That is why D-3's one-line change is load-bearing rather than
cosmetic, and it is the reason `index()` may be relied on at all.

**Why the join closes both legs.** `operator||` expands to
`make_parallel_group(co_spawn(ex, t, deferred), co_spawn(ex, u, deferred)).async_wait(wait_for_one_success(), deferred)`
(`asio/experimental/awaitable_operators.hpp:344-350` for this overload; `:258-264` for the
`void || void` one). `parallel_group::async_wait` completes only
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

> **Severity corrected at Gate A round 3 — this paragraph OVERSTATED the bare arm's consequence, in
> the bundle's own favour.** *"Silently swallow `stop()`'s signal"* is accurate about the **arm**; what
> the surrounding text implied — and what `plan.md`'s D-2 row said outright, *"a regression worse than
> the defect being fixed"* — is not. Derived in D-6.12b: under an external `total` the group's one-shot
> cancel guard (`asio/experimental/impl/parallel_group.hpp:168`, consumed at `:351`) is taken by the
> external handler, so the **read** arm's later completion cannot re-emit (`:222`) and the bare
> deadline arm is never re-cancelled — it runs to **full expiry**. The group then completes, and
> `order[0] == 0` returns the read arm's value. So the bare arm costs `stop()` **the whole deadline
> (5 s in production) — bounded, equal to the pre-fix tail — not an unbounded hang.** The genuinely
> unbounded failure is the **read** arm's, and it is FR-018's (D-2a); conflating the two inflated this
> one. **The wrapper is still required**, on its own honest merits: it is what makes `stop()` *prompt*
> instead of deadline-bounded, and it is what lets `outcome.index()` mean what D-3 needs it to mean.
> The **witness** consequence is the sharper one: because the mutant does not change the returned
> error value, a value-only assertion cannot kill it — hence the promptness thresholds D-6.12b binds
> on T2a and T2b.

So the deadline arm is a two-line coroutine that resets first **and does not throw** (D-3):

```cpp
// Re-awaits the timer armed once by the caller (D-1b). MUST NOT call expires_after().
asio::awaitable<void> await_deadline(asio::steady_timer& t) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    asio::error_code ec;
    co_await t.async_wait(asio::redirect_error(asio::use_awaitable, ec));   // never throws — D-3
    // ec is deliberately ignored: on the losing arm it is operation_aborted, and the join's
    // index() (not this arm's ec) is what decides the outcome.
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

> **SUPERSEDED at Gate A round 2 — this paragraph is half true, and the false half is the one that
> matters.** *"The read arm needs no wrapper"* is correct for the **plain** transport and **false for
> TLS**, which is the default accept path. The paragraph's own citation contains the refutation: it
> notes that `ssl::stream::async_read_some` supports `terminal`/`partial` — i.e. **not `total`** — and
> then concludes that the reset makes the abort real. It does not. The reset installs a **mask**, so
> `total` is forwarded as `total`, and the SSL composed op's own terminal-only cancellation state
> discards it. The read arm therefore **needs a wrapper — in the transport, where the wrapper already
> is, in two-argument form.** Full chain, link by link, in **§D-2a** below; the obligation is
> **FR-018**; the witness is **SC-018** (cell T6). The claim about the plain transport stands
> unchanged and is re-verified in §D-2a.

**Alternatives rejected.** Shared-owned state captured by value (leaves FR-006 open — see
spec §Clarifications Q2); shared state plus a `retired` suppression flag (two mechanisms, and the
flag inherits the same-drain ordering question it was meant to remove).

---

## D-2a — The join does not retire under `stop()` on TLS, and the only place the fix can live is the transport (FR-018)

**Added at Gate A round 2 (2026-08-05). This is the round's headline: the design as specified in D-2
would have shipped a regression strictly worse than the defect it fixes, on the default accept path.**
Every link below was read in the pinned asio (1.38.0, `~/.conan2/p/asio6e6c781a0fee4/p/include/asio/`)
or in this tree. Where the bundle and the source disagreed, the source won.

### D-2a.1 — The chain, link by link

1. **`stop()` emits `total`.** For a started session, via `co_spawn` **onto the session strand**
   (`src/session/engine.cpp:1285-1298` — `entry.session_cancel.emit(cancellation_type::total)`, with a
   direct-emit fallback only when no session strand exists yet); the accept-scope signals are emitted
   at `:1300`.
2. **`parallel_group` forwards that exact type to every arm, once.** The external-cancel handler is
   guarded by the shared `if (state->cancellations_requested_++ == 0)`
   (`asio/experimental/impl/parallel_group.hpp:344-354`) — the same counter the per-arm completion
   path uses (`:218-225`).
3. **The transport wrapper accepts `total` and re-emits `total`, unchanged.** This is the link D-2 got
   wrong. `asio::cancellation_filter` is a **MASK**, not a map — `return type & Mask`
   (`asio/cancellation_state.hpp:31-39`) — and the **one-argument**
   `this_coro::reset_cancellation_state(F)` sets **BOTH** filters to `F`:
   `impl_(… slot.template emplace<impl<Filter, Filter>>(filter, filter) …)`
   (`asio/cancellation_state.hpp:121-126`), installed via `asio/impl/awaitable.hpp:726-732`. The
   state's slot handler is `cancelled_ = in_filter_(in); out = out_filter_(cancelled_); if (out !=
   none) signal_.emit(out);` (`:216-223`). With `enable_total_cancellation` (mask
   `terminal|partial|total`), `total` in ⇒ `total` out.
4. **The forwarded `total` dies inside asio's own SSL composed operation — not at the socket.**
   `ssl::stream::async_read_some` is composed as `ssl::detail::io_op`, which derives from
   `asio::detail::base_from_cancellation_state<Handler>` and delegates to it with the **no-filter**
   overload (`asio/ssl/detail/io.hpp:100-106`). That overload is
   `cancellation_state_(get_associated_cancellation_slot(handler))`
   (`asio/detail/base_from_cancellation_state.hpp:44-48`), and the slot-only `cancellation_state`
   ctor is documented and implemented as **terminal-only** — *"Initialises the cancellation state so
   that it allows terminal cancellation only"* (`asio/cancellation_state.hpp:88-100`, emplacing
   `impl<>` whose defaults are `InFilter = enable_terminal_cancellation`, `:199-201`). So the io_op's
   inner state computes `cancelled_ = total & terminal = none`: nothing recorded, nothing forwarded to
   the `basic_stream_socket` read the SSL engine is pending on. The doc lines D-2 already cites
   (`asio/ssl/stream.hpp:843-849` — `terminal` and `partial` only) are the documented surface of
   exactly this mechanism.
5. **So the join never completes, and `stop()` never returns.** The group handler fires only at
   `--outstanding_ == 0` (`asio/experimental/impl/parallel_group.hpp:229-231`); the read arm never
   retires; `operator||`'s coroutine (`asio/experimental/awaitable_operators.hpp:337-374`) never
   resumes; `read_first_frame_bounded` never returns; and `stop()`'s step-3 join —
   **a 0 ms-timer spin on `outstanding_counter_` with no deadline and no escape**
   (`src/session/engine.cpp:1342-1353`) — spins forever.

### D-2a.2 — Aggravation: unamended, the fix is strictly worse than the defect

`stop()`'s `total` does not merely fail to cancel the read. It also:

- **retires the deadline arm** — a `steady_timer` wait op's cancel handler honours
  `terminal|partial|total` (`asio/detail/deadline_timer_service.hpp:315-320`), so the deadline arm
  completes `operation_aborted`; and
- **consumes the group's one-shot cancel guard** (link 2), so the deadline arm's own completion
  cannot emit a second cancellation to the read arm.

Pre-fix, the stall is **bounded at 5 s**: the raw lambda `timer.async_wait([&timed_out, &transport]…)`
(`src/session/engine.cpp:394-399`) has no associated cancellation slot, is therefore immune to
`stop()`'s signal, fires at expiry and calls `transport.cancel()` → `socket_.cancel(ec)`
(`src/transport/asio_tls_transport.cpp:1247-1255`), which aborts the socket operation the SSL engine
is pending on. **The joined form as specified destroys that escape**, turning a bounded 5 s stall into
an unbounded hang. That contradicts FR-015 and the *"Engine `stop()` during the first-frame read"*
edge case, both of which this bundle already carried.

### D-2a.3 — The repo already said this, and the bundle did not cite it

`src/session/engine.cpp:1302-1310`, on this feature's own file, written for 023's teardown ordering:

> *"An established session's read-pump is blocked in async_read_some with no peer EOF; **total-cancel
> alone does not break the in-flight SSL read** (see BIO_ctrl crash in
> `[[project_business_roundtrip_bio_ctrl_segv]]`). The socket MUST be closed to wake the read-pump."*

That is precisely link 4, stated in production source before 088 was written. D-2 asserted the
opposite about the same code path without opening it. **Independent in-repo corroboration is the
strongest kind, and its absence from the round-1 bundle is the process finding here**, not just the
technical one.

### D-2a.4 — Plain vs TLS: the exposure is asymmetric, and the remedy should be too

The plain transport's read arm does the same one-argument total reset
(`src/transport/asio_plain_transport.cpp:195-196`), but the TCP read op's cancel handler honours the
type explicitly:

```cpp
    void operator()(cancellation_type_t type)
    {
      if (!!(type & (cancellation_type::terminal
            | cancellation_type::partial | cancellation_type::total)))
      { reactor_->cancel_ops_by_key(descriptor_, *reactor_data_, op_type_, this); }
```

(`asio/detail/reactive_socket_service_base.hpp:716-725`.) So on a plain-TCP accept path `stop()`'s
`total` aborts the read arm promptly and the join retires. **The hang is TLS-only — which is the
default accept path.** Mapping on TLS and not on plain is therefore principled rather than
inconsistent: `total`'s stronger *no-observable-side-effects* semantics are genuinely available on a
TCP read and genuinely unavailable mid-TLS-record, which is why asio's SSL ops decline `total` in the
first place. It also matches how the connect path already differs per transport.

### D-2a.5 — An engine-side fix is structurally impossible

`this_coro::reset_cancellation_state` **replaces** the single bottom-frame cancellation state
attached to the awaitable thread's original parent slot (`asio/impl/awaitable.hpp:726-732`):

```cpp
  template <typename Filter>
  void reset_cancellation_state(Filter&& filter)
  {
    bottom_of_stack_.frame_->cancellation_state_ =
      cancellation_state(bottom_of_stack_.frame_->parent_cancellation_slot_, …);
```

There is **one state per awaitable thread, and the last reset wins.** The transport's own
first-statement reset (`src/transport/asio_tls_transport.cpp:1134`) therefore clobbers any OUT-mapping
reset an engine-side arm wrapper installed before `co_await`ing the transport. **Wrapping the read arm
inside `read_first_frame_bounded` cannot work** — not "is less clean", cannot work. The obligation
belongs to `asio_tls_transport::async_read_some`.

### D-2a.6 — Decision: the two-argument OUT map, mirroring this file's own connect path

**Decision.** Replace the one-argument reset at `src/transport/asio_tls_transport.cpp:1134` with the
two-argument form already shipped, tested and commented in the same file at
`src/transport/asio_tls_transport.cpp:918-933` (016 T008, the connect path):

```cpp
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation(), [](asio::cancellation_type ct) {
            return ct == asio::cancellation_type::none ? ct : asio::cancellation_type::terminal;
        });
```

The IN filter still admits `total` (so the transport's own `cancellation_state` reaps still see it);
the **OUT** filter maps it to `terminal` for the forwarded child op. Then: wrapper re-emits `terminal`
⇒ the io_op's terminal-only inner state records and forwards it ⇒ the socket op honours it
(§D-2a.4) ⇒ `operation_aborted` ⇒ `transport_read_cancelled`. Every hop is verified against the same
headers as the failing chain.

**Commenting discipline is part of the decision, not decoration.** The precedent at `:918-933` states
its reason in-source and carries two mnemonics (`:928-929`:
`[[feedback_asio_cospawn_total_cancellation_default]]`,
`[[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]`). FR-018 requires the same:
a reader who deletes the OUT filter as "redundant with `enable_total_cancellation()`" reintroduces an
unbounded `stop()` hang with **every existing test still green** — which is precisely the trap D-2's
own deadline-arm wrapper was written to avoid, one layer down.

**Caller census — closed and minimal, enumerated rather than asserted.** `Transport::async_read_some`
(interface `include/fixpp/transport/transport.hpp:111`) has three overrides — TLS
(`src/transport/asio_tls_transport.hpp:183`), plain (`src/transport/asio_plain_transport.hpp:87`), and
the test-only `mock_transport` (`include/fixpp/transport/test/mock_transport.hpp:164`) — and **exactly
two production call sites in the entire tree**:

1. `src/session/engine.cpp:413` — `read_first_frame_bounded` (the accept path; the site Q2 fixes), and
2. `src/session/engine.cpp:542` — `run_read_pump` (the established-session read pump).

Nothing in `session.cpp`, `reconnect_fsm.cpp`, or anywhere else. Verified two independent ways
(CodeGraph `callers` over the live index, and a by-name sweep across `src/` + `include/`), per
[[feedback_caller_census_by_call_not_syntax]]. The blast radius of changing the TLS read filter is
therefore **the whole of** those two paths and nothing more.

### D-2a.7 — INV-4a disposition: **SAFE BY STRAND CONSTRUCTION**

The second caller (the read pump) is the one that needs an answer, because 023's teardown ordering was
built when `total` could *not* wake a TLS read. Disposition, with evidence:

**What INV-4a is.** A **serialization-domain** invariant, not an ordering dependency. It is stated in
023's bundle at `specs/023-engine-session-strand/data-model.md:96-121` (E-4, "Teardown ordering"),
enacted by that feature's `tasks.md:66` (T014), and carried in `src/session/engine.cpp:1302-1310`:
*"By dispatching close() on the session strand we serialize it with the in-flight read's completion
(BIO fix — INV-4a)."* What it forbids is `close()` and the read completion touching the same SSL
object **concurrently** (the BIO_ctrl segv). Nothing in it requires the read to still be in flight
when `close()` lands — *"wakes the idle in-flight read"* is step 2's **purpose**, not its
precondition.

**Where the (b+)-aborted completion runs — the same strand, by construction.** Step 1's `total` emit
for a started session is itself dispatched onto the session strand
(`src/session/engine.cpp:1285-1298`). The cancellation handler chain runs synchronously inside that
emit, down to the reactor's `cancel_ops_by_key`
(`asio/detail/reactive_socket_service_base.hpp:716-725`), which enqueues the aborted socket completion
to the op's associated executor — the session strand, since the pump's awaitable thread was
`co_spawn`'d on it. The SSL touches (`io_op::operator()`, `map_error_code`) therefore execute **on the
session strand**, exactly as they do today when step 2's `close()` wakes the read. Step 2's `close()`
is dispatched to that same strand. **No off-strand SSL touch exists; the serialization INV-4a names is
preserved by construction.**

**The pump's own contract already documents the (b+) behaviour.** `src/session/engine.cpp:465-471`:

> *"total-cancel (stop()) → async_read_some returns transport_read_cancelled → error arm fires, close
> is a no-op on already-closing session, pump unwinds cleanly."*

and the pump's single error arm (`:545-551`) treats **every** failed read uniformly — `stop_pump()` →
`session.close(terminal)` — with no provenance special-casing to disturb. **This path is live on the
plain transport today and dead on TLS**; (b+) does not create a new pump disposition, it makes the TLS
pump behave like its own comment.

**Lifetime of the earlier-exiting pump.** The new interleaving is that the pump may now exit between
step 1 and step 2. Traced: on role-loop exit, `unpublish_entry` clears `entry.live_transport` **on the
control strand** (`src/session/engine.cpp:634-651`, written for exactly this hazard), and step 2 also
runs on the control strand — so it reads either `nullptr` (skips the close, fine) or a still-published
pointer. If published, the object is alive: the transport is owned by the `Session`, the `Session` by
the registry entry, and entries are destroyed only at step 5's `registry_.clear()`, ordered strictly
after every awaited step-2 close and the step-3 join. `close()` is idempotent
(`src/transport/asio_tls_transport.cpp:1261-1265`). **No UAF window.**

**Step 2 remains load-bearing and must not be removed.** The write path is still `total`-immune —
`async_write`'s one-argument mask (`src/transport/asio_tls_transport.cpp:1195`) plus the SSL write
op's terminal/partial-only support (`asio/ssl/stream.hpp:735-742`) — so a session blocked in a write
is still woken only by the close. (b+) narrows step 2's read-pump role to belt-and-braces; it removes
nothing.

**Disposition: no new pin is required.** The 023 teardown witnesses (its V-1 cell and the TSan matrix)
must be re-run over the amended code, but that is the **standard Gate B sanitizer/teardown pass**, not
a new obligation this feature must invent. Recorded here so a Gate B reviewer meets it as an
adjudicated question rather than an unexamined one.

> **The leg this disposition was over-reaching past is now AUDITED — Gate A round 3 (N4).** Round 2
> asserted *"no new pin required"* while §D-2a.8 simultaneously conceded that the decisive leg was
> **not examined**: whether `Session::close(terminal)` takes a different observable path when entered
> via a step-1 `transport_read_cancelled` (which FR-018 makes reachable on TLS for the first time)
> versus after step 2's `close()` — e.g. a Logout write attempted on a still-open transport. Asserting
> a disposition over an admitted gap is not a disposition. It has now been read:
>
> 1. **The pump enters terminal close, not graceful.** `stop_pump()` is
>    `(void)co_await session.close(fixpp::session::close_mode::terminal)`
>    (`src/session/engine.cpp:511-513`), and it is the *single* error arm for every failed read
>    (`:545-551`).
> 2. **`Session::close()` disables cancellation for its entire body**, as its first statement:
>    `co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});`
>    (`src/session/session.cpp:1396`). Its comment names **this exact scenario** — *"If the CALLER is
>    cancelled mid-close (run_read_pump's `co_await session.close(terminal)` entered just as
>    `Engine::stop()` fires `session_cancel.emit`)"* (`:1385-1395`) — and was written for it at 023's
>    Gate B. The (b+) ordering is the case that code already anticipates.
> 3. **Terminal mode skips phase 1 entirely, so no Logout write is attempted.** The Logout exchange
>    and its grace timer sit inside `if (mode == close_mode::graceful)` (`session.cpp:1438-1486`), and
>    `:1490` states it outright: *"Terminal close: transition directly here (phase 1 skipped, I-9)"*.
>    The specific hazard named — a logout write on a still-open transport — **is not reachable on this
>    path**.
> 4. **Concurrent entry is a no-op mirror.** The three-state model (`session.cpp:1398-1415`) returns
>    `session_already_closed` for never-opened/closed-drained and, for `closing`, awaits and mirrors
>    the first call's result **with no side effects** — so `stop()`'s own step-4 close and the pump's
>    close cannot both act.
>
> **Disposition: AUDITED — no new pin required, and now on evidence rather than over an admitted
> gap.** Residual, stated: phase 2's root-cancel fan-out (`session.cpp:1497-1500` onward) was not
> traced, but it is **common to both entry orderings** and therefore not differential — it is not the
> leg N4 named. The standard Gate B teardown/TSan re-run remains obligatory, as for any change on this
> path.

### D-2a.8 — What this analysis did NOT settle

Stated so the disposition is not read as broader than it is:

- **Static analysis only.** The chain in §D-2a.1 is six verified source links; no program that hangs
  was executed. **SC-018's cell T6, proven RED against the un-mapped build, is the reproduction** —
  that is precisely why FR-010's red-first discipline is restated in SC-018 clause 2.
- The reactor's `cancel_ops_by_key` → scheduler-enqueue step is taken from asio's standard op model,
  not stepped through `epoll_reactor`. **This is also why SC-018's leg-A error assertion is
  ordering-robust rather than exact** — the bundle does not assert what it has not derived
  (D-6.10a).
- ~~Whether `Session::close(terminal)` takes a different observable path when entered via
  `transport_read_cancelled` at step 1 versus after step 2's close … was checked only as far as the
  pump's uniform error arm and `close()`'s idempotence; the session FSM's close internals were not
  audited.~~ **CLOSED at Gate A round 3 — audited; see the disposition block in §D-2a.7.** The
  residual that remains is narrower and non-differential: phase 2's root-cancel fan-out was not
  traced, and it is identical under both entry orderings.

---

## D-3 — The `wait_for_one_success` "winner errored" caveat: reachable on the *deadline* arm, closed by `redirect_error`

**Re-derived at Gate A round 1.** The first draft answered "not reachable, no mitigation" by examining
only the **read** arm. That was correct about the read arm and wrong about the arm this feature
*introduces*.

**Question handed to `/plan`.** `wait_for_one_success`'s disposition overload returns
`cancellation_type::none` when the completing operation carries an error
(`asio/experimental/cancellation_condition.hpp:87-91`); its non-disposition overloads and its
default return `cancellation_type::all` (`:67-68`, `:73-84`). If an arm "errors" — i.e. its
`co_spawn` completion carries a non-null `std::exception_ptr`, which happens only if the arm's
coroutine **throws** — the peer arm is not cancelled.

**The read arm cannot throw. Confirmed, unchanged from the first draft:**

- `asio_{plain,tls}_transport::async_read_some` returns every failure as
  `std::unexpected{core::error::...}` — `transport_already_closed`, `transport_read_in_progress`,
  `transport_read_cancelled`, `transport_read_eof`, `transport_read_truncated`,
  `transport_read_error`. There is no `throw` in either body.
- The underlying asio call uses `asio::redirect_error(asio::use_awaitable, ec)`
  (`asio_plain_transport.cpp:220-221`, `asio_tls_transport.cpp:1162-1163`), which converts the error
  into `ec` instead of throwing.

A transport-level read failure therefore arrives as a **successful** completion carrying an
`expected_t` in the error state. **The normal failure path is unaffected.**

**The deadline arm, as first drafted, threw on EVERY successfully established connection.**
`asio::use_awaitable` throws `std::system_error` on a non-zero `error_code`. When the read arm wins —
which is what happens on every connection that establishes — `wait_for_one_success` cancels the
deadline arm, `t.async_wait` completes with `operation_aborted`, and a bare
`co_await t.async_wait(asio::use_awaitable)` **throws**. So the first draft's *"there is no `throw` in
either body"* was true of the read arm and false of the arm the feature adds, on the common path.

**Consequence, verified rather than assumed.** It is *benign for correctness*: in the
`awaitable<T> || awaitable<void>` overload, `order[0] == 0` with `!ex0` returns `in_place_index<0>`
and **discards `ex1` entirely** (`awaitable_operators.hpp:352-356`). What it is not benign for is
(a) **cost** — a thrown-and-captured `system_error` plus an `exception_ptr` on every accepted
connection, which the Article XI §6 row now prices; and (b) **the claim itself**, which a Gate B
reader would have relied on.

**Decision: close it with `redirect_error` rather than record it.** `await_deadline` (D-2) awaits
`asio::redirect_error(asio::use_awaitable, ec)`. That makes the "no arm throws" premise actually true,
removes the per-connection exception, and — per D-2 — is what makes `outcome.index()` a sound
discriminator.

**Residual, now genuinely narrow.** A `std::bad_alloc` from a coroutine-frame or `parallel_group`
state allocation (`asio/experimental/impl/parallel_group.hpp:371-374`) is still a throw neither
`redirect_error` nor `try/catch`-free code can convert. Its consequences, stated exactly:

- On the **read** arm: `ex0` non-null with `order[0] == 0` ⇒ the overload falls through to
  `!ex1` ⇒ returns `in_place_index<1>` (`:357-359`), so the caller sees
  `transport_handshake_timeout` — **the wrong error, ~5 s late**, though the connection is closed and
  the slot reclaimed either way (`engine.cpp:863-866` takes the same arm for every error).
- On the **deadline** arm: `wait_for_one_success` returns `cancellation_type::none`, so the read arm
  is not cancelled and the group waits out the read — bounded by the peer, not by the deadline.

Bounded, no leak, no UAF, and strictly better than the pre-fix behaviour on the same input (which
strands a handler). **No mitigation is added** — but the residual is now stated with its actual
mis-typing consequence rather than as "bounded at 5 s".

---

## D-4 — Per-site mechanism for the three transport timer sites (FR-014)

The spec explicitly does **not** mandate `||` here (C4/FR-014). `/plan` must state the mechanism per
site and why.

### D-4.0 — Census correction: the dangle leg is **YES** at all three sites

**This overturns the first draft's stated rationale.** It claimed *"There is no lifetime problem to
solve: the handler captures `this`, and the transport outlives it"*, and `spec.md`'s census table
recorded **"Dangle leg — no"** at all three transport rows. Both are **refuted by the owners of the
transports**, which the first draft never opened. Two independent owners destroy the transport
synchronously on the failure arm, with no drain:

| Owner | Failure arm | Destruction |
|---|---|---|
| `src/session/reconnect_fsm.cpp:250-252` | `auto connect_result = co_await t->async_connect(endpoint_); if (!connect_result) { … continue; }` | `t` is a block-scope `std::unique_ptr` (`:247`); `continue` runs its destructor |
| `src/session/reconnect_fsm.cpp:284-286` | same shape after `async_handshake` | same |
| `src/session/engine.cpp:841-844` | `auto hs_r = co_await tls_transport->async_handshake(ssl_cfg); if (!hs_r.has_value()) { transport->close(); continue; }` | `transport` is a `std::unique_ptr` declared in the `while (!engine.stopped())` body at `:810`; `continue` destroys it. **This is the handshake-timeout site (`asio_tls_transport.cpp:1032`) on this feature's own accept path**, and it was missed entirely. |

Neither destructor drains: `src/transport/asio_plain_transport.hpp:71` and
`src/transport/asio_tls_transport.hpp:176` are both `~… override = default;`. The timer is a
**coroutine-frame local** (`asio_plain_transport.cpp:128-135`, `asio_tls_transport.cpp:1030-1037`), so
its destruction cancels a *pending* wait — but the whole premise of this feature is that a `cancel()`
cannot un-queue an **already-completed** one, and that handler holds `this`, not the timer.

**Framing, stated precisely so it is not overstated.** A naive epoch guard written
`if (ec || epoch != timer_epoch_) return;` does **not** introduce a *new* read-from-freed: pre-fix the
same stranded handler already executes `socket_.cancel(ignored)` — a member call on a destroyed
`asio::ip::tcp::socket` — through the identical dangling `this`, and a member epoch would only shrink
the blast radius from a call-on-freed to an 8-byte read-on-freed. The defect is **not a regression
introduced by the guard**. The defect is that the census's "Dangle leg: no" ×3, and the
"the transport outlives it" rationale, were the **sole** stated grounds for choosing an epoch over a
join — and they are source-refuted. A mechanism decision resting on a property the source does not
have has to be re-taken, which is what D-4.1 does.

### D-4.1 — Decision: an attempt epoch held in **shared state the handler owns**, not a member

The epoch mechanism is kept — it is the right shape, and the reasons for preferring it over a join at
these sites (below) survive the census correction intact. What changes is **where the epoch lives**,
so that the guard is safe whether or not `this` is still alive:

```cpp
// src/transport/asio_plain_transport.hpp / asio_tls_transport.hpp (INTERNAL headers)
struct timer_epoch_state {          // strand-confined; plain integers, no atomics
    std::uint64_t connect{0};
    std::uint64_t handshake{0};     // TLS only; unused on the plain transport
};
std::shared_ptr<timer_epoch_state> timer_epochs_{std::make_shared<timer_epoch_state>()};
```

```cpp
// at each arming site
const std::uint64_t epoch = ++timer_epochs_->connect;      // (or ->handshake)
timer.async_wait([this, epochs = timer_epochs_, epoch](asio::error_code ec) {
    // NOTE: nothing here touches `this` until the guard has passed.
    if (ec || epoch != epochs->connect) { return; }        // stale or cancelled -> no-op
    asio::error_code ignored;
    socket_.cancel(ignored);                                // reached only while `this` is alive
});
...
++timer_epochs_->connect;                                   // retire before returning
timer.cancel();
```

```cpp
// destructor body — NOT `= default` any more
asio_plain_transport::~asio_plain_transport() {
    // Retire every armed epoch so a stranded handler can never reach `this`.
    ++timer_epochs_->connect;
}
```

**Why this closes the dangle leg, stated as the argument the reviewer must check:**

1. The lambda captures the `shared_ptr` **by value**, so `*epochs` outlives the transport
   unconditionally.
2. The guard reads only `ec` (by value) and `epochs->connect` (through the shared block). **No
   member of `this` is touched before the guard decides.**
3. The destructor **body** increments every epoch. Members are destroyed *after* the body runs, so
   the retirement is sequenced before `socket_`'s destruction. Therefore: guard passed ⇒ the
   destructor body has not run ⇒ `this` is alive ⇒ `socket_.cancel()` is safe. This is why the
   retirement must be a destructor-body statement and not, say, a member with a retiring destructor.
4. The sequencing in (3) requires handler and destructor to be on the same strand. That holds at all
   three owners named in D-4.0: `reconnect_fsm`'s `t` is destroyed inside the FSM coroutine running on
   `exec` (the transport's own executor, `factory_->make(exec, …)` at `:242`), and the accept loop's
   `transport` is destroyed inside `run_accept_loop`, which runs on the session strand the transport
   was minted on (`engine.cpp:674-676`, and the strand assertion at `:822`). This is the same
   confinement `read_in_flight_` already relies on (`asio_plain_transport.hpp:45-48`;
   `asio_tls_transport.hpp:281-285`). It is stated as a **precondition**, not assumed.

**Cost.** One `make_shared` per transport instance (one control block, two `std::uint64_t`), on the
accept/connect path — priced in D-7 and in the plan's Article XI §6 row. It is not on the hot path.

**Rationale — why not `||` here (unchanged, and it survives).**

1. `src/transport/asio_plain_transport.cpp:137-144` (and the TLS twin at
   `src/transport/asio_tls_transport.cpp:918-933`) installs an **OUT cancellation filter** that maps
   any accepted cancellation to `terminal` for the forwarded child op, deliberately, so
   `Engine::stop()`'s `total` can abort an in-flight `async_connect` that would otherwise ignore it
   (016 T008 — the TLS comment spells the whole rationale out at `:918-929`, including that
   slot-level assignment was tried and is unsafe here). Wrapping that op in a `||` inserts a `co_spawn` frame
   between the filter and the operation, so the filter would have to be re-established inside the
   arm. That is a re-plumb of working, tested, deliberately-shaped cancellation code.
2. The join's advantage is that no handler survives the frame. The **shared-state** epoch buys the
   same end state — no handler can touch dead memory — at the cost of one allocation and no
   cancellation re-plumb. (Against a *member* epoch this argument would fail, which is exactly why
   D-4.0 forced the change.)
3. The epoch integers are strand-confined (these are single-strand transports; `read_in_flight_` is a
   plain `bool` member guarded the same way — `asio_plain_transport.cpp:206`), so they need no atomic
   and add no synchronisation.

**Rationale — why an epoch rather than a plain `bool retired_`.** A `bool` is ambiguous across
successive connect attempts on a reconnecting transport: attempt *N*'s stale handler could observe
attempt *N+1*'s freshly-cleared flag and cancel a socket that is legitimately in use. The monotonic
counter makes "which attempt armed me" explicit, which is the actual question the handler must
answer.

**Sites and their retire points:**

| Site | Operation guarded | Counter | Retire immediately before |
|---|---|---|---|
| `src/transport/asio_plain_transport.cpp:130` | `async_connect` (connect timeout) | `connect` | the existing `timer.cancel()` at `:150` |
| `src/transport/asio_tls_transport.cpp:910` | `async_connect` (connect timeout) | `connect` | the existing `timer.cancel()` at `:941` |
| `src/transport/asio_tls_transport.cpp:1032` | `async_handshake` (handshake timeout) | `handshake` | the existing `timer.cancel()` at `:1045` |
| (both classes) | — | all | the **destructor body**, per D-4.1 item 3 |

**One counter per timer, not one per transport.** The TLS transport's connect and handshake
timers *are* in fact strictly ordered — verified, not assumed: `async_connect` (`:869`) and
`async_handshake` (`:984`) are separate public methods, and both drivers await them sequentially
(`src/session/reconnect_fsm.cpp:250` then `:284`; the accept path calls only `async_handshake`,
`src/session/engine.cpp:842`), while `reconnect_fsm` builds a **fresh transport per attempt**
(`factory_->make(...)` at `:242-247`), so cross-attempt aliasing on one object cannot arise either.
A single shared counter would therefore be correct today.

It is still **two counters** in the TLS state, because that correctness rests on a sequencing
property of two *callers* that nothing in the transport enforces. A future caller that interleaves
them — or a reconnect path that reused a transport — would silently convert a shared counter into a
stale-connect-handler cancelling a live handshake, which is the very defect class this feature exists
to close. Eight bytes buys the removal of an argument rather than the addition of one.

**Surface accounting (corrects a three-way contradiction in the first draft).** The delta is **one
`std::shared_ptr<timer_epoch_state>` member per class** (two members in total, one each) plus **one
user-provided destructor per class**, plus one `[[nodiscard]] std::shared_ptr<const timer_epoch_state>
timer_epochs() const noexcept` accessor per class used by the D-6 T3–T5 cells. All of it is in
`src/transport/*.hpp`, which is **not an installed include root** — that, not the size of the delta,
is what preserves SC-010/SC-017.

**Alternatives rejected.** `||` at all three (item 1 above); a **member** `std::uint64_t timer_epoch_`
(the first draft — refuted by D-4.0: the guard would read through a dangling `this`); `||` at the two
connect sites only (inconsistent mechanism for one defect class, and the handshake site is the one
whose composed `ssl::stream` op is least amenable); leaving them and filing an issue (rejected at
C4/Q3); scoping the dangle leg out with a reachability argument (not available — D-4.0's three call
sites are ordinary production paths, not exotic ones).

---

## D-5 — The internal header (FR-016) and its build wiring

**Decision.** Move the whole definition of `read_first_frame_bounded` into a new
`src/session/read_first_frame_bounded.hpp` as an **`inline` function template-free header**, and have
`src/session/engine.cpp` `#include "session/read_first_frame_bounded.hpp"`.

**Rationale — the precedent is exact for PLACEMENT; it is not exact for WIRING.**
`src/session/scan_first_frame_ids.hpp` was created for precisely this purpose (040 US2 Phase 4), and
`engine.cpp:360-362` records why: *"moved from anonymous namespace to enable direct unit testing."*
Its test target is wired at `tests/session/CMakeLists.txt:712-728` — quoted in full this time, because
the first draft quoted `:722-728` and reproduced only `:722-726`, dropping the `LABELS` lines that
Article VII §8 makes load-bearing:

```cmake
# scan_first_frame_ids and FirstFrameIds are included directly from the internal
# src/session/scan_first_frame_ids.hpp (moved from engine.cpp anonymous namespace
# to enable direct unit testing — 040 US2 Phase 4 T011). The ${CMAKE_SOURCE_DIR}/src
# include path is added so "session/scan_first_frame_ids.hpp" resolves correctly.
# No FIXPP_TEST_HOOKS needed — the function is inline in the internal header.   # <- :720
# Anchors: spec.md FR-003/FR-007/FR-007a; research.md D-3; tasks.md T011/T012.
add_threading_test(session_scan_first_frame_ids_overflow
  scan_first_frame_ids_overflow_test.cpp)                                        # :722-723
target_include_directories(session_scan_first_frame_ids_overflow PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
)                                                                                # :724-726
set_tests_properties(session_scan_first_frame_ids_overflow PROPERTIES
  LABELS "040;us2;scan_first_frame_ids;overflow")                                # :727-728
```

The "No FIXPP_TEST_HOOKS needed" sentence this feature relies on is at **`:720`**, in the comment
block — not inside `:722-728`.

**Where the precedent stops.** `scan_first_frame_ids` is a pure, dependency-free byte scanner; a
header plus an include path really are all its test needs, and it links nothing. **`read_first_frame_bounded`
is not that function.** Its body needs `wire::Framer::feed` (out-of-line in `src/wire/framer.cpp`),
`pmr_carry_buffer`, `transport::Transport`'s vtable, `asio::steady_timer`, and — through the transport
header — the asio/OpenSSL surface. So:

- *"no link against the engine object, because the function is `inline` in the header"* is true only
  of **`engine.cpp`'s object**. The witness target must still link `fixpp` for `wire::Framer` and the
  `Transport` vtable.
- *"keeps the witness binaries small"* does **not** follow, and is withdrawn.

**Delivered wiring (the contract for `/speckit-tasks`):**

```cmake
add_threading_test(session_read_first_frame_bounded read_first_frame_bounded_test.cpp)
target_include_directories(session_read_first_frame_bounded PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(session_read_first_frame_bounded PRIVATE FIXPP_ALLOW_MOCK_TRANSPORT)
set_tests_properties(session_read_first_frame_bounded PROPERTIES
  LABELS "088;us1;us2;first_frame;budget;lifetime")
```

**ctest `LABELS` for every target this feature adds** (Article VII §8 requires label-based selection;
the quickstart's `-R` invocations are convenience only and are marked as such):

| Target | `LABELS` | `TIMEOUT` |
|---|---|---|
| `session_read_first_frame_bounded` (B1–B6, T1, T2a) | `088;us1;us2;first_frame;budget;lifetime` | `120` (the `add_threading_test` default, `tests/session/CMakeLists.txt:92`) |
| `session_first_frame_stop` (T2b, engine-level) | `088;us2;first_frame;stop;live_tls` | **`120` — explicit.** T2b's regression mode is a hang (D-6.12), so it may not inherit a default silently |
| `transport_timer_epoch_retire` (T3–T5) | `088;us4;transport;timer_epoch` | `60` |
| **`session_first_frame_total_cancel_tls` (T6, legs A+B)** — *added at Gate A round 3 (C8); the table omitted it entirely* | **`088;us2;live_tls;first_frame;cancellation`** | **`60` — explicit; second-line defence behind the watchdog** |

**Why T6's omission mattered rather than being a tidiness point.** D-5's table is what makes
`ctest -L 088` a complete selection, and T6 is the **only** cell that pins FR-018 — the round-2
headline. A target absent from this table and left to `/tasks` for its name and directory is a target
`ctest -L 088` is not guaranteed to run. The full CMake contract for T6, including the unconditional
`FIXPP_TLS_FIXTURE_DIR` and the watchdog lifecycle, is in **D-6.13b/c**.

**Why `inline` in the header rather than a declaration + definition in `engine.cpp`.** A declaration
would force every witness TU to link `engine.cpp`'s object, dragging in the whole `Engine` and its
listener/registry machinery — which is a real reduction even though the `fixpp` link remains. The
inline header keeps the *failure modes* local; it does not make the binary small.

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

The RED-proof method is stated once, in **D-6.7**.

**Two levers make the boundary and lifetime cells deterministic, and both are already in the
signature.** `read_first_frame_bounded` takes `deadline` and `max_bytes` as **parameters**
(`contracts/read_first_frame_bounded.md` §Signature), so a direct-helper cell is free to use
`max_bytes = 200` and `deadline = 50 ms` instead of the production 4096/5000. Every cell below states
its numbers as *relative to `max_bytes`*, so the shapes hold at either scale; production-sized numbers
are given where the cell reads more clearly with them.

### D-6.1 — Cells, constructions, and the mutant each one kills

**Thirteen cells.** The first draft's Constitution Check and D-6 title both said "8"; B1–B5 + T1–T5
was already **ten**; the round-1 re-derivation added **B6** (D-1b's arm-once witness) and split **T2**
into T2a/T2b, giving twelve; **Gate A round 2 adds T6** (SC-018 / FR-018 — the real-TLS cancellation
witness, D-6.10). The count is derived from the table below, never remembered.

**Gate A round 3 adds NO cell — it repairs three (B2, B5, B6) and the *delivery* of two more (T2a,
T6).** The count stays at **thirteen**. One clarification so a row-counter does not derive fourteen:
**T6 is one cell with two legs** in one target — leg A (the joined helper: watchdog, promptness,
ordering-robust error class) and leg B (`async_read_some` driven directly, no join: the exact
`transport_read_cancelled`). Leg B exists because of C1 §1.1 — see D-6.10.

| Cell | Target | Construction (how the state is *made*, not hoped for) | Kills |
|---|---|---|---|
| **B1** (SC-001) | direct helper | single delivery, cumulative **exactly** `max_bytes`, complete Logon at its head | `>=` retained |
| **B2** (SC-012) | direct helper | **fragmented via `Script::inbound_chunks` (mechanism 5)**: `{1000 B, 3097 B}`, Logon ends at byte 3500, cumulative 4097 (`max_bytes + 1`). Re-derived at Gate A round 3 — see D-6.11 | budget-before-frame; **and** carry capacity `max_bytes` (D-1a); **and** `>=` retained |
| **B3** (SC-002) | direct helper | B1's delivery plus surplus; assert the returned length is **3500 exactly** | `return buf.size()` |
| **B4** (SC-003) | direct helper | over-budget, **no** complete frame ever (a declared BodyLength that never completes) | over-relaxation of FR-003 |
| **B5** (edge/FR-013) | direct helper | `inbound_chunks = {max_bytes B, 1 B}` (mechanism 5); `deadline = 50 ms`; `ioc.run()`. Assert the **sequence** `read_sizes() == {max_bytes, 1}` (mechanism 6) — the second request is the clamp's `room == 1` — **and** the outcome `wire_frame_too_large`. **MUST NOT script an `n == 0` completion** (D-1's completion clause). The mutant's **termination** depends on the non-zero `read_latency`: without it the loop yields but never lets the deadline arm be recorded first (`order[0] == 0` every iteration) and runs unbounded. Re-derived at round 3, mechanism corrected at round 4 — see D-6.11 | `room == 0`; `max_bytes - buf.size()` clamp |
| **B6** (SC-004 / D-1b) | direct helper | `max_bytes = 200`, `deadline = 50 ms`, `read_latency = 7 ms`, `inbound_chunks` = **201 chunks of 1 byte** (mechanism 5). Reads complete at 7, 14 … 49 ms; the 8th is in flight when the deadline expires at 50 ms with `buf.size() == 7` ≪ 200 ⇒ the deadline must win. Re-derived at Gate A round 3 — see D-6.11 | `expires_after` moved inside the loop / into `await_deadline` (re-arming ⇒ 7 ms < 50 ms per read ⇒ the deadline never fires ⇒ the loop drains all 201 chunks ⇒ `wire_frame_too_large` at byte 201) |
| **T1** (SC-005 / SC-006) | direct helper | **elapse-then-poll**, see D-6.2 | stranded deadline handler (write-to-freed **and** post-return `cancel()`) |
| **T2a** (SC-015 / FR-015) | direct helper | `co_spawn` an **outer wrapper coroutine** whose first statement is `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation())` and which then `co_await`s the helper — the spawn is bound to a test-owned signal via `bind_cancellation_slot`. `read_latency > 0` so the read is genuinely in flight; assert `async_reads_observed() >= 1` (**mechanism 4**); emit `cancellation_type::total`; require the outcome to be **cancellation-attributable** — `transport_read_cancelled` **or** `transport_handshake_timeout`, and not success / `transport_already_closed` / `transport_read_eof` — **and that the call returns within the promptness threshold** (D-6.12). *(**Widened at Gate A round 4.** The round-3 form bound the **exact** `transport_read_cancelled`, which **T2a can fail on correct code**: under the delivered design the `total` reaches both arms, `await_deadline` ignores its `ec` and **completes normally** (D-2), so whenever `order[0] == 1` the join returns `in_place_index<1>` (`asio/experimental/awaitable_operators.hpp:363-366`) ⇒ `transport_handshake_timeout` (D-2's discriminator). That is the very `order[0]` ordering §D-6.10a declares underivable and widened away for **T6 leg A only** — a symmetric construction given an asymmetric fix, i.e. [[feedback_half_restructure_symmetric_api]]. `transport_read_cancelled` remains **expected but non-binding**.)* **The wrapper and the threshold were both added at Gate A round 3** — without the wrapper the signal dies at the outer `co_spawn` (C2, D-6.12); without the threshold the cell does not kill its own mutant (C3/N2, D-6.12) | D-2's trap — a **bare** `timer.async_wait(use_awaitable)` deadline arm. **Corrected signature (Gate A round 3):** the mutant returns `transport_read_cancelled` **at the deadline**, *not* `transport_handshake_timeout`. The discriminator is **latency**, not the error value — see D-6.12 |
| **T2b** (SC-015 accept-slot leg) | engine level | #232's mTLS harness: peer completes the handshake then sends nothing, then `Engine::stop()`; assert the accept slot is reclaimed **and** that `stop()` returns within the promptness threshold (round 3), with a ctest `TIMEOUT` (round 3). **T2b does NOT discharge the non-vacuity clause** — no positive barrier exists at engine scope, and round 3's inverted-hook proposal is **withdrawn** (Gate A round 4, D-6.13a). Its in-flight claim is a **stated bounded inference**, not a proof; SC-015's non-vacuity rests on **T2a and T6** | an arm that outlives `stop()` at engine scope — **bounded by the threshold, not by the error value** (same correction as T2a) |
| **T3–T5** (SC-014, narrowed) | transport internal-header tests, one per site | drive a real successful connect (T3 plain, T4 TLS) / handshake (T5 TLS) against a loopback stub peer; assert `timer_epochs()->connect` (resp. `->handshake`) has **advanced past** the value armed for that attempt when the operation returns | the **retire-point-omitted** mutant (`++` deleted at the retire site) — **and nothing else; see D-6.4 on why the guard-omitted mutant has no killer** |
| **T6** (SC-018 / FR-018) — *added at Gate A round 2; construction repaired at round 3* | **real TLS**, `LoopbackTlsFixture` | see D-6.10 — a real `ssl::stream` blocked on a silent post-handshake peer, cancelled with `total` **delivered through an outer wrapper coroutine that resets to `enable_total_cancellation()` first** (C2 — without it the signal never reaches the join and the watchdog fires *with FR-018 correctly present*), with a positive **initiation barrier** and a watchdog that bounds the RED. **One cell, two legs** — leg A drives the joined helper, leg B drives `async_read_some` directly (D-6.10) | the **un-mapped** mutant: FR-018's OUT filter reverted to the one-argument `reset_cancellation_state(enable_total_cancellation())`. **No mock cell can kill this mutant — see D-6.10.** |

`mock_transport` (lower-case — `include/fixpp/transport/test/mock_transport.hpp:126`; the first draft
called it `MockTransport` throughout, and so did `plan.md`) is `FIXPP_ALLOW_MOCK_TRANSPORT`-gated and
its contract already binds it to honour cancellation rather than short-circuit it (`:21-27`).

### D-6.2 — T1's construction: elapse-then-poll, and why it is deterministic

The first draft said *"deadline `0 ms`, mock transport read completes in the same drain"*. That is not
a construction, and it does not even produce the defect: pre-fix there is **no suspension point**
between `timer.async_wait` (`engine.cpp:394`) and `transport.async_read_some` (`engine.cpp:413`), so
with a 0 ms expiry and an inline read the coroutine reaches `timer.cancel()` before the scheduler ever
runs — the handler then completes with `operation_aborted` and **the RED never fires**. A clean ASan
run would have been recorded as "no finding". That is
`[[feedback_sanitizer_canary_must_be_proven_red]]` one level up.

**The construction that does work uses a hand-driven `io_context` to elapse wall time with no handler
executing.** `mock_transport`'s read composes its own `asio::steady_timer::async_wait(read_latency)`
before completing (`mock_transport.hpp:87-93` and `:115-117`), so *both* pending completions are timers,
and asio's timer queue is a heap ordered by expiry. That, and only that, is what orders them:

1. `deadline = 10 ms`; `Script::read_latency = 1 ms`; script a complete Logon well under `max_bytes`.
2. `asio::co_spawn(ioc, read_first_frame_bounded(mock, buf, 10ms, max_bytes), asio::detached);`
3. `ioc.poll();` — runs the spawn, arms the deadline timer (10 ms) and the mock's latency timer
   (1 ms), suspends. Nothing is expired; `poll()` returns with work outstanding.
4. `std::this_thread::sleep_for(50ms);` — **both timers are now expired and neither handler has run**,
   because the context is not running. This is not a timing margin on a race: it is a one-sided,
   5×-slack margin on *elapsing* two absolute deadlines.
5. `ioc.poll();` — expired timers enter the ready queue **in expiry order**: the 1 ms read completion
   first, the 10 ms deadline second. The read completion runs ⇒ the coroutine resumes, feeds, finds
   the frame, calls `timer.cancel()` — **which cannot un-queue the already-ready deadline handler,
   which is the defect** — and `co_return`s, destroying its frame. The deadline handler then runs and
   writes `timed_out = true` into freed memory and calls `transport.cancel()`.

**Assertions, and why each survives the fix:**

- *Pre-fix (RED)*: under `linux-clang-asan` the run must report a **heap**-use-after-free. The helper
  is driven through `asio::co_spawn(..., asio::detached)`, which **cannot HALO** — see D-6.3 — so the
  frame is genuinely heap-allocated and freed. A `stack-use-after-return` report instead means the
  frame was elided and the proof measured something else.
- *Post-fix (GREEN, and non-vacuous)*: the helper returns the frame's length (so step 5 was reached
  and the read really did win), **and** `mock.cancels_observed() == 0` after the context is drained to
  completion. The second assertion is a **proxy** for contract **S5** — no handler armed by the call
  is outstanding on return — and it fails against any mechanism that lets the deadline handler survive
  the return **and call `cancel()`**. *(Round 2 called it "the direct observable of S5". **Corrected at
  Gate A round 3 (N3):** it observes that no `cancel()` **ran**, which is narrower than S5. It is not
  vacuous — `main`'s stranded handler does call `transport.cancel()` (`engine.cpp:394-399`), so the
  cell genuinely discriminates the pre-fix defect — but a hypothetical stranded handler that did
  something **other** than call `cancel()` would leave the counter at 0 and pass. S5's structural
  guarantee comes from `parallel_group::async_wait` retiring every arm, not from this counter; the
  counter is the best **observable** available at this seam, and is recorded as such rather than as
  proof of the full postcondition.)* `mock_transport::cancel()` is documented as a no-op signal
  (`mock_transport.hpp:246-251`), so this counter is a **new test-only addition** to the mock,
  mirroring the existing `writes_observed_` (`:321`) / `async_writes_observed()` (`:311`) pair — the
  mock has **no** read or cancel counter today; it is priced in D-9.

### D-6.3 — The RED proof is HALO-sensitive, and the bundle must say so

The pre-fix defect writes into `read_first_frame_bounded`'s coroutine frame. **ASan reports it only if
that frame was heap-allocated and freed before the stranded handler ran.** D-5 makes the function an
`inline` header function that the witness TU calls directly; if a cell `co_await`s it from an
enclosing test coroutine, HALO may elide the inner frame into the enclosing one — which is still alive
when the handler fires. The write then lands in live memory and **ASan reports nothing**, and a green
run gets recorded as "no finding" when the correct reading of the pre-fix source is "finding".

Article XI §6's *HALO-first* policy makes this sharper rather than hypothetical: the project wants
elision as policy and this evidence needs no-elision.

**Binding rule for every ASan RED cell in this feature:** drive the helper through
`asio::co_spawn(ioc, helper(...), asio::detached)`, never a direct `co_await` from an enclosing test
coroutine, and require the ASan report to name **heap**-use-after-free. **A clean ASan run on the
pre-fix source is read as "the proof did not fire", never as "there is no defect."**

### D-6.4 — SC-014: what the transport cells can and cannot witness (a C4 residual)

**The same-drain ordering is not constructible at the transport layer, and this must be said rather
than written as prose.** T1's elapse-then-poll trick works because *both* pending completions are
timers, so the timer queue's expiry order decides. At the three transport sites the winning event is a
**socket** completion (`async_connect` / `ssl::stream::async_handshake`) delivered by the reactor. Its
order relative to an expired timer inside one `poll()` is asio scheduler internals — not specified,
and version-sensitive. There is also no injection seam: both timers are coroutine-frame locals of
`async_connect`/`async_handshake`, unreachable from any test, and C3's determinism seam (FR-016) was
granted to the session helper only.

**Disposition, taken back to C4 rather than papered over:**

- **What SC-014 keeps** — one cell per site (C4's "one pin per site, all three" is honoured
  literally), witnessing the property the mechanism actually delivers: *the attempt's epoch has been
  retired by the time the operation returns, so any expiry still in flight is stale.* This is
  deterministic — it asserts **after** the operation returns and needs no ordering control — and it is
  what makes the guard's decision correct at every one of the three sites.
- **What SC-014 gives up** — an integration witness that actually reaches the "expiry queued with
  success, then operation succeeds, then handler runs" interleaving. **No such witness is achievable
  in this tree today.** Recorded as a residual and to be filed as a follow-up issue at close-out
  (title: *"no deterministic same-drain seam for the transport connect/handshake timeout handlers"*).
- **RED basis, and it is not `main`.** The retirement property did not exist pre-fix in a form a cell
  can address, so FR-010's RED obligation for T3–T5 is discharged against **one mutant of the
  delivered design — the retire-point-omitted mutant** (delete the `++` at the retire site; the
  counter then stops at *N+1* instead of *N+2* and the cell fails) — not against `main`. That is the
  same mutation discipline PR #232 used, and it is stated here rather than left to be discovered at
  Gate B.
- **The guard-omitted mutant has no killer, and saying otherwise would repeat this feature's own
  defect class.** Deleting `epoch != epochs->connect` from the lambda changes no counter, so the
  retirement cells still pass. And on every *constructible* run the handler arrives with
  `ec == operation_aborted` (the retire site's `timer.cancel()` has already run), so `if (ec || …)`
  short-circuits and the guard would not have been consulted anyway. The guard's branch fires **only**
  in the same-drain case — the one established above as unconstructible. Claiming a RED for it would
  be a cell that cannot fail for the reason it names, in the artifact that exists to fix exactly that.
  **The guard is therefore discharged structurally**, by D-4.1 items 1–4, alongside the destructor-body
  retirement, and both are inside the scope of the filed residual.
- **The dangle leg** (D-4.0) is discharged **structurally**, by the argument in D-4.1 items 1–4, not
  by a witness: it is a sequencing property of the destructor body, which no test can observe without
  the same unconstructible interleaving. The three T3–T5 cells additionally run under ASan with the
  transport destroyed immediately after a failed connect/handshake, which pins that the *ordinary*
  failure-then-destruction path is clean even if it cannot pin the stale-handler one.

**This narrows SC-016.** SC-016's *"the ordering under test is constructed by the test, not awaited"*
now binds the **session-layer** cells (T1, T2a) — where C3's seam exists and D-6.2 delivers a
construction — and does **not** bind T3–T5, which no longer test an ordering at all. That scoping is
recorded as Gate A round-1 clarification **G-1** in `spec.md`.

### D-6.5 — SC-006 is restated structurally; the live-session leg cannot live in this target

The first draft put T1 in the direct-helper target while SC-006 required *"a session that established
before the expiry remains live — its read pump is not cancelled and it processes a subsequent inbound
frame."* That postcondition **cannot exist there**: there is no `Session`, no
`attach_accepted_transport` (`engine.cpp:922`), and no read pump — and `mock_transport::cancel()` is a
documented no-op (`mock_transport.hpp:246-251`), so even the pre-fix *stimulus* is inert.

SC-006 is therefore restated as the **structural** property the join actually delivers, which is
contract **S5**: *no completion handler armed by this call is outstanding when it returns* — asserted
directly by `mock.cancels_observed() == 0` after a full drain (D-6.2). The live-session teardown stays
in the spec as the **consequence that makes the defect sharp**, i.e. as rationale, not as an asserted
postcondition. The end-to-end leg is inherited: everything downstream of the helper
(`scan_first_frame_ids`, registry resolution, `Session::open`, `attach_accepted_transport`) is
**untouched by 088** and is already covered by #232's six accept-path witnesses. Recorded as Gate A
round-1 clarification **G-2** in `spec.md`.

**The TSan clause is deleted from SC-005.** Both transports document strand confinement
(`asio_plain_transport.hpp:45-48`, `asio_tls_transport.hpp:281-285`) and the accept loop is
single-strand, so the stranded handler and the coroutine are **serialized**. That produces a
sequential use-after-free, not a data race — TSan has no achievable RED here, and asserting one would
be a canary that can never run red. T1 still runs under TSan as part of SC-009's matrix; it is
hygiene, not evidence.

### D-6.6 — B1/B2 assert the helper outcome, not session establishment

SC-001 and SC-012 both said *"a session establishes"* while B1/B2 live in a target that returns a
`std::size_t`. **Both SCs are narrowed to the helper outcome** — *returns the first frame's exact
length rather than `wire_frame_too_large`* — for the reason in D-6.5: the helper's return value is the
**direct** observable of the only code this feature changes, and the downstream establishment path is
untouched and already covered end-to-end by #232. This is a scope mismatch corrected by narrowing the
criterion, not a proxy witness: the cell can still fail for exactly the reason it names.

### D-6.7 — RED-proof method (one mechanism, stated once)

**RED-proof method.** The pre-fix source is `main`'s `read_first_frame_bounded`. Because D-5 makes it
an inline header, the RED run is: `git show main:src/session/engine.cpp` → extract the pre-fix body
(`main` lines 378-455) into a scratch copy of the new header → build the witness target against it →
record the failure output. This is a *source* A/B on one function, not a branch checkout, so it cannot
be contaminated by stale objects (`[[feedback_stale_build_objects_false_green_masks_pins]]`). Each
cell records its own RED output in the verify record.

**The pre-fix body must be adapted to the new position, and only to it**: wrap it in
`namespace fixpp::session::detail`, mark it `inline`, and add the includes `engine.cpp` was supplying
(`<asio/steady_timer.hpp>`, the framer/carry-buffer headers). **Change nothing else** — every `>=`,
the check ordering, the `bool timed_out`, the `timer.async_wait` lambda **and the
`pmr_carry_buffer carry{max_bytes, …}` capacity** stay exactly as `main` has them, or the A/B measures
the wrong thing.

**Not every cell REDs against `main`.** Stated per cell so Gate B does not have to infer it:

| Cell | RED basis |
|---|---|
| B1, B2, B3, B5 | `main` (pre-fix source) |
| **B4** | **none — GREEN on pre-fix source.** A `budget + 1` no-frame payload is rejected identically by `>=` and `>`. B4 is a **regression guard**, labelled as such, not a RED cell |
| B6 | mutant: `expires_after` moved into the loop / into `await_deadline` — **re-derived at round 3 against the chunked mock (D-6.11); under the round-2 construction this cell was RED against the DELIVERED design** |
| T2a | mutant: bare `timer.async_wait(use_awaitable)` deadline arm — **discriminated by LATENCY, not by the error value** (D-6.12b; the round-2 signature was wrong) |
| T2b | mutant: same bare arm at engine scope — **same latency-based discriminator** |
| T1 | `main`, under ASan, per D-6.2/D-6.3 |
| T3–T5 | mutant: **retire-point-omitted only**. The guard-omitted mutant has **no killer** and is discharged structurally — per D-6.4 |
| **T6** *(added at Gate A round 2)* | mutant: **FR-018 reverted** — the one-argument `reset_cancellation_state(enable_total_cancellation())` restored at `src/transport/asio_tls_transport.cpp:1134`. The RED must be **run before the OUT map is added** and its watchdog assertion captured — see D-6.10 |

### D-6.8 — Mutant × witness matrix

One column per mutant, one row per cell. `RED` = the cell fails against that mutant (which is the
cell's job); `—` = the cell passes, i.e. it does not discriminate that mutant. The matrix is derived
**after** the D-1a carry-capacity correction; before it, the `carry@max_bytes` column had no RED cell
at all, which is precisely how the collision survived the first draft.

**A column was added at Gate A round 2 — `TLS OUT map omitted` — and it is the second time this matrix
has exposed an empty column.** Round 1's empty column was `carry@max_bytes`. Round 2's would have been
this one, and it is worse: not a *missing* cell, but a **whole class of cells that cannot fail**,
because every planned cancellation witness drives a mock whose read honours `total` (D-6.10).

> **Re-derived at Gate A round 3, and the round-2 matrix did NOT hold.** Round 3 established that
> **five** of the ten columns had no valid RED, in two independent ways, neither of which the matrix
> showed — because the matrix recorded what each cell was *intended* to kill rather than what its
> stated construction *could* kill:
>
> - **four columns** (`comparison-only`, **`carry@max_bytes`** — round 1's own headline —
>   `room == 0`, `timer re-armed`) because B2/B5/B6's constructions were **not producible** by
>   `mock_transport` at all (N1 / D-9's overturned claim / D-6.11); and
> - **one column** (`bare deadline arm`) because its two claimed REDs both rested on a mutant
>   signature that is **wrong** — the mutant returns `transport_read_cancelled`, not
>   `transport_handshake_timeout` (C3/N2 / D-6.12b).
>
> All five are repaired below and the repairs are re-derived cell by cell in D-6.11 and D-6.12. The
> table now records, per cell, **what its delivered construction kills**, and the empty entries are
> published with their reasons rather than left inferable.

| Cell | `>=` retained | comparison-only (`>` but budget before frame) | `carry@max_bytes` | `room == 0` clamp | `return buf.size()` | bare deadline arm | timer re-armed per iteration | retire-point omitted | guard omitted | TLS OUT map omitted |
|---|---|---|---|---|---|---|---|---|---|---|
| B1 | **RED** | — | — | — | — | — | — | — | — | — |
| **B2** *(re-derived, mech. 5)* | **RED** | **RED** | **RED** | — | — | — | — | — | — | — |
| B3 | **RED** | — | — | — | **RED** | — | — | — | — | — |
| B4 *(regression guard)* | — | — | — | — | — | — | — | — | — | — |
| **B5** *(re-derived, mech. 5+6; +latency)* | **RED** | — | — | **RED** | — | — | — | — | — | — |
| **B6** *(re-derived, mech. 5)* | — | — | — | — | — | — | **RED** | — | — | — |
| T1 | — | — | — | — | — | — | — | — | — | — |
| **T2a** *(+ wrapper, + threshold)* | — | — | — | — | — | **RED** (latency) | — | — | — | **—** (mock; structurally cannot) |
| **T2b** *(+ threshold; NO barrier — r4)* | — | — | — | — | — | **RED** (latency) | — | — | — | **RED\*** (by **hang**) |
| T3–T5 | — | — | — | — | — | — | — | **RED** | — | — |
| **T6** *(+ wrapper, legs A+B)* | — | — | — | — | — | — | — | — | — | **RED** |

\* **T2b's RED in the last column is real but unusable as evidence.** T2b drives #232's real mTLS
harness, so under the un-mapped build it does hit the defect — but the failure *is the hang*: it
produces no assertion, only a ctest `TIMEOUT`. A timeout is indistinguishable from an infrastructure
stall and makes a poor RED record. **T6 is the cell that turns that hang into a bounded, attributable
assertion** via its watchdog (D-6.10), and T6 is what SC-018 cites.

**Every empty cell in the `bare deadline arm` column is now empty for a stated reason, and the two
that are RED are RED for a corrected one.** The mutant does **not** change the returned error value
(D-6.12b): under an external `total` the group's one-shot cancel guard is consumed by the external
handler, the read arm's later completion cannot re-emit, and the bare deadline arm runs to full
expiry — after which `order[0] == 0` returns the *read* arm's `transport_read_cancelled`. So the
column's discriminator is **elapsed time**, and T2a/T2b earn their REDs only because round 3 binds a
promptness threshold on both. Had the threshold been omitted, the honest entry would have been an
empty column with that reason published — which was the alternative considered and rejected as
strictly weaker.

**Reading the matrix.** B2 is the **only** cell that separates the delivered invariant from the
rejected comparison-only fix *and* the only cell that would have caught the carry-capacity collision —
which is why D-1a exists, why B2's *fragmented* shape is a spec-level obligation rather than a
test-review nicety, and why mechanism 5 is priced in D-9 instead of being improvised in the test file.
B1/B3/B5 all die first on pre-fix `>=`, so their claimed RED does not isolate what they name; they
discriminate their own mutants, which is the column that matters. B4 has no RED column by
construction and is labelled a regression guard. T1's column is empty because its discriminator is a
**sanitizer** finding rather than a mutant, and it is stated separately in D-6.2/D-6.3. `guard
omitted` is published empty with its structural discharge (D-6.4). **The last column has exactly one
usable RED**, and that is the honest state: the defect it names is invisible to every mock-driven cell
in the feature, by construction.

**Standing instruction for future rounds, earned four times over.** A column in this matrix is only
as good as the *constructibility* of the cell claiming it — **and the termination of the mutant it
claims to kill.** Before marking a cell RED, derive its construction against the **actual** test
double and the **actual** delivered loop, then derive what the **mutant** does, including whether it
ever gives control back. Rounds 1, 2, 3 and 4 each found a different instance of the same error, and
each time the matrix looked complete beforehand.

> **Round-4 correction to the `room == 0` column — and it is the one round-3 recovery that did NOT
> hold as specified.** Four of round 3's five claimed recoveries stand unchanged
> (`comparison-only`, `carry@max_bytes`, `timer re-armed`, `bare deadline arm`). **`room == 0` did
> not.** As specified at round 3, B5's mutant did not go RED at all — it **looped without bound**.
> The loop *does* yield each iteration (the join is per-iteration, so `operator||` `co_spawn`s a fresh
> read arm every time), but `order[0] == 0` on **every** iteration, so the deadline branch is never
> taken (derivation in D-6.11). The column is recovered **only once B5 binds a non-zero
> `read_latency`**, which is what lets the deadline arm be recorded first as the remaining deadline
> falls below it — under which B5 fails at ~50 ms with `read_sizes()` beginning `{4096, 0, 0, …}` and
> an outcome of `transport_handshake_timeout`. The entry reads RED on that basis, and the basis is
> named in the row.
>
> **The round-4 "B4 also kills `room == 0`" claim is WITHDRAWN and is NOT carried.** It was raised as
> an independent second RED for this column and then withdrawn by its own author, correctly: **B4 is
> `read_latency`-silent for exactly the same reason B5 was** (`research.md`'s B4 row states no
> latency), so as specified B4 **loops without terminating** on that mutant rather than killing it. Giving B4 the
> same non-zero `read_latency` treatment would remove its exposure, but
> **whether B4 then also *kills* the column is not claimed here**, and no matrix row is added for it.
> The column stands on **B5 alone**. *(Sibling hunt, accepted: B4 is the only other cell that can
> reach a zero-length request — B6's 50 ms deadline fires long before `buf.size()` can reach 200;
> B1/B2/B3/T1 never reach `buf.size() == max_bytes` with bytes outstanding; T2a/T2b/T3–T5/T6 cannot
> construct one at all.)*

**The `guard omitted` column has NO RED cell anywhere, and that is stated rather than hidden.** Per
D-6.4, no achievable cell kills it: it changes no counter, and on every constructible run the handler
short-circuits on `ec` before the guard is consulted. It is discharged **structurally** (D-4.1 items
1–4) and carried in the filed residual. A matrix column with no RED cell is exactly the shape this
bundle's own round-1 headline was — an empty `carry@max_bytes` column — so it is published empty here
instead of being filled with a cell that would pass regardless.

**FR-011 guard.** `tests/session/engine_firstframe_test.cpp`'s existing over-budget witness
(`make_carried_over_budget_payload(4097)`, `:261-269` and `:541-542`) must pass **unmodified**. Its
payload declares `9=200000` and is X-padded, so no frame ever completes.

*The first draft's stated mechanism was wrong, and the right answer only holds after D-1a.* At carry
capacity `max_bytes` the close would come from the **framer** at step 4 (`carry.append` overflow at
4097 > 4096), not from step 5 — the witness would still be green, for the wrong reason. At capacity
`max_bytes + 1` the feed succeeds, no frame is produced, and the witness closes at **step 5** on
`4097 > 4096`, which is the mechanism the draft claimed. Both readings satisfy the `< 2000 ms` elapsed
band the witness asserts (all 4097 bytes are already available, so the 1-byte clamped read completes
immediately). If it needs editing, the fix has overreached — that is the check, not a formality.

### D-6.9 — Per-branch coverage enumeration (Article IX §1)

The first draft claimed *"8 witness cells map onto every new or changed line"*. The count was wrong,
and a per-line claim is the wrong instrument. The branches this feature creates, and what covers each:

| New/changed branch | Covered by |
|---|---|
| clamp `room` computation, `want = min(...)` | B5 (`room == 1`), B1/B2 |
| frame-found early return (step 4) | B1, B2, B3 |
| feed-error propagation (step 4) | F2a path — **uncovered**; assessed below |
| budget decision `buf.size() > max_bytes` (step 5) | B2 (must NOT fire), B4 (must fire) |
| join outcome `index() == 1` (deadline won) | B6, T2a's mutant leg |
| join outcome `index() == 0` (read won) | B1, B2, B3, B4, B5, T1 |
| read-arm error propagation | existing 015 coverage + B4's transport-error leg |
| `await_deadline`'s cancellation-filter reset | T2a |
| transport epoch **retire point** (`++` before `timer.cancel()`) | T3–T5 |
| transport epoch guard — **stale** branch (`epoch != current` ⇒ return) | **uncovered** — reachable only in the unconstructible same-drain interleaving, per D-6.4 |
| transport epoch guard — **fresh** branch (guard passes ⇒ cancel) | existing transport connect/handshake-timeout tests (the normal timeout still cancels) |
| transport timer-`ec` branch (`ec` set ⇒ return) | existing transport tests (every successful connect cancels the timer) |
| destructor-body retirement | **uncovered** — structural, per D-6.4 |

**Uncovered branches are assessed, not asserted away. Three remain:**

1. the **F2a** feed-error path (a `parse_frame` rejection of a declared BodyLength above
   `Framer::cfg_.max_frame_bytes`) — pre-existing and untouched by this feature;
2. the transport epoch guard's **stale** branch — genuinely new code, and the branch the feature
   exists to add, but reachable only in the interleaving D-6.4 shows is unconstructible in this tree.
   It must **not** be waived as "defensive": it is a live correctness branch, and its assessment is
   that no seam exists to reach it, with the residual filed;
3. the **destructor-body retirement** — unobservable for the same reason.

All three are dispositioned in `.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md` per
Article IX §1's **binding rule** — per-line assessment is the gate, the percentage a target (settled
at 085 Gate B, tracked as #225).

**Added at Gate A round 2.** FR-018 adds one changed line in `asio_tls_transport::async_read_some` —
the reset's filter argument. Its two branches are: *(a)* **no cancellation** — the OUT filter is never
invoked (`asio/cancellation_state.hpp:216-223`: the filters run only inside the slot handler, i.e.
only when a signal is actually delivered) — covered by every existing test that reads over TLS; and
*(b)* **cancellation delivered** — the OUT filter maps non-`none` → `terminal` — covered by **T6, and
by T6 alone.** **No new uncovered branch is introduced**, which is the honest statement here: the line
is a filter-type substitution, not a new decision point in the read path.

> **And here is why the defect survived this long — worth stating, because it changes how much weight
> T6 carries.** The TLS read-cancellation contract is *named* in the interface —
> `include/fixpp/transport/transport.hpp:105-106`: *"Cancellation: cancellation_type::total →
> transport_read_cancelled"* — and the test written to pin it,
> `tests/transport/test_cancellation_propagation.cpp` (012 T013, *"[2h §9 seam #5] — cancellation
> propagation contract"*), has its read cells **`DISABLED_` and `GTEST_SKIP`ped**: `ReadCancelledStrand`
> (`:81-83`) and `ReadCancelledDirect` (`:86-88`), both *"Pending server+client SslCtxConfig fixture
> pair (post-MVP)"*. So **`total` → `transport_read_cancelled` has never been witnessed on a real
> `ssl::stream` in this tree**, and the contract line has been false since it was written. That is a
> named safety invariant with no live pin — the exact shape of
> [[feedback_named_safety_invariant_needs_direct_pin]].
>
> **The blocker those cells were waiting for no longer exists.** `LoopbackTlsFixture` *is* a
> server+client `SslCtxConfig` fixture pair (`tests/transport/loopback_tls_fixture.hpp:56-127`), added
> later, at 012's gate-b/r2. Re-enabling those two cells is **out of scope for 088** — it is a
> transport-test debt, not this feature's defect — but it is **carried as a follow-up issue to file at
> close-out**, alongside the SC-014 residual. T6 discharges FR-018; it does not discharge the
> interface contract's missing pin in general.

### D-6.10 — T6: the real-TLS cancellation witness, and why no mock cell can replace it

**Added at Gate A round 2 (SC-018 / FR-018).**

**The structural problem first: every other cancellation cell in this feature is blind to this
defect.** T2a, T1 and the B-cells all drive `mock_transport`. Its read composes an
`asio::steady_timer::async_wait` when `Script::read_latency > 0`
(`include/fixpp/transport/test/mock_transport.hpp:177-186`; the class contract says so outright at
`:113-119` — *"emit cancellation_type::total during the wait and the method surfaces the matching
`*_cancelled` variant"*), and a timer wait op's cancel handler **honours `total`**
(`asio/detail/deadline_timer_service.hpp:315-320`). So a mock-driven cell is **green exactly where a
real `ssl::stream` hangs**. This is not a scripting deficiency that a better `Script` would fix — the
mock's read is a timer, the production read is an SSL composed op, and the whole defect lives in the
difference. **Only a real TLS transport can discriminate.** This is the same class as
[[feedback_verification_corpus_built_from_the_read_it_checks_is_blind]]: the instrument shares the
property under test.

**The tree can host it — verified, not assumed.** `tests/transport/loopback_tls_fixture.hpp` builds a
real loopback pair: `asio_listener` bound to `127.0.0.1:0`, `file_cert_source` over the
`leaf_rsa2048` + `ca.pem` mTLS profile, and an `asio_tls_transport_factory` whose `make_client(exec)`
mints a fresh client `Transport` (`:56-127`). The server-side pattern is already in the tree —
`co_await fixture.listener().async_accept()` then `co_await tls->async_handshake(fixture.ssl_cfg())`
(`tests/transport/test_close_truncated_mapping.cpp:127`, `:134`). It is consumed from
`tests/session/` today (`tests/session/engine_loopback_harness.hpp:44`), wired with the
`${CMAKE_SOURCE_DIR}/tests` and `${CMAKE_SOURCE_DIR}/src` include paths plus
`FIXPP_TLS_FIXTURE_DIR` (`tests/session/CMakeLists.txt:839-842`).

**Construction.**

1. Bring up the loopback pair; client connects and completes the handshake; **the client then goes
   silent** — it sends nothing and does not close. The server-side `asio_tls_transport` is now
   post-handshake with no bytes available: the exact production state of a first-frame read against a
   stalled peer.
2. `co_spawn` an **outer wrapper coroutine** on the server side, bound to a **test-owned
   `asio::cancellation_signal`** via `asio::bind_cancellation_slot`. **The wrapper's first statement
   MUST be `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());`**
   — without it `co_spawn`'s terminal-only initial state discards the test's `total` and the cell
   fails with FR-018 correctly present (C2 / D-6.12). The wrapper then sets its `entered_read`
   barrier flag and `co_await`s the subject: `read_first_frame_bounded` for **leg A** (FR-016's
   internal header makes this reachable, so the cell exercises the join as delivered) and
   `transport->async_read_some` for **leg B** (D-6.10a — not an optional variant; it is what carries
   the exact-error assertion without an ordering premise).
3. Arm a **test-owned watchdog** `steady_timer` at a value comfortably below the `deadline` passed to
   the call (e.g. watchdog 1000 ms, deadline 5000 ms) whose handler sets `watchdog_fired = true` and
   closes the server transport. This is what makes the RED **bounded**.
4. Confirm the read is genuinely in flight (the completion flag is still unset), then
   `signal.emit(asio::cancellation_type::total)`.
5. Drive the context until the coroutine completes.

**Assertions, and what each one is for.**

*(Amended at Gate A round 3: A1 is replaced by a **positive** barrier, A3 is split per leg for
ordering-robustness, and A4 is given a **normative** threshold.)*

| # | Assertion | Binding? | Kills |
|---|---|---|---|
| **A1** | **positive initiation barrier** — `entered_read` set by the wrapper after its reset and immediately before the `co_await`, **and** a following `poll()` returning with work outstanding and no completion, i.e. suspended *inside* the read (D-6.13a) | yes | vacuity. **Round 2's form — "the completion flag is still unset" — is withdrawn**: it proves only *not finished*, and is satisfied by a coroutine that never ran, which under C2 is exactly what happened |
| A2 | `watchdog_fired == false` | yes | **the un-mapped mutant.** Under the one-argument reset the read never aborts, the group never retires, and the watchdog is the only thing that ends the test |
| **A3-A** *(leg A, joined)* | the outcome is **cancellation-attributable**: `transport_read_cancelled` **or** `transport_handshake_timeout`; **not** success, **not** `transport_already_closed`, **not** `transport_read_eof` | yes | a wake from the wrong mechanism, **without** asserting an `order[0]`-decided value the bundle disclaims elsewhere (D-6.10a) |
| **A3-B** *(leg B, no join)* | the outcome is **exactly** `transport_read_cancelled` | yes | the same, **exactly** — with one operation and no `parallel_group` there is no ordering premise at all |
| **A4** | elapsed from emit to completion **< 100 ms**, against a `deadline` of 5000 ms and a watchdog of 1000 ms | yes — **normative, not illustrative** | promptness, i.e. FR-015's *"promptly"* on TLS. The 10× watchdog margin and 50× deadline margin make this one-sided, not a timing race |

**Why the watchdog rather than leaning on the ctest timeout.** The regression signature here is a
**hang**, and a ctest timeout is indistinguishable from an infrastructure stall — it is a poor RED
record and a worse CI citizen ([[feedback_ci_hung_test_no_timeout_burns_6h_gdb_capture]]). The
watchdog converts the hang into a deterministic assertion failure in bounded time, so the FR-010
RED-proof produces a *captured assertion message* rather than a killed job. **The ctest `TIMEOUT`
property is still mandatory** as second-line defence (precedent: `tests/session/CMakeLists.txt:1139`,
`tests/transport/CMakeLists.txt:317`).

**RED-proof method (D-6.7's mechanism, one line of it).** The mutant is FR-018 reverted: restore
`co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());` at
`src/transport/asio_tls_transport.cpp:1134`, rebuild the cell, run, and capture A2's failure into the
verify record. **The cell MUST be seen red before the OUT map is added**
([[feedback_sanitizer_canary_must_be_proven_red]]).

**Environment discipline.** `FIXPP_TLS_FIXTURE_DIR` must be wired **unconditionally at configure
time** on the cell's target (precedent `tests/session/CMakeLists.txt:842`), never left to a runtime
env var whose absence turns the cell into a `GTEST_SKIP`. A skipped cell is a false pass, and this one
would be silently skipped in exactly the sandboxed environments where a socket-based test is most
likely to be run blind ([[feedback_codex_sandbox_blocks_sockets_false_pass]]).

### D-6.10a — T6's two legs, and why leg B exists *(added at Gate A round 3 — C1 §1.1)*

**The problem with a single joined leg.** T6 leg A emits `total` while **both** arms are live. The
group forwards it to both (`asio/experimental/impl/parallel_group.hpp:349-353`), so the read arm
aborts (via FR-018) **and** the deadline arm aborts (`asio/detail/deadline_timer_service.hpp:315-320`).
Which of the two is recorded as `order[0]` then decides the returned value entirely
(`asio/experimental/awaitable_operators.hpp:352-371`). **This bundle elsewhere refuses to rely on
exactly that class of ordering** — §D-6.4 and `spec.md`'s C4 amendment both narrow SC-014 on the
grounds that completion order inside one drain is *"unspecified asio scheduler internals"*. Asserting
an exact error value here would hold the feature to a standard it disclaims two sections earlier.

**What is derivable, and what is not.** The emit loop runs `i = 0 … N-1` ascending
(`parallel_group.hpp:352-353`) and arm 0 is the read, so arm 0's abort is *signalled* first and both
aborts are enqueued synchronously by their respective cancel handlers. That makes arm-0-first the
**expected** outcome. It is **not a guarantee**: it would require stepping
`epoll_reactor::cancel_ops_by_key` → `post_deferred_completions` → strand dispatch against the timer
service's own cancel path, which this bundle has not done and does not claim.

**Resolution — split the assertion, do not weaken it.**

- **Leg A (joined helper).** Binding: the watchdog did not fire; the call returned within the
  promptness threshold (D-6.12); and the outcome is **cancellation-attributable** — i.e. one of
  `transport_read_cancelled` or `transport_handshake_timeout`, and **not** a success, **not**
  `transport_already_closed`, **not** `transport_read_eof`. Recorded but **non-binding**:
  `transport_read_cancelled` is expected. This is ordering-robust: under the un-mapped mutant the
  group never completes at all, so *no* outcome in that set is reachable and the watchdog fires.
- **Leg B (raw `async_read_some`, no join).** Same fixture, same wrapper, same silent peer, same
  signal — but awaiting `transport->async_read_some(...)` directly. There is no `parallel_group`, no
  `order[0]`, and exactly one operation to abort, so **`transport_read_cancelled` is assertable
  exactly** with no ordering premise. Leg B is also the tighter pin on FR-018 itself: it removes the
  join from the picture entirely, so a failure attributes to the transport's filter and nothing else.

Leg B is cheap — same target, same fixture, ~15 lines — and it is what lets leg A stay honest about
what it can claim.

### D-6.11 — B2 / B5 / B6 re-derived against the amended mock *(Gate A round 3 — N1)*

Each is re-derived **step by step against the delivered loop** (§D-1), not adjusted and assumed. The
mock semantics are mechanism 5/6's, as specified in D-9.

**B2 — fragmented, cumulative `max_bytes + 1`.** `max_bytes = 4096`,
`inbound_chunks = {1000 B, 3097 B}`, the Logon ending at byte 3500.

| iter | `room = 4097 - buf.size()` | `want = min(4096, room)` | mock returns | `buf.size()` | feed → | budget `> 4096`? |
|---|---|---|---|---|---|---|
| 1 | 4097 | 4096 | **1000** (chunk 0) | 1000 | carry 1000, no frame | no |
| 2 | 3097 | 3097 | **3097** (chunk 1) | 4097 | carry 4097 → **frame at 3500** | — returns 3500 |

Carry needs 4097 and has exactly 4097 (I6) — zero slack, as already stated. Mutants:
- **`carry@max_bytes`**: feed #2 appends 3097 onto 1000 → `4097 > 4096` ⇒ `pmr_carry_buffer::append`
  overflows ⇒ `wire_frame_too_large` **before any parse**. **RED.**
- **`comparison-only`** (strict `>` but budget evaluated before the feed): after iter 2's insert,
  `4097 > 4096` fires before framing ⇒ `wire_frame_too_large`. **RED.**
- **`>=` retained** (pre-fix `main`): iter 1 inserts 1000, `1000 >= 4096` false, feeds, no frame;
  iter 2 inserts 3097 → `4097 >= 4096` ⇒ `wire_frame_too_large`. **RED.**

All three columns recovered. **This is the round-3 repair that matters most**: under the old
single-4096-delivery degradation, B2 still RED'd on `>=` — so it *looked* green-to-red and would have
passed review — while silently discriminating **neither** `comparison-only` (`4096 > 4096` is false,
feed runs, frame found, identical to the delivered design) **nor** `carry@max_bytes` (one 4096-byte
feed leaves `carry` empty and never touches the capacity). That is the same "the easy cell is green
and the discriminating cell is red" trap §D-1a records about B1, one level up.

**B5 — the clamp's `room == 1`.** `max_bytes = 4096`, `inbound_chunks = {4096 B (no complete frame),
1 B}`, **`read_latency = 3 ms` (non-zero, and load-bearing — see below)**, **`deadline = 50 ms`**,
driven with **`ioc.run()`**. *(**3 ms, not 1 ms — chosen at Gate A round 4 to satisfy B6's own rule.**
`1 ∤ 50` is false: a 1 ms series co-expires with a 50 ms deadline **exactly** at the deadline, which
is the configuration B6's rule below in this same section forbids. `3 ∤ 50`, so the two series never share an instant inside the
window. The delivered path is unaffected — 2 reads ≈ 6 ms, far inside 50 ms — and the mutant still
terminates at ~50 ms, in ~17 iterations instead of ~50.)*

| iter | `room` | `want` | `read_sizes()` after | mock returns | `buf.size()` | outcome |
|---|---|---|---|---|---|---|
| 1 | 4097 | 4096 | `{4096}` | 4096 | 4096 | no frame; `4096 > 4096` false ⇒ continue |
| 2 | **1** | **1** | `{4096, 1}` | 1 | 4097 | no frame; `4097 > 4096` ⇒ `wire_frame_too_large` |

**Binding assertions:** `read_sizes()` **begins** `{4096, 1}`, **and** the call returns
`wire_frame_too_large`.

> **Corrected at Gate A round 4 — the round-3 statement of this cell was wrong in three ways, and one
> of them made the cell HANG rather than fail.** The mutant's behaviour was asserted, not derived.
>
> **(i) At zero latency the observable is not `{4096, 0}`.** Under the `room == 0` mutant (clamp
> written `max_bytes - buf.size()`) iter 2 requests `want = 0`; mechanism 5's
> `min(chunk_remaining = 1, buf.size() = 0)` returns **0** — a **successful zero-byte completion**
> consuming nothing. `buf.size()` stays 4096, the strict comparison stays false, and the loop
> **re-enters**. The sequence is `{4096, 0, 0, 0, …}`, not `{4096, 0}`.
>
> **(ii) The claim that the assertion "no longer depends on what the mock does with a zero-length
> request" is FALSE and is struck.** The cell depends on that behaviour *entirely* — it is what makes
> the loop re-enter. What rescues the cell is that `{4096, 0, 0, …} != {4096, 1}`, so the assertion
> still fails; the dependency is real and is now stated rather than denied.
>
> **(iii) At zero latency the loop is UNBOUNDED — B5 would have HUNG — and the mechanism is
> `order[0]` determinism, NOT a missing yield.** This derivation went through **two wrong versions**
> before the right one, and the audit trail is kept because the error is exactly the class this gate
> exists to catch — a claim about asio semantics asserted inside a derivation:
>
> - *The round-4 judge* first reclassified this to P2, reasoning that a non-suspending `co_spawn` arm
>   is forced through a `post` (`asio/impl/co_spawn.hpp:152-157`), so every iteration yields and the
>   deadline fires. **The premise is right; the conclusion does not follow.**
> - *A counter-derivation* then concluded the spin was **synchronous — no yield anywhere** — on the
>   grounds that `co_spawn`'s post is on the completion path and a nested `awaitable<>` runs inline.
>   **That is wrong**, and it was reproduced independently by a second pass, which is worth recording:
>   agreement between two derivations was not evidence.
> - **The error both shared is the arm boundary.** Both reasoned as though arm 0 were *the whole
>   loop*. It is not. **The join is per-iteration** — step 2 of D-1 sits inside `loop:` — so
>   `operator||` `co_spawn`s **one mock read**, fresh, every iteration
>   (`asio/experimental/awaitable_operators.hpp:343-347`). Arm 0 therefore **completes every
>   iteration**, `co_spawn.hpp:152-157` **is** reached, `co_spawn_post` **does** fire, and the
>   `io_context` **does** regain control each time. The observations about the mock's inner body
>   (no suspension at zero latency) and about nested `awaitable<>` frames are individually correct,
>   but they describe what happens *inside* arm 0 — they say nothing about the arm boundary.
>
> **The correct mechanism: yielding is not sufficient, because the deadline arm can never be recorded
> first.**
> 1. `parallel_group_launch` initiates the arms **left-to-right in index order**
>    (`asio/experimental/impl/parallel_group.hpp:376-380`); the **read is index 0**.
> 2. The per-arm handler's **first statement** is
>    `state_->handler_.completion_order_[state_->completed_++] = I;` (`:205-206`).
> 3. With `want == 0` and zero latency, arm 0 completes **during its own launch**, so its continuation
>    is enqueued before arm 1's wait is even initiated. Every completion arm 1 can produce is
>    necessarily enqueued later — a cancellation goes through `scheduler::post_deferred_completions`,
>    and a genuine expiry is collected by the reactor and pushed to the **back** of `op_queue_`.
> 4. FIFO ⇒ `completion_order_[0] = 0` ⇒ `order[0] == 0` ⇒
>    `asio/experimental/awaitable_operators.hpp:352-357` returns **index 0** ⇒ D-2's deadline branch
>    (`index() == 1`) is **never taken**.
>
> **So it is a *yielding* infinite loop, not a synchronous spin** — unbounded either way, and the P1
> stands, but the reason matters because it is what determines the remedy.
>
> **The remedy — bind a non-zero `read_latency` (3 ms) — and now with a stateable reason.** With
> `read_latency > 0` arm 0 no longer completes during launch: it suspends on its own `steady_timer`,
> and both arms are then ordinary entries in **one** expiry-ordered queue — the timer service owns a
> single `timer_queue_` registered once (`asio/detail/deadline_timer_service.hpp:334`, `:79`), so
> every `steady_timer` on the `io_context` shares it, and `get_ready_timers` pops the min-heap root
> repeatedly into a FIFO (`asio/detail/timer_queue.hpp:147-164`). Per iteration, arm 0 is recorded
> first **only while the remaining deadline exceeds the latency**. Once remaining < 3 ms, **arm 1 is
> recorded first**, `order[0] == 1`, D-2's deadline branch is taken, and the call returns
> `transport_handshake_timeout`. **The cell terminates at ~50 ms**, deterministically — this is timer
> *expiry ordering*, not a race — with `read_sizes()` beginning `{4096, 0, 0, …}` ≠ `{4096, 1}` and
> an outcome ≠ `wire_frame_too_large`. **RED on both assertions, in bounded time.**
>
> **Tie-invariance, as belt-and-braces.** `TimeTraits::less_than` is a strict `<`
> (`asio/detail/timer_queue.hpp:251-288`), so two entries with **identical** expiry have unspecified
> heap order — which is why the `3 ∤ 50` choice above matters. **B5's outcome is invariant across a
> tie anyway**, unlike B6's: if arm 1 is recorded first at the tie the deadline branch is taken
> immediately; if arm 0 wins the tie the read returns a zero-byte success and the loop re-enters with
> the deadline timer **already expired**, so arm 1 is necessarily recorded first on the next
> iteration. Either way the outcome is `transport_handshake_timeout` and `read_sizes()` begins
> `{4096, 0, …}` — **both** assertions RED. The timer pair is chosen to avoid the tie; this note
> records that the cell does not depend on that choice. State the
> **deadline (50 ms**, as B6 uses**)** and require **`ioc.run()`** — with a hand-driven `poll()`,
> which is the shape §D-6.2 establishes for T1 and the natural thing to copy across the direct-helper
> cells, `poll()` runs newly-ready handlers within the same call and would not return.
> **No new mechanism is owed**; a seventh D-9 mechanism (a defined mock result for a zero-length
> request) was considered at round 4 and **rejected as unnecessary** once this derivation was
> corrected — the cell reaches a terminating RED without it, and D-9 stays at six.
>
> **A separate finding, made while verifying the above and belonging to none of the three
> derivations.** `mock_transport`'s class documentation states *"**All** async methods compose an
> `asio::post(exec_)` checkpoint (deferred resume)"* (`mock_transport.hpp:114-117`). **That is false
> for three of its four async methods**: the post exists **only** in `async_connect` (`:155`);
> `async_read_some` (`:164-202`), `async_write` (`:204-…`) and `async_handshake` (`:261-…`) have none.
> **This is NOT what makes the loop unbounded** — `co_spawn` supplies a yield at the arm boundary
> regardless; `order[0]` determinism is the cause. It is a **documentation-overstatement finding on
> its own terms**, of the same class this round corrects in the bundle. Not fixed here (a production
> test-header comment, outside this amendment's scope) — **filed as a residual.**

- **`room == 0`** (clamp written `max_bytes - buf.size()`): iter 2's `want` is **0**, the mock returns
  a successful **zero-byte** completion, and the loop re-enters — yielding each iteration but never
  letting the deadline arm be recorded first, until the remaining deadline drops below the 3 ms
  latency. `read_sizes()` then begins `{4096, 0, 0, …}` — not `{4096, 1}` — **and** the outcome is
  `transport_handshake_timeout`, not `wire_frame_too_large`. **RED on both assertions**, in bounded
  time, **only because of the non-zero `read_latency`**.
- The pre-mechanism-6 construction could not distinguish these at all: both ended in
  `transport_read_eof` from the exhausted cursor. **Mechanism 6 is why the cell discriminates; the
  non-zero latency is why it terminates.**
- **B4 is exposed to the same hazard** — it is `read_latency`-silent too and would loop identically
  under this mutant, so it needs the same treatment. Whether B4 *also kills* this column is **not
  claimed here**; see the matrix note at D-6.8.

**B6 — the arm-once deadline.** `max_bytes = 200`, `deadline = 50 ms`, `read_latency = 7 ms`,
`inbound_chunks` = **201 chunks of 1 byte**. Reads complete at 7, 14, 21, 28, 35, 42, 49 ms; the 8th
read's latency wait is in flight when the deadline expires at **50 ms**, with `buf.size() == 7` — far
below the 200-byte budget. The deadline arm wins ⇒ `transport_handshake_timeout`.

- **Why 7 ms and not 5 ms.** At `read_latency = 5 ms` the 10th read completes at **exactly** 50 ms,
  co-expiring with the deadline in the same drain — reintroducing precisely the unspecified ordering
  §D-6.4 refuses to depend on. **The two timer series MUST NOT share a common multiple inside the
  deadline window**; 7 ∤ 50 is the property being bought, and any equivalent choice is fine so long
  as the cell states why.
- **Mutant (`expires_after` moved into the loop or into `await_deadline`)**: the deadline is reset
  every 7 ms and never fires; the loop drains all 201 chunks and reaches `201 > 200` ⇒
  `wire_frame_too_large`. **RED.**
- Under the **old** construction this cell was **RED against the delivered design** — the mock
  returned all 201 bytes in one read, so the correct code returned `wire_frame_too_large` while the
  cell asserted `transport_handshake_timeout`. It would have failed on a correct implementation.

### D-6.12 — Cancellation delivery and the promptness threshold *(Gate A round 3 — C2, C3/N2)*

Two defects in how the cancellation cells are *delivered*, both of which make a cell unable to fail
for the reason it names. They are separate causes with a shared symptom.

**(a) C2 — the outer `co_spawn` swallows `total`, so the signal never arrives at all.**
`co_spawn` builds its initial cancellation state with the **slot-only** ctor —
`cancellation_state cancel_state(proxy_slot);` (`asio/impl/co_spawn.hpp:336`) — which is documented
and implemented as *"allows terminal cancellation only"* (`asio/cancellation_state.hpp:88-100`,
emplacing `impl<>` with `InFilter = enable_terminal_cancellation`, `:199-201`). And
`co_spawn_cancellation_handler::operator()` forwards the type **verbatim**
(`asio/impl/co_spawn.hpp:260-263`). So a test-owned `signal.emit(total)` reaches that state's IN
filter and dies: `total & terminal = none` ⇒ nothing is emitted onward.

`read_first_frame_bounded` does **not** reset its own cancellation state — in production that is
discharged one frame up, by `run_accept_loop` (`src/session/engine.cpp:673-676`), which is exactly
precondition P4. T2a and T6 as specified at round 2 spawned the helper with no such frame.

> **This is the rule the bundle already quotes, from this repo's own production source.** §D-2 cites
> `engine.cpp:673-675` — *"EVERY co_spawned loop MUST reset_cancellation_state(total) as its first
> step or stop()'s total-cancel is swallowed silently (co_spawn defaults to terminal-only)"* — and
> applies it to the **deadline arm** while leaving the **test's own spawn** unprotected. Three rounds
> of this bundle have now been wrong in the same shape: a witness that cannot fail for the reason it
> names. Recorded plainly rather than folded into a fix.

**Requirement.** T2a and T6 MUST `co_spawn` an **outer wrapper coroutine** whose *first statement* is:

```cpp
co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
```

and which then `co_await`s the subject (the helper for T2a and T6 leg A; `async_read_some` for T6
leg B). The spawn is bound to the test-owned signal via `bind_cancellation_slot`. **The wrapper is
not a test artifact — it is what makes the cell faithful**, because it reproduces the frame
production actually has (`engine.cpp:673-676`). Without it, T6's watchdog fires **with FR-018
correctly present**, i.e. the only usable pin for the round-2 headline reports failure on correct
code.

**(b) C3/N2 — the `bare deadline arm` mutant does not change the error value, and both claimed REDs
were false.** The mutant's stated signature in D-6.1 was `transport_handshake_timeout`. It is
**wrong**. Derivation, from headers read in this round:

1. `parallel_group_cancellation_handler` consumes the group's **one-shot** guard —
   `std::atomic<unsigned int> cancellations_requested_` (`parallel_group.hpp:168`, tested at `:351`)
   — and emits the type to **every** arm (`:352-353`).
2. Arm 1 (the bare deadline arm) swallows it: `total & terminal = none`
   (`cancellation_state.hpp:31-39`, `:216-223`).
3. Arm 0 (the mock read) honours it — the mock's latency wait is a `steady_timer`
   (`mock_transport.hpp:177-186`) — and completes `std::unexpected{transport_read_cancelled}`.
4. Arm 0's completion path would normally re-emit, but it is gated on the **same** counter
   (`parallel_group.hpp:222`) **already consumed at step 1**, so it does **not** fire. Arm 1 is never
   re-cancelled and **runs to full expiry**.
5. The group completes at the deadline; `order[0] == 0`, so `operator||` returns the **read** arm's
   value (`awaitable_operators.hpp:352-357`).

⇒ **The mutant returns `transport_read_cancelled`, at the deadline.** A value-only assertion passes
it. T2a and T2b were both marked RED against it and **neither was**.

*(The guard is initialised to `sizeof...(Ops)` and decremented by that amount once every arm has been
launched — `parallel_group.hpp:168`, `:385` — with the external handler registered only afterwards at
`:390-393`. So in steady state it is 0 and the first `++ == 0` wins, which is what makes step 4
hold.)*

> **A round-1/round-2 claim is corrected here, and it was an overstatement in the bundle's favour.**
> §D-2 and `plan.md`'s D-2 row said a bare deadline arm *"silently breaks `Engine::stop()`"* — the
> plan row calling it *"a regression worse than the defect being fixed"*. Per the derivation above it
> does **not** break `stop()`: the group still completes, at the **full deadline** (5 s in
> production). That is a bounded stall equal to the pre-fix tail — bad, and worth the wrapper, but
> **not** unbounded. The genuinely unbounded failure is the **read** arm's (D-2a / FR-018), and
> conflating the two overstated the deadline arm's severity. The wrapper remains justified on its own
> merits: it is what makes `stop()` *prompt* rather than deadline-bounded, alongside D-3's
> `redirect_error` grounds.

> **Corollary settled at Gate A round 4 (N1) — this derivation is why widening T2a's VALUE assertion
> costs it nothing.** T2a's discriminating power was never in the error value. The mutant is killed by
> **latency**, and the `order[0] == 0` step above holds under the *mutant* for an independent reason:
> arm 1 completes at the **full deadline**, strictly later than arm 0. Under the **delivered** design,
> by contrast, both arms are cancelled together and `await_deadline` completes **normally** (it
> ignores its `ec`, D-2), so `order[0]` may be **1** and the join returns `in_place_index<1>`
> (`asio/experimental/awaitable_operators.hpp:363-366`) ⇒ `transport_handshake_timeout`. Binding the
> exact `transport_read_cancelled` therefore risked a **RED on correct code** while adding nothing
> against the mutant. Widened; see the T2a row in D-6.1.

**Requirement — bind a promptness threshold on BOTH T2a and T2b.** The discriminator is **latency,
not the error value**:

- **T2a**: with `deadline = 500 ms`, assert the call returns **within 100 ms** of the `total` emit.
  Under the mutant it returns at ~500 ms. The ratio (5×) is the margin; it is one-sided (the correct
  code returns in ~microseconds) so it is not a timing race — the same argument §D-6.2 makes for
  elapse-then-poll.
- **T2b**: same shape at engine scope — assert `Engine::stop()` returns within a threshold well
  inside `kFirstFrameDeadline` (5000 ms). A bound of **500 ms** gives a 10× margin.
- **The threshold is normative**, not illustrative: SC-015 and SC-018 state it, and quickstart's RED
  table records the mutant's corrected signature so a reader does not chase a wrong error value.

### D-6.13 — Initiation barriers (one real, one absent), and the T6/T2b build contract *(Gate A round 3 — C6, C7, C8; T2b's leg re-settled at round 4)*

**(a) Non-vacuity: an unset completion flag proves only "not finished".** Round 2's T6 said *"confirm
the read is genuinely in flight (the completion flag is still unset)"*. That is satisfied by a
coroutine that never ran at all — which, under C2, is exactly what was happening. A **positive**
barrier is required.

- **T6.** The wrapper coroutine sets an `entered_read` flag **after** its reset and **immediately
  before** `co_await`ing the subject. The test then drives `ioc.poll()` until *(i)* `entered_read` is
  true **and** *(ii)* a subsequent `poll()` returns with work still outstanding and no completion
  recorded — i.e. the coroutine is **suspended inside the read**. Only then is `total` emitted. This
  proves the spawn ran, the reset executed, and the awaitable suspended at the read.
  **Stated limitation, because it is a real one:** it proves suspension at the awaited expression,
  not that `ssl::stream::async_read_some` reached the socket layer. `asio_tls_transport`'s
  `read_in_flight_` is private with no accessor (`src/transport/asio_tls_transport.hpp:286`) and
  **this feature does not add one** — an accessor for a witness is exactly the kind of surface D-9
  prices, and the barrier above is sufficient for the property SC-018 names. Recorded rather than
  papered over.
- **T2b — NO positive barrier exists, and T2b therefore does NOT discharge the non-vacuity clause.**
  *(Settled at Gate A round 4. Round 3 proposed an inverted `test_hook_pre_publish_` seam as T2b's
  primary barrier; **that proposal is withdrawn**, and the fallback round 3 already wrote is promoted
  to the disposition.)*

  Peer silence does **not** prove the accept loop reached `read_first_frame_bounded`: `stop()` can
  land earlier, and every error takes the identical `close(); continue;` arm
  (`src/session/engine.cpp:863-866`), so the cell can pass without ever exercising the join. That
  problem is real and unchanged. **What round 3 offered as its solution does not solve it**, for two
  independent reasons:

  1. **The interval claim was false on its face.** Round 3 asserted that a not-yet-fired hook places
     the loop where *"the only awaitable in that interval is `read_first_frame_bounded`"*. The hook
     fires at `src/session/engine.cpp:931-933`, and between the first-frame read (`:861-862`) and
     that point the loop also runs `scan_first_frame_ids` (`:877`), the registry-identity compare
     (`:888`), **`co_await local_session->open()` (`:907`)** — a second, genuinely suspending
     awaitable — and `attach_accepted_transport` (`:922`). A not-yet-fired hook is consistent with
     the loop sitting at **any** of those.
  2. **An inverted hook is a NEGATIVE barrier, which this bundle has already withdrawn once.** It
     establishes *"not yet past `:931`"* — an absence. That is the same form as the round-2 T6
     observation (*"the completion flag is still unset"*) which §D-6.13a(a) withdrew at this same
     gate, for this same reason: an absence is satisfied by a coroutine that never got there. Fixing
     one instance and shipping another is the recorded half-restructure shape
     ([[feedback_half_restructure_symmetric_api]]).

  **Disposition.** T2b is **recorded as NOT discharging the non-vacuity clause.** It remains a
  valuable cell — it is the only one asserting **accept-slot reclaim** at engine scope, and it holds
  the promptness bound of D-6.12b — but its claim to have *caught a read in flight* rests on a
  **stated, bounded inference** (the client's TLS handshake completed and the peer then sent nothing,
  so the server is somewhere between the handshake and the publish), not on a proof. **SC-015's
  non-vacuity therefore rests on T2a and T6**, which have real barriers. A visible bounded inference
  is worse than a real barrier and strictly better than a false proof, because a reader can weigh it.

  **A real near-side barrier is available and is deliberately NOT bought here.** Instrumenting the
  accepted transport to record `async_read_some` **initiation** would give a genuine positive
  observation — but it is a **seventh D-9 mechanism on the production accept path**, priced and
  argued from scratch, i.e. exactly the class of late structural addition that has cost this bundle
  three rounds. **Filed as a residual against SC-015**, alongside the `guard omitted` residual.
  *(The `FIXPP_TEST_HOOKS` gating of the hook's setter (`engine.cpp:926`) is moot under this
  disposition and is not pursued.)*

**(b) The build contract — T6 and T2b, concretely.** D-5's label table omitted T6 entirely, and the
plan left its file and directory to `/tasks`, so `ctest -L 088` was not guaranteed to include **the
only cell pinning FR-018**. The contract:

```cmake
# T6 — SC-018 / FR-018. Real TLS; MUST NOT be skippable.
add_executable(session_first_frame_total_cancel_tls first_frame_total_cancel_tls_test.cpp)
target_link_libraries(session_first_frame_total_cancel_tls PRIVATE
  fixpp fixpp_transport fixpp_tls GTest::gtest_main)
target_include_directories(session_first_frame_total_cancel_tls PRIVATE
  "${CMAKE_SOURCE_DIR}/src"     # read_first_frame_bounded.hpp + transport/asio_listener.hpp
  "${CMAKE_SOURCE_DIR}/tests")  # transport/loopback_tls_fixture.hpp
target_compile_definitions(session_first_frame_total_cancel_tls PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures")   # unconditional — never a runtime env var
add_test(NAME session_first_frame_total_cancel_tls COMMAND session_first_frame_total_cancel_tls)
set_tests_properties(session_first_frame_total_cancel_tls PROPERTIES
  LABELS "088;us2;live_tls;first_frame;cancellation"
  TIMEOUT 60)
```

`TIMEOUT 60` is second-line defence only — the watchdog is the primary bound (D-6.10), and a run that
ends in the ctest timeout instead of a watchdog assertion means the **watchdog is mis-wired**, which
is a broken cell rather than a RED proof.

**(c) Watchdog lifecycle, specified rather than implied.** The watchdog handler MUST guard on `!ec`
(an `operation_aborted` completion is the normal path and must do nothing); the watchdog MUST be
**cancelled** on the cell's normal completion; and the test MUST drive the context until the aborted
watchdog handler has run, so no handler outlives the fixture. This is the same discipline the feature
imposes on production in FR-005 — a test that strands its own handler while pinning handler lifetime
would be its own counter-example.

---

## D-7 — Allocation discipline (Article VIII §5 and Article XI §6)

**Article VIII §5: not engaged, and the position is not new.** §5 bans `new`/`delete` "between
parse and the `fromApp` callback" — the established-session read pump. `read_first_frame_bounded`
runs **once per accepted connection**, before any `Session` exists, and already allocates on that
path today: `pmr_carry_buffer carry{…, std::pmr::new_delete_resource()}`
(`src/session/engine.cpp:402`). §5's scope does not reach this path.

**Article XI §6 IS engaged, and the first draft had no row for it.** `.specify/constitution.md:236` —
*"Coroutine frame allocation: HALO-first. PMR fallback per-awaiter where HALO doesn't fire."* This is
a MANDATORY rule, and HALO **cannot** fire for the two `co_spawn(ex, …, deferred)` arms `operator||`
creates (`asio/experimental/awaitable_operators.hpp:344-347`): `co_spawn` type-erases the awaitable
into a separately-scheduled operation, so there is no caller frame to elide into. §6's fallback clause
is therefore squarely engaged.

**The full per-connection delta, re-enumerated.** The first draft said *"two coroutine frames"*. The
actual list, with the round-1 corrections folded in:

| # | Allocation | Notes |
|---|---|---|
| 1 | `co_spawn` frame — read arm | un-HALO-able (`asio/impl/co_spawn.hpp:318-342` — the arm is type-erased into an independently launched `awaitable_handler(…).launch()`, so there is no caller frame to elide into) |
| 2 | `co_spawn` frame — `await_deadline` arm | un-HALO-able, same reason |
| **2a** | **`await_deadline`'s own coroutine frame** | **added at Gate A round 2 — the round-1 table under-counted by one.** `await_deadline(timer)` is itself a coroutine; its frame is distinct from the `co_spawn` entry frame that carries it. It is created and then moved into `co_spawn`, so HALO is implausible. **Global heap.** |
| 3 | the `operator||` coroutine's own frame | may HALO into `read_first_frame_bounded`'s frame |
| 4 | `parallel_group` shared state — `std::allocate_shared<state_type>` (`asio/experimental/impl/parallel_group.hpp:371-374`) | per join, per iteration — but allocated through **asio's `recycling_allocator`** (`parallel_group_tag`), i.e. thread-local recycled after warm-up, **not** a raw global-heap `new` in steady state |
| **4a** | small type-erased handler emplaces — `co_spawn_cancellation_handler` ×2 (`asio/impl/co_spawn.hpp:326-328`) and the deadline arm's reset | all via `thread_info_base::allocate(cancellation_signal_tag, …)` (`asio/impl/cancellation_signal.ipp:52-80`) — **thread-local recycled**; added at round 2 for completeness |
| 5 | ~~a thrown-and-captured `system_error` + `exception_ptr` on every established connection~~ | **removed** by D-3's `redirect_error` — it existed in the first draft's design and would have been an every-connection cost |
| 6 | one `make_shared<timer_epoch_state>` per transport instance (D-4.1) | connect/accept path, not per read |
| **7** | **FR-018's OUT map — ZERO** | *added at Gate A round 2.* See "(b+) costs nothing" below. |

**Two corrections to the round-1 enumeration, both made at Gate A round 2.**

1. **The coroutine-frame count is four, not three.** Items 1, 2, **2a** and 3 — the round-1 table
   omitted `await_deadline`'s own frame, counting only its `co_spawn` entry frame.
2. **The steady-state *global-heap* delta is ~3–4 frames per join, not "8+ `new`s".** Items 4 and 4a
   go through asio's thread-local recycling pools, so after warm-up they are not global-heap traffic
   at all. Only the un-HALO-able coroutine frames (1, 2, 2a, and 3 when HALO does not fire) are.
   Stating this matters: the round-1 table, read literally, over-prices the join by roughly a factor
   of two, and an over-priced cost invites a bench that would measure the wrong thing.

**(b+) itself adds ZERO allocations, and this is checkable rather than asserted.** FR-018 replaces the
existing one-argument reset with a two-argument one at the same program point. The one-argument ctor
emplaces `impl<Filter, Filter>` (`asio/cancellation_state.hpp:121-126`); the three-argument one
emplaces `impl<InFilter, OutFilter>` (`:153-161`). `impl<…>` holds its two filters as plain members
(`:199-226`), and **both** filter types here are empty classes — `cancellation_filter<Mask>` (`:30-39`)
and a **captureless** lambda — so the two instantiations are the same size and alignment, and
`slot.emplace<T>`'s `prepare_memory(sizeof(T), alignof(T))` reuses or allocates identically
(`asio/impl/cancellation_signal.ipp:52-80`). **The reset already happens on every read today**
(`src/transport/asio_tls_transport.cpp:1134`); only the handler *type* changes. If the implementation
deviates from this shape — a **capturing** lambda, or a filter with state — this analysis is void and
the Article VIII §3 disposition below must be re-taken. That condition is worth checking in review.

Items 1–4 recur **per loop iteration**, not once per connection — a peer that fragments its Logon
across *k* reads pays *k* joins. The bound is the byte budget: `max_bytes + 1` iterations worst case
(4097), each with a one-byte read. That is a pre-session DoS surface worth stating explicitly, and it
is bounded by the same constant FR-013 bounds the bytes with.

**Disposition for §6.** No per-awaiter PMR fallback is specified: `co_spawn`'s frame allocation uses
the executor's associated allocator, which for `asio::any_io_executor` is
`std::allocator`/`new` — the project has no PMR-backed executor to name, and introducing one for this
path would be a larger change than the defect fix. **This is recorded as a deliberate deviation
against §6's fallback clause**, on the grounds that the path is per-accepted-connection, bounded, and
already allocating; it is entered in the plan's Constitution Check as an explicit XI §6 row rather
than left as an unrowed article.

**Article VIII §3 — re-derived at Gate A round 2 on a corrected basis. Verdict unchanged (not
triggered); the round-1 REASONING is withdrawn.**

> **The round-1 ground was wrong for the amended design, and it must not be reused.** Round 1
> dispositioned §3 as *"none of §2/§4 covers the **pre-session accept path** and no baseline exists
> for it"*. That is still true of the **join** — which lives entirely in
> `read_first_frame_bounded`, once per accepted connection, before any `Session`. It is **not** a
> valid ground for **FR-018**, because FR-018's line lands inside
> `asio_tls_transport::async_read_some` — which **this repo's own source declares hot**:
> `src/transport/asio_tls_transport.cpp:401-403` — *"per [const §VIII.5] the HOT path is
> async_read_some; the handshake is a cold path"* — and which the established-session read pump calls
> once per read (`src/session/engine.cpp:542`). Arguing "the accept path is cold" for a change on the
> session read path is the [[feedback_reachability_built_table_misses_bypassing_surface]] shape:
> a disposition derived for one caller, applied to a site with two.

Re-derived against the article text as written, read rather than paraphrased
(`.specify/constitution.md`):

- **§3 (`:186`)** — *"No perf change merged without a benchmark in the same PR."* Conditioned on the
  change **being** a perf change.
- **§2 (`:185`)** — *"Regression budget: ±5% vs `bench/baselines/` per profile."* The measurement
  basis.
- **§5 (`:191`)** — *"zero `new`/`delete` between parse and `fromApp` callback"*, scoped by
  `[const §XV.1]` (`:296`) to *"the latency-critical **in-memory** path — parse → validate → dispatch
  and `MemoryStore`"*. A transport read is upstream of parse, so **§5 is not engaged by FR-018 either**
  — the transport comment at `:402` uses "hot path" in §5's *spirit* (don't allocate here), and it is
  right to; but §5's literal ban is the parse→`fromApp` window.

**Disposition: §3 is not triggered by FR-018, on zero-delta grounds.** Three legs, each checkable:

1. **No allocation delta.** Same-size, same-alignment emplace at the same program point — the
   "(b+) costs nothing" paragraph above, with the header citations.
2. **No steady-state instruction delta.** The IN and OUT filters are invoked **only inside the
   cancellation state's slot handler** (`asio/cancellation_state.hpp:216-223`), i.e. only when a
   cancellation signal is actually delivered. On a non-cancelled read — every read on any path a
   benchmark would measure — the OUT filter is **never called**. The changed cost appears exactly once
   per teardown, on the path whose current behaviour is an unbounded hang.
3. **Nothing to regress against.** `bench/baselines/` carries `capi/ codegen/ dictionary/ log/
   session/ sync/ threading/ wire/` — **no `transport/` profile at all**, so §2's ±5% has no basis on
   this path.

**A correction to the round-1 wording, because leg 3 was overstated there.** Round 1 said *"no
benchmark covers this path"*. That is too strong: `bench/transport/bench_async_read_some_dispatch.cpp`
exists, is built (`bench/transport/CMakeLists.txt:6-13`), and is named for exactly this path with a
*"≤ 200 ns p99 (transport-side read dispatch)"* target (`:9`). **But it measures nothing** — its
fixture and benchmark bodies are unimplemented scaffolds carrying `TODO (T029)` (`:16-21`, `:44-53`,
`:59-…`), and the file says so itself: *"This bench cannot run meaningfully without the concrete
transport impl."* So the accurate statement is: **a bench target for this path exists but is an empty
scaffold with no baseline**, and there is therefore nothing to A/B. Stated precisely so a Gate B
reviewer who greps `bench/` and finds the file does not read the bundle as having missed it.

**What would change this disposition.** If the implementation uses a **capturing** lambda or a
stateful filter (breaking leg 1), or moves the map anywhere that runs per-byte rather than
per-`async_read_some`-entry, §3 re-engages and a bench is owed. Filling in `T029`'s scaffold to
produce a real read-dispatch baseline is **worth doing and is not this feature's job** — it is a
transport-bench debt of feature 012, carried as a follow-up to file at close-out. If a Gate B reviewer
presses regardless, the cheap discharge is a loopback read-dispatch A/B in the PR body **captured in
the same session as its control** ([[feedback_bench_ab_needs_same_session_control_host_drifts]]) —
optional, not owed.

---

## D-8 — Comment corrections (FR-008), enumerated

**Five** production comment sites state the pre-fix contract and become false with this change (three
in the first draft; the last two were added at Gate A round 1):

| Location | Current claim | Becomes |
|---|---|---|
| `src/session/engine.cpp:373-375` | *"returns when >= 1 complete FIX frame is present … OR when the byte budget is exceeded"* — and the ordering it implies | the delivered order: a complete frame wins over the budget; the budget fires only when no frame is extractable |
| `src/session/engine.cpp:853` | *"4096 bytes max (covers any valid FIX Logon message)"* | true only of a Logon in isolation; must state the coalescing boundary and the `max_bytes + 1` clamp bound |
| `src/session/engine.cpp:389-393` | the Q-2 rationale for `transport.cancel()` from the timer callback | the deadline is now an arm of a join, not a flag-plus-cancel; the Q-2 *requirement* (cancel the in-flight read, do not poll between reads) is **preserved** and must be re-stated, not deleted |
| `src/session/engine.cpp:402` (new) | the carry buffer's capacity argument | must carry the D-1a comment: the capacity is **derived from** the C1 clamp bound and the two must be changed together |
| `src/session/engine.cpp:416-419` (new) | *"The specific code is unobservable here (no Session, no log surface) so it is not special-cased"* | the deadline now returns `transport_handshake_timeout` **directly** from the join's index-1 arm; the comment's reasoning is obsolete and must go with the code it describes |

The Q-2 row matters most: 015's `/simplify` Q-2 rejected between-reads flag polling, and FR-004
carries that forward. The join satisfies Q-2 more strongly than the flag did (the read is aborted by
cancellation, and the arm is joined), but a reader of the new code must be able to see that the old
requirement was honoured rather than forgotten.

**FR-018's comment is an ADDITION, not one of the five corrections — the count of five stands.**
Recorded here so SC-011's *"all five sites in research §D-8"* is not silently turned into six.
`src/transport/asio_tls_transport.cpp:1134` carries no false claim today (its comment is *"Enable
total cancellation (D-17)"*, which is accurate about what the line does); what FR-018 requires is a
**new** comment stating *why the OUT map is there* — that the SSL composed op honours only `terminal`,
so an unmapped `total` is silently dropped and `stop()` hangs — plus the two mnemonics its precedent
carries at `:928-929`. This is an FR-018 obligation, checked by quickstart §6's reviewer list, not an
FR-008 row. The distinction is load-bearing in one direction only: a reader who removes the OUT filter
as redundant reintroduces the hang with **every existing test green except T6**, which is exactly why
the *reason* has to be in the source rather than only in this bundle.

---

## D-9 — New mechanisms this feature adds, priced

**Six**, so each is met as a decision rather than as an unexplained addition. *(Four at Gate A
round 2; **mechanisms 5 and 6 added at Gate A round 3** — see the overturned claim below. Gate A
round 4 considered a seventh — a defined mock result for a zero-length request — and **rejected it as
unnecessary**: B5's mutant terminates on the deadline once B5 binds a non-zero `read_latency`, so no
new mechanism is owed. See D-6.11.)*

| # | Mechanism | Where | Why | Surface |
|---|---|---|---|---|
| 1 | `read_first_frame_bounded.hpp` | `src/session/` (internal) | C3 / FR-016 — the determinism seam T1/T2a need | not installed; SC-017 holds by construction |
| 2 | `timer_epoch_state` + `shared_ptr` member + destructor body + `timer_epochs()` accessor | `src/transport/` (internal) | D-4.1 — closes the dangle leg D-4.0 found; the accessor is what makes T3–T5 assertable | not installed; one `make_shared` per transport |
| 3 | `mock_transport::cancels_observed()` | `include/fixpp/transport/test/` (test-only) | makes contract S5 / SC-006 directly assertable (D-6.2) | test-only header, excluded from production targets per `[const §VII]` |
| 4 | `mock_transport::async_reads_observed()` | `include/fixpp/transport/test/` (test-only) | T2a's **non-vacuity** observable: proves a read was actually initiated before the cancellation signal | as above |
| **5** | **`Script::inbound_chunks`** — `std::vector<std::vector<std::byte>>`; **no completion ever crosses a chunk boundary** (a chunk may take several reads to drain) | `include/fixpp/transport/test/` (test-only) | **B2, B6** — the mock has no per-read chunk control, so a *fragmented* delivery is not producible and B2/B6's constructions do not exist. See below | as above; **additive** — when empty, `inbound_bytes` behaviour is unchanged, so no existing cell moves |
| **6** | **`Script`-independent per-read requested-size observable** — `mock_transport::read_sizes()` (and/or `last_read_requested()`) | `include/fixpp/transport/test/` (test-only) | **B5** — the clamp's `want` is the property under test and there is **no observable for it**; `bytes_read_so_far()` reports the *cursor*, not the request | as above; mirrors the existing `writes_observed_` / `async_writes_observed()` pair (`:311`, `:321`) |
> ### Overturned at Gate A round 3 — *"No `Script` field is added"* was wrong, and it foreclosed the fix
>
> The round-2 sentence below read, flatly, ***"No `Script` field is added"***. It is **withdrawn**, not
> defended. It was written as a *virtue* — a smaller mechanism ledger — and it was in fact a
> **constraint the witness plan could not live inside**, which is a worse thing to be. Three cells
> specified constructions `mock_transport` cannot produce at all:
>
> `mock_transport::async_read_some` drains **one contiguous vector by cursor**
> (`include/fixpp/transport/test/mock_transport.hpp:189-199`):
> `const std::size_t available = script_.inbound_bytes.size() - read_cursor_;`
> `const std::size_t n = std::min(buf.size(), available);`
> The complete `Script` surface (`:67-110`) is `inbound_bytes`, `expected_outbound_writes`,
> `handshake_succeeds`, the identity fields, three `*_latency` values, `partial_write_bytes` and
> `connect_info`; the complete diagnostic surface (`:307-314`) is `bytes_read_so_far()` (**the
> cursor**), `outbound_bytes_seen()`, `async_writes_observed()`, `is_closed()`, `is_handshaken()`. So
> there is **no per-read chunk control, no runtime inbound-append, and no per-read requested-size
> observable.**
>
> | Cell | What it specified | What the mock actually did |
> |---|---|---|
> | **B2** | *"fragmented: 1000 B, then 3097 B"* | iter 1 requests `want = min(4096, room = 4097) = 4096`; the mock returns **4096**. The fragmentation is **not producible** |
> | **B5** | *"observe the next read's **requested length** is 1"* | **no observable exists**; and with `inbound_bytes.size() == max_bytes` the follow-up read hits `read_cursor_ >= size` → `transport_read_eof`, **identically** under the delivered clamp (`want = 1`) and under the `max_bytes - buf.size()` mutant (`want = 0`) |
> | **B6** | *"1 byte per read"* | at `max_bytes = 200`, iter 1 requests `want = min(4096, 201) = 201` and the mock returns all 201 in **one** read ⇒ `buf.size() = 201 > 200` ⇒ `wire_frame_too_large` after one latency tick. The deadline never fires — so **B6 was RED against the delivered design**, asserting `transport_handshake_timeout` on code that correctly returns `wire_frame_too_large` |
>
> **No choice of `max_bytes` rescues this**, which is the first thing a rewriter reaches for. The loop
> always requests `want = min(read_buf.size() = 4096, room)` and the mock always returns
> `min(want, available)`, so the read *sizes* are fully determined by `max_bytes` and
> `inbound_bytes.size()` — the test author has **no control over the split**. If `available >= room`
> the first read fills `buf` to the bound; if `available < room` the second read returns
> `transport_read_eof` (`mock_transport.hpp:190-192`). The only multi-read shape achievable at all is
> the clamp's own `[4096, 1]` tail at `max_bytes = 4096`, which is B4's/FR-011's shape.
>
> **A hand-rolled chunking `Transport` inside the test file is NOT the cheaper route.** It is an
> **unpriced mechanism** — a second, divergent implementation of the transport contract, maintained by
> nobody, exempt from the `FIXPP_ALLOW_MOCK_TRANSPORT` gate and from `mock_transport`'s documented
> cancellation-honouring contract (`mock_transport.hpp:113-119`). Pricing mechanisms in this ledger is
> exactly what D-9 exists to force. **Mechanisms 5 and 6 are the honest cost of the witnesses this
> feature already promised**, and they are additive: `inbound_chunks` empty ⇒ today's behaviour
> verbatim, so no existing cell in any other feature moves.
>
> **Specified semantics for mechanism 5**, so `/tasks` does not have to invent them. *(Restated at
> Gate A round 4 — the round-3 headline "one read completion per chunk" **contradicted** this
> paragraph's own remainder rule, which implies several. The remainder rule is the correct one; the
> headline is corrected. The rule that actually matters is the **boundary** rule, not a count.)*
>
> 1. **Supersession.** `inbound_chunks` supersedes `inbound_bytes` when non-empty; when empty the mock
>    behaves exactly as today (this is what keeps the mechanism additive).
> 2. **No completion crosses a chunk boundary.** Each `async_read_some` consumes from the **current**
>    chunk only and completes with `min(chunk_remaining, buf.size())` bytes. **This is the invariant
>    the mechanism exists for**; the number of completions per chunk is a consequence, not a rule.
> 3. **A non-empty chunk MAY span several reads.** A chunk larger than the request leaves its
>    remainder at the head of that same chunk for the next read; the chunk is retired only when
>    drained. So "one completion per chunk" holds *only* when every request is at least the chunk
>    size — true of B2 and B6 as constructed, and not true in general.
> 4. **Latency applies per read attempt/completion, including remainder reads** — not per chunk.
>    B6's arithmetic depends on this: 201 one-byte chunks at 7 ms each is 201 latency waits.
> 5. **Empty chunks are forbidden.** A `/tasks` implementation MUST either reject them at script
>    construction or skip them without producing a completion. An empty chunk must **never** yield a
>    zero-byte success — that is precisely the non-terminating loop the clamp proof (D-1) and B5
>    (D-6.11) exist to keep out of the harness.
> 6. **Exhaustion.** When every chunk is drained the next read returns `transport_read_eof`, exactly
>    as the cursor path does today.
> 7. **`bytes_read_so_far()` in chunk mode** — *left undefined by round 3; defined here*: it continues to
>    report the **cumulative bytes delivered across all chunks**, i.e. the same "how much has the peer
>    handed over" quantity `read_cursor_` reports today, not a per-chunk index. This keeps the one
>    executable consumer of that accessor — `tests/session/test_session_fsm_via_mock_transport.cpp:66`
>    (`EXPECT_EQ(mt.bytes_read_so_far(), 0u)`) — meaningful and unchanged, and it keeps the accessor
>    distinct from mechanism 6's `read_sizes()`, which reports **requests** rather than deliveries.
>
> **Specified semantics for mechanism 6:** `async_read_some` records `buf.size()` — the **requested**
> length, before any clamping the mock applies — into a `std::vector<std::size_t> read_sizes_`, with a
> `[[nodiscard]] std::vector<std::size_t> read_sizes() const` accessor. The sequence, not just the
> last value, is what B5 asserts. Same strand-confinement note as mechanism 3 applies.

**Mechanism 4 exists because the counter this bundle assumed does not.** The first draft's T2a
non-vacuity clause was written against `reads_observed_`. Verified against the header:
`include/fixpp/transport/test/mock_transport.hpp` has **`writes_observed_`** (`:321`) and
`async_writes_observed()` (`:311`) — and **no read counter at all**. Both new counters mirror that
existing pair exactly (`reads_observed_` incremented in `async_read_some`, `cancels_observed_` in
`cancel()`, each with a `[[nodiscard]] std::size_t …_observed() const noexcept` accessor). This is
the recorded *"planning-layer existence claims are unreliable — verify against the header"* trap; the
claim was checked rather than propagated.

**Concurrency note on mechanism 3.** `mock_transport`'s class doc names `cancel()` as the **only
off-strand path** (`mock_transport.hpp:115-124`, `:246-251`). `cancels_observed_` is therefore
written from a potentially off-strand context, exactly like the strand-confined state around it. It is
a plain `std::size_t`, read **only after the `io_context` has been driven to completion** and never
concurrently with a `cancel()`; T1 is single-threaded by construction (hand-driven `poll()`). This is
stated because the header is shared with other features' tests and this project treats a TSan finding
as real until disproven — if a future consumer calls `cancel()` from a second thread, the counter must
become an atomic at that point.

**Mechanism 2's accessor buys exactly one mutant** (retire-point-omitted, per D-6.4) — not broader
coverage. It remains justified: it is a `const` accessor on an **internal** header, adds no
production branch and no `FIXPP_TEST_HOOKS` (which is what C3 actually objected to), and without it
SC-014 has no assertable observable at all. Stated plainly so it is not read as more than it is.

No production `FIXPP_TEST_HOOKS` branch is added anywhere (C3). ~~No `Script` field is added — the
elapse-then-poll construction (D-6.2) uses the existing `Script::read_latency`
(`mock_transport.hpp:93`).~~ **Struck at Gate A round 3 — see the overturned-claim block above.** One
`Script` field **is** added (mechanism 5) plus one diagnostic (mechanism 6). What survives of the
sentence is narrow and still true: **T1's** elapse-then-poll construction (D-6.2) needs no new field
and continues to use the existing `Script::read_latency` (`mock_transport.hpp:93`).

**FR-018 adds NO further mechanism — checked at Gate A round 2, re-affirmed at round 3.** It changes
the argument list of a `reset_cancellation_state` call that already exists at
`src/transport/asio_tls_transport.cpp:1134`: no new type, no new member, no new header, no new
accessor, no allocation (research §D-7). Its **witness** T6 does add a new test target, but it
introduces no new test-only production surface either — it composes `LoopbackTlsFixture`
(`tests/transport/loopback_tls_fixture.hpp`, pre-existing) with FR-016's internal header (mechanism 1
above). Stated because "the fix is one line" is exactly the claim a reviewer should be able to check
against a mechanism ledger rather than take on trust.

**On the taxonomy objection (round-3 C10), recorded rather than argued away.** A reviewer read
*"FR-018 adds no mechanism"* against the contract's own *"a second, independent transport-side delta …
different mechanism"* (`contracts/read_first_frame_bounded.md`) and called it a contradiction. Both
sentences are kept, and the criterion is now stated instead of implied: **this ledger prices
*artifacts* — new types, members, headers, accessors, allocations, test-only surface — because its
purpose is to stop unpriced surface entering the tree.** FR-018 adds none of those; a
cancellation-type transformation at an existing call site is a genuine *change of mechanism* in the
design sense, and it is argued at length as one in §D-2a.6. The two words are not in conflict once
the ledger's unit is named. **Note the contrast with mechanisms 5 and 6**, which are artifacts by this
same criterion — new `Script` field, new member, new accessor — and are therefore ledgered, which is
what makes the criterion a real filter rather than a convenience.
