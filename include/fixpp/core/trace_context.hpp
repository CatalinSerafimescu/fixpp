// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/trace_context.hpp
//
// fixpp::otel::trace_context — minimal 32-byte trivially-copyable,
// standard-layout OTel correlation POD ([2d §1.2]/§4.6; research D-1; E11).
// 2k owns the full OTel surface and EXTENDS this namespace; it does NOT
// redefine this POD (one-direction dependency, [arch §2.3] layering).
//
// The 32-byte size + trivial-copyability + standard-layout are the CONTRACT,
// not advisory: the std::atomic<trace_context> snapshot's is_always_lock_free
// probe and the seqlock memcpy fallback (engine_config.hpp) both depend on it.
// Probe result (T001, 2026-05-19): is_always_lock_free == false on every
// Tier-1 STL (32 B > 16 B CAS) ⇒ the seqlock fallback path is selected, not a
// silent degrade (.specify/decisions/007-threading-clock-probes.md).
//
// NOTE: the `fixpp::current_trace_context` free awaitable (US4 / FR-015 / E8)
// is added to this header by T044; T007 ships the POD only.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/this_coro.hpp>

#include <fixpp/core/session_executor.hpp>   // typed session_ptr recovery (RC#1)

namespace fixpp::otel {

struct trace_context {
    std::array<std::byte, 16> trace_id{};
    std::array<std::byte, 8>  span_id{};
    std::uint8_t              flags{};
    std::array<std::byte, 7>  _pad{};   // explicit pad to a fixed 32 B
};

static_assert(sizeof(trace_context) == 32
                  && std::is_trivially_copyable_v<trace_context>
                  && std::is_standard_layout_v<trace_context>,
              "fixpp::otel::trace_context must be a 32-byte, "
              "trivially-copyable, standard-layout POD (D-1 / E11)");

}  // namespace fixpp::otel

namespace fixpp {

// fixpp::current_trace_context — free awaitable (E8 / FR-015 / I-11 / I-12;
// T044). Reads the coroutine's executor via `co_await asio::this_coro::
// executor`, then recovers Session* through the TYPED session_executor
// accessor (round-3 RC#1 — NOT asio::any_io_executor::query, NOT
// thread_local). Safe across a prior coroutine resume on a different thread:
// the value is read through the borrowed, stable Session* recovered fresh,
// never via thread-affine storage.
//
// ── USAGE ──────────────────────────────────────────────────────────────────
//
// Default path — inside any coroutine spawned on `sess.executor()` (or a
// callback dispatched by the session machinery: fromApp, toApp, on_message,
// etc.). The awaitable returns the session's current trace_context. No
// setup required:
//
//     asio::awaitable<void> on_app_message(/* ... */) {
//         auto ctx = co_await fixpp::current_trace_context();
//         log_with_trace(ctx, "processing");
//         // ... business logic ...
//     }
//
// Anti-pattern — after hopping to a NON-session executor (foreign
// `asio::thread_pool::executor`, `asio::system_executor`, an unrelated
// `asio::strand` over a different io_context, etc.), the awaitable returns
// a DEFAULT `trace_context{}`. Silently. No error, no warning:
//
//     asio::awaitable<void> bad_offload(asio::thread_pool& bg, /* ... */) {
//         co_await asio::post(bg.get_executor(), asio::use_awaitable);
//         auto ctx = co_await fixpp::current_trace_context();
//         //   ^ SILENT — no error, no warning. ctx is default-constructed
//         //     trace_context{} (D-17 engine-fallback), NOT the session's
//         //     context. Logs / spans correlated against `ctx` from here
//         //     will appear unparented.
//     }
//
// Pattern A — manual propagation (recommended when the hop is one-off or
// the foreign executor's lifetime is shorter than the session). Capture
// before the hop; carry across explicitly. Mirrors the OpenTelemetry
// "context-as-value" propagation discipline:
//
//     asio::awaitable<void> offload_pattern_a(asio::thread_pool& bg,
//                                             /* ... */) {
//         auto ctx = co_await fixpp::current_trace_context();   // capture FIRST
//         co_await asio::post(bg.get_executor(), asio::use_awaitable);
//         // Now on bg's thread. current_trace_context() would return default
//         // here — but we kept `ctx`, so use it explicitly.
//         do_background_work(ctx, /* ... */);
//     }
//
// Pattern B — automatic propagation (recommended when a custom executor is
// bound for the session's lifetime, e.g. a long-lived background pool).
// Construct a session_executor that wraps the foreign executor + carries
// the same Session* via `fixpp::core::make_session_executor`; posts to that
// wrapper resume on the foreign executor's threads but UNDER a
// session_executor, so `current_trace_context()` recovers the session
// context the same way it does on `sess.executor()`:
//
//     // Construct ONCE, hold for the session's lifetime:
//     auto bg_se = fixpp::core::make_session_executor(
//         bg.get_executor(),
//         fixpp::session::threading_mode::direct_executor,
//         /*already_serialized_executor=*/false,   // strand-wrap a multi-
//                                                  //   thread pool
//         &sess);                                  // bind to THIS session
//     // Inside any coroutine already on a session_executor for &sess:
//     asio::awaitable<void> offload_pattern_b(/* ... */) {
//         co_await asio::post(*bg_se, asio::use_awaitable);
//         // Now on bg's thread, UNDER a session_executor for &sess →
//         // current_trace_context() STILL recovers the session's context.
//         auto ctx = co_await fixpp::current_trace_context();
//         do_background_work(ctx, /* ... */);
//     }
//
// Pattern selection — A vs B:
//   • A is the OpenTelemetry-shaped default; explicit and locally obvious;
//     no extra executor lifetime to manage; works on any executor without
//     wrapping.
//   • B keeps the call-site invariant (`current_trace_context()` always
//     just works on session-bound executors), at the cost of constructing
//     and holding a wrapping session_executor per foreign executor for the
//     session's lifetime. Pick B when many call sites would otherwise have
//     to thread an explicit `ctx` argument.
//
// Background — `current_trace_context()` recovers via
// `session_executor::session_ptr()` (a stable `Session*`), NOT through
// thread_local storage. That is why the value is correct on any thread the
// coroutine resumes on, AS LONG AS the executing executor is a
// `session_executor` for the right `Session*`. Outside that wrapper —
// regardless of what asio's scheduler happens to do with threads — the
// recovery returns default. See `[2d §4.6]` / I-11 / D-17, and
// `book/concurrency_model.md` for the full topology + worked examples.
//
// Tests asserting cross-thread survival of the trace_context: don't rely
// on asio's worker-thread routing to deliver an observable thread hop
// (scheduling-luck — see gh#79). Construct a disjoint side
// `session_executor` via `make_session_executor(...)` and post to it; the
// hop is then deterministic by construction.
//
// Shape note (D-18): the data-model E8 / `contracts/trace_context.hpp` shape
// `inline constexpr struct current_trace_context_t { auto operator co_await()
// const noexcept; } current_trace_context;` is unrealizable as written INSIDE
// an asio::awaitable coroutine — asio's promise type defines
// `await_transform` and silently rejects any operand it does not recognise
// (asio::awaitable / this_coro::* / a frame-callable). The minimal idiomatic
// realisation is a coroutine function returning an asio::awaitable so the
// promise routes it through the `await_transform(awaitable<T,Executor>)`
// overload. Usage becomes `co_await fixpp::current_trace_context()`.
//
// Engine-fallback note: 007 ships no Engine type; a session-less executor
// yields a default trace_context (D-17 — concrete engine snapshot fallback
// is the downstream Engine's).
[[nodiscard]] inline asio::awaitable<fixpp::otel::trace_context>
current_trace_context() {
    asio::any_io_executor ex = co_await asio::this_coro::executor;
    if (const auto* se = ex.template target<fixpp::core::session_executor>()) {
        co_return fixpp::core::session_trace_context_of(*se);
    }
    co_return fixpp::otel::trace_context{};
}

}  // namespace fixpp
