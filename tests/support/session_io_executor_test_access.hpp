#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/session_io_executor_test_access.hpp — TEST-ONLY access to
// session_io_executor's private members.
//
// fixpp#544 (B35): `session_io_executor` (src/session/session_strand.hpp, a private header
// that is never installed) declares `friend struct session_io_executor_test_access;`.
// Define this struct only in this header; a differing definition elsewhere in the program
// is an ODR violation (ill-formed, no diagnostic required). Production targets must not
// include this header.

#include <asio/io_context.hpp>

#include "session/session_strand.hpp"

namespace fixpp::session::detail {

struct session_io_executor_test_access {
    // The io_context executor that `e.execute()` forwards to.
    [[nodiscard]] static asio::io_context::executor_type inner(
        session_io_executor const& e) noexcept {
        return e.inner();
    }
};

}  // namespace fixpp::session::detail
