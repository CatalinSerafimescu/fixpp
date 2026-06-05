# Research: Per-Session + Control-Plane Strand Binding for the Engine

**Feature**: 023-engine-session-strand
**Date**: 2026-06-05 (rev 2 — two-domain model after Gate A round 1)
**Inputs**: spec.md (clarified + Gate A round-1 session); constitution Art. XI; the
`BIO_ctrl` crash analysis; feature 007 (`fixpp::core::session_executor`); the engine
role-loop + `stop()` + `Engine::send` code; the Gate A round-1 reviews
(`research/reviews/codex_023-…_gate_a_review.md`,
`research/reviews/opus_023-…_gate_a_adversarial_review.md`).

## Problem restatement (grounded — two race classes)

There are **two** co-equal race classes when the engine runs on a multi-threaded
`io_context`, and the original (rev 1) design addressed only the first:

1. **Per-session class** — the read-pump's in-flight SSL read completion races the
   teardown `close()`. `asio::ssl::detail::engine::map_error_code` (asio
   `engine.ipp:247`) touches the BIO (`BIO_wpending`) only on a peer-FIN `eof`; on a
   multi-threaded `exec_`, `Engine::stop()`'s `live_transport->close()` (engine.cpp
   ~714) on one thread races that completion on another → the `BIO_ctrl` UAF.

2. **Control-plane class (the gap Gate A found)** — engine-global state is read and
   mutated on the bare executor with no serialization. `run_accept_loop` **writes**
   `engine.listener_endpoints_[id]` (engine.cpp:467), `engine.listeners_[id]` (468),
   `entry.session` (555), `entry.live_transport` (578) and **reads** `engine.stopped()`
   (478); `Engine::stop()` mutates `stopped_` (702), iterates `registry_`/
   `accept_scope_signals_` (704-705, 713-714, 753-757) and **clears**
   `accept_scope_signals_`/`listeners_`/`listener_endpoints_`/`registry_` (760-763);
   `Engine::send` reads `registry_`/`stopped_` (814-851). On a multi-threaded executor
   these run on **uncoordinated** frames — `make_strand(exec_)` serializes a session
   against itself but does nothing against `stop()` on bare `exec_`. Thread-A
   `registry_.clear()` while thread-B `engine.listeners_[id] = std::move(...)` is an
   `unordered_map` data race + rehash/iterator-invalidation UAF — strictly worse than
   the BIO race.

The engine's own header admits the assumption being removed: `stopped_` is "Safe under
single-executor confinement (E-5)" (engine.hpp:296); `Engine::send`'s "hop onto `exec_`
serializes with stop()" comment (engine.hpp:323) **is false on a multi-threaded `exec_`**.

**Therefore the design needs two serialization domains**, not one.

## Decisions

### D0 — Engine control-plane strand (NEW; the load-bearing rev-2 addition)

**Decision**: Introduce a single engine-owned **control strand**
(`asio::make_strand(exec_)`, created once in the `Engine`) as the serialization domain
for **all** engine-global state: the session `registry_`, `stopped_`, `listeners_`,
`listener_endpoints_`, `accept_scope_signals_`, the counters'
publication/clear ordering, and the publication of `entry.session` /
`entry.live_transport`. Every cross-thread engine entry point routes through it:
- `Engine::send` = `caller → control strand` (registry/stopped read + counter bump)
  `→ session strand` (toApp + `Session::send`).
- `Engine::stop()` runs its snapshot / signal-cancel / per-session-close dispatch /
  join / drain / **clear** on the control strand.
- `Engine::start()` creates each session's strand and spawns its loop **from** the
  control strand; the role loops only ever **read** the strand handle and publish
  `session`/`live_transport` **via a control-strand dispatch** (never a bare write the
  control strand later reads).

**Rationale**: per-session strands cannot serialize engine-global state by construction.
A second strand is the asio-idiomatic, lock-free way to serialize the control plane,
mirroring how QuickFIX-cpp/J/Fix8 serialize their global session dictionaries under a
lock (see Reference grounding). This collapses Gate A Root cause #1 (Codex-1 + NEW-2/4/5/6/7).

**Residual hazards explicitly covered** (so this is not a half-restructure):
- `listeners_`/`listener_endpoints_` writes in the accept loop → moved to the control
  strand (or pre-created in `start()` on the control strand; the loop reads only).
- `entry.session`/`entry.live_transport` publication → a control-strand dispatch (D-PUB
  below), so `stop()`'s reads (713/754) are ordered.
- `outstanding_counter_.reset()` + `registry_.clear()` ordering → control-strand-confined
  so no join/drain loop reads a half-cleared map.
- `stopped_` access discipline → D-STOP below.

**Alternatives considered**:
- *One global engine mutex* — rejected: Art. XI bans locks in awaitable headers and the
  project is strand-first; a mutex on the send/stop path also reintroduces blocking.
- *Lock-free registry snapshot for `send`, control strand only for `stop`'s clear* —
  weighed (see NEW-3 perf note); deferred as a possible optimization but NOT the v1
  design (a single control strand is simpler and correct; revisit only if the bench gate
  fails).

### D-PUB — Publication discipline for `session` / `live_transport`

**Decision**: `entry.session` and `entry.live_transport` (the handles `stop()` reads)
are written **on the control strand** (the role loop posts the publish to the control
strand), or held as atomics with release/acquire. `stop()` reads them on the control
strand. No bare-strand write that the control strand later reads.

**Rationale**: closes Gate A NEW-2 (torn read of the published handles across domains).

### D-STOP — `stopped_` memory model

**Decision**: the role-loop gate `while (!engine.stopped())` (engine.cpp:478) and
`Engine::send`'s `if (stopped_)` (822) become cross-domain reads under MT. Make
`stopped_` an `std::atomic<bool>` with acquire/release (and update the E-5 comment), OR
route both reads through the control strand. Choice: **`atomic<bool>` with
acquire/release** — cheapest, avoids a control-strand hop on every read-loop iteration,
and the accept-loop gate is a hot check. The authoritative *write* still happens on the
control strand in `stop()`.

**Rationale**: closes Gate A NEW-6; the plain-bool "safe under confinement" comment is
exactly the invariant this feature removes.

### D1 — One strand per engine-managed session, engine-owned

(unchanged from rev 1) Exactly one `make_strand(exec_)` per session, stored in
`SessionEntry`, created on the control strand in `start()`. Two `make_strand(exec_)`
calls yield **distinct** strands, so the loop, the Session, the transport, and teardown
must share the **one** stored strand.

### D2 — Run the entire role loop on the session strand (verified correct)

(unchanged conclusion; **mechanism verified at Gate A**) `co_spawn` the role loop on the
per-session strand. Gate A confirmed against asio source (`ssl/detail/io.hpp:352-374`)
that `io_op` delegates `associated_executor` to the awaiting handler, so the ssl
`io_op`'s BIO processing (`map_error_code` at io.hpp:300/316) is posted via the strand
when the read-pump coroutine runs on the strand. D2's central bet holds.

### D5 — Transport I/O object on the session strand: MANDATORY + auto-satisfied + ASSERTED

**Decision (re-ranked from rev 1's "optional defense-in-depth")**: the accepted/connected
socket MUST be associated with the session strand. Gate A established this is
**auto-satisfied by D2** — the accepted socket inherits the listener's executor
(`asio_listener.cpp:144`), the listener is built with `co_await this_coro::executor`
(engine.cpp:420/459), a moved socket keeps its executor (`asio_tls_transport.cpp:579`),
and the initiator path samples `co_await this_coro::executor` too
(`reconnect_fsm.cpp:117/225`). So the real obligation is the **inverse**: ensure **no**
construction site samples the engine's bare `exec_` member instead of the loop-local
strand, and **assert** it.

**Tasks this creates**: audit the four construction sites (engine listener-build
engine.cpp:459, `reconnect_fsm` make ~225, the two `asio_tls_transport` ctors ~542/560/579)
for bare-`exec_` leaks; add a debug assert / test that
`transport.socket().get_executor() == session_strand`. Drop all "may be scoped to
initiator" language.

**Rationale**: closes Gate A Root cause #2 (Codex-2). The failure mode (a ctor silently
sampling bare `exec_`) is invisible at compile time; the assertion is the guard.

### D3 — Bind the Session to the engine's strand via **D3-B** (decided)

**Decision (committed — D3-B, NOT the rev-1 D3-A)**: extend the per-session-strand
binding to **adopt a pre-created strand** while keeping `threading_mode::per_session_strand`.
Today `make_session_executor` unconditionally `make_strand(resolved_exec)`
(session_executor.cpp:35) — re-wrapping an already-strand executor would create a second,
distinct strand (the D1 anti-pattern). Minimal change: under `per_session_strand` with an
engine-supplied already-strand executor, store it directly with `strand_wrapped=true`
(truthful) instead of re-wrapping. Preserves mode, `is_strand_wrapped()==true`,
spin-policy legality, and FR-009.

**Why NOT D3-A** (rev-1 recommendation, **verified broken** at Gate A): D3-A
(`executor_override` + `direct_executor` + attested) would internally rewrite a user's
config to `direct_executor`, and `Session::open()` rejects `direct_executor +
lock_policy::spin` (session.cpp:597-599) — so a user who validly set `locks = spin` under
the default `per_session_strand` would have their session rejected. That is the silent
compat regression FR-007/FR-008 forbid. Closes Gate A Codex-3.

### D4 — Teardown: BOTH closes on the session strand; registry ops on the control strand

**Decision (rev-1 omitted the second close)**: `Engine::stop()` performs, per live session:
1. **transport `close()`** dispatched on the **session strand**, **before** the join — to
   wake an idle in-flight read (serialized with that read's completion → fixes the BIO race).
2. **terminal `Session::close()`** dispatched on the **session strand**, **after** the
   join + **after** the send-drain, **before** `registry_.clear()` — preserving the
   existing post-join phase (engine.cpp:753-757) that drains a parked `run_liveness_loop`
   `sleep_until` whose `system_clock_source` dereg-guard otherwise touches a freed clock
   pimpl (a prior real heap-UAF). This phase is load-bearing and must survive.

The surrounding registry iteration and `registry_.clear()` (760-763) run on the **control
strand** (D0). The existing send-drain ordering (735-739) is preserved explicitly.

**Hop semantics**: every dispatch is a **non-blocking `co_spawn(strand, …, use_awaitable)`**
(`post`, not inline `dispatch`, never a blocking `future.get()` on a strand). On a
single-threaded `io_context` this is the known-safe re-entrant post; on a multi-threaded
one no thread is blocked.

**Rationale**: closes Gate A Codex-5 (omitted `Session::close`) + NEW-1 (hop semantics).

### D6 — Deterministic witness: target the CONTROL-PLANE race (feasible)

**Decision (re-targeted from rev-1's transport-level seam, which Gate A proved infeasible)**:
the deterministic RED witness targets the **engine control-plane data race** (Root cause
#1), not the BIO crash. A test-only **latch** in the accept-loop publication path (compiled
under a `FIXPP_TEST_SEAMS`-style debug seam, zero release cost) lets the test hold an
accept-loop registry/listener write open while it drives `Engine::stop()` (and an
any-thread `send`) on other threads — so the thread-sanitizer reports the
`registry_`/`listeners_` data race on **every** pre-change run, and reports **nothing**
post-change (the control strand serializes them). The TLS-teardown BIO crash is a
**downstream symptom** covered by the multi-threaded acceptance test (SC-001 / the reused
`business_messages_roundtrip` 3-thread harness).

**Why this is feasible where the rev-1 seam was not**: Gate A showed that a `Transport`-level
wrapper gates **after** the ssl `io_op` BIO touch (too late to witness the BIO race
deterministically), and a real-BIO seam would need a custom `ssl::stream<TestNextLayer>`
that does not exist (the transport hard-codes `tcp::socket`). The control-plane race, by
contrast, is an ordinary `unordered_map` data race that TSan reports **deterministically**
given a controlled interleave — no SSL internals required. This both satisfies the
"deterministic" clarify decision AND targets the actual root cause.

**Rationale**: closes Gate A Root cause #4 (Codex-4) without a spec re-clarify; the
control-plane race is the load-bearing fix, so witnessing it deterministically is the
right gate.

### D7 — Perf: account for the TWO-hop send path

**Decision**: the send path is now `caller → control strand → session strand` (two hops vs
today's one). The Article VIII ±5% bench MUST include a **send path** and ideally a
**send-from-callback** bench under MT, and the baseline re-measured against the two-hop
design. If the control-strand hop proves hot, the D0-alternative (lock-free registry
snapshot for `send`) is the fallback — but as a measured decision, not a guess.

**Rationale**: closes Gate A NEW-3 (rev 1 undercounted the cost as "one strand dispatch
per read").

### D8 — No public API change; L-019-3 lift is CONTINGENT

**Decision**: all changes remain engine-internal (two strands, binding, stop ordering,
`stopped_` atomic). No `c_api.h`, no public `Engine`/`Session` signature change. **The
L-019-3 lift is gated on BOTH domains landing with passing TSan witnesses** (the
per-session domain AND the control-plane domain) — lifting it while either is racy would
publish a false safety claim (Gate A NEW-5). Verified via `nm` (no new exports) + `abidiff`
(no-diff).

## Reference-engine grounding (corrected)

QuickFIX-cpp/J (`ThreadedSocketAcceptor`)/Fix8 use thread-per-session + per-session locks
**and** serialize their engine/acceptor-global structures (session dictionaries, acceptor
maps) under their own locks. fixpp's per-session strand is the analogue of the former; the
**engine control strand (D0) is the analogue of the latter**. Gate A corrected the rev-1
"no divergence" read: the references DO serialize the global registry — which *reinforces*
the need for D0. No reference engine leaves the global session map racing a shutdown over a
bare shared executor (the rev-1 bug).

## Risks & mitigations

- **R1 — two-hop send deadlock/livelock** (NEW-1) → every hop is non-blocking `post`;
  mitigated by an MT re-entrant-send (`session→control→session`) TSan witness (V-9).
- **R2 — a transport ctor silently samples bare `exec_`** (D5) → debug assert
  `socket().get_executor() == session_strand` + the four-site audit (V-…).
- **R3 — control-strand hop on the hot send path costs > ±5%** (D7) → bench gate; fallback
  is the lock-free registry-snapshot alternative.
- **R4 — half-restructure risk** (the reason Gate A ruled structural) → D0 enumerates ALL
  control-plane state + publication points (D-PUB) + `stopped_` (D-STOP); the data-model
  E-0 and contract C-0 make the control domain a first-class modeled entity, not a bolt-on.
