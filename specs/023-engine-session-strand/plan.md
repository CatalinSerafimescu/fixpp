# Implementation Plan: Per-Session Strand Binding for Engine-Managed Sessions

**Branch**: `023-engine-session-strand` | **Date**: 2026-06-05 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/023-engine-session-strand/spec.md`

## Summary

Make a multi-threaded `io_context` a first-class supported execution mode for the
engine by establishing **two serialization domains** (Gate A round 1 found one
domain insufficient — see `## Gate A`):

1. **Engine control-plane strand** — all engine-global state (`registry_`,
   `stopped_`, `listeners_`/`listener_endpoints_`, `accept_scope_signals_`,
   counters, and the publication of `entry.session`/`entry.live_transport`) is
   serialized on one engine-owned control strand; every cross-thread entry point
   (any-thread `send`, `stop`) routes through it. This closes the
   `unordered_map` data race between `stop()` (clearing the registry/listener
   tables on one thread) and `run_accept_loop` (publishing into them on another)
   — a race *worse* than the TLS crash and unfixable by any per-session boundary.
2. **Per-session strand** — all of a session's work (establishment, handshake,
   read-pump, callbacks, sends, transport `close()`, terminal `Session::close()`)
   runs on one engine-owned per-session strand. Because asio composed-op
   continuations (incl. the ssl `io_op`'s BIO processing) dispatch on the awaiting
   coroutine's associated executor (verified at Gate A vs `ssl/detail/io.hpp`),
   loop-on-strand pins a session's I/O completions to its strand; posting both
   teardown closes onto the same strand serializes them with the read completion
   → the `BIO_ctrl` race is gone by construction.

`send` = `caller → control strand → session strand`; `stop` = control-strand
snapshot/cancel/clear with per-session closes dispatched to session strands. All
hops are non-blocking posts (no deadlock). Public API unchanged; single-threaded
path behavior-identical (a strand over a single-threaded `io_context` dispatches
inline). The L-019-3 lift is contingent on both domains landing with passing TSan
witnesses.

## Technical Context

**Language/Version**: C++20 (Clang 22, Tier-1 Linux; MSVC Tier-2)
**Primary Dependencies**: ASIO 1.36 (`make_strand`, `ssl::stream`, coroutines, cancellation slots); OpenSSL 3; existing `fixpp::core::session_executor` (feature 007-threading-clock); `fixpp::transport::asio_tls_transport` / `asio_listener`
**Storage**: N/A (no persistence change)
**Testing**: GoogleTest; Tier-1 sanitizers ASan + UBSan + **TSan** (TSan is the primary correctness gate here); Google Benchmark for the perf gate; existing `business_messages_roundtrip` multi-threaded harness reused as an acceptance witness
**Target Platform**: Linux/Clang (Tier 1, authoritative for sanitizers + coverage); Windows/MSVC (Tier 2)
**Project Type**: Library (multi-session FIX engine) — internal concurrency wiring change
**Performance Goals**: No regression > ±5% vs `bench/baselines/` on session throughput/latency (Article VIII §2). The change adds a strand hop on the read path AND a **second** hop on the **send** path (`caller → control strand → session strand`, vs today's one). The perf gate MUST therefore cover the send / send-from-callback path under MT and re-measure the baseline against the two-hop design (Gate A NEW-3). On a single-threaded `io_context` strand dispatch is inline.
**Constraints**: No public Engine/Session API change (FR-008 / Article X); zero-alloc hot path preserved (Article VIII §5); **no `std::mutex` in any awaitable-including header** (Article XI §3 / Article XV; memory `feedback_awaitable_header_mutex_include_edge`) — this feature adds NO locks, only a strand; cancellation stays ASIO-native (Article XI §2)
**Scale/Scope**: One control strand per engine + one strand per registered session; cross-session parallelism preserved (FR-004). Engine-internal change confined to `src/session/engine.cpp`, `include/fixpp/session/engine.hpp`, the session↔strand binding seam (`src/session/session_executor.cpp`), and `stopped_`'s type; no new module.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Gate | Source | Status | Notes |
|------|--------|--------|-------|
| **Concurrency 4-control trigger** | Art. XI §7 | **IN PROGRESS** | `/clarify` ✅ (2 sessions: initial + Gate A round-1); **Codex Gate A round 1 ✅ ran** (found structural → this two-domain re-plan); Gate A re-review pending; `/analyze` ⏳ after `/tasks`; **user `/plan` sign-off** required. The canonical concurrency-affecting feature. |
| Per-session strand default | Art. XI §4 | PASS | Reuses the existing strand primitive (`per_session_strand`) for BOTH domains via D3-B (FR-009); introduces no new concurrency mechanism and no lock. |
| Coroutines as the primitive | Art. XI §1 | PASS | Role loops & transport remain `asio::awaitable`; only their executor binding changes. |
| ASIO-native cancellation | Art. XI §2 | PASS | `session_cancel` slots unchanged; teardown still total-cancel + close, now strand-posted. |
| No `std::mutex` in awaitable headers | Art. XI §3 / XV | PASS | Zero locks added; strand only. Header-include hygiene re-checked at verify (unfiltered Tier-1 `sync` label). |
| TDD / no code without test | Art. VII §3-4 | PASS | Deterministic RED witness (test seam) precedes the fix; MT acceptance witness + single-threaded suite. |
| Sanitizers Tier-1 (ASan/UBSan/**TSan**) | Art. IX §2 | PASS (target) | The feature's pass criterion: MT lifecycle clean under all three (SC-001). |
| Coverage ≥95/85 touched | Art. IX §1 | PASS (target) | Touched: `src/session/engine.cpp` + binding seam + `session_executor.cpp`. Uncovered defensive lines get a recorded risk assessment at verify. |
| Perf ±5% | Art. VIII §2 | **GATE** | TWO strand hops added on the send path + one on the read path — must show session-throughput **and send/send-from-callback** bench within ±5% (or update baseline w/ rationale, same PR). Re-measured against the two-hop design (Gate A NEW-3). |
| ABI / no public API change | Art. X / FR-008 | PASS | No `c_api.h` change; engine/session public surface unchanged. Verified via `nm` + `abidiff` (no-diff expected). |
| Banned patterns | Art. XV | PASS | No blocking call on the strand; detached writes keep their keepalive; no synchronous logging added. |

**Verdict**: No unjustified violations. Complexity Tracking is empty. Gate A is mandatory (concurrency trigger) and is the next pipeline step after this plan + user sign-off.

## Project Structure

### Documentation (this feature)

```text
specs/023-engine-session-strand/
├── plan.md              # This file (/speckit-plan output)
├── spec.md              # Feature spec (/speckit-specify + /speckit-clarify)
├── research.md          # Phase 0 — design decisions D0 (control strand) + D1–D8 + D-PUB/D-STOP
├── data-model.md        # Phase 1 — entities E-0 (control domain) … E-6 (stopped_)
├── quickstart.md        # Phase 1 — run the engine multi-threaded + verify
├── contracts/
│   └── engine-session-strand.md   # Phase 1 — internal contract C-0…C-7 (no public API change)
└── checklists/
    └── requirements.md  # Spec quality checklist (green)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── engine.hpp           # Engine: add control_strand_ (E-0); SessionEntry: add session_strand (E-1)
│                        #   + publication discipline (E-2/INV-2); stopped_ → atomic<bool> (E-6)
└── session_config.hpp   # (read-only reference; threading_mode default already per_session_strand)

src/session/
├── engine.cpp           # CORE CHANGE (two domains):
│                        #   start()  — on control strand: per-session make_strand(exec_); bind Session
│                        #             via D3-B; spawn role loops ON the session strand
│                        #   run_accept_loop / run_connect_loop — run on session strand; transport on
│                        #             strand (E-5/INV-7); listeners_/endpoints_ writes + session/
│                        #             live_transport publication VIA control strand (E-0/E-2)
│                        #   send()   — caller → control strand (registry/stopped) → session strand
│                        #   stop()   — control strand: snapshot/cancel/clear; per-session: transport
│                        #             close() (before join) + Session::close() (after join) on strand
└── session_executor.cpp # binding seam (D3-B) — adopt a pre-created strand under per_session_strand
                         #   (store strand directly, strand_wrapped=true; no make_strand re-wrap)

tests/session/
├── test_engine_session_strand.cpp        # NEW —
│                                          #   V-8: control-plane race witness (latch interleave of
│                                          #        stop() vs accept-loop registry/listener write) TSan RED→GREEN
│                                          #   V-9: re-entrant send session→control→session under TSan ≥3 threads
│                                          #   V-10: assert transport.socket().get_executor()==session_strand
│                                          #   V-3: 2-session cross-parallelism cell
└── test_business_messages_roundtrip.cpp  # existing MT (3-thread) harness — acceptance witness (V-2), now clean

bench/
└── (session throughput + send / send-from-callback bench)   # Art. VIII gate — ±5% vs two-hop baseline (V-6)
```

**Structure Decision**: Single-library layout. The change is engine-internal: it
adds an engine control strand for engine-global state and re-routes per-session
work onto a per-session strand. No new module, no new public type. Cross-cutting
touches are the control-plane domain in `engine.{hpp,cpp}`, the session↔strand
binding seam (`session_executor.cpp`), and `stopped_`'s type — reusing 007's
`session_executor`/strand machinery.

## Complexity Tracking

> No Constitution Check violations require justification. (The Article VIII perf
> gate is a measurement obligation, not a complexity deviation.)

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Gate A

- **Round 1 (2026-06-05): Codex P1=1 P2=4 P3=1; Opus post-judging P1=2 P2=8 P3=3 → STRUCTURAL.**
  Not converged by rewrite — the user chose to **re-plan with a two-domain model**.
  Root causes addressed in this rev-2 bundle:
  - **RC#1** (the structural one): the design modeled only a per-session domain, leaving
    the engine control plane (`registry_`/`stopped_`/`listeners_`/`listener_endpoints_`/
    `accept_scope_signals_`/counters/handle-publication) racy → added **D0 engine control
    strand** + D-PUB publication discipline + D-STOP `stopped_` atomic (research D0/D-PUB/D-STOP,
    data-model E-0/E-2/E-6, contract C-0/C-5).
  - **RC#2**: D5 transport-on-strand re-ranked from "optional defense-in-depth" to
    **mandatory + auto-satisfied-by-D2 + asserted** (research D5, data-model E-5/INV-7, V-10).
  - **RC#3**: committed **D3-B** binding (not the verified-broken D3-A `direct_executor`
    rewrite); enumerated **both** teardown closes (transport + terminal `Session::close`);
    pinned non-blocking hop semantics + added re-entrant-send MT witness (research D3/D4,
    data-model E-3/E-4, contract C-2/C-6, V-9).
  - **RC#4**: re-targeted the deterministic witness to the **control-plane data race**
    (TSan-deterministic via a latch), feasible where the rev-1 transport-level BIO seam was
    not (research D6, spec SC-002, V-8).
  - Plus: two-hop perf gate (D7/V-6), contingent L-019-3 lift (D8/FR-010), checklist
    exception (P3).
  Reviews: `research/reviews/codex_023-engine-session-strand_gate_a_review.md`,
  `research/reviews/opus_023-engine-session-strand_gate_a_adversarial_review.md`.
  Re-Gate-A pending on this rev-2 bundle.
