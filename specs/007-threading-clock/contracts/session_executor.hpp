// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.8] project-owned executor
// wrapper class (round 3 root cause #1). It IS an executor (satisfies the
// ASIO executor concept) — NOT an alias to asio::any_io_executor.
#pragma once
#include <asio/any_io_executor.hpp>
#include <asio/strand.hpp>

namespace fixpp::session { class Session; enum class threading_mode : unsigned char; }

namespace fixpp::core {

template <class T> class expected_t;  // fwd

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

    // Construction by the make_session_executor helper below ([2d §4.8]
    // :913-915). Internal sites should not construct directly.
    session_executor(asio::any_io_executor inner,
                     fixpp::session::Session* session,
                     bool strand_wrapped) noexcept;

    // ASIO executor-concept surface (execute / query / context /
    // on_work_started / on_work_finished / operator== …) is provided by the
    // build header; elided in the oracle. The project-specific accessor:
    [[nodiscard]] fixpp::session::Session* session_ptr() const noexcept;

    // Discriminator ([2d §4.8]:947-950): true when constructed under
    // per_session_strand mode. Used by debug-build asserts and by the
    // re-entrancy guard test seam (seam 16); NOT on the runtime hot path.
    [[nodiscard]] bool is_strand_wrapped() const noexcept;

    bool operator==(const session_executor&) const noexcept = default;

    // private layout ([2d §4.8]:952-962): asio::any_io_executor inner_ +
    // fixpp::session::Session* session_ + bool strand_wrapped_. The
    // any_io_executor member makes session_executor copyable but NOT
    // trivially copyable (type-erased polymorphic handle) — see
    // data-model.md E6.
};

// Helper ([2d §4.8]:971-987): from a resolved (already-override-applied)
// executor + a threading_mode + the already_serialized_executor attestation
// + the owning Session pointer, return the session_executor every session
// coroutine binds to. Per C-P2-1 it returns expected_t<...> (NOT a naked
// session_executor / NOT noexcept-abort): under per_session_strand the inner
// executor is asio::make_strand(resolved_exec); under direct_executor +
// already_serialized_executor==true the inner executor is the bare attested
// resolved_exec. THIS IS THE SINGLE ENFORCEMENT POINT FOR
// error::executor_not_serialised ([2d §4.8]:996 / FR-009 / I-06): it returns
// error::executor_not_serialised when
// mode == direct_executor && !already_serialized_executor. Called from
// Session::open() with the resolved
// SessionConfig::executor_override.value_or(EngineConfig::executor)
// ([2d §4.8]:994).
[[nodiscard]] expected_t<session_executor>
    make_session_executor(asio::any_io_executor resolved_exec,
                          fixpp::session::threading_mode mode,
                          bool already_serialized_executor,
                          fixpp::session::Session* session) noexcept;

}  // namespace fixpp::core
