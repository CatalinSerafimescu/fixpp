// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/session_executor.hpp
//
// fixpp::core::session_executor — a PROJECT-OWNED value-typed class that IS
// an executor (satisfies asio::execution::is_executor_v; round 3 root cause
// #1). It is NOT an alias to asio::any_io_executor. It holds the resolved
// inner executor (asio::make_strand(...) result under per_session_strand, the
// bare attested executor under direct_executor — the strand wrapping lives
// INSIDE inner_) plus a typed Session*. session_ptr() is a PUBLIC MEMBER
// FUNCTION (NOT a property routed through asio::any_io_executor's closed
// property set — the round-1 query(void*) and round-2 typed-property designs
// were both rejected). The wrapper survives bind_executor / make_strand /
// require / prefer decoration because every ASIO machinery path operates
// against the executor concept (and require/prefer below re-wrap, preserving
// the typed Session*) — seam 21 enforces survival + the negative known-bad
// any_io_executor-cast assertion. T002 toolchain probe (2026-05-19) confirmed
// is_executor_v + bind_executor recovery on clang 22 / asio 1.36.0.
//
// Copyable but NOT trivially copyable (asio::any_io_executor is a type-erased
// polymorphic handle) — do NOT assert or imply trivial copyability (E6).
// [2d §4.8]. Realizes specs/007-threading-clock/contracts/session_executor.hpp.
#pragma once

#include <memory_resource>
#include <type_traits>
#include <utility>

#include <asio/any_io_executor.hpp>
#include <asio/execution.hpp>
#include <asio/prefer.hpp>
#include <asio/query.hpp>
#include <asio/require.hpp>

#include <fixpp/core/error.hpp>   // expected_t (std::expected) — core leaf, no session edge

// Forward declarations ONLY — core/ MUST NOT include any session/ header
// ([arch §2.3] leaf rule). threading_mode is defined in
// session/session_config.hpp; make_session_executor's body lives in a
// session/ TU (src/session/session_executor.cpp, T019) where the complete
// enum is visible. The header here only declares it.
namespace fixpp::session { class Session; enum class threading_mode : unsigned char; }
namespace fixpp::otel    { struct trace_context; }   // bridge return — incomplete OK in this decl

namespace fixpp::core {

class session_executor {
public:
    session_executor() noexcept = default;

    // Direct constructor — bypasses the threading_mode-vs-attestation
    // validation done by `make_session_executor` ([2d §4.8]:913-915 / I-06 /
    // FR-009 / slot 48). Prefer the factory for any new construction: it
    // enforces the `direct_executor && !already_serialized_executor` →
    // `error::executor_not_serialised` invariant. This constructor exists
    // for copy / require / prefer re-wrap on the dispatch hot path (round-3
    // RC#1 survival) and for default construction.
    session_executor(asio::any_io_executor inner,
                     fixpp::session::Session* session,
                     bool strand_wrapped) noexcept
        : inner_(std::move(inner)),
          session_(session),
          strand_wrapped_(strand_wrapped) {}

    // ── project-specific accessors ──────────────────────────────────────
    [[nodiscard]] fixpp::session::Session* session_ptr() const noexcept {
        return session_;
    }
    // true when constructed under per_session_strand ([2d §4.8]:947-950).
    // Used by debug-build asserts + the seam-16 re-entrancy guard; NOT on
    // the runtime hot path.
    [[nodiscard]] bool is_strand_wrapped() const noexcept {
        return strand_wrapped_;
    }
    // Engine-internal: the resolved inner executor (strand-wrapped or bare).
    [[nodiscard]] const asio::any_io_executor& underlying() const noexcept {
        return inner_;
    }

    // ── ASIO executor-concept surface (forwarded to inner_) ─────────────
    template <class F>
    void execute(F&& f) const {
        inner_.execute(std::forward<F>(f));
    }

    // NOLINTNEXTLINE(modernize-use-nodiscard,bugprone-exception-escape)
    asio::execution_context& query(asio::execution::context_t /*prop*/) const noexcept {
        return asio::query(inner_, asio::execution::context);
    }

    // asio::any_io_executor's target-validity check probes `context_as<U>`
    // (it is one of the any_io_executor supportable_properties — co_spawn(se,
    // …) needs it). NOT covered by the templated `query(Property)` below
    // because `can_query_v<any_io_executor, context_as_t<U>>` is false:
    // any_io_executor itself doesn't expose context_as via can_query, so the
    // constrained template SFINAE-rejects it. Explicit forwarding closes the
    // gap (seam 21 — survive erasure into any_io_executor for co_spawn).
    template <class U>
    U query(asio::execution::context_as_t<U> /*prop*/) const noexcept {
        return asio::query(inner_, asio::execution::context_as<U>);
    }

    template <class Property>
        requires asio::can_query_v<asio::any_io_executor, Property>
    auto query(Property p) const
        noexcept(asio::is_nothrow_query_v<asio::any_io_executor, Property>) {
        return asio::query(inner_, p);
    }

    // require/prefer RE-WRAP so the typed Session* survives the decoration
    // co_spawn applies (e.g. prefer(outstanding_work.tracked)) — the heart
    // of round-3 RC#1 survival.
    template <class Property>
        requires asio::can_require_v<asio::any_io_executor, Property>
    [[nodiscard]] session_executor require(Property p) const {
        return session_executor{asio::require(inner_, p), session_, strand_wrapped_};
    }

    template <class Property>
        requires asio::can_prefer_v<asio::any_io_executor, Property>
    [[nodiscard]] session_executor prefer(Property p) const {
        return session_executor{asio::prefer(inner_, p), session_, strand_wrapped_};
    }

    bool operator==(const session_executor& other) const noexcept {
        return inner_ == other.inner_
               && session_ == other.session_
               && strand_wrapped_ == other.strand_wrapped_;
    }
    bool operator!=(const session_executor& other) const noexcept {
        return !(*this == other);
    }

private:
    asio::any_io_executor    inner_;
    fixpp::session::Session* session_         = nullptr;
    bool                     strand_wrapped_  = false;
};

static_assert(asio::execution::is_executor_v<session_executor>,
              "session_executor MUST satisfy the ASIO executor concept "
              "(round-3 RC#1 / seam 21 / T002 probe)");
static_assert(!std::is_trivially_copyable_v<session_executor>,
              "session_executor is copyable but NOT trivially copyable "
              "(holds asio::any_io_executor) — E6; do not imply otherwise");

// THE SINGLE ENFORCEMENT POINT for error::executor_not_serialised (slot 48 /
// FR-009 / I-06 / [2d §4.8]:996). From a resolved (already-override-applied)
// executor + threading_mode + the already_serialized_executor attestation +
// the owning Session*: under per_session_strand the inner executor is
// asio::make_strand(resolved_exec); under direct_executor +
// already_serialized_executor==true it is the bare attested resolved_exec;
// mode==direct_executor && !already_serialized_executor →
// error::executor_not_serialised. Called from Session::open() with the
// resolved SessionConfig::executor_override.value_or(EngineConfig::executor).
// DEFINITION lands in US1 (T019); declared here so Phase-2 consumers link.
//
// ── CLIENT USE — trace_context Pattern B ──────────────────────────────────
//
// `make_session_executor` is the supported public primitive for binding a
// foreign executor (a background `asio::thread_pool`, a custom `io_context`,
// a `strand`) to a Session for trace_context propagation. See the Pattern B
// example in `fixpp::current_trace_context()`
// (`include/fixpp/core/trace_context.hpp`) — short form:
//
//     auto bg_se = fixpp::core::make_session_executor(
//         bg.get_executor(),                                   // foreign exec
//         fixpp::session::threading_mode::direct_executor,     // OR
//         //                                  ::per_session_strand for multi-thread
//         /*already_serialized_executor=*/false,
//         &sess);
//     // co_await asio::post(*bg_se, asio::use_awaitable) — resume runs on
//     // `bg`'s thread, UNDER a session_executor for &sess →
//     // current_trace_context() recovers the session's context.
//
// Pick `threading_mode`:
//   • `per_session_strand` (recommended for multi-thread foreign pools)
//     wraps the foreign executor in `asio::make_strand(...)`. Safe by
//     construction; `already_serialized_executor` is ignored.
//   • `direct_executor` with `already_serialized_executor=true` is the
//     attestation path — use ONLY when the foreign executor is intrinsically
//     serialized (a single-thread `io_context`, an explicit `strand`, a
//     1-worker `thread_pool`). `direct_executor && !attested` returns
//     `error::executor_not_serialised` (slot 48).
//
// Cf. the book chapter `book/concurrency_model.md` for the full topology
// + worked examples + A-vs-B selection guidance.
[[nodiscard]] expected_t<session_executor>
make_session_executor(asio::any_io_executor resolved_exec,
                       fixpp::session::threading_mode mode,
                       bool already_serialized_executor,
                       fixpp::session::Session* session) noexcept;

// The [2d §6.5]:1153-1154 arena-derivation bridge: recovers the never-null
// session PMR arena THROUGH the wrapper (== exec.session_ptr()->
// session_arena(), the [2d §4.5] chain). DECLARED here (core leaf) so
// cancellable_dispatch.hpp can derive the dispatch-node arena without core/
// including any session/ header; DEFINITION lands in the session TU
// (src/session/session_executor.cpp) where fixpp::session::Session is
// complete ([arch §2.3] leaf rule — mirrors make_session_executor). Returns
// nullptr only for a default-constructed (session-less) wrapper — never on
// the dispatch path, where the wrapper always carries a Session*.
[[nodiscard]] std::pmr::memory_resource*
session_arena_of(const session_executor& exec) noexcept;

// The [2d §4.6] / E8 / I-11 current-trace-context bridge: resolves the
// session-domain trace_context THROUGH the typed wrapper (session_ptr() — a
// MEMBER FUNCTION, round-3 RC#1 — NOT any_io_executor::query). hit
// (session_ptr non-null, session open) → the session's trace_slot_.load();
// empty-slot mid-open (recovered but not yet open) → default trace_context
// + a debug assert; session-less wrapper → default trace_context (the
// concrete engine_trace_context snapshot fallback is the downstream Engine's
// — 007 ships no Engine type; D-17). DECLARED here (core leaf) so the
// current_trace_context awaitable in trace_context.hpp recovers Session
// without core/ including any session/ header; DEFINITION lands in the
// session TU (src/session/session_executor.cpp) where Session is complete
// ([arch §2.3] — mirrors session_arena_of / make_session_executor).
[[nodiscard]] fixpp::otel::trace_context
session_trace_context_of(const session_executor& exec) noexcept;

}  // namespace fixpp::core
