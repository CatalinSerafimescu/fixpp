# Feature Specification: Bounded first-frame read — budget boundary + deadline-timer handler lifetime

**Feature Branch**: `088-firstframe-budget-timer-lifetime`

**Created**: 2026-08-04

**Status**: Draft

**Input**: User description: "Fix two production defects in `read_first_frame_bounded` (`src/session/engine.cpp`), tracked as GitHub issue #233 — the byte budget rejects at equality and rejects before framing; and the deadline timer handler can outlive the coroutine frame it captures."

**Tracking issue**: [#233](https://github.com/CatalinSerafimescu/fixpp/issues/233)

---

## Context — two production defects on the acceptor pre-session path

Both defects live in `read_first_frame_bounded` (`src/session/engine.cpp:378-455`), the bounded
first-frame read that FR-014 of feature 015 mandates. **Neither is a regression**: both predate
issue #228 and PR #232. They were surfaced by PR #232's Gate B review and *deliberately left
unfixed there* — #232 was a tests-only remediation carrying a Gate A waiver, and Article XVII §6's
auto-waive covers comment-only edits, which neither of these is. Fixing either in that PR would
have voided its waiver.

This feature does **not** introduce new requirements. It corrects the implementation so it matches
the requirements 015 already shipped:

| Governing requirement | Location | Says |
|---|---|---|
| **FR-014** | `specs/015-runtime-engine/spec.md:126` | a peer that **"exceeds"** the byte budget or the deadline MUST be closed and its accept slot reclaimed |
| **SC-011** | `specs/015-runtime-engine/spec.md:150` | closes a peer that "sends **more than** the first-frame byte budget before a valid Logon" |

### Defect 1 — the budget rejects at equality, and it rejects *before* framing

`src/session/engine.cpp:408-411` (loop-top) and `:426-429` (post-insert) both read:

```cpp
if (buf.size() >= max_bytes) {
    timer.cancel();
    co_return std::unexpected(error::wire_frame_too_large);
}
```

Two distinct wrongs compose here:

1. **`>=` closes at `==`.** FR-014 says *exceeds*; SC-011 says *more than*. With
   `kFirstFrameMaxBytes = 4096` (`:854`) and a 4096-byte `read_buf` (`:406`), a cumulative read
   landing on exactly 4096 is rejected even though the peer never exceeded the budget.
2. **The check precedes `framer.feed` (`:436`).** A chunk that *already contains a complete first
   frame* is discarded unread. The reachable manifestation is a valid Logon coalesced with enough
   of the next frame to push the cumulative read to or past the budget — e.g. Logon(3500) +
   596 bytes of the next frame = exactly 4096, or + 597 bytes = 4097. The Logon is complete and
   parseable; the connection is dropped anyway.

The production comment at `:853` asserts that the 4096-byte maximum "covers any valid FIX Logon
message" — true of a Logon in isolation, false at the coalescing boundary. That comment is part of
what this feature must make honest.

**Note:** the loop-top check at `:408-411` is **unreachable dead code**. The sole caller
default-constructs `frame_buf` (`:857`, `std::vector<std::byte> frame_buf;`, reserve-only at `:858`)
and passes it empty at the call (`:861-862`), so the first iteration cannot trip it, and `:426`
returns before control can come back round. Whatever remedy is chosen must dispose of this twin
rather than editing both copies to stay consistent.

### A third collaborator the budget decision depends on — the framer's carry capacity

`engine.cpp:402` builds the framer's carry buffer at capacity `max_bytes`, and `Framer::feed` appends
every fed byte into it **before any parse**, failing the whole feed on overflow
(`src/wire/framer.cpp:194-201`). Any cumulative bound above `max_bytes` therefore collides with that
capacity one step before the accept-path budget can be evaluated at all. This is not a third defect —
it is a constraint the remedy for Defect 1 must satisfy, and it is why FR-013 now names the carry
capacity explicitly. See research §D-1a for the full derivation.

### Defect 2 — the deadline timer handler can outlive the coroutine frame it captures

`src/session/engine.cpp:388-399`:

```cpp
bool timed_out = false;                                    // coroutine-frame local
timer.async_wait([&timed_out, &transport](const std::error_code& ec) {
    if (!ec) { timed_out = true; transport.cancel(); }
});
```

Every return path *that can precede expiry* calls `timer.cancel()` (`:409`, `:420`, `:427`, `:439`,
`:450`) — the sixth `co_return`, at `:454`, does not, because it is reached only *after* the timer has
fired, so cancellation there is moot. But **`cancel()` cannot
un-queue a handler that has already completed.** If the 5000 ms expiry and the pending
`async_read_some` completion are selected in the same event-loop drain and the read is dequeued
first, the read handler resumes the coroutine, which runs to `co_return` and destroys its frame —
the awaitable temporary at `:861-862` dies at the end of that full-expression — and the stranded
timer handler then executes against dead state.

Two consequences, of unequal severity:

- **Write-to-freed (memory safety).** `timed_out = true` targets a destroyed coroutine-frame local.
- **Spurious teardown of a live session (functional, and the sharper one).** `transport` is a
  reference to the `unique_ptr`-owned transport in `run_accept_loop`, which on the *success* path
  has by then been **moved into a live `Session`** (`:922`,
  `session->attach_accepted_transport(std::move(transport), …)`). The late `cancel()` aborts the
  read pump of a session that has just finished establishing. There is **no error path that
  attributes it** — the session simply dies with a `transport_read_cancelled` that nothing explains.

### Census — the same handler shape elsewhere in `src/`

Every non-`co_await`ed `timer.async_wait([` site in `src/` was enumerated (`grep -rn "async_wait(" src include`).
There are **four** of this shape, one more than issue #233 names:

| Site | Captures | Dangle leg | Late-cancel-on-live-object leg |
|---|---|---|---|
| `src/session/engine.cpp:394` (first-frame deadline) | coroutine-frame locals `timed_out`, `transport` | **YES** | **YES** — the transport is moved into a live `Session` at `:922` |
| `src/transport/asio_tls_transport.cpp:910` (connect timeout) | `this` | **YES** — `reconnect_fsm.cpp:250-252` destroys `t` on the failure arm | **YES** — late `socket_.cancel()` after a successful connect |
| `src/transport/asio_tls_transport.cpp:1032` (TLS handshake timeout) | `this` | **YES** — `reconnect_fsm.cpp:284-286` **and** `engine.cpp:841-844` both destroy the transport on the failure arm | **YES** — late `socket_.cancel()` after a successful handshake |
| `src/transport/asio_plain_transport.cpp:130` (connect timeout, "mirrors TLS transport") | `this` | **YES** — same `reconnect_fsm.cpp:250-252` owner | **YES** — same shape; **NOT named in issue #233** |

> **Corrected at Gate A round 1.** The three transport rows previously read *"Dangle leg: no"*, on the
> reasoning that a handler capturing `this` cannot dangle because "the transport outlives it". **That
> is refuted by the transports' owners**, neither of which was opened when the census was written:
> `reconnect_fsm.cpp:250-252` and `:284-286` destroy a block-scope `std::unique_ptr` (`:247`) via
> `continue` on the failure arm, and `engine.cpp:841-844` does the same to the accept loop's
> `transport` (declared `:810`) — that last one being the TLS-handshake-timeout site on **this
> feature's own accept path**. Neither destructor drains
> (`src/transport/asio_plain_transport.hpp:71`, `src/transport/asio_tls_transport.hpp:176` are both
> `~… override = default;`) and the timers are coroutine-frame locals, so their destruction cancels a
> *pending* wait but cannot un-queue a completed one.
>
> **The correction is not that the pre-fix code has a new bug** — pre-fix, the stranded handler
> already calls `socket_.cancel()` through the same dangling `this`. The correction is that "no dangle
> leg" was the **sole stated rationale** for choosing a per-attempt epoch over a join at these sites
> (FR-014, and research D-4), so the mechanism decision rested on a property the source does not have.
> The mechanism is re-taken in research §D-4.1: the epoch is kept but moved into **shared state the
> handler owns by value**, so the guard decides without touching `this` at all.

All remaining `async_wait` uses in `src/` are `co_await`ed — `engine.cpp:1350`, `engine.cpp:1366`,
`session.cpp:1475`, `reconnect_fsm.cpp:130`, `system_clock_source.cpp:239` — so the awaiter cannot
leave scope while the wait is outstanding and no handler can be stranded. They are **out of scope,
and that exclusion is load-bearing**: it is the reason this feature is not an engine-wide
cancellation refactor.

**All four are in scope for this feature** (Clarifications Q3 — class-fix).

> **A FIFTH touched site was added at Gate A round 2, and it is deliberately NOT a row above.** The
> census above enumerates one *shape* — a non-`co_await`ed `timer.async_wait([…])` whose handler can
> be stranded. FR-018 touches a different construct entirely: the **one-argument
> `reset_cancellation_state`** at `src/transport/asio_tls_transport.cpp:1134`, inside
> `asio_tls_transport::async_read_some`. It is listed here rather than as a fifth row so the
> shape-census stays a census of one shape (adding it would make SC-008's "all four enumerated sites"
> ambiguous), but it is in the diff and the delivered artifacts must say so.
>
> **The fifth site is SHARED, and that is its whole risk profile.** `Transport::async_read_some`
> (interface at `include/fixpp/transport/transport.hpp:111`) has exactly **two production call sites
> in the entire tree** — `src/session/engine.cpp:413` (`read_first_frame_bounded`, this feature's
> accept path) and `src/session/engine.cpp:542` (`run_read_pump`, the established-session read pump).
> The only other override is the test-only `mock_transport`
> (`include/fixpp/transport/test/mock_transport.hpp:164`). That is the **whole** caller set, verified
> two independent ways (CodeGraph `callers`, and a by-name sweep over `src/` + `include/`, per
> [[feedback_caller_census_by_call_not_syntax]]). The consequence of the sharing is that changing the
> TLS read filter changes live-session `stop()` behaviour too — the INV-4a question, dispositioned
> **SAFE BY STRAND CONSTRUCTION** in research §D-2a.
>
> **The same latent property exists elsewhere in the same file and is deliberately left alone.**
> `async_handshake` (`asio_tls_transport.cpp:988`) and `async_write` (`:1195`) use the same
> one-argument mask, and the SSL handshake/write composed ops are likewise terminal/partial-only
> (`asio/ssl/stream.hpp:489-495`, `:735-742`). (`async_connect` is **not** in this list — it already
> installs the two-argument OUT map at `:930-933`, and that is FR-018's precedent.) `stop()` during
> handshake or write is ignored by the op but stays
> **bounded** — by the epoch-guarded connect/handshake timers and by `stop()`'s step-2
> `transport->close()` (`engine.cpp:1302-1341`), which is what wakes a write-blocked session and is
> why that step remains load-bearing after (b+). Stated so the next join-shaped refactor does not
> re-discover it the hard way; **no change is made to either.**

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A valid Logon at the budget boundary establishes a session (Priority: P1)

An acceptor peer sends a well-formed Logon that the network coalesces with the leading bytes of its
next message, so the operator's first read from the socket lands exactly on — or just past — the
first-frame byte budget. The Logon itself is complete, in-budget, and parseable. Today the
connection is dropped with no diagnosis; the peer sees an unexplained disconnect immediately after a
successful TLS handshake and retries into the same wall.

**Why this priority**: this is the defect a real counterparty hits. It is a *silent interoperability
failure* on the very first message of the session, and its likelihood scales with how promptly the
peer pipelines after Logon — i.e. it is worst against a well-behaved fast peer.

**Independent Test**: drive an accepted connection with a single write whose cumulative size sits at
the boundary and which begins with a complete Logon; assert the session reaches its established
state and the surplus bytes are carried into the read pump.

**Acceptance Scenarios**:

1. **Given** an accepted, handshaken connection, **When** the peer's first read delivers exactly
   `max_bytes` cumulative bytes beginning with a complete, valid Logon frame, **Then** the first
   frame is delivered to the session and the connection is NOT closed.
2. **Given** the same connection, **When** the delivered chunk contains a complete Logon plus
   surplus bytes belonging to the next frame, **Then** only the first frame's exact length is
   reported to the caller and the surplus is carried forward into the read pump (the existing
   F-015-002 behaviour is preserved at the boundary, not only below it).
3. **Given** the same connection, **When** the peer's first read delivers a complete, valid Logon at
   a cumulative size that **exceeds** `max_bytes`, **Then** the frame still wins and the connection
   is NOT closed — the complete frame is honoured regardless of the surplus behind it.

---

### User Story 2 - A session that establishes as the deadline fires is not torn down (Priority: P1)

An acceptor peer completes its handshake and sends its Logon at the very end of the pre-session
deadline window, such that the deadline expiry and the read completion are ready in the same
event-loop drain. The session is established and published. Today a stranded timer handler can then
cancel that live session's transport — a silent teardown with no attributing error — and, on the
same path, write into a destroyed coroutine frame.

**Why this priority**: it is a memory-safety defect (undefined behaviour) *and* a
correctness defect that destroys an already-successful session. Both legs sit on the production
accept path.

**Independent Test**: force the same-drain selection deterministically (rather than hoping for it),
run under ASan and TSan, and assert both that no write-to-freed occurs and that the established
session survives — i.e. its read pump is not cancelled.

**Acceptance Scenarios**:

1. **Given** a first-frame read whose deadline expires in the same event-loop drain in which the
   read completes with a valid Logon, **When** the coroutine returns successfully, **Then** no
   handler writes to any object owned by the returned coroutine's frame.
2. **Given** the same scenario, **When** the call has returned and the event loop is drained to
   completion, **Then** the transport has received **zero** `cancel()` calls from any handler the
   call armed — which is what makes it impossible for a transport already moved into a live `Session`
   (`:922`) to have its read pump cancelled. *(Restated at Gate A round 1 from "that session's
   transport is NOT cancelled and its read pump continues" — see SC-006 and clarification G-2. The
   live-session teardown is the consequence this scenario exists to prevent; the assertable form is
   the absence of any surviving handler.)*
3. **Given** a genuine slow-loris peer that sends nothing, **When** the deadline expires with no
   read completion pending resolution, **Then** the read is still aborted and the connection is
   still closed and reclaimed — the deadline's protective purpose (FR-014) is unchanged.

---

### User Story 3 - Genuinely over-budget peers are still closed (Priority: P1, regression guard)

A hostile or broken peer streams bytes past the first-frame budget without ever completing a frame.
This MUST continue to be closed and its accept slot reclaimed. Nothing in this feature may relax
FR-014's protective intent; the fix narrows *when* the budget fires, and must not narrow *whether*
it fires.

**Why this priority**: co-equal with US1/US2 — the whole point of the budget is DoS resistance on a
pre-session window that precedes any `Session` object and therefore any per-session limit.

**Independent Test**: the existing over-budget witness from PR #232 (a `budget + 1` payload that
never completes a frame) must remain green, joined by a pin that the *new* accept path does not
admit an unbounded incomplete stream.

**Acceptance Scenarios**:

1. **Given** a peer that sends more than the budget with no complete frame present, **When** the
   budget is exceeded, **Then** the connection is closed with `wire_frame_too_large` and the accept
   slot is reclaimed.
2. **Given** a peer that never sends a complete frame but stays under the budget, **When** the
   deadline expires, **Then** the connection is closed and reclaimed.

---

### User Story 4 - Transport connect/handshake timers do not cancel a socket that succeeded (Priority: P3)

The connect-timeout and TLS-handshake-timeout timers in the plain and TLS transports carry the same
late-handler shape. A handler that completed in the same drain as a *successful* connect or handshake
still calls `socket_.cancel()` afterwards, aborting the first legitimate operation on a healthy
socket — and, because both owners destroy the transport synchronously on the failure arm (see the
census correction above), that handler can also run after the transport it captured is gone.

**Why this priority**: lower than US1–US3 — the window is narrower and the pre-fix dangle is a
call on a destroyed object rather than a torn-down live session — but it is the same root cause, and
a class-fix scoped to a single occurrence is the pattern that returns at Gate B. *(Priority left at P3
after the Gate A round-1 census correction: the memory-safety leg is real at these sites too, but it
is pre-existing, has no reported field manifestation, and the remedy is the same one already scoped.)*

**Independent Test**: for each transport, assert that the attempt's timer epoch has been retired by
the time the connect (resp. handshake) returns, so any expiry still in flight is stale — see SC-014
for why the *same-drain ordering itself* is not constructible at this layer.

**Acceptance Scenarios**:

1. **Given** a connect that succeeds in the same drain in which its connect-timeout expires,
   **When** the connect returns success, **Then** no subsequent socket operation is cancelled by the
   expired timer.
2. **Given** a TLS handshake that succeeds in the same drain in which its handshake timeout expires,
   **When** the handshake returns success, **Then** no subsequent socket operation is cancelled.

---

### Edge Cases

- **Cumulative size exactly `max_bytes`, complete frame present** — must be admitted (the defining
  US1 case; today rejected).
- **Cumulative size exactly `max_bytes`, no complete frame present** — the budget's purpose is
  unmet: the peer has *not* exceeded the budget, so it must not be closed on the budget; the
  deadline remains the backstop and the next read either completes a frame or trips the budget.
- **Cumulative size `max_bytes + 1` (or more), complete frame present in the delivered bytes** — the
  frame wins; the connection is NOT closed (Clarifications Q1 = both legs). This is the case that
  distinguishes the delivered invariant from the cheap comparison-only fix, so it MUST carry its own
  pin — a test suite that covers only the `== max_bytes` case cannot tell the two apart.
- **The chunk that pushes the cumulative size past the budget contains NO complete frame** — closed
  on the budget at that evaluation. Because each read is clamped to `max_bytes + 1 - buf.size()`
  (C1), the cumulative size at that point is exactly `max_bytes + 1` and can never be more.
- **`buf.size()` is exactly `max_bytes` on entry to the clamp** — the read requests exactly 1 byte.
  The clamp must not compute 0 (which would spin on a zero-length read) nor underflow. This is the
  off-by-one FR-013 obligates proving.
- **A read that COMPLETES with zero bytes and no error** — would leave `buf` unchanged, feed an empty
  span, and re-enter the loop identically: the same spin, reached from the completion side rather
  than the request side. **What closes this is the clamp, not a transport contract**: `room >= 1`
  guarantees the request is **never empty**, and no production transport can complete a **non-empty**
  stream read with zero bytes and no error (`include/fixpp/transport/transport.hpp:101-104`; EOF and
  error mapping at `asio_plain_transport.cpp:224-233` and `asio_tls_transport.cpp:1167-1182`). The
  clamp's boundary witness must **not** script a zero-byte success on a non-empty request, or it
  tests the harness rather than the clamp.
  *(**Qualifier added at Gate A round 4**, and it is load-bearing: the unqualified claim — "neither
  production transport can produce it" — is **FALSE for a zero-length request**. asio treats an
  all-empty stream read as a **no-op that succeeds**: `asio/detail/impl/socket_ops.ipp:890-895`
  — *"A request to read 0 bytes on a stream is a no-op"* → `error::clear(ec); return 0;` — reached
  because `async_receive` passes `all_empty(buffers)` as the operation's no-op flag
  (`asio/detail/reactive_socket_service_base.hpp:426-428`). The TLS leg is **unverified** rather than
  false (no zero-length guard in `asio/ssl/detail/impl/engine.ipp`), which is no better as a proof.
  Resting the property on the clamp makes the transports' behaviour on an empty request irrelevant.
  The prior citation of `asio_tls_transport.cpp:1162-1163` is dropped — that is the **initiation** of
  `ssl_stream_->async_read_some`; the mapping is `:1167-1182`.)*
- **A frame fragmented across several reads that completes exactly at the budget** — the stateful
  framer carries unconsumed bytes across `feed` calls (`:432-437`); the boundary must be evaluated
  against the same accumulation the existing fragmentation fix (F-015-001) established, not against
  a single chunk.
- **The framer itself reports `wire_frame_too_large`** (`:438-442`) — a distinct rejection from the
  accept-path budget, which must remain distinct. It has **two** sub-causes and FR-013's
  carry-capacity clause kills exactly one of them: a `parse_frame` rejection of a *declared*
  BodyLength or computed frame length above `Framer::cfg_.max_frame_bytes`
  (`src/wire/framer.cpp:120-121`, `:129-130`, `:179-180`) stays reachable within 4097 buffered bytes;
  the `pmr_carry_buffer::append` overflow (`:199-201`, `:246-248`) becomes **unreachable** once the
  carry capacity equals the cumulative bound. The distinction survives; the budget-attributable source
  of `wire_frame_too_large` on this path becomes the accept-path check alone, which is what FR-007
  wants. *(Clarification G-3.)*
- **Deadline expires *between* reads with no read outstanding** — must still return
  `transport_handshake_timeout` (`:454`), unchanged. This is preserved **only because** the timer is
  armed once with an absolute expiry and a wait on an already-expired timer completes immediately
  (FR-017); a design that re-armed per iteration would break it silently.
- **Engine `stop()` during the first-frame read** — total cancellation must still abort the read
  promptly; the remedy for Defect 2 must not introduce an operation that survives `stop()`.
  **This edge case is where the round-1 design failed, and it fails differently per transport.** On
  **plain TCP** the joined form is already correct — the socket read op honours `total`
  (`asio/detail/reactive_socket_service_base.hpp:716-725`). On **TLS** — the default accept path — the
  `total` is silently discarded inside asio's SSL composed operation, the read arm never retires, and
  `stop()` hangs **unboundedly**, because its own step-3 join is a deadline-less spin
  (`src/session/engine.cpp:1342-1353`). Worse, `stop()`'s `total` also retires the deadline arm and
  consumes the group's one-shot cancel guard, so the pre-fix 5 s escape is destroyed as well. Closed
  by **FR-018**; witnessed by **SC-018**. See Q2 as amended (clarification G-4).
- **A same-drain expiry on the *failure* path** (read fails, deadline fires) — the connection is
  closed either way; the remedy must not double-close or leak the accept slot.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The first-frame byte budget MUST reject a peer only when that peer has **exceeded** the
  budget, not when it has reached it. Equality with the budget MUST NOT by itself close the
  connection. *(Corrects the implementation to 015's FR-014 "exceeds" / SC-011 "more than".)*
- **FR-002**: A first-frame read MUST NOT discard bytes that already constitute a complete first
  frame. The framing decision MUST be taken on every newly-read chunk **before** the byte-budget
  decision, and a complete first frame MUST win over the budget unconditionally — including when the
  chunk that completed it pushed the cumulative size past the budget. *(Q1 = both legs; the delivered
  invariant is "reject only when no complete frame is extractable from the bytes read".)*
- **FR-003**: The over-budget close MUST be preserved for a peer that exceeds the budget without a
  complete frame becoming available: `wire_frame_too_large`, transport closed, accept slot
  reclaimed, other peers unaffected. This feature MUST NOT weaken FR-014's protective behaviour.
- **FR-013**: The pre-session read MUST remain hard-bounded in bytes, at **`max_bytes + 1`**. Because
  FR-002 defers the budget decision until after the chunk has been framed, each read MUST be clamped
  to at most `max_bytes + 1 - buf.size()` bytes so that deferral cannot widen the bound (C1). The
  bound MUST be a constant, never a function of peer behaviour, and MUST NOT be exceeded by any
  sequence of peer writes. The clamp expression MUST be shown not to underflow, and the proof MUST
  also cover the **completion** side: a read that completes with zero bytes and no error would spin.
  **That side MUST be discharged by the clamp — `room >= 1 ⇒ want >= 1 ⇒ the request is never
  empty` — and not by a transport-contract claim.** No production transport can complete a
  **non-empty** stream read with zero bytes and no error
  (`include/fixpp/transport/transport.hpp:101-104`, *"Awaitable completes with byte count (always > 0
  on success per ASIO async_read_some); EOF → transport_read_eof"*); the EOF/error mapping is at
  `asio_plain_transport.cpp:224-233` and `asio_tls_transport.cpp:1167-1182`.
  *(**Round 3's correction of this citation was itself incomplete, and is superseded at Gate A round
  4.** Round 3 substituted `transport.hpp:101-104` as *"the governing statement"* — but that sentence
  is true only of **non-empty** requests, so the substitution propagated a claim with a missing
  qualifier rather than fixing one. For a **zero-length** request asio succeeds with zero bytes:
  `asio/detail/impl/socket_ops.ipp:890-895` → `error::clear(ec); return 0;`, reached via
  `all_empty(buffers)` at `asio/detail/reactive_socket_service_base.hpp:426-428`. The proof is
  therefore rested on the clamp, which needs no transport contract at all and is unaffected by either
  transport's zero-length behaviour. Round 3's line numbers are also corrected: the TLS mapping is
  `:1167-1182`, not `:1166-1181`.)*
  **Every buffer that accumulates the cumulative prefix MUST be sized to the same bound** — that is
  `buf` and the framer's `carry`. *(Scoped at Gate A round 3 (C12). The previous wording was "every
  buffer on this path", which literally contradicts the deliberate retention of a 4096-byte
  per-read scratch `read_buf` in `data-model.md` §1.1. A **per-read scratch** buffer need not equal
  the cumulative bound and MUST NOT be required to: every request is `min(read_buf.size(), room)`, so
  a scratch smaller than the bound costs at most one extra loop iteration and resizing it to 4097
  would be functionally inert. The requirement is about **cumulative** buffers, which is what the next
  sentence always said — the general clause was simply wider than its own instance.)*
  In particular the framer's carry
  buffer (`engine.cpp:402`) MUST have capacity **at least `max_bytes + 1`**, stated in the delivered
  code as a value *derived from* this bound rather than as an independent constant. A carry capacity
  below the cumulative bound rejects the feed **before** the frame can be found and **before** the
  budget decision of FR-007 can be reached, which would defeat FR-002 and FR-007 together. *(Added at
  Gate A round 1; see research §D-1a. Clarification G-3.)*
- **FR-017**: The deadline timer MUST be armed **exactly once**, with an absolute expiry, **before**
  the read loop is entered. No per-iteration re-arming is permitted: the deadline arm of FR-005's
  join re-*awaits* the already-armed timer and MUST NOT call `expires_after`. Re-arming per iteration
  would reset the deadline on every completed read, so a peer that drips bytes indefinitely would
  never trip it — silently removing the protection FR-004 and FR-014 name, with every existing test
  still green. *(Added at Gate A round 1; see research §D-1b.)*
- **FR-004**: The deadline behaviour MUST be preserved: a peer that stalls the pre-session window
  MUST still have its in-flight read aborted and its connection closed and reclaimed within the
  deadline. The remedy for FR-005 MUST NOT reintroduce the between-reads-flag-polling behaviour
  that 015's `/simplify` (Q-2) explicitly rejected.
- **FR-005**: No deadline-timer completion handler on the accept path MAY execute against state
  owned by a coroutine frame that has been destroyed. The first-frame read MUST NOT be able to
  return while a handler capable of touching its frame is still pending or queued. *(Q2 = joined
  race: the read and the deadline wait are joined so that neither can outlive the frame, rather than
  relying on `timer.cancel()` — which cannot un-queue an already-completed handler.)* **The join is
  only correct under cancellation when FR-018 is delivered alongside it** — without the transport-side
  OUT map the joined form cannot retire under `Engine::stop()` on TLS (Q2 as amended at Gate A
  round 2; research §D-2a).
- **FR-006**: A deadline-timer completion handler MUST NOT cancel a transport that has already been
  handed to a live `Session`. A successfully established session MUST NOT be torn down by the expiry
  of the pre-session deadline that it beat. *(Delivered by the same join as FR-005, and therefore
  subject to the same FR-018 precondition.)*
- **FR-014**: At the three transport timer sites, a connect-timeout or handshake-timeout expiry MUST
  NOT cancel a socket after its connect or handshake has already completed successfully, **and MUST
  NOT dereference the transport at all once that transport has been destroyed**. **The
  mechanism is `/plan`'s choice, per site** — but it MUST be chosen against the corrected census
  above: these sites capture `this` and **do** have a dangle leg, so any guard that reads a
  *member* to decide staleness reads through a pointer that may already be dangling. The joined form
  chosen for FR-005 is not automatically the right remedy here either: a per-attempt epoch or
  generation check closes the late-cancel leg without disturbing the existing cancellation-state
  plumbing, which at `src/transport/asio_plain_transport.cpp:137-144` already installs an OUT filter
  that any `||` composition would have to compose with. Whatever is chosen, the handler MUST decide
  staleness from state it **owns by value**, before touching any member. `/plan` MUST state the
  mechanism chosen for each site and why. **Each of the three MUST carry its own witness** (C4); an
  equivalence-by-inspection argument does not discharge this. See SC-014 for what that witness can
  and cannot assert — the same-drain *ordering* is not constructible at this layer (clarification
  G-1), so the witnessed property is the retirement, not the race.
  **Relationship to FR-018, added at Gate A round 2.** FR-018 installs an OUT-mapping cancellation
  filter in the TLS **read** path (`asio_tls_transport.cpp:1134`). That is a *different* site from
  these three timer sites and a *different* mechanism from the epoch guard; the two do not interact.
  The epoch guard is unchanged, and no site fixed under this FR gains or loses an OUT filter. Stated
  because the OUT-filter idiom will then appear at **three** places in the two transports — plain
  connect (`asio_plain_transport.cpp:141-144`), TLS connect (`asio_tls_transport.cpp:930-933`), and,
  new, the TLS read path — and a reader must not conclude that FR-018 supersedes the mechanism chosen
  here.
- **FR-015**: `Engine::stop()` issued while a first-frame read is in flight MUST still abort that
  read promptly, reclaim the accept slot, and tear down cleanly under the sanitizer matrix — and this
  MUST be pinned by a **dedicated** regression test, not inferred from existing engine-stop coverage
  (C2). The joined form introduced by FR-005 MUST NOT create an operation that outlives a stop.
  **Corrected at Gate A round 2 — this requirement is the one the unamended design violated, and it
  MUST hold on BOTH transports.** On TLS, the joined form as originally specified creates exactly the
  forbidden operation: `stop()`'s `total` is dropped inside asio's SSL composed op, the read arm never
  retires, and `stop()`'s own step-3 join (`src/session/engine.cpp:1342-1353` — an unbounded spin with
  no deadline) never terminates. **FR-018 is what makes FR-015 deliverable**; the two MUST be read
  together and neither discharges the other. Note the direction of the change: with FR-018, `stop()`
  on a TLS first-frame read becomes **prompt**, where the pre-fix code was only 5 s-bounded — so
  against FR-015's own word *"promptly"* this is a strict improvement, not a restoration of the status
  quo. *(Q2 as amended / clarification G-4.)*
- **FR-018**: `asio_tls_transport::async_read_some` MUST install a **two-argument**
  `reset_cancellation_state(asio::enable_total_cancellation(), <OUT filter mapping any non-`none`
  cancellation type to `cancellation_type::terminal`>)`, replacing the one-argument form at
  `src/transport/asio_tls_transport.cpp:1134`, so that a `cancellation_type::total` emitted by
  `Engine::stop()` actually aborts the in-flight `ssl::stream::async_read_some`. The one-argument form
  does **not** suffice: `asio::cancellation_filter` is a **mask**, not a map
  (`asio/cancellation_state.hpp:31-39`), and the one-argument reset sets both the IN and the OUT
  filter to it (`:121-126`), so `total` is re-emitted as `total` and is then discarded by the SSL
  composed operation's terminal-only inner cancellation state (`asio/ssl/detail/io.hpp:100-106` →
  `asio/detail/base_from_cancellation_state.hpp:44-48` → `asio/cancellation_state.hpp:88-100`).
  **The obligation MUST be discharged in the transport, not at the call site**:
  `this_coro::reset_cancellation_state` replaces the single bottom-frame cancellation state
  (`asio/impl/awaitable.hpp:726-732`) and the last reset wins, so an engine-side arm wrapper is
  clobbered by the transport's own first-statement reset — an engine-local remedy is structurally
  impossible.
  **The delivered form MUST mirror `src/transport/asio_tls_transport.cpp:918-933`** — the same file's
  `async_connect` path, which solves this identical hazard (016 T008) — **including its commenting
  discipline**: the in-source comment MUST state *why* the map exists (that the SSL op honours only
  `terminal`, so an unmapped `total` is silently dropped) and MUST carry the same
  `[[feedback_asio_cospawn_total_cancellation_default]]` and
  `[[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]` mnemonics that
  `:928-929` already carries. Mapping to `terminal` rather than forwarding `total` is deliberate and
  MUST be stated: SSL does not support `total` because a mid-record TLS read cannot be abandoned
  side-effect-free, and every canceller on this path closes the transport afterwards.
  **The plain transport is explicitly out of scope for this requirement** and MUST NOT be changed: its
  TCP read op honours `terminal|partial|total` natively
  (`asio/detail/reactive_socket_service_base.hpp:716-725`), where `total`'s stronger no-side-effects
  semantics are genuinely available. The asymmetry is principled and MUST be recorded rather than
  smoothed over. *(Added at Gate A round 2; Q2 amended to (b+) / clarification G-4; research §D-2a.)*
- **FR-016**: `read_first_frame_bounded` MUST be reachable from tests directly, via an **internal
  header under `src/session/`** (C3), so the same-drain scenarios of FR-005/FR-006 can be constructed
  deterministically rather than awaited probabilistically. The precedent is exact:
  `scan_first_frame_ids.hpp` lives at `src/session/scan_first_frame_ids.hpp` — inside `src/`, not
  `include/` — so it is outside the install set by construction and FR-012's empty public-surface
  claim holds without any install-list work. No `FIXPP_TEST_HOOKS`-gated branch is added to the
  accept path for this purpose.
- **FR-007**: The unreachable duplicate budget check at `src/session/engine.cpp:408-411` MUST be
  eliminated rather than maintained in parallel with its live twin — the delivered code MUST have a
  single place where the budget decision is taken, and that place MUST be **after** the framing step
  of the same iteration (i.e. at the foot of the loop body, past the frame-found return). This
  placement is not cosmetic: it is what guarantees `buf.size() <= max_bytes` on entry to every
  subsequent clamp evaluation, and therefore what makes FR-013's `max_bytes + 1 - buf.size()`
  underflow-free and always `>= 1`. Moving the decision elsewhere invalidates FR-013's proof.
  **That single decision point MUST be reachable.** A dead check moved is not a dead check removed:
  the delivered design MUST be shown to admit at least one peer behaviour that reaches this
  comparison with it true — which requires FR-013's carry-capacity clause, without which the framer
  rejects one step earlier on every over-budget input and this check is dead in its new position too.
  *(Reachability clause added at Gate A round 1; the trace is in research §D-1a.)*
- **FR-008**: The production comments that state the delivered contract MUST be corrected in the
  same change — specifically the `:853` claim that 4096 bytes "covers any valid FIX Logon message"
  and the `:373-375` invariant list, both of which describe the pre-fix behaviour. The full
  enumeration is research §D-8, which lists **five** sites — the two added at Gate A round 1 are the
  carry buffer's capacity argument (`:402`, which must carry its derivation from the clamp bound) and
  the read-error arm's comment (`:416-419`), whose reasoning goes obsolete with the code it describes.
- **FR-009**: **All four** enumerated same-shape timer-handler sites MUST be fixed in this feature —
  `src/session/engine.cpp:394`, `src/transport/asio_tls_transport.cpp:910`,
  `src/transport/asio_tls_transport.cpp:1032`, and `src/transport/asio_plain_transport.cpp:130`. The
  census MUST be restated in the delivered artifacts, and if the delivered census differs from the
  four above (a site added, removed, or found to have a different shape), the difference MUST be
  recorded rather than silently absorbed. *(Q3 = class-fix.)*
- **FR-010**: Each behaviour corrected by this feature MUST carry a regression pin that has been
  **demonstrated RED against the pre-fix source**, not merely green after the fix. A pin whose red
  state is unproven does not discharge this requirement.
- **FR-011**: The pre-existing over-budget witness in `tests/session/engine_firstframe_test.cpp`
  (which uses a `budget + 1` payload precisely so it is agnostic to `>=` vs `>`) MUST remain green
  and MUST NOT be modified to accommodate the fix. If it needs modification, the fix has changed
  behaviour beyond what this spec authorises.
- **FR-012**: The feature MUST NOT change the public C++ API, the C ABI, the wire format, the
  session FSM, or the error-code enumeration. Its entire delivered surface is internal to the accept
  path and the two transports.

### Key Entities

- **First-frame byte budget** (`kFirstFrameMaxBytes`, 4096): the cumulative cap on bytes read from an
  accepted connection before a complete first frame must be present. A DoS bound on a window that
  precedes any `Session` and therefore any per-session limit.
- **First-frame deadline** (`kFirstFrameDeadline`, 5000 ms): the wall-clock cap on the same window.
  Enforced by cancelling the in-flight read, not by polling a flag between reads.
- **Deadline-timer completion handler**: the callback armed against the deadline timer. The subject
  of Defect 2 — its lifetime is not currently bounded by the lifetime of the state it captures.
- **Accept-path transport ownership transfer**: the point (`:922`) at which the accepted transport
  moves from a `run_accept_loop` local into a live `Session`. The reason a late cancel is a
  *functional* defect and not merely a wasted call.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A first-frame read whose cumulative delivery is exactly the first-frame byte budget and
  which begins with a complete, valid Logon **returns that frame's exact length rather than
  `wire_frame_too_large`**, so the connection is not closed. *(RED against pre-fix source.)*
  *(Narrowed at Gate A round 1 from "a session establishes": the witness drives the helper directly
  (C3/FR-016) and the helper's return value is the direct observable of the only code this feature
  changes. Everything downstream — `scan_first_frame_ids`, registry resolution, `Session::open`,
  `attach_accepted_transport` at `engine.cpp:922` — is untouched by 088 and is already covered
  end-to-end by PR #232's six accept-path witnesses. See research §D-6.6.)*
- **SC-002**: Surplus bytes coalesced behind that boundary-sized Logon reach the read pump; the
  reported first-frame length is the Logon's exact length, unchanged from the sub-budget case.
- **SC-003**: A peer that exceeds the byte budget without ever completing a frame is still closed
  with `wire_frame_too_large` and its accept slot reclaimed; the PR #232 over-budget witness passes
  unmodified.
- **SC-004**: A peer that sends nothing, or a partial frame, is still closed within the deadline and
  its accept slot reclaimed.
- **SC-005**: Under a deterministic same-drain selection of deadline expiry and read completion,
  **ASan reports zero findings** — specifically no use-after-free attributable to the deadline
  handler. *(RED against pre-fix source under ASan.)* The RED run MUST drive the helper through
  `co_spawn(..., detached)` so its frame is heap-allocated, and the pre-fix report MUST name a
  **heap**-use-after-free; a clean ASan run on pre-fix source is read as *"the proof did not fire"*,
  never as *"there is no defect"*. *(HALO-sensitivity clause added at Gate A round 1 — see research
  §D-6.3. The **TSan clause was removed**: both transports document strand confinement and the accept
  loop is single-strand, so the stranded handler and the coroutine are serialized — the defect is a
  sequential use-after-free, not a data race, and TSan has no achievable RED here. T1 still runs under
  TSan as part of SC-009's matrix, as hygiene rather than as evidence.)*
- **SC-006**: Under that same scenario, **no completion handler armed by the call is outstanding when
  it returns** — asserted by draining the context to completion after the helper returns and
  observing that the transport received **zero** `cancel()` calls. *(RED against pre-fix source.)*
  *(Restated structurally at Gate A round 1, clarification G-2. The previous wording — "a session that
  established before the expiry remains live; its read pump is not cancelled and it processes a
  subsequent inbound frame" — names a postcondition the C3 direct-helper target cannot express: there
  is no `Session`, no `attach_accepted_transport`, and no read pump there, and
  `mock_transport::cancel()` is a documented no-op (`include/fixpp/transport/test/mock_transport.hpp:246-251`)
  so even the pre-fix stimulus is inert. The live-session teardown remains the **consequence** that
  makes this defect sharp — see US2 and the Key Entities — but the asserted postcondition is the
  structural one the join actually delivers.)*
- **SC-007**: The delivered `read_first_frame_bounded` contains exactly one byte-budget decision
  point; the unreachable loop-top duplicate is gone.
- **SC-008**: All four enumerated timer-handler sites are fixed, and the delivered census in the
  artifacts matches the four in this spec (or records the difference). No enumerated site is silently
  left out.
- **SC-012**: The first frame's exact length is returned — not `wire_frame_too_large` — when the chunk
  that pushes the cumulative size to `max_bytes + 1` is **also** the chunk that completes the first
  frame. The witness MUST use **fragmented delivery** — e.g. 1000 bytes, then 3097 bytes, with the
  Logon ending at byte 3500 — because with the C1 clamp a *single* delivery can never exceed
  `max_bytes`, so a single-write witness cannot reach this case at all. *(RED against pre-fix source.
  See the discrimination note below — this is the only criterion that separates the delivered
  invariant from the rejected comparison-only fix, and it is easy to write a version that separates
  nothing.)*
  **This criterion is only satisfiable if FR-013's carry-capacity clause is delivered.** At the
  pre-round-1 design's carry capacity of `max_bytes`, this exact shape is rejected by
  `pmr_carry_buffer::append` at `1000 + 3097 = 4097 > 4096` **before any parse**, so the Logon at byte
  3500 is never looked for and this criterion is RED *after* the fix as well as before it. The
  measurement is recorded in
  `research/reviews/orchestrator_088-firstframe-budget-timer-lifetime_gate_a_r1_measurements.md`.
  *(Narrowed from "a session establishes" for the same reason as SC-001 — research §D-6.6.)*

> **Discrimination note — SC-001 and SC-012 test different things, and one of them is easy to get
> wrong.** SC-001 (single delivery, cumulative exactly `max_bytes`, frame present) is RED against the
> **pre-fix** `>=` source, so it discharges FR-010 — but it passes under the *rejected*
> comparison-only fix too, so it does **not** demonstrate that the delivered invariant was chosen.
> Only SC-012's fragmented shape does: at cumulative `max_bytes + 1`, budget-before-frame rejects and
> frame-before-budget returns the Logon. A test suite that covers only the `== max_bytes` case is
> green, RED-proven, and still blind to which of the two fixes shipped
> ([[feedback_subset_check_cannot_see_symmetric_omission]]).
- **SC-013**: Three bounds hold for a pre-session connection, stated separately because the first
  draft stated only the second and that is how the carry-capacity collision went unnoticed
  *(FR-013 / C1; corrected at Gate A round 1)*:
  1. **bytes read from the peer before the decision** `<= kFirstFrameMaxBytes + 1` (4097);
  2. **logical `buf.size()`** `<= kFirstFrameMaxBytes + 1` (4097) — demonstrably not exceeded by any
     sequence of peer writes;
  3. **peak resident bytes attributable to the peer** = `buf` + `carry` + `read_buf`
     ≈ 4097 + 4097 + 4096 ≈ **12 KiB**, plus `std::vector` capacity slack on `buf` and `carry`.

  The pre-fix comparison is **8191, not 4096**: pre-fix, `engine.cpp:408` rejects only *before* the
  read, `:413-414` requests the full 4096, and `:424` inserts before `:426` rejects — so pre-fix
  logical `buf.size()` peaks at `4095 + 4096 = 8191`. The clamp is still a genuine tightening, just
  from 8191 rather than from 4096, and strictly tighter than the `+ <one read-buffer>` that the
  reordering alone would have allowed.
- **SC-014**: For **each** of the three transport timer sites — plain connect, TLS connect, TLS
  handshake — the attempt's timer epoch has been **retired before the operation returns**, so any
  expiry still in flight is stale and cannot cancel the socket. Three witnesses, one per site (C4's
  "one pin per site" honoured literally). *(FR-014 / C4. RED against the retire-point-omitted and
  guard-omitted **mutants**, not against `main` — the property did not exist pre-fix in a form a cell
  can address; see research §D-6.4 and §D-6.7.)*
  **Narrowed at Gate A round 1 (clarification G-1).** The previous wording required a *same-drain*
  witness — "a connect or handshake that succeeds in the same event-loop drain as its timeout expiry".
  **No such witness is achievable in this tree**: both timers are coroutine-frame locals with no
  injection seam (C3's seam was granted to the session helper only), and unlike the session cells the
  winning event here is a **socket** completion delivered by the reactor, whose order relative to an
  expired timer inside one `poll()` is unspecified asio scheduler internals. The residual — an
  integration witness that actually reaches the stale interleaving — is **carried and filed as a
  follow-up issue at close-out**, not written as prose and discovered unimplementable at Gate B.
- **SC-015**: `Engine::stop()` during an in-flight first-frame read aborts promptly and leaves the
  sanitizer matrix clean — asserted by a **dedicated** test that is shown to actually catch a read in
  flight rather than passing vacuously (non-vacuity observable: the mock's scripted `read_latency` is
  non-zero **and** the transport records at least one read initiated before the cancellation signal is
  emitted — the read counter this requires does **not** exist on the mock today and is one of the
  **six** mechanisms priced in research §D-9; the count moved from four at Gate A round 3, which
  added the chunked `Script` inbound and the per-read requested-size observable, and **stayed at six
  at round 4** — a seventh, a defined mock result for a zero-length request, was considered and
  **rejected** once B5's derivation was corrected (research §D-6.11)). *(FR-015 / C2.)* The **accept-slot reclaim** clause is asserted at engine scope by a
  separate cell (T2b), because the C3 direct-helper target has no `Engine`, no outstanding-accept
  counter and no accept slot. *(Split at Gate A round 1; C2's "dedicated, not inherited" is satisfied
  by T2a, which is the leg that kills D-2's bare-arm mutant — see research §D-6.1.)*
  **Amended at Gate A round 2 — neither existing cell can see the TLS leg, and one of them would hang
  on it.** *(a)* **T2a is structurally blind.** It drives `mock_transport`, whose read is a
  `steady_timer` wait and therefore honours `total`; it is green against the un-mapped TLS build. T2a
  is retained — it still kills D-2's bare-arm mutant, which is a different mutant — but it **MUST NOT
  be cited as evidence for the TLS leg of FR-015**. *(b)* **T2b drives real mTLS** (#232's harness),
  so against the un-mapped build it does not fail — it **hangs**, because `stop()`'s step-3 join
  spins without a deadline (`src/session/engine.cpp:1342-1353`). T2b therefore **MUST carry a per-test
  ctest `TIMEOUT`**, for the same reason SC-018 clause 3 requires one. *(c)* The discriminating
  evidence for the TLS leg is **SC-018 / cell T6**, and SC-015 is discharged by T2a + T2b + T6
  together, not by T2a + T2b alone — **with the division of labour made explicit at Gate A round 4**:
  T2a and T6 carry the **non-vacuity**; T2b carries the **accept-slot reclaim** (which no other cell
  can assert) and its own promptness bound, but **not** non-vacuity — see *(f)*.
  **Three further obligations added at Gate A round 3, each of which the round-2 form failed.**
  *(d)* **T2a MUST deliver its cancellation through an outer wrapper coroutine that resets to
  `enable_total_cancellation()` as its first statement** — SC-018 clause 4a, same mechanism and same
  citations. A bare `co_spawn` of the helper leaves the signal to die at the spawn's terminal-only
  initial state (`asio/impl/co_spawn.hpp:336`, `:260-263`; `asio/cancellation_state.hpp:88-100`), so
  the cell asserts nothing at all.
  *(e)* **Both T2a and T2b MUST bind a promptness threshold, because the mutant they exist to kill
  does not change the returned error value.** Under an external `total` the `parallel_group`'s
  one-shot cancel guard is consumed by the external handler
  (`asio/experimental/impl/parallel_group.hpp:168`, `:351`), so the read arm's later completion cannot
  re-emit (`:222`), the bare deadline arm runs to **full expiry**, and `order[0] == 0` returns the
  read arm's `transport_read_cancelled` — late. *(The round-1/round-2 mutant signature,
  `transport_handshake_timeout`, is **wrong**; see research §D-6.12b.)* The discriminator is
  **latency, not the error value**: T2a asserts the call returns within **100 ms** of the emit against
  a 500 ms deadline; T2b asserts `Engine::stop()` returns within **500 ms** against
  `kFirstFrameDeadline` = 5000 ms. Both thresholds are normative.
  *(f)* **T2b does NOT discharge the non-vacuity clause, and MUST NOT be cited as if it did.**
  *(Settled at Gate A round 4; round 3's proposed barrier is withdrawn.)* Peer silence does **not**
  prove the accept loop reached `read_first_frame_bounded`: `stop()` can land earlier, and every error
  takes the identical `close(); continue;` arm (`src/session/engine.cpp:863-866`). Round 3 proposed an
  **inverted** `test_hook_pre_publish_` seam as the barrier; that is withdrawn for two independent
  reasons — the hook fires at `engine.cpp:931-933`, and the interval between the read (`:861-862`) and
  that point also contains `scan_first_frame_ids` (`:877`), the registry compare (`:888`),
  **`co_await local_session->open()` (`:907`)** and `attach_accepted_transport` (`:922`), so
  *"the only awaitable in that interval"* was false; and an inverted hook is a **negative** barrier
  (*"not yet past `:931`"*), the same form SC-018 clause 4b withdrew for T6 at this same gate.
  **Therefore: SC-015's non-vacuity rests on T2a and T6.** T2b keeps its two real jobs — the
  **accept-slot reclaim** assertion at engine scope, which no other cell can make, and the promptness
  bound of *(e)* — and its claim to have caught a read in flight is recorded as a **stated bounded
  inference**, not a proof. A real near-side barrier (instrumenting the accepted transport to record
  read *initiation*) would be a **seventh** D-9 mechanism on the production accept path and is
  **filed as a residual against this criterion**, not built here. Research §D-6.13a.
- **SC-016**: The witnesses for SC-005/SC-006/SC-015 are deterministic — the ordering under test
  is constructed by the test, not awaited. No **session-layer** witness in this feature depends on
  winning a timing race. *(FR-016 / C3.)* **Scoped at Gate A round 1 (clarification G-1):** this
  criterion binds the session-layer cells, where FR-016's seam exists. It does **not** bind SC-014's
  transport cells, which — after that criterion's narrowing — no longer test an ordering at all.
  **T1 discharges this via research §D-6.2's elapse-then-poll construction** (a hand-driven
  `io_context` elapses wall time with no live handler racing it) — **D-6.2 is T1's construction
  specifically, not a blanket description of every session-layer cell.**
  **Reconciled with SC-015 at T045 (carried Gate A obligation 1):** SC-015's normative figures — T2a's
  100 ms against a 500 ms deadline, T2b's 500 ms against `kFirstFrameDeadline` — read, on their own, as
  wall-clock thresholds, which would be exactly the timing race this criterion forbids. **T2a/T2b do
  not implement that reading.** Each arms a TEST-OWNED intermediate timer at the SC-015 figure and
  asserts an ORDERING between two test-controlled events — the intermediate timer's own handler
  observes whether the result is still unset at the instant it fires (`tests/session/
  read_first_frame_bounded_test.cpp`'s T2a cell; `tests/session/first_frame_stop_test.cpp`'s T2b
  cell) — never a comparison of elapsed wall-clock against a constant. SC-015's bound survives as the
  intermediate timer's value; SC-016 holds because winning that ordering requires no live race
  against real time, only against a timer the test itself controls.
- **SC-017**: The internal header added by FR-016 sits under `src/session/` and is therefore not
  installed; the installed package's headers are byte-identical to `main`'s. *(FR-012 / FR-016 / C3.)*
- **SC-018**: On a **real TLS transport**, a `cancellation_type::total` delivered while a first-frame
  read is blocked on a silent post-handshake peer aborts that read, and the call completes with
  `transport_read_cancelled` — **not** by running out the deadline, and **not** by hanging.
  *(FR-018 / clarification G-4. RED before the OUT map is added.)* Four clauses are binding, because
  each one is a way this criterion can be satisfied vacuously:
  1. **The witness MUST drive a real `ssl::stream`.** Every other cancellation cell in this feature
     drives `mock_transport`, whose read composes an `asio::steady_timer::async_wait`
     (`include/fixpp/transport/test/mock_transport.hpp:177-186`, class contract at `:113-119`) — and a
     timer wait op **honours `total`** (`asio/detail/deadline_timer_service.hpp:315-320`). So a
     mock-driven cell is **green exactly where a real `ssl::stream` hangs**. This is structural, not a
     matter of scripting the mock better: **no mock cell can ever discriminate here.** The tree can
     host the real thing — `tests/transport/loopback_tls_fixture.hpp` provides a real
     `asio_listener` + `asio_tls_transport_factory` loopback pair on an OS-assigned port, and it is
     already consumed from `tests/session/` (`tests/session/engine_loopback_harness.hpp:44`, wired at
     `tests/session/CMakeLists.txt:839-842`).
  2. **The cell MUST be proven RED against the un-mapped build** — i.e. against the delivered design
     with FR-018's OUT filter reverted to the one-argument form — and the failure output captured into
     the verify record. A witness never seen red proves nothing
     ([[feedback_sanitizer_canary_must_be_proven_red]]); and this cell's whole purpose is to
     discriminate a case every existing cell is blind to.
  3. **The cell MUST carry a per-test ctest `TIMEOUT`.** Its regression mode is a **hang**, not a
     wrong value, and an untimed hanging test burns a CI job rather than failing one
     ([[feedback_ci_hung_test_no_timeout_burns_6h_gdb_capture]]).
  4. **The RED must be bounded, the signal must actually arrive, and the non-vacuity must be
     observable.** *(Rewritten at Gate A round 3 — the round-2 form was satisfiable by a cell that
     never ran.)* The cell MUST NOT rely on the ctest timeout as its RED signal — a timeout is
     indistinguishable from an infrastructure stall. It carries its own **watchdog**: a test-owned
     `steady_timer`, armed well below the deadline passed to the call, whose expiry closes the
     transport and sets an observable flag. Five sub-clauses, each binding:
     - **4a — the cancellation MUST reach the subject.** The cell MUST `co_spawn` an **outer wrapper
       coroutine** whose *first statement* is
       `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());`,
       and which then awaits the subject. Without it the test's `total` dies at the outer `co_spawn`,
       whose initial cancellation state is built with the **terminal-only** ctor
       (`asio/impl/co_spawn.hpp:336` → `asio/cancellation_state.hpp:88-100`) and which forwards the
       type verbatim (`asio/impl/co_spawn.hpp:260-263`): `total & terminal = none`. The cell would
       then fail **with FR-018 correctly present**. The wrapper mirrors what production has one frame
       up (`src/session/engine.cpp:673-676`, precondition P4), so it is a faithfulness requirement,
       not a test convenience.
     - **4b — positive initiation barrier.** The wrapper sets an `entered_read` flag after its reset
       and immediately before awaiting; the test proceeds only once that flag is set **and** a
       further drain shows work outstanding with no completion — i.e. suspended *inside* the read.
       An unset completion flag alone proves only *not finished*, which a coroutine that never ran
       also satisfies.
     - **4c — ordering-robust outcome.** On the **joined** leg the binding assertion is that the
       outcome is **cancellation-attributable** — `transport_read_cancelled` **or**
       `transport_handshake_timeout`, and not a success, `transport_already_closed` or
       `transport_read_eof`. An **exact** value is decided by `order[0]`
       (`asio/experimental/awaitable_operators.hpp:352-371`) when both arms are cancelled at once —
       the same class of ordering this spec's own C4 amendment calls *"unspecified asio scheduler
       internals"*, and it MUST NOT be asserted on one page and disclaimed on another. The exact
       `transport_read_cancelled` IS asserted on a **second leg** that drives
       `Transport::async_read_some` directly, where there is no join and no `order[0]`.
     - **4d — the watchdog did not fire**, which is what kills the un-mapped mutant, and the call
       returned **within 100 ms of the emit** against a 1000 ms watchdog and a 5000 ms deadline.
       These thresholds are **normative**, not illustrative.
     - **4e — the cell MUST NOT be skippable.** `FIXPP_TLS_FIXTURE_DIR` wired unconditionally at
       configure time (precedent: `tests/session/CMakeLists.txt:842`), never left to an env var whose
       absence turns the cell into a `GTEST_SKIP` — a skip is a false pass
       ([[feedback_codex_sandbox_blocks_sockets_false_pass]]). The target, its labels and its
       `TIMEOUT` are fixed by the build contract in research §D-6.13b, not left to `/tasks`.
- **SC-009**: The full sanitizer ctest matrix (ASan / UBSan / TSan) is green with 0 findings, and
  the `linux-clang-debug` local build gate is green.
- **SC-010**: The public surface delta is **empty** — no header change, no new or removed error
  code, no ABI change.
- **SC-011**: The production comments describing the first-frame contract match the delivered
  behaviour; the pre-fix claims at `:373-375` and `:853` no longer overstate it, and all **five**
  sites in research §D-8 are corrected.

## Assumptions

- **The governing requirements are pre-existing and are not being amended.** FR-014 and SC-011 of
  feature 015 already say "exceeds" / "more than". This feature corrects code to spec; it does not
  re-specify the accept path. No Master Feature Catalogue row is created. (Whether the corrected
  boundary warrants a coverage-index touch is a `/plan` question, not a spec claim.)
- **`kFirstFrameMaxBytes` stays 4096 and `kFirstFrameDeadline` stays 5000 ms.** Retuning the budget
  is a separate decision; this feature changes *when and how* the budget decision is taken (ordering
  relative to framing, and `>` rather than `>=`), never the constant itself. Raising the budget would
  mask Defect 1 rather than fix it, and would not touch Defect 2 at all.
- **The single-executor model of the current engine and its tests holds.** The accept loop and the
  session strand are the same executor today; the remedy for Defect 2 must be correct in that model
  and must not silently depend on it in a way that breaks when they separate.
- **`Transport::cancel()` is idempotent and `noexcept`** (`src/transport/asio_tls_transport.cpp:1247-1256`),
  so a late cancel on an already-**closed** socket is a no-op. This is what makes the transport sites
  lower-severity — it does **not** make them harmless, because the damaging case is a late cancel on a
  socket that is *open and in use*, and idempotence says nothing about a socket that has been
  *destroyed* (see the census correction).
- **The same-drain race will not reproduce by chance, and at one layer it cannot be constructed at
  all.** US2 needs a deterministic scheduling seam, not a timing-tuned sleep; that seam is the FR-016
  internal-header exposure plus a hand-driven `io_context` (C3), and it works there because **both**
  competing completions are timers, so asio's expiry-ordered timer queue decides. US4's sites do not
  have that property — the competing completion is a socket event from the reactor — so SC-014's
  witness is narrowed to the retirement rather than the race (clarification G-1). A test that merely
  fails to observe a race is not evidence, and neither is one written as prose.
- **The clamped read reduces the surplus carried into the read pump near the boundary.** With C1, a
  coalesced next frame is only partially buffered when the first frame ends close to `max_bytes`; the
  read pump reads the remainder from the socket. This is correct but must be pinned — the existing
  F-015-002 surplus-carry behaviour must survive the clamp, not merely survive below the boundary.
- **Local toolchain is Clang 22** per Article XVII §7; the Tier-1 mirror runs `linux-clang-debug`
  plus the sanitizer presets. gcc-release and MSVC are CI-only.
- **Provenance** — the analysis this spec rests on:
  `research/G19-fix-fpml-iso20022/research/reviews/codex_pr232_review.md` (F-5) and
  `research/G19-fix-fpml-iso20022/research/reviews/opus_pr232_1_triage.md` (F-5 dispositioned; F-8 is
  where the timer-lifetime defect was newly found and argued).

## Clarifications

> Seven decisions fork what is delivered — three at `/speckit-specify` (Q1–Q3, the blocking forks)
> and four at `/speckit-clarify` (C1–C4, the residuals those three left behind). All were settled by
> explicit user decision on **2026-08-04**, before any planning. Each is recorded with the rejected
> alternatives intact, because Gate A will want to see that the cheaper options were considered and
> why they were not taken.
>
> **Three further residuals (G-1…G-3) were settled at Gate A round 1**, also 2026-08-04, and are
> recorded in their own session block below. **They reopen no locked decision** — Q1's both-legs
> invariant, Q2's join, Q3's class-fix and C1's clamp all stand — they settle questions those
> decisions handed to the design and that the first draft answered by omission. Where a locked
> decision's *supporting reasoning* was refuted by source, the decision block carries an explicit
> correction note rather than being silently rewritten (see Q3 and C4).
>
> **A fourth (G-4) was settled at Gate A round 2**, 2026-08-05. It is the largest of the four: Q2's
> **decision** (the join) is retained, but its *feasibility argument was false at its last hop* on the
> TLS transport, and unamended the join would have made `Engine::stop()` **hang unboundedly** on the
> default accept path. Q2 is amended to **(b+)** — join **plus** a transport-side OUT-mapping
> cancellation reset — and the amendment adds **FR-018** and **SC-018**. Same convention as above: the
> superseded reasoning is marked in a `>` block, not deleted.

### Session 2026-08-04

- Q: Which invariant does the budget fix deliver — comparison-only, frame-before-budget, or both?
  → **A: Both.** Frame each chunk first (a complete frame wins unconditionally), then apply the
  budget with a strict `>`.
- Q: Which remedy shape closes the timer-lifetime defect — shared-owned state, or a joined race?
  → **A: Joined race** via `asio::experimental::awaitable_operators`' `||`. One mechanism, both legs.
  *(Amended at Gate A round 2 to **(b+)** — the join **plus** a transport-side OUT-mapping cancellation
  reset in `asio_tls_transport::async_read_some`. The joined-race decision itself is unchanged; what
  changed is that the join is not correct under `Engine::stop()` without the map. See Q2 and G-4.)*
- Q: Does the census scope include the three transport sites?
  → **A: Fix all four.** Class-fix, including `asio_plain_transport.cpp:130`, which issue #233 does
  not name.
- Q: Is the `max_bytes + one read-buffer` DoS bound acceptable, or should it be tightened?
  → **A: Tighten by clamping the read** to `max_bytes + 1 - buf.size()`. Worst case becomes
  `max_bytes + 1`, not `max_bytes + 4096`.
- Q: Does the joined form owe a direct regression pin for `Engine::stop()` during an in-flight
  first-frame read? → **A: Yes, a direct pin.** Not inherited from existing engine-stop coverage.
- Q: What may the deterministic same-drain witness touch — a production seam, or an internal
  exposure? → **A: Expose `read_first_frame_bounded` via a detail header** for direct unit testing.
  No production branch, no test hook.
- Q: How much witness coverage do the three transport timer sites owe?
  → **A: One pin per site — all three**, including the harder TLS-handshake staging.

### Session 2026-08-04 (Gate A round 1)

> Three residuals surfaced by the Gate A round-1 reviews that `/clarify` did not reach. **No locked
> decision is reopened** — Q1's both-legs invariant, Q2's join, Q3's class-fix and C1's
> `max_bytes + 1` clamp all survive intact; these settle questions those decisions handed to the
> design and that the first draft answered by omission. Each answer is integrated into the FR/SC
> bodies above, not only recorded here.

- Q: SC-016 requires every same-drain ordering to be *constructed*, but the transport connect and
  handshake timers are coroutine-frame locals with no injection seam and their competing completion
  is a socket event, not a timer — so what discharges SC-014?
  → **A (G-1): narrow SC-014 to the retirement property, scope SC-016 to the session layer, and file
  the residual.** Each of the three sites keeps its own cell (C4 honoured literally), witnessing that
  the attempt's epoch is retired before the operation returns; the same-drain *ordering* witness is
  recorded as unachievable in this tree and filed as a follow-up issue at close-out. Integrated into
  FR-014, SC-014, SC-016 and US4's Independent Test.
- Q: SC-006 named a live-session postcondition — read pump not cancelled, subsequent frame processed —
  that the C3 direct-helper target cannot express, and whose pre-fix stimulus is inert there because
  `mock_transport::cancel()` is a no-op. Restate it, or move it to an accept-loop witness?
  → **A (G-2): restate it structurally.** The asserted postcondition becomes contract S5 — *no handler
  armed by the call is outstanding on return* — observed as zero `cancel()` calls on the transport
  after the call returns and the context is drained. The live-session teardown stays in the spec as
  the consequence that makes the defect sharp, and the end-to-end path is inherited from PR #232's
  coverage, since 088 changes nothing downstream of the helper. Integrated into SC-006, US2
  acceptance scenario 2, and SC-001/SC-012's narrowing.
- Q: Raising the framer's carry capacity to `max_bytes + 1` makes the carry-overflow leg of the
  framer's own `wire_frame_too_large` unreachable on this path. Is that acceptable, given the spec
  requires that rejection to stay *distinct* from the accept-path budget?
  → **A (G-3): yes, and it must be stated as a split rather than a removal.** The framer's rejection
  keeps its `parse_frame` sub-cause (a declared BodyLength above `Framer::cfg_.max_frame_bytes`),
  which remains reachable inside 4097 buffered bytes; only the carry-overflow sub-cause goes away, and
  its disappearance is exactly what makes FR-007's single budget decision point reachable. Integrated
  into FR-013, FR-007, the framer edge case, and the contract's F1/F2 rows.

### Session 2026-08-05 (Gate A round 2)

> One residual, and it is the round-2 blocker. **Q2's decision is not reopened** — the join stands —
> but the source refutes the argument that made it safe, and the unamended design would have shipped a
> regression *worse than the defect it fixes*. The answer is integrated into FR-005/FR-006/FR-014/
> FR-015, a new **FR-018**, a new **SC-018**, Q2's own block, Q3's census, and the plan's Article XI §2
> and VIII §3 rows — not only recorded here.

- Q: Q2's feasibility argument ends *"the forwarded signal is accepted and reaches the underlying
  operation, which returns `operation_aborted` → `transport_read_cancelled`"*. On the **TLS** transport
  that is false at the last hop: the `total` is accepted and re-emitted by the transport coroutine's
  own filter, then silently dropped inside asio's SSL composed operation, whose cancellation state is
  **terminal-only**. So under `Engine::stop()` the read arm never retires, the group never completes,
  and `stop()`'s step-3 join — an unbounded spin with no deadline (`src/session/engine.cpp:1342-1353`)
  — never terminates. Does Q2 flip to (a′), or is the join amended?
  → **A (G-4): the join is retained and amended to (b+).** `asio_tls_transport::async_read_some` must
  install the **two-argument** `reset_cancellation_state(enable_total_cancellation(), <OUT filter
  mapping any non-`none` cancellation to `terminal`>)`, mirroring the same file's `async_connect`
  precedent at `src/transport/asio_tls_transport.cpp:918-933`. The remedy **cannot** live on the
  engine side: `this_coro::reset_cancellation_state` *replaces* the single bottom-frame cancellation
  state (`asio/impl/awaitable.hpp:726-732`), so the transport's own first-statement reset
  (`asio_tls_transport.cpp:1134`) clobbers anything an arm wrapper installs above it — last reset
  wins. (b+) also makes `stop()` **prompt** on TLS, which the pre-fix code — bounded only by the 5 s
  deadline — never was. New **FR-018** carries the obligation; new **SC-018** carries a **real-TLS**
  witness, because every mock-driven cell is structurally blind to this defect (the mock's read is a
  `steady_timer`, which honours `total`). Integrated into FR-005, FR-006, FR-014, FR-015, FR-018,
  SC-018, Q2, Q3's census and `plan.md`'s Constitution Check.

### Q1 — Which invariant does the budget fix deliver? → **BOTH legs: frame-before-budget AND `>`**

**Context**: FR-001 / FR-002 / FR-013 / Edge case "cumulative size `max_bytes + 1`, complete frame
present".

**Decision.** Each newly-read chunk is framed **first**; a complete first frame returns immediately
and wins over the budget unconditionally. Only when no complete frame is extractable is the budget
applied, and then with a **strict `>`**. The delivered invariant is: *reject only when no complete
frame is extractable from the bytes read, and the peer has exceeded the budget.*

**Rejected — (a) comparison-only (`>=` → `>`).** Fixes exactly the `== max_bytes` instance. A
cumulative read of `max_bytes + 1` that *already contains a complete Logon* is still discarded, so
the second manifestation named in issue #233 (Logon(3500) + 597 bytes of the next frame) stays live.
It is the smallest diff and it leaves the defect class intact.

**Rejected — (c) frame-before-budget while keeping `>=`.** Honours a complete frame, but still closes
a peer sitting at exactly `max_bytes` with nothing complete yet — which is the precise wording
(`"exceeds"` / `"more than"`) this feature exists to satisfy. Half a correction.

**Consequence the decision carries.** Deferring the budget decision until after framing would, on
its own, raise the worst-case buffered size from `max_bytes` to `max_bytes + <one read-buffer>` —
4096 → 8192 with today's constants. **C1 removes that consequence** by clamping the read: see below.
**FR-013 holds the resulting bound**; if the design cannot keep it constant and single-shot, this
decision must be revisited rather than quietly relaxed.

---

### C1 — Is the widened DoS bound acceptable? → **No: clamp the read to `max_bytes + 1`**

**Context**: FR-013 / SC-013 / Edge case "the chunk that pushes cumulative past the budget".

**Decision.** Each read requests at most `max_bytes + 1 - buf.size()` bytes rather than a fixed
`read_buf.size()`. Worst-case buffered size becomes **`max_bytes + 1` (4097)** — *tighter* than the
`max_bytes + <read-buffer>` that Q1's reordering would otherwise imply, and tighter than the 8192 a
naive frame-before-budget would allow. The frame-wins rule is unaffected: any first frame of size
`≤ max_bytes` is wholly contained within the clamped bytes, so it is still detectable; a first frame
*larger* than `max_bytes` is precisely what the budget exists to reject.

**Rejected — accept `max_bytes + <one read-buffer>` (8192).** Simplest diff, and still a hard
single-shot constant. But it doubles the buffer a hostile peer can cause on a window that
deliberately precedes any `Session`-level limit, when a strictly tighter option was available at the
cost of one `min()`.

**Rejected — clamp *and* resize `read_buf` to `max_bytes + 1`.** Tightest and most self-consistent,
but it couples the read-buffer size to the budget constant — a coupling nothing needs, on a line
neither defect implicates.

**Carried obligation.** The clamp is an off-by-one surface. `max_bytes + 1 - buf.size()` must be
proven never to underflow (it is evaluated only when `buf.size() <= max_bytes`, which the strict-`>`
reject guarantees, but the guarantee must be *shown* rather than assumed) and the boundary pins must
cover `buf.size() == max_bytes` on entry to the clamp.

> **Second carried obligation, added at Gate A round 1 — and it is the one the first draft missed.**
> Raising the cumulative bound to `max_bytes + 1` **also raises the requirement on every buffer this
> path routes bytes through**. The framer's carry buffer (`engine.cpp:402`) is built at `max_bytes`,
> and `Framer::feed` appends into it *before any parse* — so at that capacity the very case this
> decision exists to admit is rejected one step earlier, by the framer, with no parse attempted.
> **C1's decision is unchanged and correct**; what it carries is that the carry capacity must follow
> it to `max_bytes + 1`, stated in the delivered code as a *derived* value. FR-013 now names this;
> research §D-1a proves sufficiency and re-traces the boundary cell.

### C2 — Does the joined form owe a direct `Engine::stop()` pin? → **Yes**

**Context**: FR-004 / FR-015 / Edge case "Engine `stop()` during the first-frame read".

**Decision.** A dedicated regression pin: `Engine::stop()` is called while a first-frame read is in
flight, and the test asserts prompt abort, accept-slot reclaim, and clean teardown under the
sanitizer matrix.

**Rejected — rely on existing engine-stop coverage.** The engine already has stop/teardown tests, but
they were written against the pre-`||` shape. If none of them happens to have a first-frame read in
flight at `stop()` time, the coverage is nominal and a regression in exactly the construct this
feature introduces would land silently. The joined form is *the* construct that could create an arm
outliving a stop; Article XVII §1 makes cancellation the Gate-A trigger surface; and a named safety
invariant needs a direct pin, not an incidental one
([[feedback_named_safety_invariant_needs_direct_pin]]).

**Carried obligation.** The pin must be shown to be meaningful rather than vacuously green — a
`stop()` test that never actually catches a read in flight asserts nothing
([[feedback_ci_gate_observes_not_asserts_witness_skips_into_green]]).

### C3 — What may the same-drain witness touch? → **Expose `read_first_frame_bounded` to tests**

**Context**: FR-016 / SC-005 / SC-006 / Assumption "the same-drain race will not reproduce by chance".

**Decision.** Move `read_first_frame_bounded`'s declaration into a `detail` header so tests can drive
it directly against a mock transport with a small deadline and a hand-driven `io_context` — both
completions ready before the drain, so the ordering under test is *constructed*, not awaited. Direct
precedent in this same file: `scan_first_frame_ids` was moved out of the anonymous namespace into
`scan_first_frame_ids.hpp` for exactly this reason (040 US2 Phase 4, noted at `engine.cpp:360-362`).

**Rejected — a `FIXPP_TEST_HOOKS`-gated seam in the accept path.** Precedent exists
(`test_hook_pre_publish_`, `engine.cpp:926-932`), and it would keep the witness end-to-end. But it
adds a production-compiled branch, and the hook would have to sit at exactly the drain boundary under
test — more delicate than the exposure, for a weaker guarantee.

**Rejected — end-to-end only against the live 5000 ms deadline.** No new surface, but a read
completion cannot be reliably steered into the same drain as a 5000 ms expiry. The witness would be
probabilistic, and a race a test merely fails to observe is not evidence
([[feedback_overlap_witness_needs_stimulus_held_until_witnessed]]).

**Where it lives — settled, not left to `/plan`.** `scan_first_frame_ids.hpp` sits at
`src/session/scan_first_frame_ids.hpp`, inside `src/`, not under `include/`. Following that precedent
puts the new header outside the install set *by construction*, so FR-012's empty-public-surface claim
and SC-017 hold with no install-list change. Putting it under `include/fixpp/session/detail/` would
work too but would make SC-017 real work instead of a tautology — take the precedent.

### C4 — How much witness coverage do the transport sites owe? → **One pin per site, all three**

**Context**: FR-014 / SC-014 / User Story 4.

**Decision.** Each of `asio_plain_transport.cpp:130` (connect), `asio_tls_transport.cpp:910`
(connect), and `asio_tls_transport.cpp:1032` (TLS handshake) gets its own same-drain witness. This is
the only reading under which SC-014 is literally true as written.

**Rejected — pin both connect sites, source-verify the handshake site.** Cheaper, and the
shape-equivalence argument is genuinely strong. But it turns SC-014 into a waiver-shaped item at
Gate B, and this project's record is that the "not reachable / equivalent by inspection" leg has to
be source-verified to a standard that costs about as much as the test
([[feedback_waiver_not_reachable_leg_must_be_source_verified]]).

**Rejected — one representative pin plus equivalence for the other two.** Fixing four sites and
witnessing one is the class-fix-scoped-to-one-occurrence pattern that the Q3 decision widened scope
specifically to avoid.

**Cost accepted.** The TLS-handshake case needs a peer driven to a controlled point; it is the most
expensive single test in this feature.

> **Amended at Gate A round 1 (clarification G-1) — the COUNT stands, the WITNESSED PROPERTY narrows.**
> C4's decision was *one pin per site, all three*, and that is delivered unchanged. What could not be
> delivered is the *same-drain* shape those pins were written against: both timers are coroutine-frame
> locals with no injection seam, and — unlike the session-layer cells, where both competing
> completions are timers and asio's expiry-ordered timer queue decides — the competing completion here
> is a **socket** event from the reactor, whose order relative to an expired timer inside one `poll()`
> is unspecified asio scheduler internals. Each cell therefore witnesses that **the attempt's epoch is
> retired before the operation returns**, which is deterministic, needs no ordering control, and is
> what makes the guard's decision correct at every site. The same-drain integration witness is
> recorded as **not achievable in this tree** and is filed as a follow-up issue at close-out. This is
> the outcome C4's own "cost accepted" clause anticipated being tested against; it is stated here
> rather than left as prose to be discovered unimplementable at Gate B.

### Q2 — Which remedy shape closes Defect 2? → **(b+) joined race via `||`, plus a transport-side OUT-mapping reset**

**Context**: FR-005 / FR-006 / FR-014 / FR-015 / **FR-018**.

> **Amended at Gate A round 2 (clarification G-4) — the DECISION stands, one of its three feasibility
> legs is FALSE and the design gains an obligation.** The joined race is retained unchanged. What the
> round-2 source read established is that the join, *as specified*, is not correct under
> `Engine::stop()` on the **TLS** transport — which is the default accept path — and that no
> engine-side wrapper can make it so. The remedy is one additional line, in the transport, of a form
> this same file already ships: **FR-018**. Q2 is therefore **(b+) = (b) + FR-018**. The superseded
> feasibility sentence is kept below and marked, per this bundle's convention.

**Decision.** Join the read and the deadline wait with
`asio::experimental::awaitable_operators`' `||`, precedented in this codebase at
`src/session/session.cpp:1468,1474`. The coroutine cannot leave scope until both operations have
retired, so **no handler can be stranded at all** — which closes the write-to-freed leg and the
spurious-teardown leg with a *single* mechanism, rather than patching each.

**Rejected — (a) shared-owned state captured by value, alone.** Kills the dangle but leaves the
sharper leg entirely: `transport.cancel()` still fires on a transport already moved into a live
`Session` at `engine.cpp:922`, tearing down a session that just established. This is exactly what
#232's Gate B triage called out as the sharper consequence.

**Rejected — (a′) shared state plus an explicit "retired" suppression flag.** Closes both legs, but
with **two** mechanisms instead of one, and the suppression flag inherits its own same-drain
ordering question (when is `retired` observed relative to the queued handler?) — i.e. it replaces the
bug with a smaller instance of the same reasoning burden.

> **Re-weighed at Gate A round 2, and still rejected — but for a sharper reason than first stated.**
> (a′) is the only defensible replacement if Q2 had to flip, and it has one property (b) lacked: it
> keeps the cancellation-immune raw timer, so it **does not hang** under `stop()` — the pre-fix 5 s
> escape survives. What it forfeits is FR-015's word *"promptly"*: it leaves `Engine::stop()` on a TLS
> first-frame read bounded only by the 5 s deadline, i.e. it **enshrines** the pre-fix latency instead
> of removing it. (b+) closes both legs with one mechanism *and* makes `stop()` prompt on both
> transports, at the cost of one line in a file that already ships the identical idiom
> (`asio_tls_transport.cpp:918-933`). Flipping to (a′) would also require a `/speckit-clarify` re-run,
> since the locked decision would change. **(b+) is strictly better on every axis Q2 originally
> weighed**, so the decision is retained and amended rather than replaced.

**Feasibility verified against the pinned asio (1.38.0), not assumed.** The mechanism was checked in
the shipped headers before this decision was locked:

- `operator||` is `make_parallel_group(co_spawn(…), co_spawn(…)).async_wait(wait_for_one_success(), deferred)`
  (`asio/experimental/awaitable_operators.hpp:258-264`). `parallel_group::async_wait` completes only
  when **every** operation has finished — so the joined-lifetime property FR-005 needs is
  *structural*, not incidental.
- `wait_for_one_success()` defaults to `cancellation_type::all`
  (`asio/experimental/cancellation_condition.hpp:67-68`), which includes `terminal`.
- The losing arm honours it: `ssl::stream::async_read_some` documents cancellation support for
  `terminal` and `partial` (`asio/ssl/stream.hpp:843-849`), and `basic_stream_socket::async_read_some`
  supports `terminal`/`partial`/`total`. Both `asio_plain_transport::async_read_some`
  (`src/transport/asio_plain_transport.cpp:191-197`) and `asio_tls_transport::async_read_some`
  (`src/transport/asio_tls_transport.cpp:1129-1134`) open with
  `reset_cancellation_state(enable_total_cancellation())`, so the forwarded signal is accepted and
  reaches the underlying operation, which returns `operation_aborted` →
  `transport_read_cancelled`.

> **SUPERSEDED at Gate A round 2 — the third bullet is FALSE at its last hop, on TLS.** The first two
> bullets are re-verified and stand. The third is true of the **plain** transport and false of the
> **TLS** one, and TLS is the default accept path. Where the bundle and the source disagree, the
> source wins; the corrected chain, link by link (asio 1.38.0, `~/.conan2/p/asio6e6c781a0fee4/p/include/asio/`):
>
> 1. `Engine::stop()` emits `cancellation_type::total` — for a started session via `co_spawn` **onto
>    the session strand** (`src/session/engine.cpp:1285-1298`), and to the accept-scope signals at
>    `:1300`.
> 2. `parallel_group`'s external-cancel handler forwards **that exact type** to every arm, **once** —
>    the shared `cancellations_requested_++ == 0` guard
>    (`asio/experimental/impl/parallel_group.hpp:344-354`).
> 3. The transport wrapper **accepts and re-emits `total` unchanged**. `asio::cancellation_filter` is
>    a **MASK, not a map** — `return type & Mask` (`asio/cancellation_state.hpp:31-39`) — and the
>    one-argument `this_coro::reset_cancellation_state(F)` sets **BOTH** the IN and the OUT filter to
>    `F` (`asio/cancellation_state.hpp:121-126` → `impl<Filter, Filter>(filter, filter)`; the state's
>    slot handler is `cancelled_ = in_filter_(in); out = out_filter_(cancelled_); emit(out)` at
>    `:216-223`). So `total` in ⇒ `total` out.
> 4. **The `total` then dies inside asio's own SSL composed operation.**
>    `ssl::stream::async_read_some` is composed as `ssl::detail::io_op`, which derives from
>    `base_from_cancellation_state<Handler>` and is constructed with the **no-filter** overload
>    (`asio/ssl/detail/io.hpp:100-106` → `asio/detail/base_from_cancellation_state.hpp:44-48`). That
>    overload's `cancellation_state(slot)` ctor is documented and implemented as **terminal-only** —
>    *"Initialises the cancellation state so that it allows terminal cancellation only"*
>    (`asio/cancellation_state.hpp:88-100`, emplacing `impl<>` whose defaults are
>    `InFilter = enable_terminal_cancellation`, `:199-201`). `total & terminal = none`: nothing is
>    recorded, and **nothing is forwarded to the socket operation the SSL engine is pending on**. The
>    doc lines Q2 already cites (`asio/ssl/stream.hpp:843-849` — `terminal` and `partial` only) are
>    the documented surface of exactly this mechanism.
> 5. So the read arm never completes ⇒ `outstanding_` never reaches 0
>    (`parallel_group.hpp:229-231`) ⇒ the group never completes ⇒ `read_first_frame_bounded` never
>    returns ⇒ `stop()`'s step-3 join — **an unbounded spin with no deadline**
>    (`src/session/engine.cpp:1342-1353`) — never terminates.
>
> **Aggravation: unamended, the fix is strictly WORSE than the defect on the default path.** `stop()`'s
> `total` also **retires the deadline arm** — a `steady_timer`'s wait op *does* honour `total`
> (`asio/detail/deadline_timer_service.hpp:315-320`) — and consumes the group's one-shot cancel guard
> (step 2 above), so the deadline arm's own completion can no longer emit a second cancel to the read
> arm. Pre-fix, the stall is bounded at 5 s because the raw timer lambda (`engine.cpp:394-399`) has no
> associated cancellation slot, is therefore immune to `stop()`, fires on expiry and calls
> `transport.cancel()` → `socket_.cancel()` (`asio_tls_transport.cpp:1247-1255`), which aborts the
> socket op the SSL engine is pending on. **The join destroys that escape.** This directly contradicts
> FR-015 and the Edge Case *"Engine `stop()` during the first-frame read"*, both of which this bundle
> already carries.
>
> **The repo knew this before 088 was written, and the bundle did not cite it.**
> `src/session/engine.cpp:1302-1310` states outright: *"An established session's read-pump is blocked
> in async_read_some with no peer EOF; **total-cancel alone does not break the in-flight SSL read**
> (see BIO_ctrl crash in `[[project_business_roundtrip_bio_ctrl_segv]]`). The socket MUST be closed to
> wake the read-pump."* That is this crux, stated in production source, on this feature's own file.
>
> **Plain transport is NOT exposed.** The TCP read op's cancel handler honours
> `terminal|partial|total` explicitly (`asio/detail/reactive_socket_service_base.hpp:716-725`), so on
> a plain accept path `stop()`'s `total` aborts the read arm and the join retires. **The hang is
> TLS-only — i.e. the default.** Asymmetric treatment (map on TLS, none on plain) is therefore
> principled, and matches how the connect path already differs per transport.
>
> **An engine-side remedy is structurally impossible.** `this_coro::reset_cancellation_state`
> **replaces** the single bottom-frame cancellation state
> (`asio/impl/awaitable.hpp:726-732` — `bottom_of_stack_.frame_->cancellation_state_ = …`); there is
> one state per awaitable thread and **the last reset wins**. The transport's own first-statement
> reset (`asio_tls_transport.cpp:1134`) therefore clobbers any OUT-mapping reset an arm wrapper
> installed at the call site. So *"wrap the read arm inside `read_first_frame_bounded`"* is **not a
> live option**; the obligation belongs to the transport, and that is **FR-018**.
>
> **What the amendment costs, and what it buys.** With the OUT map, the wrapper re-emits `terminal`,
> the io_op's terminal-only inner state records it and forwards it, the socket op honours it
> (`reactive_socket_service_base.hpp:716-725`), and the read returns `operation_aborted` →
> `transport_read_cancelled`. `stop()` then aborts a TLS first-frame read **promptly** — which the
> pre-fix code, with its 5 s tail, never did. Measured against FR-015's own word *"promptly"*, (b+) is
> a **strict improvement**, not a restoration. Mapping to `terminal` rather than passing `total` is
> also the honest semantics: SSL does not support `total` precisely because a mid-record TLS read
> cannot be abandoned side-effect-free, and every current canceller on this path (the first-frame
> deadline, `stop()`) closes the transport afterwards.

**Caveat `/plan` must handle, found in the same read.** `wait_for_one_success`'s disposition overload
returns `cancellation_type::none` when the winning arm completed with an **error**
(`cancellation_condition.hpp:87-91`). The transports return `expected_t` rather than throwing, so the
normal read-failure path still cancels the timer — but if the read arm's coroutine *throws*, the
timer arm is not cancelled and the group waits out the full deadline before retiring. Bounded (5 s),
not unbounded, but it is a latency path that does not exist today and the plan must state whether it
is reachable.

> **Handled at Gate A round 1, and the answer was not the one the first draft gave.** The read arm
> indeed cannot throw. **The deadline arm, as first designed, threw on every successfully established
> connection** — a bare `co_await t.async_wait(asio::use_awaitable)` throws when the join cancels it.
> The consequence was benign for correctness but real for cost and for the claim itself. Resolved by
> one line: the deadline arm uses `asio::redirect_error(asio::use_awaitable, ec)`, which also makes
> the join's `outcome.index()` a sound "which arm won" discriminator — without it, a throw on the read
> arm re-labels the winner and surfaces as `transport_handshake_timeout`. See research §D-2/§D-3.

**Constraint carried forward.** The joined form must remain correct under `Engine::stop()`'s
`cancellation_type::total` (see Edge Cases and FR-015) — the composition must not create an operation
that outlives a stop. `/plan` must show this, not assert it.

> **Discharged at Gate A round 2, and it did NOT hold as specified.** This is the constraint that
> failed. On TLS the joined form as written creates exactly the operation this clause forbids — one
> that outlives a stop, unboundedly (see the superseded block above). **The constraint is now
> discharged by FR-018**, not by the join alone: with the transport-side OUT map the read arm retires
> on `stop()`'s `total`, the group completes, and the composition creates no operation that survives
> a stop on either transport. The clause is restated in FR-015 in its post-amendment form, and
> **SC-018**'s real-TLS witness is what shows it rather than asserts it.

### Q3 — Does the census scope include the three transport sites? → **Fix all four (class-fix)**

**Context**: FR-009 / FR-014 / User Story 4.

**Decision.** `src/session/engine.cpp:394`, `src/transport/asio_tls_transport.cpp:910`,
`src/transport/asio_tls_transport.cpp:1032`, and `src/transport/asio_plain_transport.cpp:130` are all
fixed here. The late-cancel-on-a-successful-connect/handshake leg applies to every one of them.

> **Factual correction, Gate A round 1 — the decision stands, its supporting sentence does not.** This
> paragraph originally read *"The three transport sites capture `this`, so they carry no dangle
> leg"*. **That is false**: `reconnect_fsm.cpp:250-252` and `:284-286`, and `engine.cpp:841-844`, all
> destroy the transport synchronously on the failure arm, so a completed handler can run against a
> destroyed object. See the Census correction above and research §D-4.0. Q3's **decision** — fix all
> four sites — is unaffected and is *strengthened* by the correction; only the reasoning about how
> severe the transport sites are changes, and with it the mechanism chosen for them (research §D-4.1).

**Rejected — fix `engine.cpp` only, file a follow-up.** Narrowest Gate A surface and fastest to
merge, but it ships a spec whose own census table lists three sites as knowingly unfixed. A
class-fix scoped to one occurrence when the census found four is the recorded anti-pattern in this
project ([[feedback_census_all_handrolled_scanners_before_scoping_parse_fix]]).

**Rejected — fix `engine.cpp` + the two TLS sites, defer the plain transport.** Splits on issue
#233's boundary rather than on any technical one. `asio_plain_transport.cpp:130`'s own comment says
it *"mirrors TLS transport"*; fixing the original and not the mirror is the least defensible of the
three splits.

**Cost accepted.** This widens the Gate A surface from the session layer to the transport layer and
adds US4's per-site pins for connect and handshake on both transports.

> **Blast radius updated at Gate A round 2 — a FIFTH site, outside this census, now carries work.**
> Q3's census locks a class-fix across four `timer.async_wait([…])` sites. Q2's amendment to (b+)
> adds a fifth **touched** location of a different shape: the one-argument
> `reset_cancellation_state` at `src/transport/asio_tls_transport.cpp:1134`, inside
> `asio_tls_transport::async_read_some` (FR-018). Stated plainly, because Q3's census is what SC-008
> checks the diff against and an unannounced fifth file-touch is exactly the drift Q3 exists to
> prevent:
>
> | | Site | Shape | In the four-site census? | Witness |
> |---|---|---|---|---|
> | 1 | `src/session/engine.cpp:394` | stranded timer handler | ✔ | T1 (+ T2a/T2b) |
> | 2 | `src/transport/asio_tls_transport.cpp:910` | stranded timer handler | ✔ | T4 |
> | 3 | `src/transport/asio_tls_transport.cpp:1032` | stranded timer handler | ✔ | T5 |
> | 4 | `src/transport/asio_plain_transport.cpp:130` | stranded timer handler | ✔ | T3 |
> | **5** | **`src/transport/asio_tls_transport.cpp:1134`** | **one-arg cancellation reset → two-arg with an OUT map** | ✘ — **added at round 2 (FR-018)** | **T6 (real TLS — SC-018)** |
>
> **Site 5 is shared with the session read pump, and that is the whole of its blast radius.**
> `Transport::async_read_some` has exactly two production callers — `engine.cpp:413` (this feature's
> accept path) and `engine.cpp:542` (`run_read_pump`) — enumerated in the Census section above. That
> is the complete caller set, so the consequence of the change is bounded to those two paths. The
> live-session path raises the INV-4a / BIO-serialization question, which is dispositioned **SAFE BY
> STRAND CONSTRUCTION** in research §D-2a with its evidence; no new pin is required beyond the
> standard Gate B TSan/teardown re-run. **Q3's decision — fix all four — is unchanged**; the fifth
> site is an addition, not a re-scoping.

---

## Normative References

*Added at Gate A round 1. Its absence was a direct `[const §VI.5]` violation, and the same violation
was corrected at this same gate on feature 085. Three of the last four bundles carry this section
(083 ✔, 084 ✘, 085 ✔, 086 ✔), so it is enforced practice, not a dead letter.*

Per `[const §VI.5]` (`.specify/constitution.md:164`) — *"Every `/specify` artifact must include a
**Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the
coverage index that inform the spec."* **This feature introduces no OFFICIAL catalogue rows** — it
corrects the implementation of an already-shipped requirement and changes nothing about message
semantics, encoding or validation, so `[const §VI.4]`'s coverage-index obligation is not triggered.

**Governing FIX section** *(added at Gate A round 3. The section previously recorded the FIX set as
**empty**. That reading is defensible — 088 adds no FIX semantics, and §5 is a **presence**
obligation which the section already met — but it left the connection-establishment window this
feature bounds with no traceable anchor. That is a traceability gap rather than a §5 violation, and
closing it costs one line.)*:

- **`[FIX-SL §4.3] Establishing a FIX connection`** — `.specify/architecture.md:744`;
  `spec/coverage-index.md:43`, covered **Y** by **S-001, S-015, S-021, S-022**. This is the section
  the first-frame window belongs to: 088 corrects *when and how* the acceptor admits the first Logon
  during connection establishment. It **adds no new catalogue row and changes no covered behaviour** —
  §4.3's FIX-level semantics are already delivered by the S-001 family; what this feature corrects is
  the implementation of feature 015's bounded pre-session window sitting underneath them. Recorded
  for traceability, **not** as new coverage.

**Inherited implementation requirement** (this is what the feature corrects code *to*, and until now
it reached the reader only through the Context table):

- **`specs/015-runtime-engine/spec.md` FR-014** (`:126`) — *"A peer that **exceeds** either … MUST be
  closed and its accept slot reclaimed, without affecting other peers."* The word *exceeds* is the
  whole of Defect 1's first leg.
- **`specs/015-runtime-engine/spec.md` SC-011** (`:150`) — *"…sends **more than** the first-frame byte
  budget before a valid Logon…"* The independent second anchor for the same boundary.

**Constitutional authorities:**

- **`[const §XI.2]`** (`.specify/constitution.md:232`) — *"Cancellation: ASIO native cancellation
  slots end-to-end."* The article that makes the joined form (Q2/FR-005) the shape this project
  expects, and that D-2's filter trap sits under. **It is also the article FR-018 discharges**: at
  Gate A round 2 the end-to-end slot chain was found to be broken at its last hop on TLS — accepted by
  the transport coroutine, dropped by the SSL composed op — so "end-to-end" was true of the plumbing
  and false of the effect. FR-018 restores it. `plan.md`'s XI §2 row records the re-pass.
- **`[const §XI.6]`** (`.specify/constitution.md:236`) — *"Coroutine frame allocation: HALO-first.
  PMR fallback per-awaiter where HALO doesn't fire."* Engaged by the two un-HALO-able `co_spawn` arms
  the join creates; dispositioned in `plan.md`'s Constitution Check and research §D-7.
- **`[const §XVII.1]`** (`.specify/constitution.md:332-338`) — Gate A is mandatory for anything that
  *"Touches concurrency / threading / cancellation / executor model"*. This feature does, on the
  accept path, at four sites.
- **`[const §IX.1]`** (`.specify/constitution.md:196-200`) — the coverage gate's **binding rule**: no
  uncovered error/edge path without an explicit recorded assessment; the percentage is the target.
  Discharged per research §D-6.9 in the verify record.
- **`[const §VI.5]`** (`.specify/constitution.md:164`) — this section's own authority.
- **`[const §VII.8]`** (`.specify/constitution.md:178`) — *"Tests are selected by `ctest -L <label>`,
  never `-R <exe-name>`."* The `088` labels for every new target are named in research §D-5.

**Architectural authorities:**

- **`[arch §4.4] session`** (`.specify/architecture.md:237-263`) — the session/accept-path concurrency
  model this feature's remedy must remain inside.
- **`[arch §4.5] transport`** (`.specify/architecture.md:264-…`) — the transport concurrency and
  cancellation contract governing the three timer sites and their strand confinement.

**Prior-feature provenance** (analysis, not normative):
`research/G19-fix-fpml-iso20022/research/reviews/codex_pr232_review.md` (F-5) and
`research/G19-fix-fpml-iso20022/research/reviews/opus_pr232_1_triage.md` (F-5 dispositioned; F-8 is
where the timer-lifetime defect was newly found and argued).
