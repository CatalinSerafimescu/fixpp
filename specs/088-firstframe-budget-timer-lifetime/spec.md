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

**Note:** the loop-top check at `:408-411` is **unreachable dead code**. The sole caller passes an
empty `buf` (`:857-858`), so the first iteration cannot trip it, and `:426` returns before control
can come back round. Whatever remedy is chosen must dispose of this twin rather than editing both
copies to stay consistent.

### Defect 2 — the deadline timer handler can outlive the coroutine frame it captures

`src/session/engine.cpp:388-399`:

```cpp
bool timed_out = false;                                    // coroutine-frame local
timer.async_wait([&timed_out, &transport](const std::error_code& ec) {
    if (!ec) { timed_out = true; transport.cancel(); }
});
```

Every return path calls `timer.cancel()` (`:409`, `:420`, `:427`, `:439`, `:450`), but **`cancel()` cannot
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
| `src/transport/asio_tls_transport.cpp:910` (connect timeout) | `this` | no | **YES** — late `socket_.cancel()` after a successful connect |
| `src/transport/asio_tls_transport.cpp:1032` (TLS handshake timeout) | `this` | no | **YES** — late `socket_.cancel()` after a successful handshake |
| `src/transport/asio_plain_transport.cpp:130` (connect timeout, "mirrors TLS transport") | `this` | no | **YES** — same shape; **NOT named in issue #233** |

All remaining `async_wait` uses in `src/` are `co_await`ed — `engine.cpp:1350`, `engine.cpp:1366`,
`session.cpp:1475`, `reconnect_fsm.cpp:130`, `system_clock_source.cpp:239` — so the awaiter cannot
leave scope while the wait is outstanding and no handler can be stranded. They are **out of scope,
and that exclusion is load-bearing**: it is the reason this feature is not an engine-wide
cancellation refactor.

**All four are in scope for this feature** (Clarifications Q3 — class-fix).

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
2. **Given** the same scenario, **When** the transport has been moved into a live `Session`,
   **Then** that session's transport is NOT cancelled and its read pump continues.
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
late-handler shape. They capture `this`, so nothing dangles, but a handler that completed in the
same drain as a *successful* connect or handshake still calls `socket_.cancel()` afterwards,
aborting the first legitimate operation on a healthy socket.

**Why this priority**: lower than US1–US3 — no memory-safety leg, and the window is narrower — but
it is the same root cause, and a class-fix scoped to a single occurrence is the pattern that returns
at Gate B.

**Independent Test**: same-drain expiry/completion selection against each transport, asserting the
first post-connect (resp. post-handshake) operation is not cancelled.

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
- **A frame fragmented across several reads that completes exactly at the budget** — the stateful
  framer carries unconsumed bytes across `feed` calls (`:432-437`); the boundary must be evaluated
  against the same accumulation the existing fragmentation fix (F-015-001) established, not against
  a single chunk.
- **The framer itself reports `wire_frame_too_large`** (`:438-442`) — a frame larger than the carry
  capacity. This is a distinct rejection from the accept-path budget and must remain distinct.
- **Deadline expires *between* reads with no read outstanding** — must still return
  `transport_handshake_timeout` (`:454`), unchanged.
- **Engine `stop()` during the first-frame read** — total cancellation must still abort the read
  promptly; the remedy for Defect 2 must not introduce an operation that survives `stop()`.
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
  sequence of peer writes. The clamp expression MUST be shown not to underflow.
- **FR-004**: The deadline behaviour MUST be preserved: a peer that stalls the pre-session window
  MUST still have its in-flight read aborted and its connection closed and reclaimed within the
  deadline. The remedy for FR-005 MUST NOT reintroduce the between-reads-flag-polling behaviour
  that 015's `/simplify` (Q-2) explicitly rejected.
- **FR-005**: No deadline-timer completion handler on the accept path MAY execute against state
  owned by a coroutine frame that has been destroyed. The first-frame read MUST NOT be able to
  return while a handler capable of touching its frame is still pending or queued. *(Q2 = joined
  race: the read and the deadline wait are joined so that neither can outlive the frame, rather than
  relying on `timer.cancel()` — which cannot un-queue an already-completed handler.)*
- **FR-006**: A deadline-timer completion handler MUST NOT cancel a transport that has already been
  handed to a live `Session`. A successfully established session MUST NOT be torn down by the expiry
  of the pre-session deadline that it beat.
- **FR-014**: The same joined-lifetime property MUST hold at the three transport timer sites: a
  connect-timeout or handshake-timeout expiry MUST NOT cancel a socket after its connect or handshake
  has already completed successfully. *(Q3 class-fix; these sites capture `this` so they have no
  dangle leg — the requirement is the late-cancel leg only.)* **Each of the three MUST carry its own
  same-drain witness** (C4); an equivalence-by-inspection argument does not discharge this.
- **FR-015**: `Engine::stop()` issued while a first-frame read is in flight MUST still abort that
  read promptly, reclaim the accept slot, and tear down cleanly under the sanitizer matrix — and this
  MUST be pinned by a **dedicated** regression test, not inferred from existing engine-stop coverage
  (C2). The joined form introduced by FR-005 MUST NOT create an operation that outlives a stop.
- **FR-016**: `read_first_frame_bounded` MUST be reachable from tests directly, via an **internal
  `detail` header** (C3), so the same-drain scenarios of FR-005/FR-006 can be constructed
  deterministically rather than awaited probabilistically. That header MUST NOT be installed and MUST
  NOT become part of the public surface (FR-012 still holds). No `FIXPP_TEST_HOOKS`-gated branch is
  added to the accept path for this purpose.
- **FR-007**: The unreachable duplicate budget check at `src/session/engine.cpp:408-411` MUST be
  eliminated rather than maintained in parallel with its live twin — the delivered code MUST have a
  single place where the budget decision is taken.
- **FR-008**: The production comments that state the delivered contract MUST be corrected in the
  same change — specifically the `:853` claim that 4096 bytes "covers any valid FIX Logon message"
  and the `:373-375` invariant list, both of which describe the pre-fix behaviour.
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

- **SC-001**: A peer whose first read delivers a complete, valid Logon at a cumulative size of
  exactly the first-frame byte budget establishes a session. *(RED against pre-fix source.)*
- **SC-002**: Surplus bytes coalesced behind that boundary-sized Logon reach the read pump; the
  reported first-frame length is the Logon's exact length, unchanged from the sub-budget case.
- **SC-003**: A peer that exceeds the byte budget without ever completing a frame is still closed
  with `wire_frame_too_large` and its accept slot reclaimed; the PR #232 over-budget witness passes
  unmodified.
- **SC-004**: A peer that sends nothing, or a partial frame, is still closed within the deadline and
  its accept slot reclaimed.
- **SC-005**: Under a deterministic same-drain selection of deadline expiry and read completion, the
  full sanitizer matrix reports **zero** findings — specifically no ASan use-after-free /
  stack-use-after-return attributable to the deadline handler, and no TSan race on the captured
  state. *(RED against pre-fix source under ASan.)*
- **SC-006**: Under that same scenario, a session that established before the expiry remains live —
  its read pump is not cancelled and it processes a subsequent inbound frame. *(RED against pre-fix
  source.)*
- **SC-007**: The delivered `read_first_frame_bounded` contains exactly one byte-budget decision
  point; the unreachable loop-top duplicate is gone.
- **SC-008**: All four enumerated timer-handler sites are fixed, and the delivered census in the
  artifacts matches the four in this spec (or records the difference). No enumerated site is silently
  left out.
- **SC-012**: A peer whose first read delivers a complete, valid Logon at a cumulative size **past**
  the byte budget establishes a session. *(This is the criterion the comparison-only fix would fail —
  it is what distinguishes the delivered invariant. RED against pre-fix source.)*
- **SC-013**: The worst-case buffered size for a pre-session connection is **`kFirstFrameMaxBytes + 1`**
  (4097) — demonstrably not exceeded by any sequence of peer writes, and strictly tighter than the
  `+ <one read-buffer>` that the reordering alone would have allowed. *(FR-013 / C1.)*
- **SC-014**: For **each** of the three transport timer sites — plain connect, TLS connect, TLS
  handshake — a connect or handshake that succeeds in the same event-loop drain as its timeout expiry
  is not followed by a cancellation of the socket; the first subsequent operation completes. Three
  witnesses, one per site. *(FR-014 / C4. RED against pre-fix source.)*
- **SC-015**: `Engine::stop()` during an in-flight first-frame read aborts promptly, reclaims the
  accept slot, and leaves the sanitizer matrix clean — asserted by a dedicated test that is shown to
  actually catch a read in flight rather than passing vacuously. *(FR-015 / C2.)*
- **SC-016**: The same-drain witnesses for SC-005/SC-006 are deterministic — the ordering under test
  is constructed by the test, not awaited. No witness in this feature depends on winning a timing
  race. *(FR-016 / C3.)*
- **SC-017**: The `detail` header added by FR-016 is not installed; the installed package's headers
  are byte-identical to `main`'s. *(FR-012 / FR-016 / C3.)*
- **SC-009**: The full sanitizer ctest matrix (ASan / UBSan / TSan) is green with 0 findings, and
  the `linux-clang-debug` local build gate is green.
- **SC-010**: The public surface delta is **empty** — no header change, no new or removed error
  code, no ABI change.
- **SC-011**: The production comments describing the first-frame contract match the delivered
  behaviour; the pre-fix claims at `:373-375` and `:853` no longer overstate it.

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
  so a late cancel on an already-closed socket is a no-op. This is what makes the "captures `this`"
  transport sites lower-severity — it does **not** make them harmless, because the damaging case is a
  late cancel on a socket that is *open and in use*.
- **The same-drain race will not reproduce by chance.** Both US2 and US4 need a deterministic
  scheduling seam, not a timing-tuned sleep. Constructing that seam is part of the work, and a test
  that merely fails to observe the race is not evidence. The seam is the FR-016 `detail`-header
  exposure plus a hand-driven `io_context` (C3), not a production hook.
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

### Session 2026-08-04

- Q: Which invariant does the budget fix deliver — comparison-only, frame-before-budget, or both?
  → **A: Both.** Frame each chunk first (a complete frame wins unconditionally), then apply the
  budget with a strict `>`.
- Q: Which remedy shape closes the timer-lifetime defect — shared-owned state, or a joined race?
  → **A: Joined race** via `asio::experimental::awaitable_operators`' `||`. One mechanism, both legs.
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

**Carried obligation.** The exposure is an internal `detail` header, not public API — FR-012's
empty-public-surface claim must remain true, and the plan must say where the header lives and that
it is not installed.

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

**Cost accepted.** The TLS-handshake same-drain case needs a peer that stalls the handshake to a
controlled point; it is the most expensive single test in this feature.

### Q2 — Which remedy shape closes Defect 2? → **(b) joined race via `||`**

**Context**: FR-005 / FR-006 / FR-014.

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

**Constraint carried forward.** The joined form must remain correct under `Engine::stop()`'s
`cancellation_type::total` (see Edge Cases) — the `||` composition must not create an operation that
outlives a stop. `/plan` must show this, not assert it.

### Q3 — Does the census scope include the three transport sites? → **Fix all four (class-fix)**

**Context**: FR-009 / FR-014 / User Story 4.

**Decision.** `src/session/engine.cpp:394`, `src/transport/asio_tls_transport.cpp:910`,
`src/transport/asio_tls_transport.cpp:1032`, and `src/transport/asio_plain_transport.cpp:130` are all
fixed here. The three transport sites capture `this`, so they carry no dangle leg — but the
late-cancel-on-a-successful-connect/handshake leg applies to every one of them.

**Rejected — fix `engine.cpp` only, file a follow-up.** Narrowest Gate A surface and fastest to
merge, but it ships a spec whose own census table lists three sites as knowingly unfixed. A
class-fix scoped to one occurrence when the census found four is the recorded anti-pattern in this
project ([[feedback_census_all_handrolled_scanners_before_scoping_parse_fix]]).

**Rejected — fix `engine.cpp` + the two TLS sites, defer the plain transport.** Splits on issue
#233's boundary rather than on any technical one. `asio_plain_transport.cpp:130`'s own comment says
it *"mirrors TLS transport"*; fixing the original and not the mirror is the least defensible of the
three splits.

**Cost accepted.** This widens the Gate A surface from the session layer to the transport layer and
adds US4's same-drain pins for connect and handshake on both transports.
