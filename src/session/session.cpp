// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/session.cpp
//
// fixpp::session::Session — minimal real skeleton out-of-line impl (D-4 /
// E10). Phase 2 (T012) ships the ctor + the never-null session_arena()
// resolution chain + linkable open()/close() placeholders. The 2d-owned
// BEHAVIOUR is wired per user story (T020/T030/T037/T038/T039/T045/T050) —
// each replaces the marked placeholder body, it is not additive guesswork.
#include <fixpp/session/session.hpp>

#include <memory_resource>
#include <utility>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session_config.hpp>

namespace fixpp::session {

namespace {
// [2d §4.5] never-null resolution chain: SessionConfig::session_arena ?:
// EngineConfig::default_session_resource ?: std::pmr::get_default_resource().
std::pmr::memory_resource* resolve_session_arena(
    const fixpp::core::EngineConfig& engine,
    const SessionConfig& cfg) noexcept {
    if (cfg.session_arena != nullptr) return cfg.session_arena;
    if (engine.default_session_resource != nullptr)
        return engine.default_session_resource;
    return std::pmr::get_default_resource();
}
}  // namespace

Session::Session(const fixpp::core::EngineConfig& engine,
                 const SessionConfig& cfg) noexcept
    : engine_(engine),
      cfg_(cfg),
      session_arena_(resolve_session_arena(engine, cfg)) {
    // Resolution chain always terminates at std::pmr::get_default_resource()
    // (never null), so I-18's never-null contract holds for the lifetime.
}

Session::~Session() = default;

std::pmr::memory_resource* Session::session_arena() const noexcept {
    return session_arena_;  // I-18: frozen at ctor, never null, never swapped
}

// ── Phase-2 linkable placeholders — REPLACED per user story ─────────────
// Marked so a later phase's task body substitutes (not appends to) these.

asio::awaitable<fixpp::core::expected_t<void>> Session::open() noexcept {
    using fixpp::core::error;

    // (5) reject a second open() on the same handle (slot 51 / FR-018).
    // Any non-never_opened state means open() already ran.
    if (state_ != lifecycle::never_opened) {
        co_return std::unexpected(error::session_already_open);
    }

    // (1) executor resolution — the uniform resolved =
    // override.value_or(engine_anchor) pattern (FR-016/FR-018).
    asio::any_io_executor resolved =
        cfg_.executor_override.value_or(engine_.executor);

    // (4a) null EngineConfig::executor (no override either) →
    // invalid_session_config (slot 53 / FR-018). any_io_executor is
    // contextually false when it holds no target.
    if (!resolved) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // (4b) direct_executor + lock_policy::spin is rejected even when
    // attested (slot 53 / I-06 / FR-009). [const §XI.5]: the store-write
    // path is always mutex; spin under a bare attested executor has no
    // engine-internal serialisation to fall back on.
    if (cfg_.mode == threading_mode::direct_executor
        && cfg_.locks == lock_policy::spin) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // (1) SINGLE error::executor_not_serialised enforcement point (slot 48
    // / FR-009 / I-06): make_session_executor wraps make_strand under
    // per_session_strand, carries the bare attested executor under
    // direct_executor, and rejects direct_executor && !attested.
    auto bound = fixpp::core::make_session_executor(
        std::move(resolved), cfg_.mode, cfg_.already_serialized_executor, this);
    if (!bound) {
        co_return std::unexpected(bound.error());
    }
    exec_ = std::move(*bound);

    // (2) effective_clock = SessionConfig::clock_override ?:
    // EngineConfig::clock, resolved ONCE here, bound to session lifetime
    // (FR-005 / I-03). The clock_not_set engine-level gate (FR-006) is
    // validate_engine_config() at Engine::open — independent of per-session
    // overrides; Session::open only resolves.
    effective_clock_ = cfg_.clock_override ? cfg_.clock_override
                                           : engine_.clock;

    // (3) trace_slot_ population          → wired by T045 (US4)
    // (4c) null dictionary / sentinel     → wired by T050 (US5)

    state_ = lifecycle::open;
    co_return fixpp::core::expected_t<void>{};
}

asio::awaitable<fixpp::core::expected_t<void>>
Session::close(close_mode /*mode*/) noexcept {
    // PLACEHOLDER (T012). Real body: T037 (US3 two-phase close + phase-1
    // FileStore::flush_for_session_close() hook), T038 (idempotent
    // three-state model + cancellation surfacing), T039 (phase-2 root total
    // propagation), T045 (trace-slot teardown).
    co_return fixpp::core::expected_t<void>{};
}

}  // namespace fixpp::session
