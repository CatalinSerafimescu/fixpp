// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/engine.cpp — Engine lifecycle substrate (T006).
// Anchors: data-model.md E-1/E-5/E-7, research.md R1/R7/R9,
//          contracts/engine_api.md, contracts/realized-behavior.md C5/C6,
//          [const §XI.2/§XI.4/§XV.9],
//          [[feedback_asio_cospawn_total_cancellation_default]]
//
// Loop stubs: run_accept_loop / run_connect_loop are MINIMAL COMPILING STUBS.
// US1 (T012) fleshes out run_accept_loop; US2 (T015/T016) run_connect_loop.
// The stop() join-before-clear shape is fully correct here (Gate A New-4/E-7).

#include <fixpp/session/engine.hpp>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <cassert>
#include <chrono>
#include <memory>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

namespace fixpp::session {

using core::expected_t;
using core::error;

// Shared outstanding-loop counter for join-before-clear (E-7 / Gate A New-4).
// Each spawned loop decrements on exit. stop() waits until count == 0 before
// clearing the registry (which owns Session objects). shared_ptr so loops can
// safely decrement after co_return even during concurrent stop().
using outstanding_t = std::shared_ptr<std::atomic<int>>;

// RAII decrement — fires on co_return OR on exception (total-cancel throws
// asio::system_error(operation_aborted) through the coroutine frame; locals
// are destroyed so this guard fires correctly).
struct counter_guard {
    outstanding_t counter;
    explicit counter_guard(outstanding_t c) : counter{std::move(c)} {}
    counter_guard(counter_guard&&) = default;
    counter_guard(counter_guard const&) = delete;
    ~counter_guard() { if (counter) --(*counter); }
};

// ── ctor ─────────────────────────────────────────────────────────────────────

Engine::Engine(asio::any_io_executor exec, fixpp::core::EngineConfig cfg)
    : exec_{std::move(exec)}
    , engine_cfg_{std::move(cfg)}
    // Derive engine strand from injected executor (E-5 / [const §XV.9]).
    // NO std::mutex — strand is the sole serialisation mechanism.
    , engine_strand_{asio::make_strand(exec_)}
    , stopped_{false}
{}

// ── dtor ──────────────────────────────────────────────────────────────────────
// STRICT precondition: assert(stopped()).
// No synchronous best-effort teardown — synchronous dtor cannot drain
// in-flight coroutines holding raw Session* → UAF (Gate A Codex-9 / E-7).

Engine::~Engine()
{
    assert(stopped_ && "Engine destroyed without calling co_await stop() first");
}

// ── register_session (FR-002 / E-1) ──────────────────────────────────────────
// Records config + role only; does NOT construct a Session (lazy — Gate A New-3).
// Duplicate SessionId::from_config(cfg) → session_invalid_argument (119 / R5).

expected_t<void> Engine::register_session(SessionConfig cfg)
{
    SessionId id = SessionId::from_config(cfg);  // derive key BEFORE move
    if (registry_.count(id) != 0)
        return std::unexpected(error::session_invalid_argument);

    SessionEntry::role role = (cfg.role == session_role::acceptor)
        ? SessionEntry::role::acceptor : SessionEntry::role::initiator;

    // operator[] default-constructs in-place (SessionEntry contains
    // non-movable asio::cancellation_signal — cannot move-insert).
    auto& entry = registry_[id];  // id copied into map key
    entry.session_role = role;
    entry.config = std::move(cfg);
    return {};
}

// ── lookup (Gate A New-3) ─────────────────────────────────────────────────────

Session* Engine::lookup(SessionId const& id) const
{
    auto it = registry_.find(id);
    return (it == registry_.end()) ? nullptr : it->second.session.get();
}

bool Engine::stopped() const noexcept { return stopped_; }

// ── Loop stubs ────────────────────────────────────────────────────────────────
// EVERY co_spawned loop MUST reset_cancellation_state(total) as its first step
// or stop()'s total-cancel is swallowed silently (co_spawn defaults to
// terminal-only). [[feedback_asio_cospawn_total_cancellation_default]] / [const §XI.2]
//
// Lazy Session construction: ctor + co_await open() run INSIDE each loop
// (open() is awaitable; cannot run in synchronous void start()).

namespace {

// run_accept_loop — STUB (US1 T012 replaces the interior).
asio::awaitable<void>
run_accept_loop(fixpp::core::EngineConfig const& engine_cfg,
                SessionEntry& entry,
                asio::cancellation_signal& /*accept_scope_signal*/,
                outstanding_t counter)
{
    counter_guard guard{counter};  // decrements on any exit path

    // MANDATORY total-cancel reset.
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation());

    // Lazy Session construction + open() (E-1 / Gate A New-3).
    entry.session = std::make_unique<Session>(engine_cfg, entry.config);
    // Rebindable send-slot captured by Session::open() at cfg.transport_send
    // (E-1 / R7(b)); acceptor attach (T011) will repoint it at the live
    // Transport::async_write.
    auto res = co_await entry.session->open();
    if (!res.has_value()) { entry.session.reset(); co_return; }

    // STUB — US1 T012 inserts the full accept/handshake/resolve/attach loop here.
    co_return;
}

// run_connect_loop — STUB (US2 T016 replaces the interior).
asio::awaitable<void>
run_connect_loop(fixpp::core::EngineConfig const& engine_cfg,
                 SessionEntry& entry,
                 outstanding_t counter)
{
    counter_guard guard{counter};

    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation());

    entry.session = std::make_unique<Session>(engine_cfg, entry.config);
    auto res = co_await entry.session->open();
    if (!res.has_value()) { entry.session.reset(); co_return; }

    // STUB — US2 T016 inserts drive_reconnect_attempt + read-pump loop here.
    co_return;
}

}  // namespace

// ── start (FR-001/FR-003 / data-model "Public surface") ──────────────────────
// Non-blocking. co_spawns one per-role loop per registered session on exec_.
// Each loop co_awaits open() itself — cannot run in this synchronous void.

void Engine::start()
{
    auto counter = std::make_shared<std::atomic<int>>(0);

    for (auto& [id, entry] : registry_) {
        ++(*counter);
        if (entry.session_role == SessionEntry::role::acceptor) {
            auto& scope_sig = accept_scope_signals_[id];  // default-constructs
            asio::co_spawn(exec_,
                run_accept_loop(engine_cfg_, entry, scope_sig, counter),
                asio::bind_cancellation_slot(
                    entry.session_cancel.slot(), asio::detached));
        } else {
            asio::co_spawn(exec_,
                run_connect_loop(engine_cfg_, entry, counter),
                asio::bind_cancellation_slot(
                    entry.session_cancel.slot(), asio::detached));
        }
    }
    outstanding_counter_ = counter;
}

// ── stop (FR-011 / C5 / E-7) ─────────────────────────────────────────────────
// Idempotent total-cancellation teardown.
//  1. Guard: second call is a no-op.
//  2. Total-cancel every per-session loop + every accept-scope domain.
//  3. JOIN: yield to executor until outstanding counter reaches zero (each loop
//     decrements via counter_guard on exit). Join-before-clear invariant:
//     no Session* dereference after registry_.clear() (Gate A New-4 / E-7).
//  4. Clear registry.

asio::awaitable<void> Engine::stop()
{
    if (stopped_) { co_return; }
    stopped_ = true;

    for (auto& [id, entry] : registry_)
        entry.session_cancel.emit(asio::cancellation_type::total);
    for (auto& [id, sig] : accept_scope_signals_)
        sig.emit(asio::cancellation_type::total);

    // JOIN: yield until all loops have co_return'd.
    if (outstanding_counter_) {
        asio::steady_timer t{co_await asio::this_coro::executor};
        while (outstanding_counter_->load(std::memory_order_acquire) > 0) {
            t.expires_after(std::chrono::milliseconds{0});
            co_await t.async_wait(asio::use_awaitable);
        }
        outstanding_counter_.reset();
    }

    // Safe now: all loops have exited; Session objects may be freed.
    accept_scope_signals_.clear();
    registry_.clear();
}

}  // namespace fixpp::session
