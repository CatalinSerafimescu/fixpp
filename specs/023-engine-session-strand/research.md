# Research: Per-Session Strand Binding for Engine-Managed Sessions

**Feature**: 023-engine-session-strand
**Date**: 2026-06-05
**Inputs**: spec.md (clarified), constitution Art. XI; the failure analysis from the
`business_messages_roundtrip` `BIO_ctrl` crash; feature 007-threading-clock
(`fixpp::core::session_executor`); the engine role-loop + `stop()` code.

## Problem restatement (grounded)

`asio::ssl::detail::engine::map_error_code` (asio `engine.ipp:247`) only touches the
SSL BIO (`BIO_wpending`, a `BIO_ctrl`) when the read completes with
`asio::error::eof` — i.e. a **peer FIN**. The UAF therefore requires an in-flight
inbound read to complete with `eof` while the SSL engine is being torn down/accessed
**concurrently**. Single-threaded, this cannot happen: `close()`'s `socket_.close()`
converts a pending read to `operation_aborted` first (no BIO access), and the engine
join (`Engine::stop()`) drains the read-pump before the transport is destroyed.

The race exists only because, on a **multi-threaded** `exec_`, `Engine::stop()`'s
`live_transport->close()` (engine.cpp ~714) runs on one thread while the read-pump's
in-flight read completion (which runs asio's ssl `io_op` BIO processing) runs on
another. `exec_` is a bare executor — not serialized — so the two overlap. The
`Session` *does* own a strand (default `threading_mode::per_session_strand`,
session_config.hpp:152) but the engine's **role loops and teardown do not run on
it** (loops are `co_spawn(exec_, …)` at engine.cpp:671/675; `stop()` closes on
`exec_`). This is the documented L-019-3 limitation ("multi-threaded io_context
unsupported this slice").

## Decisions

### D1 — One strand per engine-managed session, owned by the engine

**Decision**: The engine creates exactly one `asio::strand` per registered session
(`asio::make_strand(exec_)`), stored in `SessionEntry`, and uses it as the single
serialization domain for *all* of that session's work.

**Rationale**: Two independent `make_strand(exec_)` calls produce two **distinct**
strands that do not serialize against each other. The read-pump (which calls
`on_inbound_frame` → application callbacks synchronously) and `Engine::send` (which
hops to the session's strand) and teardown `close()` must therefore all share **one**
strand or they can still overlap. A single engine-owned strand per session is the
minimal object that guarantees this.

**Alternatives considered**:
- *Each component makes its own strand* — rejected: distinct strands ⇒ no
  cross-serialization; this is exactly today's latent bug in a subtler form.
- *One engine-global strand for all sessions* — rejected: serializes unrelated
  sessions, killing cross-session parallelism (violates FR-004) and the whole point
  of a multi-threaded executor.

### D2 — Run the entire role loop on the session strand

**Decision**: `co_spawn` `run_accept_loop` / `run_connect_loop` on the per-session
strand (D1), not on bare `exec_`. The inline `run_read_pump` therefore also runs on
the strand.

**Rationale**: A transport coroutine (`async_read_some`, `async_write`) is
`co_await`-ed from the loop, so its associated executor is the loop's executor.
ASIO composed operations (including the ssl `io_op` that performs BIO processing and
`map_error_code`) dispatch their continuations on the **associated executor** of the
completion handler — here, the strand. So running the loop on the strand pins every
SSL/BIO completion step for that session to the strand. This is the load-bearing
change: it makes the read completion and the teardown close (D4) mutually exclusive.

**Alternatives considered**:
- *Read-pump + teardown only on the strand; handshake on `exec_`* — rejected at
  clarify (whole-loop chosen): handshake-on-`exec_` violates the transport's own
  stated contract ("all Transport coroutines run on `exec_`'s strand", asio_tls_transport.hpp
  FR-007) and leaves a second off-strand surface.
- *Teardown-only on the strand* — rejected at clarify: the read-pump still runs
  `on_inbound_frame` callbacks on bare `exec_` concurrently with a strand-hopped
  `Engine::send`, leaving a steady-state race.

### D3 — Bind the Session to the engine's strand (single shared domain)

**Decision**: The `Session` must use the **same** strand the loop runs on (so
callbacks/sends and the read-pump share one domain). The engine injects its
pre-built strand into the session's executor binding rather than letting
`make_session_executor` mint a fresh `make_strand`.

**Approach (to confirm at Gate A / implementation)** — two viable bindings:
- **(A, recommended) `executor_override` + attested direct path**: the engine sets
  `SessionConfig::executor_override = <engine strand>` with
  `threading_mode::direct_executor` + `already_serialized_executor = true`.
  `make_session_executor` then uses the engine strand directly (it is genuinely
  serialized → the attestation is truthful). No new `session_executor` code; reuses
  the existing slot-48 attestation path. Trade-off: `is_strand_wrapped()` reports
  `false` (the strand was made by the engine, not by the factory), which only affects
  a debug re-entrancy-guard assert's wording, not its correctness (the strand still
  serializes; the `in_dispatch_` flag still fires).
- **(B) extend the `per_session_strand` binding to accept a pre-wrapped strand**:
  keeps `is_strand_wrapped() == true` semantics; costs a small change to
  `make_session_executor` / `Session::open`.

**Rationale**: Reusing the existing attestation (A) is the smallest surface and is
semantically honest (an engine-made strand *is* a serialized executor). (B) is
cleaner for the debug invariant but touches 007's binding. Final choice deferred to
Gate A — both satisfy FR-009 (one per-session strand, reused machinery).

**Alternatives considered**:
- *Let the Session keep its own `make_strand`, run the loop on `session->executor()`
  after `open()`* — rejected for the acceptor: the transport is accepted and
  handshaken **before** the Session exists (engine.cpp:480-573), so it would run on
  `exec_`, not the session strand; and `session->executor()` is only valid post-open.
  Creating the strand up front (engine-owned) removes this ordering hazard.

### D4 — Teardown: post `close()` onto the session strand and await it

**Decision**: Replace `Engine::stop()`'s direct `entry.live_transport->close()`
(engine.cpp ~714) with a strand-dispatched close: `co_await asio::co_spawn(<session
strand>, [t = entry.live_transport]() -> awaitable<void> { (void)t->close(); co_return;
}, use_awaitable)` (or `asio::dispatch` on the strand), for each live session, before
the join.

**Rationale**: `close()` mutates the SSL engine (`SSL_shutdown`, `ssl_stream_` teardown)
and the socket. Running it on the session strand serializes it with the read-pump's
in-flight read completion (D2), so no concurrent SSL/BIO access is possible. The
existing "close-to-wake an idle read" behavior is preserved (the close still cancels
the socket and unblocks the pump) — it just happens inside the session's domain.
Ordering vs the join is unchanged (close before join).

**Open detail for plan/tasks**: `stop()` itself runs on `exec_` (it is `co_spawn`-ed
by the caller). Posting close to each strand and awaiting keeps the existing
"close-before-join" guarantee; confirm no deadlock when `stop()` and the target
strand share the single-threaded executor (a nested `co_spawn(strand, …, use_awaitable)`
on the same `io_context` is non-blocking and reentrancy-safe — memory
`feedback_asio_post_resume_bounces_to_spawn_executor`).

### D5 — Transport construction executor (belt-and-suspenders)

**Decision**: Construct each per-session transport (accepted via the listener;
connected via the factory) with the session strand as its executor where practical,
so even the transport's internal reactor work is strand-associated. For the acceptor,
this means the per-session listener / accepted-transport mint uses the strand.

**Rationale**: D2 already pins the *completion* dispatch to the strand via the
associated executor, which is sufficient for correctness. Constructing the I/O
objects on the strand additionally honors the transport's own FR-007 invariant
("strand-confined `read_in_flight_`/`write_in_flight_` booleans … all Transport
coroutines run on `exec_`'s strand") and removes any reliance on subtle
associated-executor propagation through asio's ssl `io_op`. Treated as
defense-in-depth; if a construction-site rebind proves invasive for the acceptor
path, D2+D4 remain the load-bearing guarantee and D5 may be scoped to the initiator
(noted for Gate A).

### D6 — Deterministic regression witness via a controlled interleaving seam

**Decision**: Add a **test-only** synchronization seam that lets a test force
`close()` to run between an in-flight inbound read's `eof` arrival and the
completion's BIO processing — deterministically reproducing the UAF on the
pre-change engine (reliable RED) and proving the strand serializes it away
post-change (GREEN). The seam is compiled out / zero-cost in release (e.g. a debug-only
hook, or a test-injected executor/transport wrapper that gates the read completion on
a latch the test releases after issuing close on another thread).

**Rationale**: The crash is a true multi-thread data race; a plain looped stress test
is flaky and would be rejected at Gate B (memory: FAIL-placeholder / passes-for-wrong-
reason lessons). A controlled interleaving makes the witness a real RED→GREEN gate
(clarify decision). Preferred seam shape: a test-supplied transport/stream wrapper (or
a debug `close_barrier` hook) — NOT a production behavior change. Exact seam placement
is a `/tasks` design item; the constraint is: deterministic, test-only, zero release cost.

**Alternatives considered**: probabilistic stress-loop (rejected at clarify);
sanitizer-matrix-only on the MT business test (rejected — weakest, the crash was
already rare there).

### D7 — Single-threaded behavior & performance preservation

**Decision**: Keep the default `per_session_strand` selection; on a single-threaded
`io_context`, `make_strand` dispatch resolves inline (the strand is immediately ready),
so message handling/ordering/lifecycle are unchanged (FR-007 of the spec). Add/run a
session-throughput benchmark to confirm the added strand hop stays within Article VIII
±5%; if it does not, update the baseline in the same PR with rationale.

**Rationale**: The read path gains one strand dispatch per iteration. On one thread
this is a cheap inline call; the bench gate makes the cost explicit and bounded.

### D8 — No public API change; lift L-019-3

**Decision**: All changes are engine-internal (loop spawn executor, session binding,
`stop()` teardown ordering). No `c_api.h`, no public `Engine`/`Session` signature, no
new required configuration (the default already selects per-session strand). On
landing, update the behaviors-and-limitations catalogue to **lift** L-019-3
("multi-threaded io_context unsupported") and document multi-threaded operation as
supported under the default configuration (FR-010 / SC-005).

**Rationale**: FR-008 (API stability) + Article X. Verified via `nm` (no new exported
symbols) and `abidiff` (no-diff).

## Reference-engine grounding

QuickFIX-cpp, QuickFIX/J (`ThreadedSocketAcceptor`), and Fix8 all use a
**thread-per-session** model with internal per-session locking to serialize a
session's work. fixpp's per-session **strand** over a shared multi-threaded executor
achieves the same *per-session serialization* guarantee without a dedicated thread
per session — semantically equivalent, and the asio-idiomatic form already chosen by
feature 007. No reference engine serializes teardown against in-flight reads via a
shared bare executor (our current bug); they all confine a session to one
thread/lock. This feature brings fixpp's *engine-managed* path in line with that
per-session-serialization invariant. No divergence requiring a spec change was found.

## Risks & mitigations

- **R1 — associated-executor propagation through asio's ssl `io_op` is subtler than
  assumed** → D5 (construct transports on the strand) is the mitigation; verified by
  TSan on the MT acceptance witness.
- **R2 — `stop()` strand-posted close deadlock on a single-threaded executor** →
  use non-blocking `co_spawn(strand, …, use_awaitable)`; covered by the existing
  single-threaded engine tests.
- **R3 — `executor_override` + attested-direct binding (D3-A) changes
  `is_strand_wrapped()` to false** → only affects a debug assert's wording; if it
  matters, fall back to D3-B. Flagged for Gate A.
- **R4 — perf regression from the strand hop** → Article VIII bench gate (D7).
