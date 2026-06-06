// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/session_executor.cpp
//
// fixpp::core::make_session_executor — THE SINGLE ENFORCEMENT POINT for
// error::executor_not_serialised (slot 48 / FR-009 / I-06 / [2d §4.8]:996).
// Defined in a session/ TU (NOT the core header) so the complete
// fixpp::session::threading_mode enum is visible without core/ back-edging
// into session/ ([arch §2.3] leaf rule — the core header only declares it).
// Linked into fixpp_session; its sole call site is Session::open() (T020).
#include <asio/strand.hpp>
#include <cassert>
#include <expected>
#include <fixpp/core/error.hpp>  // error enum values
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/trace_context.hpp>      // complete trace_context (bridge return)
#include <fixpp/session/session.hpp>         // complete Session (session_arena()/trace)
#include <fixpp/session/session_config.hpp>  // complete threading_mode
#include <memory_resource>
#include <utility>

namespace fixpp::core {

// NOLINTBEGIN(bugprone-exception-escape)
expected_t<session_executor> make_session_executor(asio::any_io_executor resolved_exec,
                                                   fixpp::session::threading_mode mode,
                                                   bool already_serialized_executor,
                                                   fixpp::session::Session* session) noexcept {
    using fixpp::session::threading_mode;

    switch (mode) {
        case threading_mode::per_session_strand:
            // The strand wrapping lives INSIDE inner_ ([2d §4.8]); the wrapper is
            // strand_wrapped == true.
            return session_executor{asio::any_io_executor{asio::make_strand(resolved_exec)},
                                    session,
                                    /*strand_wrapped=*/true};

        case threading_mode::direct_executor:
            if (!already_serialized_executor) {
                // THE single executor_not_serialised rejection (slot 48 / I-06).
                return std::unexpected(error::executor_not_serialised);
            }
            // Bare attested executor; no make_strand wrap.
            return session_executor{std::move(resolved_exec), session,
                                    /*strand_wrapped=*/false};
    }

    // Unreachable for the closed 2-value enum; defensive (out-of-range cast).
    return std::unexpected(error::invalid_session_config);
}
// NOLINTEND(bugprone-exception-escape)

// Engine-only adoption overload (D3-B / 023 T009).
//
// The engine pre-creates one strand per session (E-1/INV-1 / Engine::start())
// and passes it here via the adopt_strand_t tag. The strand is stored DIRECTLY
// with strand_wrapped=true (truthful) — no second make_strand wrap (D1 anti-pattern).
//
// NOLINTBEGIN(bugprone-exception-escape)
session_executor make_session_executor(adopt_strand_t, asio::any_io_executor strand_exec,
                                       fixpp::session::Session* session) noexcept {
    // Precondition (INV-3a / D3-B): strand_exec IS a strand created by the engine.
    // Store it directly — no re-wrap. strand_wrapped=true is truthful here because
    // the engine-created strand IS a strand; the public per_session_strand path
    // still unconditionally wraps (byte-unchanged at session_executor.cpp:35).
    return session_executor{std::move(strand_exec), session, /*strand_wrapped=*/true};
}
// NOLINTEND(bugprone-exception-escape)

// [2d §6.5]:1153-1154 arena bridge — defined HERE (session TU) so
// fixpp::session::Session is complete; the core header only declares it.
std::pmr::memory_resource* session_arena_of(const session_executor& exec) noexcept {
    auto* const s = exec.session_ptr();
    // NOLINTNEXTLINE(readability-implicit-bool-conversion)
    return (s != nullptr) ? s->session_arena() : nullptr;
}

// [2d §4.6] / E8 / I-11 current-trace-context bridge — defined HERE so
// Session is complete; trace_context.hpp's awaitable only declares it.
fixpp::otel::trace_context session_trace_context_of(const session_executor& exec) noexcept {
    auto* s = exec.session_ptr();
    if (s == nullptr) {
        // session_executor with no Session* (default-constructed wrapper) —
        // not the dispatch/awaiter path; engine snapshot is downstream's.
        return fixpp::otel::trace_context{};
    }
    if (!s->is_open()) {
        // Empty-slot mid-open: the slot is not yet populated (open() stores
        // it). Default trace_context + debug assert (E8 / I-12) — a
        // current_trace_context read before Session::open is a caller bug.
        assert(false &&
               "current_trace_context read before Session::open() populated "
               "the session_local<trace_context> slot");
        return fixpp::otel::trace_context{};
    }
    return s->get_trace_context();  // hit → trace_slot_.load() (017 owned amendment #1)
}

}  // namespace fixpp::core
