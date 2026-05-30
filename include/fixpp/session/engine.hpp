// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/engine.hpp
//
// fixpp::session::SessionId — registry key value type (T004)
// fixpp::session::Engine    — public multi-session runtime engine (T005)
//
// Engine lives in the EXISTING session/ module (R1 / [arch §4.4]).
// No new module; no check_layers.py ALLOWED-map change (R1).
//
// Implementation lives in src/session/engine.cpp (T006 — NOT this slice).
// DECLARATIONS ONLY here; the header must compile when included from any TU.
//
// Threading: all registry mutation/iteration is sequenced on the engine
// strand (E-5). NO std::mutex — [const §XV.9].
//
// [arch §4.4 (session public types)]: see §4.4 entry added to architecture.md.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fixpp/core/engine_config.hpp>  // EngineConfig — held by value in Engine
#include <fixpp/core/error.hpp>          // expected_t<T>, error enum (incl. slot 121)
#include <fixpp/session/session_config.hpp>  // SessionConfig (complete — by-value store)

namespace fixpp::session {
class Session;
}  // namespace fixpp::session

// ── SessionId — registry key value type (T004 / R6 / E-1) ───────────────────
//
// Regular value type: copyable, equality-comparable, hashable.
// Three-field FIX session identity; NO qualifier field (Gate A New-5).
//
// Construction:
//   SessionId::from_config(cfg)              — own-role key (registry insertion)
//   SessionId::reversed_from_logon(...)      — acceptor resolution (R4 / QFC/QFJ)
//
// Hash: all three fields participate so the type can key std::unordered_map.

namespace fixpp::session {

struct SessionId {
    std::string begin_string;
    std::string sender_comp_id;
    std::string target_comp_id;

    // Default-generated three-way equality over all three fields (C++20).
    friend bool operator==(SessionId const&, SessionId const&) noexcept = default;

    // Own-role key: { cfg.begin_string, cfg.sender_comp_id, cfg.target_comp_id }.
    // Used when registering a session (both initiator and acceptor) in the
    // engine registry.  [E-1 / data-model "SessionId"]
    static SessionId from_config(SessionConfig const& cfg) {
        return {cfg.begin_string, cfg.sender_comp_id, cfg.target_comp_id};
    }

    // Acceptor resolution: reverse the inbound Logon's CompIDs (R4 / E-2).
    // An acceptor whose registry key is {bs, sender=ME, target=PEER} is
    // resolved by a Logon whose SenderCompID(49)=PEER and TargetCompID(56)=ME:
    //   result = { begin_string, sender = logon_target, target = logon_sender }
    // Mirrors QFC lookupSession(…, true) / QFJ getReverseSessionID().
    static SessionId reversed_from_logon(std::string begin_string,
                                         std::string_view logon_sender_comp_id,
                                         std::string_view logon_target_comp_id) {
        return {std::move(begin_string),
                std::string{logon_target_comp_id},  // sender = logon_target
                std::string{logon_sender_comp_id}}; // target = logon_sender
    }
};

}  // namespace fixpp::session

// ── std::hash<SessionId> — required for unordered_map keying ─────────────────

template <>
struct std::hash<fixpp::session::SessionId> {
    std::size_t operator()(fixpp::session::SessionId const& id) const noexcept {
        // Combine hashes of all three fields.  FNV-inspired mix; no dependency
        // on boost::hash_combine.  All three fields participate (E-5 / data-model).
        std::hash<std::string> h;
        std::size_t seed = h(id.begin_string);
        seed ^= h(id.sender_comp_id) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        seed ^= h(id.target_comp_id) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// ── SessionEntry — registry value (data-model "SessionEntry" / E-5) ──────────

namespace fixpp::session {

/// Per-registered-session state stored in the Engine registry.
///
/// `session` is null until the spawned loop reaches the lazy ctor + open()
/// (construction is LAZY — never at register_session time or in start()).
/// `lookup()` returns null while session is null (Gate A New-3).
struct SessionEntry {
    enum class role : std::uint8_t { initiator = 0, acceptor = 1 };

    /// Owned session object; null until the accept/connect loop constructs it.
    std::unique_ptr<Session> session;

    role session_role = role::initiator;

    /// Retained config for re-accept/reconnect + identity resolution.
    SessionConfig config;

    /// Per-session teardown handle (read-pump + session work).
    /// Distinct from the per-listener accept-scope domain (E-7).
    asio::cancellation_signal session_cancel;
};

// ── Engine — public multi-session runtime engine (T005 / R1 / E-1) ───────────
//
// Owns the session registry and per-role loops; bound to a caller-supplied
// executor (clarify-Q3).  NOT move-constructible (holds spawned coroutine
// state + registry).  Use unique_ptr<Engine> to move ownership if needed.
//
// Lifecycle:  constructed → started → stopping → stopped
//   start()  once; stop()  idempotent from any state.
//
// Threading:
//   All registry mutation/iteration sequenced on the engine strand (E-5).
//   NO std::mutex ([const §XV.9]).  Each spawned loop/pump MUST call
//     co_await asio::this_coro::reset_cancellation_state(
//             asio::enable_total_cancellation())
//   so stop()'s total cancel actually propagates
//   ([feedback_asio_cospawn_total_cancellation_default]).
//
// Destructor: STRICT assert(stopped()) — no synchronous best-effort drain
//   (Gate A Codex-9; a synchronous dtor cannot run the caller-driven loop
//   to drain in-flight coroutines holding raw Session* → UAF, the 014 burn).
//   Callers MUST co_await stop() before destroying the Engine.
//
// Implementation: src/session/engine.cpp (T006).

class Engine {
public:
    /// Caller-supplied executor; the engine owns NO worker threads (clarify-Q3).
    /// All loops co_spawn on exec; start() does NOT block or run the executor.
    Engine(asio::any_io_executor exec, fixpp::core::EngineConfig cfg);

    Engine(Engine const&)            = delete;
    Engine& operator=(Engine const&) = delete;
    Engine(Engine&&)                 = delete;
    Engine& operator=(Engine&&)      = delete;

    /// STRICT precondition: assert(stopped()).
    /// No synchronous best-effort teardown — callers must co_await stop() first.
    ~Engine();

    /// FR-002: register a session from its config; records config + role ONLY.
    /// Does NOT construct a Session (lazy construction inside spawned loops).
    /// Duplicate SessionId::from_config(cfg) → session_invalid_argument (119).
    /// Must be called before start() in 015.  NO Application& parameter (FR-013).
    [[nodiscard]] fixpp::core::expected_t<void> register_session(SessionConfig cfg);

    /// FR-001/FR-003: non-blocking.  co_spawns one connect loop per initiator
    /// and one accept loop per acceptor on exec.  Legal to call once.
    /// Each loop co_awaits Session::open() as its first async step (open()
    /// cannot run in this synchronous void start() — Gate A New-3).
    void start();

    /// FR-011: idempotent total-cancellation teardown.
    /// Cancels every accept loop, connect loop, read-pump, accept-scope domain,
    /// and in-flight handshake via cancellation_type::total; closes transports;
    /// then JOINS all outstanding session work BEFORE clearing the registry
    /// that owns the Session objects (join-before-clear — Gate A New-4).
    /// A second stop() is a no-op.  Returns when teardown is complete.
    [[nodiscard]] asio::awaitable<void> stop();

    /// Registry addressing.  Returns nullptr if `id` is not registered, OR is
    /// registered but not yet established (e.g. acceptor with no peer yet, or
    /// a session whose loop has not yet reached open()) — null is NOT an error
    /// (Gate A New-3).
    [[nodiscard]] Session* lookup(SessionId const& id) const;

    [[nodiscard]] bool stopped() const noexcept;

private:
    // Injected executor; all loops co_spawn on this.
    asio::any_io_executor exec_;

    // Engine-level shared config (dictionaries, clock, transport factory, …).
    // NO Application& (FR-013 / Gate A New-2).
    fixpp::core::EngineConfig engine_cfg_;

    // Engine strand — serialises registry mutation/iteration (E-5).
    // Derived from exec_ in the ctor.  No std::mutex.
    asio::any_io_executor engine_strand_;

    // Session registry — keyed on SessionId, owned here (join-before-clear E-7).
    std::unordered_map<SessionId, SessionEntry> registry_;

    // Per-listener accept-scope cancellation signals (one per acceptor config).
    // Distinct from the per-session SessionEntry::session_cancel (E-7).
    std::unordered_map<SessionId, asio::cancellation_signal> accept_scope_signals_;

    // Stopped flag — ensures stop() is idempotent and dtor assert fires
    // correctly.  Sequenced on the engine strand (no atomic needed while
    // everything runs through the strand).
    bool stopped_ = false;

    // Rebindable outbound send-slot machinery: for an acceptor session the live
    // transport is unknown at open() time; the engine captures a forwarding
    // function as cfg.transport_send at open() and repoints it at the live
    // Transport::async_write during the acceptor attach (E-1 / E-2 / R7).
    // Each entry stores the rebindable function that forwards to the live sink.
    // (The actual slot is a std::function<void(std::span<const std::byte>)>
    // inside SessionConfig::transport_send; the engine wraps it.)
    std::unordered_map<SessionId, std::function<void(std::span<const std::byte>)>>
        send_slots_;
};

}  // namespace fixpp::session
