# Internal Contract: Engine Two-Domain Serialization

**Feature**: 023-engine-session-strand
**Type**: Engine-internal contract (NO public/external interface change — FR-008).
**Date**: 2026-06-05 (rev 2 — two-domain model)

No new public API, command schema, or C-ABI surface. The contract is an internal
invariant on how the engine schedules work across **two** serialization domains.
Recorded so Gate A and `/speckit-tasks` can verify it and future engine changes do not
regress it.

## C-0 — Engine control-plane domain (NEW)

There is exactly one engine control-plane serialization domain (a control strand). The
engine MUST access **all** engine-global state only within it:

- the session `registry_`,
- the `stopped_` flag (authoritative write; reads are atomic-acquire — see C-5),
- the `listeners_` / `listener_endpoints_` tables,
- the `accept_scope_signals_` table,
- the in-flight counters' reset/clear ordering,
- the publication of `entry.session` / `entry.live_transport` that the shutdown path reads.

**Guarantee**: no two threads ever read/mutate engine-global state concurrently on a
multi-threaded executor. Every cross-thread entry point (any-thread `send`, `stop`) enters
the control-plane domain before touching engine-global state.

## C-1 — Single per-session serialization domain

For every engine-managed session there is exactly one per-session domain (a session
strand, subordinate to C-0). The engine MUST schedule all of the following onto that one
domain: connection establishment, TLS handshake, the inbound read-pump
(`async_read_some` + framing + `on_inbound_frame`), application callbacks, outbound
`Engine::send` work for that session, the transport `close()`, and the terminal
`Session::close()`.

**Guarantee**: no two operations for the *same* session execute concurrently, even when
the executor is serviced by multiple threads. **Non-guarantee**: different sessions MAY
run concurrently (cross-session parallelism preserved — FR-004).

## C-2 — Hop semantics (no deadlock)

Every handoff into or between domains is a **non-blocking post**
(`co_spawn(strand, …, use_awaitable)`), never an inline `dispatch`-that-runs or a blocking
wait on a strand. Consequence: a send issued from inside an application callback
(`session strand → control strand → session strand`) cannot deadlock the domain it runs on.

## C-3 — Multi-threaded execution is supported (contingent)

- Multiple threads calling `io_context::run()` on the same context is a supported mode —
  **but only asserted once BOTH domains (C-0 and C-1) land with passing thread-sanitizer
  witnesses**. The behaviors-and-limitations **L-019-3** lift happens in the same PR and
  only then (FR-010 / SC-005). Lifting it earlier would publish a false safety claim.
- The `direct_executor` opt-out remains the caller's serialization responsibility and is
  **out of scope** (unchanged; `error::executor_not_serialised` slot 48 still rejects an
  unattested direct executor).

## C-4 — Public surface is unchanged

`Engine`/`Session` public types, signatures, and required configuration are identical
before/after (`nm -D` no new exported symbols; `abidiff` no-diff on `libfixpp_capi.so`).
No new configuration: the default `per_session_strand` mode already selects per-session
serialization; the control strand is engine-internal.

## C-5 — `stopped_` access discipline

`stopped_` is an `std::atomic<bool>` (acquire/release): authoritative write on the control
strand in `stop()`; the accept-loop gate and the send fast-fail read it with atomic
acquire. It MUST NOT remain a plain non-atomic flag justified by single-thread confinement.

## C-6 — Teardown enumerates BOTH closes

`stop()` MUST schedule, per live session: transport `close()` on the session strand
**before** the join (wake the idle read), AND terminal `Session::close()` on the session
strand **after** join + send-drain + **before** the control-strand registry clear
(preserving the documented liveness-loop-drain rationale). The send-drain ordering is
preserved explicitly.

## C-7 — Verification obligations (consumed by /speckit-tasks)

| ID | Obligation | Evidence |
|----|------------|----------|
| V-1 | Per-session teardown: transport close serialized with the in-flight read | session-strand close ordering; MT acceptance test under ASan/UBSan/TSan |
| V-2 | Full lifecycle on a ≥3-thread executor is clean | `business_messages_roundtrip` (MT) + new MT lifecycle cells, ASan/UBSan/TSan |
| V-3 | Cross-session parallelism preserved | a 2-session MT cell shows independent progress; no engine-global serialization of unrelated sessions |
| V-4 | Single-threaded behavior unchanged | existing single-threaded suite green, no rewrites |
| V-5 | No public API/ABI change | `nm` + `abidiff` no-diff |
| V-6 | Perf within ±5% — **two-hop** send path | session-throughput **and send / send-from-callback** bench vs re-measured baseline (Art. VIII) |
| V-7 | L-019-3 lifted **only after** V-8+V-9 pass | behaviors-and-limitations + concurrency doc edits, gated |
| **V-8** | **Control-plane race deterministically witnessed** | latch-controlled interleave (shutdown vs accept-loop registry/listener mutation + any-thread send) → TSan RED pre-change, GREEN post-change |
| **V-9** | **Re-entrant send across both domains has no deadlock under MT** | `session→control→session` send-from-callback cell under TSan, ≥3 threads |
| **V-10** | **Transport socket executor == session strand** | debug assert + a test over the four construction sites (engine listener-build, reconnect_fsm make, the two transport ctors) |
