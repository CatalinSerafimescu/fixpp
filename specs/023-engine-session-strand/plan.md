# Implementation Plan: Per-Session Strand Binding for Engine-Managed Sessions

**Branch**: `023-engine-session-strand` | **Date**: 2026-06-05 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/023-engine-session-strand/spec.md`

## Summary

Make a multi-threaded `io_context` a first-class supported execution mode for the
engine. Today every engine-managed session's role loop (`run_accept_loop` /
`run_connect_loop` and the inline `run_read_pump`) and its shutdown teardown
(`Engine::stop()`'s `live_transport->close()`) run on the **bare engine executor
`exec_`**, while the `Session` independently makes its *own* strand at `open()`.
On a multi-threaded `exec_`, teardown `close()` on one thread races the
read-pump's in-flight SSL read completion on another → the `BIO_ctrl` data
race / use-after-free.

Approach (per the clarified scope — whole role loop + teardown on one strand):
the engine creates **one strand per session** (`asio::make_strand(exec_)`), binds
the `Session`, the role loop, the per-session transport, *and* the teardown
`close()` all to that **single shared strand**. Because asio composed-operation
continuations (including the ssl `io_op`'s BIO processing) dispatch on the
awaiting coroutine's associated executor, running the loop on the strand pins all
of a session's I/O completion work to that strand; posting `stop()`'s `close()`
onto the same strand serializes teardown with the read completion. No two
operations for one session ever run concurrently — the race is eliminated by
construction. Public API is unchanged; the single-threaded path is behavior-identical
(a strand over a single-threaded `io_context` dispatches inline).

## Technical Context

**Language/Version**: C++20 (Clang 22, Tier-1 Linux; MSVC Tier-2)
**Primary Dependencies**: ASIO 1.36 (`make_strand`, `ssl::stream`, coroutines, cancellation slots); OpenSSL 3; existing `fixpp::core::session_executor` (feature 007-threading-clock); `fixpp::transport::asio_tls_transport` / `asio_listener`
**Storage**: N/A (no persistence change)
**Testing**: GoogleTest; Tier-1 sanitizers ASan + UBSan + **TSan** (TSan is the primary correctness gate here); Google Benchmark for the perf gate; existing `business_messages_roundtrip` multi-threaded harness reused as an acceptance witness
**Target Platform**: Linux/Clang (Tier 1, authoritative for sanitizers + coverage); Windows/MSVC (Tier 2)
**Project Type**: Library (multi-session FIX engine) — internal concurrency wiring change
**Performance Goals**: No regression > ±5% vs `bench/baselines/` on session throughput/latency (Article VIII §2). The change adds one strand dispatch hop per read iteration; on a single-threaded `io_context` this is an immediate inline dispatch.
**Constraints**: No public Engine/Session API change (FR-008 / Article X); zero-alloc hot path preserved (Article VIII §5); **no `std::mutex` in any awaitable-including header** (Article XI §3 / Article XV; memory `feedback_awaitable_header_mutex_include_edge`) — this feature adds NO locks, only a strand; cancellation stays ASIO-native (Article XI §2)
**Scale/Scope**: One strand per registered session; cross-session parallelism preserved (FR-004). Engine-internal change confined to `src/session/engine.cpp`, `include/fixpp/session/engine.hpp`, and the session↔strand binding seam; no new module.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Gate | Source | Status | Notes |
|------|--------|--------|-------|
| **Concurrency 4-control trigger** | Art. XI §7 | **PLANNED** | `/clarify` ✅ done (2 Qs); `/analyze` ⏳ after `/tasks`; **Codex Gate A mandatory** after this plan; **user `/plan` sign-off** required. This is the canonical concurrency-affecting feature. |
| Per-session strand default | Art. XI §4 | PASS | Reuses the default `per_session_strand` serialization primitive (FR-009); introduces no new concurrency mechanism. |
| Coroutines as the primitive | Art. XI §1 | PASS | Role loops & transport remain `asio::awaitable`; only their executor binding changes. |
| ASIO-native cancellation | Art. XI §2 | PASS | `session_cancel` slots unchanged; teardown still total-cancel + close, now strand-posted. |
| No `std::mutex` in awaitable headers | Art. XI §3 / XV | PASS | Zero locks added; strand only. Header-include hygiene re-checked at verify (unfiltered Tier-1 `sync` label). |
| TDD / no code without test | Art. VII §3-4 | PASS | Deterministic RED witness (test seam) precedes the fix; MT acceptance witness + single-threaded suite. |
| Sanitizers Tier-1 (ASan/UBSan/**TSan**) | Art. IX §2 | PASS (target) | The feature's pass criterion: MT lifecycle clean under all three (SC-001). |
| Coverage ≥95/85 touched | Art. IX §1 | PASS (target) | Touched: `src/session/engine.cpp` + binding seam. Uncovered defensive lines get a recorded risk assessment at verify. |
| Perf ±5% | Art. VIII §2 | **GATE** | Strand-dispatch hop added to the read path — must show session-throughput bench within ±5% or update baseline with rationale in the same PR. |
| ABI / no public API change | Art. X / FR-008 | PASS | No `c_api.h` change; engine/session public surface unchanged. Verified via `nm` + `abidiff` (no-diff expected). |
| Banned patterns | Art. XV | PASS | No blocking call on the strand; detached writes keep their keepalive; no synchronous logging added. |

**Verdict**: No unjustified violations. Complexity Tracking is empty. Gate A is mandatory (concurrency trigger) and is the next pipeline step after this plan + user sign-off.

## Project Structure

### Documentation (this feature)

```text
specs/023-engine-session-strand/
├── plan.md              # This file (/speckit-plan output)
├── spec.md              # Feature spec (/speckit-specify + /speckit-clarify)
├── research.md          # Phase 0 — design decisions D1–D8
├── data-model.md        # Phase 1 — engine-internal entities (per-session strand binding)
├── quickstart.md        # Phase 1 — run the engine multi-threaded + verify
├── contracts/
│   └── engine-session-strand.md   # Phase 1 — internal serialization contract (no public API change)
└── checklists/
    └── requirements.md  # Spec quality checklist (green)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── engine.hpp           # SessionEntry: add the per-session strand handle; update E-5 threading note
└── session_config.hpp   # (read-only reference; threading_mode default already per_session_strand)

src/session/
├── engine.cpp           # CORE CHANGE:
│                        #   start()  — create one strand per session; spawn role loops ON the strand;
│                        #             bind the Session to that SAME strand (executor injection)
│                        #   run_accept_loop / run_connect_loop — run on the strand; mint listener/
│                        #             transport on the strand
│                        #   stop()   — post each session's close() onto its strand and await (replaces
│                        #             the bare-exec_ close at ~line 714)
└── session_executor.cpp # binding seam — accept the engine-provided strand for the session
                         #   (executor_override + attestation, or a per_session_strand pre-wrap)

tests/session/
├── test_engine_session_strand.cpp        # NEW — deterministic teardown-race witness via interleaving seam
│                                          #   (RED pre-change, GREEN post-change) + MT lifecycle cells
└── test_business_messages_roundtrip.cpp  # existing MT (3-thread) harness — acceptance witness, now clean

bench/
└── (session throughput bench)            # Art. VIII gate — confirm ±5% with the strand hop
```

**Structure Decision**: Single-library layout. The change is engine-internal: it
re-routes existing engine-managed work onto a per-session strand. No new module,
no new public type. The only cross-cutting touch is the session↔strand binding
seam (`session_executor.cpp` / `engine.cpp`), reusing 007's `session_executor`.

## Complexity Tracking

> No Constitution Check violations require justification. (The Article VIII perf
> gate is a measurement obligation, not a complexity deviation.)

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
