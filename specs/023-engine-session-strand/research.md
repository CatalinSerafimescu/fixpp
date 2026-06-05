# Research: Per-Session + Control-Plane Strand Binding for the Engine

**Feature**: 023-engine-session-strand
**Date**: 2026-06-05 (rev 4 — Gate A round-3 D-SNAP fixes: bounded handle + pinned primitive + stop-before-publish)
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
A second strand is the asio-idiomatic, mutex-free way to serialize the control plane,
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

### D-PUB — Publication discipline for `session` / `live_transport` (AWAITED, ordered, join-accounted)

**Decision**: `entry.session` and `entry.live_transport` (the handles `stop()` reads) are
published **on the control strand**, and the role loop **awaits** that publication
**before** it enters the read pump:

```
co_await asio::co_spawn(control_strand, publish_handles, asio::use_awaitable);
// ↑ suspends the session-strand role loop (does NOT block a thread); the control strand
//   runs publish_handles, then resumes the loop. Two distinct strands over one context →
//   deadlock-free. Only AFTER this resumes does the loop enter async_read_some.
```

This gives **ordering**, not just well-defined access: `entry.live_transport` is visible
to any subsequent `stop()` on the control strand *before* the read pump starts, so `stop()`
can never observe `live_transport == nullptr` (engine.cpp:713-714), skip the close-to-wake,
and then have a later queued publish expose a live transport after the stop sequence began
— the missed-close that reintroduces the BIO hang this feature exists to fix.

- **Awaited, not fire-and-forget**: a detached publish is unsafe (the ordering hole above)
  and a lifetime hazard (a detached job capturing `SessionEntry&` can run after the entry is
  cleared). The publish job is **part of the outstanding-loop join accounting** (the stop
  join must not complete while a publish job is pending).
- **Unpublish on loop exit**: on role-loop exit the loop resets `entry.session` /
  `entry.live_transport` (to null/expired) **on the control strand**, before the entry can
  be cleared, so a concurrent `stop()` never reads a stale handle for a loop that has ended.
- **Snapshot republish**: each publish/unpublish on the control strand also refreshes the
  D-SNAP immutable reader snapshot, so the public readers see the new state.
- **Stop-already-in-progress (the symmetric ordering hole — Gate A round-3)**: the awaited
  publish closes the *publish-before-stop* race but NOT the *stop-before-publish* one. If
  `stop()` is already running on the control strand when the role loop reaches the awaited
  publish — `stop()` has set `stopped_`, emitted cancellation, iterated `registry_`, seen
  `live_transport == nullptr` — a naive publish would still expose a live transport for
  pumping *after* the stop sequence began. **Contract (pinned)**: the publish coroutine runs
  on the control strand and **checks `stopped_` first**; if `stopped_` is already true it MUST
  **NOT** publish a live transport for normal pumping — instead it records a **stopped
  disposition** (and leaves `live_transport == nullptr` / closes the freshly-created transport)
  and makes the session-strand role loop **close/return before any read is initiated** (the
  loop observes the stopped disposition the awaited publish returns and never enters
  `async_read_some`). So a transport created just before the awaited publish is never pumped
  once `stop()` has begun, and `stop()` never has a late publish expose a live transport behind
  its back. Verified by **V-12** (`stop()` racing exactly between transport creation and the
  awaited publish).

**The atomic-`live_transport` alternative is struck** (was offered in rev-2 as co-equal): an
atomic `live_transport` makes the *read* well-defined but does **not** provide the *ordering*
(`stop()` could still atomically acquire `nullptr` and skip the close). Presenting it as
equivalent invites an implementer to pick the broken half. The awaited control-strand publish
is mandatory; an atomic form is acceptable only for `stopped_` (D-STOP), where there is no
ordering obligation beyond the flag itself.

**Rationale**: closes Gate A round-2 Codex-#2 (P1) + NEW-3 / Root cause B (torn read AND
missed-close ordering of the published handles across domains); the stop-already-in-progress
clause + V-12 close Gate A round-3 Codex-#3 (the symmetric stop-before-publish hole).

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

### D-SNAP — Atomic immutable snapshot for the synchronous public readers (NEW; Gate A round-2 user decision; primitive pinned round-3)

**Decision**: the synchronous public readers `lookup()` (engine.cpp:117-119) and
`acceptor_bound_endpoint()` (engine.cpp:896-899) read control-plane `unordered_map`s
**off any strand**, racing `stop()`'s `registry_.clear()` / `listener_endpoints_.clear()`
(engine.cpp:761-763) under MT — the exact `unordered_map` concurrent-read/clear +
rehash-UAF class D0 exists to kill, reappearing on the public read path. A synchronous
accessor cannot enter the async control strand without an API change, a block (banned,
Art. XV), or a published snapshot. The user chose **Full MT-safe (accept the API change)**:

- After **every** control-plane mutation, the control strand **atomically publishes**
  an **immutable snapshot** (RCU-style) of the state the readers need: the registry's
  `SessionId → shared_ptr<Session>` map and the bound-endpoint table. Publish = build
  a fresh immutable structure, then `atomic_store` a `shared_ptr<const Snapshot>` to it.
- Public readers **`atomic_load`** the current snapshot (no strand entry, no `std::mutex`,
  no block) and return a value (`acceptor_bound_endpoint()` → `Endpoint` by value — no
  signature change) or a shared-ownership handle (`lookup()` → `std::shared_ptr<Session>`,
  drawn from the snapshot's `shared_ptr<Session>` map, so the handle outlives a concurrent
  `registry_.clear()` while the `Engine` is alive — bounded handle, FR-008/FR-014).
- **`lookup()`'s return type changes** from raw `Session*` to `std::shared_ptr<Session>`
  — the single deliberate, accepted, safening-only public API change (FR-008/SC-004).
  `acceptor_bound_endpoint()` keeps its `Endpoint`-by-value signature.

**Primitive (pinned — standard `std::atomic<std::shared_ptr<const Snapshot>>`)**: the
publication primitive is the **standard-library** `std::atomic<std::shared_ptr<const Snapshot>>`
(C++20, header-only, no `std::mutex`) — `atomic_store` to publish, `atomic_load` to read. It is
deliberately NOT the project's unshipped `fixpp::sync::atomic_shared_ptr` (NFR-017,
`spec/feature-catalogue.md:288` — backlog, "its OWN spec + own Gate A"), whose `std::mutex`-sharded
fallback (`../atomic-shared-ptr/.../atomic_shared_ptr.hpp`) **cannot** be included into `engine.hpp`,
which includes `asio::awaitable` (Art. XI §3 / Art. XV ban plain `std::mutex` in awaitable-including
headers, `.specify/constitution.md:146-149,217`). The design does NOT depend on NFR-017 landing;
NFR-017 is only a possible **later optimization** if the bench gate (V-6) shows the standard
primitive's read cost is material.

**Consistency with Art. XV (no `std::mutex` in our headers)**: `std::atomic<std::shared_ptr<…>>`
is an STL primitive — it introduces **no `std::mutex` into our headers** regardless of the STL's
internal lock strategy. Read cost: it is a **wait-free read on STLs where
`std::atomic<shared_ptr>` is lock-free** (a runtime `is_lock_free()` probe); otherwise the STL uses
**standard-library-internal synchronization that is NOT a `std::mutex` in our code/headers**, so the
absolute "lock-free / no lock" claim is dropped — the read is non-blocking from the caller's view but
not guaranteed lock-free on every STL. The perf gate (V-6) measures the real read/publish cost on
the supported STL matrix.

**Rationale**: closes Gate A round-2 Codex-#1 / Root cause A; reconciles FR-011's "ALL …
on the control strand" to **mutate on the strand, read via the published snapshot**. The
flagship MT harness already confined these to quiescent windows, but the quickstart invites
any-thread calls — the snapshot makes that safe rather than a documented foot-gun.

**`stopped()` is NOT part of this snapshot** — it is covered by D-STOP's `atomic<bool>`
(an atomic-acquire load is MT-safe with no API change). Over-bundling it here is wrong.

**Alternative considered**: *document-narrow* `lookup()`/`acceptor_bound_endpoint()` to
pre-`start()`/post-`stop()`/quiescent-only (preserves all signatures). Rejected by the
user in favor of full MT-safe readers — the narrowing would weaken the de-facto contract
of two shipped methods, and the snapshot cost is once-per-control-mutation (cold path),
not per-read.

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

**The adoption seam is an engine-only tag/path, NOT inference from the public
`already_serialized_executor` flag** (D3-B, Gate A round-2 Codex-#3). The public
`already_serialized_executor` flag is consulted **only** on the `direct_executor` arm
(session_executor.cpp:40); under `per_session_strand` the factory still
`make_strand`-wraps unconditionally (session_executor.cpp:35). A user may legitimately set
`already_serialized_executor` under `per_session_strand` today — so reusing that public flag
to trigger adoption would be a trap that silently adopts a **bare** (non-strand) executor as
if it were a strand, bypassing serialization. Instead, the engine passes the pre-made strand
through an **engine-only internal adoption parameter/overload** (e.g. an internal
`adopt_strand` tag distinct from the public config), so only the engine-created strand is
adopted; the ordinary user `per_session_strand` path is untouched and still `make_strand`-wraps.

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
the RED witness targets the **engine control-plane data race** (Root cause #1), not the BIO
crash, via a **one-sided park** seam — **never a bidirectional latch** (Gate A round-2
Codex-#4 / Root cause C):

- **Why a latch is self-defeating**: a *data race* is by definition two conflicting accesses
  with **no** happens-before edge between them. A two-sided latch (the writer signals,
  `stop()` waits) **creates** an HB edge across the two accesses → TSan sees synchronization
  → **no race is reported**. The naive "park the write, signal a latch, stop waits on it"
  seam suppresses the very race it must witness. (Fire the latch *before* the write and you
  instead get an ordering bug, not a concurrent race.)
- **The one-sided park that works**: a test-only seam (compiled under a `FIXPP_TEST_SEAMS`-style
  debug flag, zero release cost) parks **one** thread *inside / immediately adjacent to* an
  unsynchronized listener/endpoint-table access — e.g. a `std::this_thread::sleep`/spin
  **after** the map write begins but **before** it releases — while another thread drives
  `Engine::stop()`'s `clear()`, **with no shared synchronization object between them**. The
  window is wide enough that the conflicting access always lands inside it, so TSan flags the
  two unsynchronized accesses deterministically, **and there is no HB edge** (a one-sided
  delay, not a two-sided latch). This is the `feedback_stack_use_after_return_local_vs_ci_flake`
  "widen the window" technique applied to a map race.
- **Publication point — the listener/endpoint table, not `entry.session`**: the
  `listener_endpoints_` / `listeners_` write is reachable **before any peer connects** (so the
  witness needs no live peer), whereas an `entry.session` publication requires an established
  session. The witness targets the **listener-map write** for reachability.
- **SC-002 standard**: this is deterministic in the sense that matters — **every** pre-change
  run is RED — and is *not* the rejected flaky/probabilistic stress reproducer. SC-002 is
  worded as "a controlled one-sided-park interleave that reliably yields a TSan data-race
  report pre-change and is clean post-change," which is exactly what the seam can deliver
  (it does NOT claim a two-sided-latch guarantee the seam cannot provide).

The TLS-teardown BIO crash is a **downstream symptom** covered by the multi-threaded
acceptance test (SC-001 / the reused `business_messages_roundtrip` 3-thread harness).

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
design. If the control-strand hop proves hot, the D0-alternative (a registry
snapshot for `send`) is the fallback — but as a measured decision, not a guess.

**Rationale**: closes Gate A NEW-3 (rev 1 undercounted the cost as "one strand dispatch
per read").

### D8 — One safening-only API change; L-019-3 lift is CONTINGENT

**Decision**: all changes remain engine-internal (two strands, binding, stop ordering,
`stopped_` atomic, D-SNAP reader snapshot) **except** the single deliberate, accepted,
safening-only public signature change `Engine::lookup() : Session* → std::shared_ptr<Session>`
(D-SNAP / FR-008 / SC-004). No `c_api.h` change; `acceptor_bound_endpoint()` keeps its
`Endpoint`-by-value signature; no other public `Engine`/`Session` signature changes.

**The L-019-3 lift is gated on the FULL witness set** — V-1, V-2 (per-session
teardown/lifecycle), V-10 (transport-on-strand identity), V-8 (control-plane race), V-9
(re-entrant send), **V-11** (snapshot-reader MT), **V-12** (publish-vs-stop ordering) — **and** a clean ASan/UBSan/TSan run.
Lifting it while any of those is still racy or failing publishes a false safety claim (Gate A
round-1 NEW-5 + round-2 Codex-#5; the `feedback_completeness_gate_exact_set_not_subset` class).
Verified via `nm` + `abidiff` — which **will** show the one intended `lookup()` return-type
change (expected and recorded, not a silent break — SC-004), and no other diff.

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
  is the registry-snapshot alternative.
- **R4 — half-restructure risk** (the reason Gate A ruled structural) → D0 enumerates ALL
  control-plane state + publication points (D-PUB) + `stopped_` (D-STOP) + the synchronous
  public readers (D-SNAP); the data-model E-0 and contract C-0 make the control domain a
  first-class modeled entity, not a bolt-on.
- **R5 — a public-reader snapshot grows stale or races its own publish** (D-SNAP) → the
  control strand republishes the immutable snapshot after **every** control-plane mutation
  (including D-PUB publish/unpublish); readers `atomic_load` it. Witnessed by V-11 (concurrent
  `lookup()`/`acceptor_bound_endpoint()` from a thread while the engine runs → TSan-clean).
- **R6 — an awaited publish deadlocks the role loop** (D-PUB) → the publish runs on a
  **distinct** strand over the same context and the loop **suspends** (does not block a
  thread); witnessed by V-2 lifecycle + the absence of any thread held across the hop.
- **R7 — implementer must NOT unify the two lifetime mechanisms** (Gate A final / Opus) → the
  same `Session`-borrows-`EngineConfig&` hazard is guarded by **two intentionally separate**
  mechanisms: engine-internal `Engine::send` uses a **hard runtime barrier** (`send_counter_`
  drained before `registry_.clear()`); the app-facing `lookup()` handle uses **only** a debug
  assert + caller obligation (the bounded handle — NO `stop()` drain). Do not "unify the
  keepalive": draining on app-held leases would hang `stop()`; weakening the send barrier into
  an assert would re-open the send-path UAF. Keep them separate (note for `/tasks`).
- **R8 — D5 "no ctor samples bare `exec_`" is the silent lynchpin** (Gate A final / Opus) → a
  single un-fixed transport/listener ctor at ANY of the four audited sites silently re-opens the
  per-session race with **no compile-time signal**. Fix all four (half-restructure precedent) and
  run the **V-10** `socket().get_executor() == session_strand` assertion at every site under TSan.
