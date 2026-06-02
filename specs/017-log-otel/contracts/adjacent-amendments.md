# Contract: adjacent-module amendments (minimal 2d surface touch)

**Anchor**: `.specify/2k-log-otel.md` §7 + Appendix D §D.1. These are the **only** edits outside the two new modules + `core/error.hpp`. Per clarified scope boundary 1 (2026-06-02), **no session-FSM transition is edited** — these are surface/type completions, not behavior wiring.

## Already shipped by 2d — confirm only (do NOT re-add)
- `EngineConfig` (`include/fixpp/core/engine_config.hpp`): `std::shared_ptr<fixpp::core::Logger> logger`, `std::shared_ptr<fixpp::otel::TracerProvider> tracer`, `std::shared_ptr<fixpp::otel::MeterProvider> meter`, `fixpp::otel::trace_context engine_trace_context` (+ `engine_trace_context_snapshot` atomic publish). **Forward-declared stubs** — 017 makes the referenced types complete.
- `SessionConfig`: `clock_override`, `initial_trace_context` (value-typed).
- `Session`: `session_local<trace_context> trace_slot_` (populated at `open()` from `initial_trace_context`).
- `fixpp::core::trace_context` (`core/trace_context.hpp`).

## 017 must add
1. **`fixpp::core::Logger` alias** = `fixpp::log::Logger` (a `core/logger_fwd.hpp` so `EngineConfig::logger`'s declared type resolves to the real `log` type). Keep OTel SDK + `std::mutex` out of any header on the awaitable include-edge (`[const §XV.9]`; pimpl `Logger::Impl`).
2. **`fixpp::otel::TracerProvider` / `MeterProvider`** — define the types `EngineConfig` forward-declares (in `otel/providers.hpp`).
3. **`fixpp::otel::trace_context`** — confirm/alias over `fixpp::core::trace_context`.
4. **`Session::get_trace_context() const noexcept`** — NEW public read-only accessor returning `trace_context const&` (or by value) from `trace_slot_`. Required by the `FIXPP_SLOG` contract. Does not change FSM behavior.
5. **`SessionConfig::logger_override`, `SessionConfig::tracer_override`** — NEW nullable `std::shared_ptr` (engine-anchor + session-override per `[2d §4.5]`). `meter_override` intentionally omitted (metrics engine-scoped, anchor §4.8).
6. **`Engine::engine_trace_context() const noexcept`** — confirm the accessor exists (atomic snapshot read used by `FIXPP_ELOG`); add the thin accessor over the existing snapshot field if absent.

## Obligations
- These edits must NOT introduce a session-FSM behavior change — verified by leaving FSM transition code untouched (only accessor + config fields + type definitions).
- `tools/check_layers.py` must still pass (`log → {core}`, `otel → {core, log}`).
- The `core/engine_config.hpp` include set must not grow to pull OTel SDK headers (keep forward-decls / pimpl).
- A regression test confirms `get_trace_context()` returns the value stored from `initial_trace_context` at open (feeds TS-6a).
