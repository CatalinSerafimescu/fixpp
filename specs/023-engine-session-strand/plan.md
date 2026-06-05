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
hops are non-blocking posts (no deadlock). The synchronous public readers
(`lookup`/`acceptor_bound_endpoint`) become MT-safe via an atomically-published
immutable snapshot; the **only** public API change is `lookup()` returning
`std::shared_ptr<Session>` (was raw `Session*`) — a deliberate, accepted,
safening-only change (Gate A round-2 user decision; FR-008/SC-004). Single-threaded
path behavior-identical (a strand over a single-threaded `io_context` dispatches
inline). The L-019-3 lift is contingent on the full witness set landing under a
clean sanitizer matrix.

## Technical Context

**Language/Version**: C++20 (Clang 22, Tier-1 Linux; MSVC Tier-2)
**Primary Dependencies**: ASIO 1.36 (`make_strand`, `ssl::stream`, coroutines, cancellation slots); OpenSSL 3; existing `fixpp::core::session_executor` (feature 007-threading-clock); `fixpp::transport::asio_tls_transport` / `asio_listener`
**Storage**: N/A (no persistence change)
**Testing**: GoogleTest; Tier-1 sanitizers ASan + UBSan + **TSan** (TSan is the primary correctness gate here); Google Benchmark for the perf gate; existing `business_messages_roundtrip` multi-threaded harness reused as an acceptance witness
**Target Platform**: Linux/Clang (Tier 1, authoritative for sanitizers + coverage); Windows/MSVC (Tier 2)
**Project Type**: Library (multi-session FIX engine) — internal concurrency wiring change
**Performance Goals**: No regression > ±5% vs `bench/baselines/` on session throughput/latency (Article VIII §2). The change adds a strand hop on the read path AND a **second** hop on the **send** path (`caller → control strand → session strand`, vs today's one). The perf gate MUST therefore cover the send / send-from-callback path under MT and re-measure the baseline against the two-hop design (Gate A NEW-3). On a single-threaded `io_context` strand dispatch is inline.
**Constraints**: One safening-only public Engine/Session API change — `lookup()`→`shared_ptr<Session>` (FR-008 / Article X / SC-004); no other public change; zero-alloc hot path preserved (Article VIII §5); **no `std::mutex` in any awaitable-including header** (Article XI §3 / Article XV; memory `feedback_awaitable_header_mutex_include_edge`) — this feature adds NO locks, only a strand; cancellation stays ASIO-native (Article XI §2)
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
| ABI / one safening-only API change | Art. X / FR-008 | PASS (recorded) | No `c_api.h` change; the sole public-surface delta is `Engine::lookup()` return type `Session*` → `std::shared_ptr<Session>` (FR-008/SC-004 user decision) — `abidiff`/`nm` show exactly that one diff and no other. `acceptor_bound_endpoint()` keeps its signature (MT-safe via snapshot). |
| Banned patterns | Art. XV | PASS | No blocking call on the strand; detached writes keep their keepalive; no synchronous logging added. |

**Verdict**: No unjustified violations. Complexity Tracking is empty. Gate A is mandatory (concurrency trigger) and is the next pipeline step after this plan + user sign-off.

## Project Structure

### Documentation (this feature)

```text
specs/023-engine-session-strand/
├── plan.md              # This file (/speckit-plan output)
├── spec.md              # Feature spec (/speckit-specify + /speckit-clarify)
├── research.md          # Phase 0 — design decisions D0 (control strand) + D1–D8 + D-PUB/D-STOP/D-SNAP
├── data-model.md        # Phase 1 — entities E-0 (control domain) … E-6 (stopped_) + E-7 (reader snapshot)
├── quickstart.md        # Phase 1 — run the engine multi-threaded + verify
├── contracts/
│   └── engine-session-strand.md   # Phase 1 — internal contract C-0…C-8 (one safening-only lookup() API change)
└── checklists/
    └── requirements.md  # Spec quality checklist (green)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── engine.hpp           # Engine: add control_strand_ (E-0) + reader_snapshot_ (E-7); SessionEntry: add
│                        #   session_strand (E-1) + publication discipline (E-2/INV-2); stopped_ → atomic<bool> (E-6);
│                        #   lookup() return type Session* → std::shared_ptr<Session> (D-SNAP/FR-008)
└── session_config.hpp   # (read-only reference; threading_mode default already per_session_strand)

src/session/
├── engine.cpp           # CORE CHANGE (two domains):
│                        #   start()  — on control strand: per-session make_strand(exec_); bind Session
│                        #             via D3-B; spawn role loops ON the session strand
│                        #   run_accept_loop / run_connect_loop — run on session strand; transport on
│                        #             strand (E-5/INV-7); listeners_/endpoints_ writes + AWAITED session/
│                        #             live_transport publication VIA control strand BEFORE read-pump (E-0/E-2/D-PUB);
│                        #             each control mutation republishes reader_snapshot_ (E-7)
│                        #   lookup()/acceptor_bound_endpoint() — atomic_load(reader_snapshot_), lock-free (E-7/C-8)
│                        #   send()   — caller → control strand (registry/stopped) → session strand
│                        #   stop()   — control strand: snapshot/cancel/clear; per-session: transport
│                        #             close() (before join) + Session::close() (after join) on strand
└── session_executor.cpp # binding seam (D3-B) — adopt a pre-created strand under per_session_strand
                         #   via an ENGINE-ONLY adopt tag (NOT the public already_serialized_executor flag);
                         #   store strand directly, strand_wrapped=true; INV-3 identity assert; no make_strand re-wrap

tests/session/
├── test_engine_session_strand.cpp        # NEW —
│                                          #   V-8: control-plane race witness (latch interleave of
│                                          #        stop() vs accept-loop registry/listener write) TSan RED→GREEN
│                                          #   V-9: re-entrant send session→control→session under TSan ≥3 threads
│                                          #        (+ clean fast-fail when issued after stop() has begun)
│                                          #   V-10: assert transport.socket().get_executor()==session_strand
│                                          #   V-11: lookup()/acceptor_bound_endpoint() any-thread vs concurrent stop() — TSan-clean + keepalive
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
- **Round 2 (2026-06-05): Codex P1=2 P2=3 P3=1; Opus post-judging P1=1 P2=6 P3=3 →
  architecture sound, surgical rev-3.** The rev-2 two-domain model is confirmed correct
  (RC#2 fully closed; RC#1/#3/#4 substantially). Convergence applied in this rev-3 bundle.
  **User decision** (authoritative): **Full MT-safe public readers — accept the `lookup()`
  API change.** `acceptor_bound_endpoint()` is made MT-safe via an atomically-published
  immutable snapshot (no signature change); `lookup()` returns `std::shared_ptr<Session>`
  (was raw `Session*`) — a deliberate, accepted, safening-only public API change. Root
  causes addressed:
  - **RC#A** — D0/FR-011's "ALL engine-global state on the control strand" omitted the
    synchronous public read path → added **D-SNAP** lock-free reader snapshot + entity **E-7**
    + contract **C-8** + obligation **V-11**; **FR-008** amended (safening-only,
    `lookup()`→`shared_ptr`) + new **FR-014**; **FR-011** reconciled (mutate on strand, read
    via snapshot); **SC-004** reworded (no *unintended* change, one recorded `lookup()` diff);
    `stopped()` confirmed already covered by D-STOP/E-6 (over-bundled by Codex).
  - **RC#B** — publication described as "post," not pinned → **D-PUB** now requires the
    **awaited** control-strand publish *before* the read pump (`co_await
    co_spawn(control_strand, …, use_awaitable)`), unpublish on loop exit, publish jobs in the
    stop join (E-2/INV-2, C-6); struck the incoherent atomic-`live_transport` alternative
    (NEW-3); registry iterator-stability pinned (INV-6a, NEW-1).
  - **RC#C** — adoption + witness seams described by outcome, not mechanism → **D3-B** adoption
    via an **engine-only tag** (NOT the public `already_serialized_executor` flag) + INV-3
    identity assert/test (E-3/INV-3a); **D6** witness committed as a **one-sided park** (not a
    bidirectional latch that creates an HB edge suppressing the race), targeting the
    listener-map write reachable pre-peer; **SC-002** reworded to the achievable
    one-sided-park standard (V-8).
  - **RC#D** — V-7 lift gate under-counted → **V-7** now gates on V-1 ∧ V-2 ∧ V-8 ∧ V-9 ∧ V-10
    ∧ V-11 + clean sanitizer matrix (FR-010/SC-005, C-3/C-7); **V-9** extended to the
    post-`stop()` re-entrant-send clean-fast-fail disposition (NEW-4); **V-6** notes the
    per-establishment publish hop (NEW-2). Checklist Note reconciled with the sanitizer-name
    exception (P3).
  **One Opus disagreement with Codex recorded**: Codex rated the public-reader residual P1;
  Opus de-escalated it to **P2 + a user decision** (`stopped()` already covered by E-6; the
  flagship MT harness already confines `lookup()`/`acceptor_bound_endpoint()` to quiescent
  windows; the fix is bounded) — the user then chose the full MT-safe path, so the change is
  applied as a deliberate, accepted API delta rather than a doc-narrowing.
  Reviews: `research/reviews/codex_023-engine-session-strand_gate_a_2_review.md`,
  `research/reviews/opus_023-engine-session-strand_gate_a_2_adversarial_review.md`.
