// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/retrieve_visitor.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::retrieve_visitor.
// Frozen at /plan time. Anchor: .specify/2e-msgstore.md v0.4 §4.5.
//
// The visitor's on_frame is awaitable (root cause #2) so the visitor's
// per-frame work can suspend on its own async I/O WITHOUT the store holding
// its writer mutex (I-03). The span is stable across the visitor's co_await
// (I-04); the visitor MUST NOT retain it past the awaitable's completion.
#pragma once

#include <span>
#include <cstddef>
#include <cstdint>

#include <asio/awaitable.hpp>

#include <fixpp/core/error.hpp>                    // expected_t / error
#include <fixpp/session/seqnum.hpp>                // seqnum_t

namespace fixpp::session {

enum class visit_result : std::uint8_t {
    cont  = 0,  // continue to next frame
    stop  = 1,  // stop walking; retrieve() returns expected_t<void>{}
    abort = 2,  // abort walking; retrieve() returns visitor.abort_error()
};

class retrieve_visitor {
public:
    retrieve_visitor()                                       = default;
    retrieve_visitor(const retrieve_visitor&)                = delete;
    retrieve_visitor& operator=(const retrieve_visitor&)     = delete;
    retrieve_visitor(retrieve_visitor&&)                     = delete;
    retrieve_visitor& operator=(retrieve_visitor&&)          = delete;
    virtual ~retrieve_visitor()                              = default;

    // on_frame: called once per persisted frame in seqnum order. The frame
    // span is store-owned, [[clang::lifetimebound]] to the awaitable's
    // duration — visitor MUST NOT retain it past the awaitable's completion.
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<visit_result>>
    on_frame(seqnum_t seq,
             std::span<const std::byte> frame [[clang::lifetimebound]]) noexcept = 0;

    // abort_error: overridable virtual hook. Default returns
    // store_visitor_aborted (slot 62). Surfaces through retrieve()'s
    // expected_t<void> when on_frame returns visit_result::abort (I-20).
    [[nodiscard]] virtual fixpp::core::error abort_error() const noexcept {
        return fixpp::core::error::store_visitor_aborted;
    }
};

}  // namespace fixpp::session
