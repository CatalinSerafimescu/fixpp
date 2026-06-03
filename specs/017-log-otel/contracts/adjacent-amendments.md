# Contract: adjacent-module amendments (2d surface touch — four-item owned set)

**Anchor**: `.specify/2k-log-otel.md` §7 + Appendix D §D.1. These are the **only** edits outside the two new modules + `core/error.hpp`. The owned set is **four** items (3 adds + 1 removal), not two. Per clarified scope boundary 1 (2026-06-02), **no session-FSM transition is edited** — these are surface/type completions, not behavior wiring.

## 2d surface — consumed vs owned-amendment (authoritative)
| 2d surface | 017 disposition |
|---|---|
| `EngineConfig::{logger,tracer,meter}` fwd-declared `shared_ptr` stubs; `EngineConfig::engine_trace_context` seed VALUE (`engine_config.hpp:157`) + the `core::detail::trace_context_snapshot` helper TYPE (`engine_config.hpp:64`) | **consumed** — confirm only; 017 makes the referenced types complete (does NOT re-add the fields). The publishable snapshot is an `Engine`-held member 017 adds (owned amendment #2), seeded from this `EngineConfig` value; there is **no** `EngineConfig::engine_trace_context_snapshot` member. |
| `SessionConfig::clock_override`, `SessionConfig::initial_trace_context` | **consumed** — confirm only. |
| `Session::trace_slot_` (`session_local<trace_context>`, populated at `open()` from `initial_trace_context`) | **consumed** (storage; read-only — no second storage read introduced). |
| `Session::trace_context_value()` (existing public accessor over `trace_slot_`, `session.hpp:171`) | reconciled — see owned amendment #1 (canonical `get_trace_context()`). |
| `fixpp::core::trace_context` (`core/trace_context.hpp`) | **consumed** — confirm/alias. |
| `Session::get_trace_context()` | **owned amendment** (add — canonical name). |
| `Engine::engine_trace_context()` | **owned amendment** (add — absent today). |
| `SessionConfig::{logger,tracer}_override` | **owned amendment** (add). |
| `SessionConfig::log_sink_override` (`session_config.hpp:182`) | **owned amendment** (REMOVE — replaced by `logger_override`). |

## 017 owned amendments (the four-item set — each an owned public-surface change with a test)
1. **`Session::get_trace_context() const noexcept`** — the canonical anchor-mandated accessor (anchor §6.4 / App D §D.1) over the existing `trace_slot_`. The live header already has a public `trace_context_value()` over the **same** `trace_slot_` (`session.hpp:171`). Make `get_trace_context()` the **single canonical** accessor — a thin alias of (or rename of) `trace_context_value()` — introducing **no** second storage read. Check `codegraph_callers` of `trace_context_value()` before any removal. Required by the `FIXPP_SLOG` contract; does not change FSM behavior.
2. **`Engine::engine_trace_context() const noexcept`** — NEW public accessor on the `Engine` class, backed by a NEW `Engine`-held snapshot member. 017 adds **both** (a) an `Engine` member `fixpp::core::detail::trace_context_snapshot engine_trace_ctx_snapshot_` (the existing helper TYPE at `engine_config.hpp:64` — a seqlock/atomic wrapper with `.store()`/`.load()`), **seeded at `Engine` construction** from the real `EngineConfig::engine_trace_context` seed field (`engine_config.hpp:157`) via `trace_context_snapshot{engine_cfg_.engine_trace_context}`, and (b) the public accessor `[[nodiscard]] fixpp::otel::trace_context Engine::engine_trace_context() const noexcept { return engine_trace_ctx_snapshot_.load(); }`. Verified ABSENT from `include/fixpp/session/engine.hpp` today; `EngineConfig` carries only the seed VALUE (`engine_trace_context`, line 157) and the helper TYPE (`core::detail::trace_context_snapshot`, line 64) — there is **no** `EngineConfig::engine_trace_context_snapshot` member instance (the identifier appears only in a comment at `engine_config.hpp:11`). `FIXPP_ELOG`/SC-004/TS-6b hard-depend on this accessor. An owned public C++ surface change to a load-bearing engine class (move/copy-deleted, strict-precondition dtor).
3. **`SessionConfig::logger_override`, `SessionConfig::tracer_override`** — NEW nullable `std::shared_ptr` (engine-anchor + session-override per `[2d §4.5]`). `meter_override` intentionally omitted (metrics engine-scoped, anchor §4.8).
4. **REMOVE `SessionConfig::log_sink_override`** (`session_config.hpp:182`, `std::shared_ptr<fixpp::log::Sink>`). The new `logger_override` (a whole `Logger`) **replaces** the 2d `log_sink_override` stub (a single `Sink`) per anchor App D §D.1; leaving both yields two competing, undefined-precedence log-override surfaces. An owned public-surface removal.

Type-completion edits (not surface amendments, but required): **`fixpp::core::Logger` alias** = `fixpp::log::Logger` (`core/logger_fwd.hpp` so `EngineConfig::logger` resolves; keep OTel SDK + `std::mutex` off the awaitable include-edge, `[const §XV.9]`; pimpl `Logger::Impl`); **define `fixpp::otel::TracerProvider`/`MeterProvider`** (the fwd-declared types, in `otel/providers.hpp`); **confirm/alias `fixpp::otel::trace_context`** over `fixpp::core::trace_context`.

## Obligations
- These edits must NOT introduce a session-FSM behavior change — verified by leaving FSM transition code untouched (only accessor + config fields + type definitions).
- `tools/check_layers.py` must still pass (`log → {core}`, `otel → {core, log}`).
- The `core/engine_config.hpp` include set must not grow to pull OTel SDK headers (keep forward-decls / pimpl).
- A regression test confirms `get_trace_context()` returns the value stored from `initial_trace_context` at open (feeds TS-6a), and a TS-6b test exercises `Engine::engine_trace_context()`.
- A grep/compile regression asserts `SessionConfig::log_sink_override` is **gone** (the removal landed).
- A grep regression asserts exactly **one** canonical session trace-context accessor exists (`get_trace_context()`; no lingering duplicate of `trace_context_value()`).
