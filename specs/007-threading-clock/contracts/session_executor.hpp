// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.8] project-owned executor
// wrapper class (round 3 root cause #1). It IS an executor (satisfies the
// ASIO executor concept) — NOT an alias to asio::any_io_executor.
#pragma once
#include <asio/any_io_executor.hpp>
#include <asio/strand.hpp>

namespace fixpp::session { class Session; }

namespace fixpp::core {

// Value-typed wrapper that satisfies asio::execution::is_executor_v.
// Holds the resolved inner executor — asio::strand<asio::any_io_executor>
// under per_session_strand, a BARE asio::any_io_executor under
// direct_executor — plus a typed Session*. session_ptr() is a PUBLIC
// MEMBER FUNCTION (NOT a property routed through asio::any_io_executor's
// closed property set — the round-2 typed-property formulation was rejected,
// regression-equivalent to the round-1 query(void*) design). The wrapper
// survives bind_executor / make_strand decoration on engine-controlled paths
// because ASIO machinery operates against the executor concept and never
// erases the wrapper into any_io_executor there (seam 21 enforces this +
// the negative known-bad any_io_executor-cast assertion).
class session_executor {
public:
    session_executor() noexcept = default;

    // ASIO executor-concept surface (execute / context / on_work_started /
    // on_work_finished / operator== …) is provided by the build header;
    // elided in the oracle. The project-specific accessor:
    [[nodiscard]] fixpp::session::Session* session_ptr() const noexcept;

    bool operator==(const session_executor&) const noexcept = default;
};

}  // namespace fixpp::core
