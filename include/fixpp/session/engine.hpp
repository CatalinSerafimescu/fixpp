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
// Threading: all registry mutation/iteration is confined to the single
// injected executor (E-5 — single-executor confinement, not a strand).
// NO std::mutex — [const §XV.9].
//
// [arch §4.4 (session public types)]: see §4.4 entry added to architecture.md.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/strand.hpp>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>  // EngineConfig — held by value in Engine
#include <fixpp/core/error.hpp>          // expected_t<T>, error enum (incl. slot 121)
#include <fixpp/otel/trace_context.hpp>  // fixpp::otel::trace_context (for engine_trace_context())
#include <fixpp/session/session_config.hpp>  // SessionConfig (complete — by-value store)
#include <fixpp/transport/endpoint.hpp>      // Endpoint (for acceptor_bound_endpoint return type)
#include <fixpp/transport/listener.hpp>      // abstract Listener (for listeners_ map)
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fixpp::session {
class Session;
}  // namespace fixpp::session

namespace fixpp::transport {
class Transport;  // non-owning live_transport pointer in SessionEntry (T018)
}  // namespace fixpp::transport

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
        return {.begin_string = cfg.begin_string,
                .sender_comp_id = cfg.sender_comp_id,
                .target_comp_id = cfg.target_comp_id};
    }

    // Acceptor resolution: reverse the inbound Logon's CompIDs (R4 / E-2).
    // An acceptor whose registry key is {bs, sender=ME, target=PEER} is
    // resolved by a Logon whose SenderCompID(49)=PEER and TargetCompID(56)=ME:
    //   result = { begin_string, sender = logon_target, target = logon_sender }
    // Mirrors QFC lookupSession(…, true) / QFJ getReverseSessionID().
    static SessionId reversed_from_logon(std::string begin_string,
                                         std::string_view logon_sender_comp_id,
                                         std::string_view logon_target_comp_id) {
        return {.begin_string = std::move(begin_string),
                .sender_comp_id = std::string{logon_target_comp_id},   // sender = logon_target
                .target_comp_id = std::string{logon_sender_comp_id}};  // target = logon_sender
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
        seed ^= h(id.sender_comp_id) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
        seed ^= h(id.target_comp_id) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// ── ReaderSnapshot — immutable snapshot for public readers (E-7 / D-SNAP) ─────
//
// Holds an immutable point-in-time copy of the two control-plane maps that the
// synchronous public readers `lookup()` and `acceptor_bound_endpoint()` need.
// Published by the control strand after every mutation; `atomic_load`'d by readers
// from any thread without entering a strand, taking a lock, or blocking. [E-7/INV-9]
//
// Standard C++20 `std::atomic<std::shared_ptr<const ReaderSnapshot>>` — no
// `std::mutex` in our headers (Art. XV / [const §XV.9]). [D-SNAP / research D-SNAP]

namespace fixpp::session {

/// Immutable snapshot of the control-plane reader maps (E-7 / D-SNAP).
/// Built fresh and atomically published after every control-plane mutation
/// (registry insert/erase, listener/endpoint write, D-PUB publish/unpublish).
/// Never mutated in place — always replaced with a new allocation.
struct ReaderSnapshot {
    /// SessionId → owning shared_ptr<Session> (drawn from registry_).
    /// The shared_ptr keeps the Session alive across a concurrent registry_.clear()
    /// while the Engine is alive — the bounded keepalive (INV-9/FR-008/FR-014).
    std::unordered_map<SessionId, std::shared_ptr<Session>> sessions;

    /// SessionId → bound Endpoint (drawn from listener_endpoints_).
    /// Copied by value so the snapshot is completely self-contained.
    std::unordered_map<SessionId, fixpp::transport::Endpoint> endpoints;
};

}  // namespace fixpp::session

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
    /// shared_ptr (not unique_ptr) so Engine::send can capture a keepalive that
    /// outlives a posted closure on the session strand (prevents UAF when stop()
    /// races the post — T013 / [[feedback_detached_cospawn_write_not_in_join_counter]]).
    std::shared_ptr<Session> session;

    role session_role = role::initiator;

    /// Retained config for re-accept/reconnect + identity resolution.
    SessionConfig config;

    /// Per-session teardown handle (read-pump + session work).
    /// Distinct from the per-listener accept-scope domain (E-7).
    asio::cancellation_signal session_cancel;

    /// Non-owning pointer to the live transport once the role loop has attached
    /// it (acceptor) or connected it (initiator); null until then. Owned by the
    /// Session (accepted_transport_/reconnected_transport_). stop() closes it to
    /// unblock the read-pump's in-flight async_read_some — an idle established
    /// read has no peer EOF and total-cancel alone does not break the SSL read,
    /// so stop() must close the socket (the "closes transports" step in stop()'s
    /// contract). Valid until the registry is cleared (after join-before-clear).
    fixpp::transport::Transport* live_transport = nullptr;

    /// Per-session serialization domain (E-1/INV-1 — one strand per session).
    /// Created on the control strand in Engine::start() before each loop spawn.
    /// The whole role loop (establish/handshake/read-pump/callbacks/sends/both
    /// teardown closes) runs on this strand. NOT yet bound to the loop in this
    /// phase — US1 (T009/T010) binds the loop/Session/transport to it.
    /// INV-1: each session gets exactly one strand; never shared across sessions.
    /// nullopt until Engine::start() assigns it via emplace(make_strand(exec_)).
    /// Optional to allow default-construction of SessionEntry without a valid
    /// executor (asio::strand<any_io_executor> throws bad_executor on default-ctor).
    std::optional<asio::strand<asio::any_io_executor>> session_strand;
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
// Threading (023 two-domain design — MT-safe):
//   register_session() is called before start() (single-threaded by contract).
//   Control-plane mutations (registry/listeners/endpoints) are confined to the
//   control strand.  Per-session work (read-pump/handshake/teardown) is confined
//   to each session's own strand.  Public readers lookup() / acceptor_bound_endpoint()
//   are any-thread-safe via the atomic D-SNAP reader_snapshot_ (E-7/D-SNAP).
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

    Engine(Engine const&) = delete;
    Engine& operator=(Engine const&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    /// STRICT precondition: assert(stopped()).
    /// No synchronous best-effort teardown — callers must co_await stop() first.
    ~Engine();

    /// FR-002: register a session from its config; records config + role ONLY.
    /// Does NOT construct a Session (lazy construction inside spawned loops).
    /// Duplicate SessionId::from_config(cfg) → session_invalid_argument (119).
    /// Must be called before start() in 015.  NO Application& parameter (FR-013).
    [[nodiscard]] fixpp::core::expected_t<void> register_session(SessionConfig cfg);

    /// FR-006/007/013 (019-app-callbacks T013): any-thread outbound send.
    ///
    /// Looks up `id` in the registry; if found and the session is established
    /// (Active), posts onto the session's strand, runs `toApp` (from
    /// EngineConfig::application) to inspect/veto, then delegates to
    /// Session::send (durable-before-transmit path).
    ///
    /// Returns:
    ///   {}                              — sent successfully (AC1)
    ///   unexpected(app_do_not_send=129) — toApp veto (AC2/FR-007)
    ///   unexpected(app_callback_threw)  — toApp threw (FR-011)
    ///   unexpected(session_invalid_state_for_send=77)
    ///                                   — id registered but session not Active (AC4/FR-013)
    ///   unexpected(session_invalid_argument=119) — id not registered (FR-013)
    ///
    /// Admission gate: enrolls in send_counter_ before the first hop, then
    /// rechecks stopped_. A send that starts after stop() has already set
    /// stopped_=true fast-fails without posting any control-strand work, while
    /// a send that enrolled before stop()'s drain is waited out by stop().
    ///
    /// Any-thread safe: captures a shared_ptr keepalive from the registry
    /// that outlives the posted work (prevents UAF when stop() races the post).
    /// Re-entrant calls from on-strand callbacks are enqueued behind the
    /// current dispatch (asio::post is non-blocking → no deadlock).
    /// Backpressure = the awaited result ([const §XV.15]).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> send(
        SessionId const& id, std::span<const std::byte> app_payload);

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

    /// Registry addressing.  Returns nullptr (empty shared_ptr) if `id` is not
    /// registered, OR is registered but not yet established (e.g. acceptor with
    /// no peer yet, or a session whose loop has not yet reached open()) — null
    /// is NOT an error (Gate A New-3).
    ///
    /// T024 (FR-008 / SC-004 / D-SNAP): return type changed from Session* to
    /// std::shared_ptr<Session>.  The handle keeps the Session alive across a
    /// concurrent stop()/registry_.clear() WHILE THE ENGINE IS ALIVE (bounded
    /// handle — INV-9a).  Session borrows const EngineConfig& from the Engine;
    /// the caller MUST NOT let a handle outlive the Engine (~Engine asserts zero
    /// outstanding handles in DEBUG builds via lease_counter_ — T025/INV-9a).
    ///
    /// Reads the atomically-published reader_snapshot_ — any-thread-safe, no
    /// strand entry, no std::mutex, no block. [E-7/INV-9/D-SNAP/research D8]
    [[nodiscard]] std::shared_ptr<Session> lookup(SessionId const& id) const;

    [[nodiscard]] bool stopped() const noexcept;

    /// SC-010 delta #6: returns the OS-assigned bound port of the acceptor's
    /// listener for a given acceptor SessionId. Returns Endpoint{} (port==0)
    /// if the id is not a registered acceptor or the listener has not been
    /// built yet (i.e. before start() runs the accept loop).
    /// The accept loop builds the listener synchronously at entry; the endpoint
    /// is readable once the engine's executor runs at least one step after start().
    /// [data-model "Listener acquisition"; tasks.md SC-010 delta #6]
    [[nodiscard]] fixpp::transport::Endpoint acceptor_bound_endpoint(SessionId const& id) const;

    // 017 owned amendment #2 (contracts/adjacent-amendments.md §2 / [2k App D §D.2]).
    // Returns the engine-level static lifecycle trace_context (an atomic snapshot).
    // Used by FIXPP_ELOG callers:
    //   FIXPP_ELOG(logger, info, engine, cat::control, "msg {}", ...);
    // Backed by engine_trace_ctx_snapshot_ seeded at Engine construction from
    // EngineConfig::engine_trace_context. Thread-safe (seqlock/atomic).
    [[nodiscard]] fixpp::otel::trace_context engine_trace_context() const noexcept;

    // Returns the engine-level clock (EngineConfig::clock).
    // Used by FIXPP_ELOG to stamp record timestamps with the effective clock.
    // Never null post-construction (validate_engine_config rejects null clocks).
    [[nodiscard]] const std::shared_ptr<fixpp::core::Clock>& clock() const noexcept;

#ifdef FIXPP_TEST_HOOKS
    // gate-b/r1 #3 (V-12 seam): public setter for the pre-publish hook.
    // Set before calling start(); invoked by run_accept_loop on the session strand
    // between step 7 (attach_accepted_transport) and step 7a (publish_entry).
    // Production builds (FIXPP_TEST_HOOKS not defined) carry zero overhead.
    // [contracts C-6/V-12; gate-b/r1 #3]
    void set_pre_publish_hook(std::function<asio::awaitable<void>()> hook) {
        test_hook_pre_publish_ = std::move(hook);
    }

    // gate-b/r3 P1 witness seam: invoked by stop() on the control strand after
    // the send_counter_ drain has observed zero and before the registry clear.
    // Lets a test start a late Engine::send() in the post-drain window.
    void set_post_send_drain_hook(std::function<asio::awaitable<void>()> hook) {
        test_hook_post_send_drain_ = std::move(hook);
    }
#endif  // FIXPP_TEST_HOOKS

private:
    // Injected executor; all loops co_spawn on this.
    asio::any_io_executor exec_;

    // Engine-level shared config (dictionaries, clock, transport factory, …).
    // NO Application& (FR-013 / Gate A New-2).
    fixpp::core::EngineConfig engine_cfg_;

    // 017 owned amendment #2: engine-held trace_context snapshot seeded at
    // construction from EngineConfig::engine_trace_context ([2k App D §D.2]).
    // The helper TYPE (core::detail::trace_context_snapshot — seqlock/atomic
    // wrapper) is defined in engine_config.hpp:64.
    fixpp::core::detail::trace_context_snapshot engine_trace_ctx_snapshot_;

    // Session registry — keyed on SessionId, owned here (join-before-clear E-7).
    std::unordered_map<SessionId, SessionEntry> registry_;

    // Per-listener accept-scope cancellation signals (one per acceptor config).
    // Distinct from the per-session SessionEntry::session_cancel (E-7).
    std::unordered_map<SessionId, asio::cancellation_signal> accept_scope_signals_;

    // Per-acceptor listeners (SC-010 delta #6 / "Listener acquisition" in data-model).
    // Built by run_accept_loop from reconnect_endpoint (repurposed as bind endpoint).
    // Torn down by stop() via total-cancel of accept_scope_signals_.
    // Key matches the acceptor's SessionId in registry_.
    std::unordered_map<SessionId, std::unique_ptr<fixpp::transport::Listener>> listeners_;

    // Bound endpoints for each acceptor listener — populated once the listener
    // is successfully constructed in run_accept_loop. Used by acceptor_bound_endpoint().
    // Port 0 in the stored Endpoint means the listener is not yet built.
    std::unordered_map<SessionId, fixpp::transport::Endpoint> listener_endpoints_;

    // Engine control strand (E-0/D0/INV-0) — single serialization domain for ALL
    // engine-global state: registry_, listeners_, listener_endpoints_,
    // accept_scope_signals_, stopped_, send_counter_, outstanding_counter_, and
    // handle publication (D-PUB).
    // INV-0: distinct from every session strand (each session has its own
    //   SessionEntry::session_strand; the control strand is shared engine-global).
    // Constructed once over exec_ in Engine::Engine(). No routing through it yet
    // (that is US1 send / US2 control-plane confinement). [E-0/D0/C-0]
    asio::strand<asio::any_io_executor> control_strand_;

    // Stopped flag (E-6/D-STOP/FR-013) — ensures stop() is idempotent and dtor
    // assert fires correctly.
    // Memory-order discipline (INV-8):
    //   Authoritative WRITE: on the control strand in stop() — sequenced after
    //     all control-plane reads (registry, listeners, counters).
    //   READs at two sites: accept-loop gate (run_accept_loop while-guard) and
    //     send fast-fail (Engine::send Step B) — both use .load(acquire) so they
    //     observe the stop() write reliably on a multi-threaded executor.
    // Behavior-preserving on a single-threaded executor (015 scope). [E-6/D-STOP/C-5/FR-013]
    std::atomic<bool> stopped_ = false;

    // T012 friend: run_accept_loop (engine.cpp, namespace fixpp::session)
    // accesses Engine's private members (listeners_, listener_endpoints_, etc.)
    // and Session's public attach_accepted_transport (now public in session.hpp).
    // The coroutine is declared in the fixpp::session namespace (not anonymous)
    // so the friend declaration is valid per [dcl.friend].
    friend asio::awaitable<void> run_accept_loop(fixpp::core::EngineConfig const&, Engine&,
                                                 SessionId const&, SessionEntry&,
                                                 asio::cancellation_signal&,
                                                 std::shared_ptr<std::atomic<int>>);

    // T013 friend: run_connect_loop (engine.cpp, namespace fixpp::session)
    // T013 added Engine& parameter for control_strand_ + stopped_ access (D-PUB).
    // Declared in fixpp::session namespace (not anonymous, not static) per [dcl.friend].
    friend asio::awaitable<void> run_connect_loop(fixpp::core::EngineConfig const&, Engine&,
                                                  SessionEntry&, std::shared_ptr<std::atomic<int>>);

    // T023 (E-7/D-SNAP): internal snapshot republisher — called on the control strand
    // (or pre-start single-thread) after every control-plane mutation. Builds a fresh
    // ReaderSnapshot from registry_+listener_endpoints_ and atomically stores it.
    void publish_reader_snapshot_unlocked_();

    // (015 /simplify R-3: the rebindable outbound send-slot lives entirely inside
    // each Session — transport_send_ is rebound by attach_accepted_transport /
    // install_reconnected_transport to the live Transport::async_write. The engine
    // holds no per-session send map; an earlier `send_slots_` member was vestigial
    // and removed.)

    // Joint-before-clear counter (E-7 / Gate A New-4): tracks how many
    // co_spawned loops are still running. start() initialises this and
    // each loop decrements on exit. stop() waits until it reaches zero
    // before clearing the registry that owns the Session objects.
    // The shared_ptr allows loops to safely decrement after co_return even
    // if Engine::stop() is executing concurrently.
    // Strictly required by the join-before-clear invariant — private only.
    std::shared_ptr<std::atomic<int>> outstanding_counter_;

    // FIX-2 (gate-b/r1): in-flight send counter. Engine::send bumps this
    // at entry (on exec_, after the stopping_ check) and decrements on
    // co_return. stop() drains this BEFORE registry_.clear() so no send
    // coroutine can dereference engine_ (via session's engine_ ref) after
    // Engine::~Engine() runs. Separate from outstanding_counter_ (loop
    // counter) so the two domains can be drained independently.
    std::shared_ptr<std::atomic<int>> send_counter_;

    // T023 (E-7 / D-SNAP / INV-9): atomic immutable snapshot for the synchronous
    // public readers lookup() and acceptor_bound_endpoint().
    //
    // Published (via atomic_store/store(release)) on the control strand after
    // EVERY control-plane mutation: registry inserts/erases (register_session,
    // publish_entry, unpublish_entry, stop() clear), listener/endpoint writes
    // (run_accept_loop map write), and stop()'s clears.
    //
    // Read (via atomic_load/load(acquire)) by the public readers from any thread
    // without entering a strand, taking a lock, or blocking. [E-7/INV-9/D-SNAP]
    //
    // Standard C++20 std::atomic<shared_ptr<>> — NO std::mutex in our headers
    // (Art. XV / [const §XV.9]). Not lock-free on all STLs (measured by V-6
    // perf gate), but correctness does not depend on lock-freedom. [D-SNAP]
    //
    // Initialized to a non-null empty Snapshot in the ctor so readers never
    // load null even before start() has been called. [E-7 invariant]
    std::atomic<std::shared_ptr<const ReaderSnapshot>> reader_snapshot_;

    // gate-b/r1 #3 (V-12 seam): awaitable hook invoked by run_accept_loop on the
    // session strand BETWEEN step 7 (attach_accepted_transport) and step 7a
    // (publish_entry co_spawn).  The hook is always compiled in (so engine.cpp,
    // which does not get FIXPP_TEST_HOOKS, can always check it) but is always null
    // in production (no production code path calls set_pre_publish_hook, which is
    // guarded by FIXPP_TEST_HOOKS).  Overhead = one null function<> check per
    // accepted connection: zero in practice. [contracts C-6/V-12; gate-b/r1 #3]
    std::function<asio::awaitable<void>()> test_hook_pre_publish_;  // null unless test sets it

    // gate-b/r3 P1 (post-drain seam): awaitable hook invoked by stop() on the
    // control strand AFTER the send_counter_ drain has observed zero and BEFORE
    // step-4 session close / step-5 registry clear. Always compiled in; null in
    // production because only FIXPP_TEST_HOOKS builds can install it.
    std::function<asio::awaitable<void>()> test_hook_post_send_drain_;

#ifndef NDEBUG
    // T025 (INV-9a / FR-014 / R7): debug-only outstanding-lease counter.
    //
    // In DEBUG builds, lookup() returns an aliasing shared_ptr<Session> backed
    // by a small Lease control block that:
    //   ctor: increments this counter (new handle issued)
    //   dtor (last copy): decrements this counter (handle released)
    // ~Engine() debug-asserts this counter is zero (all handles returned before
    // engine destruction).
    //
    // STRICTLY SEPARATE from send_counter_ (R7):
    //   - send_counter_ is a hard runtime barrier drained by stop() (prevents UAF).
    //   - lease_counter_ is a debug assert + caller obligation ONLY.
    //   - NEVER drives a stop() drain (draining on app-held leases would hang stop()).
    //
    // Release builds carry NO counter — zero runtime overhead. [R7]
    // `mutable`: lookup() is logically const (a snapshot read) but increments this
    // debug-only instrumentation counter — the canonical use of mutable (avoids a
    // const_cast in the const lookup()).
    mutable std::atomic<std::uint64_t> lease_counter_{0};
#endif
};

}  // namespace fixpp::session
