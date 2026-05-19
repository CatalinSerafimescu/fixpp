// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.6] session-serialisation-
// domain-local storage. NOT thread_local. NOT executor-property-based.
#pragma once

namespace fixpp::core {

// Owned by the Session object (one slot per Session for trace_context).
// Storage is plain ownership: the slot survives coroutine resume on a
// different thread because the value is read through the borrowed, stable
// Session pointer (recovered from session_executor::session_ptr() — §4.8).
// Caller MUST be inside the owning session's serialisation domain (strand
// under per_session_strand; attested executor under direct_executor);
// debug builds assert via a Session* self-check on the recovered pointer.
template <typename T>
class session_local {
public:
    session_local() noexcept = default;

    [[nodiscard]] T const& load() const noexcept;   // in-domain only
    [[nodiscard]] T&       load() noexcept;          // in-domain only
    void store(T value) noexcept;                    // session open: from initial_trace_context
    void clear() noexcept;                           // session close: reset to default

private:
    T value_{};
};

}  // namespace fixpp::core
