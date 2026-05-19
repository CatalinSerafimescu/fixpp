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
#include <fixpp/core/session_executor.hpp>

#include <utility>

#include <asio/strand.hpp>

#include <fixpp/session/session_config.hpp>   // complete threading_mode

namespace fixpp::core {

expected_t<session_executor>
make_session_executor(asio::any_io_executor resolved_exec,
                       fixpp::session::threading_mode mode,
                       bool already_serialized_executor,
                       fixpp::session::Session* session) noexcept {
    using fixpp::session::threading_mode;

    switch (mode) {
    case threading_mode::per_session_strand:
        // The strand wrapping lives INSIDE inner_ ([2d §4.8]); the wrapper is
        // strand_wrapped == true.
        return session_executor{
            asio::any_io_executor{asio::make_strand(std::move(resolved_exec))},
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

}  // namespace fixpp::core
