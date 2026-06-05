# Internal Contract: Engine Per-Session Serialization Domain

**Feature**: 023-engine-session-strand
**Type**: Engine-internal contract (NO public/external interface change — FR-008).
**Date**: 2026-06-05

This feature exposes **no new public API**, command schema, or C-ABI surface. The
"contract" it establishes is an internal invariant on how the engine schedules a
session's work. It is recorded here so Gate A and `/speckit-tasks` can verify it and
so future engine changes do not regress it.

## C-1 — Single per-session serialization domain

For every engine-managed session, there exists exactly one serialization domain (a
per-session strand). The engine MUST schedule **all** of the following onto that one
domain:

1. connection establishment (accept or connect),
2. TLS handshake,
3. the inbound read-pump (`async_read_some` + framing + `on_inbound_frame`),
4. application callbacks dispatched by the session,
5. outbound `Engine::send` work for that session,
6. teardown `close()` during `Engine::stop()`.

**Guarantee**: no two of the above for the *same* session execute concurrently, even
when `exec_` is serviced by multiple threads.

**Non-guarantee**: operations for *different* sessions MAY run concurrently
(cross-session parallelism preserved — FR-004).

## C-2 — Public surface is unchanged

- `Engine` and `Session` public types, signatures, and required configuration are
  identical before and after (verified: `nm -D` no new exported symbols; `abidiff`
  no-diff on `libfixpp_capi.so`).
- An application obtains safe multi-threaded behavior with **no** new configuration:
  the default `threading_mode::per_session_strand` already selects per-session
  serialization; this feature makes the engine actually honor it for the role loops
  and teardown.

## C-3 — Multi-threaded execution is supported

- Running the engine with multiple threads calling `io_context::run()` on the same
  context is a supported mode. The previously documented limitation
  (behaviors-and-limitations **L-019-3**, "multi-threaded io_context unsupported")
  is lifted on landing.
- The `direct_executor` opt-out remains the caller's responsibility for serialization
  and is explicitly **out of scope** (an application that selects `direct_executor`
  with a multi-threaded executor without a truthful `already_serialized_executor`
  attestation is rejected today via `error::executor_not_serialised`, slot 48 —
  unchanged).

## C-4 — Verification obligations (consumed by /speckit-tasks)

| ID | Obligation | Evidence |
|----|------------|----------|
| V-1 | Deterministic teardown-race witness reproduces RED pre-change, GREEN post-change | `tests/session/test_engine_session_strand.cpp` (interleaving seam) under ASan/UBSan/TSan |
| V-2 | Full lifecycle on a ≥3-thread executor is clean | `business_messages_roundtrip` (MT) + new MT lifecycle cells, ASan/UBSan/TSan |
| V-3 | Cross-session parallelism preserved | a 2-session MT cell shows independent progress; no engine-global serialization |
| V-4 | Single-threaded behavior unchanged | existing single-threaded suite green, no rewrites |
| V-5 | No public API/ABI change | `nm` + `abidiff` no-diff |
| V-6 | Perf within ±5% | session-throughput bench vs baseline (Art. VIII) |
| V-7 | L-019-3 lifted + docs updated | behaviors-and-limitations + concurrency doc edits |
