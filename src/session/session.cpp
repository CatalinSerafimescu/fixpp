// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/session.cpp
//
// fixpp::session::Session — minimal real skeleton out-of-line impl (D-4 /
// E10). Phase 2 (T012) ships the ctor + the never-null session_arena()
// resolution chain + linkable open()/close() placeholders. The 2d-owned
// BEHAVIOUR is wired per user story (T020/T030/T037/T038/T039/T045/T050) —
// each replaces the marked placeholder body, it is not additive guesswork.
#include <algorithm>
#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/async_result.hpp>  // NOLINT(misc-include-cleaner) — IWYU: async_initiate via use_awaitable
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>  // NOLINT(misc-include-cleaner) — asio::co_spawn used at session.cpp:916 (cancellable_dispatch fan-out); clang-tidy doesn't see the use through templates
#include <asio/detached.hpp>
#include <asio/error.hpp>  // asio::error::operation_aborted — F5 noexcept-throw absorption
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <charconv>
#include <chrono>
#include <compare>  // NOLINT(misc-include-cleaner) — IWYU: strong_ordering/operator> via chrono spaceship
#include <coroutine>  // NOLINT(misc-include-cleaner) — IWYU: coroutine_handle via awaitable machinery
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fixpp/core/clock.hpp>  // Clock::steady_now / sleep_until
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>     // expected_t, error values
#include <fixpp/core/fix_time.hpp>  // 005 US5: fix_string_to_utc_time (T055/T056)
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/session_local.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/admin_messages.hpp>         // 005 US1: interpret_logon / T046: build_logout
#include <fixpp/session/direction.hpp>              // 005 US4: direction_t (store outbound)
#include <fixpp/session/message_store.hpp>          // 008-message-store — store_ unique_ptr dtor
#include <fixpp/session/message_store_factory.hpp>  // 008-message-store — make() call site
#include <fixpp/session/retrieve_visitor.hpp>       // 013 FR-010/FR-012: resend store-walk visitor
#include <fixpp/session/security_profile.hpp>  // SecurityProfile::kind::unset sentinel check (lives in `session` per [arch §6 line 243])
#include <fixpp/session/sending_time.hpp>  // 005 US5: check_sending_time (T055)
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>  // 005 US2: SeqnumManager (T031)
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>  // 013 T036: SessionEvent variants
#include <fixpp/session/session_fsm.hpp>    // 005 US1: fsm_state enum (T023–T025)
#include <fixpp/transport/transport_factory.hpp>  // cfg_.transport_factory_override deref (reconnect_fsm.hpp now fwd-decls it per [const §XV.9])
#include <fixpp/wire/writer.hpp>  // 013 FR-010: replay-frame re-serialization
// 014 T015: handshake_result full definition needed for install_reconnected_transport.
// session.cpp is in the session layer; transport is an allowed dependency ([arch §5]).
#include <fixpp/transport/tls_transport.hpp>

#include "msgtype_classifier.hpp"  // 019 T006: is_admin_msgtype (session-internal)
// 019 T011: Application callback dispatch (inbound). Include here (session.cpp
// only) to avoid pulling wire/parser.hpp into the awaitable-corpus headers.
// session → wire is ALLOWED per [arch §5.3] / check_layers.py.
#include <fixpp/session/application.hpp>  // Application::fromAdmin / fromApp
#include <fixpp/session/engine.hpp>       // SessionId::from_config
#include <fixpp/wire/parser.hpp>          // wire::Parser<Index>, wire::MessageView<Index>
// NOTE: fixpp/tls/peer_identity.hpp is transitively available via session_config.hpp
// → compid_authorization_policy.hpp → peer_identity.hpp. A direct include from
// session.cpp would violate [arch §2.3] session→tls edge (check_layers.py).
// We rely on the transitive include to access fixpp::tls::peer_identity.
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace fixpp::session {

namespace {
// [2d §4.5] never-null resolution chain: SessionConfig::session_arena ?:
// EngineConfig::default_session_resource ?: std::pmr::get_default_resource().
std::pmr::memory_resource* resolve_session_arena(const fixpp::core::EngineConfig& engine,
                                                 const SessionConfig& cfg) noexcept {
    if (cfg.session_arena != nullptr) {
        return cfg.session_arena;
    }
    if (engine.default_session_resource != nullptr) {
        return engine.default_session_resource;
    }
    return std::pmr::get_default_resource();
}

// 016 T008 — resolve the per-session reconnect policy. An operator-supplied policy
// wins; otherwise default to the QuickFIX-compat shape (single 30 s interval,
// unbounded) which has a NON-ZERO backoff — replacing the prior hard-coded empty
// ReconnectPolicy{} whose 0-backoff schedule busy-spun on repeated connect failure
// (015 down-peer L2 carry-forward). The arena allocates the schedule vector. [FR-004]
fixpp::transport::ReconnectPolicy resolve_reconnect_policy(const SessionConfig& cfg,
                                                           std::pmr::memory_resource* arena) {
    if (cfg.reconnect_policy.has_value()) {
        return *cfg.reconnect_policy;
    }
    return fixpp::transport::ReconnectPolicy::defaults_quickfix_compat(arena);
}
}  // namespace

Session::Session(const fixpp::core::EngineConfig& engine, const SessionConfig& cfg)
    : engine_(engine),
      cfg_(cfg),
      session_arena_(resolve_session_arena(engine, cfg)),
      reconnect_fsm_(
          cfg.transport_factory_override.get(),  // non-owning raw ptr; factory owned by cfg_
          resolve_reconnect_policy(cfg, session_arena_),  // 016 T008: was empty
                                                          // ReconnectPolicy{} (busy-spin)
          cfg.heartbeat_interval.value_or(std::chrono::seconds{30}),
          std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms}) {
    // Resolution chain always terminates at std::pmr::get_default_resource()
    // (never null), so I-18's never-null contract holds for the lifetime.
    // reconnect_fsm_ owns AwaitingResend state (FR-009 per data-model §E-1).
}

// D-23: release any per-session Clock state (system_clock_source's reusable
// timer-slot pool keyed by Session*, etc.) BEFORE the session_arena memory
// is reclaimed by member destruction. Idempotent — the default Clock hook
// is a no-op for clocks without per-session state. effective_clock_ is
// nullptr until open() resolves it (state_ == never_opened); after a
// successful open() it remains non-null until destruction (the Clock is
// shared_ptr-owned by EngineConfig and outlives the session — [2d §4.1]).
Session::~Session() {
    if (effective_clock_) {
        effective_clock_->forget_session(this);
    }
}

std::pmr::memory_resource* Session::session_arena() const noexcept {
    return session_arena_;  // I-18: frozen at ctor, never null, never swapped
}

// ── FR-004 / D-2 — FSM transition ring-buffer helpers ────────────────────
// record_state_transition_: write new_state into the 16-slot ring and advance
// fsm_state_. The write index (fsm_visit_write_idx_) is a separate uint32 that
// always advances; the public count (fsm_visit_count_) saturates at UINT8_MAX
// to signal "≥255 transitions" without freezing the ring rotation.
void Session::record_state_transition_(fsm_state new_state) noexcept {
    const bool was_active = (fsm_state_ == fsm_state::Active);

    fsm_visit_history_[fsm_visit_write_idx_++ % 16] = new_state;
    if (fsm_visit_count_ < std::numeric_limits<std::uint8_t>::max()) {
        ++fsm_visit_count_;
    }
    fsm_state_ = new_state;

    // ── 019 T016: lifecycle callbacks pinned to the Active↔!Active edge ───────
    // Fired here so ALL converging paths (graceful close / terminal close /
    // callback-threw) share one guard, satisfying INV-7 exactly-once semantics.
    // record_state_transition_ is noexcept; invoke_callback_safe catches any
    // throw and stores it in lifecycle_cb_threw_. Callers that transition TO
    // Active must check lifecycle_cb_threw_ and call close(terminal) if set.
    // [019-app-callbacks T016; FR-009; data-model.md INV-7; research D3]
    if (engine_.application == nullptr) return;

    if (new_state == fsm_state::Active && !onLogon_fired_) {
        onLogon_fired_ = true;
        const SessionId sid = SessionId::from_config(cfg_);
        callback_dispatch_scope cs{*this};
        auto r = invoke_callback_safe([&]() { engine_.application->onLogon(sid); });
        (void)cs;
        if (!r) lifecycle_cb_threw_ = true;
    } else if (was_active && new_state != fsm_state::Active && !onLogout_fired_) {
        // Fire onLogout on ANY Active→!Active transition:
        //   Active → LogoutSent  (graceful close phase-1 begin)
        //   Active → Disconnected (terminal close or fatal)
        // The fire-once guard (onLogout_fired_) prevents double-fire across all
        // converging paths (graceful + terminal + callback-threw). [INV-7; FR-009]
        onLogout_fired_ = true;
        const SessionId sid = SessionId::from_config(cfg_);
        callback_dispatch_scope cs{*this};
        auto r = invoke_callback_safe([&]() { engine_.application->onLogout(sid); });
        (void)cs;
        // A throw from onLogout is absorbed: the session is already leaving Active.
        // No additional close(terminal) needed. app_callback_threw is silently
        // noted; the fire-once guard (onLogout_fired_) prevents re-entry.
        (void)r;
    }
}

// fsm_visit_history: membership-witness view over the last ≤16 recorded
// transitions (physical-buffer order; NOT chronologically meaningful — see header).
std::span<const fsm_state> Session::fsm_visit_history() const noexcept {
    return std::span<const fsm_state>{fsm_visit_history_.data(),
                                      std::min<std::size_t>(fsm_visit_count_, 16)};
}

// ── 013 FR-035 — SessionEvent ring-buffer helpers ────────────────────────
// emit_event: write ev into the kSessionEventRingCapacity-slot ring on the
// per-session strand ([const §XI.4]). Body wired fully in Phase 5 T040;
// this stub is link-green for Phase 2/3/4. [data-model §E-6]
void Session::emit_event(SessionEvent ev) noexcept {
    recent_events_[events_write_idx_++ % kSessionEventRingCapacity] = ev;
    if (events_count_ < kSessionEventRingCapacity) {
        ++events_count_;
    }
}

// ── parse_and_dispatch_ ───────────────────────────────────────────────────────
//
// Shared parse-and-callback ritual extracted from 5 inbound + 1 outbound sites:
//   - fire_to_admin_           (toAdmin; 8192-byte arena — admin frames are small)
//   - fromAdmin SequenceReset  (16384-byte arena — inbound frames may be larger)
//   - fromAdmin Logout         (16384-byte arena)
//   - fromAdmin generic        (16384-byte arena)
//   - fromApp                  (16384-byte arena — app payloads can be large)
//   - toApp in send_impl       (16384-byte arena)
//
// Arena sizing: two named constants document the intentional difference.
//   kAdminParseArena  = 8192: admin messages (Heartbeat/Logon/TestRequest/…) have a
//     bounded small field-set; 8 KiB is always sufficient.
//   kInboundParseArena = 16384: inbound/app frames may carry arbitrary payload; 16 KiB
//     provides headroom for larger messages without heap fallback.
//
// On parse failure (Framer or Parser): returns expected_t<void>{} — skip callback,
// not fatal.  This matches every call site's existing disposition.
// [const §VIII.5] (stack-only — no heap between parse and callback)
// [019-app-callbacks T011/T013/T014/T016]

namespace {
constexpr std::size_t kAdminParseArena = 8192;     // admin frames: bounded small
constexpr std::size_t kInboundParseArena = 16384;  // inbound/app: larger payloads
}  // namespace

template <class CB>
[[nodiscard]] fixpp::core::expected_t<void> Session::parse_and_dispatch_(
    std::span<const std::byte> frame, std::size_t arena_bytes, CB&& cb) noexcept {
    // Stack parse arena ([const §VIII.5] — no heap).
    // arena_bytes is caller-supplied so the size choice is explicit at each site.
    std::array<std::byte, kInboundParseArena> pa_buf_storage{};
    // We always allocate the max stack size but hand the requested slice to the MBR.
    // Both constants fit; static_assert guards this.
    static_assert(kAdminParseArena <= kInboundParseArena);
    std::pmr::monotonic_buffer_resource pa_mr{pa_buf_storage.data(), arena_bytes,
                                              std::pmr::null_memory_resource()};
    std::array<std::byte, 512> carry_store{};
    std::pmr::monotonic_buffer_resource carry_mr{carry_store.data(), carry_store.size(),
                                                 std::pmr::null_memory_resource()};
    fixpp::wire::pmr_carry_buffer carry{carry_store.size(), &carry_mr};
    fixpp::wire::Framer pd_framer;
    std::array<fixpp::wire::frame_view, 1> pd_out{};
    auto feed_r = pd_framer.feed(frame, carry, std::span<fixpp::wire::frame_view>{pd_out});
    if (!feed_r || feed_r->empty()) return fixpp::core::expected_t<void>{};  // parse error — skip

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> pd_parser;
    auto mv_r = pd_parser.parse((*feed_r)[0], &pa_mr);
    if (!mv_r) return fixpp::core::expected_t<void>{};  // parse error — skip

    const SessionId sid = SessionId::from_config(cfg_);
    callback_dispatch_scope cs{*this};
    auto result = invoke_callback_safe([&]() { return std::forward<CB>(cb)(*mv_r, sid); });
    (void)cs;
    return result;
}

// ── 019 T014 — fire_to_admin_ ─────────────────────────────────────────────────
//
// Called before store_then_emit at every engine-originated admin emit site.
// Parses the built frame and invokes engine_.application->toAdmin() if Application
// is registered. Admin is inspect-only (not vetoable). Returns false only if the
// callback threw (caller must terminal-close + return an error).
//
// Stack-local parse arena ([const §VIII.5] — no heap).
// Called on the engine executor (exec_) under single-thread confinement
// (015 E-5), NOT on an engaged per-session strand — see L-019-3 + INV-2.
// [FR-008/010]
// [019-app-callbacks T014; FR-008/010; research D3]
bool Session::fire_to_admin_(std::span<const std::byte> frame) noexcept {
    if (engine_.application == nullptr) return true;
    // kAdminParseArena: admin frames (Heartbeat/Logon/TestRequest/…) have a
    // bounded small field-set; 8 KiB is always sufficient.
    auto cb_r = parse_and_dispatch_(frame, kAdminParseArena, [&](auto& mv, auto& sid) {
        engine_.application->toAdmin(mv, sid);
    });
    // Only possible error: app_callback_threw (FR-011).
    // Caller must terminal-close + return error.
    return cb_r.has_value();
}

// recent_events: membership-witness view over the last ≤16 emitted SessionEvents
// (physical-buffer order; NOT chronologically meaningful). [data-model §E-6]
std::span<const SessionEvent> Session::recent_events() const noexcept {
    return std::span<const SessionEvent>{recent_events_.data(),
                                         std::min(events_count_, kSessionEventRingCapacity)};
}

// ── 013 T044 — FR-030 / D-11 — Operator-facing credential rotation forwarder ──
//
// Pure forwarder: validates nullptr + factory-present, then delegates to
// cfg_.transport_factory_override->reload_credentials(new_source).
// Session is forwarder-only — no direct atomic-swap per
// [[feedback_half_restructure_symmetric_api]] (factory IS the symmetric authority
// for both initiator and acceptor rotation paths).
//
// session_event_credentials_rotated emission is DEFERRED to 014:
//   The event must fire BEFORE the first handshake on the rotated cert_source
//   (data-model E-7) and must carry the real cert SHA-256 fingerprint (old + new),
//   which is only available inside the async load_credentials() path. Both the
//   correct emit-site (drive_reconnect_attempt, before TransportFactory::make)
//   and the fingerprint computation require the live-transport lifecycle.
//   ReconnectFsm::drive_reconnect_attempt() is a stub in 013; wiring lands in
//   the 014 transport-active / interop slice.
//   [[project_013_carryforwards_to_014]] / data-model §E-7 / FR-032.
//
// [FR-030 / FR-033 / US4 AC1+AC2 / D-11]
fixpp::core::expected_t<void> Session::reload_credentials(
    std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept {
    if (!new_source) {
        return std::unexpected{fixpp::core::error::session_invalid_argument};
    }
    if (!cfg_.transport_factory_override) {
        return std::unexpected{fixpp::core::error::session_invalid_argument};
    }
    return cfg_.transport_factory_override->reload_credentials(std::move(new_source));
}

// ── 014 T010/T015 — Session::install_reconnected_transport ───────────────────
//
// Called by ReconnectFsm::drive_reconnect_attempt() on a successful attempt
// (step 8 of data-model E-1). Performs the cross-object handoff:
//   1. Store the live peer identity (hr.peer_id) as live_peer_id_ so the
//      LogonSent→Active Logon-ack guard can use arm (1-live) (E-2 / T015).
//   2. Take ownership of the live transport (reconnected_transport_).
//   3. Re-enter LogonSent so Session::on_inbound_frame drives the session
//      back to Active when the peer Logon-ack arrives.
//
// §XV.9: handshake_result is forward-declared in session.hpp; the by-value
// parameter here is fine because this function is defined in session.cpp
// which already #includes "fixpp/transport/tls_transport.hpp" (the full
// definition). The session.hpp declaration uses the forward-declaration to
// avoid dragging std::shared_mutex into the awaitable closure.
//
// noexcept: move + optional-assign + record_state_transition_ are all
// non-throwing. [data-model §E-1 step 8; E-2; contracts C1; C2; FR-001; FR-006]
void Session::install_reconnected_transport(std::unique_ptr<fixpp::transport::Transport> transport,
                                            fixpp::transport::handshake_result hr) noexcept {
    // 1. Store live peer identity for arm (1-live) in the Logon-ack guard.
    //    The peer_id is moved out of hr (hr.peer_id is owning-by-value per
    //    tls_transport.hpp:52-53). [data-model §E-2; contracts C2; FR-006]
    live_peer_id_ = std::move(hr.peer_id);

    // 2. Take ownership of the live transport.
    reconnected_transport_ = std::move(transport);

    // 2a. FQ-A (gate-b/r2): live writes now go through live_write_serialized_()
    //     which reads live_transport_shared_() at call time. No transport_send_
    //     rebind needed for the live path — the live accessor picks up the new
    //     reconnected_transport_ automatically. transport_send_ continues to serve
    //     the pre-live/config-time test path (cfg_.transport_send set at open()).
    //     [data-model §E-1a; T016(c); FR-003; FQ-A gate-b/r2]

    // 3. Re-enter LogonSent so on_inbound_frame drives back to Active.
    //    The session's next peer Logon-ack will be processed by the LogonSent
    //    row of the FSM matrix, transitioning back to Active.
    //    [data-model §E-1 step 8; FR-001; US1 AC1]
    record_state_transition_(fsm_state::LogonSent);
}

// 015 T016(b) — public engine connect-loop driver (SC-010 (7)).
// Thin awaitable over the private reconnect_fsm_.drive_reconnect_attempt(), with
// the post-connect Logon emission folded in. On a successful attempt,
// install_reconnected_transport (called inside drive_reconnect_attempt step 8)
// has already rebound transport_send_ to the live sink (T016(c)) and re-entered
// LogonSent; we then emit the initial Logon over that live sink (connect-then-
// Logon, FR-003 / E-1a). emit_initiator_logon_ handles its own Disconnected-on-
// failure disposition. [data-model §E-1a; T016(b); FR-003/FR-004]
asio::awaitable<fixpp::core::expected_t<void>> Session::drive_reconnect() noexcept {
    auto drive_r = co_await reconnect_fsm_.drive_reconnect_attempt();
    if (!drive_r.has_value()) {
        co_return std::unexpected(drive_r.error());
    }
    // Transport is live + LogonSent (install_reconnected_transport). Emit the
    // initial Logon POST-connect over the now-live transport_send_.
    co_return co_await emit_initiator_logon_();
}

// 015 T016(b) — live-transport accessor for the read-pump (SC-010 (8)).
// reconnected_transport_ (initiator) or accepted_transport_ (acceptor). The
// engine only calls this after a successful install, so exactly one is non-null.
fixpp::transport::Transport& Session::live_transport() noexcept {
    return reconnected_transport_ ? *reconnected_transport_ : *accepted_transport_;
}

// FQ-A (gate-b/r2): returns the live transport as a shared_ptr<Transport>.
// The shared_ptr keepalive ensures the Transport is not freed by
// registry_.clear() while a write is in-flight (restores Q-1 UAF fix).
// Returns nullptr if no live transport is attached yet.
std::shared_ptr<fixpp::transport::Transport> Session::live_transport_shared_() const noexcept {
    if (reconnected_transport_) return reconnected_transport_;
    if (accepted_transport_) return accepted_transport_;
    return nullptr;
}

// FQ-A (gate-b/r2): one serialized live write.
// Acquires write_gate_ so at most one async_write is ever in-flight on the
// live Transport (satisfies transport.hpp:47-50 ≤1-in-flight contract).
// Holds a shared_ptr<Transport> keepalive across the co_await so the
// transport cannot be freed mid-write (restores Q-1 keepalive).
// Releases the gate on completion (success or error) via RAII.
// Returns dispatch_aborted if:
//   - The gate acquire is cancelled (operation_aborted from cancel_and_drain
//     during Session::close()) — converted per
//     [feedback_async_mutex_us3_asio_cancel_and_subagent_seams].
//   - async_write returns !has_value() (any transport error).
// If no live transport is present, returns ok (no-op; pre-live path).
// NEVER holds the gate across any read (write-submit→complete window only).
// [transport.hpp:47-50; FQ-A D-6; gate-b/r2]
asio::awaitable<fixpp::core::expected_t<void>> Session::live_write_serialized_(
    std::span<const std::byte> frame) noexcept {
    auto live = live_transport_shared_();
    if (!live) {
        // No live transport — pre-live path, no-op.
        co_return fixpp::core::expected_t<void>{};
    }

    // Acquire the write gate across the completion (not just up to suspension).
    // Wrap in try/catch to convert asio's thrown operation_aborted (from
    // cancel_and_drain or root cancel propagating through the awaitable) into
    // the contract's expected_t<void> return.
    // [feedback_async_mutex_us3_asio_cancel_and_subagent_seams]
    fixpp::sync::async_lock_guard guard;
    try {
        auto lock_r = co_await write_gate_.async_lock();
        if (!lock_r.has_value()) {
            // sync_lock_drained or sync_lock_aborted — gate closed/cancelled.
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
        guard = std::move(*lock_r);
    } catch (const asio::system_error& e) {
        if (e.code() == asio::error::operation_aborted) {
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
        co_return std::unexpected(fixpp::core::error::dispatch_aborted);
    }

    // Gate held — at most one async_write in-flight. `live` shared_ptr is the
    // keepalive so the transport cannot be freed while we are suspended here.
    fixpp::core::expected_t<std::size_t> write_r;
    try {
        write_r = co_await live->async_write(frame);
    } catch (const asio::system_error& e) {
        if (e.code() == asio::error::operation_aborted) {
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
        co_return std::unexpected(fixpp::core::error::dispatch_aborted);
    }
    // guard destructor releases the gate when we leave this scope.
    if (!write_r.has_value()) {
        co_return std::unexpected(fixpp::core::error::dispatch_aborted);
    }
    co_return fixpp::core::expected_t<void>{};
}

// 015 T011 — Acceptor attach primitive.
// Called by run_accept_loop STRICTLY-BEFORE the first on_inbound_frame (E-4).
// Two actions (distinct from install_reconnected_transport):
//   1. Store live peer identity for arm (1-live) at the acceptor gate (:1048).
//   2. Take ownership of the transport.
// Does NOT rebind transport_send_ — live writes go through live_write_serialized_()
// which reads live_transport_shared_() at call time (FQ-A gate-b/r2).
// Does NOT transition the FSM — the acceptor stays NotConnected; the gate at
// :1048 fires when on_inbound_frame processes the first Logon.
// [data-model §E-2; T011; FR-005/006/008; contracts C1 step 5; T-041; FQ-A]
void Session::attach_accepted_transport(std::unique_ptr<fixpp::transport::Transport> transport,
                                        fixpp::transport::handshake_result hr) noexcept {
    // 1. Store live peer identity for the acceptor authorization gate (E-4).
    //    Consumed one-shot by the gate at :1048 in on_inbound_frame.
    live_peer_id_ = std::move(hr.peer_id);

    // 2. Take ownership of the transport.
    // FQ-A: no transport_send_ rebind needed — live_write_serialized_() picks
    // up accepted_transport_ via live_transport_shared_() at call time.
    accepted_transport_ = std::move(transport);
    // FSM NOT advanced — the NotConnected→LogonReceived transition fires at
    // the acceptor gate (:1048) when the first inbound Logon is processed.
}

// 024 T003 — shared durable-reset helper.
// Body: co_await seqnum_mgr_.reset_to_one() then co_await store_->reset().
// Disposition keys store-failure handling on the trigger CAUSE (not location):
//   fatal  → a store failure propagates → caller can block reaching Active (C2.6
//             knob-driven Logon path).
//   logged → store failure swallowed (I-07 logged-then-proceed; matching the
//             existing :1589-1592 inline pattern for the 013-only 141 path and
//             all teardown paths).
// seqnum_mgr_.reset_to_one() failure always propagates regardless of disposition
// (the live-counter reset is the primary gate; a store failure is I-07-able but
// a seqnum-manager failure is not).
// store_ is null-checked to match the existing :1589 null-guard pattern.
// NOT wired to any trigger in this slice (T003 foundational only); wired in
// T007 (initiator Logon), T008 (acceptor Logon), T014 (teardown).
// [024 data-model §"Durable reset helper"; C2.6; research D2]
asio::awaitable<fixpp::core::expected_t<void>> Session::reset_seqnums_to_one_durable(
    reset_disposition disposition) noexcept {
    // Step 1: reset the live seqnum counters to {1, 1}.
    // On failure: propagate regardless of disposition (live-counter reset is
    // the primary gate — if it fails the in-memory state is unknown).
    auto rst_r = co_await seqnum_mgr_.reset_to_one();
    if (!rst_r) {
        co_return std::unexpected(rst_r.error());
    }

    // Step 2: persist the reset via store_->reset().
    // Disposition controls failure handling:
    //   fatal  → propagate the error to the caller.
    //   logged → swallow (I-07): the session can still proceed; a subsequent
    //            open will re-observe stale counters from the store (acceptable
    //            per the logged-then-proceed policy for teardown + 013 paths).
    if (store_) {
        auto store_rst_r = co_await (*store_).reset();
        if (!store_rst_r) {
            if (disposition == reset_disposition::fatal) {
                co_return std::unexpected(store_rst_r.error());
            }
            // logged: (void) — store_io_failure → logged-then-proceed (I-07).
            (void)store_rst_r;
        }
    }

    co_return fixpp::core::expected_t<void>{};
}

// 015 T016(d) — initiator Logon emission, extracted from open()'s initiator arm.
// Two call sites: open() (per-session-direct, AT open) and drive_reconnect()
// (engine lazy-connect, POST-connect). The build/seqnum/store-emit sequence and
// its Disconnected-on-failure disposition are UNCHANGED from the original open()
// body — this is a behavior-preserving extraction. The caller owns the LogonSent
// transition (open() before the call; install_reconnected_transport before
// drive_reconnect's call). [data-model §E-1a; T016(d); FR-003/FR-004]
asio::awaitable<fixpp::core::expected_t<void>> Session::emit_initiator_logon_() noexcept {
    // Emit the initial Logon via build_logon + store_then_emit.
    // This is the SAME admin-builder emission path used by other admin
    // frames (build_heartbeat, build_test_request, etc.) — NOT the
    // Session::send() path which is for opaque application payloads.
    // [spec.md FR-004 analyze findings B1 + E1; data-model.md §E1]
    // stamp_sending_time internal helper is defined after open() in this TU;
    // use the public two-arg form from sending_time.hpp with a local buffer.

    // 024 T007: reset_on_logon — durable reset here (the shared emission point)
    // so BOTH open() (per-session-direct, engine_managed=false) AND drive_reconnect()
    // (engine lazy-connect and reconnect) reset symmetrically before building the Logon.
    // fatal disposition: a store failure propagates → record Disconnected + return error.
    // peek_outbound() below samples the post-reset seqnum=1 (analyze B1).
    // [[feedback_half_restructure_symmetric_api]]: fix at the shared point, not two copies.
    // [024 data-model initiator row; C2.1; C2.6; plan.md Complexity Tracking row 1]
    if (cfg_.reset_on_logon) {
        auto rst_r = co_await reset_seqnums_to_one_durable(reset_disposition::fatal);
        if (!rst_r) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(rst_r.error());
        }
    }

    std::array<std::byte, 256> logon_buf{};
    std::array<char, 32> time_buf{};
    std::string_view sending_time_view;
    if (effective_clock_) {
        auto fmt_r =
            fixpp::session::stamp_sending_time(effective_clock_->now(), cfg_.sending_time_precision,
                                               std::span<char>{time_buf.data(), time_buf.size()});
        if (fmt_r) {
            sending_time_view = std::string_view{fmt_r->data(), fmt_r->size()};
        }
    }
    const int heartbt_sec = cfg_.heartbeat_interval.has_value()
                                ? static_cast<int>(cfg_.heartbeat_interval->count())
                                : 30;  // D-8 default 30 s
    // F6+F7 (Round-A drift): peek seqnum first; only advance on success of
    // BOTH build_logon AND store_then_emit. Prevents seqnum hole when
    // build_logon fails (buffer overflow). [spec.md FR-001(e); F6/F7 drift fix]
    // RC#A (gate-b/r1-green): use seqnum_mgr_.peek_outbound() (not bare field).
    const seqnum_t logon_seq = seqnum_mgr_.peek_outbound();  // peek via manager
    // RC#C (gate-b/r1): bilateral_strict → send 141=Y in our outbound Logon.
    // 024 T007: extend to OR-of-three predicate: also emit 141=Y when any reset knob
    // is on AND seqnums are {1,1} post-reset (QFcpp shouldSendReset / QFJ isResetNeeded).
    // The {1,1} guard is evaluated against the LIVE post-reset manager state (peek_outbound()
    // + next_inbound_unsafe()) — always after reset_seqnums_to_one_durable() above, so the
    // sample is post-reset for both the open() and drive_reconnect() call sites.
    // [spec.md FR-017; Clarifications Q1=A; 024 data-model OR-of-three predicate; C2.1]
    const bool any_reset_knob =
        cfg_.reset_on_logon || cfg_.reset_on_logout || cfg_.reset_on_disconnect;
    // logon_seq already holds peek_outbound() (sampled just above on this strand, no
    // suspension between) — reuse it rather than re-querying the manager.
    const bool seqnums_at_one =
        (logon_seq == seqnum_min && seqnum_mgr_.next_inbound_unsafe() == seqnum_min);
    const bool initr_reset_seqnum =
        (cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict) ||
        (any_reset_knob && seqnums_at_one);
    // 027 T013 I-NEX-1: advertise next_inbound_unsafe() when knob is on (plain, NO +1).
    // Absent (nullopt) when knob is off ⇒ byte-identical baseline. [contract C2, I-NEX-7]
    const std::optional<fixpp::session::seqnum_t> initr_next_expected =
        cfg_.enable_next_expected_msg_seq_num
            ? std::optional<fixpp::session::seqnum_t>{seqnum_mgr_.next_inbound_unsafe()}
            : std::nullopt;
    auto logon_result = fixpp::session::build_logon(
        std::span<std::byte>{logon_buf.data(), logon_buf.size()}, logon_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, cfg_.begin_string, heartbt_sec, sending_time_view, initr_reset_seqnum,
        initr_next_expected);
    if (!logon_result) {
        // build_logon failed (oversized IDs → wire_frame_too_large).
        // Session-fatal — initiator handshake never reached the wire; transition
        // to Disconnected to match the acceptor send-throw symmetry promised by
        // FR-009 + the "session-fatal → Disconnected" precedent set by the
        // liveness loop (assign_outbound failure at session.cpp:~1466) and the
        // Active send-throw witness in send_path_test.
        // [W3.4 / /simplify B-8 fix; FR-009 "symmetric to acceptor witness";
        //  [FIX-SL §4.3] initiator handshake failure semantics]
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(logon_result.error());
    }
    // Advance outbound counter through manager ONLY on build success.
    // RC#A: was ++next_outbound_seq_; now routes through SeqnumManager.
    auto assign_r = co_await seqnum_mgr_.assign_outbound();
    if (!assign_r) {
        // Seqnum overflow — same disposition as build_logon failure above.
        // [W3.4 / /simplify B-8 fix]
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(assign_r.error());  // overflow (I-8)
    }
    // 019 T014: toAdmin before transmitting the initiator Logon. [FR-008/010]
    if (!fire_to_admin_(*logon_result)) {
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(fixpp::core::error::app_callback_threw);
    }
    auto emit_r = co_await store_then_emit(logon_seq, *logon_result);
    if (!emit_r) {
        // store_then_emit failed (store I/O or transport throw → dispatch_aborted).
        // Same disposition: session-fatal → Disconnected.
        // [W3.4 / /simplify B-8 fix; F7 drift fix; spec.md FR-001(e)]
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(emit_r.error());
    }
    co_return fixpp::core::expected_t<void>{};
}

// ── Phase-2 linkable placeholders — REPLACED per user story ─────────────
// Marked so a later phase's task body substitutes (not appends to) these.

asio::awaitable<fixpp::core::expected_t<void>> Session::open() noexcept {
    using fixpp::core::error;

    // ── All config validations run BEFORE any observable state mutation ──────
    // (RC#2 P2.2 fix: reorder to prevent partial side-effects on failure path)

    // (5) reject a second open() on the same handle (slot 51 / FR-018).
    // Any non-never_opened state means open() already ran.
    if (state_ != lifecycle::never_opened) {
        co_return std::unexpected(error::session_already_open);
    }

    // (1) executor resolution — the uniform resolved =
    // override.value_or(engine_anchor) pattern (FR-016/FR-018).
    asio::any_io_executor resolved = cfg_.executor_override.value_or(engine_.executor);

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
    if (cfg_.mode == threading_mode::direct_executor && cfg_.locks == lock_policy::spin) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // T048 (US5): runtime out-of-range-cast reject for backpressure_mode
    // (I-14 / [const §XV.15] / [2d §6.4] / FR-010). The enum is closed with
    // exactly 2 values (block=0, disconnect_and_recover=1); drop_oldest is
    // UNREPRESENTABLE. A cast from an out-of-range integer (FFI/SWIG bypass)
    // is caught here as a defence-in-depth backstop.
    FIXPP_ASSERT_BACKPRESSURE_SWITCH_EXHAUSTIVE(fixpp::session::SessionConfig::backpressure_mode);
    {
        const auto raw = static_cast<std::uint8_t>(cfg_.app_backpressure);
        if (raw != static_cast<std::uint8_t>(
                       fixpp::session::SessionConfig::backpressure_mode::block) &&
            raw != static_cast<std::uint8_t>(
                       fixpp::session::SessionConfig::backpressure_mode::disconnect_and_recover)) {
            co_return std::unexpected(error::invalid_session_config);
        }
    }

    // T050 (US5): null dictionary → invalid_session_config (slot 53 / FR-016
    // / FR-018 / I-13). dictionary is REQUIRED — the uniform resolved =
    // override.value_or(engine_anchor) pattern for the dictionary axis: a
    // null dictionary means neither session nor engine supplied one.
    if (!cfg_.dictionary) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // RC#1 (gate-b/r1): default-constructed security_profile sentinel →
    // invalid_session_config (slot 53 / N-P2-3 / [const §XII.5] / FR-018).
    // The minimal-stub pattern (D-15 / D-21 amended) ships SecurityProfile
    // as a complete value type with a sentinel discriminant (kind::unset).
    // 2g extends with the concrete TLS binding; the field SHAPE is now
    // correct and the constitutional no-implicit-default rule is enforced.
    if (cfg_.security_profile.k == fixpp::session::SecurityProfile::kind::unset) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // ── Executor binding — the single executor_not_serialised enforcement
    // point (slot 48 / FR-009 / I-06): make_session_executor wraps
    // make_strand under per_session_strand, carries the bare attested
    // executor under direct_executor, and rejects direct_executor && !attested.
    // This is the first observable mutation; all config rejections are above.
    //
    // 023 T009 (D3-B / E-3 / INV-3a): if the engine pre-created a strand and
    // stored it in cfg_.engine_adopt_strand, use the adopt_strand_t overload
    // which stores it DIRECTLY with strand_wrapped=true (no second make_strand
    // wrap — the D1 anti-pattern). The ordinary user per_session_strand path
    // (the make_session_executor(resolved, mode, ...) call below) is
    // BYTE-UNCHANGED and still unconditionally wraps with make_strand.
    if (cfg_.engine_adopt_strand.has_value()) {
        // Engine-only path: adopt the pre-created strand directly.
        exec_ = fixpp::core::make_session_executor(fixpp::core::adopt_strand_t{},
                                                   *cfg_.engine_adopt_strand, this);
    } else {
        // Ordinary user path (per_session_strand or direct_executor).
        auto bound = fixpp::core::make_session_executor(std::move(resolved), cfg_.mode,
                                                        cfg_.already_serialized_executor, this);
        if (!bound) {
            co_return std::unexpected(bound.error());
        }
        exec_ = std::move(*bound);
    }
    // INV-3 (E-3/023): when the engine adopts a strand, the session's inner
    // executor IS that strand. Debug-assert verifies identity (catches D1 double-
    // wrap if the engine accidentally re-wraps before calling open()).
    assert((!cfg_.engine_adopt_strand.has_value() ||
            exec_.underlying() == *cfg_.engine_adopt_strand) &&
           "INV-3: adopted strand mismatch — session executor must equal engine_adopt_strand");

    // (2) effective_clock = SessionConfig::clock_override ?:
    // EngineConfig::clock, resolved ONCE here, bound to session lifetime
    // (FR-005 / I-03). The clock_not_set engine-level gate (FR-006) is
    // validate_engine_config() at Engine::open — independent of per-session
    // overrides; Session::open only resolves.
    effective_clock_ = cfg_.clock_override ? cfg_.clock_override : engine_.clock;

    // (3) T045: populate the session_local<trace_context> slot from
    // SessionConfig::initial_trace_context (FR-014). Stored in-domain at
    // open; current_trace_context reads it through the stable Session*
    // (survives cross-thread resume — NOT thread_local).
    trace_slot_.store(cfg_.initial_trace_context);

    // ── 008-message-store ownership wire (T011 / FR-005 / FR-025 / FR-026 /
    //    FR-028 A1 hook) ────────────────────────────────────────────────────
    // Mint the per-session MessageStore via the factory when configured.
    // The 007 baseline leaves SessionConfig::store_factory null on smoke
    // paths; 005's FSM will require it. The engine threads in:
    //   - sender_comp_id / target_comp_id (4th and 5th positional per
    //     FR-005; FileStore composes the on-disk log path from them post
    //     CompID-safety validation per [2e §D.4]),
    //   - &store_arena_resource_ as the 3rd mr argument — the dedicated
    //     monotonic_buffer_resource whose lifetime matches the store
    //     instance (FR-026 peer-not-sub-resource rule),
    //   - engine_.max_store_memory_per_session as the 4th cap (FR-014a;
    //     storage-DoS guard binding),
    //   - engine_.file_io_executor as the 5th file-I/O executor (FR-024a;
    //     FileStore async pwrite/fdatasync target).
    // store_factory_failed surfaces verbatim; the store is bound as N1
    // unique ownership before state_ flips to open (no observable open
    // session without a usable store when one was requested).
    if (cfg_.store_factory) {
        auto minted = cfg_.store_factory->make(
            cfg_.sender_comp_id, cfg_.target_comp_id, &store_arena_resource_,
            engine_.max_store_memory_per_session, engine_.file_io_executor);
        if (!minted) {
            co_return std::unexpected(minted.error());  // store_factory_failed
        }
        store_ = std::move(*minted);
        // A1 factory-type tag: read the hook ONCE here. Null for impls that
        // do not satisfy detail::has_flush_for_session_close (MemoryStore /
        // user impls with no flush method); FileStore returns a typed thunk.
        // T032 (Phase 4 US2) dispatches this at close(graceful).
        close_flush_hook_a1_ = store_->flush_hook();
    }

    state_ = lifecycle::open;

    // US4 (T046): capture transport_send from config (null if not set).
    // Called from store_then_emit() AFTER store(outbound) completes (I-3).
    transport_send_ = cfg_.transport_send;

    // 014 T009/T010: wire the FSM back-pointer, reconnect endpoint, and TLS profile.
    // ReconnectFsm::drive_reconnect_attempt() uses these to call async_connect(),
    // async_handshake(), and install_reconnected_transport() on success.
    reconnect_fsm_.set_session_owner(this);
    reconnect_fsm_.set_reconnect_endpoint(cfg_.reconnect_endpoint);

    // 014 T018 — Wire the strand-bound credentials_rotated emit callback on the
    // Session's internal reconnect_fsm_.  The FSM detects rotation at step 2 of
    // drive_reconnect_attempt and invokes this lambda, which calls emit_event()
    // (private, defined in session.cpp) to push the event into recent_events_.
    // The lambda captures `this` by pointer; lifetime is guaranteed because the
    // FSM is a value member of Session (reconnect_fsm_, session.hpp:517) and is
    // destroyed before Session is — so `this` is always valid when the callback fires.
    // §XI.4: emit_event() is always called from the session strand (the FSM
    //   coroutine runs on the session executor set in Session::open()).
    // Resolves the "DEFERRED to 014" comment at session.hpp:274.
    // [data-model §E-3; contracts C3; FR-009; §XI.4; T017/T018]
    reconnect_fsm_.set_emit_credentials_rotated(
        [this](fixpp::session::session_event_credentials_rotated ev) noexcept { emit_event(ev); });
    // Map session-layer SecurityProfile::kind to tls::SecurityProfile so
    // async_handshake's profile-check is satisfied (not transport_psk_unsupported).
    // The enum values are identical for the common cases (mtls_ca=1, mtls_pinned=2,
    // one_way_ca=3). [data-model §E-1 step 3]
    {
        auto k = cfg_.security_profile.k;
        using SK = fixpp::session::SecurityProfile::kind;
        using TK = fixpp::tls::SecurityProfile;
        TK tls_profile = TK::unset;
        if (k == SK::mtls_ca)
            tls_profile = TK::mtls_ca;
        else if (k == SK::mtls_pinned)
            tls_profile = TK::mtls_pinned;
        else if (k == SK::one_way_ca) {
            // one_way_ca is deprecated in the TLS layer but still supported
            // for legacy interop (session layer retains it per [const §XII.5]).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            tls_profile = TK::one_way_ca;
#pragma clang diagnostic pop
        }
        reconnect_fsm_.set_tls_profile(tls_profile);
    }

    // T011 (US2, Phase 4): branch on cfg_.role per FR-004 + Opus triage RC#2.
    // Initiator arm: NotConnected → LogonSent; emit initial Logon frame via
    //   build_logon + store_then_emit (admin-builder path, NOT Session::send).
    //   [spec.md FR-004 §US2 AC3; data-model.md §E1; opus_pr81_1_triage.md RC#2]
    // Acceptor arm:  stay NotConnected; emit NO outbound Logon; wait for peer
    //   Logon via on_inbound_frame (NotConnected → LogonReceived → Active).
    //   [spec.md FR-004 §US2 AC1; contracts/session_role.hpp]
    if (cfg_.role == session_role::initiator) {
        // 015 T016(d): engine-managed lazy-connect initiators DEFER the entire
        // initiator arm — there is no live transport at open(), so emitting a
        // Logon now would either hit a null sink or write before connect
        // (violating connect-then-Logon, FR-003). The Engine's run_connect_loop
        // drives Session::drive_reconnect() (install_reconnected_transport →
        // LogonSent + transport_send_ rebind, T016(c)) and then emits the Logon
        // POST-connect via emit_initiator_logon_() (E-1a). The per-session-direct
        // model (013/014, engine_managed=false) keeps emitting at open below.
        if (!cfg_.engine_managed) {
            // 024 T007: reset_on_logon is handled inside emit_initiator_logon_() (the
            // shared emission point for open() and drive_reconnect()) so both call sites
            // reset symmetrically. [[feedback_half_restructure_symmetric_api]]
            record_state_transition_(fsm_state::LogonSent);
            auto logon_r = co_await emit_initiator_logon_();
            if (!logon_r) {
                // emit_initiator_logon_ has already transitioned to Disconnected.
                co_return std::unexpected(logon_r.error());
            }
        }
    } else {
        // Acceptor: stay in NotConnected, emit nothing.
        // fsm_state_ remains fsm_state::NotConnected (its default constructed value).
    }

    // T041 (US3): seed last_inbound_steady_ and last_outbound_steady_ at open()
    // so the liveness loop starts measuring from session-open, not the epoch.
    if (effective_clock_) {
        last_inbound_steady_ = effective_clock_->steady_now();
        last_outbound_steady_ = last_inbound_steady_;  // T018 Cell A: outbound idle tracking
    }

    // 019 T016: fire onCreate after open() has fully initialized exec_
    // (executor is valid post-open() per research D3 / data-model.md INV-7).
    // Invoked directly on the engine executor (exec_); serialization derives from
    // single-thread confinement (015 E-5), NOT from an engaged per-session strand.
    // See data-model.md INV-2 + spec/behaviors-and-limitations.md L-019-3.
    // On throw → terminal close + return error (FR-011; US3 AC1). [FR-009]
    if (engine_.application != nullptr) {
        const SessionId create_id = SessionId::from_config(cfg_);
        fixpp::core::expected_t<void> cb_r{};
        {
            // T019: scope the guard BEFORE close(terminal) so onLogout (fired
            // inside close → record_state_transition_) can acquire its own
            // callback_dispatch_scope without hitting the re-entrancy assert.
            callback_dispatch_scope cs{*this};
            cb_r = invoke_callback_safe([&]() { engine_.application->onCreate(create_id); });
        }  // cs drops here — in_dispatch_ = false
        if (!cb_r) {
            co_await close(fixpp::session::close_mode::terminal);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
    }

    co_return fixpp::core::expected_t<void>{};
}

asio::awaitable<fixpp::core::expected_t<void>> Session::close(close_mode mode) {
    using fixpp::core::error;

    // F2 (Gate-B/r1): close() is teardown — once it commits to `closing` it MUST run
    // to completion and publish close_result_. If the CALLER is cancelled mid-close
    // (run_read_pump's `co_await session.close(terminal)` entered just as Engine::stop()
    // fires session_cancel.emit) a later co_await here would otherwise abort with
    // operation_aborted, unwinding BEFORE close_result_ is set — and then the
    // Engine::stop() post-join drain re-enters, takes the `closing` branch below, and
    // awaits a result nobody will ever set → hang. Disable cancellation on this
    // coroutine for the whole of close() so it is immune to the caller's signal. This
    // shields close()'s OWN co_awaits only; the session work it tears down is still
    // cancelled via root_cancel_.emit(total) + cancel_sleeps() fired in phase 2.
    // (Engine::stop() also drives a fresh close() for sessions whose role-loop close()
    // was cancelled BEFORE entry — the two fixes are complementary.) [Codex P1]
    co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});

    // ── T038: idempotent THREE-STATE model (I-10 / [2d §4.7]:830-832,863) ──
    // never-opened OR already-closed(drained) → session_already_closed
    // (slot 52); no side effects.
    if (state_ == lifecycle::never_opened || state_ == lifecycle::closed_drained) {
        co_return std::unexpected(error::session_already_closed);
    }
    // already-closing (in-flight) → the SAME in-flight result, NO error, NO
    // side effects: await the first call's shared slot, then mirror it. (The
    // 2d-owned property the seam asserts; the scripted double drives the
    // interleave deterministically — [2d §6.5]:1172.)
    if (state_ == lifecycle::closing) {
        auto shared = close_result_;
        while (!shared || !shared->has_value()) {
            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - guarded by has_value() above
        co_return **shared;
    }

    // ── First close() on an OPEN session: run the two-phase body once ──────
    state_ = lifecycle::closing;
    close_result_ = std::make_shared<std::optional<fixpp::core::expected_t<void>>>();

    // T037 phase 1 — graceful ONLY (terminal skips phase 1 entirely; the
    // hook is NOT invoked). Invoked EXACTLY ONCE, after the (scripted) last
    // in-flight store(...) resumes and BEFORE the Logout step. A
    // store_io_failure is logged then close PROCEEDS (I-07); 007 has no log
    // sink wired (005/2e), so the failure is observed by the hook's own
    // bookkeeping and close still completes successfully. The real Logout
    // exchange + Clock::sleep_until close-timeout under a CHILD
    // cancellation_state are 005-owned (no transport / no D-9 timeout value
    // in 007 — D-16); the 2d-owned phase-1 obligation wired here is the
    // call-site + the once/never ordering the seam asserts.
    //
    // T032 (008-message-store / US2 / FR-028 / I-17 / Appendix D §D.2):
    // A1-pinned graceful-close hook dispatch via the typed thunk stashed at
    // open(). Non-virtual (concept-shaped, NOT dynamic_cast). Runs OUTSIDE
    // phase-1's child timeout (the real child timeout for Logout is 005-owned;
    // this plain co_await runs without a child cancellation_state). terminal
    // skips this block entirely per the mode guard above.
    if (mode == close_mode::graceful) {
        // A1 dispatch: the factory-type-tag typed thunk (non-null for FileStore;
        // null for MemoryStore / user impls without flush_for_session_close).
        if (close_flush_hook_a1_ != nullptr && store_ != nullptr) {
            auto flush_result = co_await (*close_flush_hook_a1_)(*store_);
            (void)flush_result;  // store_io_failure → logged-then-proceed (I-07)
        }

        // Scripted seam-5 hook (007's D-16 scripted-test-double; kept for
        // backward compatibility with seam-5 test assertions). Runs AFTER the
        // A1 typed-thunk dispatch in phase 1 ordering.
        if (close_flush_hook_) {
            const auto flush = close_flush_hook_();
            (void)flush;  // store_io_failure → logged-then-proceed (I-07)
        }

        // US4 / T047: phase-1 Logout exchange under a CHILD cancellation_state.
        // Only runs when the session was in Active state (i.e. reached Active
        // and has a transport to emit on, or the FSM is at LogoutSent/Active
        // when close(graceful) is called). If the session never opened or is
        // already below Active, the Logout step is a no-op.
        //
        // The child cancellation_state isolates the Logout write + 2s sleep
        // from the phase-2 root total signal: phase-2 fires root_cancel_.emit()
        // AFTER this block resolves (peer ACK | timeout | cancellation).
        //
        // [feedback_asio_cospawn_total_cancellation_default]: we use the root
        // slot for the child; the run_logout_phase1 coroutine resets to
        // enable_total_cancellation internally.
        if (fsm_state_ == fsm_state::Active || fsm_state_ == fsm_state::LogonReceived) {
            using namespace asio::experimental::awaitable_operators;

            auto ex = co_await asio::this_coro::executor;
            asio::steady_timer close_grace{ex};
            close_grace.expires_after(std::chrono::milliseconds{cfg_.logout_disconnect_timeout_ms});

            auto phase1_or_timeout =
                co_await (run_logout_phase1() || close_grace.async_wait(asio::use_awaitable));
            if (phase1_or_timeout.index() == 0) {
                auto phase1_r = std::get<0>(std::move(phase1_or_timeout));
                (void)phase1_r;  // timeout is logged-then-proceed (I-07; force-disconnect)
            } else if (auto live = live_transport_shared_()) {
                // FQ-G: if phase 1 wedges behind write_gate_ / async_write,
                // force-close the transport so the blocked writer unwinds and
                // phase 2 can drain the gate without deadlocking close(graceful).
                (void)live->close();
            }
        }
    }

    // US4: ensure FSM is Disconnected before phase-2 fires.
    // Graceful close: run_logout_phase1 already transitioned to Disconnected.
    // Terminal close: transition directly here (phase 1 skipped, I-9).
    // Any other FSM state (LogonSent, NotConnected, etc.) also becomes
    // Disconnected per the matrix close(terminal)/fatal column.
    if (fsm_state_ != fsm_state::Disconnected) {
        record_state_transition_(fsm_state::Disconnected);
    }

    // T037/T039 phase 2 — fire root cancellation_type::total ONLY after
    // phase 1 has resolved (peer ACK | child timeout | child cancelled —
    // collapsed to "phase 1 done" in the scripted scope). This is the single
    // propagation point: every strand of in-flight session work bound to
    // root_cancellation_slot() (transport r/w, heartbeat sleep, mutex
    // acquire, cancellable_dispatch, parser→fromApp — all 005-owned) unwinds
    // here. terminal reaches phase 2 immediately (phase 1 skipped).
    //
    // US4 (T047): cancel any mock_clock (or real clock) sleepers BEFORE
    // emitting the ASIO total-cancellation signal. The liveness loop's
    // sleep_until is waiting on the mock_clock timer (not an ASIO channel),
    // so root_cancel_.emit() alone cannot wake it up — mock_clock::cancel_sleeps()
    // must be called first so the liveness loop exits its try/catch and the
    // coroutine can be collected by ioc.run_for() after close() returns.
    // Real asio::steady_timer sleeps are also cancelled by the ASIO slot, so
    // this call is safe for non-mock clocks too (cancel_sleeps() is a no-op
    // when no sleepers are registered).
    if (effective_clock_) {
        effective_clock_->cancel_sleeps();
    }
    root_cancel_.emit(asio::cancellation_type::total);
    if (auto live = live_transport_shared_()) {
        (void)live->close();
    }

    // T045: clear the session_local<trace_context> slot at close completion
    // (FR-014). Reached in BOTH graceful and terminal once the two phases
    // resolve; the slot stays valid until here (seam 17: never read through
    // a destroyed slot — the slot lives in the Session, drained, not freed).
    trace_slot_.clear();

    // FQ-A (gate-b/r2): wait for the liveness loop to exit before the seqnum drain.
    // root_cancel_.emit() above has already fired total cancellation (which cancels
    // the liveness sleep_until via cancel_sleeps() + root cancel propagation), and
    // the live transport is synchronously closed above so any in-progress liveness
    // write completes with error before the drain waits on write_gate_. We yield the
    // executor until liveness_counter_ reaches 0, meaning the liveness coroutine has
    // fully exited run_liveness_loop. This ensures registry_.clear() cannot destroy
    // the Session while the liveness coroutine is still touching Session members.
    // [feedback_detached_cospawn_write_not_in_join_counter; FQ-A D-6 F4]
    {
        auto lc = liveness_counter_;
        while (lc->load(std::memory_order_acquire) > 0) {
            // Yield one step; liveness loop's try/catch converts cancellation to
            // a clean return, then the RAII guard decrements the counter.
            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        }
    }

    // 024 T014 — teardown reset (reset_on_logout / reset_on_disconnect).
    // Placement: BEFORE write_gate_.cancel_and_drain() (the first drain).
    // Binding constraint (analyze F1): reset_seqnums_to_one_durable() acquires the
    // seqnum async-mutex via reset_to_one(). If seqnum_mgr_.drain() has already run,
    // the mutex is drained and reset_to_one() would return session_already_closed,
    // silently no-oping without resetting counters. Must therefore run before BOTH
    // drains (write_gate_.cancel_and_drain() and seqnum_mgr_.drain()).
    //
    // Single-fire guard (C5.1): teardown_reset_done_ prevents a logout+disconnect
    // double-trigger (e.g. graceful Logout → timeout → terminal close via close()
    // re-entry) from calling store_->reset() twice. FileStore::reset() is
    // non-idempotent I/O (full atomic-rename + fdatasync + dir-fsync per call).
    //
    // Predicate: (logout_seen_ && cfg_.reset_on_logout) || cfg_.reset_on_disconnect.
    //   - reset_on_logout: fires when a Logout occurred in either direction
    //     (local sent → run_logout_phase1 set logout_seen_; peer received →
    //      on_inbound_frame 35=5 handler set logout_seen_). [C3.1]
    //   - reset_on_disconnect: fires on ANY close() — graceful, terminal, or
    //     abnormal read-pump EOF → close(terminal). [C4.1, C4.2]
    //
    // Disposition: logged-then-proceed (I-07) — teardown store failure is not fatal;
    // the session should still complete close() normally. [contracts C3.1, C4.1]
    //
    // [contracts/reset-knobs.md C3.1, C4.1, C4.2, C5.1; plan.md Complexity row 3]
    if (!teardown_reset_done_ &&
        ((logout_seen_ && cfg_.reset_on_logout) || cfg_.reset_on_disconnect)) {
        teardown_reset_done_ = true;
        auto rst_r = co_await reset_seqnums_to_one_durable(reset_disposition::logged);
        (void)rst_r;  // I-07 logged-then-proceed: store failure does not abort close.
    }

    // FQ-A (gate-b/r2): drain the write gate after the liveness loop exits.
    // Any in-flight live write (started before total cancel propagated) has
    // its socket closed above so async_write completes with error → write_gate_
    // released before cancel_and_drain() waits for the current holder.
    // cancel_and_drain() cancels any pending waiters (they get dispatch_aborted
    // from live_write_serialized_) and waits for the current holder (if any)
    // to unlock — satisfying the async_mutex destructor precondition.
    // [FQ-A D-6 F1/F3; transport.hpp:47-50]
    {
        auto wg_drain_r = co_await write_gate_.cancel_and_drain();
        (void)wg_drain_r;  // I-07 logged-then-proceed.
    }

    // T022 (009 Phase 6 / FR-011 / RC#7 / D-2):
    // Drain the SeqnumManager's async_mutex before state_ = closed_drained.
    // Satisfies the async_mutex destructor's not_locked precondition:
    // any in-flight check_inbound / assign_outbound that was suspended
    // (e.g., waiting for the mutex under concurrent access) is cancelled
    // and completes before SeqnumManager is destroyed with Session.
    //
    // Policy (research.md D-2 / I-07): drain failures (sync_lock_aborted
    // if the reaper's own cancellation slot fired) are logged-then-proceed;
    // the close still completes successfully.
    //
    // Safe on never-locked mutexes (session never reached check_inbound):
    // cancel_and_drain() on an idle mutex is a no-op that returns ok.
    {
        auto drain_r = co_await seqnum_mgr_.drain();
        (void)drain_r;  // D-2 logged-then-proceed: drain failure does not abort close.
    }

    // Completed: both phases drained (transport closed / arenas reset /
    // trace slot cleared above / seqnum mutex drained). Cancellation surfaces as
    // operation_aborted/dispatch_aborted on the in-flight work, never a
    // thrown exception across parse→fromApp (I-09); close() itself
    // completes expected_t<void>{}.
    *close_result_ = fixpp::core::expected_t<void>{};
    state_ = lifecycle::closed_drained;
    co_return **close_result_;
}

// ── 005-session-establishment-fsm additions (T018) ──────────────────────────
// Bodies wired per user story (Phase 3 / T023–T025). Each placeholder is
// replaced by the full body below; NOT additive — the comment is the anchor.

namespace {

// Minimal SOH-delimited field scanner — no heap, no library.
// Reads tag 8 (BeginString), 34 (MsgSeqNum), 35 (MsgType),
// 49 (SenderCompID), 52 (SendingTime), 56 (TargetCompID), 112 (TestReqID)
// plus 013 Phase 3 tags:
//   7 (BeginSeqNo), 16 (EndSeqNo), 36 (NewSeqNo), 43 (PossDupFlag),
//   123 (GapFillFlag), 141 (ResetSeqNumFlag)
// from a raw FIX frame.
// Used by the inbound dispatch path (scenarios 2i/2k/seqnum check + US3 liveness
// + US5 SendingTime MaxLatency check + T017/T026 recovery).
struct FrameHeader {
    std::string_view begin_string;
    std::string_view sender_comp_id;
    std::string_view target_comp_id;
    std::string_view msg_seq_num;   // tag 34 raw string value
    std::string_view msg_type;      // tag 35 raw string value (T041 US3)
    std::string_view sending_time;  // tag 52 raw string value (T055 US5)
    std::string_view test_req_id;   // tag 112 raw string value (T041 US3)
    // 013 Phase 3 — recovery / reset fields
    std::string_view begin_seqno;        // tag 7 (BeginSeqNo in ResendRequest)
    std::string_view end_seqno;          // tag 16 (EndSeqNo in ResendRequest)
    std::string_view new_seqno;          // tag 36 (NewSeqNo in SequenceReset)
    std::string_view poss_dup_flag;      // tag 43 (PossDupFlag "Y"/"N")
    std::string_view orig_sending_time;  // tag 122 (OrigSendingTime) — 021 PossDup
    std::string_view gap_fill_flag;      // tag 123 (GapFillFlag in SequenceReset)
    std::string_view reset_seqnum_flag;       // tag 141 (ResetSeqNumFlag in Logon)
    std::string_view next_expected_msg_seq_num;  // tag 789 value (may be empty if field present-but-empty) — 027
    bool next_expected_present = false;          // tag 789 was present in frame (even if value is empty) — 027
};

[[nodiscard]] FrameHeader scan_frame_header(std::span<const std::byte> frame) noexcept {
    FrameHeader h;
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    std::size_t i = 0;
    const std::size_t n = frame.size();

    while (i < n) {
        std::uint32_t tag = 0;
        bool tag_ok = true;
        while (i < n && frame[i] != EQ && frame[i] != SOH) {
            auto c = static_cast<unsigned char>(frame[i]);
            if (c < '0' || c > '9') {
                tag_ok = false;
            }
            // Overflow guard: a tag wider than UINT32_MAX would wrap and could
            // ALIAS a known small tag (e.g. 2^32+35 → 35 overrides MsgType).
            // Mark unparseable so the field is skipped, never aliased.
            if (tag > 429496729U) {
                tag_ok = false;
            }
            tag = (tag * 10U) + static_cast<std::uint32_t>(c - '0');
            ++i;
        }
        if (i >= n || frame[i] != EQ || !tag_ok) {
            while (i < n && frame[i] != SOH) {
                ++i;
            }
            if (i < n) {
                ++i;
            }
            continue;
        }
        ++i;  // skip '='
        std::size_t vstart = i;
        while (i < n && frame[i] != SOH) {
            ++i;
        }
        std::string_view val(reinterpret_cast<const char*>(frame.data() + vstart), i - vstart);
        if (i < n) {
            ++i;
        }  // skip SOH

        switch (tag) {
            case 7:
                h.begin_seqno = val;
                break;  // T026 ResendRequest
            case 8:
                h.begin_string = val;
                break;
            case 16:
                h.end_seqno = val;
                break;  // T026 ResendRequest
            case 34:
                h.msg_seq_num = val;
                break;
            case 35:
                h.msg_type = val;
                break;  // T041 US3
            case 36:
                h.new_seqno = val;
                break;  // T026 SequenceReset
            case 43:
                h.poss_dup_flag = val;
                break;  // T026 PossDupFlag
            case 49:
                h.sender_comp_id = val;
                break;
            case 52:
                h.sending_time = val;
                break;  // T055 US5 SendingTime
            case 56:
                h.target_comp_id = val;
                break;
            case 112:
                h.test_req_id = val;
                break;  // T041 US3
            case 122:
                h.orig_sending_time = val;
                break;  // 021 PossDup OrigSendingTime
            case 123:
                h.gap_fill_flag = val;
                break;  // T026 SequenceReset GapFillFlag
            case 141:
                h.reset_seqnum_flag = val;
                break;  // T027 ResetSeqNumFlag
            case 789:
                h.next_expected_msg_seq_num = val;
                h.next_expected_present = true;  // set even when val is empty (D-10 empty-value guard)
                break;  // 027 NextExpectedMsgSeqNum in Logon
            default:
                break;
        }
    }
    return h;
}

// Parse a decimal seqnum from a string_view. Returns 0 if invalid.
// Zero is never a valid FIX seqnum (seqnum_min=1), so 0 signals parse failure.
// No heap, no library, stack-only. (I-7 no-alloc hot path.)
[[nodiscard]] fixpp::session::seqnum_t parse_seqnum(std::string_view sv) noexcept {
    using fixpp::session::seqnum_t;
    if (sv.empty()) {
        return 0;
    }
    seqnum_t val = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') {
            return 0;
        }
        const auto digit = static_cast<seqnum_t>(c - '0');
        // Overflow guard: seqnum_max / 10 = UINT32_MAX / 10 = 429496729.
        if (val > 429496729U || (val == 429496729U && digit > 5U)) {
            return 0;  // overflow
        }
        val = (val * 10U) + digit;
    }
    return val;
}

// stamp_sending_time: format effective_clock.now() into a stack buffer and
// return a string_view into it. Returns an empty string_view if the clock is
// null or formatting fails (caller must check before passing to build_*).
// FR-003/RC#4: replaces kSendingTimePlaceholder at all admin-builder call sites.
// Buffer must be at least 27 bytes (nanos precision max). noexcept per I-7.
// prec is NON-DEFAULTED — compiler enforces exhaustive threading (I-NST-6).
struct SendingTimeStamp {
    std::array<char, 32> buf{};
    std::string_view value;  // points into buf
};

[[nodiscard]] SendingTimeStamp stamp_sending_time(fixpp::core::Clock& clock,
                                                  fixpp::core::fix_time_precision prec) noexcept {
    SendingTimeStamp s;
    auto fmt_r = fixpp::core::utc_time_to_fix_string(clock.now(), prec,
                                                     std::span<char>{s.buf.data(), s.buf.size()});
    if (fmt_r) {
        s.value = std::string_view{fmt_r->data(), fmt_r->size()};
    }
    return s;
}

// 013 FR-010 [FIX-SL §4.3.5] — re-serialize a STORED outbound frame for resend
// reply: copy every original field (preserving MsgSeqNum 34), append
// PossDupFlag(43)=Y and OrigSendingTime(122)=<the stored SendingTime(52)>, and
// recompute BodyLength(9)/CheckSum(10) via fixpp::wire::Writer. The replayed
// message keeps its ORIGINAL sequence number and does NOT advance the live
// outbound counter (resend semantics). Stack-only; the 9=/10= source fields are
// skipped (the Writer rebuilds them on commit).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_replay_frame(
    std::span<std::byte> out, std::span<const std::byte> stored) noexcept {
    fixpp::wire::Writer w(out, std::pmr::null_memory_resource());
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    std::string_view orig_sending_time;
    std::size_t i = 0;
    const std::size_t n = stored.size();
    while (i < n) {
        std::uint32_t tag = 0;
        bool tag_ok = true;
        while (i < n && stored[i] != EQ && stored[i] != SOH) {
            auto c = static_cast<unsigned char>(stored[i]);
            if (c < '0' || c > '9') tag_ok = false;
            tag = (tag * 10U) + static_cast<std::uint32_t>(c - '0');
            ++i;
        }
        if (i >= n || stored[i] != EQ || !tag_ok) {
            while (i < n && stored[i] != SOH) ++i;
            if (i < n) ++i;
            continue;
        }
        ++i;  // skip '='
        const std::size_t vstart = i;
        while (i < n && stored[i] != SOH) ++i;
        std::span<const std::byte> val{stored.data() + vstart, i - vstart};
        if (i < n) ++i;                       // skip SOH
        if (tag == 9 || tag == 10) continue;  // BodyLength/CheckSum recomputed on commit
        if (tag == 52) {
            orig_sending_time =
                std::string_view{reinterpret_cast<const char*>(val.data()), val.size()};
        }
        if (auto r = w.append_raw(tag, val); !r) return std::unexpected(r.error());
    }
    // PossDupFlag(43)=Y
    {
        std::byte y[] = {static_cast<std::byte>('Y')};
        if (auto r = w.append_raw(43, std::span<const std::byte>{y}); !r) {
            return std::unexpected(r.error());
        }
    }
    // OrigSendingTime(122) = the stored SendingTime(52) value.
    {
        std::span<const std::byte> ost{reinterpret_cast<const std::byte*>(orig_sending_time.data()),
                                       orig_sending_time.size()};
        if (auto r = w.append_raw(122, ost); !r) return std::unexpected(r.error());
    }
    auto committed = std::move(w).commit();
    if (!committed) return std::unexpected(committed.error());
    return out.subspan(0, *committed);
}

// 013 FR-010 — a retrieve_visitor that copies a single stored frame into a
// stack buffer for the resend store-walk (one retrieve(K,K) per slot).
//
// RC#B (gate-b/r1): buffer enlarged from 1024→4096B.
// A FIX NewOrderSingle with repeating NoPartyIDs groups easily exceeds 1024B.
// The old 1024B limit silently collapsed oversized real app messages into a
// SequenceReset-GapFill — same silent-data-loss class as FR-010/FR-012.
// 4096B covers all realistic FIX app messages; the replay buffer below is
// matched to the same size. [const §VIII.5]: fixed member, no per-frame alloc.
// If a frame exceeds 4096B (degenerate/malformed), the caller disconnects
// rather than silently GapFilling a real app message. [triage RC#B]
class CaptureVisitor final : public fixpp::session::retrieve_visitor {
public:
    static constexpr std::size_t kCapBufSize = 4096;
    std::array<std::byte, kCapBufSize> buf{};
    std::size_t len = 0;
    bool captured = false;
    bool truncated = false;

    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        fixpp::session::seqnum_t /*seq*/, std::span<const std::byte> frame) noexcept override {
        if (frame.size() <= buf.size()) {
            std::ranges::copy(frame, buf.begin());
            len = frame.size();
            captured = true;
        } else {
            truncated = true;
        }
        co_return fixpp::session::visit_result::cont;
    }
};

}  // namespace

// ── emit_session_reject_ ─────────────────────────────────────────────────────
//
// Builds and emits a session Reject(35=3, RefTagID=0, reason=3) for an inbound
// message that was rejected by a fromAdmin() veto (not-threw path),
// OR for an inbound app-typed message when no Application is registered (unknown
// MsgType in Active → reason=3 per [FIX-SL §4.5.4]).
//
// Error handling: Disconnected-on-failure for both assign_outbound and
// store_then_emit. Callers that require "best-effort" emit (e.g. the Logout and
// SequenceReset paths where the session disconnects or acts regardless) keep the
// inline form — only the two sites with identical Disconnected-on-fail handling
// are extracted here.
//
// Returns expected_t<void>{} on success or build-failure (not fatal if build fails).
// Returns unexpected(error) on assign_outbound / store_then_emit failure, after
// recording Disconnected. Caller should propagate with co_return std::unexpected(…).
// [019-app-callbacks T011/T016; FR-005; D4; INV-4]
asio::awaitable<fixpp::core::expected_t<void>> Session::emit_session_reject_(
    seqnum_t ref_seq, std::string_view ref_msg_type) noexcept {
    std::array<std::byte, 512> rj_buf{};
    const auto rj_st52 = effective_clock_
                             ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                             : SendingTimeStamp{};
    const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
    auto rj_r =
        fixpp::session::build_reject(std::span<std::byte>{rj_buf.data(), rj_buf.size()}, rj_seq,
                                     cfg_.sender_comp_id, cfg_.target_comp_id, ref_seq,
                                     0,                // RefTagID: n/a for MsgType/veto rejection
                                     ref_msg_type, 3,  // SessionRejectReason = 3
                                     cfg_.begin_string, rj_st52.value);
    if (rj_r) {
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(assign_r.error());
        }
        auto emit_r = co_await store_then_emit(rj_seq, *rj_r);
        if (!emit_r) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(emit_r.error());
        }
    }
    co_return fixpp::core::expected_t<void>{};
}

// ── 013 T036 US2 — Logon-time CompID authorization helpers ───────────────────
//
// parse_cn_from_dn_local: extract the first "CN=" value from an OpenSSL
// text-form DN string. Mirrors the implementation in
// compid_authorization_policy.cpp (which is in an anonymous namespace there).
// Declared locally here to avoid cross-TU linkage of an internal helper.
// noexcept — pure string scanning.
[[nodiscard]] static std::string_view parse_cn_from_dn_local(std::string_view dn) noexcept {
    std::size_t pos = 0;
    while (pos < dn.size()) {
        const auto found = dn.find("CN=", pos);
        if (found == std::string_view::npos) return {};
        if (found > 0) {
            const char pre = dn[found - 1];
            if (pre != ',' && pre != ' ' && pre != '/') {
                pos = found + 3;
                continue;
            }
        }
        const std::size_t vstart = found + 3;
        if (vstart >= dn.size()) return {};
        std::size_t vend = vstart;
        while (vend < dn.size() && dn[vend] != ',') ++vend;
        const std::string_view value = dn.substr(vstart, vend - vstart);
        if (!value.empty()) return value;
        pos = vend;
    }
    return {};
}

// T024/T025 (US1, Phase 3) + T032/T034/T035 (US2, Phase 4) +
// T056 (US5, Phase 7): Inbound FSM dispatch.
//
// Guard precedence per data-model.md matrix preamble (T056 adds steps 1/3/5):
//   (1) parse/type recognised → else session Reject; no-loop-guard exempts
//       Reject(35=3) and Logout(35=5) from triggering a Reject.
//   (2) CompID/BeginString gate (post-logon states)
//   (3) SendingTime(52) MaxLatency vs effective clock (Q3):
//       established session → Reject(reason=10, refTag=52) → Logout → Disconnect
//       Logon path         → logout-with-error, no standalone Reject (D-3)
//   (4) seqnum class (too-low / too-high / in-seq) — T035
//   (5) message-type-for-state: unrecognized app msg type → session Reject;
//       session stays Active (no loop for Reject/Logout).
//
// Seqnum check (T031/T032/T035):
//   Too-low  → session_seqnum_too_low (69)              → fatal: Disconnected
//              Exception: Heartbeat(0) too-low silently ignored per T020-A.
//   Too-high → 013 FR-009: AwaitingResend + ResendRequest(2) via reconnect_fsm_
//              (slot 70 session_seqnum_gap_unrecoverable deleted per 013 T006a;
//               state owned by ReconnectFsm per data-model §E-1 / Fix1 T023)
//   In-seq   → advance counter, proceed
//
// For the NotConnected/Logon path the peer's first Logon carries seq=1.
// If seq != 1 → too-low or too-high → fatal.
//
// Inbound ordering (I-3 / [2e §7.6]): store(inbound) BEFORE fromAdmin/fromApp.
// T034 wires the store call; here the seqnum check is the gate.
//
// LogoutSent / Disconnected: all inbound silently drained (defined cells).
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_frame(
    std::span<const std::byte> frame) noexcept {
    switch (fsm_state_) {
        case fsm_state::NotConnected: {
            // First message must be a Logon. interpret_logon validates:
            //   MsgType==A, BeginString==cfg_.begin_string,
            //   SenderCompID==cfg_.target_comp_id (peer's sender = our target),
            //   TargetCompID==cfg_.sender_comp_id (peer's target = our sender),
            //   HeartBtInt present and ≥ 0.
            auto result = fixpp::session::interpret_logon(
                frame,
                cfg_.target_comp_id,  // expected_sender: peer's 49= is our target
                cfg_.sender_comp_id,  // expected_target: peer's 56= is our sender
                cfg_.begin_string);

            if (!result) {
                // Refusal — BeginString/CompID mismatch or not-Logon.
                // Per matrix NotConnected row (data-model.md:19):
                //   inbound Logon (refused)   → Disconnected (FR-006 / RC#3)
                //   inbound non-Logon (first) → Disconnected
                // No MsgType discrimination: every refusal on this row lands in
                // Disconnected. Phase-3 "stays NotConnected" compromise removed
                // by T014 [US3] per spec.md FR-006 + opus_pr81_1_triage.md RC#3.
                record_state_transition_(fsm_state::Disconnected);
                co_return fixpp::core::expected_t<void>{};
            }

            // Valid Logon: scan header for seqnum + 013 T027 ResetSeqNumFlag(141).
            // The Logon must carry seq=1 on initial session (seqnum_mgr_ starts at 1).
            // peer_sent_reset declared at case scope so the acceptor-reply block below
            // can read it when deciding whether to mirror 141=Y in our reply Logon.
            // [spec.md FR-017; RC#C gate-b/r1]
            //
            // 027 T014: peer_789_raw hoisted to case scope so the RC#4-ordering honor
            // block (after reply store_then_emit) can read it. string_view is safe
            // because frame (the coroutine parameter) outlives this case.
            // [contract C4, data-model I-NEX-2, RC#4]
            bool peer_sent_reset = false;
            std::string_view peer_789_raw;
            bool peer_789_present = false;
            {
                auto hdr = scan_frame_header(frame);
                peer_789_raw = hdr.next_expected_msg_seq_num;
                peer_789_present = hdr.next_expected_present;
                const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
                if (seq == 0) {
                    // Cannot parse seq — treat as invalid (fatal for protocol safety).
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }

                // 024: capture peer_sent_reset before check_inbound. The reset SITE is
                // cause-dependent (this preserves FR-001 byte-identity — a /speckit-verify
                // finding: a single unified pre-check reset changed the 013 received-141
                // post-state from next_inbound==1 to ==2):
                //   - knob-driven (reset_on_logon): reset BEFORE check_inbound so a fresh
                //     peer Logon at seq=1 is admitted even when local next_inbound > 1
                //     (Gate A note (a)); post-state next_inbound == 2 (witness 6).
                //   - 013-only received-141 (no knob): reset AFTER check_inbound (below),
                //     byte-identical to pre-024 — next_inbound == 1 (reset_seqnum_policy_matrix
                //     acceptor cells; FR-017:150).
                // The two are mutually exclusive (the post-check arm guards on !reset_on_logon),
                // so exactly one store reset fires per path (C5.1 / witness 7).
                // [024 data-model acceptor row; Gate A notes (a)/(d); C2.2/C2.6; FR-001]
                peer_sent_reset = (hdr.reset_seqnum_flag == "Y");

                if (cfg_.reset_on_logon) {
                    // Knob-driven → fatal: a store failure blocks Active (C2.6).
                    auto rst_r = co_await reset_seqnums_to_one_durable(reset_disposition::fatal);
                    if (!rst_r) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return std::unexpected(rst_r.error());
                    }
                }

                auto chk = co_await seqnum_mgr_.check_inbound(seq);
                if (!chk) {
                    // 027 T016 — behind-side tolerance (formulation A, I-NEX-5/D-7):
                    // When the knob is on AND the failure is too-high (NOT too-low),
                    // do NOT take the fatal branch. Leave next_inbound_ at X (the
                    // handler did not advance it — check_inbound only advances on
                    // in-sequence). Do NOT call set_next_inbound. Emit no at-logon
                    // ResendRequest. Proceed toward Active so the peer's proactive
                    // resend [X, peer_N-1] arrives in-sequence through the Active path.
                    // [contract C5, data-model I-NEX-5/12, research D-7]
                    //
                    // Too-low: unchanged — session-fatal (I-2/[FIX-SL §4.1]).
                    // Knob-OFF: completely unchanged — fatal on too-high exactly as today.
                    if (cfg_.enable_next_expected_msg_seq_num &&
                        chk.error() == fixpp::core::error::session_seqnum_too_high) {
                        // Behind-side: tolerate. next_inbound_ stays at X (not advanced).
                        // Fall through to the existing acceptor reply-build path below.
                    } else {
                        // Too-low OR knob off: session-fatal (I-2/I-4/[FIX-SL §4.1]).
                        record_state_transition_(fsm_state::Disconnected);
                        co_return fixpp::core::expected_t<void>{};
                    }
                }

                // T027 FR-017 — ResetSeqNumFlag(141) policy (Clarifications Q1=A).
                // bilateral_strict: REQUIRES mutual agreement on 141=Y. If peer does
                //   NOT send 141=Y when our policy is bilateral_strict, disconnect with
                //   session_seqnum_reset_mismatch(116). [spec.md FR-017; T017 Cell 2]
                // bilateral_lenient: if peer sends 141=Y → honour; if not → accept.
                // unilateral: always honour any peer 141=Y.
                // All modes: if peer sends 141=Y → emit session_event_sequence_numbers_reset.
                // [spec.md FR-017; data-model.md §E-4; Clarifications Q1=A]

                if (!peer_sent_reset &&
                    cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict) {
                    // bilateral_strict requires peer to also send 141=Y.
                    // Peer omitted 141=Y → session_seqnum_reset_mismatch(116) + Disconnected.
                    // RC#C (gate-b/r1): surface the typed error code instead of bare
                    // Disconnected, per triage RC#C(b) + spec.md FR-017 / US1 AC7.
                    record_state_transition_(fsm_state::Disconnected);
                    co_return std::unexpected(fixpp::core::error::session_seqnum_reset_mismatch);
                }

                // peer_sent_reset: event emission deferred to after the reply-Logon block
                // so the event fires with consistent post-reset counters (FR-018) and the
                // reply Logon is stamped at seq=1 (FR-017). The reset itself has already
                // run above via reset_seqnums_to_one_durable() when reset_on_logon.
            }

            // 013 T036 US2: CompID authorization BEFORE FSM transition to
            // LogonReceived/Active (FR-019/FR-020/FR-021/FR-024).
            // Acceptor: asserted CompID = peer SenderCompID(49) = cfg_.target_comp_id.
            //
            // ACCEPTOR LIVE-BINDING (T-041 CLOSED, 015 US4):
            //   The acceptor binds the REAL handshake identity from
            //   attach_accepted_transport — the test seam is gone (T020/SC-006).
            //   [[feedback_half_restructure_symmetric_api]]: both roles now bind a
            //   live identity and fail CLOSED symmetrically (initiator guard below).
            //
            // Two-arm guard (015 T020 removed the seam arm from the T013 form):
            //   (1) live_peer_id_ set + is_mtls → arm (1-live): authorize with the
            //       real handshake peer_id from attach_accepted_transport (T011).
            //       Happens-before invariant (Gate A New-1 / E-4): live_peer_id_ is
            //       set by attach_accepted_transport STRICTLY-BEFORE the first
            //       on_inbound_frame reaches this gate — guaranteed by run_accept_loop
            //       calling attach_accepted_transport before co_awaiting on_inbound_frame.
            //       Admit on-list / fail-CLOSED off-list-or-absent (T-041 acceptor path).
            //   (2) mTLS + no identity available → FAIL CLOSED (RC#A). Fires when
            //       is_mtls but live_peer_id_ is absent (happens-before violated
            //       would land here — the safe default per Gate A New-1).
            //   (3) Non-mTLS (one_way_ca / unset-but-passed-open-guard) → skip.
            // [015 T013/T020; data-model §E-4; FR-006/007/008/009; T-041; Gate A New-1/E-4]
            // [FR-019; FR-024 symmetric]
            {
                const bool is_mtls =
                    cfg_.security_profile.k == fixpp::session::SecurityProfile::kind::mtls_ca ||
                    cfg_.security_profile.k == fixpp::session::SecurityProfile::kind::mtls_pinned;

                if (live_peer_id_.has_value() && is_mtls) {
                    // (1) Live acceptor path: use real handshake peer_id set by
                    //     attach_accepted_transport. Mirrors the initiator arm at :1864.
                    const fixpp::tls::peer_identity& auth_pid = *live_peer_id_;
                    const std::string_view asserted_compid = cfg_.target_comp_id;
                    auto auth_r =
                        cfg_.compid_authorization_policy.authorize(auth_pid, asserted_compid);
                    if (!auth_r) {
                        // Fail-closed: off-list or absent identity.
                        emit_event(fixpp::session::session_event_compid_authorization_failed{
                            .cn = {},
                            .asserted_compid = asserted_compid,
                            .expected_compids = {},
                            .principal_source = fixpp::session::bound_principal::source::CN,
                        });
                        live_peer_id_.reset();  // consume (one-shot)
                        record_state_transition_(fsm_state::Disconnected);
                        co_return fixpp::core::expected_t<void>{};
                    }
                    // Authorization succeeded: emit peer_identity_bound event.
                    // cn EMPTY: live_peer_id_.reset() frees backing store (UAF guard,
                    // matching the initiator arm pattern at :1900-1911).
                    emit_event(fixpp::session::session_event_peer_identity_bound{
                        .cn = {},
                        .sans = {},
                        .sha256_fingerprint = auth_pid.leaf_fingerprint,
                        .cipher = {},
                        .bound_compid = asserted_compid,
                        .principal_source = auth_r->from,
                    });
                    live_peer_id_.reset();  // consume (one-shot per Logon)
                } else if (is_mtls) {
                    // (2) mTLS + no peer_identity available → fail CLOSED.
                    // Peer_identity required for mTLS CompID binding but the live
                    // handshake identity is absent (happens-before violated, or a
                    // non-engine acceptor with no attach_accepted_transport call).
                    // Silent-admit here would bake a fail-open default. [triage RC#A]
                    const std::string_view asserted_compid = cfg_.target_comp_id;
                    emit_event(fixpp::session::session_event_compid_authorization_failed{
                        .cn = {},
                        .asserted_compid = asserted_compid,
                        .expected_compids = {},
                        .principal_source = fixpp::session::bound_principal::source::CN,
                    });
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }
                // (3) Non-mTLS (one_way_ca): no client cert → gate skipped.
            }

            // Valid Logon + in-seq: transition to LogonReceived, then emit
            // the acceptor's own Logon reply and transition to Active.
            // [spec.md FR-005 §US2 AC2; data-model.md:19 matrix row; F1 Round-A drift fix]
            // RC#B (gate-b/r1-green): gate the LogonReceived→Active transition on
            // successful reply build AND emit. Build/emit failure → Disconnected.
            // [009 spec.md FR-005; 005 data-model.md:19 matrix row "reply Logon, agreed
            // HeartBtInt"]
            record_state_transition_(fsm_state::LogonReceived);

            // Emit the acceptor reply Logon using the same admin-builder path
            // as the initiator's open() Logon. [spec.md FR-005 line 112]
            {
                std::array<std::byte, 256> reply_buf{};
                std::array<char, 32> reply_time_buf{};
                std::string_view reply_sending_time_view;
                if (effective_clock_) {
                    auto fmt_r = fixpp::session::stamp_sending_time(
                        effective_clock_->now(), cfg_.sending_time_precision,
                        std::span<char>{reply_time_buf.data(), reply_time_buf.size()});
                    if (fmt_r) {
                        reply_sending_time_view = std::string_view{fmt_r->data(), fmt_r->size()};
                    }
                }
                const int heartbt_sec = cfg_.heartbeat_interval.has_value()
                                            ? static_cast<int>(cfg_.heartbeat_interval->count())
                                            : 30;  // D-8 default 30 s

                // 024: the 013-only received-141 reset runs HERE (after check_inbound,
                // before the reply Logon) to keep next_inbound == 1 byte-identical to
                // pre-024 — check_inbound advanced inbound 1->2, this rewinds it to 1 while
                // the reply Logon consumes outbound 1->2 (reset_seqnum_policy_matrix acceptor
                // cells: {next_inbound=1, next_outbound=2}). The knob-driven reset already ran
                // BEFORE check_inbound; guarded on !reset_on_logon so the two are mutually
                // exclusive — exactly one store reset per path (C5.1 / witness 7). Logged
                // (I-07): a store failure is swallowed; the session still reaches Active
                // (witness 5, FR-001). [024 C2.6; FR-001; FR-017:150]
                if (peer_sent_reset && !cfg_.reset_on_logon) {
                    auto rst_r = co_await reset_seqnums_to_one_durable(reset_disposition::logged);
                    if (!rst_r) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return std::unexpected(rst_r.error());
                    }
                }
                // FR-018: emit the reset event once, after post-reset state is consistent,
                // when any reset happened (knob-driven OR received-141). by_peer_request
                // reflects whether the peer requested it via 141=Y.
                if (cfg_.reset_on_logon || peer_sent_reset) {
                    emit_event(fixpp::session::session_event_sequence_numbers_reset{
                        .by_peer_request = peer_sent_reset});
                }

                // RC#A (gate-b/r1-green): peek via manager (not bare field).
                // RC#B: gate the advance on build success; gate Active on emit success.
                // RC#C-2 (gate-b/r2): bilateral_lenient also mirrors 141=Y in reply —
                // it is the defining behavior of bilateral_lenient (FR-017:148-149).
                // unilateral: outbound 141 is config-driven, NOT mirror-driven (FR-017:149).
                // peer_sent_reset captured before this block from the seqnum-check block.
                // [spec.md FR-017: bilateral_strict + bilateral_lenient mirror 141=Y in reply]
                const bool acpt_reset_seqnum =
                    (cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict ||
                     cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_lenient) &&
                    peer_sent_reset;
                const seqnum_t reply_seq = seqnum_mgr_.peek_outbound();
                // 027 T013 I-NEX-1, E-OBO: acceptor reply is built AFTER check_inbound (`:1571`)
                // which already advanced next_inbound_. Advertise plain next_inbound_unsafe() —
                // NO +1 (E-OBO). Value is cause-dependent under 141 reset (data-model Reset table).
                // [contract C2, I-NEX-1, E-OBO]
                const std::optional<fixpp::session::seqnum_t> acpt_next_expected =
                    cfg_.enable_next_expected_msg_seq_num
                        ? std::optional<fixpp::session::seqnum_t>{seqnum_mgr_.next_inbound_unsafe()}
                        : std::nullopt;
                auto reply_logon = fixpp::session::build_logon(
                    std::span<std::byte>{reply_buf.data(), reply_buf.size()}, reply_seq,
                    cfg_.sender_comp_id, cfg_.target_comp_id, cfg_.begin_string, heartbt_sec,
                    reply_sending_time_view, acpt_reset_seqnum, acpt_next_expected);
                if (!reply_logon) {
                    // Build failed (oversized IDs → wire_frame_too_large).
                    // RC#B: must NOT reach Active — Disconnected, propagate error.
                    record_state_transition_(fsm_state::Disconnected);
                    co_return std::unexpected(reply_logon.error());
                }
                // Advance outbound counter through manager (RC#A: was ++next_outbound_seq_).
                auto assign_r = co_await seqnum_mgr_.assign_outbound();
                if (!assign_r) {
                    record_state_transition_(fsm_state::Disconnected);
                    co_return std::unexpected(assign_r.error());
                }
                // 019 T014: toAdmin before transmitting the acceptor reply Logon. [FR-008/010]
                if (!fire_to_admin_(*reply_logon)) {
                    record_state_transition_(fsm_state::Disconnected);
                    co_return std::unexpected(fixpp::core::error::app_callback_threw);
                }
                auto emit_r = co_await store_then_emit(reply_seq, *reply_logon);
                if (!emit_r) {
                    // Emit failed (transport error). RC#B: Disconnected, not Active.
                    record_state_transition_(fsm_state::Disconnected);
                    co_return std::unexpected(emit_r.error());
                }
            }

            // 027 T014/T021 — acceptor 789 honor (RC#4 ordering: AFTER reply store_then_emit).
            // Read peer's NextExpectedMsgSeqNum(789) from the inbound Logon.
            // N = peek_outbound() sampled here (post-reply-assign); messages [1,N-1] sent.
            // Integrity guard ordering (D-10): invalid-X FIRST, then X>N, then X<N.
            // [contract C4/C6, data-model I-NEX-2/3/4/9/11, D-6/D-10]
            if (cfg_.enable_next_expected_msg_seq_num && peer_789_present) {
                // peer_789_present: tag 789 was in the inbound Logon (even if value is empty).
                // A present-but-empty/unparseable 789 must be handled as invalid (D-10).
                const seqnum_t x789 = parse_seqnum(peer_789_raw);
                const seqnum_t n789 = seqnum_mgr_.peek_outbound();  // OUTBOUND (I-NEX-11)
                if (x789 == 0) {
                    // Present-but-invalid (parse→0: empty / non-digit / overflow).
                    // Evaluated FIRST (D-10): a parse→0 value must never reach the X<N branch
                    // (which would clamp begin to 1 and replay [1,N-1]).
                    // [contract C6, I-NEX-9, D-10]
                    {
                        std::array<std::byte, 256> lo_buf{};
                        const auto lo_st52 = effective_clock_
                            ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                            : SendingTimeStamp{};
                        const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
                        auto lo_result = fixpp::session::build_logout(
                            std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id,
                            "NextExpectedMsgSeqNum invalid",
                            cfg_.begin_string, lo_st52.value);
                        if (lo_result) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (assign_r) {
                                auto emit_r = co_await store_then_emit(lo_seq, *lo_result);
                                (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                            }
                        }
                    }
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                } else if (x789 > n789) {
                    // X > N: peer claims to have received frames we haven't sent yet.
                    // Sequence-integrity violation: Logout(text) + disconnect.
                    // [contract C6, I-NEX-4, D-6]
                    {
                        // Build "NextExpectedMsgSeqNum too high, expecting N but received X".
                        // Stack-only: use a fixed-size char buffer (N and X are seqnum_t ≤10 digits each).
                        char text_buf[80];
                        char* tp = text_buf;
                        const char* prefix = "NextExpectedMsgSeqNum too high, expecting ";
                        for (const char* p = prefix; *p; ++p) *tp++ = *p;
                        auto [n_end, n_ec] = std::to_chars(tp, text_buf + sizeof(text_buf) - 20, n789);
                        if (n_ec == std::errc{}) tp = n_end;
                        const char* mid = " but received ";
                        for (const char* p = mid; *p; ++p) *tp++ = *p;
                        auto [x_end, x_ec] = std::to_chars(tp, text_buf + sizeof(text_buf), x789);
                        if (x_ec == std::errc{}) tp = x_end;
                        const std::string_view text_sv{text_buf, static_cast<std::size_t>(tp - text_buf)};

                        std::array<std::byte, 256> lo_buf{};
                        const auto lo_st52 = effective_clock_
                            ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                            : SendingTimeStamp{};
                        const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
                        auto lo_result = fixpp::session::build_logout(
                            std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id, text_sv,
                            cfg_.begin_string, lo_st52.value);
                        if (lo_result) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (assign_r) {
                                auto emit_r = co_await store_then_emit(lo_seq, *lo_result);
                                (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                            }
                        }
                    }
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                } else if (x789 < n789) {
                    // X < N: proactively resend [X, N-1].
                    // Caller (this) owns the Disconnected transition on failure.
                    // [contract C4, I-NEX-2/3]
                    auto rr789 = co_await replay_outbound_range_(
                        x789, n789 - 1U, /*end_is_through_current=*/true);
                    if (!rr789) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return std::unexpected(rr789.error());
                    }
                }
                // X == N: in sync, no resend.
            }

            // Reply Logon successfully emitted: transition to Active.
            // T039/T041 (US3): seed last_inbound_steady_ and spawn liveness.
            if (effective_clock_) {
                last_inbound_steady_ = effective_clock_->steady_now();
            }
            record_state_transition_(fsm_state::Active);
            // 019 T016: if onLogon threw, terminal-close the session.
            if (lifecycle_cb_threw_) {
                lifecycle_cb_threw_ = false;
                co_await close(fixpp::session::close_mode::terminal);
                co_return std::unexpected(fixpp::core::error::app_callback_threw);
            }
            // Spawn liveness loop (same as initiator's LogonSent→Active path).
            {
                auto ex = co_await asio::this_coro::executor;
                liveness_counter_->fetch_add(1, std::memory_order_relaxed);
                // NOLINTNEXTLINE(misc-include-cleaner)
                asio::co_spawn(ex, run_liveness_loop(),
                               asio::bind_cancellation_slot(root_cancel_.slot(), asio::detached));
            }
            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::LogonReceived:
        case fsm_state::Active: {
            // T056 (US5) guard-precedence ordering:
            // (1) parse/type recognised → else session Reject (no-loop-guard)
            // (2) CompID/BeginString gate
            // (3) SendingTime MaxLatency (Q3)
            // (4) seqnum class
            // (5) message-type-for-state

            auto hdr = scan_frame_header(frame);

            // ── Guard (2): CompID/BeginString gate (scenarios 2i/2k) ──────────
            // Any mismatch → session-fatal → Disconnected.
            if (hdr.begin_string != cfg_.begin_string ||
                hdr.sender_comp_id != cfg_.target_comp_id ||
                hdr.target_comp_id != cfg_.sender_comp_id) {
                record_state_transition_(fsm_state::Disconnected);
                co_return fixpp::core::expected_t<void>{};
            }

            // ── Guard (3): SendingTime MaxLatency (Q3, T055/T056) ─────────────
            // Check |inbound_sending_time − effective_now| ≤ MaxLatency (D-8: 120 s).
            // No-reject-loop guard: Reject(35=3) and Logout(35=5) are exempt per I-5.
            // Established session: Reject(reason=10, refTag=52) → Logout → Disconnect.
            // FR-007: missing SendingTime (empty) → Reject-Logout-Disconnect.
            // FR-008: malformed SendingTime (parse failure) → Reject-Logout-Disconnect.
            // T016 [US3] RC#5: both empty AND parse-failure fall through to the path.
            // Note: the Logon-path special case (D-3) is handled in LogonSent below.
            if (hdr.msg_type != "3" && hdr.msg_type != "5" && effective_clock_) {
                // Determine if SendingTime is valid: present AND parseable AND in-range.
                bool sending_time_ok = false;
                if (!hdr.sending_time.empty()) {
                    auto parse_r = fixpp::core::fix_string_to_utc_time(
                        std::span<const char>{hdr.sending_time.data(), hdr.sending_time.size()});
                    if (parse_r) {
                        const auto max_lat = cfg_.sending_time_threshold.has_value()
                                                 ? std::chrono::duration_cast<std::chrono::seconds>(
                                                       *cfg_.sending_time_threshold)
                                                 : std::chrono::seconds{120};  // D-8 default 120 s
                        auto chk_st = fixpp::session::check_sending_time(
                            *parse_r, effective_clock_->now(), max_lat);
                        sending_time_ok = chk_st.has_value();
                    }
                    // parse failure: !parse_r → sending_time_ok stays false → path fires.
                }
                // sending_time absent (empty) → sending_time_ok stays false → path fires.

                if (!sending_time_ok) {
                    // Q3 established-session path: Reject(reason=10, refTag=52) → Logout →
                    // Disconnect.
                    const auto st52 =
                        stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
                    // Step 1: emit Reject(35=3, RefTagID=52, reason=10).
                    {
                        std::array<std::byte, 512> rj_buf{};
                        const seqnum_t ref_seq = parse_seqnum(hdr.msg_seq_num);
                        const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
                        auto rj_result = fixpp::session::build_reject(
                            std::span<std::byte>{rj_buf.data(), rj_buf.size()}, rj_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id, ref_seq,
                            52,  // RefTagID = 52 (SendingTime)
                            hdr.msg_type,
                            10,  // SessionRejectReason = 10 (SendingTime accuracy)
                            cfg_.begin_string, st52.value);
                        if (rj_result) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (!assign_r) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(assign_r.error());
                            }
                            auto emit_r = co_await store_then_emit(rj_seq, *rj_result);
                            (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                        }
                    }
                    // Step 2: emit Logout(35=5).
                    {
                        std::array<std::byte, 256> lo_buf{};
                        const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
                        auto lo_result = fixpp::session::build_logout(
                            std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id, {}, cfg_.begin_string,
                            st52.value);
                        if (lo_result) {
                            // 019 T014: toAdmin before transmitting Logout. [FR-008/010]
                            // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
                            if (!fire_to_admin_(*lo_result)) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(fixpp::core::error::app_callback_threw);
                            }
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (!assign_r) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(assign_r.error());
                            }
                            auto emit_r = co_await store_then_emit(lo_seq, *lo_result);
                            (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                        }
                    }
                    // Step 3: Disconnect.
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }
            }

            // ── Inbound SequenceReset(35=4) — Reset mode (GapFillFlag ≠ Y) ───
            // FIX-SL §4.8.6: a Reset-mode SequenceReset is processed REGARDLESS
            // of its own MsgSeqNum — it bypasses the seqnum gate (its purpose as
            // an admin recovery tool). Mirrors QuickFIX verify(msg, false, false).
            // GapFill mode (123=Y) is handled AFTER the gate (it IS subject to
            // ordering). [S-023; QuickFIX Session::nextSequenceReset]
            if (hdr.msg_type == "4" && hdr.gap_fill_flag != "Y") {
                // 019 T016 — fromAdmin completeness fix (FR-004):
                // Wire fromAdmin for inbound SequenceReset(Reset mode) AFTER the
                // FSM acts on it (apply_inbound_sequence_reset below applies the
                // new seqno). We fire BEFORE here since apply_inbound_sequence_reset
                // may co_return early on Reject — the message was accepted by the
                // FSM guards above and we fire consistent with US1 wiring (after
                // seqnum/FSM validation accepts). A fromAdmin reject emits
                // session Reject(35=3) per FR-005/D4.
                // [019-app-callbacks T016; FR-004; research D3/D4]
                if (engine_.application != nullptr) {
                    // kInboundParseArena: inbound frames may carry arbitrary payload.
                    auto cb_r =
                        parse_and_dispatch_(frame, kInboundParseArena, [&](auto& mv, auto& sid) {
                            return engine_.application->fromAdmin(mv, sid);
                        });
                    if (!cb_r && cb_r.error() == fixpp::core::error::app_callback_threw) {
                        co_await close(close_mode::terminal);
                        co_return std::unexpected(cb_r.error());
                    }
                    if (!cb_r) {
                        // fromAdmin reject → emit session Reject(35=3).
                        // Best-effort: proceed even if assign or emit fails
                        // (session still applies the SequenceReset below — co_return ok).
                        const seqnum_t rj_ref = parse_seqnum(hdr.msg_seq_num);
                        const auto rj_st52 =
                            effective_clock_
                                ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                                : SendingTimeStamp{};
                        const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
                        std::array<std::byte, 512> rj_buf{};
                        auto rj_r = fixpp::session::build_reject(
                            std::span<std::byte>{rj_buf.data(), rj_buf.size()}, rj_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id, rj_ref, 0, hdr.msg_type, 3,
                            cfg_.begin_string, rj_st52.value);
                        if (rj_r) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (assign_r) {
                                (void)co_await store_then_emit(rj_seq, *rj_r);
                            }
                        }
                        co_return fixpp::core::expected_t<void>{};
                    }
                }
                co_return co_await apply_inbound_sequence_reset(parse_seqnum(hdr.new_seqno),
                                                                parse_seqnum(hdr.msg_seq_num));
            }

            // ── Guard (4): seqnum check (T035 / 013 T026 AwaitingResend) ─────
            {
                const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
                if (seq == 0) {
                    // Cannot parse seq — session-fatal.
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }

                // 013 T026 FR-009: too-high inbound seqnum → AwaitingResend
                // (NOT Disconnected per 013 T006a amendment).
                // State owned by reconnect_fsm_ (data-model §E-1 / T023 Fix1).
                // [spec.md FR-009; data-model.md §E-1; plan.md T026]
                //
                // 027 T017 (confirm/review): this arm is NOT the primary 789 honor site
                // (the Logon-path honor runs in the NotConnected and LogonSent handlers).
                // It stays active as the recovery-of-last-resort for a lost proactive
                // resend: if the peer's 789-driven resend is dropped, the next inbound
                // frame triggers this arm and issues a ResendRequest (I-NEX-10 / D-11).
                // Knob-off path byte-identical. A future suppression of this arm would
                // create a never-recover hole — see L-027-2. No behavioural change.
                // [data-model I-NEX-10, research D-11]
                const seqnum_t next_expected = seqnum_mgr_.next_inbound_unsafe();
                if (seq > next_expected && !reconnect_fsm_.is_awaiting_resend()) {
                    // Too-high: enter AwaitingResend and emit ResendRequest(2).
                    // reconnect_fsm_.enter_awaiting_resend() owns state; we emit
                    // ResendRequest inline (requires seqnum_mgr_ + store_then_emit).
                    auto enter_r =
                        co_await reconnect_fsm_.enter_awaiting_resend(next_expected, seq - 1U);
                    (void)enter_r;  // state set; emit inline below

                    // Emit ResendRequest(2){BeginSeqNo=next_expected, EndSeqNo=0}
                    // via the shared admin builder into a stack buffer (no heap —
                    // [const §VIII.5]). EndSeqNo=0 → "through current" per FIX-SL §4.3.2.
                    std::array<std::byte, 256> rr_buf{};
                    const auto st52 =
                        effective_clock_
                            ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                            : SendingTimeStamp{};
                    const seqnum_t rr_seq = seqnum_mgr_.peek_outbound();
                    auto rr_result = fixpp::session::build_resend_request(
                        std::span<std::byte>{rr_buf.data(), rr_buf.size()}, rr_seq,
                        cfg_.sender_comp_id, cfg_.target_comp_id, next_expected, 0U,
                        cfg_.begin_string, st52.value);
                    if (rr_result) {
                        // 019 T014: toAdmin before transmitting ResendRequest. [FR-008/010]
                        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
                        if (!fire_to_admin_(*rr_result)) {
                            record_state_transition_(fsm_state::Disconnected);
                            co_return std::unexpected(fixpp::core::error::app_callback_threw);
                        }
                        auto assign_r = co_await seqnum_mgr_.assign_outbound();
                        if (!assign_r) {
                            record_state_transition_(fsm_state::Disconnected);
                        } else {
                            auto emit_r = co_await store_then_emit(rr_seq, *rr_result);
                            if (!emit_r) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(emit_r.error());
                            }
                        }
                    }
                    // Remain in Active (not Disconnected) per FR-009.
                    co_return fixpp::core::expected_t<void>{};
                }

                // 021 T008 Stage-1 — PossDup OrigSendingTime validation (Arms C/D/E).
                // Runs for any 43=Y non-SequenceReset frame, AFTER the too-high arm
                // (forward gaps still ResendRequest per engine parity — user decision
                // 2026-06-04) and BEFORE check_inbound, so at-expected/too-low malformed
                // dups are rejected without advancing the sequence number.
                // data-model.md §1 Stage 1 rows 0–4; contracts/session-possdup.md C1.
                // Arm E (row 0): 35=4 (SequenceReset) is exempt — guard below.
                if (hdr.poss_dup_flag == "Y" && hdr.msg_type != "4") {
                    if (hdr.orig_sending_time.empty()) {
                        // Arm C (row 2): OrigSendingTime(122) absent → Reject(35=3),
                        // 371=122 (RefTagID=OrigSendingTime), 373=1 (RequiredTagMissing).
                        // Session survives (no disconnect). data-model INV-3; research D4.
                        const auto st52_c =
                            effective_clock_
                                ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                                : SendingTimeStamp{};
                        const seqnum_t rj_ref_c = parse_seqnum(hdr.msg_seq_num);
                        const seqnum_t rj_seq_c = seqnum_mgr_.peek_outbound();
                        std::array<std::byte, 512> rj_buf_c{};
                        auto rj_r_c = fixpp::session::build_reject(
                            std::span<std::byte>{rj_buf_c.data(), rj_buf_c.size()}, rj_seq_c,
                            cfg_.sender_comp_id, cfg_.target_comp_id, rj_ref_c,
                            122,  // RefTagID = 122 (OrigSendingTime)
                            hdr.msg_type,
                            1,  // SessionRejectReason = 1 (RequiredTagMissing)
                            cfg_.begin_string, st52_c.value);
                        if (rj_r_c) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (!assign_r) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(assign_r.error());
                            }
                            auto emit_r = co_await store_then_emit(rj_seq_c, *rj_r_c);
                            (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                        }
                        // Arm C: survive — do NOT disconnect, do NOT advance seqnum.
                        co_return fixpp::core::expected_t<void>{};
                    }
                    // Arm D (row 3): parse 122 and 52; if BOTH parse and t122 > t52 (strict)
                    // → Reject(371=122, 373=10) + Logout + Disconnect.
                    // 122 == 52 (INV-4: equality) and 122 < 52 → validated, fall through.
                    // gate-b/r1 RC#1: if 122 is present (non-empty) but fails to parse →
                    // route to Arm C (Reject 371=122/373=1 RequiredTagMissing, session survives).
                    // Rationale: an unparseable 122 is unusable — morally identical to absent 122;
                    // Arm C (survive+reject) is consistent with SC-002 and the "present & valid"
                    // contract prose. data-model §1 row 2 (gate-b/r1 addendum); contracts C1
                    // gate-b/r1. Note: unparseable 52 while 122 parses is only reachable for 35=3/5
                    // (Guard-3 skips them); left as existing fall-through with a comment in that
                    // sub-case. data-model INV-4; research D5; reuses §1685-1737 pattern (RefTagID
                    // 52→122).
                    {
                        auto parse_122 = fixpp::core::fix_string_to_utc_time(std::span<const char>{
                            hdr.orig_sending_time.data(), hdr.orig_sending_time.size()});
                        // RC#1: present-but-unparseable 122 → Arm C (RequiredTagMissing, survive).
                        if (!parse_122) {
                            // 122 is non-empty (Arm C's empty-check above already handled empty),
                            // so it is present but malformed — treat as RequiredTagMissing.
                            const auto st52_rc1 =
                                effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                      cfg_.sending_time_precision)
                                                 : SendingTimeStamp{};
                            const seqnum_t rj_ref_rc1 = parse_seqnum(hdr.msg_seq_num);
                            const seqnum_t rj_seq_rc1 = seqnum_mgr_.peek_outbound();
                            std::array<std::byte, 512> rj_buf_rc1{};
                            auto rj_r_rc1 = fixpp::session::build_reject(
                                std::span<std::byte>{rj_buf_rc1.data(), rj_buf_rc1.size()},
                                rj_seq_rc1, cfg_.sender_comp_id, cfg_.target_comp_id, rj_ref_rc1,
                                122,  // RefTagID = 122 (OrigSendingTime)
                                hdr.msg_type,
                                1,  // SessionRejectReason = 1 (RequiredTagMissing)
                                cfg_.begin_string, st52_rc1.value);
                            if (rj_r_rc1) {
                                auto assign_r = co_await seqnum_mgr_.assign_outbound();
                                if (!assign_r) {
                                    record_state_transition_(fsm_state::Disconnected);
                                    co_return std::unexpected(assign_r.error());
                                }
                                auto emit_r = co_await store_then_emit(rj_seq_rc1, *rj_r_rc1);
                                (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                            }
                            co_return fixpp::core::expected_t<void>{};
                        }
                        auto parse_52 = fixpp::core::fix_string_to_utc_time(std::span<const char>{
                            hdr.sending_time.data(), hdr.sending_time.size()});
                        if (parse_122 && parse_52 && *parse_122 > *parse_52) {
                            // Arm D — strict 122 > 52.
                            const auto st52_d =
                                effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                      cfg_.sending_time_precision)
                                                 : SendingTimeStamp{};
                            // Step 1: emit Reject(35=3, 371=122, 373=10).
                            {
                                std::array<std::byte, 512> rj_buf{};
                                const seqnum_t rj_ref = parse_seqnum(hdr.msg_seq_num);
                                const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
                                auto rj_result = fixpp::session::build_reject(
                                    std::span<std::byte>{rj_buf.data(), rj_buf.size()}, rj_seq,
                                    cfg_.sender_comp_id, cfg_.target_comp_id, rj_ref,
                                    122,  // RefTagID = 122 (OrigSendingTime — research D5)
                                    hdr.msg_type,
                                    10,  // SessionRejectReason = 10 (SendingTimeAccuracyProblem)
                                    cfg_.begin_string, st52_d.value);
                                if (rj_result) {
                                    auto assign_r = co_await seqnum_mgr_.assign_outbound();
                                    if (!assign_r) {
                                        record_state_transition_(fsm_state::Disconnected);
                                        co_return std::unexpected(assign_r.error());
                                    }
                                    auto emit_r = co_await store_then_emit(rj_seq, *rj_result);
                                    (void)emit_r;
                                }
                            }
                            // Step 2: emit Logout(35=5).
                            {
                                std::array<std::byte, 256> lo_buf{};
                                const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
                                auto lo_result = fixpp::session::build_logout(
                                    std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq,
                                    cfg_.sender_comp_id, cfg_.target_comp_id, {}, cfg_.begin_string,
                                    st52_d.value);
                                if (lo_result) {
                                    if (!fire_to_admin_(*lo_result)) {
                                        record_state_transition_(fsm_state::Disconnected);
                                        co_return std::unexpected(
                                            fixpp::core::error::app_callback_threw);
                                    }
                                    auto assign_r = co_await seqnum_mgr_.assign_outbound();
                                    if (!assign_r) {
                                        record_state_transition_(fsm_state::Disconnected);
                                        co_return std::unexpected(assign_r.error());
                                    }
                                    auto emit_r = co_await store_then_emit(lo_seq, *lo_result);
                                    (void)emit_r;
                                }
                            }
                            // Step 3: Disconnect.
                            record_state_transition_(fsm_state::Disconnected);
                            co_return fixpp::core::expected_t<void>{};
                        }
                        // else: equal or 122 < 52 → validated, fall through.
                        // (parse failure of 52 while 122 parsed: only 35=3/5 reach here since
                        // Guard-3 kills malformed-52 for all other msg_types; leave as
                        // fall-through.)
                    }
                }
                // Stage-1 passed (or not a 43=Y non-35=4 frame). Proceed to check_inbound.

                // Too-low → session-fatal (not recoverable per I-4).
                // Exception: Heartbeat(0) with too-low seqnum is silently dropped
                // (no echo, no disconnect) to allow liveness-warmup passes to
                // not disrupt an otherwise healthy session. [T020-A warmup behavior]
                // in-seq → advance; too-high-while-awaiting → advance (it's a fill).
                auto chk = co_await seqnum_mgr_.check_inbound(seq);
                if (!chk) {
                    if (hdr.msg_type == "0") {
                        // Too-low Heartbeat: silently ignore (preserve Active, no echo).
                        co_return fixpp::core::expected_t<void>{};
                    }
                    // 021 T005 Stage-2 — Arm A: too-low possible-duplicate tolerance.
                    // data-model.md §1 Stage 2 rows 6/7/8; contracts/session-possdup.md C1.
                    // Guard: poss_dup_flag == "Y" (Stage-1 validation Arms C/D runs AFTER
                    // the too-high arm and BEFORE check_inbound; by the time we reach here
                    // the 122 value is already validated, so we do NOT re-validate it here).
                    if (hdr.poss_dup_flag == "Y") {
                        // Admin possdup: always silently ignore (row 6). No seqnum advance
                        // (check_inbound already returned false so no increment happened;
                        // INV-1). Session stays Active.
                        if (detail::is_admin_msgtype(hdr.msg_type)) {
                            co_return fixpp::core::expected_t<void>{};
                        }
                        // App possdup (rows 7/8): disposition governed by redeliver_poss_dup.
                        if (cfg_.redeliver_poss_dup && engine_.application != nullptr) {
                            // Row 8: redeliver opt-in — call fromApp with the original
                            // frame (which carries 43=Y, so fromApp sees it flagged possdup).
                            // Same invocation pattern as the in-sequence fromApp dispatch
                            // at ~session.cpp:2332-2338. No seqnum advance (INV-1).
                            auto cb_r = parse_and_dispatch_(
                                frame, kInboundParseArena, [&](auto& mv, auto& sid) {
                                    return engine_.application->fromApp(mv, sid);
                                });
                            if (!cb_r) {
                                if (cb_r.error() == fixpp::core::error::app_callback_threw) {
                                    co_await close(close_mode::terminal);
                                    co_return std::unexpected(cb_r.error());
                                }
                                // fromApp reject on redeliver: drop (no BusinessMessageReject
                                // — a too-low frame has no live seqnum slot to assign for the
                                // reply and the session spec does not mandate a reject here).
                            }
                        }
                        // Row 7 (redeliver=false) or post-redeliver: stay Active, no advance.
                        co_return fixpp::core::expected_t<void>{};
                    }
                    // Arm B: too-low non-Heartbeat without 43=Y — fatal (row 5).
                    // UNCHANGED, byte-identical to pre-feature (INV-2).
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }

                // Gap close check: if we filled through the gap endpoint, exit AwaitingResend.
                // reconnect_fsm_ owns AwaitingResend state per data-model §E-1 / T023 Fix1.
                if (reconnect_fsm_.is_awaiting_resend() &&
                    reconnect_fsm_.current_resend_state().outstanding_end > 0 &&
                    seqnum_mgr_.next_inbound_unsafe() >
                        reconnect_fsm_.current_resend_state().outstanding_end) {
                    reconnect_fsm_.exit_awaiting_resend();
                }
            }

            // ── Inbound SequenceReset(35=4) — GapFill mode (GapFillFlag = Y) ─
            // Subject to seqnum ordering: Guard 4 above advanced the counter by
            // 1 for the in-seq GapFill (MsgSeqNum = gap start); now jump it to
            // NewSeqNo(36) to skip the filled span and exit AwaitingResend.
            // Reset mode was handled before Guard 4. [S-023]
            if (hdr.msg_type == "4") {  // GapFillFlag == "Y" (Reset handled before gate)
                co_return co_await apply_inbound_sequence_reset(parse_seqnum(hdr.new_seqno),
                                                                parse_seqnum(hdr.msg_seq_num));
            }

            // T046 (US4): inbound Logout in Active/LogonReceived state.
            // Per data-model.md matrix:
            //   Active row:        inbound Logout → emit Logout, → Disconnected.
            //   LogonReceived row: inbound Logout → Disconnected ([FIX-SL §4.6]).
            // T019: emit session_event_sequence_numbers_reset{by_peer_request=false}
            //   when Active receives peer Logout (peer-initiated clean termination
            //   implies seqnum context is being reset at next Logon). [spec FR-018]
            if (hdr.msg_type == "5") {  // Logout (35=5)
                // 024 T013 (site b): mark that a peer Logout was received.
                // Set before any FSM action so close() can detect logout_seen_ even
                // if later teardown arrives via terminal close after this transition.
                // Both Active (emit-confirming-Logout → Disconnected) and LogonReceived
                // (Disconnected directly per [FIX-SL §4.6]) paths are covered.
                // [contracts/reset-knobs.md C3.1; plan.md Gate A convergence note (b)]
                logout_seen_ = true;

                if (fsm_state_ == fsm_state::Active) {
                    // Active → emit confirming Logout → Disconnected.
                    // Emit a confirming Logout via store_then_emit.
                    // Use a stack buffer (I-7: no heap on inbound-dispatch path).
                    std::array<std::byte, 256> buf{};
                    const auto st52 =
                        effective_clock_
                            ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                            : SendingTimeStamp{};
                    const seqnum_t logout_seq = seqnum_mgr_.peek_outbound();
                    auto logout_result = fixpp::session::build_logout(
                        std::span<std::byte>{buf.data(), buf.size()}, logout_seq,
                        cfg_.sender_comp_id, cfg_.target_comp_id, {}, cfg_.begin_string,
                        st52.value);
                    if (logout_result) {
                        // 019 T014: toAdmin before confirming Logout. [FR-008/010]
                        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
                        if (!fire_to_admin_(*logout_result)) {
                            record_state_transition_(fsm_state::Disconnected);
                            co_return std::unexpected(fixpp::core::error::app_callback_threw);
                        }
                        auto assign_r = co_await seqnum_mgr_.assign_outbound();
                        if (!assign_r) {
                            record_state_transition_(fsm_state::Disconnected);
                            co_return std::unexpected(assign_r.error());
                        }
                        auto emit_r = co_await store_then_emit(logout_seq, *logout_result);
                        (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                    }
                    // T019 FR-018: emit SessionEvent so operators can observe the
                    // logout-driven sequence-reset context (by_peer_request=false
                    // because it's an inbound Logout, not an inbound 141=Y reset).
                    emit_event(fixpp::session::session_event_sequence_numbers_reset{
                        .by_peer_request = false});
                }
                // 019 T016 — fromAdmin completeness fix (FR-004):
                // Wire fromAdmin for inbound Logout (35=5) AFTER the FSM acts
                // (confirming Logout emitted above). Fires on Active→Disconnected
                // transition, while still Active, consistent with US1 wiring.
                // A fromAdmin reject here emits session Reject(35=3) per FR-005/D4
                // but the session still disconnects (Logout has been confirmed).
                if (engine_.application != nullptr) {
                    // kInboundParseArena: inbound frames may carry arbitrary payload.
                    // T019 note: parse_and_dispatch_ drops callback_dispatch_scope
                    // before returning, so onLogout in record_state_transition_ below
                    // can acquire its own scope.
                    auto cb_r =
                        parse_and_dispatch_(frame, kInboundParseArena, [&](auto& mv, auto& sid) {
                            return engine_.application->fromAdmin(mv, sid);
                        });
                    if (!cb_r && cb_r.error() == fixpp::core::error::app_callback_threw) {
                        // throw from fromAdmin on Logout path: terminal close.
                        // onLogout will still fire in record_state_transition_ below.
                        record_state_transition_(fsm_state::Disconnected);
                        co_return std::unexpected(cb_r.error());
                    }
                    // fromAdmin reject on Logout: emit Reject(35=3) but still disconnect.
                    // Best-effort: proceed even if assign or emit fails (session disconnects
                    // regardless).
                    if (!cb_r) {
                        const seqnum_t rj_ref = parse_seqnum(hdr.msg_seq_num);
                        const auto rj_st52 =
                            effective_clock_
                                ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                                : SendingTimeStamp{};
                        const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
                        std::array<std::byte, 512> rj_buf{};
                        auto rj_r = fixpp::session::build_reject(
                            std::span<std::byte>{rj_buf.data(), rj_buf.size()}, rj_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id, rj_ref, 0, hdr.msg_type, 3,
                            cfg_.begin_string, rj_st52.value);
                        if (rj_r) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (assign_r) {
                                (void)co_await store_then_emit(rj_seq, *rj_r);
                            }
                        }
                    }
                }
                // Both Active and LogonReceived → Disconnected.
                // onLogout fires inside record_state_transition_ (INV-7 guard).
                record_state_transition_(fsm_state::Disconnected);
                co_return fixpp::core::expected_t<void>{};
            }

            // I-5: inbound Reject(35=3) is logged and accepted; never re-rejected.
            if (hdr.msg_type == "3") {  // Reject (35=3)
                // Active row: session-level log, no Reject-of-a-Reject (I-5).
                // Remain in current state.
                co_return fixpp::core::expected_t<void>{};
            }

            // T041 (US3): in the Active state, update liveness state and handle
            // liveness-specific message types (Heartbeat / TestRequest).
            if (fsm_state_ == fsm_state::Active) {
                // Update last_inbound_steady_ — used by run_liveness_loop to
                // detect inbound silence windows.
                if (effective_clock_) {
                    last_inbound_steady_ = effective_clock_->steady_now();
                }

                // ── 019 T011: fromAdmin dispatch for admin-typed messages ────
                // Called here (top of Active-only handling) for ALL admin MsgTypes
                // that reach this point after FSM + seqnum validation.
                // Note: Logout(35=5) and SequenceReset(35=4) return early ABOVE
                // this block; they are NOT dispatched to fromAdmin in this slice
                // (deferred to US3/US2 wiring). Heartbeat/TestRequest/ResendRequest/
                // Reject are dispatched here.
                // [research D3/D4; FR-004; INV-6]
                if (engine_.application != nullptr && detail::is_admin_msgtype(hdr.msg_type)) {
                    // kInboundParseArena: inbound frames may carry arbitrary payload.
                    // T019 note: parse_and_dispatch_ drops callback_dispatch_scope
                    // before returning, so onLogout in record_state_transition_ below
                    // can acquire its own scope.
                    auto cb_r =
                        parse_and_dispatch_(frame, kInboundParseArena, [&](auto& mv, auto& sid) {
                            return engine_.application->fromAdmin(mv, sid);
                        });
                    if (!cb_r) {
                        if (cb_r.error() == fixpp::core::error::app_callback_threw) {
                            co_await close(close_mode::terminal);
                            co_return std::unexpected(cb_r.error());
                        }
                        // fromAdmin reject → session Reject(35=3). (INV-4; D4)
                        // Disconnected-on-failure for assign_outbound + store_then_emit.
                        co_return co_await emit_session_reject_(parse_seqnum(hdr.msg_seq_num),
                                                                hdr.msg_type);
                    }
                }

                // T041 US3 / T018-D: Active row — inbound Heartbeat → liveness.
                // If we had an outstanding TestRequest:
                //   - inbound Heartbeat TestReqID matches ours → clear (TR answered)
                //   - inbound Heartbeat TestReqID does NOT match ours → mismatch →
                //     session_testreqid_mismatch(118) → Disconnected
                //     [spec.md FR-006; data-model.md §E-1; T018-D]
                // T020-A: echo every inbound Heartbeat with an outbound Heartbeat.
                if (hdr.msg_type == "0") {  // Heartbeat (35=0)
                    if (!pending_test_req_id_.empty()) {
                        // We have an outstanding TestRequest. Check echo.
                        if (!hdr.test_req_id.empty() && hdr.test_req_id != pending_test_req_id_) {
                            // TestReqID mismatch: peer sent a Heartbeat echoing a
                            // different (or stale) TestReqID than our outstanding one.
                            // session_testreqid_mismatch=118 → Disconnected.
                            // [spec.md FR-006; T018-D]
                            record_state_transition_(fsm_state::Disconnected);
                            co_return fixpp::core::expected_t<void>{};
                        }
                        // Matching or empty TestReqID: TR answered, clear flag.
                        pending_test_req_id_.clear();
                        unanswered_tr_ = false;
                    }
                    // T020-A: echo inbound Heartbeat with outbound Heartbeat reply.
                    // This confirms the steady-state path on the alloc-guard window.
                    // [spec.md US1 Heartbeat echo; T020-A behavioral gate]
                    {
                        std::array<std::byte, 256> hb_buf{};
                        const auto st52 =
                            effective_clock_
                                ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                                : SendingTimeStamp{};
                        const seqnum_t hb_seq = seqnum_mgr_.peek_outbound();
                        // Echo with the inbound TestReqID (if any), else empty.
                        auto hb_result = fixpp::session::build_heartbeat(
                            std::span<std::byte>{hb_buf.data(), hb_buf.size()}, hb_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id, hdr.test_req_id,
                            cfg_.begin_string, st52.value);
                        if (hb_result) {
                            // 019 T014: toAdmin before Heartbeat echo. [FR-008/010]
                            // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
                            if (!fire_to_admin_(*hb_result)) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(fixpp::core::error::app_callback_threw);
                            }
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (!assign_r) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(assign_r.error());
                            }
                            auto emit_r = co_await store_then_emit(hb_seq, *hb_result);
                            if (!emit_r) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(emit_r.error());
                            }
                        }
                    }
                    // Remain in Active — liveness tick.
                    co_return fixpp::core::expected_t<void>{};
                }

                // T041 US3 / T042 retirement (US4): Active row — inbound
                // TestRequest (35=1) → emit Heartbeat echoing TestReqID(112).
                if (hdr.msg_type == "1") {  // TestRequest (35=1)
                    std::array<std::byte, 256> hb_buf{};
                    const auto st52 =
                        effective_clock_
                            ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                            : SendingTimeStamp{};
                    const seqnum_t hb_seq = seqnum_mgr_.peek_outbound();
                    auto hb_result = fixpp::session::build_heartbeat(
                        std::span<std::byte>{hb_buf.data(), hb_buf.size()}, hb_seq,
                        cfg_.sender_comp_id, cfg_.target_comp_id, hdr.test_req_id,
                        cfg_.begin_string, st52.value);
                    if (hb_result) {
                        // 019 T014: toAdmin before Heartbeat reply. [FR-008/010]
                        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
                        if (!fire_to_admin_(*hb_result)) {
                            record_state_transition_(fsm_state::Disconnected);
                            co_return std::unexpected(fixpp::core::error::app_callback_threw);
                        }
                        auto assign_r = co_await seqnum_mgr_.assign_outbound();
                        if (!assign_r) {
                            // Overflow or closed: session-fatal per data-model.md:30 E3.
                            // Do NOT emit with unassigned seq — skip and disconnect.
                            record_state_transition_(fsm_state::Disconnected);
                            co_return std::unexpected(assign_r.error());
                        }
                        auto emit_r = co_await store_then_emit(hb_seq, *hb_result);
                        if (!emit_r) {
                            record_state_transition_(fsm_state::Disconnected);
                            co_return std::unexpected(emit_r.error());
                        }
                    }
                    // Remain in Active.
                    co_return fixpp::core::expected_t<void>{};
                }

                // 013 FR-010/FR-011/FR-012 [FIX-SL §4.3.5] — inbound
                // ResendRequest(2): reply by walking our outbound MessageStore.
                //   - stored application message → replay it with PossDupFlag(43)=Y
                //     + OrigSendingTime(122), keeping its ORIGINAL MsgSeqNum;
                //   - absent (pre-/post-store-horizon, internal gap) OR admin
                //     message → collapse the run into one SequenceReset-GapFill(4)
                //     {GapFillFlag(123)=Y, NewSeqNo(36)=<next live seq>} (admin
                //     replay forbidden, FR-011).
                // Resend-reply messages reuse the replayed sequence numbers and
                // are transmit-only — they do NOT advance the live outbound
                // counter and are not re-stored.
                if (hdr.msg_type == "2") {  // ResendRequest (35=2)
                    // 027 T006: delegate to replay_outbound_range_() — the
                    // extracted walk helper (T005). Caller owns the FSM
                    // Disconnected transition on any unexpected return.
                    // Two-value end model: rr_end==0 → end_is_through_current=true.
                    // [research D-5, contracts C3, data-model I-NEX-3]
                    const seqnum_t rr_begin = parse_seqnum(hdr.begin_seqno);
                    const seqnum_t rr_end = parse_seqnum(hdr.end_seqno);
                    auto replay_r = co_await replay_outbound_range_(
                        rr_begin, rr_end, /*end_is_through_current=*/(rr_end == 0));
                    if (!replay_r) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return std::unexpected(replay_r.error());
                    }
                    // Remain in Active after responding to ResendRequest.
                    co_return fixpp::core::expected_t<void>{};
                }

                // ── Guard (5): message-type-for-state (T056 US5) ─────────────
                // Session admin types silently passed-through in Active: 0/1/3/5
                // (Heartbeat / TestRequest / Reject / Logout — handled above OR
                // dispatched via the in-seq path). Any other MsgType in Active →
                // session-level Reject(35=3) with SessionRejectReason and RefMsgType.
                // Session stays Active. No-reject-loop: guard (type == "3" || type
                // == "5") exempted above.
                //
                // 010 F4 / W3.3-final fix (codex + QuickFIX-cpp + QuickFIX/J survey
                // 2026-05-23): "A" (dup-Logon) IS NOT in is_session_admin — per 005
                // data-model row 22 + FR-017 "never silent no-op" it must emit a Reject.
                // "2" (ResendRequest, 013 Phase 3 T015) and "4" (SequenceReset,
                // S-023 — both GapFill + Reset arms) are now handled above —
                // they no longer reach the Reject branch.
                // The "A" (dup-Logon) cell stays Reject per 005's intentional
                // defensive divergence from QuickFIX convention.
                {
                    // 019 T006: use the shared classifier (single source of truth).
                    // "A" (dup-Logon-in-Active) is deliberately EXCLUDED from
                    // is_admin_msgtype — it falls through to Reject per 005
                    // data-model row 22 / FR-017.
                    //
                    // 019 T011: app-accept branch.
                    // When engine_.application is registered, a non-admin MsgType
                    // in Active is an application message → route to fromApp instead
                    // of emitting the default Reject(35=3). This SUPPRESSES the
                    // default Reject for known app types when an Application exists.
                    // FR-014 byte-identity preserved: when application==nullptr the
                    // existing Reject path is unchanged. (research D8)
                    if (!detail::is_admin_msgtype(hdr.msg_type)) {
                        if (engine_.application == nullptr) {
                            // No Application registered — pre-019 behaviour: Reject.
                            // Unknown / app-type MsgType in Active →
                            // Reject(reason=session_msg_type_invalid_for_state=3).
                            // SessionRejectReason 3 = unsupported message type per [FIX-SL §4.5.4].
                            // Disconnected-on-failure for assign_outbound + store_then_emit.
                            co_return co_await emit_session_reject_(parse_seqnum(hdr.msg_seq_num),
                                                                    hdr.msg_type);
                        }
                        // else: Application registered → falls through to fromApp dispatch below.
                    }
                }

                // ── 019 T011: fromApp dispatch for app-typed messages ─────────
                // Reached ONLY when engine_.application != nullptr (guard above
                // lets app messages fall through only if Application is registered).
                // Admin messages are dispatched via fromAdmin at the top of the
                // Active block and return early; they never reach here.
                // FR-003; research D3/D4/D8; [const §VIII.5] (stack parse arena).
                if (engine_.application != nullptr) {
                    // kInboundParseArena: app frames may carry arbitrary payload.
                    // T019 note: parse_and_dispatch_ drops callback_dispatch_scope
                    // before returning. (FR-003; research D3/D4/D8; [const §VIII.5])
                    auto cb_r = parse_and_dispatch_(
                        frame, kInboundParseArena,
                        [&](auto& mv, auto& sid) { return engine_.application->fromApp(mv, sid); });
                    if (!cb_r) {
                        if (cb_r.error() == fixpp::core::error::app_callback_threw) {
                            co_await close(close_mode::terminal);
                            co_return std::unexpected(cb_r.error());
                        }
                        // fromApp reject → BusinessMessageReject(35=j). (D4; FR-005)
                        // NOT merged with the 35=3 Reject helper — different builder/fields.
                        const seqnum_t ref_seq = parse_seqnum(hdr.msg_seq_num);
                        const auto st52 =
                            effective_clock_
                                ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                                : SendingTimeStamp{};
                        const seqnum_t bmr_seq = seqnum_mgr_.peek_outbound();
                        std::array<std::byte, 512> bmr_buf{};
                        auto bmr_r = fixpp::session::build_business_message_reject(
                            std::span<std::byte>{bmr_buf.data(), bmr_buf.size()}, bmr_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id, ref_seq,
                            hdr.msg_type,  // RefMsgType(372)
                            0,             // BusinessRejectReason(380) = Other
                            cfg_.begin_string, st52.value);
                        if (bmr_r) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (!assign_r) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(assign_r.error());
                            }
                            auto emit_r = co_await store_then_emit(bmr_seq, *bmr_r);
                            if (!emit_r) {
                                record_state_transition_(fsm_state::Disconnected);
                                co_return std::unexpected(emit_r.error());
                            }
                        }
                    }
                    // If parse fails (cb_r == ok from parse_and_dispatch_):
                    // frame accepted for seqnum; session stays Active.
                }
            }

            // In-sequence: counter advanced. Remain in current state.
            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::LogoutSent: {
            // Per matrix LogoutSent row:
            //   inbound Logout → Disconnected (confirm)
            //   all other inbound → (drained) — silently accepted, no FSM change
            //     (seqnum NOT advanced, no fromAdmin/fromApp dispatch)
            auto hdr = scan_frame_header(frame);
            if (hdr.msg_type == "5") {  // Logout(35=5) confirms our Logout
                record_state_transition_(fsm_state::Disconnected);
                logout_confirmed_ = true;  // signal run_logout_phase1 coroutine
                // Wake up the sleep_until in run_logout_phase1 so it can
                // detect the confirmation and return early (before the 2s timeout).
                if (effective_clock_) {
                    effective_clock_->cancel_sleeps();
                }
            }
            // All other inbound frames: drained (no FSM change, no seqnum advance).
            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::LogonSent: {
            // Initiator path: Logon emitted, awaiting peer Logon ack.
            // Per data-model.md matrix LogonSent row:
            //   inbound Logon (valid)            → Active (validate HeartBtInt/CompID/BeginString)
            //   inbound Logon (refused)          → Disconnected
            //   inbound Logout                   → Disconnected ([FIX-SL §4.6])
            //   inbound Heartbeat/TR/Reject/oos  → session-fatal Logout+disconnect
            //   seqnum too-low / too-high        → fatal Logout(text)+disconnect
            //
            // Phase 4 (US2) scope: emit-Logout-then-Disconnected is deferred to
            // US4/Phase 6 (when the Logout build/send path is wired); for now
            // we transition directly to Disconnected on the fatal/refusal cells,
            // matching the same pattern Phase 3 used for NotConnected refusals.
            auto result = fixpp::session::interpret_logon(frame, cfg_.target_comp_id,
                                                          cfg_.sender_comp_id, cfg_.begin_string);

            if (!result) {
                // Either a refused Logon (CompID/BeginString) OR a non-Logon
                // inbound (Heartbeat/TestRequest/Reject/out-of-scope admin /
                // invalid MsgType). Per matrix LogonSent row: every one of
                // these cells transitions to Disconnected (with Logout in US4).
                record_state_transition_(fsm_state::Disconnected);
                co_return fixpp::core::expected_t<void>{};
            }

            // Valid Logon-ack shape: scan header fields (SendingTime + seqnum).
            auto hdr = scan_frame_header(frame);

            // ── Guard (3): SendingTime MaxLatency — Logon-path special case ───
            // D-3 / FR-009 / RC#5: if the inbound Logon's SendingTime is absent,
            // malformed, or stale, emit Logout-with-error only (NO standalone Reject —
            // no session established yet per [FIX-SL §4.3]).
            // FR-009: empty OR parse-failure OR stale → all go to Logout-only path.
            // T018 [US3]: remove lenient fall-through for missing/malformed SendingTime.
            if (effective_clock_) {
                std::string_view sending_time_error;
                if (hdr.sending_time.empty()) {
                    sending_time_error = "SendingTime(52) missing";
                } else {
                    auto parse_r = fixpp::core::fix_string_to_utc_time(
                        std::span<const char>{hdr.sending_time.data(), hdr.sending_time.size()});
                    if (!parse_r) {
                        sending_time_error = "SendingTime(52) malformed";
                    } else {
                        const auto max_lat = cfg_.sending_time_threshold.has_value()
                                                 ? std::chrono::duration_cast<std::chrono::seconds>(
                                                       *cfg_.sending_time_threshold)
                                                 : std::chrono::seconds{120};  // D-8 default 120 s
                        auto chk_st = fixpp::session::check_sending_time(
                            *parse_r, effective_clock_->now(), max_lat);
                        if (!chk_st) {
                            sending_time_error = "SendingTime(52) accuracy";
                        }
                    }
                }

                if (!sending_time_error.empty()) {
                    // Logon-path Q3: emit Logout only (no standalone Reject — D-3).
                    std::array<std::byte, 256> lo_buf{};
                    const auto st52 =
                        stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
                    const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
                    auto lo_result = fixpp::session::build_logout(
                        std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq,
                        cfg_.sender_comp_id, cfg_.target_comp_id, sending_time_error,
                        cfg_.begin_string, st52.value);
                    if (lo_result) {
                        auto assign_r = co_await seqnum_mgr_.assign_outbound();
                        if (!assign_r) {
                            record_state_transition_(fsm_state::Disconnected);
                            co_return std::unexpected(assign_r.error());
                        }
                        auto emit_r = co_await store_then_emit(lo_seq, *lo_result);
                        (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                    }
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }
            }

            // ── Guard (4): seqnum check (T035 LogonSent row) ─────────────────
            const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
            if (seq == 0) {
                record_state_transition_(fsm_state::Disconnected);
                co_return fixpp::core::expected_t<void>{};
            }

            auto chk = co_await seqnum_mgr_.check_inbound(seq);
            if (!chk) {
                // 027 T016 — behind-side tolerance (formulation A, I-NEX-5/D-7):
                // When the knob is on AND the failure is too-high (NOT too-low),
                // do NOT take the fatal branch. Leave next_inbound_ at X. Emit no
                // at-logon ResendRequest. Proceed toward Active so the peer's
                // proactive resend [X, peer_N-1] is admitted in-sequence.
                // [contract C5, data-model I-NEX-5/12, research D-7]
                //
                // Too-low OR knob off: fatal exactly as today.
                if (cfg_.enable_next_expected_msg_seq_num &&
                    chk.error() == fixpp::core::error::session_seqnum_too_high) {
                    // Behind-side: tolerate. Fall through to Active transition below.
                } else {
                    // Too-low or too-high with knob off → fatal (I-2/I-4).
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }
            }

            // RC#C (gate-b/r1): bilateral_strict initiator path — symmetric to acceptor.
            // We sent 141=Y in our outbound Logon (see open() above). If the peer's
            // Logon-ack omits 141=Y, reject with session_seqnum_reset_mismatch(116).
            // [spec.md FR-017; triage RC#C(b); [[feedback_half_restructure_symmetric_api]]]
            {
                const bool peer_ack_sent_reset = (hdr.reset_seqnum_flag == "Y");
                if (!peer_ack_sent_reset &&
                    cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict) {
                    record_state_transition_(fsm_state::Disconnected);
                    co_return std::unexpected(fixpp::core::error::session_seqnum_reset_mismatch);
                }
                if (peer_ack_sent_reset) {
                    // RC#C-1 (gate-b/r2): reset live counters + store before event.
                    // FR-017:150: mutual reset → both sides advance to 1.
                    // FR-018: event fires AFTER post-reset state is consistent.
                    // [[feedback_half_restructure_symmetric_api]]: symmetric to acceptor arm.
                    auto rst_r = co_await seqnum_mgr_.reset_to_one();
                    if (!rst_r) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return std::unexpected(rst_r.error());
                    }
                    if (store_) {
                        auto store_rst_r = co_await (*store_).reset();
                        (void)store_rst_r;  // store_io_failure → logged-then-proceed (I-07)
                    }
                    // FR-018 mode mapping: bilateral_strict initiator-confirm path →
                    // WE sent 141=Y first, peer confirmed → by_peer_request=false (WE initiated).
                    // bilateral_lenient / unilateral: WE did NOT send 141=Y, peer sent it →
                    // by_peer_request=true (peer initiated).
                    const bool we_initiated =
                        (cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict);
                    emit_event(fixpp::session::session_event_sequence_numbers_reset{
                        .by_peer_request = !we_initiated});
                }
            }

            // 014 T015 US2 / 013 T036 US2: CompID authorization BEFORE FSM
            // transition to Active (FR-006/FR-007/FR-008/FR-019/FR-020/FR-021/FR-024
            // symmetric initiator path).
            // Initiator: asserted CompID = peer SenderCompID(49) = cfg_.target_comp_id.
            //
            // Two-arm guard (015 T020 removed the seam arm from the T015 form):
            //   (1) live_peer_id_ set (live reconnect path, T014/T015) →
            //       authorize with the real handshake peer_id, making the
            //       already-fail-CLOSED mTLS gate *operable* with a live identity
            //       (admit on-list; fail-close off-list/absent). FR-006/FR-008.
            //   (2) mTLS + no identity available → fail CLOSED. RC#A.
            //   (3) Non-mTLS → skip (backward compat, permissive). FR-019.
            //
            // live_peer_id_ is stored by install_reconnected_transport (T015) on a
            // successful reconnect handshake and reset() one-shot below once this
            // guard authorizes it. The acceptor site now binds a live identity too
            // (T020/T-041 CLOSED) — both roles are symmetric, the seam is gone.
            // [[feedback_half_restructure_symmetric_api]]: symmetric one-pass fix.
            // [data-model §E-2; contracts C2; FR-006; FR-007; FR-008; FR-009]
            {
                const bool is_mtls =
                    cfg_.security_profile.k == fixpp::session::SecurityProfile::kind::mtls_ca ||
                    cfg_.security_profile.k == fixpp::session::SecurityProfile::kind::mtls_pinned;

                if (live_peer_id_.has_value() && is_mtls) {
                    // (1) Live reconnect path: use real handshake peer_id.
                    // Only active when mTLS is configured (binding gate applicable)
                    // AND a live peer_id was set by install_reconnected_transport.
                    // Non-mTLS sessions skip to arm (4) permissive (no client cert).
                    // FR-006: the identity source is the real handshake_result.peer_id
                    // (no fabricated/stand-in identity on this path). FR-008: removes
                    // the residual fabricated auth payload from the live path.
                    const fixpp::tls::peer_identity& auth_pid = *live_peer_id_;
                    const std::string_view asserted_compid = cfg_.target_comp_id;
                    auto auth_r =
                        cfg_.compid_authorization_policy.authorize(auth_pid, asserted_compid);
                    if (!auth_r) {
                        // Fail-closed: emit event, Disconnected.
                        // On the open-Logon path (not reconnect), Disconnected
                        // is terminal. The reconnect-path auth fail is handled in
                        // reconnect_fsm.cpp step 7 BEFORE reaching here; this arm
                        // fires only if the open-Logon path somehow has a live peer_id
                        // (future: or if this guard is reached from the reconnect path
                        // after an auth-pass in the FSM — should not happen, but
                        // fail-closed is the safe default). [contracts C2; FR-007]
                        // cn EMPTY: live_peer_id_.reset() below frees the backing
                        // store, so a view into it would dangle in the persisted
                        // recent_events_ ring. (Owned-cn fix + the success-arm cn
                        // lifetime are tracked in the 014 verify doc.)
                        emit_event(fixpp::session::session_event_compid_authorization_failed{
                            .cn = {},
                            .asserted_compid = asserted_compid,
                            .expected_compids = {},
                            .principal_source = fixpp::session::bound_principal::source::CN,
                        });
                        live_peer_id_.reset();  // consume the live identity (one-shot)
                        record_state_transition_(fsm_state::Disconnected);
                        co_return fixpp::core::expected_t<void>{};
                    }
                    // Authorization succeeded: emit peer_identity_bound event.
                    // cn EMPTY: live_peer_id_.reset() below frees the backing store;
                    // owned-cn deferred — see verify doc. sha256_fingerprint (owned
                    // std::array) and bound_compid (config-stable) are safe. Matches
                    // the failure-arm precedent (gate-b/r2 FQ-2).
                    emit_event(fixpp::session::session_event_peer_identity_bound{
                        .cn = {},
                        .sans = {},
                        .sha256_fingerprint = auth_pid.leaf_fingerprint,
                        .cipher = {},
                        .bound_compid = asserted_compid,
                        .principal_source = auth_r->from,
                    });
                    live_peer_id_.reset();  // consume (one-shot per Logon-ack)
                } else if (is_mtls) {
                    // (2) mTLS + no peer_identity → fail CLOSED (same as acceptor arm).
                    const std::string_view asserted_compid = cfg_.target_comp_id;
                    emit_event(fixpp::session::session_event_compid_authorization_failed{
                        .cn = {},
                        .asserted_compid = asserted_compid,
                        .expected_compids = {},
                        .principal_source = fixpp::session::bound_principal::source::CN,
                    });
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }
                // (4) Non-mTLS (one_way_ca): no client cert → gate skipped.
            }

            // T-041 CLOSED (015 US4): the acceptor guard above now binds the live
            // handshake identity from attach_accepted_transport and fails CLOSED
            // symmetrically; the per-config peer-identity test seam is removed in
            // production AND tests (T020/T021, SC-006/FR-009). Both roles bind a
            // real identity — no asymmetry remains. [FR-008/009; data-model §E-2; C2]

            // Valid Logon-ack + in-seq → Active (initiator handshake complete).
            record_state_transition_(fsm_state::Active);
            // 019 T016: if onLogon threw, terminal-close the session.
            if (lifecycle_cb_threw_) {
                lifecycle_cb_threw_ = false;
                co_await close(fixpp::session::close_mode::terminal);
                co_return std::unexpected(fixpp::core::error::app_callback_threw);
            }

            // T041 (US3): seed last_inbound_steady_ from this Logon-ack.
            if (effective_clock_) {
                last_inbound_steady_ = effective_clock_->steady_now();
            }

            // T039/T041 (US3): co_spawn the liveness loop on the session executor
            // with the root cancellation slot so Session::close() can cancel it.
            // asio::bind_cancellation_slot threads the root slot into the spawned
            // coroutine, overriding the default terminal-only slot
            // ([feedback_asio_cospawn_total_cancellation_default]).
            {
                auto ex = co_await asio::this_coro::executor;
                liveness_counter_->fetch_add(1, std::memory_order_relaxed);
                // asio::co_spawn provided by <asio/co_spawn.hpp> at the top of this file;
                // clang-tidy doesn't see the include through template machinery.
                // NOLINTNEXTLINE(misc-include-cleaner)
                asio::co_spawn(ex, run_liveness_loop(),
                               asio::bind_cancellation_slot(root_cancel_.slot(), asio::detached));
            }

            // 027 T015/T021 — initiator 789 honor (after processing the Logon-ack).
            // Our own Logon was already sent at open()/emit_initiator_logon_() (:601).
            // hdr is at case scope (scanned at :2665); next_expected_msg_seq_num valid.
            // N = peek_outbound() sampled here (post-Active, post-Logon-ack processing).
            // Integrity guard ordering (D-10): invalid-X FIRST, then X>N, then X<N.
            // [contract C4/C6/C8, data-model I-NEX-2/3/4/9/11, D-6/D-10]
            if (cfg_.enable_next_expected_msg_seq_num && hdr.next_expected_present) {
                // hdr.next_expected_present: tag 789 was in the Logon-ack (even if value is empty).
                // A present-but-empty/unparseable 789 must be handled as invalid (D-10).
                const seqnum_t x789 = parse_seqnum(hdr.next_expected_msg_seq_num);
                const seqnum_t n789 = seqnum_mgr_.peek_outbound();  // OUTBOUND (I-NEX-11)
                if (x789 == 0) {
                    // Present-but-invalid (parse→0: empty / non-digit / overflow).
                    // Evaluated FIRST (D-10): must never reach the X<N branch.
                    // [contract C6, I-NEX-9, D-10]
                    {
                        std::array<std::byte, 256> lo_buf{};
                        const auto lo_st52 = effective_clock_
                            ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                            : SendingTimeStamp{};
                        const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
                        auto lo_result = fixpp::session::build_logout(
                            std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id,
                            "NextExpectedMsgSeqNum invalid",
                            cfg_.begin_string, lo_st52.value);
                        if (lo_result) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (assign_r) {
                                auto emit_r = co_await store_then_emit(lo_seq, *lo_result);
                                (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                            }
                        }
                    }
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                } else if (x789 > n789) {
                    // X > N: sequence-integrity violation — Logout(text) + disconnect.
                    // [contract C6, I-NEX-4, D-6]
                    {
                        char text_buf[80];
                        char* tp = text_buf;
                        const char* prefix = "NextExpectedMsgSeqNum too high, expecting ";
                        for (const char* p = prefix; *p; ++p) *tp++ = *p;
                        auto [n_end, n_ec] = std::to_chars(tp, text_buf + sizeof(text_buf) - 20, n789);
                        if (n_ec == std::errc{}) tp = n_end;
                        const char* mid = " but received ";
                        for (const char* p = mid; *p; ++p) *tp++ = *p;
                        auto [x_end, x_ec] = std::to_chars(tp, text_buf + sizeof(text_buf), x789);
                        if (x_ec == std::errc{}) tp = x_end;
                        const std::string_view text_sv{text_buf, static_cast<std::size_t>(tp - text_buf)};

                        std::array<std::byte, 256> lo_buf{};
                        const auto lo_st52 = effective_clock_
                            ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                            : SendingTimeStamp{};
                        const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
                        auto lo_result = fixpp::session::build_logout(
                            std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq,
                            cfg_.sender_comp_id, cfg_.target_comp_id, text_sv,
                            cfg_.begin_string, lo_st52.value);
                        if (lo_result) {
                            auto assign_r = co_await seqnum_mgr_.assign_outbound();
                            if (assign_r) {
                                auto emit_r = co_await store_then_emit(lo_seq, *lo_result);
                                (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
                            }
                        }
                    }
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                } else if (x789 < n789) {
                    // X < N: proactively resend [X, N-1].
                    // Caller (this) owns the Disconnected transition on failure.
                    // [contract C4/C8, I-NEX-2/3]
                    auto rr789 = co_await replay_outbound_range_(
                        x789, n789 - 1U, /*end_is_through_current=*/true);
                    if (!rr789) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return std::unexpected(rr789.error());
                    }
                }
                // X == N: in sync, no resend.
            }

            co_return fixpp::core::expected_t<void>{};
        }

        case fsm_state::Disconnected:
            // Disconnected row: all inbound cells `ignored` per matrix.
            co_return fixpp::core::expected_t<void>{};
    }

    co_return fixpp::core::expected_t<void>{};
}

// T008 (US1 / FR-001): Session::send outbound pipeline.
//
// Pipeline per FR-001 + [2e §4.1] durable-before-transmit:
//   (1) stamp SendingTime(52) from effective_clock.now();
//   (2) assign outbound MsgSeqNum(34) via seqnum_mgr_.assign_outbound() (RC#A);
//   (3) build the framed wire bytes into a stack buffer ([const §VIII.5] — no heap);
//   (4) store_then_emit (I-3): store(outbound) BEFORE transport_send_.
//
// Frame layout: 8=<begin_string>\x01 9=<NNN>\x01 34=<seq>\x01 49=<sender>\x01
//               52=<time>\x01 56=<target>\x01 <app_payload> 10=<CCC>\x01
//
// BodyLength (9=): bytes from after "9=NNN\x01" through end of last field before "10=".
// CheckSum (10=): byte-sum mod 256 over all bytes from start through end of "10=CCC\x01" body.
// Per [FIX-SL §4.2]: 9= and 10= computed here; all other session fields stamped inline.
//
// Stack buffer: 4096 bytes. app_payload larger than ~3800 bytes returns wire_frame_too_large.
// [const §VIII.5]: no heap allocation on this path.
asio::awaitable<fixpp::core::expected_t<void>> Session::send(
    std::span<const std::byte> app_payload) noexcept {
    using fixpp::core::error;

    // FR-005 / D-3: FSM precondition — Session::send is only valid in Active.
    // spec.md US1 ACs all premise Active; sending while in LogonSent/NotConnected/
    // LogonReceived/LogoutSent/Disconnected is a programmer error.
    // Returns session_invalid_state_for_send (=77) — not session_invalid_logon —
    // to give the caller a semantically distinct diagnosis. [FR-005 / D-3]
    if (fsm_state_ != fsm_state::Active) {
        co_return std::unexpected(error::session_invalid_state_for_send);
    }

    // F5 (Round-A drift): wrap the entire send body in try/catch to absorb
    // asio::system_error{operation_aborted} thrown when the async_mutex awaitable
    // is cancelled (e.g. Session::close() fires root_cancel_ while a send is in flight).
    // The noexcept window on this coroutine must never let an uncaught exception
    // propagate (std::terminate). [F5 drift fix;
    // [[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]]
    try {
        auto impl_r = co_await send_impl(app_payload);
        // F9 (Round-A drift): if store_then_emit converted an operation_aborted throw
        // into dispatch_aborted expected_t error, transition to Disconnected per US1 AC3.
        if (!impl_r && impl_r.error() == error::dispatch_aborted) {
            record_state_transition_(fsm_state::Disconnected);  // [spec.md US1 AC3; F9 drift fix]
        }
        co_return impl_r;
    } catch (const asio::system_error& e) {
        if (e.code() == asio::error::operation_aborted) {
            // Uncaught operation_aborted from a co_await inside send_impl —
            // transition to Disconnected per US1 AC3. [spec.md US1 AC3; F5+F9 drift fix]
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(error::dispatch_aborted);
        }
        // Unexpected system_error — still transition to Disconnected (I-09).
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(error::dispatch_aborted);
    } catch (...) {
        // Unexpected exception from send_impl — transition to Disconnected (I-09).
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(error::dispatch_aborted);
    }
}

// send_impl: the actual send pipeline, called from the noexcept wrapper above.
// May throw asio::system_error on cancellation of the store awaitable.
// Separated so the outer noexcept wrapper can catch and convert to expected_t.
// [F5 Round-A drift fix: noexcept-throw trap separation]
//
// ── 020 T010: Opaque-payload validation (FR-016; INV-8; research.md D1) ──────
// Validate app_payload BEFORE stamping SendingTime, peeking seqnum, or building
// the frame. Any rejection returns app_payload_malformed (131) with NO seqnum
// consumption, NO transmit.
//
// Validation rules (boundary-aware token matching):
//   (1) payload must not be empty;
//   (2) payload must begin with the bytes '3','5','=' (i.e. "35=" at offset 0);
//   (3) no DUPLICATE 35= field (scan for SOH+"35=");
//   (4) no embedded session header/trailer tag at a field boundary:
//       tags 8, 9, 34, 49, 52, 56, 10 — matched as "<tag>=" ONLY at a
//       field boundary (start of payload or right after SOH), so "54=" / "150="
//       / "134=" do NOT false-match.
//
// Helper: returns true if the byte-string `sv` contains the token `tok` at any
// field boundary (start of sv, or preceded by \x01).  noexcept, no allocation.
// [020-g2-business-messages T010; research.md D1 "Opaque-payload validation"]
static bool has_boundary_token(std::string_view sv, std::string_view tok) noexcept {
    if (tok.size() > sv.size()) return false;
    // Check at offset 0 (start of payload is a field boundary).
    if (sv.starts_with(tok)) return true;
    // Scan for \x01 followed by tok.
    for (std::size_t i = 1; i + tok.size() <= sv.size(); ++i) {
        if (sv[i - 1] == '\x01' && sv.substr(i, tok.size()) == tok) return true;
    }
    return false;
}

asio::awaitable<fixpp::core::expected_t<void>> Session::send_impl(
    std::span<const std::byte> app_payload) {
    using fixpp::core::error;

    // ── T010: Opaque-payload validation — BEFORE seqnum peek/assign/stamp ──────
    // [020-g2 research.md D1; data-model.md INV-8; FR-016]
    {
        // Cast to string_view for boundary-token scanning. No allocation.
        std::string_view pv{reinterpret_cast<const char*>(app_payload.data()), app_payload.size()};

        // (1) Empty payload is malformed.
        if (pv.empty()) {
            co_return std::unexpected(error::app_payload_malformed);
        }

        // (2) Payload must lead with "35=".
        if (pv.size() < 3 || pv[0] != '3' || pv[1] != '5' || pv[2] != '=') {
            co_return std::unexpected(error::app_payload_malformed);
        }

        // (2a) Payload must end with SOH so the last field is terminated before
        //      checksum append. A trailing-SOH-less payload would cause the checksum
        //      field to be appended without a field boundary. [RC#2: gate-b/r1]
        // cppcheck-suppress containerOutOfBounds  // FP: pv.empty() guard at :3101 returns first
        if (pv.back() != '\x01') {
            co_return std::unexpected(error::app_payload_malformed);
        }

        // (2b) MsgType value must be non-empty: the first SOH must be at offset > 3
        //      (i.e. at least one byte between "35=" and the SOH terminator).
        //      "35=\x01" has first_soh==3 → empty MsgType → malformed. [RC#2: gate-b/r1]
        {
            const std::size_t fst = pv.find('\x01');
            // fst != npos is guaranteed because pv.back() == '\x01' was verified above.
            if (fst <= 3U) {
                co_return std::unexpected(error::app_payload_malformed);
            }
        }

        // (3) Duplicate 35=: a second 35= at a field boundary (after the leading one).
        // The leading "35=" is at offset 0; look for any further SOH+"35=".
        bool has_dup_35 = false;
        for (std::size_t i = 1; i + 3 <= pv.size(); ++i) {
            if (pv[i - 1] == '\x01' && pv[i] == '3' && pv[i + 1] == '5' && pv[i + 2] == '=') {
                has_dup_35 = true;
                break;
            }
        }
        if (has_dup_35) {
            co_return std::unexpected(error::app_payload_malformed);
        }

        // (4) Embedded session header/trailer tags at field boundaries.
        // Tags: 8=, 9=, 34=, 49=, 52=, 56=, 10=.
        // Use has_boundary_token; the leading "35=" is allowed and already verified.
        // Note: "8=" must match only as a complete field tag (not e.g. inside "38=").
        // has_boundary_token ensures boundary-context.
        if (has_boundary_token(pv, "8=") || has_boundary_token(pv, "9=") ||
            has_boundary_token(pv, "34=") || has_boundary_token(pv, "49=") ||
            has_boundary_token(pv, "52=") || has_boundary_token(pv, "56=") ||
            has_boundary_token(pv, "10=")) {
            co_return std::unexpected(error::app_payload_malformed);
        }
    }

    // ── 022 T008+T009: Per-field scanner + AllowPosDup excision ─────────────────
    // Anchors: research.md D2/D5/D6; data-model.md §2 (INV-1..5); contracts §C2.1–C2.6.
    //
    // T008 — Scanner: walk every post-35= field and validate it is
    //   <non-empty digit-only tag>=<value>\x01
    // On the FIRST malformed field → return app_payload_malformed=131, no seqnum,
    // no transmit. The 020 floor guarantees trailing SOH so every interior field IS
    // SOH-terminated; there is no run-off-the-end case.
    //
    // T009 — Excision (only over a fully-validated payload):
    //   allow_pos_dup==false (default): copy the leading 35= field + all surviving
    //     post-35= fields (those whose tag is NOT 43 or 122) into strip_buf in
    //     original order; rebind app_payload to that buffer.
    //   allow_pos_dup==true: passthrough verbatim (scanner runs but no copy made).
    //   INV-1: 35= (field 0) never touched.
    //   INV-2: only complete, boundary-anchored 43=..\x01 / 122=..\x01 removed.
    //   INV-3: stripped payload remains 35=-leading SOH-delimited.
    //   INV-4: no heap — ONE stack scratch strip_buf (sized same as body_buf).
    //   INV-5: build_replay_frame is NOT on this path.

    // strip_buf declared at this scope so it outlives the scanner+excision block
    // and remains valid when the framing block below reads app_payload (which may
    // be rebound to point into it). [research.md D6; INV-4]
    std::array<std::byte, 4096> strip_buf{};
    std::size_t strip_len = 0;

    {
        std::string_view pv{reinterpret_cast<const char*>(app_payload.data()), app_payload.size()};

        // Skip the leading 35=<value>\x01 field (field 0 — never modified).
        // pv.back()=='\x01' is guaranteed by the 020 floor; find('\x01') always succeeds.
        const std::size_t lead_soh = pv.find('\x01');
        // lead_soh != npos: guaranteed (020 floor ensures trailing SOH → at least one SOH).

        // --- T008: Scanner pass ---------------------------------------------
        // Walk each field starting at lead_soh+1 (first byte after the 35= field).
        // Each field is: everything up to (and including) the next '\x01'.
        // Validate: non-zero-length content, contains '=', tag (bytes before '=')
        // is non-empty and all ASCII digits, and value (bytes after '=') is non-empty.
        {
            std::size_t pos = lead_soh + 1;
            while (pos < pv.size()) {
                // Find the terminating SOH for this field.
                const std::size_t soh = pv.find('\x01', pos);
                // soh != npos is guaranteed: the 020 floor ensures pv ends with '\x01',
                // so the last field IS SOH-terminated; pos is always inside pv.

                // (a) Empty field (stray doubled SOH): soh == pos.
                if (soh == pos) {
                    co_return std::unexpected(error::app_payload_malformed);
                }

                // (b) Must contain '='.
                std::string_view field_sv = pv.substr(pos, soh - pos);
                const std::size_t eq = field_sv.find('=');
                if (eq == std::string_view::npos) {
                    co_return std::unexpected(error::app_payload_malformed);
                }

                // (c) Tag (before '=') must be non-empty and all ASCII digits.
                if (eq == 0) {
                    co_return std::unexpected(error::app_payload_malformed);
                }
                for (std::size_t i = 0; i < eq; ++i) {
                    if (field_sv[i] < '0' || field_sv[i] > '9') {
                        co_return std::unexpected(error::app_payload_malformed);
                    }
                }

                // (d) Value (after '=', before SOH) must be non-empty.
                if (eq + 1 >= field_sv.size()) {
                    co_return std::unexpected(error::app_payload_malformed);
                }

                pos = soh + 1;
            }
        }

        // --- T009: Excision pass --------------------------------------------
        // Only when allow_pos_dup==false; only over a scanner-validated payload.
        if (!cfg_.allow_pos_dup) {
            // Lambda: append bytes into strip_buf; returns false on overflow.
            const auto wstrip = [&](std::string_view sv) -> bool {
                if (strip_len + sv.size() > strip_buf.size()) return false;
                for (char c : sv) strip_buf[strip_len++] = static_cast<std::byte>(c);
                return true;
            };

            // INV-1: always copy the leading 35=<value>\x01 field verbatim.
            if (!wstrip(pv.substr(0, lead_soh + 1))) {
                co_return std::unexpected(error::wire_frame_too_large);
            }

            // Copy each subsequent field, skipping tag 43 and tag 122.
            std::size_t pos = lead_soh + 1;
            while (pos < pv.size()) {
                const std::size_t soh = pv.find('\x01', pos);
                // soh guaranteed to exist (scanner validated entire payload above).
                std::string_view field_sv = pv.substr(pos, soh - pos);

                // Identify tag number (bytes before '=').
                const std::size_t eq = field_sv.find('=');
                // eq != npos and eq > 0 are guaranteed by the scanner pass above.
                std::string_view tag_sv = field_sv.substr(0, eq);

                // INV-2: excise ONLY complete boundary-anchored 43 or 122 fields.
                // A literal "43=" inside another field's value never reaches this
                // branch because the scanner identified ONLY true tag-delimited fields.
                const bool is_43 = (tag_sv == "43");
                const bool is_122 = (tag_sv == "122");

                if (!is_43 && !is_122) {
                    // Copy surviving field (including its terminating SOH).
                    if (!wstrip(pv.substr(pos, soh - pos + 1))) {
                        co_return std::unexpected(error::wire_frame_too_large);
                    }
                }

                pos = soh + 1;
            }

            // Rebind app_payload to the stripped buffer — the existing framing below
            // consumes it unchanged (INV-3; contracts §C2.6).
            // strip_buf is alive at send_impl scope until co_return.
            app_payload = std::span<const std::byte>(strip_buf.data(), strip_len);
        }
        // allow_pos_dup==true: app_payload unchanged (passthrough verbatim).
    }

    // ── T009: Correct framing — MsgType(35) at wire field-3, digit-only BodyLength
    // [020-g2 research.md D1; data-model.md INV-1; FR-004a]
    //
    // The payload is guaranteed (by validation above) to start with "35=<value>\x01"
    // followed by the rest of the business fields.
    //
    // Strategy (avoids the placeholder + memmove):
    //   1. Stamp SendingTime(52).
    //   2. Peek seqnum.
    //   3. Split the leading "35=<value>\x01" off app_payload.
    //   4. Build the BODY into a local stack scratch buffer `body_buf`:
    //        35=<value>\x01  34=<seq>\x01  49=<sender>\x01  52=<time>\x01
    //        56=<target>\x01  <rest-of-payload>
    //   5. Measure body length L.
    //   6. Build the final wire frame into `buf`:
    //        "8=<begin>\x01"  "9="  <L as digits, no padding>  "\x01"
    //        <body_buf[0..L)>
    //      Compute checksum over those bytes, append "10=<CCC>\x01".

    // (1) Stamp SendingTime(52) from effective_clock.now().
    const auto st52 = effective_clock_
                          ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                          : SendingTimeStamp{};

    // (2) Peek outbound MsgSeqNum(34) — NOT yet advanced.
    // assign_outbound() is called AFTER toApp check (same ordering as before).
    const seqnum_t seq = seqnum_mgr_.peek_outbound();

    // (3) Split the leading 35=<value>\x01 field off the payload.
    // Payload is guaranteed to start with "35="; find the first SOH.
    std::string_view pv{reinterpret_cast<const char*>(app_payload.data()), app_payload.size()};
    std::size_t first_soh = pv.find('\x01');
    // first_soh must exist (a well-formed field ends with SOH). If not, treat as
    // truncated — the payload has no SOH terminator, which is a well-formed
    // frame requirement. Reject gracefully.
    if (first_soh == std::string_view::npos) {
        co_return std::unexpected(error::app_payload_malformed);
    }
    std::string_view msgtype_field = pv.substr(0, first_soh + 1);  // "35=D\x01"
    std::string_view rest_payload = pv.substr(first_soh + 1);      // remaining fields

    // (4) Build BODY into a second stack buffer.
    // Body layout: 35=<value>\x01  34=<seq>\x01  49=<sender>\x01  52=<time>\x01
    //              56=<target>\x01  <rest_payload>
    std::array<std::byte, 4096> body_buf{};
    std::size_t bpos = 0;
    const std::byte SOH{0x01};

    const auto wb_body = [&](const char* s, std::size_t n) -> bool {
        if (bpos + n > body_buf.size()) return false;
        for (std::size_t i = 0; i < n; ++i) body_buf[bpos++] = static_cast<std::byte>(s[i]);
        return true;
    };
    const auto wsv_body = [&](std::string_view sv) -> bool {
        return wb_body(sv.data(), sv.size());
    };
    const auto wfield_body = [&](std::string_view tag_eq, std::string_view val) -> bool {
        if (!wsv_body(tag_eq) || !wsv_body(val)) return false;
        if (bpos >= body_buf.size()) return false;
        body_buf[bpos++] = SOH;
        return true;
    };

    // 35=<value>\x01 (already includes SOH from the split above)
    if (!wsv_body(msgtype_field)) {
        co_return std::unexpected(error::wire_frame_too_large);
    }
    // 34=<seq>\x01
    {
        char nbuf[12];
        auto [end, ec] = std::to_chars(nbuf, nbuf + sizeof(nbuf), static_cast<std::uint32_t>(seq));
        (void)ec;
        if (!wfield_body("34=", std::string_view{nbuf, static_cast<std::size_t>(end - nbuf)})) {
            co_return std::unexpected(error::wire_frame_too_large);
        }
    }
    // 49=<sender>\x01
    if (!wfield_body("49=", cfg_.sender_comp_id)) {
        co_return std::unexpected(error::wire_frame_too_large);
    }
    // 52=<time>\x01
    if (!wfield_body("52=", st52.value)) {
        co_return std::unexpected(error::wire_frame_too_large);
    }
    // 56=<target>\x01
    if (!wfield_body("56=", cfg_.target_comp_id)) {
        co_return std::unexpected(error::wire_frame_too_large);
    }
    // rest of app_payload (business fields after the 35= field)
    if (bpos + rest_payload.size() > body_buf.size()) {
        co_return std::unexpected(error::wire_frame_too_large);
    }
    if (!wsv_body(rest_payload)) {
        co_return std::unexpected(error::wire_frame_too_large);
    }

    const std::size_t body_len = bpos;  // exact body length, no padding

    // (5) Build the wire frame into `buf`.
    // Frame: "8=<begin>\x01" "9=" <body_len digits> "\x01" <body_buf[0..body_len)>
    //        "10=" <CCC> "\x01"
    std::array<std::byte, 4096> buf{};
    std::size_t pos = 0;

    const auto wb = [&](const char* s, std::size_t n) -> bool {
        if (pos + n > buf.size()) return false;
        for (std::size_t i = 0; i < n; ++i) buf[pos++] = static_cast<std::byte>(s[i]);
        return true;
    };
    const auto wsv = [&](std::string_view sv) -> bool { return wb(sv.data(), sv.size()); };
    const auto wfield = [&](std::string_view tag_eq, std::string_view val) -> bool {
        if (!wsv(tag_eq) || !wsv(val)) return false;
        if (pos >= buf.size()) return false;
        buf[pos++] = SOH;
        return true;
    };

    // 8=<BeginString>\x01
    if (!wfield("8=", cfg_.begin_string)) {
        co_return std::unexpected(error::wire_frame_too_large);
    }

    // 9=<body_len digits (no padding)>\x01
    {
        char bl_buf[12];
        auto [bl_end, bl_ec] = std::to_chars(bl_buf, bl_buf + sizeof(bl_buf), body_len);
        (void)bl_ec;
        if (!wfield("9=", std::string_view{bl_buf, static_cast<std::size_t>(bl_end - bl_buf)})) {
            co_return std::unexpected(error::wire_frame_too_large);
        }
    }

    // Append the pre-built body (35=…34=…49=…52=…56=…rest).
    if (pos + body_len > buf.size()) {
        co_return std::unexpected(error::wire_frame_too_large);
    }
    for (std::size_t i = 0; i < body_len; ++i) {
        buf[pos++] = body_buf[i];
    }

    // Compute checksum over all bytes so far (8=…body, before 10=).
    unsigned int csum = 0;
    for (std::size_t i = 0; i < pos; ++i) {
        csum += static_cast<unsigned int>(static_cast<unsigned char>(buf[i]));
    }
    csum &= 0xFFU;

    // 10=<CCC>\x01
    {
        char cs_buf[4];
        cs_buf[0] = static_cast<char>('0' + (csum / 100U));
        cs_buf[1] = static_cast<char>('0' + ((csum % 100U) / 10U));
        cs_buf[2] = static_cast<char>('0' + (csum % 10U));
        if (!wfield("10=", std::string_view{cs_buf, 3})) {
            co_return std::unexpected(error::wire_frame_too_large);
        }
    }

    // ── 019 T013: toApp inspection/veto (FR-006/007; US2 AC1/AC2; research D6) ──
    // Called AFTER the complete frame is built so the MessageView passed to toApp
    // contains all FIX fields including MsgType (now correctly at field-3).
    // Run BEFORE store_then_emit so a vetoed/aborted send is not stored or transmitted.
    // A veto does NOT consume the seqnum (assign_outbound called below, after this check).
    // [research D6; spec.md US2 AC1/AC2; FR-006/007; data-model.md INV-5]
    if (engine_.application != nullptr) {
        std::span<const std::byte> built_frame{buf.data(), pos};
        auto cb_r = parse_and_dispatch_(built_frame, kInboundParseArena, [&](auto& mv, auto& sid) {
            return engine_.application->toApp(mv, sid);
        });
        if (!cb_r) {
            if (cb_r.error() == fixpp::core::error::app_callback_threw) {
                co_await close(close_mode::terminal);
                co_return std::unexpected(cb_r.error());
            }
            // toApp veto (app_do_not_send) or other error → drop, return error.
            // Session stays Active (INV-5/SC-004).
            co_return std::unexpected(cb_r.error());
        }
        // If parse fails (cb_r == ok): frame accepted (no-op on toApp).
    }

    // (3b) Advance outbound seqnum counter via assign_outbound() — AFTER toApp check
    // so a vetoed send does NOT consume a seqnum. [FR-001(a); T013 ordering]
    {
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            co_return std::unexpected(assign_r.error());  // store_seqnum_overflow (I-8)
        }
        // The assigned value must equal the peeked seq (single-strand, no race).
        (void)assign_r;
    }

    // (4) store(outbound) BEFORE transport ([2e §4.1]).
    // Pass the stamped seqnum explicitly (RC#A: next_outbound_seq_ removed).
    co_return co_await store_then_emit(seq, std::span<const std::byte>(buf.data(), pos));
}

fsm_state Session::state() const noexcept { return fsm_state_; }

// ── T039/T041 (US3): Liveness loop ───────────────────────────────────────────
//
// run_liveness_loop() — the heartbeat / test-request / unanswered-TR timer
// coroutine. Co_spawned (asio::detached) when the session first enters Active.
// Runs on the session executor; cancelled via the root cancellation slot when
// Session::close() fires.
//
// Algorithm:
//   1. If HeartBtInt = 0 → return immediately (all liveness disabled, FR-006).
//   2. Loop until the session is no longer Active or cancellation fires:
//      a. Sleep until last_inbound_steady_ + heartbt_int.
//      b. If session is no longer Active → stop.
//      c. If inbound data arrived during the sleep (last_inbound_steady_
//         updated past the deadline) → reset and loop (no TestRequest needed).
//      d. No inbound data: emit TestRequest (record pending_test_req_id_).
//      e. Sleep another heartbt_int.
//      f. If still no inbound Heartbeat reply (pending_test_req_id_ still set):
//         → session_test_request_unanswered → Disconnected → stop.
//      g. If a Heartbeat was received (pending_test_req_id_ cleared by
//         on_inbound_frame) → loop continues.
//
// Traps observed in project memory:
//   [feedback_asio_cospawn_total_cancellation_default]: co_spawn defaults to
//     terminal-only; must reset to enable_total_cancellation for close() to
//     cancel the sleep_until.
//   [feedback_asio_post_resume_bounces_to_spawn_executor]: we co_spawn on the
//     session executor directly, so resumes stay on that executor.
//   [feedback_async_mutex_us3_asio_cancel_and_subagent_seams]: wrap the outer
//     loop in try/catch(asio::system_error) to convert operation_aborted into
//     clean return (not a crash).
//
// HeartBtInt source: cfg_.heartbeat_interval (std::optional<seconds>, D-8).
// Default: 30s if not set; HeartBtInt=0 disables.
asio::awaitable<void> Session::run_liveness_loop() noexcept {
    using namespace std::chrono_literals;

    // [feedback_asio_cospawn_total_cancellation_default]: reset the coroutine's
    // cancellation filter to accept total cancellation. The root slot was bound
    // at co_spawn time (via bind_cancellation_slot), so this only changes the
    // filter — it does not reassign the slot's backing storage.
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});

    // FQ-C (gate-b/r3): spawn sites increment liveness_counter_ BEFORE co_spawn
    // publishes the detached frame, closing the pre-start close()/destroy race.
    // The coroutine body owns only the matching decrement, using a captured
    // shared_ptr so the counter storage survives until the detached work exits.
    // [feedback_detached_cospawn_write_not_in_join_counter; FQ-C]
    auto live_ctr = liveness_counter_;  // shared ownership
    struct liveness_dec {
        std::shared_ptr<std::atomic<int>> ctr;
        ~liveness_dec() { ctr->fetch_sub(1, std::memory_order_release); }
    } live_dec_guard{live_ctr};

    // Resolve HeartBtInt from config (D-8 default: 30s; 0 = disabled).
    std::chrono::seconds heartbt_int{30};
    if (cfg_.heartbeat_interval.has_value()) {
        heartbt_int = *cfg_.heartbeat_interval;
    }

    // HeartBtInt=0 → all liveness disabled (FR-006 / [FIX-SL §4.3.4]).
    if (heartbt_int.count() == 0) {
        co_return;
    }

    if (!effective_clock_) {
        co_return;  // No clock (should not happen post-open(), but guard).
    }

    // Test-request threshold: 1 × HeartBtInt (D-8 default).
    std::chrono::milliseconds threshold =
        std::chrono::duration_cast<std::chrono::milliseconds>(heartbt_int);
    if (cfg_.test_request_threshold.has_value()) {
        threshold = *cfg_.test_request_threshold;
    }

    // Liveness loop.
    // Two independent cadences:
    //   (A) Outbound idle for heartbt_int  → emit Heartbeat(0)   [T018 Cell A / spec FR-003]
    //   (B) Inbound idle for heartbt_int   → emit TestRequest(1) [T018 Cell B / spec FR-004]
    //       then grace window of heartbt_int; no reply → Disconnected [T018 Cell C]
    // Sleep until the EARLIEST of last_outbound + heartbt_int OR last_inbound + heartbt_int.
    try {
        while (fsm_state_ == fsm_state::Active) {
            // Compute earliest sleep deadline.
            const auto outbound_deadline = last_outbound_steady_ + heartbt_int;
            const auto inbound_deadline = last_inbound_steady_ + heartbt_int;
            const auto deadline =
                outbound_deadline < inbound_deadline ? outbound_deadline : inbound_deadline;

            // Sleep until that deadline (or until cancellation fires).
            co_await effective_clock_->sleep_until(deadline);

            // Check if we're still Active after the sleep.
            if (fsm_state_ != fsm_state::Active) {
                co_return;
            }

            auto now = effective_clock_->steady_now();

            // (A) Outbound idle → Heartbeat. T018 Cell A / spec FR-003:
            // If no outbound for heartbt_int, emit Heartbeat(0) to tell peer we're alive.
            // This updates last_outbound_steady_ via store_then_emit.
            if (last_outbound_steady_ + heartbt_int <= now) {
                std::array<std::byte, 256> hb_buf{};
                const auto st52_hb =
                    stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
                const seqnum_t hb_seq = seqnum_mgr_.peek_outbound();
                auto hb_result = fixpp::session::build_heartbeat(
                    std::span<std::byte>{hb_buf.data(), hb_buf.size()}, hb_seq, cfg_.sender_comp_id,
                    cfg_.target_comp_id, {}, cfg_.begin_string, st52_hb.value);
                if (hb_result) {
                    // 019 T014: toAdmin before liveness Heartbeat. [FR-008/010]
                    // FIX-3 (gate-b/r1): throw → terminal-close + co_return.
                    if (!fire_to_admin_(*hb_result)) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return;
                    }
                    auto assign_r = co_await seqnum_mgr_.assign_outbound();
                    if (!assign_r) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return;
                    }
                    auto emit_r = co_await store_then_emit(hb_seq, *hb_result);
                    if (!emit_r) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return;
                    }
                }
                if (fsm_state_ != fsm_state::Active) {
                    co_return;
                }
                now = effective_clock_->steady_now();
            }

            // (B) Inbound idle → TestRequest. T018 Cell B / spec FR-004:
            // Did inbound data arrive during the sleep (updating last_inbound_steady_)?
            if (last_inbound_steady_ + heartbt_int > now) {
                // Inbound data arrived; the deadline was reset. Loop again.
                continue;
            }

            // No inbound data for heartbt_int: emit TestRequest.
            // Generate a unique TestReqID using the per-session counter
            // (FR-010 / RC#6): ++next_test_request_id_ replaces the prior
            // process-global `static tr_counter`. Single-writer on the session
            // strand; wrap-around at UINT32_MAX is acceptable per research.md D-3.
            std::array<char, 32> id_buf{};
            id_buf[0] = 'T';
            id_buf[1] = 'R';
            auto [end, ec] = std::to_chars(id_buf.data() + 2, id_buf.data() + id_buf.size(),
                                           ++next_test_request_id_);
            (void)ec;  // 32-byte buffer is sufficient for "TR" + max uint32_t (10 digits).
            pending_test_req_id_.assign(id_buf.data(), end);
            unanswered_tr_ = false;

            // T042 (US4 retirement): emit the TestRequest frame via
            // store_then_emit (I-3 outbound half). Stack buffer; noexcept.
            {
                std::array<std::byte, 256> tr_buf{};
                const auto st52 =
                    stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
                const seqnum_t tr_seq = seqnum_mgr_.peek_outbound();
                auto tr_result = fixpp::session::build_test_request(
                    std::span<std::byte>{tr_buf.data(), tr_buf.size()}, tr_seq, cfg_.sender_comp_id,
                    cfg_.target_comp_id, pending_test_req_id_, cfg_.begin_string, st52.value);
                if (tr_result) {
                    // 019 T014: toAdmin before TestRequest. [FR-008/010]
                    // FIX-3 (gate-b/r1): throw → terminal-close + co_return.
                    if (!fire_to_admin_(*tr_result)) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return;
                    }
                    auto assign_r = co_await seqnum_mgr_.assign_outbound();
                    if (!assign_r) {
                        // Overflow or closed: session-fatal per data-model.md:30 E3.
                        // Liveness loop is fire-and-forget (no expected_t return):
                        // log by transitioning to Disconnected and stopping the loop.
                        record_state_transition_(fsm_state::Disconnected);
                        co_return;
                    }
                    auto emit_r = co_await store_then_emit(tr_seq, *tr_result);
                    if (!emit_r) {
                        record_state_transition_(fsm_state::Disconnected);
                        co_return;
                    }
                }
            }

            // Grace window: sleep until inbound_deadline + heartbt_int + 1ns.
            // T018 Cell C fix: grace_deadline = inbound_deadline + heartbt_int
            // NOT now + heartbt_int — the deadline was already in the past when
            // we woke up, so using now would overshoot.
            // +1ns guard: prevents mock_clock fire_now=true (asio::post immediate
            // completion) when inbound_deadline + heartbt_int == clock exactly,
            // which would fire the grace check before the test can deliver the
            // Heartbeat echo. The 1ns offset preserves Cell C (advance 11s > 10s+1ns)
            // while letting TR_DistinctNow's echo arrive before the grace fires.
            // [T018 Cell C grace_deadline bug fix; admin_builder_distinct_now_test compat]
            const auto grace_deadline =
                inbound_deadline + heartbt_int + std::chrono::nanoseconds{1};
            co_await effective_clock_->sleep_until(grace_deadline);

            if (fsm_state_ != fsm_state::Active) {
                co_return;
            }

            // Still Active after grace window. Did a Heartbeat arrive?
            if (!pending_test_req_id_.empty()) {
                // No Heartbeat reply received — unanswered TestRequest.
                // Per data-model Active row + [FIX-SL §4.5.5]:
                // session_test_request_unanswered (slot 74) → Disconnected.
                unanswered_tr_ = true;
                record_state_transition_(fsm_state::Disconnected);
                // (Phase 6 US4 wires the Logout emission before Disconnected.)
                co_return;
            }

            // Heartbeat was received (pending_test_req_id_ cleared by
            // on_inbound_frame). Loop continues for the next window.
        }
    } catch (const std::system_error& e) {
        // operation_aborted from sleep_until cancellation (close() fired).
        // [feedback_async_mutex_us3_asio_cancel_and_subagent_seams]
        // Convert to clean return — not a fatal error.
        (void)e;
    } catch (...) {  // NOLINT(bugprone-empty-catch) — noexcept-window absorption per FR-15: a
                     // throwing user callback must trap, not propagate; nothing else to do here.
        // Any other exception: absorb (noexcept window).
    }
}

// ── US4 T046: store_then_emit ─────────────────────────────────────────────────
//
// Durable-before-transmit (I-3 outbound half):
//   1. If store_ is available: co_await store_->store(stamped_seq, frame, outbound).
//   2. AFTER store returns: call transport_send_(frame) if non-null.
// Errors from store(): logged-then-proceed (I-07); close still completes.
// stamped_seq: the MsgSeqNum already written into `frame` by the builder — passed
//   explicitly (RC#A: next_outbound_seq_ removed; RC#B: transport errors surfaced).
// [gate-b/r1-green: RC#A removes next_outbound_seq_ - 1U arithmetic;
//  gate-b/r1-green: RC#B surfaces transport throws as dispatch_aborted]
asio::awaitable<fixpp::core::expected_t<void>> Session::store_then_emit(
    seqnum_t stamped_seq, std::span<const std::byte> frame) noexcept {
    // Step 1: store (durable-before-transmit).
    if (store_) {
        // F5 (Round-A drift): wrap co_await store_->store() in try/catch to absorb
        // asio::system_error{operation_aborted} thrown by the store awaitable on
        // cancellation. Without this, the throw propagates out of the noexcept
        // store_then_emit frame → std::terminate.
        // [F5 drift fix; [[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]]
        try {
            auto store_r = co_await store_->store(stamped_seq, frame, direction_t::outbound);
            (void)store_r;  // store errors → logged-then-proceed (I-07)
        } catch (const asio::system_error& e) {
            if (e.code() == asio::error::operation_aborted) {
                // Cancellation won before the store committed. Per US1 AC3 and I-07:
                // propagate as an error so the caller can transition to Disconnected.
                co_return std::unexpected(fixpp::core::error::dispatch_aborted);
            }
            // Non-abort system_error: absorb (I-07 logged-then-proceed on store path).
        } catch (...) {  // NOLINT(bugprone-empty-catch) — noexcept-window absorption:
            // any other exception from the store awaitable is absorbed (I-07).
        }
    }

    // Step 2: transmit (ONLY after store completes — I-3).
    //
    // FQ-A (gate-b/r2): for a LIVE transport, route through live_write_serialized_()
    // which acquires write_gate_ across the async_write completion, holds a
    // shared_ptr<Transport> keepalive, and returns an error if the write fails.
    // This satisfies three invariants simultaneously:
    //   (a) serialization: write_gate_ ensures ≤1 async_write in-flight
    //       (transport.hpp:47-50 ≤1-in-flight contract);
    //   (b) error propagation: write error → dispatch_aborted → caller disconnects;
    //   (c) lifetime safety: shared_ptr keepalive prevents UAF (Q-1 fix).
    // [transport.hpp:47-50; FQ-A D-6; realized-behavior.md C1/C2]
    //
    // Pre-live (config-time transport_send_): sync std::function set from
    // cfg_.transport_send at open() — used by direct-Session tests.
    {
        if (live_transport_shared_()) {
            // Live path: serialized write through write_gate_.
            auto write_r = co_await live_write_serialized_(frame);
            if (!write_r.has_value()) {
                co_return std::unexpected(fixpp::core::error::dispatch_aborted);
            }
        } else if (transport_send_) {
            // Config-time sync sink (pre-live / test path).
            try {
                transport_send_(frame);
            } catch (const asio::system_error&) {
                co_return std::unexpected(fixpp::core::error::dispatch_aborted);
            } catch (...) {
                co_return std::unexpected(fixpp::core::error::dispatch_aborted);
            }
        }
    }

    // T018 Cell A: track outbound activity for outbound-idle Heartbeat cadence.
    // Updated on EVERY successful outbound emit (store + transport).
    if (effective_clock_) {
        last_outbound_steady_ = effective_clock_->steady_now();
    }

    co_return fixpp::core::expected_t<void>{};
}

// ── apply_inbound_sequence_reset (S-023) ────────────────────────────────────
//
// Apply an inbound SequenceReset(35=4) NewSeqNo(36) to the expected-inbound
// counter. Mirrors QuickFIX-cpp Session::nextSequenceReset (Session.cpp:339)
// arms; the caller places GapFill vs Reset mode relative to the seqnum gate.
asio::awaitable<fixpp::core::expected_t<void>> Session::apply_inbound_sequence_reset(
    seqnum_t new_seqno, seqnum_t ref_seq) noexcept {
    // NewSeqNo(36) absent/invalid (parse_seqnum → 0): nothing to apply → no-op
    // (matches QuickFIX getFieldIfSet: absent NewSeqNo ⇒ no counter change).
    if (new_seqno == 0) {
        co_return fixpp::core::expected_t<void>{};
    }

    const seqnum_t expected = seqnum_mgr_.next_inbound_unsafe();

    if (new_seqno > expected) {
        // Advance past the filled gap (GapFill) or to the admin reset target
        // (Reset). [QuickFIX setNextTargetMsgSeqNum]
        auto sr = co_await seqnum_mgr_.set_next_inbound(new_seqno);
        if (!sr) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(sr.error());
        }
        // If this completes an outstanding resend span, leave recovery.
        if (reconnect_fsm_.is_awaiting_resend()) {
            reconnect_fsm_.exit_awaiting_resend();
        }
        co_return fixpp::core::expected_t<void>{};
    }

    if (new_seqno < expected) {
        // Below expected — would move the inbound stream backward. Reject with
        // SessionRejectReason=5 (ValueIsIncorrect), RefTagID=36; counter
        // unchanged. [QuickFIX generateReject(SessionRejectReason_VALUE_IS_INCORRECT)]
        std::array<std::byte, 512> rj_buf{};
        const auto st52 = effective_clock_
                              ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                              : SendingTimeStamp{};
        const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
        auto rj_result =
            fixpp::session::build_reject(std::span<std::byte>{rj_buf.data(), rj_buf.size()}, rj_seq,
                                         cfg_.sender_comp_id, cfg_.target_comp_id, ref_seq,
                                         36,   // RefTagID = 36 (NewSeqNo)
                                         "4",  // RefMsgType = SequenceReset
                                         5,    // SessionRejectReason = 5 (ValueIsIncorrect)
                                         cfg_.begin_string, st52.value);
        if (rj_result) {
            auto assign_r = co_await seqnum_mgr_.assign_outbound();
            if (!assign_r) {
                record_state_transition_(fsm_state::Disconnected);
                co_return std::unexpected(assign_r.error());
            }
            auto emit_r = co_await store_then_emit(rj_seq, *rj_result);
            (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
        }
        co_return fixpp::core::expected_t<void>{};
    }

    // new_seqno == expected → no-op (counter already aligned).
    co_return fixpp::core::expected_t<void>{};
}

// ── US4 T047: run_logout_phase1 ───────────────────────────────────────────────
//
// Two-phase graceful Logout (D-6 / [2d §6.5] / I-9):
//   1. Build Logout frame (build_logout) + store_then_emit (I-3 outbound half).
//   2. Transition FSM to LogoutSent.
//   3. Sleep up to 2 s (D-8: session_logout_timeout default) under a CHILD
//      cancellation_state, waking early if logout_confirmed_ is set by
//      on_inbound_frame() when the peer's confirming Logout arrives.
//   4. If confirmed → ok.
//      If timeout → transition to Disconnected, return session_logout_timeout.
//
// Called from close(graceful) phase 1 ONLY. The root cancellation_state fires
// phase-2 total AFTER this coroutine returns (either path).
//
// [feedback_asio_cospawn_total_cancellation_default]: this coroutine is called
// via co_await from close(); the current coroutine's cancellation state is
// already bound to the root slot. We do NOT reset here — the caller (close)
// controls the root slot. Instead we use a loop-and-check polling pattern
// for the logout confirmation, which is safe on the single-strand session.
asio::awaitable<fixpp::core::expected_t<void>> Session::run_logout_phase1() noexcept {
    using namespace std::chrono_literals;
    using std::chrono::seconds;

    // Build the Logout frame into a stack buffer.
    std::array<std::byte, 256> buf{};
    const auto st52 = effective_clock_
                          ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                          : SendingTimeStamp{};
    const seqnum_t logout_seq = seqnum_mgr_.peek_outbound();
    auto logout_result = fixpp::session::build_logout(
        std::span<std::byte>{buf.data(), buf.size()}, logout_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, {}, cfg_.begin_string, st52.value);

    if (!logout_result) {
        // Build failure (unlikely): treat as no-frame-sent, proceed to timeout.
        // The session still transitions to LogoutSent and times out.
    } else {
        // Emit the Logout frame (store first, then transport_send — I-3).
        // 019 T014: toAdmin before graceful-close Logout. [FR-008/010]
        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
        if (!fire_to_admin_(*logout_result)) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            // Overflow or closed: session-fatal per data-model.md:30 E3.
            // Abort logout, force-disconnect with propagated error.
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(assign_r.error());
        }
        auto emit_r = co_await store_then_emit(logout_seq, *logout_result);
        (void)emit_r;  // store-side errors: logged-then-proceed (I-07)
    }

    // 024 T013 (site a): mark that a Logout was sent on the local graceful path.
    // Keyed here (after emit, before LogoutSent) so logout_seen_ is true whenever
    // the Logout frame was actually placed on the wire. The teardown reset in close()
    // reads this flag to distinguish a graceful-Logout teardown from an abnormal drop.
    // [contracts/reset-knobs.md C3.1; plan.md Gate A convergence note (b)]
    logout_seen_ = true;

    // Transition to LogoutSent.
    record_state_transition_(fsm_state::LogoutSent);
    logout_confirmed_ = false;

    if (!effective_clock_) {
        // No clock (should not happen post-open): force-disconnect immediately.
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(fixpp::core::error::session_logout_timeout);
    }

    // Sleep until the configurable graceful-close timeout (cfg_.logout_disconnect_timeout_ms).
    // Default 2000 ms (matching QuickFIX/J SessionState.logoutTimeoutMs). Wired from
    // SessionConfig per FR-008 / RC#D (gate-b/r1); previously hardcoded seconds{2}.
    // on_inbound_frame() will set logout_confirmed_=true AND call
    // effective_clock_->cancel_sleeps() when the peer's confirming Logout arrives,
    // waking us up early. When cancel_sleeps fires, sleep_until throws
    // system_error(operation_aborted); we catch it and check logout_confirmed_.
    auto deadline = effective_clock_->steady_now() +
                    std::chrono::milliseconds{cfg_.logout_disconnect_timeout_ms};
    try {
        co_await effective_clock_->sleep_until(deadline);
    } catch (const std::system_error&) {  // NOLINT(bugprone-empty-catch) — wake-early signal: the
                                          // `logout_confirmed_` flag check after this block is the
                                          // actual control-flow decision; no action needed in the
                                          // handler itself.
        // Woken early: either peer confirmed (logout_confirmed_=true)
        // or root cancellation fired (phase 2). Check the flag.
    } catch (...) {  // NOLINT(bugprone-empty-catch) — noexcept-window absorption per FR-15.
        // Any other exception: absorb (noexcept window).
    }

    if (logout_confirmed_) {
        // Peer confirmed; FSM already set to Disconnected by on_inbound_frame.
        co_return fixpp::core::expected_t<void>{};
    }

    // Timeout (or root cancellation before confirm): force-disconnect.
    record_state_transition_(fsm_state::Disconnected);
    co_return std::unexpected(fixpp::core::error::session_logout_timeout);
}

// ── 027 T005 — replay_outbound_range_ ────────────────────────────────────────
//
// Extracted from the inline ResendRequest-reply walk (was session.cpp:2485-2635).
// Replays [begin, requested_end] (or through-current when end_is_through_current)
// using the same two-value end model as the original inline block.
//
// TWO-VALUE END MODEL (data-model I-NEX-3, research D-5, contracts C3):
//   eff_end = (end_is_through_current || (our_last > 0 && requested_end > our_last))
//                 ? our_last : requested_end
//   empty/short-store GapFill NewSeqNo =
//               end_is_through_current ? peek_outbound() : (requested_end + 1)
//
// The helper NEVER calls record_state_transition_ — the CALLER owns all FSM
// Disconnected transitions. Returns std::unexpected on all failure paths so
// the caller can detect and handle them.
//
// Returns:
//   expected_t<void>{}                          — success (resend complete)
//   unexpected(app_callback_threw)              — a GapFill toAdmin threw
//   unexpected(dispatch_aborted)                — transport write error

asio::awaitable<fixpp::core::expected_t<void>>
Session::replay_outbound_range_(seqnum_t begin, seqnum_t requested_end,
                                bool end_is_through_current) noexcept
{
    const auto st52_sr =
        effective_clock_
            ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
            : SendingTimeStamp{};

    // Transmit a single replay/gapfill frame; returns false on write error.
    // Uses the live-write path when a transport is attached; sync transport_send_
    // for pre-live/test paths.
    const auto transmit_async =
        [&](std::span<const std::byte> f) -> asio::awaitable<bool> {
        if (live_transport_shared_()) {
            auto wr = co_await live_write_serialized_(f);
            co_return wr.has_value();
        }
        if (!transport_send_) {
            co_return true;
        }
        try {
            transport_send_(f);
            co_return true;
        } catch (...) {  // NOLINT(bugprone-empty-catch)
            co_return false;
        }
    };

    const auto is_admin_type = [](std::string_view mt) -> bool {
        return mt == "0" || mt == "1" || mt == "2" || mt == "3" || mt == "4" ||
               mt == "5" || mt == "A";
    };

    // FIX-3 (gate-b/r1): set when emit_gapfill_async detects a toAdmin throw.
    bool gapfill_callback_threw = false;
    const auto emit_gapfill_async =
        [&](seqnum_t at_seq, seqnum_t new_seqno) -> asio::awaitable<bool> {
        std::array<std::byte, 256> gf_buf{};
        auto gf = fixpp::session::build_sequence_reset_gapfill(
            std::span<std::byte>{gf_buf.data(), gf_buf.size()}, at_seq,
            cfg_.sender_comp_id, cfg_.target_comp_id, new_seqno, cfg_.begin_string,
            st52_sr.value);
        if (!gf) {
            co_return true;
        }  // build failure treated as no-op
        // 019 T014: toAdmin before SequenceReset-GapFill. [FR-008/010]
        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
        if (!fire_to_admin_(*gf)) {
            gapfill_callback_threw = true;
            co_return false;
        }
        co_return co_await transmit_async(*gf);
    };

    // Resolve the effective end: through-current or clamped to our last stored
    // sequence number. Mirrors the original rr_end resolution.
    seqnum_t our_last = 0;
    if (store_) {
        auto ns = co_await store_->next_seqnum(direction_t::outbound, false);
        if (ns) our_last = (*ns > 0) ? (*ns - 1U) : 0;
    }
    const seqnum_t eff_end =
        (end_is_through_current || (our_last > 0 && requested_end > our_last))
            ? our_last
            : requested_end;

    // No store, or nothing to replay in range → single GapFill covering the
    // whole requested range (empty-store CHK032).
    // NewSeqNo uses the two-value model:
    //   end_is_through_current → peek_outbound() (mirrors EndSeqNo=0 path)
    //   else                   → requested_end + 1 (mirrors explicit rr_end+1)
    if (!store_ || our_last == 0 || begin > eff_end) {
        const seqnum_t new_seq_no =
            end_is_through_current ? seqnum_mgr_.peek_outbound() : (requested_end + 1U);
        if (!co_await emit_gapfill_async(begin > 0 ? begin : 1U, new_seq_no)) {
            if (gapfill_callback_threw) {
                co_return std::unexpected(fixpp::core::error::app_callback_threw);
            }
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
        co_return fixpp::core::expected_t<void>{};
    }

    // Per-slot store-walk over [begin, eff_end]. Accumulate absent/admin runs
    // into one GapFill; flush before each replay.
    static constexpr std::size_t kRpBufSize =
        CaptureVisitor::kCapBufSize + 256;  // capture + replay-tag overhead
    bool gap_open = false;
    seqnum_t gap_start = 0;
    for (seqnum_t k = begin; k <= eff_end; ++k) {
        CaptureVisitor cv;
        auto rr = co_await store_->retrieve(k, k, direction_t::outbound, cv);

        // Truncated frame: disconnect rather than silently losing data. [RC#B]
        if (cv.truncated) {
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }

        const bool app_present =
            rr && cv.captured &&
            !is_admin_type(
                scan_frame_header(std::span<const std::byte>{cv.buf.data(), cv.len})
                    .msg_type);
        if (app_present) {
            if (gap_open) {
                if (!co_await emit_gapfill_async(gap_start, k)) {
                    if (gapfill_callback_threw) {
                        co_return std::unexpected(fixpp::core::error::app_callback_threw);
                    }
                    co_return std::unexpected(fixpp::core::error::dispatch_aborted);
                }
                gap_open = false;
            }
            std::array<std::byte, kRpBufSize> rp_buf{};
            auto rp = build_replay_frame(
                std::span<std::byte>{rp_buf.data(), rp_buf.size()},
                std::span<const std::byte>{cv.buf.data(), cv.len});
            if (rp && !co_await transmit_async(*rp)) {
                co_return std::unexpected(fixpp::core::error::dispatch_aborted);
            }
        } else {
            // Absent slot or admin message → fold into a GapFill run.
            if (!gap_open) {
                gap_open = true;
                gap_start = k;
            }
        }
    }
    if (gap_open) {
        if (!co_await emit_gapfill_async(gap_start, eff_end + 1U)) {
            if (gapfill_callback_threw) {
                co_return std::unexpected(fixpp::core::error::app_callback_threw);
            }
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
    }
    // Remain in Active after responding to ResendRequest / 789 honor.
    co_return fixpp::core::expected_t<void>{};
}

}  // namespace fixpp::session
