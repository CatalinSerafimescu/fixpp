// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/session_local.hpp
//
// fixpp::core::session_local<T> — session-serialisation-domain-local storage.
// NOT thread_local. NOT executor-property-based (D-3). Owned by the Session
// object (one slot per Session for trace_context). The slot survives
// coroutine resume on a different thread because the value is read through
// the borrowed, stable Session pointer recovered from
// session_executor::session_ptr() ([2d §4.8]); storage here is plain value
// ownership. Caller MUST be inside the owning session's serialisation domain
// (strand under per_session_strand; attested executor under direct_executor);
// the debug Session* self-check on the RECOVERED pointer lives at the
// current_trace_context awaiter recovery site (T044/US4 — FR-015), this slot
// is the storage primitive. [2d §4.6] / E7.
// Realizes specs/007-threading-clock/contracts/session_local.hpp.
#pragma once

#include <utility>

namespace fixpp::core {

template <typename T>
class session_local {
public:
    session_local() noexcept = default;

    [[nodiscard]] T const& load() const noexcept { return value_; }   // in-domain only
    [[nodiscard]] T&       load() noexcept        { return value_; }   // in-domain only

    void store(T value) noexcept { value_ = std::move(value); }        // open: from initial_trace_context
    void clear() noexcept        { value_ = T{}; }                     // close: reset to default

private:
    T value_{};
};

}  // namespace fixpp::core
