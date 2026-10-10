// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/session_strand.cpp — `make_session_strand` (fixpp#544, B35;
// `.specify/544-hot-path-zero-alloc.md` §2.1). Every session strand is built here, so the
// session executor, the transport and `SessionEntry::session_strand` compare equal only
// when they come from one call. Re-derive the callers with
// `git grep -n make_session_strand -- src`.
#include "session_strand.hpp"

#include <asio/any_io_executor.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>

namespace fixpp::session::detail {

asio::any_io_executor make_session_strand(asio::any_io_executor const& exec) {
    if (auto const* const io = exec.target<asio::io_context::executor_type>()) {
        return asio::any_io_executor{asio::make_strand(session_inner_executor_t{*io})};
    }
    return asio::any_io_executor{asio::make_strand(exec)};
}

}  // namespace fixpp::session::detail
