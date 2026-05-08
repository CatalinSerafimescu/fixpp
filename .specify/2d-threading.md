# Design Doc 2d — Application Threading Contract & `fixpp::core::Clock`

> **Status:** Draft v0.4 — Gate A round 3 converged (post-cap line-edit pass — 2c precedent)
> **Date:** 2026-05-08
> **Convergence-log pointer:** addresses Codex round-3 review (1 P1 / 0 P2 / 0 P3) and Opus round-3 adversarial review (combined post-judging 1 P1 / 0 P2 / 0 P3; 1 root cause), see Appendix C round 3 entry.
> **Owner:** `fixpp::core` (`include/fixpp/core/clock.hpp`, `include/fixpp/core/engine_config.hpp`, `include/fixpp/core/session_executor.hpp`, `include/fixpp/core/session_local.hpp`, `include/fixpp/core/trace_context.hpp`); `fixpp::session` (`include/fixpp/session/session_config.hpp`); test surface co-owned with `tests/support/` (`include/fixpp/core/test/mock_clock.hpp`).
> **Inherits:** `[arch §1.1] Goals` (pluggable-clocks promise), `[arch §4.1] core` (core surface — `Clock` + `current_trace_context`), `[arch §4.4] session` (session surface — `SessionConfig` field list, **Threading default (locked)** paragraph), `[arch §5.1] Executor model` (`any_io_executor`, per-session strand, ASIO cancellation slots), `[arch §5.3] Error model` (no exceptions hot path), `[arch §5.4] Trace context` (strand-stored, not `thread_local`), `[arch §5.6] Configuration shape` (frozen-config rule + close-and-reopen), `[arch §5.7] Logging` (logging integration), `[arch §5.8] Backpressure` (backpressure), `[arch §6] Plugin Pattern` (plugin pattern + ≤5 pure-virtual cap), `[arch §10] Hand-off to Design Docs 2a–2m` row 2d (scope spans §5.1 + §4.1 + §4.4 + §6), `[arch §11] Open Architectural Questions` row 7 (NFR-015 row to be added by this doc).
> **Cites:** `[const §I.1]` (codegen-vs-runtime split — referenced only to confirm 2d does not touch the version surface), `[const §VI.5]` (exact-citation rule), `[const §VII]` (testing — every plugin needs a mock + test seam), `[const §VIII.5]` (zero allocation between parse and `fromApp`), `[const §X.4]` (out-of-range C-ABI code mapping), `[const §XI.1]` (`asio::awaitable<T>` composition), `[const §XI.2]` (ASIO native cancellation slots), `[const §XI.3]` (awaitable mutex required), `[const §XI.4]` (per-session strand default), `[const §XI.6]` (HALO-first frame allocation), `[const §XIII]` (observability), `[const §XIII.3]` (strand-stored trace context, no `thread_local`), `[const §XIV.2]` (≤5 pure-virtual on plugin interfaces), `[const §XV.15]` (banned `drop-oldest` on app/session message paths), `[const §XVII.1]` (Codex Gate A before `/tasks`), `[SYN §3.2 Q6a]` (ASIO native cancellation, DECIDED), `[SYN §3.2 Q6b]` (awaitable-mutex shape, DECIDED, owned by 2f — quoted for executor-compat surface), `[SYN §3.2 Q6c]` (application threading contract, DECIDED — option 3 with default per-session strand), `[SYN §5 row 4]` (priority pass — application threading contract unblocks C ABI shape).
> **Sibling docs:** `2a-decimal.md` v0.3 (Tier 1 latency-ceiling idiom; PMR-allocates-but-`expected_t`-fallible pattern), `2b-wire.md` v0.2 (`MessageView::get<Tag>() → expected_t<field_view>`; parse-completion executor handoff at `[2b §6.6]`/`[2b §7.3]`), `2c-codegen.md` v1.3 (`dict::reify` → typed-message handoff; `[arch §5.6]` mid-session-swap rejection — no swap of dictionary mid-session).
> **Catalogue rows owned:** **NFR-015** — pluggable Clock interface (NEW row; to be added to `library/spec/feature-catalogue.md` and indexed in `library/spec/coverage-index.md` at sign-off per §11 — the orchestrator applies the drop-in amendment per the precedent in `[2c App D]`; 2d itself ships only Appendices A, B, C unless §11 needs a constitutional amendment).
> **Catalogue rows touched (not owned) — clock-seam input only, no row discharge claimed by 2d (per C-P2-6 / Opus confirm):** **S-035** (session scheduling — owned by the session-module Phase-4 spec; consumes `Clock::sleep_until` via this doc's seam), **S-003 / S-004** (heartbeat — owned by the session-module Phase-4 spec; the timing seam is `effective_clock.steady_now()` + `effective_clock.sleep_until(...)`), **LOG-001..004** + **OBS-001..003** (owned by **2k**; this doc supplies the `effective_clock.now()` source for session-scoped records and `EngineConfig::clock->now()` for engine-scope records, with the `clock_scope` discriminator at §7.9 / §6.7).
> **Convergence log:** see Appendix C — Round 1 (v0.1 → v0.2) populated below.

---

## 1. Goals

1. Lock the **application threading contract** (`[SYN §3.2 Q6c]` decision: option 3 with default per-session strand). Every `Session` runs on a `strand` derived from a user-supplied `asio::any_io_executor`; the engine never picks a concrete executor. Application callbacks (`onLogon`, `onLogout`, `toAdmin`, `fromAdmin`, `toApp`, `fromApp`) dispatch onto that strand by default per `[const §XI.4]`.
2. Lock the **executor opt-out**: a user who supplies their own `asio::any_io_executor` *and* explicitly asks `SessionConfig::threading_mode = direct_executor` runs callbacks directly on that executor without an extra `make_strand` wrap. The opt-out is **not** a delegation of engine-internal serialisation to the user — the user must attest at construction that the supplied executor is already per-session-serialised (`SessionConfig::already_serialized_executor = true`); construction otherwise rejects with `error::executor_not_serialised` (see §6.1, §6.7). The engine continues to treat the supplied executor as a serialisation domain (no `make_strand` wrap, no double-serialisation cost) — the user-attested property is what makes the opt-out safe vis-à-vis `[const §XI.4]`.
3. Lock the **`fixpp::core::Clock` plugin interface** at exactly 4 pure-virtual methods (`now`, `steady_now`, `sleep_until`, `cancel_sleeps`) — well within the `[const §XIV.2]` ≤5 cap. One default impl (`fixpp::core::system_clock_source`) wraps `std::chrono::system_clock` + `std::chrono::steady_clock` + ASIO `steady_timer`. One mock impl (`fixpp::core::mock_clock`) ships in a public test header so user tests can step time deterministically.
4. Lock the **`EngineConfig` / `SessionConfig` split**, applying a single uniform "engine-anchor + session-`*_override` nullable" pattern across the dictionary, executor, and clock axes (per `[arch §5.6]`'s "session inherits from engine unless overridden" rule). `EngineConfig` carries engine-level shared resources: executor pool, allocator factories, **`Clock` source**, **`dict::version_registry` constructed from `EngineConfig::dictionaries`** (per `[2c §4.9]` and `[2c §10] Q10` deferral to this doc), default log/OTel providers, default plugin selections. `SessionConfig` carries session-level frozen-at-open knobs as overrides: `executor_override`, `threading_mode`, `already_serialized_executor`, `lock_policy`, `clock_override`, `dictionary` (the session's anchor for non-FIXT.1.1 sessions), `dialect_overlay`, `security_profile`, `store_factory`, `cert_source` override, tap consumer, log/OTel hooks, PMR arenas, value-typed `initial_trace_context` (replacing the v0.1 `std::function`-based `trace_context_provider` per C-P2-4). The single-effective-clock rule (§7.9): `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock`, and every session-scoped consumer (heartbeat, SendingTime, S-035 schedule, session-scoped LOG/OBS records) reads from `effective_clock`; engine-scope LOG/OBS records read directly from `EngineConfig::clock` and carry a `clock_scope = engine` discriminator. `clock_not_set` is a hard invariant at `Engine::open` regardless of whether sessions override.
5. Lock **ASIO native cancellation slot propagation** end-to-end (`[SYN §3.2 Q6a]`) under a **two-phase close model** (root cause #1, §6.5):
   - **Phase 1 — graceful close.** `Session::close()` opens a *child* `asio::cancellation_state` (the "graceful state") composed below the root, attempts a FIX `Logout` exchange under that child state, and times the exchange out via `Clock::sleep_until` bound to the same child state. The root remains uncancelled during phase 1; in-flight transport reads, heartbeats, store waiters, and dispatch continue to run.
   - **Phase 2 — teardown.** When phase 1 resolves (Logout ACK observed, child timeout fires, or the child state is itself cancelled), the root cancellation slot fires `asio::cancellation_type::total`, propagating to in-flight transport `async_read` / `async_write`, the heartbeat `Clock::sleep_until`, the awaitable-mutex acquire (executor-compat surface owed by 2f), the application-callback dispatch (via `cancellable_dispatch` — §6.5), and the parser → `fromApp` chain.
   - `Session::close(terminal)` skips phase 1 and goes directly to phase 2. `partial` is dropped from the v1.0 public surface (root cause #1 / N-P1-3 resolution; per §4.7 enumeration table). The C ABI's `fixpp_session_close()` triggers phase 1 (i.e., the same `Session::close()` entry point).
6. Lock the **session-domain-stored `trace_context`** awaitable (`[const §XIII.3]` + `[arch §5.4]`): `co_await fixpp::current_trace_context` returns the value bound to the current session serialisation domain. The storage mechanism is **a published `fixpp::core::session_local<T>` slot owned by the `Session` instance** (root cause #3, §4.6 — renamed from v0.2's `strand_local<T>` per round 2 root cause #1 because the keying axis is the session serialisation domain, which subsumes both `per_session_strand`'s strand and `direct_executor`'s attested executor) — *not* a `void*` carried in `asio::any_io_executor::query` over a type-erased executor (rejected per C-P1-5; `query`-on-opaque-property is not a published contract on the type-erased executor and does not survive `make_strand` / `bind_executor` decoration). The access mechanism is a project-typed `session_ptr()` member-function accessor on the project-owned `fixpp::core::session_executor` value-typed wrapper class (§4.8 — round 3 root cause #1: the wrapper publishes the typed `Session*` accessor as a public member function, not via ASIO's property-query pipeline, so the typed `Session*` survives `bind_executor` / `make_strand` decoration without depending on `asio::any_io_executor`'s closed property set). `thread_local` is forbidden because coroutines may resume on a different thread. Outside session scope (listener accept, control-plane handlers), the wrapper accessor is unreachable (the awaiter's bound executor is the user-supplied `asio::any_io_executor`, not a `session_executor` wrapper) and the awaiter falls back to a per-engine atomic snapshot of `EngineConfig::engine_trace_context` (§4.4 / §7.8 / N-P2-2 resolution).
7. Lock the **threading-wired `Clock`** so heartbeat (S-003 / S-004), SendingTime, **session-scoped** LOG/OBS records (i.e., LOG-001..004 + OBS-001..003 records produced under a session strand), and S-035 session scheduling all flow through the **same per-session `effective_clock`** — and so `mock_clock` (when set as `clock_override`) deterministically steps every session-scoped consumer at once in tests. Engine-scope LOG/OBS records (listener accept, control-plane) come from `EngineConfig::clock` directly. NFR-015's discharge claim is bounded to "the clock seam"; the row owners (the session-module Phase-4 spec for S-003/S-004/S-035, **2k** for LOG/OBS) consume the seam (§7.9, Appendix A.2 — re-scoped per C-P2-6).
8. Stay zero-allocation on the dispatch hot path `[const §VIII.5]`: the strand-bound `dispatch` of a parsed `MessageView` to `fromApp` allocates zero bytes when HALO fires; PMR fallback per `[const §XI.6]` when it does not.
9. Stay exception-free across the parse → `fromApp` window `[arch §5.3]`. Cancellation propagates as `asio::error::operation_aborted`, mapped at the C ABI to a dedicated `fixpp_error_t::CANCELLED` per `[const §X.4]`.

### 1.1 Scope boundary — what 2d controls vs what 2e/2f/2g/2h/2i/2j/2k/2l/2m control

2d **owns**: the per-session strand discipline; `EngineConfig` / `SessionConfig` split; `Clock` interface, default impl, mock impl; the cancellation contract on `Session::close()`; the strand-stored `trace_context` awaitable; the executor-compat surface that 2f's awaitable mutex must respect; the threading hot-path allocation budget.

2d **does not own**:

- The awaitable-mutex *internals* (own implementation in `fixpp::sync` per `[SYN §3.2 Q6b]`) — owned by **2f**. 2d only specifies the executor-compat surface and the cancellation-slot propagation rule.
- The session FSM state machine (Logon, gap-fill, ResendRequest, sequence reset) — owned by **2e** (MessageStore async API) and the session-module Phase-4 spec.
- The `MessageStore` plugin interface details — owned by **2e**. 2d only specifies that `MessageStore` ops dispatch on the session strand by default.
- TLS handshake mechanics, cert rotation timing — owned by **2g**. 2d only specifies that handshake coroutines run on the session strand and respect cancellation.
- Transport `async_connect` / `async_read_some` / `async_write` — owned by **2h**. 2d only specifies that transport completions land on the session strand.
- The C ABI cancellation-token representation — owned by **2i**. 2d specifies that `fixpp_session_close()` flips the cancellation slot; 2i decides whether the slot leaks any opaque token shape across the boundary.
- The control-plane interface — owned by **2j**. 2d specifies that control-plane handlers use `co_await fixpp::current_trace_context` outside session scope (the engine-level fallback context).
- The async logger producer/consumer split + sink interface + OTel exporter wiring — owned by **2k**. 2d specifies that LOG / OTel timestamps come from the engine `Clock`, not from `std::chrono::system_clock` directly.
- The session-tap ring buffer — owned by **2l**. 2d specifies that `drop-oldest` is permitted on the *tap* path (a telemetry path under `[const §XIII.2]`) and banned on the *app/session message* path per `[const §XV.15]`.
- The SWIG / Python binding shape — owned by **2m**.

### 1.2 Magnitude domain — what the threading contract is sized for

- **Executor model.** One `asio::any_io_executor` per engine (typically a thread pool of 1..N threads, where N = vCPU count for the deployment); one `strand` per session derived from that executor. The engine does not assume a concrete executor type and works under `asio::thread_pool::executor_type`, `asio::system_executor`, `asio::io_context::executor_type`, and any custom executor that satisfies the ASIO `executor` concept.
- **Strand cost.** `asio::strand<asio::any_io_executor>` is a small handle (typically two pointers) plus a per-strand atomic counter; ~24..32 bytes per session. For 10⁴ concurrent sessions this is ~250..320 KiB of strand bookkeeping — bounded and predictable.
- **`Clock::sleep_until` waiters.** One `asio::steady_timer` per session-scoped sleep (heartbeat, S-035 schedule). At most O(1) outstanding `sleep_until` per session at any point under the v1.0 session FSM (heartbeat is one timer; S-035 schedule is one timer; SendingTime is read-only). Worst case O(2) per session.
- **`trace_context` slot.** One `fixpp::otel::trace_context` per strand, ~32 bytes (trace_id 16 B + span_id 8 B + flags 1 B + padding). Per-strand, not per-coroutine-frame, because callbacks within a session share the parent span.
- **Cancellation propagation.** One root cancellation slot per session; one child slot per outstanding async op (parser, transport read/write, store write, heartbeat, sleep). Slot count is O(active-coroutine-count); typical session has ≤ 6 in flight (transport read, transport write, parse, app-callback dispatch, heartbeat, optional store write).

These bounds let the threading contract be analysed for memory and DoS surface independently of the FIX dictionary or message rates.

---

## 2. Non-goals

- **No version-surface decisions.** `[const §I.1]` v0.2 codegen-vs-runtime-XML split is sibling-doc-owned (**2c**); 2d does not edit it. The `Clock` is FIX-version-agnostic.
- **No wire format / parser / framer / writer / validator surface.** Owned by **2b** (`[arch §4.3]`). 2d does not touch `Parser`, `Writer`, `OffsetTable`, `Validator`, or `Framer`.
- **No dictionary or codegen surface.** Owned by **2c** (`[arch §4.2]`). 2d does not edit `dict::Dictionary`, `dict::FieldRef`, `dict::reify`, or any generated `fixpp::vXX::*` typed-message class. Dictionary mid-session swap is rejected categorically per `[2c §7.2]` / `[arch §5.6]`; 2d records but does not re-decide that.
- **No `MessageStore` API shape.** Owned by **2e**. 2d locks only that store ops run on the strand by default and that `MessageStore::write(...)` returns `asio::awaitable<void>` per `[arch §4.4]`.
- **No awaitable-mutex internals.** Owned by **2f**. 2d quotes the six-item design list from `[SYN §3.2 Q6b]` and locks the executor-compat / cancellation-compat surface 2f must satisfy.
- **No C ABI surface.** Owned by **2i**. 2d enumerates which threading concepts cross the C ABI but does not pick the symbol shapes.
- **No TLS cert handling, transport reset semantics, or pinset rotation timing.** Owned by **2g** / **2h**. 2d specifies only the strand-discipline these subsystems consume.
- **No log producer/consumer machinery, sink interface, OTel exporter wiring, or quill-vs-own benchmark.** Owned by **2k**. 2d locks only the `Clock` source feeding the timestamps.
- **No tap ring buffer mechanics or iceoryx2 publishing.** Owned by **2l**.
- **No SWIG/Python binding.** Owned by **2m**.

---

## 3. Inherited surface

### 3.1 From `[arch §1.1]`

> Design seams for testability. Every interface that touches the outside world (sockets, disks, **clocks**, OTel exporters) is pluggable so the session FSM and parser can be unit-tested without real I/O `[const §VII]` `[SYN §3.4 Q16]`.

This doc delivers the clock seam.

### 3.2 From `[arch §4.1]` (core surface)

> `fixpp::core::Clock` — pluggable timing source. ≤5 pure-virtual: `now()` (UTC), `steady_now()` (monotonic), `sleep_until(...)` (awaitable), `cancel_sleeps()`. Default impl `fixpp::core::system_clock_source` wraps `std::chrono::system_clock` + `std::chrono::steady_clock` + ASIO `steady_timer`. Required test seam for heartbeat timers, SendingTime threshold checks, session scheduling (S-035), and log/OTel timestamps (`[arch §1.1]` promised pluggable clocks; this row delivers it). Owned by **2d** (timing belongs to the threading contract).

> `fixpp::current_trace_context` — strand-stored `trace_context` accessor `[const §XIII.3]`.

This doc refines (but does not diverge from) both rows.

### 3.3 From `[arch §4.4] session` — **Threading default (locked)** paragraph

> **Threading default (locked):** every `Session` runs on a `strand` derived from a user-supplied executor. Application callbacks dispatch onto that strand by default `[const §XI.4]` `[SYN §3.2 Q6c]`. Owned by **2d**.

This doc spells the contract.

### 3.4 From `[arch §5.1]` (executor model)

> - **Primitive:** `asio::any_io_executor`. The engine never picks a concrete executor; users pass one in.
> - **Per-session strand.** Construction wraps the user executor in `asio::make_strand(...)` unless the user supplies an explicit `executor` opt-out in `SessionConfig`. Application callbacks always dispatch onto the strand `[const §XI.4]`.
> - **Coroutine composition.** `asio::awaitable<T>` is the return type of every async session/transport entry point `[const §XI.1]`. Cancellation flows via ASIO native cancellation slots `[const §XI.2]`.
> - **No `co_await` of `std::mutex`.** `fixpp::sync::async_mutex` is the only legal coroutine mutex `[const §XI.3]`.
> - **HALO-first frame allocation.** The engine does not pin a compiler version. Where HALO doesn't fire on the inbound dispatch path, a per-awaiter override constructs the promise on the per-session PMR arena `[const §XI.6]` `[SYN §3.2 Q6]`.

This doc fixes the public API around all five bullets.

### 3.5 From `[arch §5.4]` (trace context)

> - **Storage:** `SessionConfig.trace_context_provider`, called once at session open, returns a `fixpp::otel::trace_context` stored on the session strand.
> - **Access:** `co_await fixpp::current_trace_context` returns the value bound to the current strand; outside session scope, returns the `Engine`-level fallback context `[const §XIII.3]`.
> - **`thread_local` is prohibited.** A coroutine that suspends on thread A may resume on thread B; a `thread_local` write made before suspension is not guaranteed visible after resume. Hence the strand-stored awaitable.

This doc operationalises the awaitable.

### 3.6 From `[arch §5.6]` (frozen config + close-and-reopen)

> **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay. The supported pattern for any of these is close-and-reopen the session. … **`EngineConfig`** sits one level up: shared executors, allocator factories, log/otel providers, the **`Clock` source** (§4.1, §6), and default plugin selections. `SessionConfig` inherits the `Clock` from its `EngineConfig` unless overridden.

This doc fixes the field list of both `EngineConfig` and `SessionConfig`.

### 3.7 From `[arch §6]` (plugin pattern, ≤5 pure-virtual)

> **≤5 pure-virtual methods** `[const §XIV.2]`. Larger surfaces require a one-paragraph justification reviewed at Gate A.
>
> | Interface | Default impl | Design doc | Notes |
> |---|---|---|---|
> | `fixpp::core::Clock` | `system_clock_source` | **2d** | Test seam for heartbeat / SendingTime / session scheduling / log + OTel timestamps. Carried by `EngineConfig` (§5.6); mock impl steps time deterministically in tests. |

`Clock` has exactly 4 pure-virtual methods (§4.1) — under the cap.

### 3.8 From `[arch §10]` row 2d (handoff)

> **2d** — Application threading contract — Default per-session strand, executor opt-out, callback dispatch; **`fixpp::core::Clock` interface** + default `system_clock_source` + mock impl + threading through `EngineConfig`/`SessionConfig` — cross-cutting hooks: §5.1 executor model; §4.1 core; §4.4 session; §6 plugin pattern.

### 3.9 From `[arch §11]` row 7 (open architectural questions)

> Add catalogue row `NFR-015 — pluggable Clock interface` to `feature-catalogue.md`. Owner: **2d**. Disposition: TODO — added when 2d lands; tracked here so the spine doesn't claim the row exists yet.

This doc claims NFR-015 in Appendix A and queues the catalogue + coverage-index amendments in §11 (drop-in language). 2d ships an Appendix D with one drop-in amendment for `[arch §5.4]` (the trace-context-storage rename surfaced by C-P2-4 / root cause #3); the orchestrator applies that and the §11 amendments at sign-off, following the `[2c App D]` precedent (the rewrite agent does not edit `architecture.md` / `feature-catalogue.md` / `coverage-index.md` directly).

### 3.10 From `[const §XI]` (concurrency)

> 1. **C++20/23 coroutines (`asio::awaitable<T>`) are the session/transport composition primitive.** …
> 2. **Cancellation: ASIO native cancellation slots end-to-end.** No parallel `stop_token` abstraction. `fixpp_session_close()` from the C ABI signals the cancellation slot.
> 3. **Awaitable mutex required in coroutine context.** `fixpp::sync::async_mutex` … is the only allowed mutex shape for coroutines. **Plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`.**
> 4. **Application threading default: per-session strand.**
> 5. **Hot-path lock policy: per-session policy with hard-coded callsite caps.** Default = mutex. Spin opt-in via session config. Store-write path always uses mutex regardless of policy.
> 6. **Coroutine frame allocation: HALO-first.** PMR fallback per-awaiter where HALO doesn't fire.

### 3.11 From `[const §XIII.3]` (observability — strand-stored trace context)

> **OTel `trace_id` / `span_id` in every log record.** … `co_await fixpp::current_trace_context` … `thread_local` propagation of trace context is **prohibited** — coroutines may resume on a different thread than they suspended on, and a `thread_local` write made before suspension is not guaranteed visible after resume.

### 3.12 From `[const §XV.15]` (banned `drop-oldest`)

> **Application-layer message drops on slow consumer.** Backpressure-aware dispatch with two configurable modes: `block` (push back to the producer) or `disconnect-and-recover` (terminate the session and rely on FIX `ResendRequest` semantics on reconnect). `drop-oldest` is **never** permitted on the application or session message path — silent loss desynchronises the sequence-number contract. Telemetry and log queues may use `drop-oldest` under the rules in Article XIII §2.

### 3.13 From `[SYN §3.2 Q6c]` (application threading contract — DECIDED)

> Three options were on the table:
> - inline on the parser-completion executor (lowest latency, slow user blocks I/O coroutine);
> - `co_spawn` onto a separate user-strand (one extra context switch, isolation guarantee);
> - queue onto a user-supplied executor (most flexible, requires user to understand executors).
>
> **Decision: option 3 with a default `strand` per session.** Users who say nothing get the safety of option 2 (callbacks serialised per session, never on the I/O thread). Users who want lower latency or a custom thread model supply their own executor. … parse runs on the I/O strand → completion is dispatched onto the user-strand → user callback runs there. No coupling between user code speed and TCP recv.

### 3.14 From `[SYN §3.2 Q6a]` (cancellation — DECIDED)

> ASIO native. No parallel `stop_token` abstraction. Session shutdown propagates by cancelling the root coroutine, which propagates cancellation to in-flight `async_read`, heartbeat timers, and awaitable-mutex acquires (the latter must respect this — see §3.2 Q6b's design list, item 3). The C ABI's `fixpp_session_close()` triggers cancellation at the C++ boundary by signalling the cancellation slot.

### 3.15 From `[SYN §3.2 Q6b]` (awaitable mutex — DECIDED, owned by 2f, executor-compat surface 2d must support)

> What `fixpp::sync::async_mutex` must add over the upstream baseline:
> 1. **Waiter embedded in the awaiter object** (cppcoro-style), not heap-allocated. … zero extra allocations in the contended path.
> 2. **PMR-aware fallback** for the rare cases where embedding isn't possible …
> 3. **ASIO cancellation slot support** — `asio::cancellation_type::total` removes the waiter from the linked list and completes with `asio::error::operation_aborted`. Required for clean session shutdown and for §3.2 Q6a's stop-token propagation.
> 4. **`dispatch` vs `post` policy** on completion, configurable per-mutex (default `dispatch`; HFT/fairness-sensitive sites pick `post`).
> 5. **Safe destructor semantics** — drain or assert; spec it explicitly, don't leave it as upstream's silent UB-in-release.
> 6. **Tests covering**: FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters policy, contention stress, TSan + ASan clean.

2d locks: items 3, 4, and the executor-compat surface (the awaiter completes on the strand-bound executor, not a foreign one). Items 1, 2, 5, 6 are 2f's internals.

This document refines the inherited surface; it does **not** diverge.

---

## 4. Public C++ API

### 4.1 `fixpp::core::Clock` — abstract interface

```cpp
// include/fixpp/core/clock.hpp
#include <chrono>
#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>

namespace fixpp::core {

// UTC system time used for FIX SendingTime, log/OTel timestamps. Type matches
// the project alias from [arch §4.1] core surface (`fixpp::core::time_point`).
using utc_time_point   = std::chrono::time_point<std::chrono::system_clock>;
using steady_time_point= std::chrono::time_point<std::chrono::steady_clock>;

// Pure-virtual plugin interface. EXACTLY 4 methods (≤5 cap, [const §XIV.2]).
//
// Construction lifetime: a `Clock` instance is owned by the EngineConfig (or
// optionally overridden per-SessionConfig); the implementation must outlive
// every session that references it.
class Clock {
public:
    virtual ~Clock() = default;

    // (1) Wall-clock UTC. Used for FIX SendingTime, log/OTel timestamps,
    //     SendingTime threshold checks. Must be safe to call from any thread,
    //     non-blocking, noexcept. Monotonicity per call within a strand is a
    //     contract item — see §6.6.
    [[nodiscard]] virtual utc_time_point now() const noexcept = 0;

    // (2) Monotonic time. Used for heartbeat-elapsed measurement, latency
    //     bench harness, S-035 schedule deltas. Same thread-safety + noexcept
    //     contract as `now()`.
    [[nodiscard]] virtual steady_time_point steady_now() const noexcept = 0;

    // (3) Awaitable sleep until a steady-clock deadline. Returns when the
    //     deadline passes OR cancellation is signalled (in which case the
    //     awaitable completes with `asio::error::operation_aborted`).
    //     Implementations MUST honour the awaiter's cancellation slot
    //     (`asio::cancellation_type::total`) per [SYN §3.2 Q6a].
    //     The awaitable resumes on the awaiter's bound executor (the session
    //     strand for in-session work).
    [[nodiscard]] virtual asio::awaitable<void>
        sleep_until(steady_time_point deadline) = 0;

    // (4) Cancel every outstanding `sleep_until` waiter belonging to this
    //     Clock instance. Used at engine shutdown and at clock replacement
    //     (test scenarios; v1.0 disallows mid-session swap so this is mostly
    //     test-only). Each cancelled waiter completes with operation_aborted.
    //     Callers MUST NOT rely on `cancel_sleeps` being synchronous — it
    //     posts cancellation; waiters complete asynchronously on their bound
    //     executors.
    virtual void cancel_sleeps() noexcept = 0;
};

}  // namespace fixpp::core
```

Notes:

- **4 pure-virtual, not 5.** Concept-vs-virtual: chosen virtual because (a) `Clock` is held by value-erased pointer in `EngineConfig` to support runtime substitution at engine construction; (b) test seam for `mock_clock` is most ergonomic as a derived class; (c) the surface is so small (4 methods) that vtable cost is negligible; (d) the awaitable return type from `sleep_until` is hard to express as a concept without leaking the implementation's promise type. If a 5th method ever appears (e.g., `cancellable_token_for(deadline)`), it must come with a Gate-A-reviewed paragraph per `[arch §6]`; if a 6th would be added, switch to a concept.
- **No `[[clang::lifetimebound]]` on `now()` / `steady_now()`** — they return value types, not views.
- **`[[nodiscard]]` on every value-returning method.** `sleep_until` returns `asio::awaitable<void>` (already `[[nodiscard]]` by ASIO convention; we re-mark it for clarity).
- **No `expected_t<T>` here.** `now()` and `steady_now()` cannot fail in any defensible way; if a clock implementation is broken, that is an invariant violation — `[arch §5.3]` says abort, not return. `sleep_until` reports cancellation through `asio::error::operation_aborted` (the standard ASIO path) per `[const §XI.2]`.

#### 4.1.1 Clock implementer's recipe (root cause #5 / C-P2-7)

A third-party `Clock` implementation conformant to `[const §VII]` (every plugin needs a mock + test seam) and `[const §VIII.5]` (zero `new`/`delete` between parse and `fromApp`, extended here to the heartbeat path because heartbeat fires between messages on the same strand) must satisfy:

1. **Read the awaiter's executor.** The `sleep_until` coroutine body must obtain `auto exec = co_await asio::this_coro::executor;` and bind every completion through `asio::bind_executor(exec, ...)`. Completion lands on the awaiter's bound executor (the session strand for in-session callers; the engine executor for engine-scope callers).
2. **Read the awaiter's cancellation state.** The body must obtain `auto state = co_await asio::this_coro::cancellation_state;` and arm any timer / waiter via `asio::bind_cancellation_slot(state.slot(), ...)`. `asio::cancellation_type::total` MUST remove the waiter (or cancel the timer) and complete the awaitable with `asio::error::operation_aborted`. The graceful-close phase relies on this composing under a *child* cancellation state per §6.5.
3. **Allocate waiter state from a session PMR resource.** Implementations MUST NOT touch the global heap on the `sleep_until` path. Two conformant patterns:
   - **(a) Per-session pre-allocated reusable timer.** One `asio::steady_timer` (or equivalent waiter slot) constructed once per session at session open, lifetime = session, reused per cycle by resetting `expires_at(...)` then `async_wait(...)`. v1.0's bound is "≤ O(2) outstanding `sleep_until` per session" (§1.2) so a small fixed-count slot pool suffices. The default `system_clock_source` adopts this pattern (§4.2).
   - **(b) Per-call PMR-allocated waiter state.** The awaiter exposes its session PMR resource via `asio::bind_allocator(pmr_allocator, ...)`; implementations allocate timer/waiter state from that resource on each `sleep_until` invocation. No global-heap touch.
4. **Be safe under `cancel_sleeps()` from any thread.** The bookkeeping that `cancel_sleeps()` walks (typically an intrusive list of in-flight waiters) must be atomic-or-strand-protected. The default impl uses lock-free single-producer-consumer-ish bookkeeping (`system_clock_source` impl detail).

Conformance test seam **"Third-party `Clock` conformance test"** (§9) exercises a non-`system_clock_source`, non-`mock_clock` Clock against this recipe to ensure third-party impls remain implementable.

### 4.2 `fixpp::core::system_clock_source` — default impl

```cpp
// include/fixpp/core/system_clock_source.hpp
namespace fixpp::core {

class system_clock_source final : public Clock {
public:
    // Construct with the engine-level executor. Per-session sleep slots
    // (the per-session reusable steady_timer pool, keyed by `Session*` per
    // round 2 root cause #1) are allocated lazily at first sleep_until on a
    // given session and reset (not destroyed) between cycles — see N-P1-1
    // / root cause #5: zero per-cycle heap touch on the heartbeat path.
    // Uniform across both `per_session_strand` and `direct_executor` modes
    // because the keying axis is `Session*`, not strand handle.
    explicit system_clock_source(asio::any_io_executor exec) noexcept;

    // Destructor:
    //   1. cancel_sleeps() — fires cancellation on every in-flight waiter.
    //   2. drains the intrusive waiter list.
    //   3. Engine ordering: ~Engine MUST drain all sessions before clearing
    //      its shared_ptr<Clock> (the clock outlives every session per §4.4
    //      lifetime rule + §10 Q3 close, root cause #5 / N-P1-2).
    ~system_clock_source() override;

    [[nodiscard]] utc_time_point    now()        const noexcept override;
    [[nodiscard]] steady_time_point steady_now() const noexcept override;

    // sleep_until reads `co_await asio::this_coro::executor` for completion
    // binding and `co_await asio::this_coro::cancellation_state` for slot
    // wiring (per §4.1.1). Waiter state is drawn from the per-session
    // reusable timer slot pool, not allocated per call. The intrusive list
    // hand-off is single-strand on the awaiter's strand; cancel_sleeps()
    // signals cancellation cross-thread via the slot mechanism.
    [[nodiscard]] asio::awaitable<void>
        sleep_until(steady_time_point deadline) override;

    void cancel_sleeps() noexcept override;

private:
    asio::any_io_executor exec_;
    // Detail: per-session reusable timer slot pool keyed by `Session*`
    // (NOT by strand handle — root cause #1 / round 2: under
    // `direct_executor` mode there is no strand handle, so the keying axis
    // is the typed `Session*` captured at `Session::open` from the project
    // session-serialisation-domain executor wrapper — recovered via
    // `session_executor::session_ptr()` per §4.8 / round 3 root cause #1).
    // The
    // intrusive list of in-flight waiters is single-strand-or-attested-
    // serialised per session at the session boundary; the cross-thread
    // cancel_sleeps walk is lock-free single-producer-consumer-ish since
    // cancel_sleeps races against completions across sessions, not within
    // one session. Implementation in src/core/system_clock_source.cpp.
};

}  // namespace fixpp::core
```

Notes:

- The constructor takes the engine-level executor (`EngineConfig::executor`) — *not* the session strand. Per `[const §VIII.5]`'s extension to the heartbeat path (the heartbeat fires between messages, not just inside the parse → `fromApp` window), the implementation MUST NOT allocate per-call heap state. **v1.0 picks pattern (a) from §4.1.1**: one `asio::steady_timer` slot per session is allocated at first `sleep_until` on a given session and reused on every subsequent cycle by `expires_at(...)` + `async_wait(...)`. **The slot pool is keyed by `Session*`, not by strand handle** (root cause #1 / round 2 — under `direct_executor` mode there is no strand wrapper; the typed `Session*` is the stable session-domain identity in both threading modes, recoverable via the project `fixpp::core::session_executor` wrapper's `session_ptr()` accessor — see §4.6 / §4.8). The §1.2 bound — at most O(2) outstanding `sleep_until` per session — means a fixed-count pool suffices. The slot pool's lifetime is the session's; storage is the session's PMR `default_session_resource`. (Pattern (b), per-call PMR allocation through `bind_allocator`, is permitted for third-party `Clock` implementations per §4.1.1; the default impl picks (a) for simplicity.)
- The `sleep_until` awaiter binds its completion to the caller's bound executor (`co_await asio::this_coro::executor`) using `asio::bind_executor`. The cancellation slot is read from `co_await asio::this_coro::cancellation_state` per §4.1.1 step 2; both `total` (from phase 2 / `terminal`) and a child state from phase 1 (graceful close timeout) are honoured.
- `now()` calls `std::chrono::system_clock::now()`; `steady_now()` calls `std::chrono::steady_clock::now()`. No PMR allocation, no exception, no syscall beyond what the standard library does.
- `cancel_sleeps()` is `noexcept` and idempotent. Calling it after destruction is forbidden — lifetime ordering is locked at §10 Q3 / root cause #5: `~Engine` blocks on session drains, then clears its `shared_ptr<Clock>` reference last. Test fixtures that hold a `mock_clock` `shared_ptr` outside the engine see the `mock_clock` outlive the engine (the mock's allocation discipline is test-only, no hot-path constraint). Debug builds assert if a `system_clock_source` is destroyed with undrained waiters; release builds UB.
- The destructor drives `cancel_sleeps()` then waits for the intrusive list to drain. Engine shutdown ordering — §10 Q3 closed — is `~Engine` → drain all open sessions → clear `EngineConfig::clock` `shared_ptr` last (§9 seam **"Engine-shutdown ordering test"**).

### 4.3 `fixpp::core::mock_clock` — test impl

```cpp
// include/fixpp/core/test/mock_clock.hpp
//
// Public test header. Lives under <fixpp/core/test/...> so user tests against
// the engine can also use it without depending on the engine's private test
// helpers. Per [const §VII] every plugin interface ships a mock + test seam.
//
// CONSTITUTIONAL CONSTRAINT: This header includes <asio/awaitable.hpp> via
// the Clock base. Per [const §XI.3] line 146, plain std::mutex is BANNED in
// any header that includes asio::awaitable<...>. Therefore mock_clock's
// mutable state lives behind a pimpl in src/core/test/mock_clock.cpp; the
// header declares only the public surface and an opaque impl pointer.
// (Root cause #4 / C-P1-6 resolution.)

namespace fixpp::core {

namespace detail { class mock_clock_state; }  // pimpl, defined in .cpp.

class mock_clock final : public Clock {
public:
    // Construct with an initial UTC + steady wall-time. Both clocks step
    // independently via advance() / set_now(), so a test can simulate
    // wall-clock skew vs monotonic time (e.g., NTP step) deterministically.
    mock_clock(utc_time_point initial_utc,
               steady_time_point initial_steady,
               asio::any_io_executor exec);

    ~mock_clock() override;

    [[nodiscard]] utc_time_point    now()        const noexcept override;
    [[nodiscard]] steady_time_point steady_now() const noexcept override;

    // sleep_until queues the awaiter on a per-deadline list maintained by
    // the pimpl. advance() (or step_to()) wakes any awaiter whose deadline
    // has passed. Cancellation through the awaiter's cancellation_state
    // slot (per §4.1.1) still works — the awaiter is removed from the list
    // and completes with operation_aborted.
    [[nodiscard]] asio::awaitable<void>
        sleep_until(steady_time_point deadline) override;

    void cancel_sleeps() noexcept override;

    // ── Test-only API ────────────────────────────────────────────────────
    // Step monotonic time by `delta`. Wakes any sleep_until awaiter whose
    // deadline ≤ new steady_now. Wall-clock UTC `now()` is moved by the same
    // delta unless the test set a wall-clock skew via set_utc_skew().
    void advance(std::chrono::nanoseconds delta) noexcept;

    // Force monotonic time to a given point. Wakes any sleep_until awaiter
    // whose deadline ≤ point. Useful for "fast-forward to next heartbeat."
    void step_to(steady_time_point point) noexcept;

    // Set a wall-clock-only delta that does not affect steady_now (NTP step
    // simulation; SendingTime threshold tests).
    void set_utc_skew(std::chrono::nanoseconds skew) noexcept;

private:
    // Pimpl: all mutable state (utc_now, steady_now, per-deadline waiter
    // map, the synchronisation primitive used to protect them under
    // concurrent test harnesses) lives in src/core/test/mock_clock.cpp.
    // No std::mutex visible from this header; the synchronisation choice
    // (e.g., std::mutex inside the .cpp, or an asio strand-bound state
    // owned by the pimpl) is an implementation detail of the .cpp file
    // and satisfies [const §XI.3] by construction.
    std::unique_ptr<detail::mock_clock_state> impl_;
};

}  // namespace fixpp::core
```

Notes:

- **Public, not private.** The header lives at `include/fixpp/core/test/mock_clock.hpp` so user tests against the engine can use it. Per `[const §VII]` mocks are first-class.
- **`[const §XI.3]` satisfied by construction.** The header declares only the awaitable signatures and a pimpl pointer. The synchronisation primitive (mutex or otherwise) lives in the `.cpp`, which never includes `asio::awaitable<...>` in a way that exposes the primitive; the grep gate (`[const §XI.3]`'s "header that includes `asio::awaitable<...>`") sees no `std::mutex` declaration. Root cause #4 / C-P1-6 closed.
- **Determinism.** `advance` and `step_to` update the pimpl's time and wake awaiters; the awaiters themselves resume on their bound executor (whatever the test passed in — typically `asio::system_executor` for synchronous test execution, or an `asio::io_context` the test runs explicitly).
- **No allocation budget.** `mock_clock` is allowed to allocate (it is test-only); the dispatch hot-path zero-alloc rule does not apply to mock implementations.

### 4.4 `fixpp::core::EngineConfig` — engine-level shared resources

```cpp
// include/fixpp/core/engine_config.hpp
namespace fixpp::core {

struct EngineConfig {
    // ── Required ─────────────────────────────────────────────────────────
    asio::any_io_executor    executor;            // engine pool — sessions derive strands from this.
    std::shared_ptr<Clock>   clock;               // engine-anchor clock; rejected if null at Engine::open
                                                  // regardless of session overrides (root cause #2).

    // ── Dictionary registry (root cause #2 / C-P1-1; closes [2c §10] Q10) ─
    // Engine-anchored list of dictionaries the engine pre-loads at init. The
    // engine constructs `dict::version_registry` from this list at Engine::open;
    // sessions reach the registry via Engine::version_registry() to resolve
    // FIXT.1.1 per-message ApplVerID(1128) overrides at dispatch time per
    // [2c §4.9] / [2c §6.3] Frame 3. Lifetime: dictionaries are engine-owned
    // (shared_ptr<const Dictionary> for keepalive); the registry holds borrowed
    // pointers per [2c §4.9]'s `get(application_version) -> Dictionary const*`
    // shape. SessionConfig::dictionary remains the per-session anchor for
    // non-FIXT.1.1 sessions.
    std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> dictionaries;

    // ── Required, but defaultable ────────────────────────────────────────
    std::pmr::memory_resource*    default_message_resource = std::pmr::get_default_resource();
    std::pmr::memory_resource*    default_session_resource = std::pmr::get_default_resource();

    // ── Observability (optional in the API; effectively required for production) ─
    std::shared_ptr<fixpp::core::Logger>           logger;        // null → no-op.
    std::shared_ptr<fixpp::otel::TracerProvider>   tracer;        // null → no-op trace context.
    std::shared_ptr<fixpp::otel::MeterProvider>    meter;         // null → no-op metrics.

    // ── Default plugin selections (a session may override each in SessionConfig) ─
    std::shared_ptr<fixpp::session::MessageStoreFactory> default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source>             default_cert_source;
    std::shared_ptr<fixpp::transport::TransportFactory>  default_transport_factory;

    // ── Engine-level fallback trace_context (per N-P2-2) ────────────────
    // Storage: held by the engine in a `std::atomic<trace_context>` snapshot
    // (trace_context is 32 bytes, lock-free atomic on supported platforms;
    // if not lock-free, a `seqlock` is acceptable). The
    // `current_trace_context` awaiter (§4.6) reads this snapshot when no
    // session-domain `session_local` slot is reachable (control-plane handlers,
    // listener accept). Updates between Engine::open() and Engine::close()
    // are permitted on this snapshot only — the snapshot is set once at
    // engine open from this field and may be updated through
    // `Engine::set_engine_trace_context(trace_context)` (an engine-scope
    // mutator separate from the frozen-config rule, since the engine-level
    // fallback is observability-shaped, not a session FSM input).
    fixpp::otel::trace_context engine_trace_context {};
};

}  // namespace fixpp::core
```

Notes:

- `EngineConfig` is value-typed. `Engine::open(EngineConfig)` consumes it once at engine construction; the engine holds copies of the `shared_ptr`s and the dictionary list for its lifetime.
- **`clock` rejection invariant (root cause #2 / C-P2-6 hardening).** `Engine::open` returns `error::clock_not_set` if `clock == nullptr`, regardless of whether all sessions provide `clock_override`. Rationale: the engine-scope LOG/OBS path (§7.9) reads `EngineConfig::clock` directly and has no session to fall back to.
- **`clock` is `shared_ptr` (not `unique_ptr`)** because the engine is not the unique owner across multiple `Engine::open` cycles in test fixtures, and because test scenarios hold a `mock_clock` reference outside the engine to drive `advance(...)` deterministically (per N-P3-2: rationale tightened — the lifetime axis is "user-controlled across multiple `Engine::open` cycles," not just "drive `advance` from outside"). Lifetime ordering at engine teardown: `~Engine` drains all open sessions before clearing its `shared_ptr<Clock>` reference (§10 Q3 close, root cause #5).
- **Engine-level fallback trace_context storage (N-P2-2).** The engine holds a per-engine `std::atomic<trace_context>` (or `seqlock`-protected snapshot if `is_always_lock_free` is false on some target — `trace_context` is 32 bytes per §1.2). `current_trace_context` outside any session serialisation domain (i.e., when the awaiter's bound executor is not a `session_executor` wrapper — see §4.6 + §4.8) reads this snapshot directly; no domain query is needed because the snapshot is atomic. Inside a session serialisation domain, the awaiter reads the session's `session_local<trace_context>` slot per §4.6.
- The defaultable PMR resources are pointers, not `polymorphic_allocator`s, because the engine plugs them into `polymorphic_allocator<T>` constructors at use site.
- Log / OTel providers default to null; the engine treats null as no-op (matches the `[arch §5.7]` rule that observability is a feature, not a precondition).
- `dictionaries` may be empty for engines that operate only with runtime-loaded dictionaries on a per-session basis; the registry then has no entries and FIXT.1.1 per-message overrides will fail with `dict_no_dictionary_for_application_version` per `[2c §6.7]`. Engines that handle FIXT.1.1 cross-vocabulary traffic MUST populate at least the four codegen-version dictionaries the registry expects per `[2c §6.3]`.

### 4.5 `fixpp::session::SessionConfig` — session-level frozen-at-open knobs

```cpp
// include/fixpp/session/session_config.hpp
namespace fixpp::session {

enum class threading_mode : std::uint8_t {
    // Default. Wrap the resolved executor (executor_override or
    // EngineConfig::executor) in asio::make_strand. Application callbacks
    // dispatch onto that strand. Safe; serialised per session.
    per_session_strand = 0,

    // Expert-only opt-out. Run callbacks directly on the resolved executor
    // without a make_strand wrap. Engine FSM/transport/store state continues
    // to be single-thread-accessed by construction — the user MUST attest
    // (`already_serialized_executor = true`) that the executor is already
    // per-session-serialised. Construction rejects with
    // `error::executor_not_serialised` otherwise (root cause #1 / C-P1-2).
    // Used by HFT shops that own their own per-thread fan-out and don't
    // want a second strand layer; the engine continues to assume per-session
    // single-threaded access semantics on that executor.
    direct_executor    = 1,
};

enum class lock_policy : std::uint8_t {
    mutex = 0,    // default; safe.
    spin  = 1,    // opt-in; per [const §XI.5] store-write path always uses mutex.
};

struct SessionConfig {
    // ── Threading (locked by 2d) ─────────────────────────────────────────
    // Resolved executor = executor_override.value_or(EngineConfig::executor).
    // Per C-P2-1 / root cause #2: the field is *_override (nullable), not
    // required. The engine-anchor + session-override pattern matches
    // [arch §5.6]'s "session inherits from engine unless overridden."
    std::optional<asio::any_io_executor> executor_override;

    threading_mode              mode = threading_mode::per_session_strand;
    lock_policy                 locks = lock_policy::mutex;

    // direct_executor attestation (root cause #1). Required `true` when
    // mode == direct_executor; ignored otherwise. The engine does not
    // enforce serialisation under direct_executor — the user contracts
    // that the resolved executor is already per-session-serialised.
    bool                        already_serialized_executor = false;

    // Optional Clock override; null → inherits from EngineConfig::clock.
    // Effective_clock per §7.9 = clock_override ?: EngineConfig::clock.
    std::shared_ptr<fixpp::core::Clock> clock_override;

    // ── Session identity (owned by the session-module Phase-4 spec) ─────
    std::string                 sender_comp_id;
    std::string                 target_comp_id;
    std::string                 begin_string;          // e.g. "FIX.4.4".

    // ── Plugin overrides (each null → inherit from EngineConfig) ────────
    std::unique_ptr<MessageStoreFactory>           store_factory;   // unique ownership per [arch §5.6] / [2e §4.4]
    std::shared_ptr<fixpp::tls::cert_source>       cert_source;

    // SecurityProfile per [const §XII.5] — no implicit default; the type
    // must default-construct to a sentinel (e.g. `unset`) and the engine
    // rejects that sentinel at Session::open with
    // `error::invalid_session_config` (per N-P2-3 + §6.7). The actual
    // sentinel-values are owned by 2g; 2d records only the required-pick
    // discipline.
    fixpp::tls::SecurityProfile                    security_profile;

    // ── Dictionary + dialect overlay (locked by 2c) ─────────────────────
    // Per-session anchor dictionary for non-FIXT.1.1 sessions; for FIXT.1.1
    // sessions, the FSM also reaches `EngineConfig::dictionaries` via the
    // engine's `dict::version_registry` for per-message ApplVerID(1128)
    // overrides per [2c §4.9] / [2c §6.3].
    std::shared_ptr<const fixpp::dict::Dictionary> dictionary;        // required.
    std::shared_ptr<const fixpp::dict::DialectOverlay> dialect_overlay;  // optional; null → none.

    // ── Recovery / reject thresholds (owned by the session-module Phase-4 spec) ─
    // 2d does NOT pick concrete defaults for these per [SYN §3.2 Q10] +
    // [arch §11] row 6 deferral (root cause #2 alignment with C-P2-8).
    // The fields are declared here so the threading row can name the
    // timing source (`effective_clock`) without back-referencing the
    // session-module spec; the *values* are picked at session-module
    // sign-off. `std::nullopt` means "session-module spec value when
    // available; engine uses session-module-defined fallback at
    // Session::open."
    std::optional<std::chrono::seconds>      heartbeat_interval;          // owner: session-module spec.
    std::optional<std::chrono::milliseconds> test_request_threshold;      // owner: session-module spec.
    std::optional<std::chrono::milliseconds> sending_time_threshold;      // owner: session-module spec.
    RejectPolicy                reject_policy = RejectPolicy::strict_reject_then_logout;

    // ── PMR (locked by [2b §8]) ─────────────────────────────────────────
    // Three-arena split per [2b §6.6] / [2b §8] (per-message, framer-carry,
    // session-lifetime; the parser-completion arena 2b folds into per-message
    // — N-P3-1 editorial). Null means "engine provides the default."
    std::pmr::memory_resource*  message_arena       = nullptr;
    std::pmr::memory_resource*  framer_carry_arena  = nullptr;
    std::pmr::memory_resource*  session_arena       = nullptr;

    // ── Observability hooks (interface-level only; locked by 2k) ────────
    // Per C-P2-4 / N-P2-2: the trace_context_provider field is replaced
    // with a value-typed `initial_trace_context` (no heap-capable callable
    // in frozen config). Tests/users that need dynamic generation may set
    // `initial_trace_context` to the freshly-generated value at config
    // construction time — the field is read once at session open by
    // construction. The §6.7 `trace_context_provider_threw` error variant
    // is dropped (no callable to throw); replaced by validation on the
    // value-typed field at Session::open.
    fixpp::otel::trace_context  initial_trace_context {};
    std::shared_ptr<fixpp::log::Sink> log_sink_override;       // null → engine default.

    // ── Tap (locked by 2l) ──────────────────────────────────────────────
    fixpp::tap::TapConsumer     tap_consumer;          // variant; default-constructed = no tap.

    // ── Backpressure (locked by [const §XV.15]) ─────────────────────────
    enum class backpressure_mode : std::uint8_t {
        block                  = 0,    // push back to producer (parser/IO coroutine suspends).
        disconnect_and_recover = 1,    // terminate session; FIX ResendRequest on reconnect.
        // drop_oldest is BANNED on app/session message paths — not exposed.
    };
    backpressure_mode app_backpressure = backpressure_mode::block;
};

}  // namespace fixpp::session
```

Notes:

- **Frozen at session open.** Per `[arch §5.6]`, no mid-session reconfiguration of any of: `executor_override`, `mode`, `already_serialized_executor`, `locks`, `dictionary`, `dialect_overlay`, `security_profile`, `store_factory`, `clock_override`, `initial_trace_context`. The supported pattern is close-and-reopen.
- **Engine-anchor + session-override pattern (root cause #2).** The dictionary axis (`EngineConfig::dictionaries` + `SessionConfig::dictionary`), the executor axis (`EngineConfig::executor` + `SessionConfig::executor_override`), and the clock axis (`EngineConfig::clock` + `SessionConfig::clock_override`) all follow the same shape: engine-anchor required at `Engine::open`; session-override nullable / `std::optional`-typed; resolved value at `Session::open` is `override.value_or(engine_anchor)`. The §7.9 single-effective-clock rule generalises to single-effective-executor for the executor axis as well.
- **`direct_executor` attestation (root cause #1 / C-P1-2).** When `mode == direct_executor`, `already_serialized_executor` MUST be `true`. The engine does not call `make_strand` on the resolved executor in this mode but continues to access engine-internal session state (FSM, transport completion bookkeeping, store-write coordination, heartbeat) as if it were running under a strand — the user contracts that the executor satisfies that property. Construction rejects with `error::executor_not_serialised` (§6.7) when `mode == direct_executor && !already_serialized_executor`. The combination `direct_executor + lock_policy::spin` is also rejected with `error::invalid_session_config` (§6.7 / §6.1) — spin assumes single-thread strand access for its bound; without `already_serialized_executor`, the bound assumption is unverified.
- **Backpressure enum has only two values.** `drop_oldest` is **not** available on the app/session message path per `[const §XV.15]`. The enum is exhaustive on purpose so a user cannot construct an illegal value. Telemetry and tap paths use the `[const §XIII.2]` exception under their own configs (owned by 2k / 2l), not this enum.
- **`security_profile` no-implicit-default discipline (N-P2-3).** Per `[const §XII.5]`, the `SecurityProfile` type default-constructs to a sentinel that the engine rejects at `Session::open` with `error::invalid_session_config`. The exact sentinel value (e.g., `unset`) is owned by 2g; 2d records only the rejection invariant.
- **`message_arena` / `framer_carry_arena` / `session_arena`** match the three-arena lifetime pinning from `[2b §6.6]` / `[2b §8]`; null means "engine provides the default."
- **Engine-internal `Session::session_arena()` accessor** (added at `2f-async-mutex.md` v1.5 sign-off per `[2f Appendix D §D.1]`).

```cpp
class Session {
public:
    // … existing public surface (open, close, get_executor, etc.) …

    // Engine-internal accessor (not part of the user-facing public surface;
    // exposed for the fixpp::session-layer helper
    // async_lock_via_session_executor per [2f §4.3.2] / [2f Appendix D §D.1]).
    // Returns the per-session PMR resource carried as
    // SessionConfig::session_arena per [2d §4.5]. noexcept; never returns
    // null — the constructor pre-conditions a non-null session_arena via
    // the [2d §4.4] resolution chain (SessionConfig::session_arena ?:
    // EngineConfig::default_session_resource ?:
    // std::pmr::get_default_resource()); per [arch §5.6] the resolved value
    // is frozen at session open and never swaps mid-session, so the
    // accessor's never-null contract holds for the session lifetime.
    [[nodiscard]] std::pmr::memory_resource* session_arena() const noexcept;

private:
    // … existing private members …
};
```

  The accessor is **engine-internal** (callable from `fixpp::session/`; not part of `Session`'s documented public user-facing API). `core::async_mutex` does not call this accessor (the session-side helper in `fixpp::session/` is the only caller); per `[arch §2.3]`'s leaf rule, `core/` cannot back-edge into `session/`. Recorded in this file's Appendix C cross-doc entry as a 2f-driven amendment.
- **No concrete threshold defaults (root cause #2 alignment with C-P2-8).** `heartbeat_interval`, `test_request_threshold`, `sending_time_threshold` are `std::optional<...>` placeholders; concrete defaults are picked at the session-module Phase-4 spec sign-off per `[SYN §3.2 Q10]` + `[arch §11]` row 6. 2d names the timing source (`effective_clock`); the session-module spec picks the values.
- **Trace-context provider replaced with value (C-P2-4).** No `std::function`-based provider; `initial_trace_context` is a value-typed `fixpp::otel::trace_context`. Dynamic generation at config construction time is the user's responsibility (cheap — 32 bytes copied). The §6.7 `trace_context_provider_threw` variant is dropped.

### 4.6 `fixpp::current_trace_context` — session-domain awaitable + `session_local<T>` storage (root cause #3 / round 2 root cause #1)

```cpp
// include/fixpp/core/session_local.hpp
namespace fixpp::core {

// Project-defined session-serialisation-domain-local storage. Each Session
// owns exactly one `session_local<trace_context>` slot; the slot's value is
// populated at session open from `SessionConfig::initial_trace_context` and
// cleared at session close. The `session_local<T>` instance lives inside
// the Session object; awaiters bound to that session's executor reach the
// slot through a typed `Session*` recovered at access time from the
// project-owned `fixpp::core::session_executor` value-typed wrapper
// (§4.8) via its public `session_ptr()` member-function accessor — see
// the "Access mechanism" notes below.
//
// Renamed from v0.2's `strand_local<T>` (round 2 root cause #1 / Codex
// C-R2-P2-2 / Opus N2-P1-1) because the keying axis is the **session
// serialisation domain**, not "the strand": under
// `threading_mode::per_session_strand` the domain is `asio::strand<...>`;
// under `threading_mode::direct_executor` the domain is the user-attested
// serialised executor (no strand wrapper). Both modes converge on the
// same project-owned `session_executor` wrapper — see §4.8.
//
// Storage is plain ownership — NOT executor-property-based, NOT
// thread_local. The slot survives coroutine resume on a different thread
// because the value is read through the borrowed Session pointer, which
// remains stable across resume points (per `[const §XIII.3]`'s requirement).
//
// Per C-P1-5 / root cause #3: `asio::any_io_executor::query(void*)` over a
// type-erased executor is NOT a published storage contract — `make_strand`/
// `bind_executor` decoration is not guaranteed to forward arbitrary opaque
// properties on `any_io_executor`. **Per round 3 root cause #1 / Codex
// C-R3-P1-1: the same defect applies to typed user-defined properties on
// `any_io_executor` (its supportable property set is fixed and closed —
// `context_t`, `blocking_t`, `outstanding_work_t`, `relationship_t`,
// `allocator_t<void>` — and arbitrary user-defined properties are NOT
// forwarded). The round-2 access fix's typed `session_ptr_property` query
// on a `session_executor_t = any_io_executor` alias was therefore
// regression-equivalent to the rejected `query(void*)` design.** Round 3
// re-picks the type as a project-owned `session_executor` wrapper class
// (§4.8) whose typed `Session*` accessor is a public member function on
// the wrapper, NOT a property query routed through ASIO's
// `any_io_executor`-shaped property pipeline. The accessor survives
// `bind_executor` / `make_strand` decoration because ASIO's standard
// machinery operates against the wrapper-as-executor concept and never
// erases the wrapper into `any_io_executor` on engine-controlled paths.
template <typename T>
class session_local {
public:
    session_local() noexcept = default;

    // Read the slot's current value. Caller MUST be running inside the
    // owning session's serialisation domain (i.e., on the session's strand
    // under `per_session_strand` mode, or on the user-attested serialised
    // executor under `direct_executor` mode); the engine arranges that
    // callers (the `current_trace_context` awaiter, internal log/OTel emit
    // sites) only ever read this from inside the right domain. Debug
    // builds assert the precondition via a `Session*` self-check on the
    // recovered session pointer.
    [[nodiscard]] T const& load() const noexcept;
    [[nodiscard]] T& load() noexcept;

    // Set the slot's value. Caller MUST be inside the owning session's
    // serialisation domain. Used at session open (engine populates from
    // `SessionConfig::initial_trace_context`) and at session close (engine
    // clears the slot).
    void store(T value) noexcept;

    // Reset the slot to a default-constructed T. Used at session close.
    void clear() noexcept;

private:
    T value_ {};
};

}  // namespace fixpp::core
```

```cpp
// include/fixpp/core/trace_context.hpp
namespace fixpp {

// A free awaitable. Inside a session serialisation domain, returns the
// value held by the session's `fixpp::core::session_local<trace_context>`
// slot (populated at session open from `SessionConfig::initial_trace_context`).
// Outside session scope (listener accept, control-plane handlers, engine
// bootstrap), the awaiter falls back to a per-engine atomic snapshot of
// `EngineConfig::engine_trace_context` (per N-P2-2: the engine holds the
// snapshot in a `std::atomic<trace_context>`-or-seqlock — see §4.4).
//
// NOT thread_local. The session-domain slot is owned by the Session via
// `session_local<trace_context>`; the awaiter recovers the typed
// `Session*` from the project-owned `session_executor` wrapper's
// `session_ptr()` member-function accessor (§4.8) and reads the slot
// directly. Coroutine resume on a different thread is safe because the
// read goes through the stable Session pointer (per `[const §XIII.3]`).
//
// The awaitable is value-typed and stateless; a single global object is
// the canonical instance.
inline constexpr struct current_trace_context_t {
    [[nodiscard]] auto operator co_await() const noexcept;
} current_trace_context;

}  // namespace fixpp
```

Notes:

- **Storage mechanism (root cause #3 close).** The trace-context slot lives in the `Session` object as a `fixpp::core::session_local<fixpp::otel::trace_context>` member. No `void*` round-trip on `any_io_executor::query`; no dependency on whether `make_strand` / `bind_executor` forwards unknown properties on `any_io_executor`.
- **Access mechanism (round 3 close — closes Codex C-R3-P1-1; supersedes the round-2 typed-property formulation).** The `current_trace_context` awaiter (a) reads `co_await asio::this_coro::executor` to obtain the awaiter's bound executor, (b) checks whether that executor is a `fixpp::core::session_executor` value (the project-owned wrapper class — §4.8) — implementation-wise via `dynamic_cast`-equivalent static type recovery on the awaiter's executor type, since the wrapper is a concrete project type at the binding site under `bind_executor` / `make_strand` over the wrapper, not an erased `asio::any_io_executor`, (c) on a hit, calls `session_executor::session_ptr()` (the public member-function accessor on the wrapper) to recover the typed `Session*`, and (d) reads `session->trace_slot_.load()` (the `session_local<trace_context>` slot). The recovery does **not** route through ASIO's property-query pipeline (round 3 root cause #1: that pipeline is what `any_io_executor`'s closed property set rejected; the round-2 formulation aliasing the wrapper to `any_io_executor` was therefore regression-equivalent to the rejected `query(void*)` design — see Codex C-R3-P1-1 + Appendix C round 3). Instead, the accessor is a member function on the wrapper class — ASIO's `bind_executor` / `make_strand` machinery operates against the wrapper-as-executor concept (the wrapper IS the executor type that consumers bind to), so the wrapper survives the executor pipeline as a concrete project type and the typed accessor remains reachable. On miss (the awaiter's bound executor is *not* a `session_executor` wrapper — i.e., outside any session, including listener accept, control-plane, engine bootstrap, where the awaiter is bound to the user-supplied `asio::any_io_executor` directly), the awaiter falls back to the engine snapshot. The mechanism is uniform across both threading modes: `per_session_strand`'s strand-wrapped `session_executor` and `direct_executor`'s bare-wrapped `session_executor` are both the same project-owned wrapper type, so the accessor is reachable in both.
- **Engine-level fallback (N-P2-2 close).** When the awaiter resolves outside any session serialisation domain (the property query misses), it reads the engine's atomic snapshot of `EngineConfig::engine_trace_context` (§4.4). The fallback path needs no domain because the snapshot is atomic. Control-plane handlers (`[arch §4.11]`, owner 2j) and listener-accept coroutines hit this path.
- **`thread_local` is forbidden** per `[const §XIII.3]`. The `session_local<T>` slot is owned by the `Session`; resume-on-different-thread is safe because the value is reached through the stable `Session*`, not through any thread-bound storage.
- The awaiter completes synchronously in the common case (wrapper-type hit; `session_ptr()` accessor returns the typed `Session*`; slot populated; one atomic load on the engine snapshot when the wrapper-type recovery misses). If the slot is empty mid-`Session::open()` (a coroutine that runs before the slot is populated — should not happen under the v1.0 session FSM ordering), the awaiter returns a default-constructed `trace_context` and emits a debug-build assertion.
- The §10 Q6 trade-off closes here in favour of `session_local<T>` (per Codex C-P1-5 + Opus root cause #3 round 1; refined under round 2 root cause #1 from `strand_local<T>` to `session_local<T>` so the access mechanism is the same in both threading modes). If a future session-domain-local consumer appears, the same template is reused.

### 4.7 Cancellation propagation API — two-phase close (root cause #1)

```cpp
namespace fixpp::session {

// v1.0 surface — graceful + terminal only. `partial` was dropped per
// N-P1-3: ASIO's cancellation_type::partial does not have well-defined
// per-component semantics across the {transport read, transport write,
// heartbeat, async_mutex, fromApp dispatch, store write, Logout exchange}
// matrix, and 2d declines to ship an underspecified parameter. Future
// reintroduction would require the per-component effects table at sign-off.
enum class close_mode : std::uint8_t {
    graceful = 0,    // default; phase 1 (Logout exchange under child state) → phase 2 (teardown).
    terminal = 1,    // skip phase 1; go directly to phase 2 (root total cancellation).
};

class Session {
public:
    // ... (full surface owned by session-module Phase-4 spec) ...

    // Two-phase close.
    //
    //   Phase 1 (graceful only) — opens a CHILD asio::cancellation_state
    //   composed below the session's root cancellation_state. Attempts a
    //   FIX `Logout` exchange (toAdmin(Logout) → async_write → wait for
    //   peer's Logout ACK or fail-fast on disconnect). The wait timer is a
    //   `Clock::sleep_until(...)` awaitable bound to the CHILD slot, with
    //   the deadline = effective_clock.steady_now() + close_timeout (a
    //   value picked at the session-module Phase-4 spec; 2d does NOT pick
    //   it per C-P2-8 + N-P2-1). Because the child state is independent of
    //   the root, the Logout async_write and its timeout sleep are NOT
    //   pre-cancelled by the eventual root total cancellation. When
    //   phase 1 resolves (peer ACK observed | child timeout fires | child
    //   state itself cancelled by an outer policy), phase 2 begins.
    //
    //   Phase 2 (always) — fires asio::cancellation_type::total on the
    //   session's ROOT cancellation_state, propagating to:
    //     - in-flight transport async_read     → operation_aborted.
    //     - in-flight transport async_write    → operation_aborted (any
    //       partially-written FIX bytes are unrecoverable; callers'
    //       toApp(...) had already returned).
    //     - heartbeat Clock::sleep_until      → operation_aborted.
    //     - async_mutex::lock (2f)            → operation_aborted (per
    //       [SYN §3.2 Q6b] item 3).
    //     - the application-callback dispatch (the strand's posted handler
    //       is reaped before invocation by the cancellable_dispatch
    //       primitive — §6.5 — which checks the cancellation_state at the
    //       hand-off boundary; if cancellation lands during invocation, the
    //       next co_await checkpoint observes the slot).
    //     - parser → fromApp chain            → cancelled at next
    //       checkpoint (see §6.5 deterministic three-case answer).
    //
    // Returns asio::awaitable<expected_t<void>> that completes when both
    // phases have drained: transport closed; PMR per-message arenas reset;
    // session_local<trace_context> slot cleared; per-session reusable timer
    // slot pool returned to the session arena.
    //
    // Idempotent — calling close() on an already-closing session returns
    // immediately with no error; calling close() on an already-closed
    // session returns session_already_closed (§6.7).
    [[nodiscard]] asio::awaitable<expected_t<void>>
        close(close_mode mode = close_mode::graceful) noexcept;
};

}  // namespace fixpp::session
```

Per-mode effect table (per N-P1-3 — close the under-specification gap):

| Affected op | `graceful` (phase 1) | `graceful` (phase 2) | `terminal` |
|---|---|---|---|
| Transport `async_read` | runs (peer messages still arrive — Logout, ACK, etc.) | cancelled (`operation_aborted`) | cancelled |
| Transport `async_write` (Logout frame) | runs under child state | n/a (frame already issued in phase 1) | not issued; `terminal` skips Logout |
| Transport `async_write` (other in-flight) | runs to completion | cancelled (`operation_aborted`) | cancelled |
| Heartbeat `Clock::sleep_until` | runs | cancelled (`operation_aborted`) | cancelled |
| `async_mutex::lock` (historical row name; corresponds to `async_mutex::async_lock(...)` per `[2f §4.1.1]` v1.3) (in-flight waiter) | runs | cancelled — completes with `expected_t::unexpected{error::sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]` / `[2f §6.5]` | cancelled (same as graceful phase 2) |
| `fromApp` dispatch (posted, not yet invoked) | runs | reaped by `cancellable_dispatch` (§6.5) — applies under both `per_session_strand` and `direct_executor` because the primitive accepts `session_executor` (round 2 root cause #1; round 3 root cause #1: wrapper class, not a type alias to `any_io_executor`) | reaped |
| `fromApp` dispatch (already executing) | runs to user-callback return | next `co_await` checkpoint observes total | next checkpoint observes total |
| Logout-wait `Clock::sleep_until` (child-state-bound) | runs (deadline = close_timeout) | n/a | n/a |
| `MessageStore::write` (in-flight) | runs to completion | cancelled (`operation_aborted`) | cancelled |
| `FileStore::flush_for_session_close()` (engine-internal hook) | runs (graceful pre-phase-1 store-durability flush) | n/a (already drained) | not invoked (`terminal` skips phase 1 entirely per §4.7) |

> **`async_mutex::lock` (historical row name — corresponds to `async_mutex::async_lock(...)` per `[2f §4.1.1]` v1.3) cancellation outcome at the 2f boundary (driven by `[2f §4.5]` / `[2f §6.5]` / `[2f Appendix D §D.2]`).** `fixpp::sync::async_mutex::async_lock(...)` is a C++-only awaitable returning `asio::awaitable<expected_t<async_lock_guard>>`; on `cancellation_type::total` (or `terminal`, treated as `total` per `[2f §4.5]`) it removes the waiter from the LIFO list via the per-waiter `phase_` CAS protocol and completes the awaitable with `expected_t::unexpected{error::sync_lock_aborted}`. The wording "`operation_aborted`" elsewhere in `[2d §4.7]` continues to apply to ASIO completion-token-shaped cancellations on operations that surface their cancellation through ASIO's `error_code` channel; 2f's 2f-boundary outcome is `expected_t::unexpected` because the operation's value channel is `expected_t<async_lock_guard>` and `[arch §5.3]` forbids exceptions on the hot path. At the C ABI both shapes coalesce into `FIXPP_ERR_CANCELLED` per `[2d §6.7]`. The `[2d §6.5]` `cancellable_dispatch → expected_t::unexpected{dispatch_aborted}` precedent is the project-internal idiom this rewording matches.

> **`FileStore::flush_for_session_close()` hook contract (driven by `[2e §7.6]`).** The engine-internal `FileStore::flush_for_session_close()` is a non-virtual, non-public method on the concrete `FileStore` (NOT on `MessageStore`'s pure-virtual interface — see `[2e §4.1.1]`); the engine reaches it via the session's stored `unique_ptr<MessageStore>` through a friend mechanism. Under `close_mode::graceful` it is invoked once during phase 1, after the FSM's last in-flight `store(...)` awaitable has resumed and before the Logout `async_write` is issued; it drains any pending `commit_batched` / `commit_interval` records to durable storage so the regulator-mandated tail records make it past a host crash that follows close. Cancellation: the hook completes either with success (`expected_t<void>{}`) or with `expected_t::unexpected{store_io_failure}` on a mid-flush `fdatasync`/`FlushFileBuffers` error; the engine logs the failure and proceeds with phase 1's Logout exchange (the durability gap is documented as a `commit_batched` / `commit_interval` data-loss window per `[2e §4.3.1]`). Under `close_mode::terminal` the hook is **not** invoked — terminal close fires root cancellation immediately, and the in-flight `MessageStore::write` row above governs the in-flight state. The hook is **idempotent**: a second invocation is a no-op (returns `expected_t<void>{}` immediately).

Notes:

- `close()` is the *public* trigger. The C ABI's `fixpp_session_close()` (owned by **2i**) calls into the same entry point with `close_mode::graceful`. The `terminal` opt-in is C++-only on the v1.0 surface; 2i may add a C-side enum if needed.
- **Why a child cancellation state?** ASIO's `asio::cancellation_state` composition rules let a coroutine create a child state that is signalled independently of the root: the child state's slot can fire without firing the root, and an outer cancellation on the root propagates *down* to the child. The graceful-close path opens a child state, runs the Logout `async_write` and the timeout `Clock::sleep_until` under it, and lets phase 2 fire `cancellation_type::total` on the root *only after* phase 1 resolves. This closes Codex C-P1-3 (the "self-cancelling Logout" defect) and Opus root cause #1.
- **Idempotency.** Calling `close()` on an already-closing session returns the same awaitable as the in-flight close (functionally a no-op for the second caller). Calling `close()` on a never-opened or already-closed session returns `error::session_already_closed` (§6.7).
- The `cancellation_propagation_timeout` knob from v0.1's §6.7 is dropped per N-P2-1; the close-timeout value lives in the session-module Phase-4 spec, not in `SessionConfig`.

### 4.8 `fixpp::core::session_executor` + executor resolution path (round 2 root cause #1; round 3 root cause #1)

```cpp
// include/fixpp/core/session_executor.hpp
namespace fixpp::core {

// Round 2 root cause #1 + round 3 root cause #1: the **session
// serialisation domain** abstraction is the unifying value-typed wrapper
// class that subsumes both threading modes. Under
// `threading_mode::per_session_strand`, the wrapper holds an
// `asio::strand<asio::any_io_executor>` over the resolved executor; under
// `threading_mode::direct_executor`, the wrapper holds the user-attested
// already-serialised `asio::any_io_executor` with no strand wrapper.
// Both shapes hold a typed `Session*` slot recovered through the public
// `session_ptr()` member-function accessor; the access mechanism is a
// member-function call on the project-owned wrapper, NOT a property
// query on a type-erased ASIO executor. This is what makes the typed
// `Session*` survive `bind_executor` / `make_strand` decoration: ASIO's
// standard machinery operates against the wrapper-as-executor concept
// (the wrapper publishes `execute(F)`, the typed `query(P)` for the
// closed ASIO property set, and the executor-concept relational
// members); the wrapper's own typed accessors (e.g., `session_ptr()`)
// are not erased through ASIO's `any_io_executor` shape because the
// wrapper IS the executor type that consumers bind to — there is no
// erasure step into `any_io_executor` between the wrapper and the
// consumer.
//
// Round 3 root cause #1 (Opus): v0.3's `using session_executor_t =
// asio::any_io_executor` aliased the wrapper into ASIO's
// closed-property-set type-erasure shape, which does not forward
// arbitrary user-defined queries (only the fixed ASIO property set —
// `context_t`, `blocking_t`, `outstanding_work_t`, `relationship_t`,
// `allocator_t<void>`). The typed `session_ptr_property` query the
// round-2 fix promised either failed to compile or always missed
// after that erasure — a regression-equivalent of round-1's rejected
// `query(void*)` design. Round 3 re-picks the type as a project-owned
// wrapper class so the typed accessor is on the type the project
// controls, not on ASIO's erased shape.
class session_executor {
public:
    // The two underlying shapes: strand-wrapped (per_session_strand) or
    // bare (direct_executor). The wrapper is value-typed and trivially
    // copyable; it is what every session coroutine binds to.
    session_executor() noexcept = default;

    // Construction by the make_session_executor helper below. Internal
    // sites should not construct directly.
    session_executor(asio::any_io_executor inner,
                     fixpp::session::Session* session,
                     bool strand_wrapped) noexcept;

    // ── ASIO executor concept ──────────────────────────────────────────
    // Forwards to the inner executor. The project's wrapper IS an executor
    // (satisfies asio::execution::executor concept); ASIO's `bind_executor`
    // and `make_strand` operate against this type directly.
    template <typename F, typename A>
    void execute(F&& f, A&& a) const;

    // Forwards the closed ASIO property set to the inner executor. Custom
    // project-typed accessors (`session_ptr()`, etc.) are NOT routed
    // through `query(P)` — they are direct member functions; the wrapper
    // does not pretend to publish them via ASIO's property-query pipeline
    // (round 3 root cause #1: that pipeline is what `any_io_executor`'s
    // closed property set rejected).
    template <typename Property>
    decltype(auto) query(Property const& p) const;

    // Equality/relationship per the executor concept.
    [[nodiscard]] friend bool operator==(session_executor const&, session_executor const&) noexcept;
    [[nodiscard]] friend bool operator!=(session_executor const&, session_executor const&) noexcept;

    // ── Project-owned typed accessors (round 3 root cause #1) ──────────
    // The typed `Session*` accessor used by §4.6's `session_local<T>`
    // access mechanism. This is a public member function on the
    // project-owned wrapper; the awaiter calls it directly on the
    // resolved `session_executor` value. ASIO erasure into
    // `any_io_executor` is NOT involved on the path that uses this
    // accessor — see §4.6 for the awaiter's recovery-via-this-accessor
    // contract.
    [[nodiscard]] fixpp::session::Session* session_ptr() const noexcept;

    // Discriminator: returns true when constructed under
    // `per_session_strand` mode. Used by debug-build asserts and by the
    // re-entrancy guard test seam; not on the runtime hot path.
    [[nodiscard]] bool is_strand_wrapped() const noexcept;

private:
    // Implementation detail: holds the inner executor (always
    // `asio::any_io_executor`-shaped at the inner layer; the wrapper is
    // the project type at the outer layer that consumers bind to), the
    // typed Session* slot, and the strand-wrapped discriminator. The
    // strand wrapping (when applied) lives inside `inner_` per the
    // make_session_executor helper.
    asio::any_io_executor       inner_ {};
    fixpp::session::Session*    session_ = nullptr;
    bool                        strand_wrapped_ = false;
};

// DEPRECATED at the public-surface level — retained as a doc alias only
// for the `per_session_strand` mode case. Consumers MUST type against
// `session_executor` (round 2 root cause #1 / Opus N2-P2-2 close — the
// project wrapper is the unifying type — and round 3 root cause #1
// close — the wrapper is a class, not an alias to `any_io_executor`).
// using session_strand_t = asio::strand<asio::any_io_executor>;

// Helper: from a resolved (already-override-applied) executor + a
// threading_mode + the already_serialized_executor attestation + the
// owning Session pointer, return the `session_executor` every session
// coroutine binds to.
//
// Per C-P2-1 hardening: returns expected_t<...> rather than a naked
// session_executor because (a) the wrapper's construction over the
// underlying executor may fail depending on the underlying executor,
// and (b) the direct_executor + already_serialized_executor=false case
// must reject at construction with error::executor_not_serialised. The
// noexcept signature from v0.1 is dropped — explicit error path replaces
// "abort on failure."
[[nodiscard]] expected_t<session_executor>
    make_session_executor(asio::any_io_executor resolved_exec,
                          fixpp::session::threading_mode mode,
                          bool already_serialized_executor,
                          fixpp::session::Session* session) noexcept;

}  // namespace fixpp::core
```

Notes:

- **Resolution rule.** `Session::open()` resolves the session's executor as `SessionConfig::executor_override.value_or(EngineConfig::executor)` and feeds that to `make_session_executor` along with `mode`, `already_serialized_executor`, and the owning `Session*`.
- **Round 2 + round 3 unification (closes Opus root cause #1 + round-2 N2-P1-1 / N2-P2-2 + round-3 root cause #1 / C-R3-P1-1).** `make_session_executor` wraps the resolved executor in the project-owned `session_executor` value-typed class. Under `per_session_strand` mode the wrapper's inner executor is `asio::make_strand(resolved_exec)`; under `direct_executor` mode the wrapper's inner executor is the bare attested-serialised `resolved_exec`. Both shapes carry the typed `Session*` and expose it through `session_ptr()` — a project-owned member function, NOT an ASIO property query (round 3 root cause #1: the property-query path through `asio::any_io_executor` does not forward unknown user-defined properties, which is why v0.3's `session_executor_t = any_io_executor` alias was rejected). Every dependent primitive (`cancellable_dispatch` §6.5, `session_local<T>::load()` §4.6, the per-session `steady_timer` slot pool §4.2, the §4.7 effect-table reaping rows) consumes `session_executor` directly — no strand-extraction step at any §4.8 → §6.5 boundary, no "owning strand" precondition that `direct_executor` cannot satisfy, and no dependence on `any_io_executor`'s closed property set.
- `make_session_executor` returns a strand-wrapped wrapper when `mode == per_session_strand` (default), and a bare-wrapped wrapper when `mode == direct_executor && already_serialized_executor == true`. It returns `error::executor_not_serialised` when `mode == direct_executor && !already_serialized_executor` (root cause #1 invariant). `session_executor::is_strand_wrapped()` discriminates the two at runtime when needed.
- **The returned `session_executor` is what every coroutine inside the session binds to.** `asio::bind_executor(session_exec, awaitable)` is the idiomatic form; ASIO operates against the wrapper-as-executor concept and never erases the wrapper into `asio::any_io_executor` on paths the engine controls. (User code that explicitly converts the wrapper to `asio::any_io_executor` loses the typed accessors — that is fine, because such conversion is outside any session domain by definition; the awaiter's fallback path then reads the engine snapshot per §4.6.)
- **`[arch §5.1]` compatibility.** The user-supplied executor at the API surface (`EngineConfig::executor`, `SessionConfig::executor_override`) is still `asio::any_io_executor` per `[arch §5.1]`. The project's `session_executor` wrapper is engine-internal — `make_session_executor` consumes the user's `any_io_executor` and produces the wrapper. No `[arch §5.1]` text changes; the user-supplied-vs-engine-internal split is the same dual-type pattern as 2c's `version_profile`/`resolved_message_version` axis pair.
- Tap consumers and custom `MessageStore` impls call this helper internally to schedule their own work onto the session's executor; the `expected_t` signature lets them propagate `error::executor_not_serialised` rather than silently UB.

---

## 5. Public C ABI

The full C ABI is delegated to **2i**. 2d enumerates the threading concepts that surface across the boundary and asks 2i to lock the symbol shapes.

### 5.1 Threading concepts the C ABI must expose

- **Strand handle.** Decision: **the C ABI does not expose strands explicitly.** The C ABI is callback-shaped (`fixpp_session_set_callback(session, &my_callback, user_data)`); the engine arranges that `my_callback` is invoked on the C++-side strand (per `[const §X.5]` — the symbol's reentrancy contract is `requires-session-lock`, which the engine satisfies by dispatching on the strand). C consumers do not need a strand object. Rationale: the strand is a C++ concept; a pure-C consumer working in a thread-per-session model would not benefit from one, and a callback-shaped API matches the SWIG / Python idiom where the binding owns the call site.
- **Cancellation.** `fixpp_session_close(session_handle)` flips the C++ session's root cancellation slot per `[SYN §3.2 Q6a]`. The slot itself is *not* a C ABI type — 2i does not expose `asio::cancellation_slot` across the boundary. The C ABI sees only the `fixpp_session_close()` symbol; the engine internally signals the slot. This matches `[const §XI.2]` and avoids leaking ASIO into C consumers.
- **Clock.** Decision: **the C ABI does not expose the `Clock` interface.** `fixpp::core::Clock` is C++-only. The C ABI's session-open call (`fixpp_session_open`) takes a `fixpp_engine_t` handle, which already carries the chosen `Clock` (set on the C++ side when the engine was constructed). C consumers who need to inject a mock clock for testing must do so on the C++ side before calling `fixpp_session_open`.
- **`trace_context`.** Decision: **the C ABI does not expose the strand-stored awaitable.** The trace_context is a C++23 awaitable; expressing it in C is incoherent. The C ABI does expose a `fixpp_session_get_trace_id(session, char* out, size_t cap)` accessor (locked by 2i) that copies the current trace_id as a hex string; that is sufficient for log-line correlation in C consumers.

### 5.2 Cancellation flow at the C boundary (two-phase, root cause #1)

```
C consumer:                          C++ engine:
─────────────                        ──────────────
fixpp_session_close(s)
       │
       ▼
   <C ABI thunk>                     Session::close(close_mode::graceful)
       │                                 │
       │                                 ▼
       │                             ┌─── PHASE 1 — graceful (child cancellation_state) ────┐
       │                             │                                                       │
       │                             │   open child cancellation_state below root            │
       │                             │   issue toAdmin(Logout) → transport async_write       │
       │                             │     ▲ bound to child slot — NOT root                  │
       │                             │   start Clock::sleep_until(deadline) on child slot    │
       │                             │     ▲ deadline = effective_clock.steady_now() +       │
       │                             │       close_timeout (session-module spec value)       │
       │                             │   wait for: peer Logout ACK | child timeout fires |   │
       │                             │             child state cancelled by outer policy     │
       │                             │                                                       │
       │                             └─── child state resolved ─────────────────────────────┘
       │                                 │
       │                                 ▼
       │                             ┌─── PHASE 2 — teardown (root cancellation_type::total) ┐
       │                             │                                                       │
       │                             │   ROOT cancellation_type::total fires → propagates to:│
       │                             │     - transport async_read       → operation_aborted  │
       │                             │     - transport async_write (other) → operation_aborted│
       │                             │     - Clock::sleep_until (heartbeat) → operation_aborted│
       │                             │     - async_mutex::lock (2f)     → operation_aborted  │
       │                             │     - cancellable_dispatch'd fromApp → reaped or      │
       │                             │           next-checkpoint-aborted (§6.5)              │
       │                             │     - parser → fromApp chain     → cancelled at next  │
       │                             │                                    checkpoint         │
       │                             │                                                       │
       │                             └─── root coroutine completes (drain) ─────────────────┘
       │                                 │
       │                                 ▼
       │                             arenas reset; trace_slot cleared; timer pool returned;
       │                             return expected_t<void>{}.
       │                                 │
       │                                 ▼
       └─◀── return code (FIXPP_ERR_OK on clean close;
                          FIXPP_ERR_SESSION_ALREADY_CLOSED if redundant)
```

The exact error numeric range and the symbol's reentrancy classification are 2i's call. The C ABI does not expose `close_mode::terminal` on the v1.0 surface (per §5.1's "no strand handles, no `Clock`, no `trace_context` awaitable across C" discipline); a future C-side enum could surface it if a use case appears.

---

## 6. Behavioral contract

### 6.1 Strand semantics (root cause #1 close)

- **Per-session strand (default).** The resolved executor (`SessionConfig::executor_override.value_or(EngineConfig::executor)`) is wrapped in `asio::make_strand(...)` and held inside the project-owned `session_executor` wrapper class (§4.8). The strand serialises completion handlers — at most one of `{onLogon, onLogout, toAdmin, fromAdmin, toApp, fromApp, MessageStore op, Clock sleep wake-up, transport completion}` runs at a time within the session. Two sessions on the same engine executor may run their callbacks concurrently *across* sessions; the strand only serialises *within* a session. The wrapper (§4.8) publishes the typed `session_ptr()` member-function accessor consumed by `session_local<T>` (§4.6) under both modes (round 3 root cause #1: a member function on the wrapper class, NOT a property query on `asio::any_io_executor`), so the access mechanism is identical whether the strand wrapping inside the wrapper is in place or not.
- **`direct_executor` opt-out (root cause #1 / C-P1-2 close).** With `SessionConfig::mode == direct_executor`, the engine does *not* call `make_strand` on the resolved executor. The user attests at construction (`SessionConfig::already_serialized_executor = true`) that the resolved executor is already per-session-serialised — i.e., it never schedules two completions for this session concurrently. The engine continues to access engine-internal session state (FSM, transport completion bookkeeping, store-write coordination, heartbeat slot pool) under the same single-thread-access semantics it would under a `make_strand` wrap; there is no engine-internal `async_mutex` or atomic regime introduced for `direct_executor`. **This is a contract, not a delegation:** `direct_executor + already_serialized_executor=false` rejects with `error::executor_not_serialised` (§6.7). `direct_executor + lock_policy::spin` rejects with `error::invalid_session_config` (§6.7) — the spin-lock's single-thread assumption needs the same attestation, and the policy combination is rejected even with `already_serialized_executor=true` to avoid two attestations on the same axis.
- **Why the contract is enforceable.** A user who satisfies `already_serialized_executor=true` is typically wrapping their own per-thread fan-out (HFT pattern: one worker thread per session, threads never cross). The engine doesn't need to verify the property at runtime — it cannot, in general — but it does require the explicit pick at construction. Test seam **"`direct_executor` re-entrancy guard test"** (§9, new in v0.2) exercises a `serial_executor_adapter` that wraps a strand-shaped executor and exposes the attestation; the engine's invariants survive when the adapter actually serialises and trip-asserts in debug builds when it doesn't.
- **Reentrancy guarantees the application sees.** Within the session's serialisation domain (strand under default, attested executor under `direct_executor`), `fromApp` for message N+1 cannot run before `fromApp` for message N has returned. The user's callback can mutate user-side state without locks (subject to the in-domain-only access discipline). Cross-session, no such guarantee — if the user shares state across sessions, that is their responsibility.
- **Non-reentrant from inside a callback.** A user `fromApp` that calls `Session::send(...)` inside its body schedules the send on the serialisation domain; the send does *not* run before `fromApp` returns. (This is ASIO's strand reentrancy guarantee under default mode; under `direct_executor` it is the user-attested executor's serialisation guarantee.)

### 6.2 Allocation / exceptions / threading on the dispatch hot path

- **Zero allocation between parse and `fromApp`** per `[const §VIII.5]`. The dispatch path:
  1. wire `Parser` produces a `MessageView` over the per-message arena (`[2b §8]`);
  2. session FSM resolves `MsgType` → typed message via `dict::reify` (`[2c §4.8]`) — typed message is a flyweight, no copy;
  3. session FSM `co_await`s the strand to dispatch `fromApp(typed_message)` (when `direct_executor` is off, this is `asio::dispatch(strand, ...)`);
  4. user `fromApp` runs.

  Steps 1..3 allocate from PMR arenas only; the global heap is never touched. Step 3's `asio::dispatch` is HALO-friendly per `[const §XI.6]`; when HALO does not fire, the per-awaiter PMR override constructs the promise on `SessionConfig::message_arena`. Linux/Clang Tier 1 verifies via the `mallocnesia` interceptor (§9 seam **"Allocation guard on dispatch hot path"** — same harness as 2a §9 seam #6 / 2b §9 seam #10; per N-P2-4 the seam catches *global-heap* `new`/`delete`/`malloc`, not all allocation — PMR-arena allocations are expected and not flagged).

- **No exceptions across the parse → `fromApp` window** per `[arch §5.3]`. Cancellation is *not* an exception — it is reported through `asio::error::operation_aborted` per `[const §XI.2]`. Trait wrappers around throwing third-party libraries trap (the `fixpp::core::detail::trap_throw` helper from `2a §4.2 note`).

- **Threading.** Inside the strand, all session-internal state is single-threaded; no atomics needed for state-machine variables. Cross-session, the engine's executor is multi-threaded but each session's strand is its own serialisation domain.

### 6.3 Latency Tier 1 ceilings

Per the 2a v0.3 §6.5 / 2b v0.2 §6.6 idiom: Linux/Clang/x86_64 warm-cache, named workload. CI fails on >5% regression vs the previous tagged release.

| Operation | Workload | Ceiling | Notes |
|---|---|---|---|
| Strand `dispatch` handoff | parser-completion → `fromApp` (single message, in-strand HALO-fired) | ≤ 25 ns | Pure ASIO `dispatch` cost when the executor is already on the right thread; HALO elides the awaiter heap frame. |
| Strand `dispatch` handoff (cross-thread) | parser-completion → `fromApp` (single message, executor off-strand thread) | ≤ 250 ns | One queue insert + cross-thread wakeup; OS-scheduler dependent (§10 Q4 — bench spike during 2d implementation may tighten this). |
| Cross-strand `reify` + dispatch (20-tag, per C-P2-3) | `fromApp` posts `owning_message_t<>` reified via `dict::reify` to a foreign executor (the user-pattern at §7.1) | ≤ 1.25 µs | Sub-components: `dict::reify` ≤ 1 µs (per `[2c §1.2]` and `[2c §6.2]` ≤4 PMR allocs) + cross-strand `dispatch` ≤ 250 ns. Same PMR arena and cancellation slot shape as the in-strand path. |
| Cross-strand `reify` + dispatch (200-tag, per C-P2-3) | same as above, large-message workload | ≤ 10.25 µs | `dict::reify` ≤ 10 µs (per `[2c §1.2]`) + dispatch. |
| `Clock::now()` (default impl) | single call | ≤ 25 ns | `clock_gettime(CLOCK_REALTIME)` typical Linux-x86_64 cost. |
| `Clock::steady_now()` (default impl) | single call | ≤ 20 ns | `clock_gettime(CLOCK_MONOTONIC)` typical. |
| Session-domain `trace_context` access | `co_await fixpp::current_trace_context` (slot populated, inside session domain) | ≤ 15 ns | Wrapper-type recovery on the awaiter's bound executor + `session_executor::session_ptr()` member-function call (one direct member access; constant-time on the project wrapper — round 3 root cause #1) + `session_local<T>::load()` (one value-typed read). No `void*` query overhead and no ASIO property-query overhead — the v0.1 `any_io_executor::query(void*)` design was rejected per root cause #3; the round-2 typed-property formulation was rejected per round 3 root cause #1 (`any_io_executor`'s closed property set does not forward unknown user-defined properties); the published mechanism is a member-function call on the project-owned `session_executor` wrapper class. |
| Engine-fallback `trace_context` access | `co_await fixpp::current_trace_context` (outside any session strand) | ≤ 25 ns | One atomic load on `EngineConfig::engine_trace_context` snapshot (§4.4). |
| `mock_clock::now()` | single call | ≤ 50 ns | Pimpl indirection + internal sync — not a hot-path ceiling, just for context. |

These are bench-harness regression bars (§9 seam **"Latency regression bench"**). The cross-thread dispatch ceiling is intentionally generous — it is OS-scheduler dependent and we want CI to flag regressions, not OS jitter. The cross-strand `reify` rows are budgeted end-to-end so 2d's ceilings reflect the real cost of the §7.1 cross-strand handoff pattern (per C-P2-3 / Opus confirm); seam **"Latency regression bench"** is extended to bench both 20-tag and 200-tag workloads.

### 6.4 Backpressure on the strand

The locked decision per `[const §XV.15]` and the `opus_plan.md` "User Decisions So Far" entry: **`drop-oldest` is BANNED on the application / session message path.** Only `block` and `disconnect-and-recover` are permitted. Telemetry / log / tap queues may use `drop-oldest` under their own configs.

How this interacts with the strand's reentrancy guarantee:

- **`block` (default).** The parser/IO coroutine is suspended until `fromApp` returns. The strand serialises `fromApp` invocations, so a slow application callback creates back-pressure all the way to the TCP socket — `async_read_some` is not re-issued until the previous message's `fromApp` returns. The TCP socket's receive buffer fills; the kernel rate-limits the peer. This is the FIX-spec-friendly choice (sequence numbers stay in lock-step with what the application has actually processed).
- **`disconnect_and_recover`.** When a slow consumer is detected (criterion: `fromApp` invocation has been pending for longer than `SessionConfig::test_request_threshold`), the session terminates the transport; on reconnect, FIX `ResendRequest` semantics replay the missing messages from `MessageStore`. Sequence-number invariants survive.
- **`drop_oldest` is unrepresentable.** The `backpressure_mode` enum has only two values (§4.5). A user cannot construct an illegal state. The compile-time enum exhaustiveness is enforced by `[[clang::enum_extensibility(closed)]]` where supported, and by a static_assert at every switch on the enum.

The strand's reentrancy guarantee is **the** mechanism that makes `block` work — without strand serialisation, the parser could outpace the application even when both are on the same executor, requiring a queue between them. The strand is the queue (depth 1).

### 6.5 Cancellation semantics — two-phase + `cancellable_dispatch` (root cause #1)

- **Two-phase semantics.** `Session::close(graceful)` opens a child `asio::cancellation_state` per §4.7's per-mode effect table; the Logout `async_write` and the Logout-wait `Clock::sleep_until(...)` run under the child state, **not** under the root. Phase 2 fires `asio::cancellation_type::total` on the root *only after* phase 1 resolves. `Session::close(terminal)` skips phase 1. The full per-component matrix is in §4.7.
- **`cancellable_dispatch(session_executor, slot, handler)` — project-owned primitive (root cause #1 / C-P2-2 round 1 + Codex C-R2-P1-1 + C-R2-P2-1 round 2 close + round 3 root cause #1 type re-pick).** ASIO's plain `asio::dispatch(executor, handler)` posts a handler whose cancellation-aware reaping is *not* part of `[asio.dispatch]`'s contract — the cancellation_slot fires on async operations whose composition wires it in (`async_*` + `co_await` checkpoints), but a bare posted handler does not respond to slot signals. 2d declares a project-owned wrapper:

  ```cpp
  // include/fixpp/core/cancellable_dispatch.hpp
  namespace fixpp::core {

  // Posts `handler` to the session executor `exec` such that:
  //   1. If `slot` is signalled BEFORE the handler is picked up by the
  //      session executor for invocation, the handler is reaped (NOT
  //      invoked) and the reaping is observable through the awaitable's
  //      completion as `expected_t<void>{ unexpect, error::dispatch_aborted }`
  //      (round 2: `dispatch_aborted` is a non-error cancellation outcome
  //      mapped to `FIXPP_ERR_CANCELLED` at the C ABI — see §6.7).
  //   2. If `slot` is signalled DURING the handler's execution, behaviour
  //      degrades to ASIO's standard "next co_await checkpoint observes
  //      cancellation" semantics — the handler runs to its first
  //      cancellable suspension point, then the slot is honoured. The
  //      awaitable completes with the handler's outcome (or
  //      operation_aborted on cancellation).
  //   3. If `slot` is NOT signalled, behaviour is identical to
  //      `asio::dispatch(exec, handler)` plus a single relaxed-atomic
  //      slot check at hand-off (≤ 5 ns added cost). The awaitable
  //      completes with `expected_t<void>{}`.
  //
  // The first parameter is `session_executor` (the project-owned wrapper
  // class — round 2 root cause #1; round 3 root cause #1 re-picked the
  // type from the rejected `any_io_executor` alias to a wrapper class)
  // so the primitive uniformly accepts both `per_session_strand`'s
  // strand-wrapped executor and `direct_executor`'s user-attested
  // serialised executor — the wrapper holds either shape internally.
  // The dispatch node is allocated from the awaiter's session PMR
  // resource (no global heap touch on the dispatch path).
  template <typename Handler>
  [[nodiscard]] asio::awaitable<expected_t<void>>
      cancellable_dispatch(session_executor exec,
                           asio::cancellation_slot slot,
                           Handler&& handler);

  }  // namespace fixpp::core
  ```

  The session FSM uses `cancellable_dispatch` for the parser-completion → `fromApp` hand-off and for any other dispatch where reaping-before-invocation is part of the §4.7 effect-table contract. §9 seam **"Cancellation-slot propagation test (parse → fromApp)"** tests against this primitive's contract directly.

- **`fromApp` for an in-flight message is fire-or-not-fire deterministically.** Under `cancellable_dispatch`, the deterministic three-case answer holds:
  - If the session FSM has posted the dispatch and the session executor has begun invoking `fromApp` when cancellation lands → `fromApp` runs to completion. The user's callback is never partially executed. The next `co_await` checkpoint within the session FSM (after `fromApp` returns) observes the cancellation slot.
  - If the session FSM has posted the dispatch but the session executor has not yet picked it up when cancellation lands → `fromApp` is *not* invoked. The dispatch node observes the slot at hand-off and is reaped (per `cancellable_dispatch` contract step 1). The awaiter completes with `expected_t<void>{ unexpect, error::dispatch_aborted }` — observable through the published `awaitable<expected_t<void>>` return type (round 2 close on the unobservable-outcome defect).
  - If the session FSM has not yet posted the dispatch (still in the parser loop) → `fromApp` is *not* invoked. The parser's `co_await` checkpoint observes the cancellation and unwinds.

- **Logout exchange (graceful).** `Session::close(graceful)` attempts the FIX `Logout` exchange under the child cancellation state per §4.7. The Logout `async_write` and its `Clock::sleep_until` timeout are bound to the child slot — they are NOT pre-cancelled by phase 2's eventual root total. The close-timeout value is picked at the session-module Phase-4 spec (per N-P2-1). Phase 2's root cancellation fires only after phase 1 resolves.
- **Idempotency.** `Session::close(...)` called twice returns the same outcome on the second call without side effects. Calling it on a never-opened or already-closed session returns `error::session_already_closed`.

### 6.6 Clock contract (per-call discipline; C-P2-5 fix)

- **`steady_now()` is monotonic per call within a strand; `now()` is NOT monotonic** (C-P2-5 close). Wall-clock UTC steps backward under NTP correction (`adjtimex`), under admin clock-set, and under leap-second smoothing on some kernels. Strand serialisation prevents *concurrent* observation of the step but does not make wall-clock time monotonic. Linux `clock_gettime(CLOCK_REALTIME)` is *not* monotonic; only `CLOCK_MONOTONIC` is. The v0.1 "two consecutive `now()` calls satisfy `t2 >= t1`" claim is dropped. Concrete consequences:
  - Heartbeat-elapsed measurement uses `steady_now()` only.
  - `sending_time_threshold` checks (when picked at the session-module Phase-4 spec) use `steady_now()` for elapsed deltas; `now()` is consulted only for the wire-formatted UTC timestamp on the outgoing FIX message and for log/OTel record timestamps.
  - The S-035 schedule timing is `steady_now()` + `sleep_until(steady_time_point)`; the wire-side scheduled-event timestamp is `now()`.
  - A benign NTP step never trips a SendingTime-threshold reject (because elapsed deltas are steady-clock-based).
- **Per-call within-strand stability of `now()`.** A single `now()` call returns the value at the call site; nothing about consecutive calls is guaranteed. If a session FSM needs a stable wall-clock value across a span (e.g., to write the same SendingTime to a `MessageStore` journal entry and the wire), it caches the value at the start of the span.
- **Thread-safety of the default impl.** `system_clock_source::now()` and `steady_now()` are safe to call from any thread, any number of times concurrently. `sleep_until` is safe to call from any executor; the awaiter binds to the caller's bound executor for completion (per §4.1.1 step 1). `cancel_sleeps` is safe to call from any thread, including from inside a `sleep_until` completion handler (it is idempotent and will not re-enter).
- **`cancel_sleeps()` interaction with mid-flight `sleep_until` awaiters.** Each in-flight awaiter has been registered in the clock's intrusive list at `sleep_until` entry. `cancel_sleeps` walks the list and signals each awaiter's cancellation slot via the slot mechanism (per §4.1.1 step 2); awaiters complete with `operation_aborted` on their bound executors. The walk is O(N) in the awaiter count; v1.0's worst case is O(2 × number-of-sessions) which is tolerable for 10⁴-session scale at engine shutdown.
- **`~Engine` ordering vs. `Clock` waiters (root cause #5 / N-P1-2 close).** `~Engine` blocks on session drains (every open `Session` is closed with `close_mode::terminal` if not already closing) and clears `EngineConfig::clock` `shared_ptr<Clock>` last. The default `system_clock_source` destructor then drives `cancel_sleeps()` and drains its intrusive list; in well-formed shutdowns there are no waiters left because the sessions drained their heartbeat slots first. Test fixtures that hold a `mock_clock` `shared_ptr` outside the engine see the `mock_clock` outlive the engine — fine, because `mock_clock` is test-only and `cancel_sleeps()` on the mock has no live waiters after engine teardown completes. §9 seam **"Engine-shutdown ordering test"** exercises both shapes.
- **Mock clock determinism.** `mock_clock::advance(delta)` walks the per-deadline ordered map (held in the pimpl) and wakes every awaiter with `deadline ≤ new_steady_now`. This is deterministic across test runs — the same sequence of `advance` calls yields the same wake-up order — provided the test code does not depend on platform-specific scheduler ordering for awaiter resumption.

### 6.7 Errors introduced by this design

| `fixpp::core::error` / `fixpp::session::error` variant | Source section | Remediation class |
|---|---|---|
| `executor_already_stopped` | §4.4 — the resolved executor (`SessionConfig::executor_override.value_or(EngineConfig::executor)`) was joined before `Engine::open` / `Session::open`. | Configuration / lifetime bug. Fix construction order. |
| `executor_not_serialised` | §4.5 / §6.1 / §4.8 — `SessionConfig::mode == direct_executor` was set without `already_serialized_executor = true`. | Construction error; either flip the attestation or use `per_session_strand`. (NEW in v0.2 per root cause #1 / C-P1-2.) |
| `clock_sleeps_cancelled` | §4.1 / §6.6 — emitted when a `sleep_until` waiter completes via `cancel_sleeps`. (Maps to `asio::error::operation_aborted` at the awaitable level; this enum value is the `expected_t` form for callers that prefer.) | Cancellation; not an error in most contexts — caller decides. |
| `strand_dispatch_failed_oom` | §6.2 / §6.5 — PMR fallback for the strand's posted handler (or for `cancellable_dispatch`'s dispatch node) exhausted the per-session arena. | Configuration bug; raise arena cap or fix leak. Forced disconnect. |
| `session_already_open` | §4.7 — `Session::open()` called twice on the same handle. | Programmer error. |
| `session_already_closed` | §4.7 — `Session::close()` called twice or on a never-opened session. | Idempotency contract; not a fatal error — caller may ignore. |
| `invalid_session_config` | §4.5 / §6.1 — incompatible combination, e.g., `direct_executor` + `lock_policy::spin` (rejected even with `already_serialized_executor = true`); or `EngineConfig::executor` is null; or `dictionary` is null; or `security_profile` is the default-constructed sentinel (per `[const §XII.5]` no-implicit-default rule + N-P2-3). | Construction error; reject `Session::open`. |
| `clock_not_set` | §4.4 — `EngineConfig::clock` is null at `Engine::open`, regardless of whether sessions provide `clock_override`. | Configuration bug. (Hardened invariant per root cause #2.) |
| `dispatch_aborted` | §6.5 — `cancellable_dispatch`'s cancellation slot fired BEFORE the posted handler was picked up by the session executor for invocation; the handler was reaped (not invoked). Surfaces as the awaitable's `expected_t<void>{ unexpect, error::dispatch_aborted }` completion. (NEW in v0.3 per round 2 root cause #1 / Codex C-R2-P2-1 — gives the §4.7 effect-table reaping contract an observable C++-level outcome.) | Cancellation; not an error in most contexts — the §4.7 close path expects this on phase 2. |

C-ABI mapping (delegated to **2i**) per N-P2-5 / sibling-doc precedent (`[2c §6.7]`'s `FIXPP_ERR_DICT_*` family + `[2b §6.7]`'s `FIXPP_ERR_WIRE_*` family): the natural coalescing groups are
- configuration errors (`executor_already_stopped`, `executor_not_serialised`, `invalid_session_config`, `clock_not_set`) → **`FIXPP_ERR_THREAD_CONFIG`**;
- lifecycle errors (`session_already_open`, `session_already_closed`) → **`FIXPP_ERR_THREAD_SESSION_LIFECYCLE`**;
- runtime errors (`strand_dispatch_failed_oom`) → **`FIXPP_ERR_THREAD_RUNTIME`**;
- cancellation (`clock_sleeps_cancelled`, `dispatch_aborted`) reuses the existing **`FIXPP_ERR_CANCELLED`** per `[const §XI.2]`.

The `FIXPP_ERR_THREAD_*` prefix matches the per-doc-prefix discipline 2c v1.3 (`FIXPP_ERR_DICT_*`) and 2b v0.2 (`FIXPP_ERR_WIRE_*`) established. The v0.1 generic `FIXPP_ERR_INVALID_CONFIG` / `FIXPP_ERR_RUNTIME` names are dropped to avoid a third doc colliding on the same generic group. Final coalescing is 2i's call. The v0.1 `trace_context_provider_threw` variant is dropped per C-P2-4 (no callable in frozen config — `SessionConfig::initial_trace_context` is value-typed). The v0.1 `cancellation_propagation_timeout` variant is dropped per N-P2-1 (the close-timeout knob lives in the session-module Phase-4 spec, not here). **The v0.2 `version_registry_dictionary_missing` variant is dropped in v0.3 per round 2 / Opus N2-P2-1** — the failure mode is the signed-off `[2c §6.7] dict_no_dictionary_for_application_version` (mapped to `FIXPP_ERR_DICT_CONFIG` at the dict layer). The engine builds the registry via the 2c API at `Engine::open`; failures surface through 2c's variant directly. 2d's §4.4 already cites `[2c §6.7]` for the dispatch-time path; the engine-init-time path now also routes through 2c's variant rather than introducing a 2d-layer synonym that would route the same failure to a different C-ABI group.

---

## 7. Integration with adjacent modules

### 7.1 Wire (`[arch §4.3]`, owner **2b**)

The wire `Parser` runs on the session strand (per `[2b §6.6]` "Framer is not thread-safe — one per session, owned by the session's I/O strand"). When the parser produces a `MessageView`, the session FSM dispatches to `fromApp` *on the same strand* — no extra hop. The parse → reify → dispatch sequence is one strand-local invocation chain; HALO targets this chain for elision (§6.2).

`MessageView` aliases the per-message arena; capturing it past `fromApp` return is undefined per `[2b §6.6]` view-escape contract. If the user needs to cross strands (e.g., post processing to a thread-pool), they call `dict::reify(view, profile, mr) → owning_message_t<>` (defined in `[2c §4.8]`) before the cross-strand handoff. 2d budgets the end-to-end cost (`reify` + cross-strand `dispatch`) in §6.3 per C-P2-3; 2b/2c own the API. 2d records this.

### 7.2 Codegen / typed messages (`[arch §4.2]`, owner **2c**)

`dict::reify(view, dictionary)` is called on the session strand, returning a typed `fixpp::vXX::*` flyweight. The user's `fromApp(typed_message)` receives the typed value and runs on the strand. Mid-session swap of dictionary is rejected categorically per `[2c §7.2]` / `[arch §5.6]`; 2d records but does not re-decide that.

### 7.3 MessageStore (`[arch §4.4]`-adjacent, owner **2e**)

Default discipline: `MessageStore::write(awaitable<void>)` is called on the session strand. The store impl may post work onto a different executor (e.g., a dedicated I/O thread for `FileStore`); the awaiter's completion rebinds to the session strand for callback delivery. 2e owns the precise shape; 2d guarantees the strand is the dispatch target.

### 7.4 Awaitable mutex (2f)

2d locks the **executor-compat surface** that `fixpp::sync::async_mutex` (2f) must satisfy:

- The mutex's `async_lock` awaitable must complete on the awaiter's bound executor, not on a foreign executor. If the awaiter is bound to the session strand, the completion runs on the strand.
- The mutex (per `[2f §4.1.1]` v1.3 surface `async_mutex::async_lock(...)` — the `[2d §4.7]` table's historical row name `async_mutex::lock` maps to this 2f API) must honour `asio::cancellation_type::total` per `[SYN §3.2 Q6b]` item 3 — when cancellation is signalled, the waiter is removed from the LIFO list (via the per-waiter `phase_` CAS protocol per `[2f §4.5]`) and the awaitable completes with `expected_t::unexpected{error::sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]` / `[2f §6.5]`. The `[2d §6.5]` `cancellable_dispatch → expected_t::unexpected{dispatch_aborted}` precedent is the project-internal idiom this surface matches.
- The mutex's `dispatch` vs `post` policy (per `[SYN §3.2 Q6b]` item 4) defaults to `dispatch`. Choosing `post` adds an executor hop on completion; HFT/fairness-sensitive sites pick this. 2d locks the *default* as `dispatch`; per-mutex override is 2f's call.

2f's internals (waiter-embedded, PMR-aware fallback, FIFO drain cycle, destructor semantics, test list) are 2f's responsibility. 2d does not prescribe them.

### 7.5 TLS (`[arch §4.6]`, owner **2g**)

The TLS handshake coroutine (`async_handshake`) runs on the session strand. Cert rotation triggered via `Pinset::add(cert)` / `Pinset::remove(cert)` per `[arch §4.6]` may run on any thread — the pinset object is independently thread-safe (owned by 2g). When a rotation lands during a session, the session's next handshake (e.g., on reconnect) picks up the new pinset; mid-handshake rotation does not affect an in-flight handshake. 2d records the strand-safety boundary; 2g owns the rotation timing.

### 7.6 Transport (`[arch §4.5]`, owner **2h**)

`Transport::async_connect`, `async_read_some`, `async_write`, `cancel`, `close` all run on the session strand. The default `asio_tls_transport` impl uses ASIO's own composed operations; cancellation propagates through ASIO's slot mechanism. Transport reset (e.g., reconnect after a network blip) re-issues `async_connect` on the same strand. 2d locks the strand-discipline; 2h owns the impl.

### 7.7 C ABI (`[arch §4.10]`, owner **2i**)

Threading concepts crossing the C ABI per §5: no explicit strand, no explicit `Clock`, no explicit `trace_context` awaitable; only `fixpp_session_close()` for cancellation and `fixpp_session_get_trace_id()` for log correlation. 2i locks the symbol shapes and reentrancy classifications.

### 7.8 Control plane (`[arch §4.11]`, owner **2j**)

Control-plane handlers (gRPC `OpenSession`, `CloseSession`, `Configure`, `StreamMetrics`, `StreamLogs`) run on the engine executor *outside* any session serialisation domain. They use `co_await fixpp::current_trace_context` to get the *engine-level* fallback `trace_context` per `[arch §5.4]`. The fallback path is well-defined (N-P2-2 close, refined under round 2 root cause #1, finalised under round 3 root cause #1): the awaiter reads its bound executor (`co_await asio::this_coro::executor`) and tries to recover it as a `fixpp::core::session_executor` value (the project-owned wrapper class — §4.8) via static type recovery, then calls `session_executor::session_ptr()` on a hit to obtain the typed `Session*`. Inside a session serialisation domain the awaiter's bound executor IS a `session_executor` wrapper (the engine binds session coroutines to it at `Session::open` per §4.8), and the awaiter reads the session's `session_local<trace_context>` slot (§4.6). Outside any session — control plane, listener accept, engine bootstrap — the awaiter's bound executor is a plain `asio::any_io_executor` (the user-supplied executor, never wrapped in `session_executor` by the engine), wrapper-type recovery misses, and the awaiter falls through to the engine's atomic snapshot of `EngineConfig::engine_trace_context` (§4.4). The recovery path does not depend on ASIO's property-query pipeline — round 3 root cause #1 / Codex C-R3-P1-1 closed the regression where the round-2 typed-property formulation routed through `any_io_executor`'s closed property set and either failed to compile or always missed. Where a control-plane operation must cross into a session (e.g., `CloseSession` triggering `Session::close()`), the handler dispatches onto that session's `session_executor` wrapper and the in-domain awaiter then reads the session's `session_local<trace_context>` slot. 2j owns the control-plane interface; 2d records the domain-crossing convention.

### 7.9 Log + OTel (`[arch §4.7]`, `[arch §4.8]`, owner **2k**) — single effective_clock per session (root cause #2)

**Effective-clock rule (root cause #2 / C-P1-4 close).** Per session: `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock`. The engine resolves `effective_clock` once at `Session::open` and binds it to the session's lifetime. Every session-scoped consumer reads from `effective_clock`:

- Heartbeat (`Clock::steady_now()` for elapsed; `Clock::sleep_until(...)` for the timer; `Clock::now()` for any wire-formatted timestamp on outgoing heartbeat / TestRequest frames).
- SendingTime threshold checks (per §6.6 fix: `steady_now()` for elapsed deltas, `now()` for the wire timestamp).
- S-035 session scheduling (`steady_now()` + `sleep_until(...)`).
- **Session-scoped LOG/OBS records** — every record produced under the session's strand carries `clock_scope = session` and the timestamp is `effective_clock.now()`. Examples: every `fromApp` log line; every per-message OTel span; every per-session metric counter increment.

**Engine-scope LOG/OBS records** — records produced outside any session strand (listener accept, control-plane handlers, engine bootstrap, engine-level metrics). These read `EngineConfig::clock->now()` directly and carry `clock_scope = engine`. The discriminator field `clock_scope = {engine, session}` is owned by 2k's record schema (per `[arch §5.7]` logging integration); 2d's commitment is that the producer-side timestamp source matches the discriminator.

**Closing §10 Q5.** Under the v0.1 design, `SessionConfig::clock_override` was a session-level mock option but `[2d v0.1 §7.9]` hardwired LOG/OBS to `EngineConfig::clock`, producing the conformance-corpus-mismatch the v0.1 §10 Q5 admitted. Under the v0.2 design, session-scoped records use `effective_clock` (so a `mock_clock` set as `clock_override` deterministically drives session-scoped log/OTel timestamps in conformance-corpus runs); engine-scope records continue to use `EngineConfig::clock`. The §10 Q5 question closes here, in this doc, not deferred to 2k. **2k is bound to honour the `clock_scope` discriminator** in its sink interface; the drop-in language is in §11 Hand-off.

The session-domain `trace_context` populates every log record's `trace_id` / `span_id` per `[const §XIII.3]`. The producer-side log call extracts the awaiter's value synchronously: in-session-domain records read the session's `session_local<trace_context>` slot through the typed `Session*` recovered from the `session_executor` wrapper's `session_ptr()` member-function accessor (per §4.6 / §4.8 — round 3 root cause #1: a member function on the project-owned wrapper class, not an ASIO property query); engine-scope records read the engine's atomic snapshot of `EngineConfig::engine_trace_context` (per §4.4 + §7.8).

### 7.10 Tap (`[arch §4.9]`, owner **2l**)

The tap consumer reads `frame_view::bytes()` from the parser hand-off, on the session strand. `drop-oldest` is permitted on the tap path per `[const §XV.15]` exception (tap is telemetry, not the FIX message stream). The session FSM does not block on tap publication; tap backpressure is the tap's own concern. 2d records the strand-discipline; 2l owns the ring buffer + backpressure policy.

### 7.11 SWIG / Python (`[arch §4.12]`, owner **2m**)

Python callbacks reacquire the GIL on receive (per `[arch §4.12]`); the GIL reacquire happens *inside* the strand's invocation of `fromApp`. The strand serialisation guarantee implies that two `fromApp` invocations on the same session do not contend for the GIL — no second `fromApp` runs until the first returns. 2m owns the GIL discipline; 2d records the implication.

---

## 8. PMR — recap

Threading touches the following PMR arenas (canonical reference for cross-doc consistency; the wire-layer arenas are the load-bearing ones from `[2b §8]`):

| Arena | Lifetime | Holds (threading-relevant) | Reset by |
|---|---|---|---|
| `EngineConfig::default_session_resource` (or `SessionConfig::session_arena`) | session lifetime | session-executor wrapper state (the project-owned `session_executor` value-typed class from §4.8, holding the resolved `asio::any_io_executor` inner executor — strand-wrapped under `per_session_strand` mode, bare under `direct_executor` mode — plus the typed `Session*` slot — round 3 root cause #1 wrapper-class shape), awaitable-mutex state (2f), the per-session reusable `steady_timer` slot pool used by `system_clock_source::sleep_until` (per root cause #5 / N-P1-1; keyed by `Session*` per round 2 root cause #1), `cancellable_dispatch` node storage (§6.5), the `Session::trace_slot_` `session_local<trace_context>` member's storage (per §4.6) | session destruction |
| `EngineConfig::default_message_resource` (or `SessionConfig::message_arena`) | per-message (reset after `fromApp`) | inbound message dispatch coroutine frames (PMR fallback when HALO does not fire); `dict::reify` typed-message construction scratch | session FSM after `fromApp` returns |
| `SessionConfig::framer_carry_arena` | session lifetime | (owned by 2b — recorded here for cross-doc consistency; unused by 2d directly) | session destruction |

Per N-P3-1: `[2b §6.6]` actually defines four PMR arenas (per-message, framer-carry, session-lifetime, plus a parser-completion arena that 2b folds into per-message); the "three-arena" framing above is at the level 2d cares about.

Lifetime classes for non-arena objects:

- **Session-executor (`fixpp::core::session_executor`, project-owned value-typed wrapper class)** — session lifetime, value-semantic; copied on construction and held by the `Session` instance. Under `per_session_strand` mode the wrapper holds an `asio::strand<asio::any_io_executor>` over the resolved executor; under `direct_executor` mode the wrapper holds the user-attested already-serialised `asio::any_io_executor` (no strand). Both shapes satisfy the session-serialisation-domain contract (round 2 root cause #1) and both expose the typed `Session*` via the wrapper's `session_ptr()` member-function accessor consumed by `session_local<T>`'s access mechanism (§4.6) — round 3 root cause #1: a member function on the project-owned wrapper class, NOT a property query on `asio::any_io_executor`.
- **`session_local<trace_context>` slot** — session lifetime, populated at `Session::open()` from `SessionConfig::initial_trace_context`. Cleared at `Session::close()` completion. Storage is inside the `Session` object (root cause #3 / §4.6; renamed from v0.2's `strand_local<T>` per round 2 root cause #1).
- **`Clock` instance** — engine lifetime (or longer if user holds `shared_ptr` for test scenarios). `~Engine` blocks on session drains and clears its `EngineConfig::clock` `shared_ptr` last (root cause #5 / §10 Q3 close).
- **Per-session reusable `steady_timer` slot pool** — session lifetime; allocated lazily at first `sleep_until` on the session from `SessionConfig::session_arena`; **keyed by `Session*`** (round 2 root cause #1 — drops the v0.2 "strand handle" keying that broke under `direct_executor`); reused on every cycle (no per-cycle heap touch, satisfying `[const §VIII.5]`'s extension to the heartbeat path per N-P1-1).
- **Engine-level fallback `trace_context` snapshot** — engine lifetime; storage is a `std::atomic<trace_context>` (or `seqlock`-protected snapshot if the type is not `is_always_lock_free`) inside the `Engine` object (per §4.4 / N-P2-2).
- **`mock_clock` pimpl** — test-only; lifetime is the `mock_clock` instance's. Tests that hold the `mock_clock` past its bound `asio::io_context::run_for(...)` is the user's bug; debug builds assert if waiters survive destruction.

Per `[const §VIII.5]`: zero `new`/`delete` between parse and `fromApp`, **extended here to the heartbeat path** because heartbeat fires between messages on the same strand (per N-P1-1). The strand's `dispatch` (and `cancellable_dispatch`) of a posted handler is the critical step; HALO elides its frame in the warm path, PMR fallback uses `message_arena` in the cold path. The `system_clock_source::sleep_until` path reuses the per-session timer slot, so heartbeat allocations are bounded to the one-time slot construction at session open. `tools/check_alloc.py` (§9 seam **"Allocation guard on dispatch hot path"**) verifies under `mallocnesia` on Linux/Clang Tier 1; per N-P2-4 the guard catches *global-heap* `new`/`delete`/`malloc` between parse and `fromApp` — PMR-arena allocations are expected and not flagged.

---

## 9. Test seams

Per `[arch §10]` requirement (4): every design doc ends with the test seams it exposes. ≥ 10 seams required by the brief; v0.4 ships 21 seams (v0.3's 20 plus 1 added under the round-3 post-cap line-edit pass to cover the round-3 root cause: the wrapper-class typed accessor must survive ASIO's executor pipeline). Seams are referenced by **name** rather than ordinal in cross-references (per P3-3 — names are stable across review rounds).

1. **`mock_clock` determinism test.** Construct two parallel `mock_clock` instances seeded identically; drive each with the same sequence of `advance` calls; verify each clock's `now()`, `steady_now()`, and `sleep_until` wake-up order match. Catches non-determinism in the awaiter resumption path. Lives in `tests/core/test_mock_clock_determinism.cpp`.
2. **Strand-serialisation property test.** Construct a `Session` with the default `per_session_strand` mode; concurrently invoke `Session::send(...)` from N user threads; verify `fromApp` and `toApp` callbacks observe a single-threaded ordering (no two callbacks ever overlap). Run under TSan on Linux/Clang Tier 1. Lives in `tests/session/test_strand_serialisation.cpp`.
3. **Executor-opt-out compatibility test.** Build the session against three executor shapes — `asio::thread_pool::executor_type`, `asio::system_executor`, and a custom one-thread `asio::io_context::executor_type` — under both `per_session_strand` and `direct_executor` (with `already_serialized_executor = true`) modes (six combinations). Verify a Logon → NewOrderSingle → ExecutionReport → Logout sequence completes correctly in each. Lives in `tests/session/test_executor_compat.cpp`.
4. **Cancellation-slot propagation test (parse → `fromApp`).** Inject a `mock_clock` whose `sleep_until` parks indefinitely. Issue `Session::close(graceful)` while a `fromApp` is pending dispatch on the session executor. Verify the pending dispatch is reaped before invocation **via `cancellable_dispatch`**'s contract (§6.5) — the awaitable form completes with `expected_t<void>{ unexpect, error::dispatch_aborted }` (round 2 root cause #1 close on the unobservable-outcome defect). The seam tests against `cancellable_dispatch`'s slot-check-before-invoke property, the awaitable's `dispatch_aborted` outcome observability, and the deterministic three-case answer in §6.5. Run under both `per_session_strand` and `direct_executor` modes — the primitive's `session_executor` first parameter (the project-owned wrapper class — round 3 root cause #1) unifies both. Lives in `tests/session/test_cancellation_parse_to_fromapp.cpp`.
5. **Cancellation-slot propagation test (`fromApp` → close).** Block inside `fromApp` on a `co_await Clock::sleep_until` that is cancelled by `Session::close(terminal)` (terminal because phase 1 is skipped — direct phase-2 root cancellation). Verify `fromApp` returns with `operation_aborted` and the close drains. Variant: under `Session::close(graceful)` the heartbeat-blocked `fromApp` runs to user-callback return during phase 1, and phase 2's root total then propagates. Lives in `tests/session/test_cancellation_fromapp_to_close.cpp`.
6. **`trace_context` coroutine-resume-on-different-thread test.** Construct an `asio::thread_pool` with N=4 threads; bind a session over it; in `fromApp`, capture the trace_context (read through `co_await fixpp::current_trace_context` resolving against the session's `session_local<trace_context>` slot via the `session_executor::session_ptr()` member-function accessor on the awaiter's bound wrapper per §4.6 / §4.8), `co_await` a sleep that resumes on a different thread (verified via thread-id assertion), re-read the trace_context, verify byte-equality. Catches the `thread_local` regression and verifies the wrapper-class `Session*` accessor survives resume. Run under both `per_session_strand` and `direct_executor` modes (round 2 close — the access mechanism is identical across modes; round 3 close — the wrapper-class shape replaces the rejected typed-property formulation). Lives in `tests/core/test_trace_context_resume.cpp`.
7. **Allocation guard on dispatch hot path.** Run a 10⁴-message Logon → NewOrderSingle → ExecutionReport → Logout corpus through the session under `tools/check_alloc.py` + `mallocnesia` (Linux/Clang). Per N-P2-4: the seam catches *any global-heap* `new`/`delete`/`malloc` between parse and `fromApp` (PMR-arena allocations are expected and not flagged). Trip wires: the `system_clock_source` per-session timer slot pool must allocate exactly once per session (at slot construction), not per heartbeat cycle. Catches HALO regressions and N-P1-1 regressions. Lives in `tests/perf/test_dispatch_alloc_guard.cpp`. (Same Linux-only caveat as `[2a §9]` seam #6 / `[2b §9]` seam #10.)
8. **Latency regression bench.** Google Benchmark on the §6.3 ceilings: strand dispatch handoff (in-strand and cross-thread), **cross-strand `reify` + dispatch (20-tag and 200-tag) per C-P2-3**, `Clock::now()` (default), `Clock::steady_now()` (default), session-domain `trace_context` access (in-domain via the `session_executor::session_ptr()` member-function accessor and engine-fallback). CI fails on >5% regression vs baseline. Lives in `bench/threading/`.
9. **Heartbeat-window simulation under `mock_clock`.** Construct a session with `heartbeat_interval` set to a session-module-spec value (test fixture picks it; 2d does not pin the value); inject `mock_clock`; advance time and verify the session-module FSM emits Heartbeat / TestRequest / disconnect-on-no-response per the session-module Phase-4 spec. All without real wall-clock time. Lives in `tests/session/test_heartbeat_under_mock_clock.cpp`.
10. **`sleep_until` + `cancel_sleeps` race test.** Spawn N=100 `sleep_until` awaiters with deadlines spread across [now, now+10s]; concurrently fire `cancel_sleeps` from another thread; verify every awaiter completes (either with `operation_aborted` or with the deadline-reached path), no leaks (TSan + ASan clean), no double-completion. Lives in `tests/core/test_sleep_cancel_race.cpp`.
11. **Conformance corpus run on injected `mock_clock`.** The 17-scenario FIX-TC conformance corpus (`tests/conformance/`) runs with `mock_clock` injected via `SessionConfig::clock_override`. Per §7.9's effective-clock rule, session-scoped LOG/OBS records, SendingTime, and S-035 timers all read from the override; CI diff-checks the output against a golden expected log. Catches any clock-source leak (a code path using `std::chrono::system_clock::now()` directly instead of `effective_clock.now()`). Engine-scope records (listener accept) are not in the corpus and are unaffected. Lives in `tests/conformance/test_corpus_mock_clock.cpp`.
12. **Fuzz harness for cancellation timing.** libFuzzer-driven cancellation injection: at every `co_await` checkpoint in the session FSM, the fuzzer may fire `Session::close(graceful)` or `Session::close(terminal)`. Verify the engine never deadlocks, never double-frees, never leaks PMR memory. Required by `[const §IX.4]` (fuzzing for parser-touching modules — extended here to threading-touching code per Gate-A discretion). Lives in `tests/fuzz/fuzz_session_cancellation.cpp`.
13. **Drop-oldest banned-on-app-path enforcement test.** Compile-time: a static_assert at every `switch` over `SessionConfig::backpressure_mode` enumerates exactly two cases (`block`, `disconnect_and_recover`); attempting to extend the enum with a `drop_oldest` value triggers the static_assert. Runtime: a unit test attempts to construct a `SessionConfig` with an out-of-range backpressure value (cast from an int) and verifies the engine rejects it with `error::invalid_session_config`. **Justification for compile-fail-not-runtime-reject:** the locked decision is a constitutional ban (`[const §XV.15]`), not a runtime config knob; making it unrepresentable at the type level matches the constitutional discipline of "banned patterns are CI-enforced wherever feasible" `[const §XV]`. The runtime check is a defence-in-depth backstop for FFI / SWIG paths that may erode the type-level guarantee. Lives in `tests/session/test_backpressure_drop_oldest_banned.cpp`.
14. **Engine-shutdown ordering test (root cause #5 / N-P1-2 close).** Construct an engine with two open sessions; tear down the engine while one session has an in-flight `fromApp` and another has an in-flight `Clock::sleep_until` heartbeat. Verify `~Engine`'s ordering: (a) every open session is closed (`close_mode::terminal`); (b) every session drains; (c) `system_clock_source::cancel_sleeps()` fires; (d) the intrusive waiter list drains; (e) `EngineConfig::clock` `shared_ptr` is cleared last. Variant: a test fixture that holds a `mock_clock` `shared_ptr` outside the engine — verify the mock outlives the engine and is destructible after `~Engine` returns. No leaks, no UB. Lives in `tests/core/test_engine_shutdown_order.cpp`.
15. **Third-party `Clock` conformance test (root cause #5 / C-P2-7 close, NEW in v0.2).** Implement a minimal third-party `Clock` derivative (not `system_clock_source`, not `mock_clock`) following the §4.1.1 implementer's recipe: `co_await asio::this_coro::executor` for binding, `co_await asio::this_coro::cancellation_state` for slot wiring, allocation via `bind_allocator(pmr_allocator, ...)` (pattern (b)). Drive a session with this clock through Logon → NewOrderSingle → cancel; verify (a) `sleep_until` completion lands on the awaiter's bound executor; (b) `cancellation_type::total` and child-state cancellation both honour the slot; (c) the global-heap allocation guard from seam **"Allocation guard on dispatch hot path"** still passes; (d) cross-thread completion uses `bind_executor`. Catches third-party Clock plugin contract drift. Lives in `tests/core/test_third_party_clock_conformance.cpp`.
16. **`direct_executor` re-entrancy guard test (root cause #1 / new in v0.2).** Construct a session with `mode == direct_executor` + `already_serialized_executor = true` against a `serial_executor_adapter` (test-only) that wraps a strand. Verify a Logon → NewOrderSingle → ExecutionReport → Logout sequence runs correctly. Then construct the same session against a *non-serialised* `asio::thread_pool::executor_type` (still claiming `already_serialized_executor = true`) — debug builds trip the strand-invariant assert (the engine asserts on detected concurrent FSM entry); release builds are documented UB (the user broke the contract). Also: verify `direct_executor + already_serialized_executor = false` rejects at construction with `error::executor_not_serialised`. Lives in `tests/session/test_direct_executor_reentrancy.cpp`.
17. **`session_local<T>` lifetime-under-cancellation test (root cause #3 / new in v0.2; renamed in v0.3 per round 2 root cause #1; access mechanism finalised in v0.4 per round 3 root cause #1).** Construct a session; populate the `session_local<trace_context>` slot at session open; inside `fromApp`, capture `co_await fixpp::current_trace_context`; trigger `Session::close(graceful)` mid-`fromApp` such that phase 2's root cancellation lands during the user callback's return. Verify the slot remains valid until the close completes (the slot's storage is the `Session` object, which is alive through the close coroutine), and that the awaiter never reads through a destroyed slot. Variant: cross-thread resume — verify the typed `Session*` recovery via the `session_executor::session_ptr()` member-function accessor survives a resume on a different pool thread. Lives in `tests/core/test_session_local_lifetime.cpp`.
18. **Allocation guard on `Clock::sleep_until` path (root cause #5 / N-P1-1 close, new in v0.2).** Drive a session for N=10⁴ heartbeat cycles under `tools/check_alloc.py` + `mallocnesia` (Linux/Clang). Verify no global-heap allocation occurs on the heartbeat path after the first cycle (the per-session `steady_timer` slot is allocated once at session open from `session_arena` and **keyed by `Session*`** per round 2 root cause #1; subsequent cycles reset `expires_at` and call `async_wait` with no allocation). Run under both `per_session_strand` and `direct_executor` modes — the keying axis is `Session*`, not strand handle, so both modes converge on the same lifetime contract. Catches N-P1-1 regressions and round-2 keying-axis regressions. Lives in `tests/perf/test_clock_sleep_alloc_guard.cpp`.
19. **`session_executor` round-trip across both threading modes (round 2 root cause #1, NEW in v0.3; access mechanism re-shaped in v0.4 per round 3 root cause #1).** Construct two sessions: one under `per_session_strand` (the default), one under `direct_executor` with `already_serialized_executor = true`. For each, exercise the four round-1 primitives that round 2 re-typed against `session_executor` (the project-owned wrapper class — round 3 root cause #1: a class, not an alias to `any_io_executor`): (a) `cancellable_dispatch(session_executor, slot, handler)` — verify the awaitable completes with `expected_t<void>{}` on success and `expected_t<void>{ unexpect, error::dispatch_aborted }` on slot-signal-before-pickup; (b) `session_local<T>::load()` — verify the `session_executor::session_ptr()` member-function accessor recovers the typed `Session*` and the load returns the populated slot value; (c) the `system_clock_source` per-session timer slot pool — verify the slot is keyed by `Session*` and reused across cycles in both modes; (d) the §4.7 effect-table reaping rows — verify the close path's "reaped by `cancellable_dispatch`" effect lands under both modes. Catches round-2 root-cause regressions where any of the round-1 primitives drift back to a strand-only signature. Lives in `tests/core/test_session_executor_round_trip.cpp`.
20. **`version_registry` dictionary-missing routes through `[2c §6.7]` (NEW in v0.3 per Opus N2-P2-1).** Construct an `EngineConfig` whose `dictionaries` list omits a version that a downstream FIXT.1.1 session will reach via `ApplVerID(1128)` per-message override. Open the engine; open the session; receive a per-message override frame for the missing version. Verify that the failure surfaces as `[2c §6.7] dict_no_dictionary_for_application_version` (mapped to `FIXPP_ERR_DICT_CONFIG` at the C ABI), **not** as a 2d-layer variant routing to a different C-ABI group. Variant: the registry-construction failure at `Engine::open` — same expectation; the engine builds the registry through the 2c API, which raises the 2c variant directly. Catches future drift toward a 2d-layer synonym that would route the same failure to a different C-ABI group. Lives in `tests/session/test_version_registry_missing_routes_to_dict_layer.cpp`.
21. **`session_executor` typed-accessor survives ASIO erasure (NEW in v0.4 per round 3 root cause #1 / Codex C-R3-P1-1).** Compile-time + runtime guard against the round-3 regression: the typed `Session*` accessor on the project-owned wrapper class must survive ASIO's `bind_executor` and `make_strand` decoration on engine-controlled paths. **Compile assertion:** the test `static_assert`s that `fixpp::core::session_executor` satisfies `asio::execution::is_executor_v<...>` (the wrapper IS an executor concept), AND that `bind_executor(session_executor, awaitable)` returns an awaitable whose bound-executor type is convertible back to `session_executor` (the wrapper survives `bind_executor`). **Runtime assertion:** spawn an `asio::awaitable<void>` coroutine bound to a `session_executor` value via `bind_executor`; inside the coroutine, `co_await asio::this_coro::executor` and verify the recovered executor is a `session_executor` value (not erased into `asio::any_io_executor`); call `session_ptr()` on the recovered wrapper and verify it returns the original `Session*`. Repeat the assertion after wrapping the awaitable in an outer `make_strand` over the wrapper — verify `session_ptr()` is still reachable on the inner-coroutine's bound executor. **Negative assertion:** explicitly cast a `session_executor` value to `asio::any_io_executor`; verify that the resulting `any_io_executor` does NOT support a `query` for any project-typed `Session*` property (this is the v0.3 regression shape; the test documents it as a known-bad path so future contributors do not re-introduce it). Catches the C-R3-P1-1 regression where the typed accessor was routed through `any_io_executor`'s closed property set. Lives in `tests/core/test_session_executor_accessor_survives_erasure.cpp`.

---

## 10. Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | Exact awaitable-mutex executor binding shape — does `fixpp::sync::async_mutex::async_lock` take an `asio::any_io_executor` parameter, or does it inherit from the awaiter's bound executor? Per N-P3-3: the *contract* is locked here at §7.4 ("completion on awaiter's bound executor", "honour `total`", "default `dispatch`"); only the *signature* is 2f's. | SIGNATURE DEFERRED to **2f**; contract locked here per §7.4. | 2f |
| 2 | C ABI cancellation token representation — should `fixpp_session_close()` return a token that the caller can `wait_for(...)` on, or is it fire-and-forget with the session's eventual close-completion observable through a callback? 2d records that no token leaks across the C ABI; the precise pattern is 2i's call. | DEFERRED to **2i**. | 2i |
| 3 | ~~Engine-shutdown ordering relative to in-flight sessions~~ | **CLOSED in v0.2** per root cause #5 / N-P1-2: `~Engine` blocks on session drains and clears `EngineConfig::clock` `shared_ptr<Clock>` last (§4.4 + §6.6 + §9 seam **"Engine-shutdown ordering test"**). Test fixtures that hold a `mock_clock` `shared_ptr` outside the engine see the mock outlive the engine — fine, because the mock is test-only and `cancel_sleeps()` on the mock has no live waiters after engine teardown completes. | — |
| 4 | Cross-thread `dispatch` ceiling (250 ns per §6.3) — is this defensible, or do we want stricter? Tied to OS scheduler jitter; a more aggressive bench harness on isolated cores might justify ≤ 100 ns. | Bench spike during 2d implementation; revise if jitter measurements suggest headroom. | 2d follow-up |
| 5 | ~~`Clock` per-session override interaction with `mock_clock` for log timestamps~~ | **CLOSED in v0.2** per root cause #2 / C-P1-4: §7.9 publishes the single-effective-clock rule (`effective_clock = clock_override ?: engine.clock`); session-scoped LOG/OBS records read from `effective_clock` (so a `mock_clock` set as `clock_override` deterministically drives session-level conformance-corpus runs); engine-scope records read from `EngineConfig::clock`. The `clock_scope = {engine, session}` discriminator is owned by 2k's record schema (drop-in language in §11). | — |
| 6 | ~~Session-domain `trace_context` storage mechanism — `query` through `asio::any_io_executor` vs a `session_local<T>` template~~ | **CLOSED in v0.2 (storage axis), refined in v0.3 (access axis), and finalised in v0.4 (access mechanism re-shaped from typed-property to wrapper-class) per round 3 root cause #1**: §4.6 publishes `fixpp::core::session_local<T>` (renamed from v0.2's `strand_local<T>` so the keying axis is the session serialisation domain, uniform across both threading modes) and the `Session`-owned slot. The access mechanism is a public `session_ptr()` member-function accessor on the project-owned `fixpp::core::session_executor` value-typed wrapper class (§4.8 — round 3 root cause #1: the v0.3 typed `session_ptr_property` query on a `session_executor_t = any_io_executor` alias was rejected because `any_io_executor`'s closed property set does not forward arbitrary user-defined queries, regression-equivalent to round-1's rejected `query(void*)` design; the wrapper-class shape replaces the property-query pipeline with a direct member-function call on the project type that consumers bind to). The `any_io_executor::query(void*)` path remains rejected; the round-2 typed-property path is also rejected; the wrapper-class member-function path is the closed shape. | — |
| 7 | (NEW in v0.2) Should the `Engine` expose a runtime-mutable `set_engine_trace_context(trace_context)` mutator, or freeze the engine-level fallback at `Engine::open` like every other engine config field? §4.4 documents the mutator as permissible (the engine-level fallback is observability-shaped and not part of the session-FSM input axis), but no upstream consumer requires runtime mutation in v1.0. The `EngineConfig::engine_trace_context` field is the open-time value; the mutator is a v1.0-or-v1.1 question. | Phase 2 implementation choice; default to "no mutator in v1.0" unless a consumer surfaces. | 2d follow-up |

---

## 11. Hand-off

**Docs unblocked by 2d sign-off (downstream):**

- **2e** (MessageStore async API) — needs the strand-discipline + `Clock::now()` for journal timestamps.
- **2f** (`fixpp::sync::async_mutex`) — needs the executor-compat surface from §7.4.
- **2g** (TLS `cert_source` + pinset) — needs the strand-safety boundary from §7.5.
- **2h** (Transport interface) — needs the strand-discipline + cancellation contract from §7.6.
- **2i** (C ABI message rep + error enum) — needs the cancellation-flow conventions from §5.
- **2j** (Control plane) — needs the engine-level fallback `trace_context` + cross-strand convention from §7.8.
- **2k** (Log + OTel) — needs the `Clock` source feeding LOG / OBS timestamps from §7.9.
- **2l** (Session tap) — needs the strand-discipline + tap-backpressure exception from §7.10.
- **2m** (SWIG / Python) — needs the GIL-on-strand discipline from §7.11.

Effectively the entire downstream of the threading model unblocks.

**Catalogue + coverage-index amendments owed at sign-off** (drop-in language pattern from `[2c App D]`; the orchestrator applies these during the sign-off commit, not the 2d rewrite agent):

- Add **NFR-015** to `library/spec/feature-catalogue.md`:
  > **NFR-015 — Pluggable Clock interface.** The engine's timing is sourced from a pluggable `fixpp::core::Clock` interface (4 pure-virtual methods: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`. Default impl `fixpp::core::system_clock_source` wraps `std::chrono::system_clock` + `std::chrono::steady_clock` + ASIO `steady_timer` (with per-session reusable timer slots keyed by `Session*` from `session_arena` for zero-per-cycle heap allocation on the heartbeat path under both `per_session_strand` and `direct_executor` modes). Test impl `fixpp::core::mock_clock` ships in `<fixpp/core/test/mock_clock.hpp>` (pimpl'd over an opaque mutable-state object to satisfy `[const §XI.3]`). The `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` rule (per `[2d §7.9]`) routes heartbeat (S-003 / S-004), SendingTime, S-035 session scheduling, and **session-scoped** LOG/OBS records through the per-session clock; engine-scope LOG/OBS records read from `EngineConfig::clock` directly and carry a `clock_scope = engine` discriminator. NFR-015 covers the *clock seam* only; the consuming-row owners (the session-module Phase-4 spec for S-003/S-004/S-035, **2k** for LOG-001..004 + OBS-001..003) discharge their own rows. Per `[2d §4.1]` and `[2d Appendix A]`.
- Add a corresponding entry to `library/spec/coverage-index.md` linking `[2d §4.1]` and `[arch §1.1]` (pluggable clocks promise) to **NFR-015**.
- Update `[arch §11]` row 7 disposition from `TODO — added when 2d lands` to `DONE — added in feature-catalogue.md by 2d v<final>`.
- **Drop-in language for 2k's record schema** (consumed by 2k at its sign-off; per `[2d §7.9]` close of v0.1 §10 Q5):
  > Each `log::Record` and OTel span carries a `clock_scope` field with values `engine` or `session`. Producer-side records emitted under a session strand carry `clock_scope = session` and read their timestamp from `effective_clock.now()`; producer-side records emitted outside any session strand (listener accept, control-plane handlers, engine bootstrap) carry `clock_scope = engine` and read their timestamp from `EngineConfig::clock->now()`. The discriminator is part of the sink interface contract; backends that do not need it MAY drop it.

The amendments are **not** applied by 2d itself; per `[2c App D]` precedent, the rewrite agent does not edit `feature-catalogue.md`, `coverage-index.md`, or `architecture.md` directly. The orchestrator (parent session) applies the amendment text during the sign-off commit.

---

## Appendix A — Catalogue row coverage

This doc owns one new catalogue row and touches several others.

### A.1 Owned

| Row | Family | What 2d covers | Status |
|---|---|---|---|
| **NFR-015** (NEW) | NFR — pluggable Clock interface | `fixpp::core::Clock` interface, `system_clock_source` default impl, `mock_clock` test impl, `EngineConfig`/`SessionConfig` wiring, integration with heartbeat / SendingTime / log + OTel timestamps / S-035 scheduling. | Claimed by 2d; row to be added to `feature-catalogue.md` at sign-off (§11 + Appendix D). |

### A.2 Cross-references — clock-seam input only (no row discharge claimed by 2d, per C-P2-6)

| Row | Family | Owner | What 2d touches (clock seam only) |
|---|---|---|---|
| S-035 | Session scheduling | session-module Phase-4 spec | `effective_clock.sleep_until(...)` is the timing seam used by S-035; `mock_clock` (as `clock_override`) injects deterministic scheduling for tests. **Row discharge is the session-module spec's responsibility.** |
| S-003, S-004 | Heartbeat | session-module Phase-4 spec | `effective_clock.steady_now()` for elapsed measurement; `effective_clock.sleep_until(...)` for the heartbeat timer; per-session reusable `steady_timer` slot pool to satisfy `[const §VIII.5]` on the between-message path; cancellation through the session strand's slot. **Row discharge is the session-module spec's responsibility.** |
| LOG-001..004 | Async logger | **2k** | Producer-side timestamps for *session-scoped* records read `effective_clock.now()`; *engine-scope* records read `EngineConfig::clock->now()`; the `clock_scope` discriminator (drop-in language at §11) gates the choice. The `session_local<trace_context>` slot (renamed from v0.2's `strand_local<T>` per round 2 root cause #1) populates `trace_id`/`span_id` per record. **Row discharge is 2k's responsibility once 2k accepts the §11 drop-in.** |
| OBS-001..003 | OTel observability | **2k** | TracerProvider / MeterProvider span timestamps derived from the per-session `effective_clock` (or `EngineConfig::clock` for engine-scope spans); `mock_clock` yields deterministic conformance-corpus output for session-scoped records. **Row discharge is 2k's responsibility.** |

(No application-message rows like A-XXX, no W-XXX wire rows, no D-XXX dictionary rows are owned or touched by 2d. Per C-P2-6: 2d's claim is bounded to "clock seam available" — the row owners discharge their rows.)

---

## Appendix B — Normative References

Per `[const §VI.5]`, every `/specify` artifact lists the exact `[DocAbbrev §X.Y.Z] Title` references that inform it. Per P3-2 close: this appendix is rewritten in exact-title form (no line-number citations; line numbers are unstable across rewrites).

### B.1 Coverage-index normative references

| Source | Title (exact) | Where applied |
|---|---|---|
| `[arch §1.1]` | Goals | §1 Goals, §3.1 |
| `[arch §4.1]` | `core` | §4.1, §4.6, §3.2 |
| `[arch §4.4]` | `session` | §4.5, §3.3 (threading-default sentence in `[arch §4.4]`) |
| `[arch §5.1]` | Executor model | §4.5, §4.7, §6.1, §3.4 |
| `[arch §5.3]` | Error model | §6.2, §6.7 |
| `[arch §5.4]` | Trace context | §4.6, §3.5 |
| `[arch §5.6]` | Configuration shape | §4.4, §4.5, §3.6 |
| `[arch §5.7]` | Logging | §7.9 |
| `[arch §5.8]` | Backpressure | §6.4 |
| `[arch §6]` | Plugin Pattern | §4.1, §3.7 |
| `[arch §10]` | Hand-off to Design Docs 2a–2m | §1.1, §3.8 |
| `[arch §11]` | Open Architectural Questions | §11, Appendix A, §3.9 |
| `[const §VI.5]` | Spec Coverage Discipline (the 100% FIX Rule) — exact-title citation rule | this appendix's structure |
| `[const §VII]` | Testing Requirements — plugin mock-and-seam rule | §4.3, §9 |
| `[const §VIII.5]` | Performance Budgets & Benchmarks — allocator policy on the hot path | §1.8, §6.2, §8 |
| `[const §X.4]` | ABI Policy — C-ABI out-of-range code mapping | §1.9, §6.7 |
| `[const §XI.1]` | Concurrency & Coroutines — `asio::awaitable<T>` composition primitive | §4.1, §4.7, §3.10 |
| `[const §XI.2]` | Concurrency & Coroutines — Cancellation: ASIO native cancellation slots end-to-end | §4.7, §5.2, §6.5, §3.10 |
| `[const §XI.3]` | Concurrency & Coroutines — Awaitable mutex required in coroutine context | §4.3 (mock_clock pimpl), §4.5, §7.4, §3.10 |
| `[const §XI.4]` | Concurrency & Coroutines — Application threading default: per-session strand | §4.5, §6.1, §3.10 |
| `[const §XI.6]` | Concurrency & Coroutines — Coroutine frame allocation: HALO-first | §6.2, §8, §3.10 |
| `[const §XII.5]` | Security & TLS — `SecurityProfile` no-implicit-default rule | §4.5, §6.7 |
| `[const §XIII]` | Observability & Logging | §4.6, §7.9 |
| `[const §XIII.3]` | Observability & Logging — strand-stored trace context, no `thread_local` | §4.6, §3.11 |
| `[const §XIV.2]` | Pluggable Interfaces — ≤5 pure-virtual methods | §4.1, §3.7 |
| `[const §XV.15]` | Banned Patterns — Application-layer message drops on slow consumer | §4.5, §6.4, §3.12, §9 seam **"Drop-oldest banned-on-app-path enforcement test"** |
| `[const §XVII.1]` | Codex Review Gates — Codex Gate A before `/tasks` | this doc requires Gate A before `/tasks` |
| `[2a §4.2]` | `decimal_t::from_chars` (signed v0.3) | §6.3 (cross-strand reify cost ladder) |
| `[2a §6.5]` | Latency Tier 1 ceiling idiom (signed v0.3) | §6.3 |
| `[2b §4.3]` | `MessageView::get<Tag>()` typed accessor (signed v0.2) | §6.2 (parse → reify → dispatch chain) |
| `[2b §6.6]` | Three-arena PMR lifetime pinning (signed v0.2) | §4.5, §7.1, §8 |
| `[2b §6.7]` | C-ABI coalescing groups precedent — `FIXPP_ERR_WIRE_*` (signed v0.2) | §6.7 (`FIXPP_ERR_THREAD_*` shape) |
| `[2b §8]` | PMR recap (signed v0.2) | §4.5, §8 |
| `[2c §4.8]` | `dict::reify` → typed-message handoff (signed v1.3) | §6.3, §7.1, §7.2 |
| `[2c §4.9]` | `dict::version_registry` (signed v1.3) | §4.4 (root cause #2 / C-P1-1) |
| `[2c §6.2]` | `dict::reify_as` allocation budget (signed v1.3) | §6.3 |
| `[2c §6.7]` | C-ABI coalescing groups precedent — `FIXPP_ERR_DICT_*` (signed v1.3); also owns `dict_no_dictionary_for_application_version` reused by 2d v0.3 in lieu of the retired `version_registry_dictionary_missing` per round-2 N2-P2-1 | §6.7 (`FIXPP_ERR_THREAD_*` shape; v0.3 reuse for the missing-dictionary path), §9 seam **"`version_registry` dictionary-missing routes through `[2c §6.7]`"** |
| `[2c §7.2]` | Mid-session dialect-overlay swap rejection (signed v1.3) | §1 non-goals, §7.2 |
| `[2c §10] Q10` | `dict::version_registry` ownership model — DEFERRED to 2d | §4.4 (closed in v0.2 per root cause #2) |
| `[SYN §3.2 Q6a]` | Cancellation propagation model (DECIDED — ASIO native cancellation slots end-to-end) | §1.5, §4.7, §5.2, §6.5, §3.14 |
| `[SYN §3.2 Q6b]` | Awaitable mutex (DECIDED — own implementation in `fixpp::sync`) | §7.4, §3.15 |
| `[SYN §3.2 Q6c]` | Application threading contract (DECIDED — option 3 with default per-session strand) | §1.1, §4.5, §6.1, §3.13 |
| `[SYN §3.2 Q10]` | TestRequestThreshold / SendingTimeThreshold defaults and overrides (DEFERRED to session-module spec) | §4.5 (no concrete defaults) |
| `[SYN §5 row 4]` | What Phase 2 Should Tackle First — application threading unblocks C ABI shape | §11 Hand-off |

### B.2 Engineering-judgment citations (non-normative, inline at point of use)

Engineering-judgment decisions whose primary driver is engineering judgment rather than a specific spec section — the precise field list of `EngineConfig` / `SessionConfig`, the `mock_clock` API shape, the strand cost / waiter-count bounds in §1.2, the `cancellable_dispatch` primitive in §6.5, the `session_local<T>` shape + `session_executor::session_ptr()` member-function access mechanism in §4.6 (renamed from v0.2's `strand_local<T>` per round-2 root cause #1; access mechanism re-shaped to a wrapper-class member function in v0.4 per round-3 root cause #1), the `session_executor` wrapper-class unification in §4.8 (round 3 root cause #1: a project-owned class, not an alias to `asio::any_io_executor`), the per-session reusable-timer-slot pattern in §4.2 — cite `[const §X.y]` / `[arch §X.y]` / `[SYN §3.x Q#]` inline at point of use; they are not spec normatives and are intentionally omitted from §B.1.

---

## Appendix C — Convergence log

### Round 1: v0.1 → v0.2 (2026-05-08)

**Reviews input:**
- Codex Gate A (6 P1 / 8 P2 / 3 P3): `research/reviews/codex_2d_threading_review.md`
- Opus adversarial (post-judging 9 P1 / 13 P2 / 6 P3; 5 root causes): `research/reviews/opus_2d_threading_adversarial_review.md`

**Closing recommendation followed:** "v0.2 can ship after a single convergence pass."

**Root causes addressed:**

- **#1 — Cancellation/serialisation contract operationalisation.** §4.7 splits `Session::close(...)` into a graceful phase (under a child `asio::cancellation_state`, Logout `async_write` + `Clock::sleep_until` timeout bound to the child slot) and a teardown phase (root `cancellation_type::total`); `partial` dropped from the v1.0 surface, `terminal` skips phase 1. §4.7 ships a per-mode effect table over `{transport read, transport write, heartbeat, async_mutex, fromApp dispatch, store write, Logout exchange}`. §6.1 makes `direct_executor` mean "user attests `already_serialized_executor = true`" — engine FSM/transport/store state continues to be single-thread-accessed; `direct_executor + already_serialized_executor=false` rejects with new `error::executor_not_serialised`. §6.5 declares `cancellable_dispatch(strand, slot, handler)` as the project-owned primitive that gives the §4.7 reaping contract its implementation. §5.2 redraws the C-ABI cancellation diagram in two phases.

- **#2 — `EngineConfig`/`SessionConfig` field-list normalisation across dictionary, executor, clock axes.** §4.4 adds `EngineConfig::dictionaries` (and the engine-built `dict::version_registry` per `[2c §4.9]` / `[2c §10] Q10`), keeps `EngineConfig::clock` as engine-anchor, hardens `clock_not_set` as a `Engine::open` invariant regardless of session overrides, and publishes the engine-level `engine_trace_context` atomic-snapshot fallback path (closing N-P2-2). §4.5 flips `SessionConfig::executor` to `executor_override` (nullable `std::optional`), adds `already_serialized_executor` attestation, drops the `std::function` `trace_context_provider` for a value-typed `initial_trace_context` (closing C-P2-4), drops concrete heartbeat/test-request/sending-time threshold defaults to `std::optional<...>` placeholders (closing C-P2-8), and adds the `security_profile` no-implicit-default discipline (closing N-P2-3). §6.7 renames the C-ABI coalescing groups to `FIXPP_ERR_THREAD_CONFIG` / `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` / `FIXPP_ERR_THREAD_RUNTIME` matching `[2b §6.7]` / `[2c §6.7]` per-doc-prefix discipline (closing N-P2-5). §7.9 publishes the single-effective-clock rule (`effective_clock = clock_override ?: engine.clock`), closes v0.1 §10 Q5, and ships drop-in language for 2k's `clock_scope` discriminator. §10 Q5 closed.

- **#3 — `current_trace_context` storage.** §4.6 publishes `fixpp::core::strand_local<T>` (in `<fixpp/core/strand_local.hpp>`); the trace-context slot is owned by the `Session` object as a `strand_local<trace_context>` member; the awaiter reads through a borrowed `Session*` capture (stable across coroutine resume). The `void*`-via-`asio::any_io_executor::query` design is rejected (not a published contract on type-erased executors; does not survive `make_strand` / `bind_executor` decoration). §10 Q6 closed.

- **#4 — `mock_clock` `[const §XI.3]` violation.** §4.3 pimpl's `mock_clock` mutable state behind `std::unique_ptr<detail::mock_clock_state>`; the header declares only the awaitable signatures + public test API + the pimpl pointer. The synchronisation primitive (mutex or otherwise) lives in `src/core/test/mock_clock.cpp`; the `[const §XI.3]` grep gate sees no `std::mutex` declaration in any header that includes `asio::awaitable<...>`. Satisfied by construction.

- **#5 — `system_clock_source` allocation discipline + Clock implementer's recipe + `~Engine` ordering.** §4.1.1 publishes the **Clock implementer's recipe** as a one-paragraph contract: `co_await asio::this_coro::executor` for binding, `co_await asio::this_coro::cancellation_state` for slot wiring, allocation either via per-session pre-allocated reusable timer slot (pattern (a)) or `bind_allocator` PMR resource (pattern (b)). §4.2 picks pattern (a) for the default `system_clock_source` — one `steady_timer` per session, allocated lazily at first `sleep_until` from `session_arena`, reused per cycle by `expires_at(...)` + `async_wait(...)`. The hot-path zero-`new`/`delete` rule from `[const §VIII.5]` is explicitly extended to the heartbeat path. §10 Q3 closed: `~Engine` blocks on session drains and clears `EngineConfig::clock` `shared_ptr<Clock>` last; test fixtures that hold a `mock_clock` `shared_ptr` outside the engine see the mock outlive the engine without UB.

**Per-finding resolution:**

| Finding | Severity | Resolution | Section(s) edited |
|---|---|---|---|
| C-P1-1 (`EngineConfig` missing `dictionaries`/`version_registry`) | P1 | Subsumed under root cause #2 (field-list normalisation). `EngineConfig::dictionaries` added; engine builds `dict::version_registry` at `Engine::open` per `[2c §4.9]`; `[2c §10] Q10` closes here. New `error::version_registry_dictionary_missing` variant added in §6.7 (mapped to `[2c §6.7]`'s `dict_no_dictionary_for_application_version` at the dict layer). | §4.4, §4.5, §6.7 |
| C-P1-2 (`direct_executor` delegates engine state synchronisation to the user) | P1 | Subsumed under root cause #1 (cancellation/serialisation contract). `already_serialized_executor` attestation added; `error::executor_not_serialised` rejection at construction; engine FSM/transport/store state continues to assume single-thread access by user-attested executor. | §1.2, §4.5, §4.8, §6.1, §6.7 |
| C-P1-3 (`close(total)` self-cancels its own Logout) | P1 | Subsumed under root cause #1. `Session::close(graceful)` opens a child `cancellation_state`; phase 1's Logout `async_write` and timeout `Clock::sleep_until` bind to the child slot, NOT root; phase 2's root `cancellation_type::total` fires only after phase 1 resolves. `partial` dropped from public surface; `terminal` skips phase 1. | §1.5, §4.7, §5.2, §6.5 |
| C-P1-4 (clock provenance split between `EngineConfig::clock` and `SessionConfig::clock_override`) | P1 | Subsumed under root cause #2. §7.9 publishes `effective_clock = clock_override ?: engine.clock`; session-scoped LOG/OBS records read from `effective_clock`; engine-scope records read from `EngineConfig::clock`; `clock_scope = {engine, session}` discriminator drop-in at §11 for 2k. | §4.4, §4.5, §6.6, §7.9, §10 Q5 |
| C-P1-5 (`any_io_executor::query(void*)` is not a published storage contract) | P1 | Subsumed under root cause #3. `fixpp::core::strand_local<T>` published in `<fixpp/core/strand_local.hpp>`; `Session` holds `strand_local<trace_context> trace_slot_`; awaiter reads through borrowed `Session*` capture. `query(void*)` design rejected. | §4.6, §10 Q6 |
| C-P1-6 (`mock_clock` public header with `std::mutex` violates `[const §XI.3]`) | P1 | Subsumed under root cause #4. `mock_clock` mutable state pimpl'd into `std::unique_ptr<detail::mock_clock_state>`; sync primitive lives in `.cpp`. | §4.3 |
| C-P2-1 (`SessionConfig::executor` required clobbers engine inheritance) | P2 | Subsumed under root cause #2. `SessionConfig::executor` flipped to `std::optional<asio::any_io_executor> executor_override`; resolution rule `executor_override.value_or(EngineConfig::executor)`. `make_session_executor` returns `expected_t<asio::any_io_executor>` (was naked `noexcept`). | §4.5, §4.8 |
| C-P2-2 (posted `fromApp` reaping is asserted, not specified) | P2 | Subsumed under root cause #1. `cancellable_dispatch(strand, slot, handler)` declared in `<fixpp/core/cancellable_dispatch.hpp>`; reaping-before-invocation contract spelled out; PMR-allocated dispatch node from session arena. | §6.5, §9 seam **"Cancellation-slot propagation test (parse → fromApp)"** |
| C-P2-3 (cross-strand `dict::reify` cost missing from §6.3 ceilings) | P2 | §6.3 grew two new rows: cross-strand `reify` + dispatch (20-tag ≤ 1.25 µs; 200-tag ≤ 10.25 µs) per `[2c §1.2]` / `[2c §6.2]`. §9 seam **"Latency regression bench"** extended to bench both. | §6.3, §9 seam **"Latency regression bench"** |
| C-P2-4 (`std::function` trace provider allocation-prone, may throw) | P2 | `SessionConfig::trace_context_provider` (`std::function<...>`) replaced with value-typed `initial_trace_context` (`fixpp::otel::trace_context`); the `trace_context_provider_threw` `[v0.1 §6.7]` variant dropped. | §4.5, §6.7 |
| C-P2-5 (`system_clock::now()` is not monotonic within a strand) | P2 | §6.6 monotonicity claim corrected: `steady_now()` is monotonic per call within a strand; `now()` is NOT (UTC steps under NTP / admin / leap-second smoothing). Heartbeat-elapsed and threshold deltas use `steady_now()` only; `now()` only for wire-formatted UTC. Benign NTP step does not trip SendingTime threshold reject. | §6.6 |
| C-P2-6 (S-035 / LOG / OBS ownership references dangling or premature) | P2 | Status block, §1.2, §11, Appendix A.2 all rephrased: 2d's claim is bounded to "clock seam available." Row discharge for S-035, S-003, S-004, LOG-001..004, OBS-001..003 is the responsibility of the session-module Phase-4 spec / 2k. | status block, §1.2, §11 hand-off, Appendix A.2 |
| C-P2-7 (`sleep_until` lacks executor/cancellation/PMR construction recipe) | P2 | Subsumed under root cause #5. §4.1.1 published as a sub-section: Clock implementer's recipe (4-step contract). §4.2 picks pattern (a) for default impl. §9 seam **"Third-party `Clock` conformance test"** added. | §4.1.1, §4.2, §9 seam **"Third-party `Clock` conformance test"** |
| C-P2-8 (threshold defaults leak from deferred session spec) | P2 | `heartbeat_interval`, `test_request_threshold`, `sending_time_threshold` flipped from concrete `std::chrono::seconds{30}` / `{36000}` / `{120000}` defaults to `std::optional<...>` placeholders. Defaults picked at session-module Phase-4 spec sign-off per `[SYN §3.2 Q10]`. The `cancellation_propagation_timeout` knob (v0.1 §6.7) likewise dropped per N-P2-1. | §4.5, §6.7 |
| C-P3-1 (Appendix D references but doc only has A/B/C) | P3 | "§11 + Appendix D" references rewritten to "§11 (drop-in language)" with explicit note that 2d does not ship its own Appendix D — orchestrator applies catalogue/coverage-index amendments at sign-off per `[2c App D]` precedent. | §3.9, §11 |
| C-P3-2 (Appendix B not in exact-title form) | P3 | Appendix B rewritten in `[DocAbbrev §X.Y.Z] Title` form per `[const §VI.5]`; line-number citations removed. §B.2 separated for engineering-judgment non-normative citations. | Appendix B |
| C-P3-3 (test-seam ordinal cross-references stale) | P3 | §6.2 / §6.3 ordinal references rewritten as seam **names** ("Allocation guard on dispatch hot path", "Latency regression bench"); §9 introductory paragraph notes name-based cross-referencing as the stable pattern. | §6.2, §6.3, §9 |
| N-P1-1 (`system_clock_source::sleep_until` per-call timer allocation contradicts hot path) | P1 | Subsumed under root cause #5. §4.2 picks pattern (a) — per-session reusable `steady_timer` slot from `session_arena`; no per-cycle allocation. §8 PMR table updated. New §9 seam **"Allocation guard on `Clock::sleep_until` path"** added. | §4.2, §8, §9 seam **"Allocation guard on `Clock::sleep_until` path"** |
| N-P1-2 (`~Engine` ordering vs. in-flight `system_clock_source` waiters undefined) | P1 | Subsumed under root cause #5. §4.2 destructor ordering spelled out; §6.6 `~Engine` ordering paragraph added; §10 Q3 closed. §9 seam **"Engine-shutdown ordering test"** updated to exercise both `system_clock_source` and `mock_clock` ownership shapes. | §4.2, §4.4, §6.6, §10 Q3, §9 seam **"Engine-shutdown ordering test"** |
| N-P1-3 (`partial` / `terminal` close semantics under-specified) | P1 | Subsumed under root cause #1. `partial` dropped from v1.0 surface; `terminal` keeps documented semantics; per-mode effect table in §4.7 enumerates per-component behaviour. The `close_mode` enum replaces the `asio::cancellation_type` parameter on the public surface. | §4.7, §5.2, §6.5 |
| N-P2-1 (`cancellation_propagation_timeout` is a phantom `SessionConfig` knob) | P2 | The `cancellation_propagation_timeout` v0.1 §6.7 variant dropped; the close-timeout value lives in the session-module Phase-4 spec, sourced via `effective_clock.sleep_until(...)` bound to phase 1's child cancellation state per §4.7 / §6.5. | §4.7, §6.5, §6.7 |
| N-P2-2 (engine-level `engine_trace_context` fallback path mechanism unspecified) | P2 | §4.4 specifies the engine holds `EngineConfig::engine_trace_context` in a `std::atomic<trace_context>` (or `seqlock`) snapshot; `current_trace_context` outside any session strand reads the snapshot directly (no strand needed because the snapshot is atomic). §7.8 spells out the strand-crossing convention. | §4.4, §7.8 |
| N-P2-3 (`security_profile` no-implicit-default discipline unenforced) | P2 | §4.5 `SecurityProfile` field annotated: type default-constructs to a sentinel that the engine rejects at `Session::open` with `error::invalid_session_config` per `[const §XII.5]`. Sentinel value owned by 2g; 2d records only the rejection invariant. | §4.5, §6.7 |
| N-P2-4 (seam #7 wording conflates "no allocation" with "no global-heap allocation") | P2 | §9 seam **"Allocation guard on dispatch hot path"** description rephrased: the guard catches *global-heap* `new`/`delete`/`malloc` between parse and `fromApp`; PMR-arena allocations are expected and not flagged. §6.2 + §8 cross-references updated to match. | §6.2, §8, §9 seam **"Allocation guard on dispatch hot path"** |
| N-P2-5 (C-ABI coalescing groups collide on generic names with sibling docs) | P2 | §6.7 C-ABI mapping renamed: `FIXPP_ERR_THREAD_CONFIG` / `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` / `FIXPP_ERR_THREAD_RUNTIME` (matching `[2b §6.7]`'s `FIXPP_ERR_WIRE_*` and `[2c §6.7]`'s `FIXPP_ERR_DICT_*` per-doc-prefix discipline). The cancellation variant continues to reuse `FIXPP_ERR_CANCELLED` per `[const §XI.2]`. Final coalescing remains 2i's call. | §6.7 |
| N-P3-1 ("three-arena" framing in §8 is approximate vs. `[2b §6.6]`'s four arenas) | P3 | §8 PMR table footnote added: `[2b §6.6]` defines four arenas (per-message, framer-carry, session-lifetime, parser-completion folded into per-message); the "three-arena" framing is at the level 2d cares about. | §8 |
| N-P3-2 (`shared_ptr<Clock>` rationale in §4.4 thin) | P3 | §4.4 note tightened: rationale is "user-controlled lifetime over multiple `Engine::open` cycles in tests" rather than "drive `advance` from outside." | §4.4 |
| N-P3-3 (§10 Q1 phrased as "DEFERRED" when contract is locked) | P3 | §10 Q1 rephrased: "SIGNATURE DEFERRED to **2f**; contract locked here per §7.4." | §10 Q1 |

**Disagreements:** None — every Codex finding was confirmed at its rated severity, often clustered under a root cause. No Opus "Disagree" verdicts in round 1.

**Net effect summary:** v0.2 lands the v1.0 spine intact (4-pure-virtual `Clock`, default per-session strand, `EngineConfig`/`SessionConfig` value-typed split, ASIO native cancellation slots, strand-stored `current_trace_context`) and operationalises every contract that v0.1 left in prose. Five root causes resolved across §4.1.1 (new sub-section: Clock implementer's recipe), §4.2 (per-session reusable timer slot pattern), §4.3 (mock_clock pimpl), §4.4 (`EngineConfig::dictionaries` + atomic engine_trace_context snapshot), §4.5 (`executor_override`, `already_serialized_executor`, value-typed `initial_trace_context`, `optional` thresholds), §4.6 (`strand_local<T>` published; `query(void*)` rejected), §4.7 (two-phase close + `close_mode` enum + per-mode effect table), §4.8 (`expected_t<asio::any_io_executor>` resolution), §5.2 (two-phase C-ABI cancellation diagram), §6.1 (`direct_executor` attestation contract), §6.5 (`cancellable_dispatch` primitive), §6.6 (`now()` non-monotonic correction + `~Engine` ordering), §6.7 (8 error variants, renamed coalescing groups), §7.8 (engine-fallback awaiter path), §7.9 (single-effective-clock rule + `clock_scope` discriminator), §8 (PMR table updated), §10 (Q3 + Q5 + Q6 closed; Q7 added). 4 new test seams added (count: v0.1 had 14; v0.2 has 18) — **"Third-party `Clock` conformance test"**, **"`direct_executor` re-entrancy guard test"**, **"`strand_local<T>` lifetime-under-cancellation test"**, **"Allocation guard on `Clock::sleep_until` path"**. 2 new error variants in §6.7 — `executor_not_serialised`, `version_registry_dictionary_missing`. 3 v0.1 error variants dropped — `trace_context_provider_threw`, `cancellation_propagation_timeout`, and the v0.1 generic `FIXPP_ERR_INVALID_CONFIG`/`FIXPP_ERR_RUNTIME` group names (renamed). 3 open questions resolved (Q3, Q5, Q6); 1 new open question added (Q7 — engine-level fallback mutator). The doc grew with the §4.1.1 implementer's recipe, the `strand_local<T>` declaration in §4.6, the §4.7 per-mode effect table, the `cancellable_dispatch` primitive in §6.5, the new error variants and renamed coalescing groups in §6.7, the `effective_clock` rule + 2k drop-in in §7.9 / §11, and 4 new seams in §9. No structural section was rewritten; the v1.0 surface shapes (4-virtual `Clock`, value-typed `EngineConfig`/`SessionConfig`, per-session strand default, ASIO cancellation slots, awaitable trace_context) carry forward intact.

**Sibling-doc tensions surfaced by the convergence pass (input to round 2 if it happens):**

1. **`[arch §5.4]` Trace context wording** says: "**Storage:** `SessionConfig.trace_context_provider`, called once at session open, returns a `fixpp::otel::trace_context` stored on the session strand." Per C-P2-4 / Opus confirm, 2d v0.2 replaces the callable with a value-typed `initial_trace_context` field. The arch §5.4 wording is now stale; the orchestrator surfaces this to the user as a one-line architecture amendment (see Appendix D below). This is the only sibling-doc text that the v0.2 convergence directly touched.
2. **`[arch §4.4]` Configuration shape claim** at line 240: "`fixpp::session::SessionConfig` — frozen config struct: `SecurityProfile`, dictionary, `MessageStore` factory, executor opt-out, lock policy, recovery thresholds, dialect overlay, tap consumer, log/otel hooks." The 2d v0.2 surface adds `executor_override`, `already_serialized_executor`, `clock_override`, `initial_trace_context` to that enumeration; all are additive, none remove a v0.2-arch field. Editorial only — orchestrator may fold into the Appendix D amendment if convenient.

### Round 2: v0.2 → v0.3 (2026-05-08)

**Reviews input:**
- Codex Gate A round 2 (1 P1 / 3 P2 / 1 P3): `research/reviews/codex_2d_2_threading_review.md`
- Opus adversarial round 2 (post-judging 2 P1 / 5 P2 / 1 P3; 1 root cause): `research/reviews/opus_2d_2_threading_adversarial_review.md`

**Closing recommendation followed:** "v0.3 can ship after a single convergence pass."

**Round 1 verification:** all 5 round-1 root causes verified ✓ in v0.2 (Opus baseline-check pass), with 2 partials (#1 reaping primitive — `cancellable_dispatch` was strand-only; #3 awaiter access mechanism — hand-waved at §4.6) folded into round-2 root cause #1.

**Round 2 root causes addressed:**

- **#1 — Implicit serialisation-domain abstraction.** Every round-1 primitive (`cancellable_dispatch`, `strand_local<T>`, the per-session reusable timer slot pool, the `current_trace_context` awaiter access mechanism) was typed or worded against `session_strand_t` / "strand handle" / "owning strand," but §6.1's `direct_executor` mode formally has no strand — only an attested-already-serialised `any_io_executor`. The round-1 fix promoted "user attests serialisation" to a contract, but the dependent primitives never received the matching abstraction. **Fix.** §4.8 publishes `fixpp::core::session_executor_t = asio::any_io_executor` plus the project-owned executor adapter that publishes the typed `session_ptr_property` (the awaiter access mechanism); `make_session_executor` returns the adapter optionally wrapped in `asio::make_strand` under `per_session_strand` or bare under `direct_executor`; `is_strand_wrapped(...)` discriminates at runtime. §4.6 renames `strand_local<T>` → `session_local<T>` and publishes the access mechanism (project-typed property query on the project adapter). §4.2 + §8 re-key the per-session reusable `steady_timer` slot pool by `Session*`. §6.5 re-types `cancellable_dispatch` to take `session_executor_t` and return `awaitable<expected_t<void>>` so the `dispatch_aborted` reaping outcome is observable. §4.7 effect-table footnote notes the reaping contract applies under both modes. §6.7 drops the v0.2 `version_registry_dictionary_missing` collision (independent line-edit, see N2-P2-1 below) and adds `dispatch_aborted` as a non-error cancellation variant routed to `FIXPP_ERR_CANCELLED`. Sections edited: §1 goal #6, §4.2, §4.6, §4.7, §4.8, §6.1, §6.3, §6.5, §6.7, §7.8, §7.9, §8, §10 Q6, §11 NFR-015 drop-in.

**Per-finding resolution:**

| Finding | Severity | Resolution | Section(s) edited |
|---|---|---|---|
| C-R2-P1-1 (`cancellable_dispatch` strand-typed; `direct_executor` has no strand) | P1 | Subsumed under round-2 root cause #1. `cancellable_dispatch`'s first parameter re-typed from `session_strand_t` to `session_executor_t = asio::any_io_executor`; `make_session_executor` already returns the wider type so no extraction step is needed at the §4.8 → §6.5 boundary. The §4.7 effect-table reaping contract applies under both threading modes by primitive-type construction. | §4.8, §6.5, §4.7 |
| C-R2-P2-1 (`cancellable_dispatch`'s `dispatch_aborted` outcome unobservable through `void` return) | P2 | Subsumed under round-2 root cause #1. Signature changed to `cancellable_dispatch(session_executor_t, slot, handler) -> asio::awaitable<expected_t<void>>`; the slot-signal-before-pickup path completes the awaitable with `expected_t<void>{ unexpect, error::dispatch_aborted }`. New `dispatch_aborted` non-error cancellation variant added to §6.7, routed to `FIXPP_ERR_CANCELLED` (joining `clock_sleeps_cancelled`). | §6.5, §6.7 |
| C-R2-P2-2 (`strand_local<T>::load()` precondition is "on the owning strand"; `direct_executor` has no strand) | P2 | Subsumed under round-2 root cause #1. `strand_local<T>` renamed to `session_local<T>`; precondition becomes "inside the owning session's serialisation domain" (per `per_session_strand`'s strand or per `direct_executor`'s attested executor); access mechanism published as a project-typed `session_ptr_property` query on the project-owned `session_executor_t` adapter (§4.8). Closes the round-1 partial on root cause #3's access axis at the same edit. | §4.6, §6.3 (latency row) |
| C-R2-P2-3 (`system_clock_source` slot pool keyed by "strand handle"; `direct_executor` has no strand) | P2 | Subsumed under round-2 root cause #1. §4.2 detail comment + Notes + §8 PMR recap + §8 lifetime classes all re-key the per-session reusable `steady_timer` slot pool by **`Session*`**, not by strand handle. The keying axis is uniform across both threading modes. §9 seam **"Allocation guard on `Clock::sleep_until` path"** updated to run under both modes. | §4.2, §8, §9 seam **"Allocation guard on `Clock::sleep_until` path"** |
| C-R2-P3-1 (Appendix D drop-in carries review-internal IDs and non-title citations) | P3 | Appendix D rewritten in exact-title form per `[const §VI.5]`: every citation uses `[DocAbbrev §X.Y.Z] Title`; review-internal IDs ("C-P2-4 close", "root cause #3 / C-P1-5") are dropped from the architecture-amendment text. Round-1 amendment refined to use the round-2 `session_local<T>` storage name. | Appendix D |
| N2-P1-1 (Opus new — `current_trace_context` awaiter `Session*`-capture mechanism hand-waved) | P1 | Subsumed under round-2 root cause #1. §4.6 publishes the access mechanism as a project-typed `session_ptr_property` `query` on the project-owned `session_executor_t` adapter (§4.8); the adapter is project-owned, so `make_strand` / `bind_executor` over the adapter preserves the property by construction. The §6.3 latency-ceiling row for "Session-domain `trace_context` access" is rewritten to reflect the typed-property mechanism. §7.8's control-plane fallback path is rewritten to describe the property-query miss as the engine-snapshot trigger. | §4.6, §6.3, §7.8 |
| N2-P2-1 (error-variant collision with `[2c §6.7] dict_no_dictionary_for_application_version`) | P2 | Independent line-edit. Retired the v0.2 `version_registry_dictionary_missing` variant from §6.7; reuses 2c v1.3's `[2c §6.7] dict_no_dictionary_for_application_version` (mapped to `FIXPP_ERR_DICT_CONFIG`) for both engine-init-time and dispatch-time failures. The engine builds the registry through the 2c API at `Engine::open`; failures surface through 2c's signed-off variant directly — same operator-config failure mode now routes to a single C-ABI group. §4.4's existing `[2c §6.7]` citation already covered the dispatch-time path; round-2 extends that to the engine-init-time path. | §6.7 (drop row + remove from coalescing group), §9 (new seam **"`version_registry` dictionary-missing routes through `[2c §6.7]`"**) |
| N2-P2-2 (Opus new — `make_session_executor` returns `any_io_executor` but `cancellable_dispatch` consumes `session_strand_t`; strand-extraction step unspecified) | P2 | Subsumed under round-2 root cause #1. `session_strand_t` demoted to a doc-comment alias for the `per_session_strand`-mode case; consumers (`cancellable_dispatch`, `session_local<T>`'s access path, the §4.7 effect-table reaping rows, the §8 PMR-recap "session-executor" row) all type against `session_executor_t` (= `asio::any_io_executor`). No strand-extraction step at the §4.8 → §6.5 boundary; the runtime `is_strand_wrapped(...)` discriminator covers the few sites that need to know which mode they are in. | §4.8, §6.5, §8 |

**Disagreements:** None — every Codex round-2 finding was confirmed at its rated severity and clustered (with one exception, P3.1, which is editorial) into the single round-2 root cause along with the two new Opus P1/P2 findings (N2-P1-1, N2-P2-2). N2-P2-1 is the independent sibling-doc collision. No "Disagree" verdicts on either side.

**New sibling-doc tensions surfaced by the round-2 rewrite:**

1. **`[arch §5.4]` Trace context — Storage bullet, refined.** The round-1 Appendix D amendment cited `strand_local<T>`; the round-2 rewrite renames the storage template to `session_local<T>` and publishes the access mechanism (project-typed property query). Appendix D §D.1 below replaces the round-1 form with the round-2-final form (orchestrators applying the amendment should use the round-2 text). No new sibling-doc text outside `[arch §5.4]`'s Storage bullet was touched.

**Net effect summary:** v0.3 closes the round-2 root cause at the type-alias level with ~1 type alias (`session_executor_t`), 1 rename (`strand_local<T>` → `session_local<T>`), 1 published access mechanism (project-typed `session_ptr_property` on a project-owned executor adapter), 1 signature change on `cancellable_dispatch` (`void` → `awaitable<expected_t<void>>`), 1 keying-axis change on the `system_clock_source` slot pool (`strand handle` → `Session*`), and 1 §6.7 drop + 1 §6.7 add (`version_registry_dictionary_missing` retired in favour of `[2c §6.7] dict_no_dictionary_for_application_version`; `dispatch_aborted` added as a non-error cancellation variant). The v1.0 spine — 4-pure-virtual `Clock`, value-typed `EngineConfig`/`SessionConfig`, per-session strand default, ASIO native cancellation slots, two-phase close, session-domain-stored `current_trace_context`, the 18-seam list — carries forward intact; no structural section was rewritten. 2 new test seams added (count: v0.2 had 18; v0.3 has 20) — **"`session_executor_t` round-trip across both threading modes"**, **"`version_registry` dictionary-missing routes through `[2c §6.7]`"**. 1 new error variant in §6.7 (`dispatch_aborted`); 1 v0.2 error variant dropped (`version_registry_dictionary_missing`) — net zero on the 2d-owned variant count, but the C-ABI group `FIXPP_ERR_THREAD_CONFIG` no longer collides with `FIXPP_ERR_DICT_CONFIG` on the same operator-config failure mode. §10 Q6 refined (storage axis closed in v0.2; access axis closed in v0.3 — both noted in the Q6 row). All sections edited are line-edit-class; the ~30-line scope estimate from the round-2 review held.

### Round 3: v0.3 → v0.4 (2026-05-08, post-cap line-edit pass per 2c precedent)

**Reviews input:**
- Codex Gate A round 3 (1 P1 / 0 P2 / 0 P3): `research/reviews/codex_2d_3_threading_review.md`
- Opus adversarial round 3 (post-judging 1 P1 / 0 P2 / 0 P3; 1 root cause): `research/reviews/opus_2d_3_threading_adversarial_review.md`

**Closing recommendation followed:** "v0.4 can ship after a single convergence pass."

**Round 2 verification:** all 8 round-2 edit-list items verified ✓ in v0.3 (Opus round-3 baseline-check pass) — `cancellable_dispatch` re-typed against `session_executor_t`, `dispatch_aborted` made observable through `awaitable<expected_t<void>>`, `strand_local<T>` renamed to `session_local<T>`, `system_clock_source` slot pool re-keyed by `Session*`, Appendix D rewritten in exact-title form, `version_registry_dictionary_missing` retired in favour of `[2c §6.7]`, `session_strand_t` demoted to a doc-comment alias — but the **access-mechanism subclause** introduced under round-2 root cause #1 (the typed `session_ptr_property` query on `session_executor_t`) regressed because the underlying type alias `session_executor_t = asio::any_io_executor` does not let user-defined property queries survive type erasure (`any_io_executor`'s supported property set is fixed and closed: `context_t`, `blocking_t`, `outstanding_work_t`, `relationship_t`, `allocator_t<void>`).

**Round 3 root cause addressed:**
- **#1 — Type-erasure shape mismatch on `session_executor_t` access mechanism (Codex C-R3-P1-1 / Opus round-3 root cause #1):** v0.3's `using session_executor_t = asio::any_io_executor` aliased the project-owned executor adapter into ASIO's closed-property-set type-erasure shape, so the typed `session_ptr_property` query the round-2 fix promised either failed to compile (the property is not in `any_io_executor`'s supportable-property list) or compiled but always missed (returning the property's default `static_query_v<>`), routing every in-session `current_trace_context` read through the engine fallback path — a regression-equivalent to round-1's rejected `query(void*)` design. **Fix:** §4.8 re-declares `session_executor` as a project-owned value-typed **wrapper class** (NOT an alias to `asio::any_io_executor`) holding both the inner `asio::any_io_executor` and the typed `Session*` slot. The wrapper publishes the typed `Session*` accessor as a public **member function** `session_ptr()`, NOT via ASIO's property-query pipeline. ASIO's `bind_executor` / `make_strand` machinery operates against the wrapper-as-executor concept (the wrapper publishes `execute(F)`, `query(P)` for the closed ASIO property set, and the executor relational members); the wrapper IS the executor type that consumers bind to, so it is NOT erased into `asio::any_io_executor` on engine-controlled paths and the typed accessor remains reachable. §4.6 awaiter rewritten to recover the wrapper via static type-recovery on `co_await asio::this_coro::executor` and call `session_executor::session_ptr()` directly. §7.8 control-plane fallback path rewritten to describe the wrapper-type-recovery miss as the engine-snapshot trigger. §8 PMR table + lifetime-classes re-worded to describe the wrapper-class shape. §6.3 latency row re-worded to reflect the member-function-call mechanism. §4.7 effect-table footnote updated. §6.5 `cancellable_dispatch` first-parameter type re-pointed from `session_executor_t` to `session_executor`. §10 Q6 row finalised. Sections edited: §1 goal #6, §4.2, §4.4 fallback-storage note, §4.6, §4.7, §4.8, §6.1, §6.3, §6.5, §7.8, §7.9, §8, §10 Q6, §B.2, Appendix D §D.1.

**Fix shape chosen:** **(a) Project-owned thin executor wrapper class** — chosen over (b) `asio::any_executor<Properties...>` because shape (a) keeps the user-supplied `[arch §5.1]` `any_io_executor` primitive at the API surface unchanged (no Appendix D §D.2 architecture amendment needed), avoids `any_executor<Properties...>` template-list invasion across every consumer's signature, and matches Opus's "default to (a) if all-else-equal" recommendation. The dual-type pattern (user-supplied `any_io_executor` at the API surface, engine-internal `session_executor` wrapper inside the engine, with `make_session_executor` as the wrap step) parallels 2c's `version_profile`/`resolved_message_version` axis pair.

**Per-finding resolution:**

| Finding | Severity | Resolution | Section(s) edited |
|---|---|---|---|
| C-R3-P1-1 (`session_executor_t = asio::any_io_executor` does not let `query(exec, fixpp::core::session_ptr_property)` compile/hit on the type-erased side; round-2 access-mechanism fix is regression-equivalent to the round-1 `query(void*)` defect) | P1 | Subsumed under round-3 root cause #1. `session_executor` re-declared as a project-owned value-typed wrapper class in §4.8 (replacing `using session_executor_t = asio::any_io_executor`); typed `Session*` accessor published as `session_ptr()` public member function on the wrapper, NOT via ASIO's property-query pipeline. Awaiter access mechanism in §4.6 rewritten as wrapper-type recovery + `session_ptr()` member-function call. Knock-on prose updates in §6.1 / §6.3 / §6.5 / §7.8 / §7.9 / §8 to point at the wrapper-class member-function path. Negative assertion test added in §9 seam **"`session_executor` typed-accessor survives ASIO erasure"** (NEW seam 21) verifying the wrapper survives `bind_executor` / `make_strand` decoration AND that explicit erasure to `asio::any_io_executor` correctly drops the typed accessor (documenting the v0.3 regression shape as a known-bad path). | §1 goal #6, §4.2, §4.4, §4.6, §4.7, §4.8, §6.1, §6.3, §6.5, §7.8, §7.9, §8, §10 Q6, §B.2, Appendix D §D.1, §9 seam **"`session_executor` typed-accessor survives ASIO erasure"** |

**Disagreements (if any):** None — Codex got the severity right; Opus confirmed at P1; both agreed on the type-erasure shape mismatch as the single root cause and on the line-edit-class scope. No new findings surfaced under Opus's round-3 hunt for second-order defects (cancellation racing on the `Session*`-keyed timer pool, coroutine-resume on a different thread for `session_ptr_property`, sibling-doc contract drift, seam 19/20 testability, citation precision per `[const §VI.5]`); every other angle either folds into C-R3-P1-1 or is correctly handled in v0.3.

**New sibling-doc tensions surfaced by the round-3 rewrite:**

1. **`[arch §5.4]` Trace context — Storage bullet, finalised.** The round-2 Appendix D amendment cited the typed `session_ptr_property` query on a project-owned `session_executor_t` adapter; the round-3 rewrite re-shapes the access mechanism as a `session_ptr()` member-function accessor on a project-owned `session_executor` wrapper class (round 3 root cause #1). Appendix D §D.1 below replaces the round-2 form with the round-3-final form (orchestrators applying the amendment should use the round-3 text). No new sibling-doc text outside `[arch §5.4]`'s Storage bullet was touched. **`[arch §5.1]` Executor model is intact** — the user-supplied `any_io_executor` primitive at the API surface is unchanged; the engine-internal `session_executor` wrapper is consumed only after `make_session_executor` wraps the user's `any_io_executor`, so no §D.2 amendment is required.

**Net effect summary:** v0.4 closes the round-3 root cause at the type-alias level with 1 type re-pick (`session_executor_t = asio::any_io_executor` → `class session_executor { ... }`), 1 access-mechanism re-shape (typed `session_ptr_property` query → `session_ptr()` public member function on the wrapper), and prose updates across §1 goal #6, §4.2, §4.4, §4.6, §4.7, §4.8, §6.1, §6.3, §6.5, §7.8, §7.9, §8, §10 Q6, §B.2, and Appendix D §D.1. 1 new test seam added (count: v0.3 had 20; v0.4 has 21) — **"`session_executor` typed-accessor survives ASIO erasure"** — explicitly testing that the wrapper survives `bind_executor` / `make_strand` decoration AND that explicit erasure to `asio::any_io_executor` correctly drops the typed accessor (documenting the v0.3 regression shape as a known-bad path so future contributors do not re-introduce it). 0 new error variants in §6.7; 0 dropped error variants — the §6.7 surface is unchanged. §10 Q6 row finalised (storage axis closed in v0.2; access axis closed in v0.3 then re-shaped to wrapper-class in v0.4). The v1.0 spine — 4-pure-virtual `Clock`, value-typed `EngineConfig`/`SessionConfig`, per-session strand default, ASIO native cancellation slots, two-phase close, session-domain-stored `current_trace_context`, the (now 21-)seam list — carries forward intact; no structural section was rewritten. **2c-style post-cap framing:** the round cap was hit at round 3 / max 3 with one P1 cluster and a line-edit-class fix (~15–25 lines per Opus's scope estimate; actual delta ~110 net lines including the new §4.8 wrapper class declaration, the new §9 seam #21, the round-3 Appendix C entry, and the prose ripples — well within the post-cap line-edit envelope and matching the 2c v1.2 → v1.3 precedent precisely). **Appendix D §D.2 was NOT added** — fix shape (a) is `[arch §5.1]`-compatible by construction (the user-supplied executor primitive at the API surface is still `any_io_executor`; the project's `session_executor` wrapper is engine-internal).

### Cross-doc amendments applied at `2f-async-mutex.md` v1.5 sign-off (2026-05-08)

Three sibling-doc edits to this file were declared by `[2f-async-mutex.md App D]` and applied at 2f sign-off (this entry records the application; the authoritative drop-in language lives in `[2f-async-mutex.md App D §D.1]`, `[App D §D.2]`, and `[App D §D.3]`):

1. **`[2d §4.5] fixpp::session::SessionConfig` — engine-internal `Session::session_arena()` accessor:** added a new Notes bullet at the end of §4.5 declaring `[[nodiscard]] std::pmr::memory_resource* Session::session_arena() const noexcept;` as an engine-internal accessor (not user-facing public API). Returns the resolved per-session PMR resource per the `[2d §4.4]` resolution chain (`SessionConfig::session_arena ?: EngineConfig::default_session_resource ?: std::pmr::get_default_resource()`); never null; frozen at session open per `[arch §5.6]`. Sole caller is the `fixpp::session/`-side helper `async_lock_via_session_executor` per `[2f §4.3.2]`; `core/` does not back-edge into `session/` per `[arch §2.3]`. Closes 2f RC#2 layering tension.
2. **`[2d §4.7] Cancellation propagation API — two-phase close` — `async_mutex::lock` per-mode effect-table row reworded:** the row's "`operation_aborted`" wording replaced with `expected_t::unexpected{error::sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]` / `[2f §6.5]`. One-paragraph contract added immediately after the per-mode effect table (before the `FileStore::flush_for_session_close()` hook paragraph) explaining that 2f's value-channel-typed `expected_t<async_lock_guard>` return shape forces the `expected_t::unexpected` cancellation outcome (per `[arch §5.3]`'s no-exceptions-on-the-hot-path rule) while ASIO completion-token-shaped operations elsewhere in this table continue to surface cancellation through `operation_aborted`. Closes 2f RC#4.
3. **`[2d §7.4] Awaitable mutex (2f) — Locked contract surface` — second bullet reworded:** the bullet's "`operation_aborted`" wording replaced with `expected_t::unexpected{error::sync_lock_aborted}` at the 2f boundary (mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`/`[2f §6.5]`), and the `[2d §4.7]` table's historical row name `async_mutex::lock` annotated as mapping to 2f's published `async_mutex::async_lock(...)` API per `[2f §4.1.1]`. Closes 2f RC#4 / RC-D — the §D.2 effect-table edit alone left §7.4's locked surface inconsistent with the §4.7 table; §D.3 aligns both.

These edits do NOT change the v0.4 sign-off baseline — they discharge sibling-doc text that 2f v1.5's design depends on. The 2d v0.4 spine (4-pure-virtual `Clock`, two-phase close, `session_executor` wrapper class, 21 seams, NFR-015 catalogue row) is unchanged.

### Cross-doc amendments applied at `2e-msgstore.md` v0.4 sign-off (2026-05-08)

Two sibling-doc edits to this file were declared by `[2e-msgstore.md App D]` and applied at 2e sign-off (this entry records the application; the authoritative drop-in language lives in `[2e-msgstore.md App D §D.1]` and `[App D §D.2]`):

1. **`[2d §4.5] fixpp::session::SessionConfig` — `store_factory` field type:** `std::shared_ptr<MessageStoreFactory>` → `std::unique_ptr<MessageStoreFactory>`. Drives unique-ownership semantics per `[arch §5.6]` mid-session-swap ban + `[2e §4.4]` factory contract (round 1 `N1` + round 2 Codex `C-R2-P1-4` close). One-line trailing comment added: `// unique ownership per [arch §5.6] / [2e §4.4]`. `cert_source` stays `shared_ptr` (owned by 2g; not affected).
2. **`[2d §4.7] Cancellation propagation API — two-phase close` — per-mode effect table:** appended one row for `FileStore::flush_for_session_close()` (engine-internal hook) covering graceful phase 1 / graceful phase 2 / `terminal` columns + a one-paragraph hook contract immediately after the table (drop-in addition before the `Notes:` block). Discharges `[2e §7.6]`'s phantom-citation tension (round-2 root cause #2 / Codex `C-R2-P1-5`). The hook is non-virtual on the concrete `FileStore` (NOT on `MessageStore`'s pure-virtual interface), idempotent, runs only under `close_mode::graceful`, and is short-circuited under `close_mode::terminal`.

These edits do NOT change the v0.4 sign-off baseline — they discharge sibling-doc text that 2e v0.4's design depends on. The 2d v0.4 spine (4-pure-virtual `Clock`, two-phase close, `session_executor` wrapper class, 21 seams, NFR-015 catalogue row) is unchanged.

## Appendix D — Drop-in amendments for sibling-doc text touched by this rewrite

Per convergence rule 6, sibling-doc text touched by this rewrite is surfaced as drop-in amendment language for the orchestrator to apply at sign-off. The rewrite agent does not edit `architecture.md` directly. Per `[const §VI.5]`, every reference uses the exact `[DocAbbrev §X.Y.Z] Title` form; review-internal IDs (e.g., "C-P2-4 close", "root cause #3") are not carried into architecture text (P3.1 round-2 close).

### D.1 `[arch §5.4] Trace context` — Storage bullet (round 1; refined in round 2; finalised in round 3)

**Before** (current `architecture.md` v0.2 text, `[arch §5.4] Trace context`):

> - **Storage:** `SessionConfig.trace_context_provider`, called once at session open, returns a `fixpp::otel::trace_context` stored on the session strand.

**After** (drop-in replacement):

> - **Storage:** `SessionConfig.initial_trace_context` (value-typed `fixpp::otel::trace_context`, replacing v0.1's `std::function`-based `trace_context_provider`) is read once at session open and stored in the session's `fixpp::core::session_local<trace_context>` slot. The slot lives inside the `Session` object — not in `asio::any_io_executor::query` over a type-erased executor, which is not a published storage contract and does not survive `make_strand` / `bind_executor` decoration; nor is it accessed through a typed user-defined property on `asio::any_io_executor` (whose supported property set is fixed and closed and does not forward arbitrary user-defined queries). The awaiter recovers the typed `Session*` by calling the public `session_ptr()` member-function accessor on the project-owned `fixpp::core::session_executor` value-typed wrapper class — uniform across both `threading_mode::per_session_strand` (the wrapper holds an `asio::strand` over the resolved executor) and `threading_mode::direct_executor` (the wrapper holds the user-attested already-serialised executor) modes. See `[2d §4.5] fixpp::session::SessionConfig — session-level frozen-at-open knobs`, `[2d §4.6] fixpp::current_trace_context — session-domain awaitable + session_local<T> storage`, and `[2d §4.8] fixpp::core::session_executor + executor resolution path`.

The remaining `[arch §5.4]` bullets (Access, `thread_local` prohibited, Logs+traces correlate) carry forward unchanged. The rewrite is a field-name + storage-mechanism + access-mechanism update; the architectural axis is preserved.

(The round-1 amendment cited `strand_local<T>`; the round-2 rewrite renamed the storage template to `session_local<T>` because the keying axis is the **session serialisation domain** — which subsumes both `per_session_strand`'s strand and `direct_executor`'s attested executor — not "the strand." Round 2 also published the access mechanism as a typed `session_ptr_property` query on a project-owned `session_executor_t = asio::any_io_executor` adapter; round 3 rejected that formulation per Codex C-R3-P1-1 / Opus root-3 root cause #1 because `asio::any_io_executor`'s property set is fixed and closed and does not forward arbitrary user-defined queries. The round-3 rewrite re-shapes the access mechanism as a `session_ptr()` member-function accessor on a project-owned `session_executor` wrapper class — the wrapper IS the executor type that consumers bind to, so the typed accessor survives `bind_executor` / `make_strand` decoration without depending on ASIO's closed property set. The drop-in above is the round-3-final form; orchestrators applying the amendment should use this version, not the round-1 or round-2 forms recorded in earlier Appendix D revisions.)
