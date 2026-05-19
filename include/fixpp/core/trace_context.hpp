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
