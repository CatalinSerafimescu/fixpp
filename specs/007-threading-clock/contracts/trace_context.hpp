// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.6]/§1.2 — fixpp::otel::
// trace_context POD (minimal; 2k extends — D-1) + the current_trace_context
// free awaitable.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace fixpp::otel {

// Minimal 32-byte trivially-copyable value (trace_id 16 + span_id 8 +
// flags 1 + padding). Lock-free-atomic-eligible. 2k extends this namespace;
// it does NOT redefine the POD.
struct trace_context {
    std::array<std::byte, 16> trace_id{};
    std::array<std::byte, 8>  span_id{};
    std::uint8_t              flags{};
    // implementation padding to 32 B
};

}  // namespace fixpp::otel

namespace fixpp {

// Free, stateless, value-typed awaitable. Inside a session serialisation
// domain → the session's session_local<trace_context> slot, recovered via
// session_executor::session_ptr() (a MEMBER FUNCTION — §4.8, round 3 root
// cause #1 — NOT asio::any_io_executor::query, NOT thread_local). Outside
// any session (control plane, listener accept, bootstrap) → the engine's
// atomic snapshot of EngineConfig::engine_trace_context (N-P2-2). Safe
// across coroutine resume on a different thread (read via the stable
// Session*). A single global object is the canonical instance.
inline constexpr struct current_trace_context_t {
    [[nodiscard]] auto operator co_await() const noexcept;
} current_trace_context;

}  // namespace fixpp
