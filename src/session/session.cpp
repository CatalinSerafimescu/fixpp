// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/session.cpp
//
// fixpp::session::Session — minimal real skeleton out-of-line impl (D-4 /
// E10). Phase 2 (T012) ships the ctor + the never-null session_arena()
// resolution chain + linkable open()/close() placeholders. The 2d-owned
// BEHAVIOUR is wired per user story (T020/T030/T037/T038/T039/T045/T050) —
// each replaces the marked placeholder body, it is not additive guesswork.
//
// 104-sequence-reset-fidelity supersedes three decisions this file implemented, by name:
//   - 024 contracts/reset-knobs.md C3.1, reset_on_logout on a Logout "in either direction":
//     a Logout received from the peer resets at its arm's reset point, and no Logout fixpp
//     sends resets (contract RR-1 and RR-2; B-104-1).
//   - 093 contracts/inbound-frame-dispositions.md C-6's close() bullet, close() waiting
//     "only when it is about to issue its teardown reset", event-driven: close() waits by a
//     poll, whatever the knobs (contract RR-2 item 4, RT-5; B-104-2).
//   - 027 contracts/next-expected-msgseqnum.md C3's explicit-end case, NewSeqNo = EndSeqNo
//     + 1: an end at or above the next outbound number is answered through the current
//     number (contract U6, research D3.3a; B-104-5).
// A site that implements one names 104 in its comment:
// `git grep -n '104' -- src/session/session.cpp` lists them.
#include <algorithm>
#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/as_tuple.hpp>
#include <asio/async_result.hpp>  // NOLINT(misc-include-cleaner) — IWYU: async_initiate via use_awaitable
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>  // NOLINT(misc-include-cleaner) — asio::co_spawn used to spawn run_liveness_loop(); clang-tidy doesn't see the use through templates
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>  // asio::error::operation_aborted — F5 noexcept-throw absorption
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <cassert>  // 066-dict-backed-inbound-parse T006: inbound_tv_ invariant assert
#include <charconv>
#include <chrono>
#include <compare>  // NOLINT(misc-include-cleaner) — IWYU: strong_ordering/operator> via chrono spaceship
#include <coroutine>  // NOLINT(misc-include-cleaner) — IWYU: coroutine_handle via awaitable machinery
#include <cstddef>
#include <cstdint>
#include <cstring>    // 034: std::memcpy for the masked-frame coroutine-frame copy
#include <exception>  // 103: std::uncaught_exceptions, the bound's wake clearer
#include <expected>
#include <fixpp/core/clock.hpp>  // Clock::steady_now / sleep_until
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>               // expected_t, error values
#include <fixpp/core/fix_time.hpp>            // 005 US5: fix_string_to_utc_time (T055/T056)
#include <fixpp/core/pmr_arena_upstream.hpp>  // detail::arena_upstream (MSVC-debug proxy)
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/session_local.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/log/level.hpp>               // 093 E-12: log::cat::session
#include <fixpp/log/logger.hpp>              // 093 E-12: FIXPP_SLOG, the garble record
#include <fixpp/session/admin_messages.hpp>  // 005 US1: interpret_logon / T046: build_logout
#include <fixpp/session/config_byte_floor.hpp>  // 090-capi-refusals (fixpp#452): contains_forbidden_config_byte (D-5b/FR-013)
#include <fixpp/session/detail/log_rate_gate.hpp>
#include <fixpp/session/direction.hpp>  // 005 US4: direction_t (store outbound)
#include <fixpp/session/logon_credentials.hpp>  // 034: frame_has_genuine_tag554 / mask_tag554_same_length_inplace
#include <fixpp/session/message_store.hpp>          // 008-message-store — store_ unique_ptr dtor
#include <fixpp/session/message_store_factory.hpp>  // 008-message-store — make() call site
#include <fixpp/session/retrieve_visitor.hpp>       // 013 FR-010/FR-012: resend store-walk visitor
#include <fixpp/session/security_profile.hpp>  // SecurityProfile::kind::unset sentinel check (lives in `session` per [arch §6]'s `SecurityProfile` bullet)
#include <fixpp/session/sending_time.hpp>      // 005 US5: check_sending_time (T055)
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>  // 005 US2: SeqnumManager (T031)
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>  // 013 T036: SessionEvent variants
#include <fixpp/session/session_fsm.hpp>    // 005 US1: fsm_state enum (T023–T025)
#include <fixpp/transport/transport_factory.hpp>  // cfg_.transport_factory_override deref (reconnect_fsm.hpp now fwd-decls it per [const §XV.9])
#include <fixpp/wire/framer.hpp>             // 093 E-1: garble_summary (note_garbles_)
#include <fixpp/wire/length_data_carry.hpp>  // fixpp#426: counted Data values
#include <fixpp/wire/tag_scan.hpp>  // fixpp#421: accumulate_tag_digit (send + replay scanners)
#include <fixpp/wire/writer.hpp>    // 013 FR-010: replay-frame re-serialization
// 014 T015: handshake_result full definition needed for install_reconnected_transport.
// session.cpp is in the session layer; transport is an allowed dependency ([arch §5]).
#include <fixpp/transport/tls_transport.hpp>
// 033 T006/T008: version_registry + version_profile full definitions.
// Not in session.hpp (fwd-decl only there per [const §XV.9] closure guard).
#include <fixpp/dict/dictionary_snapshot.hpp>  // fixpp#215 item 1 (Option C): dict_snapshot / shared_dictionary_view
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>

#include "fixt_route_set.hpp"            // 099 data-model E-1
#include "fixt_session_check.hpp"        // 099 data-model E-9
#include "inbound_limit.hpp"             // 093 E-2: L, the carry and the parse buffer
#include "logon_msg_type_defaults.hpp"   // 099 data-model E-10
#include "msgtype_classifier.hpp"        // 019 T006: is_admin_msgtype (session-internal)
#include "peek_fixt_route.hpp"           // 099 data-model E-7
#include "read_first_frame_bounded.hpp"  // 093 C-6: await_deadline for close()'s wait
#include "scan_frame_header.hpp"  // 040 US1: FrameHeader + scan_frame_header (moved from anon ns)
#include "send_header_tags.hpp"   // 098: kSendHeaderTags + is_send_header_tag (moved from here)
#include "session_strand.hpp"     // 103: detail::session_strand_check
// 019 T011: Application callback dispatch (inbound). Include here (session.cpp
// only) to avoid pulling wire/parser.hpp into the awaitable-corpus headers.
// session → wire is ALLOWED per [arch §5.3] / check_layers.py.
#include <fixpp/session/application.hpp>  // Application::fromAdmin / fromApp
#include <fixpp/session/engine.hpp>       // SessionId::from_config
#include <fixpp/wire/parser.hpp>          // wire::Parser<Index>, wire::MessageView<Index>
// 041-validation-gate-wiring T014: full definition of dictionary_driven_validator
// (forward-declared in session.hpp to keep it out of the awaitable closure per
// [const §XV.9]). Also includes dict/table_view.hpp for as_table_view().
// session → wire is ALLOWED per [arch §5.3] / check_layers.py.
#include <fixpp/wire/reject_reason_map.hpp>  // T011: wire_error_to_session_reject_reason
#include <fixpp/wire/validator.hpp>          // T014: dictionary_driven_validator
// NOTE: fixpp/tls/peer_identity.hpp is transitively available via session_config.hpp
// → compid_authorization_policy.hpp → peer_identity.hpp. A direct include from
// session.cpp would violate [arch §2.3] session→tls edge (check_layers.py).
// We rely on the transitive include to access fixpp::tls::peer_identity.
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace fixpp::session {

namespace {
// The HeartBtInt a session runs when heartbeat_interval is unset (D-8); 105 (tasks.md T105)
// moves the constant into read_first_frame_bounded.hpp, which engine.cpp includes too.
using detail::kDefaultHeartBtInt;

// 093 (data-model E-2): one block from a memory resource, max_align_t-aligned, freed
// at scope exit unless release()d. Its allocation is noexcept: a bad_alloc leaves it
// empty, which open() reports as its error.
class owned_block {
public:
    owned_block(std::pmr::memory_resource* mr, std::size_t bytes) noexcept : mr_{mr} {
        try {
            data_ = static_cast<std::byte*>(mr->allocate(bytes, alignof(std::max_align_t)));
            size_ = bytes;
        } catch (std::bad_alloc const&) {
            data_ = nullptr;
        }
    }
    owned_block(owned_block const&) = delete;
    owned_block& operator=(owned_block const&) = delete;
    owned_block(owned_block&&) = delete;
    owned_block& operator=(owned_block&&) = delete;
    ~owned_block() {
        if (data_ != nullptr) mr_->deallocate(data_, size_, alignof(std::max_align_t));
    }
    explicit operator bool() const noexcept { return data_ != nullptr; }
    [[nodiscard]] std::span<std::byte> release() noexcept {
        return {std::exchange(data_, nullptr), size_};
    }

private:
    std::pmr::memory_resource* mr_;
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

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

// 033: the FIXT-only Logon fields (DefaultApplVerID(1137) + optional Username(553)/
// Password(554)) derived from SessionConfig — all-nullopt for a non-FIXT session, so a
// FIX.4.x Logon emit is byte-identical (INV-FIXT-1 / SC-002 / W4). Shared by the initiator
// emit and the acceptor reply (each advertises its OWN config; R1/FR-002). The string_views
// point into cfg.username/password (Session-lifetime) — valid for the synchronous build_logon
// call at the use site.
struct logon_fixt_fields {
    std::optional<fixpp::dict::application_version> default_appl_ver_id;
    std::optional<std::string_view> username;
    std::optional<std::string_view> password;
};
[[nodiscard]] logon_fixt_fields derive_logon_fixt_fields(const SessionConfig& cfg) noexcept {
    if (!cfg.is_fixt()) {
        return {};
    }
    return logon_fixt_fields{
        .default_appl_ver_id = cfg.default_appl_ver_id,
        .username = cfg.username.has_value() ? std::optional<std::string_view>{*cfg.username}
                                             : std::nullopt,
        .password = cfg.password.has_value() ? std::optional<std::string_view>{*cfg.password}
                                             : std::nullopt,
    };
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

Session::Session(const fixpp::core::EngineConfig& engine, const SessionConfig& cfg,
                 const fixpp::dict::version_registry* app_version_registry)
    : engine_(engine),
      cfg_(cfg),
      session_arena_(resolve_session_arena(engine, cfg)),
      app_version_registry_(app_version_registry),
      reconnect_fsm_(
          cfg.transport_factory_override.get(),  // non-owning raw ptr; factory owned by cfg_
          resolve_reconnect_policy(cfg, session_arena_)) {  // 016 T008: was empty
                                                            // ReconnectPolicy{} (busy-spin)
    // Resolution chain always terminates at std::pmr::get_default_resource()
    // (never null), so I-18's never-null contract holds for the lifetime.
    // reconnect_fsm_ owns AwaitingResend state (FR-009 per data-model §E-1).
    //
    // 033 T006: capture the engine-built application version registry (nullable;
    // null for test/FIX.4.x paths that don't need serviceability checks).
    // Assignment in body (not member-init) to match declaration order in session.hpp
    // (app_version_registry_ is declared after reconnect_fsm_'s logical grouping).
}

// D-23: release any per-session Clock state (system_clock_source's reusable
// timer-slot pool keyed by Session*, etc.) BEFORE the session_arena memory
// is reclaimed by member destruction. Idempotent — the default Clock hook
// is a no-op for clocks without per-session state. effective_clock_ is
// nullptr until open() resolves it (state_ == never_opened); after a
// successful open() it remains non-null until destruction (the Clock is
// shared_ptr-owned by EngineConfig and outlives the session — [2d §4.1]).
Session::~Session() {
    // 103 (research D4.4): close() joins the too-high Logout's bound; a Session destroyed
    // with one running was not closed first.
    assert(peer_logout_bound_counter_->load(std::memory_order_acquire) == 0 &&
           "Session destroyed with the too-high Logout's bound running (close() first)");
    // 105 (research D1 item 5): close() joins the close frame, as it joins the bound.
    assert(connection_close_counter_->load(std::memory_order_acquire) == 0 &&
           "Session destroyed with a connection close frame running (close() first)");
    // 105 (data-model E-3): close() joins the ending Logout's bound.
    assert(ending_logout_bound_counter_->load(std::memory_order_acquire) == 0 &&
           "Session destroyed with the ending Logout's bound running (close() first)");
    // 106 (data-model E-3, the joiners): close() joins the connection bound, as it joins 105's.
    assert(connection_bound_counter_->load(std::memory_order_acquire) == 0 &&
           "Session destroyed with the connection bound running (close() first)");
    if (effective_clock_) {
        effective_clock_->forget_session(this);
    }
    // 093 (data-model E-2): the carry, its resource and its block, in reverse order of
    // construction, then the parse buffer. Both blocks are open()'s, absent before it.
    carry_.reset();
    carry_resource_.reset();
    if (!carry_block_.empty()) {
        carry_arena_->deallocate(carry_block_.data(), carry_block_.size(),
                                 alignof(std::max_align_t));
    }
    if (!inbound_parse_block_.empty()) {
        session_arena_->deallocate(inbound_parse_block_.data(), inbound_parse_block_.size(),
                                   alignof(std::max_align_t));
    }
}

// 093 (contract C-3 I-2): the spill witness counts, then forwards.
void* Session::spill_witness::do_allocate(std::size_t bytes, std::size_t alignment) {
    spills_.fetch_add(1U, std::memory_order_relaxed);
    return ::fixpp::detail::arena_upstream()->allocate(bytes, alignment);
}

void Session::spill_witness::do_deallocate(void* p, std::size_t bytes, std::size_t alignment) {
    ::fixpp::detail::arena_upstream()->deallocate(p, bytes, alignment);
}

bool Session::spill_witness::do_is_equal(std::pmr::memory_resource const& other) const noexcept {
    return this == &other;
}

std::pmr::memory_resource* Session::session_arena() const noexcept {
    return session_arena_;  // I-18: frozen at ctor, never null, never swapped
}

// 033 T008 / data-model E2 — negotiated FIXT application version profile.
// Returns {session=vt11, default_appl=negotiated_appl_version_} for FIXT
// sessions where negotiated_appl_version_ has been set at inbound-Logon.
// Returns {session=Unknown, default_appl=Unknown} for FIX.4.x sessions or
// FIXT sessions not yet past inbound-Logon (negotiated_appl_version_==Unknown).
// Strand-confined; return by value (4-byte trivially copyable).
// [033 data-model.md E2; FR-005; SC-006/W5; INV-FIXT-2]
fixpp::dict::version_profile Session::negotiated_version_profile() const noexcept {
    if (negotiated_appl_version_ == fixpp::dict::application_version::Unknown) {
        return fixpp::dict::version_profile{
            .session = fixpp::dict::session_version::Unknown,
            .default_appl = fixpp::dict::application_version::Unknown,
            .has_per_message_override = false,
            ._reserved = 0};
    }
    return fixpp::dict::version_profile{.session = fixpp::dict::session_version::vt11,
                                        .default_appl = negotiated_appl_version_,
                                        .has_per_message_override = false,
                                        ._reserved = 0};
}

// ── fixpp#564 (`.specify/563-564-callback-send.md` §2.2) — the debug guard ────
//
// The owner token's value for the calling thread: the address of an object with
// thread storage duration. Each live thread has its own.
const void* Session::callback_thread_tag_() noexcept {
    thread_local char tag = 0;
    return &tag;
}

// The same-thread assertions of §2.2. The owner token (callback_owner_hold in
// session.hpp) lets a nested entry on the owner's thread pass; these name the
// nestings that are outside the contract, each where it begins. Each kind has
// one text, whichever site evaluates it, and no text contains another's or the
// token's, so a death test can match the assertion it is written for. They are
// macros so that an abort names the function and line of the site.
//
// A parse site is a function that builds a memory resource over
// inbound_parse_buf_ (re-derive: `grep -n "inbound_parse_buf_" src/session/session.cpp`).
// It evaluates this before it builds the resource, ahead of any scope.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FIXPP_ASSERT_NO_CALLBACK_AT_PARSE_SITE()                        \
    assert(callback_depth_ == 0 &&                                      \
           "inbound parse begun inside a callback of the same session " \
           "(on_inbound_frame() called from an application callback?)")
// 101-inbound-reject-fidelity D7 (FR-008): an inbound reply takes the session's inbound reply
// slot only while the slot is free; on_inbound_frame calls that overlap on the same Session are
// outside its contract. Evaluated by inbound_reply_slot_hold::take before it sets the flag.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FIXPP_ASSERT_INBOUND_REPLY_SLOT_FREE()                              \
    assert(!s_->inbound_reply_slot_held_ &&                                 \
           "inbound reply slot taken while another inbound reply holds it " \
           "(overlapping on_inbound_frame() calls on one Session?)")
// The stack-arena overload of parse_and_dispatch_ evaluates this after its
// callback_dispatch_scope is constructed and before it raises the outbound depth.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FIXPP_ASSERT_NO_OUTBOUND_CALLBACK_AT_OUTBOUND_ENTRY()              \
    assert(outbound_callback_depth_ == 0 &&                                \
           "toApp or toAdmin entered inside toApp or toAdmin of the same " \
           "session (a callback ran the session's executor?)")
// The onLogon and onLogout sites evaluate this after the site's
// callback_dispatch_scope is constructed and before the callback is called.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FIXPP_ASSERT_NO_OUTBOUND_CALLBACK_AT_LIFECYCLE_ENTRY()                \
    assert(outbound_callback_depth_ == 0 &&                                   \
           "onLogon or onLogout entered inside toApp or toAdmin of the same " \
           "session (the session entered or left Active inside an emit)")
// The deferred path of Session::send evaluates this when it resumes after its post,
// before it starts the send (§2.1, "When the post does not move the send out of the
// callback").
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FIXPP_ASSERT_NO_OUTBOUND_CALLBACK_AT_DEFERRED_SEND()                    \
    assert(outbound_callback_depth_ == 0 &&                                     \
           "deferred send resumed inside toApp or toAdmin of the same session " \
           "(a callback ran the session's executor?)")
// Session::close evaluates this when it resumes after the wait it makes for the
// callbacks of the session to return, before it reads the lifecycle (§2.1a,
// "`Session::close`, both modes (R-4)").
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FIXPP_ASSERT_NO_CALLBACK_AT_WAITED_CLOSE()                \
    assert(callback_depth_ == 0 &&                                \
           "close resumed inside a callback of the same session " \
           "(a callback ran the session's executor?)")
// 103 (research D1 step 0b): drive_reconnect evaluates this after the same wait.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FIXPP_ASSERT_NO_CALLBACK_AT_WAITED_RECONNECT()                      \
    assert(callback_depth_ == 0 &&                                          \
           "drive_reconnect resumed inside a callback of the same session " \
           "(a callback ran the session's executor?)")

// ── FR-004 / D-2 — FSM transition ring-buffer helpers ────────────────────
// record_state_transition_: write new_state into the 16-slot ring and advance
// fsm_state_. The write index (fsm_visit_write_idx_) is a separate uint32 that
// always advances; the public count (fsm_visit_count_) saturates at UINT8_MAX
// to signal "≥255 transitions" without freezing the ring rotation.
// 105 D1 (research D1 item 1; data-model E-2): the edge sets the connection-end latch; its
// consumers are the pump after the frame (session_engine_access::take_connection_end) and
// every detached root and application call at its exit (release_connection_end_). The owe runs
// on a transition into Disconnected from another state only, above the no-application return.
void Session::record_state_transition_(fsm_state new_state) noexcept {
    const bool was_active = (fsm_state_ == fsm_state::Active);
    const fsm_state prior = fsm_state_;

    fsm_visit_history_[fsm_visit_write_idx_++ % 16] = new_state;
    if (fsm_visit_count_ < std::numeric_limits<std::uint8_t>::max()) {
        ++fsm_visit_count_;
    }
    fsm_state_ = new_state;
    // 103 (research D5′; data-model "Replay gate", wake sources): the Disconnected edge wakes
    // the replay gate's waiters, which then refuse or abort.
    if (new_state == fsm_state::Disconnected) {
        replay_gate_.wake_all();
        // 103 (research D4.4, D4.6): the edge clears the Logout owner and wakes the bound.
        logout_in_flight_ = false;
        if (prior != fsm_state::Disconnected) {
            owe_connection_end_();
        }
        wake_peer_logout_bound_();
        // 106 D11 (research D11; data-model E-10): the edge takes a free claim, so a decider after
        // any ending finds it taken, and wakes the connection bound, whose exit row then holds.
        if (ending_claim_ == ending_claim::none) {
            ending_claim_ = ending_claim::internal;
        }
        wake_connection_bound_();
    }

    // 093 (data-model E-6): the establishment deadline's disarm. Set here, before the
    // no-application return below, so a session with no application attached stops
    // testing the deadline once Active too.
    if (new_state == fsm_state::Active) {
        reached_active_ = true;
    }

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
        FIXPP_ASSERT_NO_OUTBOUND_CALLBACK_AT_LIFECYCLE_ENTRY();
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
        FIXPP_ASSERT_NO_OUTBOUND_CALLBACK_AT_LIFECYCLE_ENTRY();
        auto r = invoke_callback_safe([&]() { engine_.application->onLogout(sid); });
        (void)cs;
        // A throw from onLogout is absorbed: the session is already leaving Active.
        // No additional close(terminal) needed. app_callback_threw is silently
        // noted; the fire-once guard (onLogout_fired_) prevents re-entry.
        (void)r;
    }
}

// 105 (data-model E-2, "The one setter"): see session.hpp.
// 105 (data-model E-3): a wait armed before Active refuses the owe (the refusal's own
// Disconnected, row 6); a wait armed after Active is ended by it (row 12).
void Session::owe_connection_end_() noexcept {
    if (ending_wait_.phase == ending_wait_state::phase_t::armed) {
        if (!ending_wait_.from_active()) {
            return;
        }
        resolve_ending_wait_();
    }
    if (state_ == lifecycle::open && !reconnect_drain_in_flight_) {
        connection_end_owed_ = conn_gen_;
    }
}

// 105 (research D1 item 4; data-model E-2, "The close frame"): see session.hpp. Every test is
// made here, synchronously, on the strand; the frame decides nothing. The spawn follows
// on_inbound_active_too_high_logout_'s shape for its join (the coroutine's parameter), not for
// its catch: this catch fails closed and does not rethrow (clarification D-g).
// 105 (data-model E-3, "Other roots during writing"): while an ending Logout is being written
// the release defers to the writer's root.
void Session::release_connection_end_() noexcept {
    if (!connection_end_owed_ || *connection_end_owed_ != conn_gen_ || state_ != lifecycle::open ||
        inbound_depth_ != 0 || ending_wait_.phase == ending_wait_state::phase_t::writing) {
        return;
    }
    auto transport = live_transport_shared_();
    if (!transport) {
        return;
    }
    spawn_connection_close_(std::move(transport));
    connection_end_owed_.reset();
}

void Session::spawn_connection_close_(
    std::shared_ptr<fixpp::transport::Transport> transport) noexcept {
    if (!transport) {
        return;
    }
    try {
        connection_close_join join{connection_close_counter_};
        if (close_frame_spawn_fails_ && close_frame_spawn_fails_()) {
            throw std::bad_alloc{};
        }
        asio::co_spawn(exec_.underlying(), close_transport_(std::move(join), transport),
                       asio::detached);
    } catch (...) {
        // The close cannot be spawned: the synchronous close of the transport given, as
        // close()'s grace makes it (contract C-8.6; on TLS abortive while an SSL operation is
        // suspended, L-105-h). Its result is discarded, as the grace's is.
        (void)transport->close();
    }
}

// 105 (research D1 item 5): see session.hpp.
asio::awaitable<void> Session::close_transport_(
    [[maybe_unused]] connection_close_join join,
    std::shared_ptr<fixpp::transport::Transport> transport) {
    try {
        (void)co_await transport->close_async();
    } catch (...) {  // NOLINT(bugprone-empty-catch): a detached frame; nothing awaits it
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

// ── 093-inbound-frame-dispositions — garbled-frame accounting (data-model E-4,
// E-12) ─────────────────────────────────────────────────────────────────────
// A summary with regions == 0 only continued a region an earlier summary opened and
// counted, so it adds nothing, emits nothing and logs nothing.
// The log record is the session's first production log site (plan OD-5): FIXPP_SLOG
// with the session's trace_context, its format string registered in
// src/log/format_registry.cpp. At most one record per max(HeartBtInt, 1 s) on
// effective_clock_. A record names the first region of the summary that triggered it
// (its kind) and that summary's discarded bytes, and carries the number of regions
// counted since the previous record that no record named: every region of each
// rate-suppressed summary, plus the triggering summary's other regions (plan OD-21).
// So, when the logger drops no record, the sum over records of (1 + that number)
// equals garbled_frame_count() when the last record is written. HeartBtInt is the
// configured heartbeat_interval, or kDefaultHeartBtInt when it is unset, as
// run_liveness_loop resolves it; a HeartBtInt of 0 is legal, and the 1 s floor still
// bounds the rate. The logger's overflow policy decides what a full queue does; its
// default, drop_newest, drops the record without blocking the strand, and the count
// stays exact.
void Session::note_garbles_(fixpp::wire::garble_summary const& g) noexcept {
    if (g.regions == 0U) {
        return;
    }
    garbled_frames_.fetch_add(g.regions, std::memory_order_relaxed);
    auto const bytes = static_cast<std::uint32_t>(
        std::min<std::size_t>(g.discarded, std::numeric_limits<std::uint32_t>::max()));
    emit_event(session_event_garbled_frame{
        .first_kind = g.first_kind, .frames = g.regions, .discarded_bytes = bytes});

    auto const suppressed = admit_log_record_(garble_log_gate_, g.regions);
    if (!suppressed) {
        return;
    }
    std::uint64_t const unnamed = *suppressed + (g.regions - 1U);
    FIXPP_SLOG(logger_.get(), warn, get_trace_context(), fixpp::log::cat::session,
               "inbound garbled frame disregarded: kind={} discarded_bytes={} "
               "suppressed_since_last={}",
               fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(g.first_kind)),
               fixpp::log::ArgValue::from_u64(g.discarded),
               fixpp::log::ArgValue::from_u64(unnamed));
}

std::optional<std::uint64_t> Session::admit_log_record_(log_rate_gate& gate,
                                                        std::uint64_t occurrences) noexcept {
    auto const now =
        effective_clock_ ? effective_clock_->steady_now() : fixpp::core::steady_time_point{};
    // 105 (research D8; data-model E-7): the admit step is detail::admit_log_record, its window's
    // end detail::log_rate_window_end.
    return detail::admit_log_record(
        gate, now, detail::log_rate_window_end(now, cfg_.heartbeat_interval), occurrences);
}

// ── 093-inbound-frame-dispositions — the establishment timeout (contract C-4;
// data-model E-5, E-12) ─────────────────────────────────────────────────────
// The event, then one FIXPP_SLOG record carrying T, through the logger open() resolved
// and with the session's trace_context; its format string is registered in
// src/log/format_registry.cpp. No rate bound: the pump closes the connection right
// after, so it is written at most once per connection.
void Session::note_establishment_timeout_() noexcept {
    emit_event(session_event_establishment_timeout{});
    FIXPP_SLOG(logger_.get(), warn, get_trace_context(), fixpp::log::cat::session,
               "session not established within logon_timeout_ms={}",
               fixpp::log::ArgValue::from_u64(cfg_.logon_timeout_ms));
}

// ── 104-sequence-reset-fidelity — a failed receipt reset (contract RT-4, O16) ──
// At a received Logout's reset point, when the receipt reset unit returned an error: one
// FIXPP_SLOG record carrying the part that failed (the manager set, or the store's reset_to)
// and its error, through the logger open() resolved and with the session's trace_context; its
// format string is registered in src/log/format_registry.cpp. No rate bound: a connection has
// one receipt reset point. On a refusing persistent store the stored numbers keep their
// pre-reset values (L-104-2).
void Session::note_receipt_reset_failed_(reset_unit_result const& unit) noexcept {
    if (unit.manager && unit.store) {
        return;
    }
    auto const failure = !unit.manager ? unit.manager.error() : unit.store.error();
    FIXPP_SLOG(logger_.get(), warn, get_trace_context(), fixpp::log::cat::session,
               "received Logout's sequence reset failed, not retried: part={} error={}",
               !unit.manager ? FIXPP_SLIT("manager") : FIXPP_SLIT("store"),
               fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(failure)));
}

// fixpp#708 (contract RT-4): a throw from the receipt reset unit's store reset_to. One
// FIXPP_SLOG record naming the store part, as note_receipt_reset_failed_'s, with its own format
// string (registered in src/log/format_registry.cpp) since a throw carries no error value.
void Session::note_receipt_reset_threw_() noexcept {
    FIXPP_SLOG(logger_.get(), warn, get_trace_context(), fixpp::log::cat::session,
               "received Logout's sequence reset threw, not retried: part={}", FIXPP_SLIT("store"));
}

// 104 (contract RT-4), fixpp#708: every receipt arm's reset point awaits this. A throw out of
// run_reset_unit_ is the store part's: the manager set reports its failures by return, and the
// unit clears reset_unit_in_flight_ before it rethrows. The throw is logged and rethrown
// unchanged, so the arm's ending is the one it has without the record. The handler sits in this
// coroutine's frame rather than in the arms' frames.
asio::awaitable<void> Session::run_receipt_reset_unit_() noexcept {
    try {
        note_receipt_reset_failed_(co_await run_reset_unit_(seqnum_min, seqnum_min));
    } catch (...) {
        note_receipt_reset_threw_();
        throw;
    }
}

// ── 093-inbound-frame-dispositions — a frame over L (FR-013; data-model E-12) ──
// One FIXPP_SLOG record carrying the failure kind and L, through the logger open()
// resolved and with the session's trace_context; its format string is registered in
// src/log/format_registry.cpp. No rate bound: the pump closes the session right after,
// so it is written at most once per connection.
void Session::note_frame_too_large_() noexcept {
    FIXPP_SLOG(logger_.get(), warn, get_trace_context(), fixpp::log::cat::session,
               "inbound frame over the limit closed the session: kind={} limit={}",
               fixpp::log::ArgValue::from_u64(
                   static_cast<std::uint64_t>(fixpp::core::error::wire_frame_too_large)),
               fixpp::log::ArgValue::from_u64(inbound_limit_));
}

// ── parse_and_dispatch_ ───────────────────────────────────────────────────────
//
// Shared parse-and-callback ritual for the receive callbacks (fromAdmin/fromApp) over
// inbound frames and the send callbacks (toAdmin/toApp) over frames fixpp built. Each
// call site passes its arena size. Re-derive the call sites with contract C-6's command,
// `grep -n "parse_and_dispatch_(\|validate_inbound_(" src/session/session.cpp`, and read
// each call's byte provenance (received from the peer, or built by fixpp).
//
// Arena sizing for the stack overload: two named constants document the intentional
// difference.
//   kAdminParseArena: admin messages fixpp builds (Heartbeat/Logon/TestRequest/…).
//   kSendParseArena: the outbound application frames fixpp builds (toApp).
// 093-inbound-frame-dispositions (data-model E-2, contract C-3) supersedes the
// 16 KiB inbound stack arena: every late inbound site calls the inbound_parse_t
// overload, which parses over the session's parse buffer B(L). The admin and outbound
// sites keep their stack arenas (C-3 I-6).
//
// On parse failure (Framer or Parser): returns dispatch_outcome::parse_failed and
// the callback does not run. Every late inbound site (a call over bytes received
// from the peer) treats it as terminal through close_on_late_parse_failure_; the
// sites that parse a frame fixpp built ignore it. Superseded decision: a parse
// failure used to return success, "skip, not fatal", read by every caller as a
// delivered frame; 092-garbled-frame-reject contract C-6 (owner ruling O-2)
// replaced it. Re-derive the late sites with contract C-6's command.
// [const §VIII.5] (stack-only — no heap between parse and callback)
// [019-app-callbacks T011/T013/T014/T016; 092 data-model E-3, contract C-6]

namespace {
constexpr std::size_t kAdminParseArena = 8192;  // admin frames: bounded small
constexpr std::size_t kSendParseArena = 16384;  // outbound app frames: larger payloads

// Raises the outbound-callback depth for one toApp or toAdmin call and restores the
// value it found, on every exit (`.specify/563-564-callback-send.md` §2.1 "Writer").
class outbound_callback_raise {
public:
    explicit outbound_callback_raise(std::uint32_t& depth) noexcept
        : depth_(depth), previous_(depth) {
        ++depth_;
    }
    ~outbound_callback_raise() noexcept { depth_ = previous_; }
    outbound_callback_raise(const outbound_callback_raise&) = delete;
    outbound_callback_raise(outbound_callback_raise&&) = delete;
    outbound_callback_raise& operator=(const outbound_callback_raise&) = delete;
    outbound_callback_raise& operator=(outbound_callback_raise&&) = delete;

private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::uint32_t& depth_;
    std::uint32_t previous_;
};
}  // namespace

template <class CB>
[[nodiscard]] fixpp::core::expected_t<Session::dispatch_outcome> Session::parse_and_dispatch_(
    std::span<const std::byte> frame, std::size_t arena_bytes, CB&& cb) noexcept {
    // Stack parse arena ([const §VIII.5] — no heap).
    // arena_bytes is caller-supplied so the size choice is explicit at each site.
    std::array<std::byte, kSendParseArena> pa_buf_storage{};
    // We always allocate the max stack size but hand the requested slice to the MBR.
    // Both constants fit; static_assert guards this.
    static_assert(kAdminParseArena <= kSendParseArena);
    std::pmr::monotonic_buffer_resource pa_mr{pa_buf_storage.data(), arena_bytes,
                                              ::fixpp::detail::arena_upstream()};
    std::array<std::byte, 512> carry_store{};
    std::pmr::monotonic_buffer_resource carry_mr{carry_store.data(), carry_store.size(),
                                                 ::fixpp::detail::arena_upstream()};
    fixpp::wire::pmr_carry_buffer carry{carry_store.size(), &carry_mr};
    fixpp::wire::Framer pd_framer;
    std::array<fixpp::wire::frame_view, 1> pd_out{};
    auto feed_r = pd_framer.feed(frame, carry, std::span<fixpp::wire::frame_view>{pd_out});
    if (!feed_r || feed_r->empty()) return dispatch_outcome::parse_failed;

    // 066-dict-backed-inbound-parse T006: dict-backed parse — inbound_tv_ is
    // GUARANTEED (see hpp comment above the member + Session::open()): both
    // callers (fire_to_admin_ and the receive loop) run only post-open.
    assert(inbound_tv_ != nullptr);
    // fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.6): the OWNED route,
    // so a reify or clone of a view handed to an application callback (C++ and C)
    // shares the table; inbound_tv_ is its owner object (session.hpp).
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> pd_parser{
        fixpp::wire::detail::owned_route_key{}, inbound_tv_};
    auto mv_r = pd_parser.parse((*feed_r)[0], &pa_mr);
    if (!mv_r) return dispatch_outcome::parse_failed;

    const SessionId sid = SessionId::from_config(cfg_);
    // fixpp#563/#564 (`.specify/563-564-callback-send.md` §2.1 "Writer", §2.2): this
    // overload is the one route to toApp and toAdmin, so it is the one writer of the
    // outbound depth. The order is: the scope, the assertion, the raise, the callback.
    // The raise is declared after the scope, so it is restored before the scope exits.
    callback_dispatch_scope cs{*this};
    FIXPP_ASSERT_NO_OUTBOUND_CALLBACK_AT_OUTBOUND_ENTRY();
    const outbound_callback_raise outbound{outbound_callback_depth_};
    auto result = invoke_callback_safe([&]() { return std::forward<CB>(cb)(*mv_r, sid); });
    (void)cs;
    if (!result) return std::unexpected(result.error());
    return dispatch_outcome::dispatched;
}

// 093-inbound-frame-dispositions (data-model E-2, contract C-3 I-1..I-3): the late
// inbound sites' overload. The parse runs over a fresh monotonic_buffer_resource on the
// session's parse buffer, upstream the spill witness, so a draw past the buffer is
// recorded (and refused where the witness's upstream is null). The entry cap is N(L),
// and the entries are reserved up front for the most fields the frame can hold, which
// B(L) budgets. One buffer serves every inbound parse because none nests inside
// another: callbacks are synchronous, and no parse over the buffer begins while a
// callback of the session is on the stack. On a build with assertions each parse
// site asserts that (fixpp#564, `.specify/563-564-callback-send.md` §2.2).
template <class CB>
[[nodiscard]] fixpp::core::expected_t<Session::dispatch_outcome> Session::parse_and_dispatch_(
    std::span<const std::byte> frame, inbound_parse_t /*tag*/, inbound_route const& route,
    CB&& cb) noexcept {
    FIXPP_ASSERT_NO_CALLBACK_AT_PARSE_SITE();
    std::pmr::monotonic_buffer_resource pa_mr{inbound_parse_buf_.data(), inbound_parse_buf_.size(),
                                              &parse_spill_witness_};
    std::array<std::byte, 512> carry_store{};
    std::pmr::monotonic_buffer_resource carry_mr{carry_store.data(), carry_store.size(),
                                                 ::fixpp::detail::arena_upstream()};
    fixpp::wire::pmr_carry_buffer carry{carry_store.size(), &carry_mr};
    fixpp::wire::Framer pd_framer;
    std::array<fixpp::wire::frame_view, 1> pd_out{};
    auto feed_r = pd_framer.feed(frame, carry, std::span<fixpp::wire::frame_view>{pd_out});
    if (!feed_r || feed_r->empty()) return dispatch_outcome::parse_failed;

    assert(*route.entry.owner != nullptr);
    // fixpp#495: the OWNED route, as in the stack overload above. Its owner object is the route's,
    // which is inbound_tv_ on a non-FIXT session (099, data-model E-6).
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> pd_parser{
        fixpp::wire::detail::owned_route_key{}, *route.entry.owner};
    auto mv_r =
        pd_parser.parse((*feed_r)[0], &pa_mr,
                        fixpp::wire::OffsetTable::Config{.max_offset_entries = inbound_entry_cap_},
                        detail::parse_capacity::reserve_for(inbound_entry_cap_, frame.size()));
    if (!mv_r) return dispatch_outcome::parse_failed;

    // 099 (data-model E-5): the version the frame's route resolved, for the callback's view.
    fixpp::wire::detail::message_view_resolved_access::set(*mv_r, route.resolved);

    const SessionId sid = SessionId::from_config(cfg_);
    callback_dispatch_scope cs{*this};
    auto result = invoke_callback_safe([&]() { return std::forward<CB>(cb)(*mv_r, sid); });
    (void)cs;
    if (!result) return std::unexpected(result.error());
    return dispatch_outcome::dispatched;
}

// ── 092-garbled-frame-reject: close_on_late_parse_failure_ ───────────────────
//
// Contract C-6: the one action at every late inbound parse site whose parse
// failed. Terminal close; no Reject, no Logout; the caller returns this result
// without invoking the receive callback. Returns success, as the session's other
// Disconnected paths do: the close is the outcome, and run_read_pump's own later
// close is idempotent.
// 100-wire-group-read-integrity (research D1, FR-008) supersedes 093's "any admitted
// frame parses" for a frame crafted against the index seed: its index build exceeds
// the per-build probe budget, so its parse fails and it reaches this close with no
// test seam.
// [092 data-model E-3; contract C-6; research R-4; spec FR-016]
asio::awaitable<fixpp::core::expected_t<void>> Session::close_on_late_parse_failure_() noexcept {
    (void)co_await close(close_mode::terminal);
    co_return fixpp::core::expected_t<void>{};
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
bool Session::install_reconnected_transport(std::unique_ptr<fixpp::transport::Transport> transport,
                                            fixpp::transport::handshake_result hr) noexcept {
    // 103 (research D1 step 4): the commit point. A close() or Engine::stop() that ran during
    // the connect or the handshake is caught here, with no suspension between this test and
    // the install: the new transport is closed and nothing is installed or recorded.
    if (closing_or_stopped_()) {
        if (transport) {
            (void)transport->close();
        }
        return false;
    }
    // 103 (research D1 step 3, D4.6): the drain left the old connection quiescent, with no
    // Logout in flight and none of its detached coroutines running.
    assert(!logout_in_flight_ && "a Logout is in flight across a reconnect install");
    // 105 (data-model E-3 row 17; research D5): the next connection's start.
    begin_connection_();
    assert(replay_gate_.shared == 0 && inbound_depth_ == 0 &&
           peer_logout_bound_counter_->load(std::memory_order_acquire) == 0 &&
           liveness_counter_->load(std::memory_order_acquire) == 0 &&
           connection_bound_counter_->load(std::memory_order_acquire) == 0 &&
           "the reconnect installs over a connection that is not quiescent");
    // 1. Store live peer identity for arm (1-live) in the Logon-ack guard.
    //    The peer_id is moved out of hr (hr.peer_id is owning-by-value per
    //    handshake_result::peer_id is owning-by-value). [data-model §E-2; contracts C2; FR-006]
    //
    //    043 T013 (D-10 #2 MUST): for insecure_plain_tcp, live_peer_id_ MUST stay
    //    nullopt — the handshake was skipped so there is no peer identity. Passing a
    //    default hr{} from the FSM plaintext branch yields a present-but-empty
    //    peer_identity; assigning it would leave live_peer_id_ as a present-but-
    //    empty optional, violating the fail-closed-by-construction MUST (D-10 #2).
    //    Guard on the profile rather than on hr.peer_id emptiness.
    //    Every TLS caller passes a real hr from async_handshake — behaviour unchanged.
    //    [data-model §E-5; D-10 #2; 043 T013]
    if (!is_insecure_plain_tcp(cfg_.security_profile.k)) {
        live_peer_id_ = std::move(hr.peer_id);
    }

    // 2. Take ownership of the live transport.
    reconnected_transport_ = std::move(transport);

    // 2a. FQ-A (gate-b/r2): live writes now go through live_write_serialized_()
    //     which reads live_transport_shared_() at call time. No transport_send_
    //     rebind needed for the live path — the live accessor picks up the new
    //     reconnected_transport_ automatically. transport_send_ continues to serve
    //     the pre-live/config-time test path (cfg_.transport_send set at open()).
    //     [data-model §E-1a; T016(c); FR-003; FQ-A gate-b/r2]

    // 2b. 103 (research D1, "Clearing site"; FR-006): the new connection starts with no
    //     resend outstanding. exit_awaiting_resend clears the awaiting flag and resets the
    //     resend state (the outstanding range, started_at, inbound_filled_through), before
    //     the LogonSent transition and so before drive_reconnect's emit_initiator_logon_
    //     sends the new Logon. Then the connection generation moves. Both are noexcept and
    //     allocate nothing. The Disconnected edge clears neither (research D1, "Why the edge
    //     clears neither the resend state nor the mark").
    reconnect_fsm_.exit_awaiting_resend();
    // 103 (data-model, the mark's "new transport installed" row): no bound of the old
    // connection exists (the drain waited for its counter), so the mark is cleared here.
    peer_logout_.phase = peer_logout_mark::phase_t::none;
    ++conn_gen_;
    // 104 (contract RR-2 item 6, RR-6 item 2): a received Logout never resets a later
    // connection, and ruling W's hold ends with the connection it was set on.
    logout_seen_ = receipt_marker::none;
    wire_closed_by_reset_ = false;
    // 104 (contract RR-5 item 1): the thaw, with the marker's `none`, before LogonSent.
    seqnum_mgr_.thaw(SeqnumManager::freeze_key{});
    assert(seqnum_mgr_.frozen(SeqnumManager::freeze_key{}) ==
               (logout_seen_ == receipt_marker::reached) &&
           "104 RR-5 item 4: after install_reconnected_transport's thaw, frozen differs from the "
           "marker reached");

    // 3. Re-enter LogonSent so on_inbound_frame drives back to Active.
    //    The session's next peer Logon-ack will be processed by the LogonSent
    //    row of the FSM matrix, transitioning back to Active.
    //    [data-model §E-1 step 8; FR-001; US1 AC1]
    record_state_transition_(fsm_state::LogonSent);
    return true;
}

// 105-transport-liveness: see session.hpp.
void Session::begin_connection_() noexcept {
    assert((ending_wait_.phase == ending_wait_state::phase_t::none ||
            ending_wait_.phase == ending_wait_state::phase_t::resolved) &&
           "an ending Logout is in progress at a connection's start");
    ending_wait_.phase = ending_wait_state::phase_t::none;
    establishment_cap_.reset();
    test_request_id_floor_ = next_test_request_id_;
    pending_test_req_id_.clear();
    // 106 D11 (data-model E-10): the claim is per connection; no decider of the previous one is
    // alive here (its bound was joined, or asserted gone, before this call).
    ending_claim_ = ending_claim::none;
    emit_in_flight_ = false;
    give_up_ = give_up_state{};
    // 106 (data-model E-5): the write limit's flag and cut epoch, and the bound's once-per-
    // connection latch, are per connection.
    write_in_flight_ = false;
    write_cut_epoch_ = 0;
    connection_bound_.spawned = false;
}

// 103 (research D1 step 3): see session.hpp.
Session::inbound_depth_holder::~inbound_depth_holder() {
    --s_.inbound_depth_;
    if (s_.inbound_depth_ == 0) {
        s_.replay_gate_.wake_all();
    }
}

// 105 (T129): the one join of detached frames' counters (its users: grep -n
// 'FIXPP_JOIN_COUNTERS(' src/session/session.cpp): a post on the caller's executor until every
// counter named reads 0, all of them re-read after each post, and no await at all when none is
// held. A spin and not a wake: a waker would be a member
// every hold's destructor reaches, and a hold is destroyed wherever its frame ends, at its
// execution context's shutdown outside any handler included. A macro and not a coroutine, for
// FIXPP_INBOUND_SPLIT_AWAIT's reason: a co_await of a helper is a cancellation check of its own,
// made even when no counter is held.
namespace {
template <class... Counter>
[[nodiscard]] bool any_counter_held(Counter const&... counter) noexcept {
    return ((counter->load(std::memory_order_acquire) > 0) || ...);
}
}  // namespace
// NOLINTBEGIN(cppcoreguidelines-macro-usage): see above
#define FIXPP_JOIN_COUNTERS(...)          \
    while (any_counter_held(__VA_ARGS__)) \
    co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable)
// NOLINTEND(cppcoreguidelines-macro-usage)

// 103 (research D1 steps 2 and 3; data-model "Quiescent"): see session.hpp.
asio::awaitable<fixpp::core::expected_t<void>> Session::drain_for_reconnect_() noexcept {
    using fixpp::core::error;
    auto const refused = [this]() noexcept { return closing_or_stopped_(); };
    auto const cancelled = [](asio::cancellation_state const cs) noexcept {
        return cs.cancelled() != asio::cancellation_type::none;
    };
    try {
        // 105 (research D1 item 6): the latch is cleared and the close frame joined before the
        // old connection is left, so no close frame outlives its connection. The drain owns
        // that connection's close (drive_reconnect holds reconnect_drain_in_flight_ around
        // this call), so no close is owed here.
        connection_end_owed_.reset();
        // 105 (data-model E-3, the bound's wake sources): the ending Logout's bound re-tests at
        // the drain's entry (its live predicate fails on reconnect_drain_in_flight_).
        wake_ending_logout_bound_();
        // 106 (data-model E-3, the wakers): the connection bound re-tests here too, and leaves on
        // reconnect_drain_in_flight_.
        wake_connection_bound_();
        FIXPP_JOIN_COUNTERS(connection_close_counter_);
        // Step 2: leave the old connection. The edge fires onLogout from Active and wakes
        // the bound and the gate's waiters; from here no emitter can start.
        if (fsm_state_ == fsm_state::Active || fsm_state_ == fsm_state::LogonReceived) {
            record_state_transition_(fsm_state::Disconnected);
        }
        // Step 3: no emitter between its number peek and the return of its store_then_emit
        // (the gate's shared count), and no inbound frame being processed. The condition
        // and step 1's refusal are tested again after every wake.
        if (replay_gate_.shared != 0 || inbound_depth_ != 0) {
            using namespace asio::experimental::awaitable_operators;
            auto const ex = co_await asio::this_coro::executor;
            asio::steady_timer bound{ex};
            bound.expires_after(std::chrono::milliseconds{cfg_.logout_disconnect_timeout_ms});
            bool bound_reached = false;
            while (replay_gate_.shared != 0 || inbound_depth_ != 0) {
                if (refused()) {
                    co_return std::unexpected(error::session_already_closed);
                }
                replay_gate_.assert_on_strand();
                if (!bound_reached) {
                    auto const r = co_await (replay_gate_.wait_for_wake() ||
                                             bound.async_wait(asio::as_tuple(asio::use_awaitable)));
                    if (r.index() == 1) {
                        // At the bound the old transport is force-closed, so a writer
                        // blocked in async_write unwinds; the wait goes on with no deadline.
                        bound_reached = true;
                        if (auto live = live_transport_shared_()) {
                            (void)live->close();
                        }
                    }
                } else {
                    co_await replay_gate_.wait_for_wake();
                }
                if (cancelled(co_await asio::this_coro::cancellation_state)) {
                    co_return std::unexpected(error::dispatch_aborted);
                }
            }
        }
        if (refused()) {
            co_return std::unexpected(error::session_already_closed);
        }
        // The old connection's detached coroutines: the liveness loop ends at the total
        // cancellation close() uses (see `root_cancel_`; cell RS-1), and the bound was woken by
        // step 2's edge.
        root_cancel_.emit(asio::cancellation_type::total);
        FIXPP_JOIN_COUNTERS(peer_logout_bound_counter_, liveness_counter_,
                            connection_bound_counter_);
        // 105 (research D2 item 9; data-model E-2 I-4, I-7): the ending Logout's bound, then the
        // close frame's counter again, so no frame of the old connection is in flight at the
        // install.
        FIXPP_JOIN_COUNTERS(ending_logout_bound_counter_, connection_close_counter_);
        if (refused()) {
            co_return std::unexpected(error::session_already_closed);
        }
    } catch (...) {  // the caller's cancellation, at a suspension of this drain
        co_return std::unexpected(error::dispatch_aborted);
    }
    co_return fixpp::core::expected_t<void>{};
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
    using fixpp::core::error;
    // 105 (research D1 item 7): an application call; at every exit, a refused one included,
    // the connection-end latch is released. Declared first, so it runs after every other
    // holder of this frame.
    connection_end_releaser const release_at_exit{*this};
    // 103 (research D1 step 0b): single flight, for the whole operation.
    if (reconnect_in_flight_) {
        co_return std::unexpected(error::session_already_open);
    }
    struct in_flight_holder {
        bool& f;
        explicit in_flight_holder(bool& flag) noexcept : f{flag} { f = true; }
        ~in_flight_holder() { f = false; }
        in_flight_holder(in_flight_holder const&) = delete;
        in_flight_holder& operator=(in_flight_holder const&) = delete;
    } const in_flight{reconnect_in_flight_};
    // From a callback: a post first, as close()'s R-4 wait, so the closing test below reads
    // the state the call resumes into and onLogout does not run nested in the callback.
    if (callback_depth_ != 0) {
        co_await asio::post(exec_.underlying(), asio::deferred);
        FIXPP_ASSERT_NO_CALLBACK_AT_WAITED_RECONNECT();
    }
    // Step 1: a closing, closed or stopped Session.
    if (closing_or_stopped_()) {
        co_return std::unexpected(error::session_already_closed);
    }
    // Step 1a, the reader fence: the Engine's read pump owns the installed transport.
    if (cfg_.role == session_role::acceptor ||
        (cfg_.engine_managed && live_transport_shared_() != nullptr)) {
        co_return std::unexpected(error::session_already_open);
    }
    // Steps 2 and 3, the drain. 105 (research D1 item 1): reconnect_drain_in_flight_ is held
    // around this call only, so the drain's own Disconnected edge owes no close (B-103-9).
    auto const gen_before_drain = conn_gen_;
    reconnect_drain_in_flight_ = true;
    auto drained = co_await drain_for_reconnect_();
    reconnect_drain_in_flight_ = false;
    if (!drained) {
        // 106 (data-model E-3, "Respawn after a failed drain"; N-6): the bound left on the drain
        // term, and a drain that failed leaves the old connection with its limited write or the
        // loop's emit still in flight, which nothing later re-arms (later writes queue at the
        // write gate, and the loop starts only at Active). The bound is spawned again on this
        // coroutine's executor; it re-arms on the in-flight write's own start. The running term
        // is defensive (the old bound's exit runs during the drain's park). A respawn that
        // fails fails closed: the write is already started, so the claim, Disconnected and the
        // close, which fails that write (research D3, "One close").
        if (conn_gen_ == gen_before_drain && !closing_or_stopped_() &&
            (write_in_flight_ || emit_in_flight_) && !connection_bound_.running &&
            !spawn_connection_bound_(co_await asio::this_coro::executor)) {
            fail_closed_without_bound_();
        }
        co_return drained;
    }
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
// live Transport (satisfies transport.hpp's [2h §4.1] RC#3 in-flight exclusivity contract).
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
// [transport.hpp's [2h §4.1] RC#3 in-flight contract; FQ-A D-6; gate-b/r2]
asio::awaitable<fixpp::core::expected_t<Session::live_write_outcome>>
Session::live_write_serialized_(std::span<const std::byte> frame, wire_reset_exemption exemption,
                                write_class wclass) noexcept {
    auto live = live_transport_shared_();
    if (!live) {
        // No live transport — pre-live path, no-op.
        co_return live_write_outcome::written;
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

    // 104 (contract RR-6 item 3, ruling W): after the grant and before the write, so every
    // frame granted the gate once a reset point has run, one that waited on it included, is
    // held back; a write already handed to the transport before the reset point completes.
    // The exemption is fixpp's own Logout in a crossing (RR-6 item 4).
    if (wire_closed_by_reset_ && exemption != wire_reset_exemption::crossing_logout) {
        co_return live_write_outcome::held_back;
    }

    // 106 D5 supersedes 105 D-e's scope (only ending Logouts are limited): every non-Logout write
    // is limited, opt-in; the clock starts at async_write, not at the gate (105's ending limit
    // counts from the leaf's entry). With SessionConfig::write_timeout_ms set, an ordinary-class
    // write marks its start, epoch and flag here, after the gate's grant and ruling W's test,
    // and the connection bound cuts it at its start + the limit (data-model E-5). The first
    // such write on a connection with no bound spawns it on this coroutine's executor; a spawn
    // that fails fails closed, the write not started (research D3, "Spawn failure"). With the
    // limit off this adds only the knob's test below.
    // The flag's clear: explicit at each return below, and by the destructor only while an
    // exception unwinds the frame, as emit_in_flight_guard's (a frame destroyed unresumed, an
    // io_context torn down with no close(), touches nothing). The clear does not wake the
    // bound: at its deadline it finds the write returned and parks.
    struct write_in_flight_guard {
        Session* s = nullptr;  // the session while its limited write is in flight
        int unwinding_at = std::uncaught_exceptions();
        write_in_flight_guard() noexcept = default;
        write_in_flight_guard(write_in_flight_guard const&) = delete;
        write_in_flight_guard& operator=(write_in_flight_guard const&) = delete;
        void clear() noexcept {
            if (s != nullptr) {
                s->write_in_flight_ = false;
                s = nullptr;
            }
        }
        ~write_in_flight_guard() {
            if (std::uncaught_exceptions() > unwinding_at) {
                clear();
            }
        }
    } in_flight;
    bool limited = false;
    std::uint32_t epoch = 0;
    if (cfg_.write_timeout_ms != 0 && wclass == write_class::ordinary && effective_clock_) {
        if (!connection_bound_.running && !connection_bound_.spawned &&
            !spawn_connection_bound_(co_await asio::this_coro::executor)) {
            fail_closed_without_bound_();
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
        write_started_at_ = effective_clock_->steady_now();
        epoch = ++write_epoch_;
        write_in_flight_ = true;
        in_flight.s = this;
        limited = true;
        // A bound armed on an earlier write's deadline (never later than this one's) re-tests
        // there and re-arms; a later deadline, or a parked bound, is woken.
        if (connection_bound_.armed_until >
            detail::deadline_after(write_started_at_,
                                   std::chrono::milliseconds{cfg_.write_timeout_ms})) {
            wake_connection_bound_();
        }
    }
    // What a failed write returns: session_write_timeout when the bound cut this write (its
    // epoch marked before the close), dispatch_aborted for every other failure (research D5,
    // N-3).
    auto const failed = [&]() noexcept {
        return limited && epoch == write_cut_epoch_ ? fixpp::core::error::session_write_timeout
                                                    : fixpp::core::error::dispatch_aborted;
    };

    // Gate held — at most one async_write in-flight. `live` shared_ptr is the
    // keepalive so the transport cannot be freed while we are suspended here.
    fixpp::core::expected_t<std::size_t> write_r;
    try {
        write_r = co_await live->async_write(frame);
    } catch (const asio::system_error& e) {
        (void)e;
        in_flight.clear();
        co_return std::unexpected(failed());
    }
    in_flight.clear();
    // guard destructor releases the gate when we leave this scope.
    if (!write_r.has_value()) {
        co_return std::unexpected(failed());
    }
    co_return live_write_outcome::written;
}

// 015 T011 — Acceptor attach primitive.
// Called by run_accept_loop STRICTLY-BEFORE the first on_inbound_frame (E-4).
// Two actions (distinct from install_reconnected_transport):
//   1. Store live peer identity for arm (1-live) at on_inbound_frame's NotConnected gate.
//   2. Take ownership of the transport.
// Does NOT rebind transport_send_ — live writes go through live_write_serialized_()
// which reads live_transport_shared_() at call time (FQ-A gate-b/r2).
// Does NOT transition the FSM — the acceptor stays NotConnected; the gate at
// the acceptor's live-binding CompID-authorization gate (arm 1-live) fires when on_inbound_frame
// processes the first Logon. [data-model §E-2; T011; FR-005/006/008; contracts C1 step 5; T-041;
// FQ-A]
void Session::attach_accepted_transport(std::unique_ptr<fixpp::transport::Transport> transport,
                                        fixpp::transport::handshake_result hr) noexcept {
    // 1. Store live peer identity for the acceptor authorization gate (E-4).
    //    Consumed one-shot by the acceptor's live-binding CompID-authorization gate (arm 1-live) in
    //    on_inbound_frame.
    //
    //    043 T013 (D-10 #3 MUST): for insecure_plain_tcp, live_peer_id_ MUST stay
    //    nullopt — the acceptor handshake was skipped; there is no peer identity.
    //    Acceptor twin of install_reconnected_transport's guard above (#2).
    //    Every TLS caller passes a real hr from async_handshake — behaviour unchanged.
    //    [data-model §E-5; D-10 #3; 043 T013]
    if (!is_insecure_plain_tcp(cfg_.security_profile.k)) {
        live_peer_id_ = std::move(hr.peer_id);
    }

    // 105 (data-model E-3 row 17; research D5): the accepted connection's start.
    begin_connection_();
    // 2. Take ownership of the transport.
    // FQ-A: no transport_send_ rebind needed — live_write_serialized_() picks
    // up accepted_transport_ via live_transport_shared_() at call time.
    accepted_transport_ = std::move(transport);
    // FSM NOT advanced — the NotConnected→LogonReceived transition fires at
    // on_inbound_frame's NotConnected gate when the first inbound Logon is processed.
}

// 024 T003 — shared durable-reset helper.
// Body: co_await seqnum_mgr_.reset_to_one() then co_await store_->reset().
// Disposition keys store-failure handling on the trigger CAUSE (not location):
//   fatal  → a store failure propagates → caller can block reaching Active (C2.6
//             knob-driven Logon path).
//   logged → store failure swallowed (I-07 logged-then-proceed; matching the
//             existing store_is_persistent_-ternary disposition pattern for the 013-only 141 path
//             and all teardown paths).
// seqnum_mgr_.reset_to_one() failure always propagates regardless of disposition
// (the live-counter reset is the primary gate; a store failure is I-07-able but
// a seqnum-manager failure is not).
// store_ is null-checked to match the null-guard pattern used below in this helper.
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

// 070-fix44-closeout S-029 — pure decision helper: given the local posture and the
// raw TestMessageIndicator(464) value from the inbound Logon, does the session refuse?
// Symmetric rule (research.md D-A): 464=Y ⇒ peer test; 464=N or empty/absent ⇒ peer
// production. A non-empty 464 value ∉ {Y,N} is malformed → refuse. Only called when
// posture.has_value() (enforcement enabled). [FR-002; data-model E1]
static bool should_refuse_posture(session_posture posture, std::string_view tmi_464) noexcept {
    const bool malformed = !tmi_464.empty() && tmi_464 != "Y" && tmi_464 != "N";
    if (malformed) {
        return true;
    }
    const bool peer_is_test = (tmi_464 == "Y");
    return (posture == session_posture::production && peer_is_test) ||
           (posture == session_posture::test && !peer_is_test);
}

// 070-fix44-closeout S-030 — parse a raw MaxMessageSize(383) wire value to uint32.
// Returns nullopt for empty/malformed/overflowing input (peer advertised nothing
// usable). [FR-007]
static std::optional<std::uint32_t> parse_u32_opt(std::string_view sv) noexcept {
    if (sv.empty()) {
        return std::nullopt;
    }
    std::uint32_t v = 0;
    auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (ec != std::errc{} || p != sv.data() + sv.size()) {
        return std::nullopt;
    }
    return v;
}

// 029 T007 — one-shot cold-open hydration gate (C2.1–C2.6 / INV-H3/H4/H5/H6).
//
// Called at:
//   (a) the first line of emit_initiator_logon_() — before the reset_on_logon block
//       (C2.5/C2.6: outbound hydrate runs BEFORE reset so 024 reset still wins).
//   (b) the acceptor NotConnected inbound-Logon case, AFTER the peer_sent_reset /
//       reset_on_logon header pre-scan and BEFORE check_inbound (C2.5).
//
// The OUTBOUND seed is applied unconditionally; the INBOUND seed is applied only when
// apply_inbound_seed is true (C2.4 split — withheld on the reset-Logon path so the reset
// arm owns the post-state and check_inbound(1) is in-sequence).
//
// Body design (C2.1–C2.3, D-9):
//   one-shot latch  : if (hydrated_)  co_return ok   — already done this lifetime
//   re-entrancy     : if (hydrating_) co_return ok   — concurrent call on same strand
//   non-persistent  : if (!store_is_persistent_) skip reads, co_return ok (INV-H4)
//   read both       : next_seqnum(inbound,false) then next_seqnum(outbound,false)
//   read failure    : Disconnected, no partial seed (C2.3/INV-H6)
//   hydrate         : seqnum_mgr_.hydrate(apply_inbound_seed ? *in : seqnum_min, *out)
//   latch           : hydrated_ = true (set ONLY after success, D-9)
//   every exit      : hydrating_ cleared, by a guard object, so an exception that leaves
//                     a co_await below clears it too (ruling R-20;
//                     `.specify/536-537-538-recovery-liveness.md` §9, D-CAN-1)
//
// [029 tasks T007; contracts C2.1–C2.6; data-model INV-H3..H6; research D-6/D-9/D-10]
// 029 T011 — apply_inbound_seed: when true, seed next_inbound from the store;
// when false (reset-Logon path), withhold the inbound seed so the reset arm owns
// the post-state and check_inbound(1) is in-sequence (RC-1, C2.4, INV-H5).
// [029 tasks T011; contracts C2.4/C2.6; data-model INV-H5; research RC-1/D-6]
asio::awaitable<fixpp::core::expected_t<void>> Session::ensure_hydrated_(
    bool apply_inbound_seed, bool force, std::optional<fsm_state> arm) noexcept {
    // One-shot: already hydrated this session lifetime.
    // force=true (025 refresh_on_logon) bypasses the latch to re-read on each reconnect.
    if (hydrated_ && !force) {
        co_return fixpp::core::expected_t<void>{};
    }
    // Re-entrancy guard (strand-confined; a second co_await before the first returns).
    if (hydrating_) {
        co_return fixpp::core::expected_t<void>{};
    }
    hydrating_ = true;
    // Constructed after the re-entrancy test above, so a re-entrant call leaves the
    // outer call's flag set.
    struct clear_on_exit {
        bool* flag;
        ~clear_on_exit() { *flag = false; }
    } const clear_hydrating{&hydrating_};

    // Non-persistent skip (D-10 / C2.2 / INV-H4): memory store, null store, or any
    // store whose factory returned yields_persistent_store()==false.
    // No read, no mutation — byte-identical to the pre-feature baseline.
    if (!store_is_persistent_) {
        co_return fixpp::core::expected_t<void>{};
    }

    // Read both counters from the store (C2.3).
    // IMPORTANT: mutate the manager only after BOTH reads succeed (no partial seed).
    auto in_r = co_await store_->next_seqnum(direction_t::inbound, false);
    if (!in_r) {
        disconnect_unless_superseded_(arm);
        co_return std::unexpected(in_r.error());
    }
    auto out_r = co_await store_->next_seqnum(direction_t::outbound, false);
    if (!out_r) {
        disconnect_unless_superseded_(arm);
        co_return std::unexpected(out_r.error());
    }

    // Apply seeds: outbound unconditionally; inbound conditionally (RC-1/C2.4).
    // When apply_inbound_seed==false (reset-Logon path), keep next_inbound at
    // the construction value seqnum_min so check_inbound(1) is in-sequence and
    // the existing reset arm (received-141 / reset_on_logon) owns the post-state.
    // [029 tasks T011; contracts C2.4; data-model INV-H5/RC-1]
    const seqnum_t seed_inbound = apply_inbound_seed ? *in_r : seqnum_min;
    auto hydrate_r = co_await seqnum_mgr_.hydrate(seed_inbound, /*next_outbound=*/*out_r);
    if (!hydrate_r) {
        disconnect_unless_superseded_(arm);
        co_return std::unexpected(hydrate_r.error());
    }

    // Latch success: set hydrated_ ONLY after both reads and hydrate() succeeded (D-9).
    hydrated_ = true;
    co_return fixpp::core::expected_t<void>{};
}

// 029 T010 — durable inbound advance (C3.0).
// Called at every PERSIST site in the disposition matrix (data-model §Persist matrix).
// Skips when store_is_persistent_==false (INV-H4 / C3.5).
// Failure → Disconnected (D-3 / C3.3 / SC-006).
// A failed store call returns store_io_failure, whatever the store returned, except
// store_cancelled, which is returned unchanged: both timings of a cancelled persist then
// return one code (`.specify/536-537-538-recovery-liveness.md` §9, D-CAN-4).
// [029 tasks T010; contracts C3.0/C3.3/C3.5; data-model INV-H1/H2]
asio::awaitable<fixpp::core::expected_t<void>> Session::persist_inbound_advance_(
    std::optional<fsm_state> arm) noexcept {
    if (!store_is_persistent_) {
        co_return fixpp::core::expected_t<void>{};
    }
    // 104 (contract RR-4, "By construction"): once the receipt marker is `reached`, no new
    // inbound persist is issued; the caller continues as after a write. A persist issued
    // earlier runs to its end (RR-2 item 5).
    if (logout_seen_ == receipt_marker::reached) {
        co_return fixpp::core::expected_t<void>{};
    }
    auto r = co_await store_->next_seqnum(direction_t::inbound, /*increment=*/true);
    if (!r) {
        disconnect_unless_superseded_(arm);
        if (r.error() == fixpp::core::error::store_cancelled) {
            co_return std::unexpected(r.error());
        }
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }
    co_return fixpp::core::expected_t<void>{};
}

// fixpp#423 — a message answered by a session Reject is received, so an in-sequence one
// consumes its MsgSeqNum (FIX-SL 2020 §4.5.4: "Rejected messages must be logged and
// NextNumIn incremented by 1"). Called at each Reject that runs BEFORE Guard (4)'s
// check_inbound; the Rejects after it are already counted (thorny C-102, qfj-557).
// check_inbound refuses any other number, so an out-of-sequence message stays
// unconsumed. Logon and SequenceReset are excluded, as both QuickFIX engines'
// generateReject exclude them. Erratum fixpp#423 (owner ruling 2026-09-14) supersedes
// 041 contract C-3's and 021 FR-004's "does not advance".
// 092 FR-019: at NextNumIn = seqnum_max the in-sequence message cannot be consumed, so
// this takes the silent transition to Disconnected and returns the error. A caller must
// return a failed result before its Reject (FR-013's ordering); re-derive the callers with
// `grep -n "consume_rejected_seqnum_(" src/session/session.cpp` and read each failed branch.
asio::awaitable<fixpp::core::expected_t<void>> Session::consume_rejected_seqnum_(
    seqnum_t seq, std::string_view msg_type) noexcept {
    if (msg_type == "A" || msg_type == "4") {
        co_return fixpp::core::expected_t<void>{};
    }
    if (auto chk = co_await seqnum_mgr_.check_inbound(seq); !chk) {
        if (chk.error() == fixpp::core::error::store_seqnum_overflow) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(chk.error());
        }
        co_return fixpp::core::expected_t<void>{};
    }
    note_inbound_advance_();
    co_return co_await persist_inbound_advance_();
}

// Exit AwaitingResend once the inbound counter has passed the requested gap's end.
// reconnect_fsm_ owns AwaitingResend state per data-model §E-1 / T023 Fix1.
// fixpp#537 (D-537-5): both callers have just advanced NextNumIn, so while the resend is
// still outstanding the instant its deadline counts from is refreshed. The refresh stays
// behind the "a resend is outstanding" test: a session with no gap pays one flag read
// and nothing more ([const §VIII.5]).
void Session::close_filled_resend_gap_() noexcept {
    if (!reconnect_fsm_.is_awaiting_resend()) {
        return;
    }
    if (reconnect_fsm_.current_resend_state().outstanding_end > 0 &&
        seqnum_mgr_.next_inbound_unsafe() > reconnect_fsm_.current_resend_state().outstanding_end) {
        reconnect_fsm_.exit_awaiting_resend();
        return;
    }
    if (effective_clock_) {
        reconnect_fsm_.mark_resend_progress(effective_clock_->steady_now());
    }
}

// 103 (research D4.3): the frame path wakes the bound when N passes a pending Logout's S; it
// never acknowledges inline. One byte compare when no mark is set ([const §VIII.5]).
void Session::note_inbound_advance_() noexcept {
    close_filled_resend_gap_();
    if (peer_logout_.phase == peer_logout_mark::phase_t::pending &&
        seqnum_mgr_.next_inbound_unsafe() > peer_logout_.seq) {
        wake_peer_logout_bound_();
    }
}

// fixpp#518 and 093 (data-model E-13; contract C-6): see session.hpp.
bool Session::logon_arm_superseded_(fsm_state expected) const noexcept {
    return state_ == lifecycle::closing || engine_stop_requested_ || fsm_state_ != expected;
}

// 093 plan OD-26: see session.hpp.
void Session::disconnect_unless_superseded_(std::optional<fsm_state> arm) noexcept {
    if (arm && logon_arm_superseded_(*arm)) {
        return;
    }
    // The merge of B41 into B28 (.specify/536-537-538-recovery-liveness.md §8, B41): the
    // write is made only when the state is not Disconnected already, as run_logout_phase1's
    // exits and Session::send's D-OUT-9 statements make theirs, because
    // record_state_transition_ adds an entry to the state history on every call. A cut
    // Logout's store call that is cancelled reaches here with the state already written.
    if (fsm_state_ != fsm_state::Disconnected) {
        record_state_transition_(fsm_state::Disconnected);
    }
}

// 093-inbound-frame-dispositions (data-model E-10; contract C-6, its steps 3 and 4) — the
// 141=Y reset unit's body. Each manager setter takes the seqnum mutex, which grants
// inline on its uncontended fast path (L-518-1's condition), so nothing here suspends
// before the store's reset_to. The first manager error stops the set and is returned;
// the store is then not touched. Otherwise the store's one reset_to runs with
// reset_unit_in_flight_ set until it returns or throws, which close() polls (104 deletes the
// unit's completion signal).
// Plan OD-25 (supersedes OD-14's in-place shield, which an async_mutex grant replaces):
// only the reset_to is co_spawned, on this strand, completing through a token bound to
// an empty cancellation slot. Plan OD-26 names the two parts: running reset_to on its
// own awaitable thread is what keeps Engine::stop()'s total emission out, because a
// child thread's slot forwards only through the arm's filter, which is terminal-only
// once the manager set has taken its first lock; the empty slot extends that to every
// emission kind and any parent filter. The manager set stays inline, so co_spawn's
// launch path cannot add a suspension before the store.
asio::awaitable<Session::reset_unit_result> Session::run_reset_unit_(seqnum_t next_in,
                                                                     seqnum_t next_out) noexcept {
    reset_unit_result r{};
    assert(!seqnum_mgr_.frozen(SeqnumManager::freeze_key{}) &&
           "104 RR-5 item 4: run_reset_unit_'s entry assert: the manager is frozen");
    r.manager = co_await seqnum_mgr_.reset_to_one();
    if (r.manager) r.manager = co_await seqnum_mgr_.set_next_inbound(next_in);
    if (r.manager) r.manager = co_await seqnum_mgr_.set_next_outbound(next_out);
    assert(!seqnum_mgr_.frozen(SeqnumManager::freeze_key{}) &&
           "104 RR-5 item 4: run_reset_unit_'s post-set assert: the manager is frozen");
    // 104 (contract RR-5 items 1 and 4): a receipt's unit (the marker `reached`) freezes the
    // manager after its manager set, whatever that set returned, and before its first
    // suspension (reset_unit_in_flight_ and the store's reset_to) or its return with no store;
    // the manager calls grant inline, so the marker's move, the set and the freeze are one
    // strand turn. A Logon arm's 141=Y unit runs with the marker not `reached` and freezes
    // nothing.
    if (logout_seen_ == receipt_marker::reached) {
        seqnum_mgr_.freeze(SeqnumManager::freeze_key{});
    }
    assert(seqnum_mgr_.frozen(SeqnumManager::freeze_key{}) ==
               (logout_seen_ == receipt_marker::reached) &&
           "104 RR-5 item 4: after run_reset_unit_'s freeze, frozen differs from the marker "
           "reached");
    if (!r.manager || store_ == nullptr) {
        co_return r;
    }
    reset_unit_in_flight_ = true;
    // 104 (contract RR-2 item 4): the flag is cleared on every resume, a return or an exception
    // from the store's reset_to or from co_spawn's launch. It is cleared here and not by a scope
    // guard, because a frame destroyed unresumed may outlive the Session once close()'s bounded
    // wait has expired, and a guard's destructor would then write the destroyed Session.
    try {
        r.store = co_await asio::co_spawn(
            co_await asio::this_coro::executor, store_->reset_to(next_in, next_out),
            asio::bind_cancellation_slot(asio::cancellation_slot{}, asio::use_awaitable));
    } catch (...) {
        reset_unit_in_flight_ = false;
        throw;
    }
    reset_unit_in_flight_ = false;
    co_return r;
}

// 104-sequence-reset-fidelity (contract RR-2 items 2, 3 and 5; RR-3 step 3): the receipt
// marker's move at a receipt arm's reset point. Each caller resets its thread's cancellation
// state, calls this, and starts run_reset_unit_ when it returns true, with no suspension and
// no callback between, so the reset point is one strand turn. The too-high Logout's bound is
// told apart by the 103 mark `acking` (INV-X): it finds `none` and makes its one write. A pump
// frame finds its own `pending`, or `reached` when close() closed the receipt out, and then
// runs no unit. Any other value fails the Debug assert.
bool Session::enter_receipt_reset_point_() noexcept {
    if (peer_logout_.phase == peer_logout_mark::phase_t::acking) {
        assert(logout_seen_ == receipt_marker::none &&
               "104 RR-2 item 5: the too-high Logout's bound found the receipt marker not none at "
               "its reset point");
        mark_reset_point_reached_();
        return true;
    }
    assert(logout_seen_ != receipt_marker::none &&
           "104 RR-2 item 5: a receipt arm's pump frame found the receipt marker none at its "
           "reset point");
    if (logout_seen_ == receipt_marker::reached) {
        return false;
    }
    mark_reset_point_reached_();
    return true;
}

// 015 T016(d) — initiator Logon emission, extracted from open()'s initiator arm.
// Two call sites: open() (per-session-direct, AT open) and drive_reconnect()
// (engine lazy-connect, POST-connect). The build/seqnum/store-emit sequence and
// its Disconnected-on-failure disposition are UNCHANGED from the original open()
// body — this is a behavior-preserving extraction. The caller owns the LogonSent
// transition (open() before the call; install_reconnected_transport before
// drive_reconnect's call). [data-model §E-1a; T016(d); FR-003/FR-004]
//
// Ruling R-20 (.specify/536-537-538-recovery-liveness.md §10, D-OUT-5), superseding "its
// Disconnected-on-failure disposition" above for an exception: the whole body sits in one
// try. Its handlers only note whether the exception was a cancellation. The statements
// after them give the call one ending, at whichever await the exception was met: the
// thread's cancellation state is reset, Disconnected is recorded when the state is not
// that already, and a value is returned.
asio::awaitable<fixpp::core::expected_t<void>> Session::emit_initiator_logon_() noexcept {
    bool caught_cancellation = false;
    try {
        // Emit the initial Logon via build_logon + store_then_emit.
        // This is the SAME admin-builder emission path used by other admin
        // frames (build_heartbeat, build_test_request, etc.) — NOT the
        // Session::send() path which is for opaque application payloads.
        // [spec.md FR-004 analyze findings B1 + E1; data-model.md §E1]
        // stamp_sending_time internal helper is defined after open() in this TU;
        // use the public two-arg form from sending_time.hpp with a local buffer.

        // 029 T007/T011 — call site 1 (initiator): hydrate BEFORE the reset_on_logon block
        // so the store-recovered outbound value is in the manager when peek_outbound()
        // is sampled below; the reset_on_logon block then overwrites it (024 reset wins,
        // INV-H5 / C2.6). One-shot latch makes this safe for both the direct open() and
        // engine-managed drive_reconnect() call paths.
        // [[feedback_initiator_logon_wire_at_shared_emit_point]] T011 (RC-1 / C2.4): WITHHOLD the
        // inbound seed when reset_on_logon is set — the reset block below owns next_inbound
        // post-state on that path (INV-H5). [029 tasks T007/T011 call-site 1; contracts C2.4/C2.5;
        // data-model INV-H3/H5; research D-6]
        // 103 (research D2.2; FR-003): bilateral_strict resets durably before every Logon,
        // on reset_on_logon's path: the inbound seed is withheld and the fatal reset below
        // runs for either.
        const bool reset_before_logon =
            cfg_.reset_on_logon ||
            cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict;
        {
            const bool withhold_inbound = reset_before_logon;
            // 025 T006 — refresh_on_logon: re-read store on each logon (store-wins, up or down).
            // Suppressed under bilateral_strict (INV-RoL-3 / FR-008 / D-RoL-3): under strict
            // the Logon always resets, so a refresh has nothing to set (103 D2.2). Default
            // false → zero delta (byte-identical pre-025).
            const bool refresh_active =
                cfg_.refresh_on_logon && cfg_.reset_seqnum_policy_field !=
                                             fixpp::session::reset_seqnum_policy::bilateral_strict;
            auto h_r = co_await ensure_hydrated_(/*apply_inbound_seed=*/!withhold_inbound,
                                                 /*force=*/refresh_active);
            if (!h_r) {
                // ensure_hydrated_ already transitioned to Disconnected on failure (C2.3).
                co_return std::unexpected(h_r.error());
            }
        }

        // 024 T007: reset_on_logon — durable reset here (the shared emission point)
        // so BOTH open() (per-session-direct, engine_managed=false) AND drive_reconnect()
        // (engine lazy-connect and reconnect) reset symmetrically before building the Logon.
        // 103 D2.2: bilateral_strict takes the same reset (reset_before_logon above).
        // fatal disposition: a store failure propagates → record Disconnected + return error.
        // peek_outbound() below samples the post-reset seqnum=1 (analyze B1).
        // [[feedback_half_restructure_symmetric_api]]: fix at the shared point, not two copies.
        // [024 data-model initiator row; C2.1; C2.6; plan.md Complexity Tracking row 1]
        if (reset_before_logon) {
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
            auto fmt_r = fixpp::session::stamp_sending_time(
                effective_clock_->now(), cfg_.sending_time_precision,
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
        // RC#A: use seqnum_mgr_.peek_outbound() (not bare field).
        const seqnum_t logon_seq = seqnum_mgr_.peek_outbound();  // peek via manager
        // RC#C: bilateral_strict → send 141=Y in our outbound Logon.
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
        // 103 (research D2.2; FR-002): (strict || any reset knob) && both numbers at 1, so a
        // Logon with 141=Y carries 34=1 under every policy by construction; under strict
        // the reset above has just put both numbers at 1. unilateral keeps lenient's
        // predicate (D2.5).
        const bool initr_reset_seqnum =
            ((cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict) ||
             any_reset_knob) &&
            seqnums_at_one;
        // 032 T007: unconditionally latch the emit-time 141=Y fact. Overwrites to false
        // when fixpp does NOT emit 141=Y — ensures a stale latch cannot survive across
        // reconnects (this is the shared open()/drive_reconnect() emit point).
        // [032 contract C4, data-model, RC1 unconditional-assign invariant]
        own_logon_sent_reset_flag_ = initr_reset_seqnum;
        // 027 T013 I-NEX-1: advertise next_inbound_unsafe() when knob is on (plain, NO +1).
        // Absent (nullopt) when knob is off ⇒ byte-identical baseline. [contract C2, I-NEX-7]
        const std::optional<fixpp::session::seqnum_t> initr_next_expected =
            cfg_.enable_next_expected_msg_seq_num
                ? std::optional<fixpp::session::seqnum_t>{seqnum_mgr_.next_inbound_unsafe()}
                : std::nullopt;
        // 033 T017/T023: thread the FIXT-only Logon fields (1137 + optional 553 and 554) into
        // the initiator emit — all-nullopt for FIX.4.x → byte-identical (INV-FIXT-1 / SC-002 / W4).
        const auto initr_fixt = derive_logon_fixt_fields(cfg_);
        // 070-fix44-closeout T004: empty opts ⇒ no 383/464/384 ⇒ byte-identical
        // baseline (FR-012). Populated from cfg_ per-story (US1/US2/US3).
        auto logon_result = fixpp::session::build_logon(
            std::span<std::byte>{logon_buf.data(), logon_buf.size()}, logon_seq,
            cfg_.sender_comp_id, cfg_.target_comp_id, cfg_.begin_string, heartbt_sec,
            sending_time_view, initr_reset_seqnum, initr_next_expected,
            initr_fixt.default_appl_ver_id, initr_fixt.username, initr_fixt.password,
            // 070-fix44-closeout S-029: advertise 464=Y when local posture==test; unset
            // ⇒ no 464 ⇒ byte-identical baseline (FR-012). 383 and 384 land per-story.
            fixpp::session::logon_advertise_options{
                .max_message_size = cfg_.advertised_max_message_size,
                .test_message_indicator = (cfg_.posture == session_posture::test),
                .supported_msg_types = cfg_.supported_msg_types});
        if (!logon_result) {
            // build_logon failed (oversized IDs → wire_frame_too_large).
            // Session-fatal — initiator handshake never reached the wire; transition
            // to Disconnected to match the acceptor send-throw symmetry promised by
            // FR-009 + the "session-fatal → Disconnected" precedent set by the
            // liveness loop (assign_outbound failure inside run_liveness_loop) and the
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
            record_disconnected_unless_already_();
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
            // 106 D11: a cut write or another ending's close returns here (FR-017).
            record_disconnected_unless_already_();
            co_return std::unexpected(emit_r.error());
        }
        co_return fixpp::core::expected_t<void>{};
    } catch (const asio::system_error& e) {
        caught_cancellation = e.code() == asio::error::operation_aborted;
    } catch (...) {  // NOLINT(bugprone-empty-catch): noted as "not a cancellation" above
    }
    // D-OUT-5: as after on_inbound_active_'s handlers (D-CAN-3). The thread is left as the
    // returned timing leaves it, so open() and drive_reconnect() return to a caller whose
    // next co_await does not throw. This await is never pre-checked, so it cannot throw.
    co_await asio::this_coro::reset_cancellation_state();
    if (fsm_state_ != fsm_state::Disconnected) {
        record_state_transition_(fsm_state::Disconnected);
    }
    if (caught_cancellation) {
        // The code that the returned timing of a cancelled hydration read, or of a
        // cancelled store reset, passes to the caller; one handler cannot tell which
        // await threw.
        co_return std::unexpected(fixpp::core::error::store_cancelled);
    }
    co_return std::unexpected(fixpp::core::error::dispatch_aborted);
}

// ── Phase-2 linkable placeholders — REPLACED per user story ─────────────
// Marked so a later phase's task body substitutes (not appends to) these.

namespace {

// fixt_session_config_valid refuses a null registry ahead of this. Should that check ever lose its
// clause, the session then routes on S alone rather than dereferencing null.
const fixpp::dict::version_registry& registry_or_empty(
    const fixpp::dict::version_registry* registry) noexcept {
    static const fixpp::dict::version_registry empty;
    return registry != nullptr ? *registry : empty;
}

}  // namespace

// 099-fixt-appl-version-validation (data-model E-6, FR-001, FR-002, FR-003): the slot rule and the
// transport route. Every entry but S's carries S's table as its envelope.
void Session::bind_fixt_routes_() noexcept {
    using fixpp::dict::application_version;
    fixpp::dict::version_registry const& registry = registry_or_empty(app_version_registry_);
    fixpp::dict::table_view const* const s_table = session_route_.owner->get();
    // S when the set holds a {d, S} entry, else null, read from the set, so this cannot fail: the
    // second key of every lookup here. S's own entry is find(S, nullptr), as open() reads it.
    fixpp::dict::Dictionary const* const pair_source =
        routes_->has_pair_source(cfg_.dictionary.get()) ? cfg_.dictionary.get() : nullptr;
    auto const routed = [this, s_table, pair_source](fixpp::dict::Dictionary const* d) noexcept {
        detail::fixt_route_set::entry const* const e = routes_->find(d, pair_source);
        return e == nullptr
                   ? route_entry{}
                   : route_entry{
                         .owner = &e->owner, .validator = e->validator.get(), .envelope = s_table};
    };

    auto const s_session = cfg_.dictionary->which_session_version();
    auto const s_application = fixpp::dict::session_to_application(s_session);
    for (auto slot = static_cast<std::size_t>(application_version::Unknown) + 1;
         slot < app_routes_.size(); ++slot) {
        auto const version = static_cast<application_version>(slot);
        auto const d = registry.get(version);
        route_entry route{};
        if (version == s_application ||
            (d.has_value() && (*d)->which_session_version() == s_session)) {
            route = session_route_;
        } else if (d.has_value()) {
            route = routed(*d);
        }
        app_routes_[slot] = route;
    }

    transport_route_ = session_route_;
    if (!defines_fixt_admin(*cfg_.dictionary)) {
        auto const transport = registry.transport();
        if (transport.has_value() && defines_fixt_admin(**transport)) {
            route_entry const route = routed(*transport);
            if (route.owner != nullptr) {
                transport_route_ = route;
            }
        }
    }
}

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
    // / FR-018 / I-13). The dictionary has no engine anchor and is required on the
    // session (099-fixt-appl-version-validation, FR-019, fixpp#651): no fallback, so a
    // null one is refused here. The resolved = override.value_or(engine_anchor) pattern
    // holds only for the executor and clock, above and below.
    if (!cfg_.dictionary) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // 066-dict-backed-inbound-parse T002 (data-model.md "Session inbound
    // table_view"): resolve the once-built inbound dict-membership table
    // HERE, immediately after the guard above — cfg_.dictionary is
    // guaranteed non-null at this point. Mirrors the strict validator's own
    // owned table_view build below (validator_'s construction, further down). Lands the
    // invariant that inbound_tv_ is non-null whenever a successfully-opened
    // session later reaches parse_and_dispatch_ (both callers run post-open
    // only).
    //
    // fixpp#215 item 1 (Option C, `.specify/215-dictionary-view.md` §3): PREFER
    // a snapshot the config already carries. `as_table_view()` is a full walk
    // of every message/group/field with no cache of its own, so the C-ABI —
    // which must build one anyway for its outbound commit path — hands that
    // same snapshot over in cfg_.dict_snapshot rather than making this line
    // walk the identical Dictionary a second time. Null (every non-C-ABI
    // producer) → build one here, exactly as before.
    //
    // Provenance (C4) is REJECTED FAIL-CLOSED here, before any observable
    // mutation (state_ = lifecycle::open happens later, further down in open()): a supplied
    // snapshot whose source() is not cfg_.dictionary would silently drive
    // inbound parsing/validation from the wrong grammar (§2b of the design
    // doc). shared_dictionary_view() is how both this line and
    // fixpp_session_open take the snapshot's table; it shares the table's own
    // owner, so inbound_tv_ pins the table and not the snapshot or its
    // Dictionary (fixpp#495 D-4, `.specify/495-493-486-dict-reify-copy.md` §6).
    // Owner-object site (session.hpp's inbound_tv_ comment). No assert that it
    // is unset: a failed open() leaves state_ at never_opened and a legitimate
    // retry reassigns it while no view exists.
    //
    // 099-fixt-appl-version-validation (data-model E-6): a FIXT session walks nothing here. It
    // takes S's owner from the route set, below, after every refusal.
    fixt_routing_ = cfg_.begin_string == "FIXT.1.1";
    if (cfg_.dict_snapshot) {
        if (cfg_.dict_snapshot->source() != cfg_.dictionary) {
            co_return std::unexpected(error::invalid_session_config);
        }
        inbound_tv_ = fixpp::dict::shared_dictionary_view(cfg_.dict_snapshot);
    } else if (!fixt_routing_) {
        inbound_tv_ =
            std::make_shared<const fixpp::dict::table_view>(cfg_.dictionary->as_table_view());
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

    std::shared_ptr<fixpp::transport::TransportFactory> resolved_transport_factory;

    // gate-b/r2 FQ-4 + gate-b/r3 FQ-6 (043 T023 / FR-003a / FR-008): resolve the
    // effective factory ONCE before any observable mutation, then run the single
    // kind() consistency check on that resolved object.
    {
        const auto k_early = cfg_.security_profile.k;
        if (is_insecure_plain_tcp(k_early) && !cfg_.transport_factory_override) {
            auto plain_factory_r = fixpp::transport::make_asio_plain_transport_factory(
                fixpp::transport::Transport::Config{});
            if (!plain_factory_r) {
                co_return std::unexpected(plain_factory_r.error());
            }
            resolved_transport_factory =
                std::shared_ptr<fixpp::transport::TransportFactory>(std::move(*plain_factory_r));
        } else {
            resolved_transport_factory = cfg_.transport_factory_override
                                             ? cfg_.transport_factory_override
                                             : engine_.default_transport_factory;
        }

        if (resolved_transport_factory) {
            using TFK = fixpp::transport::transport_security_kind;
            const TFK required_early = is_insecure_plain_tcp(k_early) ? TFK::plaintext : TFK::tls;
            if (resolved_transport_factory->kind() != required_early) {
                co_return std::unexpected(error::invalid_session_config);
            }
        }
    }

    // 099-fixt-appl-version-validation (data-model E-9, FR-003, FR-004): the FIXT check, one
    // shared with Engine::register_session. Tested against begin_string directly, NOT is_fixt(),
    // which encodes the default_appl_ver_id half and would MISS the begin_string-without-default
    // case. On an Engine route this runs in the role loop, whose failure branch logs and returns
    // nothing to the host. So only a validating session gets a config-load failure, from
    // Engine::register_session; a non-validating one is refused here alone (L-099-4).
    if (fixt_routing_ && !fixt_session_config_valid(cfg_, app_version_registry_)) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // gate-b/r1 FQ-3 (finding #3) + 090-capi-refusals (fixpp#452, FR-012/FR-013,
    // EC-7): configured-string delimiter injection validation.
    //
    // sender_comp_id/target_comp_id/begin_string, each configured
    // supported_msg_types[].msg_type (RefMsgType(372)), and username/password
    // are all copied verbatim into append_raw() in build_logon
    // (admin_messages.cpp's `build_logon`, tags 49/56/8/372/553/554) with no
    // SOH/= validation. A configured value containing SOH (\x01) or '=' can
    // inject arbitrary FIX fields. This is the known
    // feedback_delimiter_injection_verbatim_field_copy anti-pattern.
    //
    // Floor: reject any byte < 0x20 (incl. SOH \x01) or '=' (0x3D) — the ONE
    // definition fixpp::session::contains_forbidden_config_byte
    // (config_byte_floor.hpp, D-5b; contracts/session-config-byte-floor.md
    // §2/§8; FR-013). Fail-closed at open()-time before any emission. FIX.4.x
    // paths are NOT bypassed: sender_comp_id/target_comp_id/begin_string are
    // version-agnostic and emitted by every admin builder; build_logon
    // conditionally emits 372/553/554 for any config that sets them.
    // Clean/absent configs never trip this guard → W4/W6 byte-identical
    // preserved.
    // [feedback_delimiter_injection_verbatim_field_copy; FR-012/FR-013;
    // data-model.md E3/EC-7]
    if (contains_forbidden_config_byte(cfg_.sender_comp_id) ||
        contains_forbidden_config_byte(cfg_.target_comp_id) ||
        contains_forbidden_config_byte(cfg_.begin_string)) {
        co_return std::unexpected(error::invalid_session_config);
    }
    for (const auto& entry : cfg_.supported_msg_types) {
        if (contains_forbidden_config_byte(entry.msg_type)) {
            co_return std::unexpected(error::invalid_session_config);
        }
    }
    if (cfg_.username.has_value() && contains_forbidden_config_byte(*cfg_.username)) {
        co_return std::unexpected(error::invalid_session_config);
    }
    if (cfg_.password.has_value() && contains_forbidden_config_byte(*cfg_.password)) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // 034 T007 (C3): credential-length guard — role-independent, at the shared
    // open() validation point (before any observable mutation). The masked-Logon
    // persist path (store_then_emit, T006) copies the frame into a
    // kMaxMaskableLogonBytes (== build_logon's logon_buf/reply_buf capacity)
    // coroutine-frame array. build_logon ALREADY fails-closed
    // (wire_frame_too_large) when its output exceeds that buffer, so the T006
    // over-bound branch is already production-unreachable; this guard surfaces
    // the same ceiling at CONFIG time for the credential case and — by validating
    // cfg_.username/password regardless of role — keeps the bound-exactness
    // argument (and therefore the dead over-bound branch) symmetric across BOTH
    // initiator open() AND acceptor reply-Logon, not just the initiator
    // ([[feedback_symmetric_api_claim_unreachable_arm]]; C3 / FR-004). The check
    // is a sufficient-condition floor: credentials whose combined length alone
    // cannot leave room for the fixed Logon overhead can never produce a
    // maskable (≤ kMaxMaskableLogonBytes) Logon. Clean/absent credentials never
    // trip it → W4 byte-identical preserved.
    {
        const std::size_t cred_len = (cfg_.username.has_value() ? cfg_.username->size() : 0U) +
                                     (cfg_.password.has_value() ? cfg_.password->size() : 0U);
        if (cred_len >= Session::kMaxMaskableLogonBytes) {
            co_return std::unexpected(error::invalid_session_config);
        }
    }

    // 093-inbound-frame-dispositions (data-model E-7): a zero establishment timeout is
    // refused, as Engine::register_session refuses it.
    if (cfg_.logon_timeout_ms == 0) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // 105 (FR-011, contract C-6.6): a set test_request_threshold of zero or below is refused,
    // as Engine::register_session refuses it. There is no upper bound.
    if (!test_request_threshold_in_range(cfg_)) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // 093-inbound-frame-dispositions (FR-010, plan OD-2): an advertised MaxMessageSize(383)
    // outside its range is refused, as Engine::register_session refuses it.
    if (!advertised_max_message_size_in_range(cfg_)) {
        co_return std::unexpected(error::invalid_session_config);
    }

    // 093-inbound-frame-dispositions (data-model E-2): the session's inbound limit L.
    // Computed from the config alone, before the first observable mutation below.
    inbound_limit_ = inbound_limit_for(cfg_);

    // 093 (data-model E-2, plan OD-13): the carry block, from framer_carry_arena (else
    // new_delete), and the parse buffer B(L), from the session arena, each allocated
    // once here, before the first observable mutation below. A bad_alloc is an open()
    // error. Each block frees itself unless the commit before `state_ = open` takes it.
    std::pmr::memory_resource* const carry_arena =
        cfg_.framer_carry_arena ? cfg_.framer_carry_arena : std::pmr::new_delete_resource();
    owned_block carry_block{carry_arena, detail::inbound_carry_block_bytes(inbound_limit_)};
    owned_block parse_block{session_arena_, detail::parse_capacity::buffer_bytes(inbound_limit_)};
    if (!carry_block || !parse_block) {
        co_return std::unexpected(error::out_of_memory);
    }

    // 099-fixt-appl-version-validation (data-model E-6, FR-028): S's entry of the route set. Here
    // so that a config any guard above refuses has walked nothing, and on the same side of the
    // commit as the two blocks above. A session the Engine handed no set builds its own, over
    // every registry slot and its own dictionary.
    if (fixt_routing_) {
        if (!routes_) {
            auto built = detail::fixt_route_set::build(registry_or_empty(app_version_registry_),
                                                       std::views::single(cfg_.dictionary.get()));
            if (!built) {
                co_return std::unexpected(built.error());
            }
            routes_ = std::move(*built);
        }
        // data-model E-10: the one block a Logon writes into. Ahead of the first route write,
        // which nothing fallible may follow.
        try {
            msg_type_defaults_.reserve(kMaxMsgTypeDefaults);
        } catch (std::bad_alloc const&) {
            co_return std::unexpected(error::out_of_memory);
        }
        const detail::fixt_route_set::entry* const own =
            routes_->find(cfg_.dictionary.get(), nullptr);
        if (own == nullptr) {
            co_return std::unexpected(error::invalid_session_config);  // an adopted set lacks S
        }
        session_route_ = route_entry{.owner = &own->owner, .validator = own->validator.get()};
        if (!cfg_.dict_snapshot) {
            inbound_tv_ = own->owner;
        }
        bind_fixt_routes_();
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
    // (the make_session_executor(resolved, mode, ...) call below) still
    // unconditionally wraps in a strand.
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
    // 103 (research D5′): the replay gate's strand check runs against this executor.
    replay_gate_.bind(exec_.underlying(), &detail::session_strand_check);
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

    // 093 (data-model E-12): the logger, resolved once here; may be null.
    logger_ = cfg_.logger_override ? cfg_.logger_override : engine_.logger;

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
        // 029 T005 — capture persistence discriminator (C2.2 / D-10 / New-A).
        // Read ONCE here (before any counter touch) from the factory that just
        // minted the store.  false for MemoryStoreFactory (volatile); true for
        // FileStoreFactory + any custom factory that does not override the default.
        // The null-store path (no cfg_.store_factory) leaves store_is_persistent_
        // at its default false (no read, no allocation — INV-H4).
        // Both the direct-open and engine-managed paths call Session::open() and
        // flow through this single if(cfg_.store_factory) branch — ONE capture
        // point covers both roles (confirmed by inspection: store_ is minted
        // exclusively here; reconnect_fsm.cpp factory_->make() mints the transport,
        // not the session store).
        store_is_persistent_ = cfg_.store_factory->yields_persistent_store();
    }

    // ── 041-validation-gate-wiring T014 — build validator once at open() ──────
    // Construct the dictionary_driven_validator from the session dictionary's
    // table_view (built once here, zero per-message heap — [const §VIII.5]/§XV.1).
    // Guard: cfg_.validate_inbound_messages && cfg_.dictionary non-null.
    // cfg_.dictionary is guaranteed non-null by this point: the null-dict check
    // the null-dict guard above (T050 US5) returns invalid_session_config before reaching here.
    // A directly-constructed Session (bypassing register_session's T004 gate)
    // that sets validate_inbound_messages=true with a null dictionary is
    // therefore caught by open()'s own null-dict guard too. The additional
    // null-check here is a defensive belt-and-suspenders for future callers.
    // [041 T014; data-model E-2; research R-2/R-6; SC-005; FR-002]
    //
    // fixpp#215 item 1: COPY the view resolved above rather than walking the
    // Dictionary a third time. `dictionary_driven_validator` holds its
    // `table_view` BY VALUE and that is a frozen design point (validator.hpp
    // "SC-007: no virtual edge"), so it cannot share `inbound_tv_`'s object —
    // but a copy duplicates already-built tables instead of re-deriving them
    // from the Dictionary (no re-traversal, no per-context key reconstruction,
    // no membership dedup rescans). One walk + one copy, where it used to be
    // two walks — and, on the C-ABI path, three.
    //
    // 099-fixt-appl-version-validation (data-model E-6): on a FIXT session validator_ stays null,
    // and S's validator is the route set's, set above.
    if (cfg_.validate_inbound_messages && !fixt_routing_) {
        // Defensive: dictionary should be non-null here (open() rejects null-dict
        // above), but guard explicitly to avoid a null deref if the invariant drifts.
        // inbound_tv_ is non-null whenever cfg_.dictionary is — both are set by the
        // same guarded block above.
        if (cfg_.dictionary && inbound_tv_) {
            validator_ = std::make_unique<fixpp::wire::dictionary_driven_validator>(*inbound_tv_);
        }
        // If dictionary is null despite the guard, validator_ stays null → the
        // validate gate in on_inbound_frame skips (fail-closed skip, not crash).
        // This matches R-6: "validation enabled + no dict = config error".
        // In practice this branch is unreachable in production (open() would
        // have returned invalid_session_config above).
    }
    if (!fixt_routing_) {
        session_route_ = route_entry{.owner = &inbound_tv_, .validator = validator_.get()};
    }

    // 093 (data-model E-2): commit the blocks allocated above. Nothing below can fail:
    // the monotonic resource covers exactly the carry block, and the carry's noexcept
    // constructor reserves L plus one read, which the block serves (its container
    // proxy, where the STL draws one, is the block's kContainerSlack).
    carry_arena_ = carry_arena;
    carry_block_ = carry_block.release();
    carry_resource_.emplace(carry_block_.data(), carry_block_.size(), &parse_spill_witness_);
    carry_.emplace(std::size_t{inbound_limit_} + detail::kReadPumpReadSize, &*carry_resource_);
    inbound_parse_block_ = parse_block.release();
    inbound_parse_buf_ = inbound_parse_block_;
    inbound_entry_cap_ = detail::parse_capacity::entry_cap_for(inbound_limit_);

    state_ = lifecycle::open;
    // 105 (research D5; data-model E-4): the first connection's start. open() runs only from
    // never_opened, so the ending state is none and the cap empty here already.
    begin_connection_();

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
    // FSM is a value member of Session (reconnect_fsm_'s member declaration) and is
    // destroyed before Session is — so `this` is always valid when the callback fires.
    // §XI.4: emit_event() is always called from the session strand (the FSM
    //   coroutine runs on the session executor set in Session::open()).
    // Resolves the "DEFERRED to 014" comment at `reload_credentials`'s doc comment.
    // [data-model §E-3; contracts C3; FR-009; §XI.4; T017/T018]
    reconnect_fsm_.set_emit_credentials_rotated(
        [this](fixpp::session::session_event_credentials_rotated ev) noexcept { emit_event(ev); });
    // 043 T012+T023 (D-4/D-6 — FR-003a/FR-008) — Effective-factory wiring.
    //
    // Step 1: insecure_plain_tcp is ACCEPTED (not unset); the unset reject above
    //   is unchanged.
    // Step 2: adopt the already-resolved effective factory into the owning member.
    // Step 4: wire the resolved factory into the FSM via set_transport_factory()
    //   so the FSM mints through the same validated factory.
    // Step 5 (SK→TK mapping): plaintext leaves tls_profile=unset (no SslCtxConfig)
    //   and sets the FSM plaintext indicator; TLS profiles map as before.
    // [data-model §E-6; D-4; D-6; D-7; 043 T011/T012]
    {
        auto k = cfg_.security_profile.k;
        using SK = fixpp::session::SecurityProfile::kind;
        using TK = fixpp::tls::SecurityProfile;

        effective_transport_factory_ = std::move(resolved_transport_factory);

        // Step 4: wire the resolved factory into the FSM.
        reconnect_fsm_.set_transport_factory(effective_transport_factory_.get());

        // Step 5: SK→TK mapping.
        // Map session-layer SecurityProfile::kind to tls::SecurityProfile so
        // async_handshake's profile-check is satisfied (not transport_psk_unsupported).
        // The enum values are identical for the common cases (mtls_ca=1, mtls_pinned=2,
        // one_way_ca=3). [data-model §E-1 step 3]
        TK tls_profile = TK::unset;
        if (k == SK::mtls_ca) {
            tls_profile = TK::mtls_ca;
        } else if (k == SK::mtls_pinned) {
            tls_profile = TK::mtls_pinned;
        } else if (k == SK::one_way_ca) {
            // one_way_ca is deprecated in the TLS layer but still supported
            // for legacy interop (session layer retains it per [const §XII.5]).
            // Portable `#elif defined` spelling: see is_insecure_plain_tcp() in
            // fixpp/session/security_profile.hpp.
            // NOLINTBEGIN(readability-use-concise-preprocessor-directives)
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            // NOLINTEND(readability-use-concise-preprocessor-directives)
            tls_profile = TK::one_way_ca;
            // NOLINTBEGIN(readability-use-concise-preprocessor-directives)
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
            // NOLINTEND(readability-use-concise-preprocessor-directives)
        }
        // insecure_plain_tcp: tls_profile stays unset; no SslCtxConfig arm.
        reconnect_fsm_.set_tls_profile(tls_profile);

        // Set the plaintext indicator AFTER the profile mapping so the FSM
        // skips the dynamic_cast + async_handshake (D-7). [043 T011/T012; §E-5]
        reconnect_fsm_.set_plaintext_profile(is_insecure_plain_tcp(k));
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
            // 105 (research D1 item 7): open() where it emits a direct initiator's Logon is an
            // application call, so the latch is released at this point of its exit.
            release_connection_end_();
            if (!logon_r) {
                // emit_initiator_logon_ has already transitioned to Disconnected.
                co_return std::unexpected(logon_r.error());
            }
        }
    } else {
        // Acceptor: stay in NotConnected, emit nothing.
        // fsm_state_ remains fsm_state::NotConnected (its default constructed value).
        // Hydration is lazy (first-counter-touch, D-1/D-6): the acceptor hydrates in the
        // NotConnected inbound-Logon handler (call site 2), not at open() — no counter is
        // sampled at acceptor open().
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
            // T019: the scope ends BEFORE close(terminal), so the close below and
            // the onLogout it fires (inside close → record_state_transition_) do
            // not run with onCreate counted as on the stack.
            callback_dispatch_scope cs{*this};
            cb_r = invoke_callback_safe([&]() { engine_.application->onCreate(create_id); });
        }  // cs drops here
        if (!cb_r) {
            (void)co_await close(fixpp::session::close_mode::terminal);
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
    // shields close()'s OWN co_awaits only; the liveness loop it tears down is still
    // cancelled via root_cancel_.emit(total), fired in phase 2 (see `root_cancel_`; cell RS-1).
    // (Engine::stop() also drives a fresh close() for sessions whose role-loop close()
    // was cancelled BEFORE entry — the two fixes are complementary.) [Codex P1]
    co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});

    // fixpp#563 (`.specify/563-564-callback-send.md` §2.1a "`Session::close`, both modes
    // (R-4)"): a close started while a callback of this session is on the stack waits by
    // one post onto the session's executor and then proceeds as it does without the wait.
    // §2.1 "When the post does not move the send out of the callback (R-7)" names the
    // setup in which the post does not take it off the callback's stack. The wait sits
    // after the reset above, so a cancellation emitted during it is not recorded
    // ("Cancellation and the wait"), and before the three-state test below, so that test
    // reads the state the close finds when it resumes. It is one post and not a loop:
    // where the assertion is compiled out and the depth is still non-zero, a close that
    // waited again would post again each time it resumed.
    if (callback_depth_ != 0) {
        // fixpp#590 (`.specify/563-564-callback-send.md` §2.6 "The mechanism",
        // statement 1): a terminal close that takes this wait is counted, before the
        // post, so that the caller of the callback it was started in can tell, when
        // that callback returns, that the count moved. A graceful close is not counted.
        if (mode == close_mode::terminal) {
            ++waited_terminal_closes_;
        }
        co_await asio::post(exec_.underlying(), asio::deferred);
        FIXPP_ASSERT_NO_CALLBACK_AT_WAITED_CLOSE();
    }

    // ── T038: idempotent THREE-STATE model (I-10 / [2d §4.7]'s "Idempotency" note) ──
    // never-opened OR already-closed(drained) → session_already_closed
    // (slot 52); no side effects.
    if (state_ == lifecycle::never_opened || state_ == lifecycle::closed_drained) {
        co_return std::unexpected(error::session_already_closed);
    }
    // already-closing (in-flight) → the SAME in-flight result, NO error: await the
    // first call's shared slot, then mirror it.
    // OD-29 (093 — terminal close escalates a graceful one), narrowing [2d §4.7] and
    // [2d §6.5]'s "Idempotency" bullets: the join has no side effects unless a
    // terminal close arrives while a graceful one is in flight. Then it ends the
    // graceful close's grace wait, or keeps its phase 1 from starting, so the
    // transport closes now; both callers still get the in-flight result.
    // One exception (fixpp#590; `.specify/563-564-callback-send.md` §2.6 "The
    // mechanism", statement 4, "The join's waker"; rulings R-8 and R-9), which narrows
    // OD-29 on that route: for a Logout that was cut the terminal close sets the latch
    // and does not end the grace wait. The cut Logout's step ends by itself when its
    // store call returns, so nothing cancels that call before the grace timer expires
    // (§2.6 "The bound").
    if (state_ == lifecycle::closing) {
        if (mode == close_mode::terminal) {
            close_escalated_ = true;
            if (close_grace_wake_ && !logout_cut_) close_grace_wake_();
        }
        auto shared = close_result_;
        while (!shared || !shared->has_value()) {
            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - guarded by has_value() above
        co_return **shared;
    }

    // ── First close() on an OPEN session: run the two-phase body once ──────
    state_ = lifecycle::closing;
    // 105 (research D1 item 6): close() owns the connection's close from here; the latch is
    // cleared at the commit of closing, and owe_connection_end_ refuses while closing.
    connection_end_owed_.reset();
    // 103 (research D5′; data-model "Replay gate", wake sources): immediately after the first
    // commit of closing, with no suspension between; a joining close() does not wake again.
    replay_gate_.wake_all();
    // 103 (research D4.4, D4.6): close() claims a pending mark and wakes the bound,
    // unconditionally, at the same position.
    claim_peer_logout_none_();
    // 105 (data-model E-3, the bound's wake sources): the ending Logout's bound re-tests here.
    wake_ending_logout_bound_();
    // 106 (data-model E-3, the wakers): so does the connection bound, which leaves on closing.
    wake_connection_bound_();
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
        // [feedback_asio_cospawn_total_cancellation_default]: run_logout_phase1 makes
        // no reset_cancellation_state call; it runs under close()'s cancellation
        // state, which the root slot does not reach (see `root_cancel_`; cell RS-1).
        // OD-29: a terminal close that arrived during the flush hooks above has set
        // close_escalated_, and phase 1 does not start.
        if (!close_escalated_ &&
            (fsm_state_ == fsm_state::Active || fsm_state_ == fsm_state::LogonReceived)) {
            using namespace asio::experimental::awaitable_operators;

            auto ex = co_await asio::this_coro::executor;
            asio::steady_timer close_grace{ex};
            close_grace.expires_after(std::chrono::milliseconds{cfg_.logout_disconnect_timeout_ms});

            // OD-29: an escalating terminal close cancels close_grace. The wait completes
            // through as_tuple, so a cancel counts as the timer's completion and takes the
            // force-close arm below; through use_awaitable it would throw, which `||`
            // counts as a failure, and the join would keep waiting on phase 1.
            // For a Logout that was cut the wake is not called (the joining branch
            // above; fixpp#590, `.specify/563-564-callback-send.md` §2.6, rulings R-8
            // and R-9), so no terminal close ends the timer's wait there.
            close_grace_wake_ = [&close_grace] { close_grace.cancel(); };
            bool timer_arm_won = false;
            if (logout_in_flight_) {
                // 103 (research D4.6, the poll-join): a Logout of the session is in flight, so
                // phase 1 sends none. The poll by post, as the liveness join polls, races the
                // grace; its emitter records Disconnected before close() records it and closes
                // the transport. The grace running out takes the force-close below.
                // FR-010 case 5, L-103-6.  claim-ok: a spec case and a B&L row, not a tally
                auto in_flight_ended = [this]() -> asio::awaitable<void> {
                    auto const ex = co_await asio::this_coro::executor;
                    while (logout_in_flight_) {
                        co_await asio::post(ex, asio::use_awaitable);
                    }
                };
                auto joined_or_timeout =
                    co_await (in_flight_ended() ||
                              close_grace.async_wait(asio::as_tuple(asio::use_awaitable)));
                timer_arm_won = joined_or_timeout.index() != 0;
            } else {
                auto phase1_or_timeout =
                    co_await (run_logout_phase1() ||
                              close_grace.async_wait(asio::as_tuple(asio::use_awaitable)));
                timer_arm_won = phase1_or_timeout.index() != 0;
                if (!timer_arm_won) {
                    auto phase1_r = std::get<0>(std::move(phase1_or_timeout));
                    (void)phase1_r;  // timeout is logged-then-proceed (I-07; force-disconnect)
                }
            }
            close_grace_wake_ = nullptr;
            // The direct close of the transport is made when the timer's arm won, for
            // the reason at the call below, and also for a Logout that was cut,
            // whichever arm won (fixpp#590; `.specify/563-564-callback-send.md` §2.6
            // "The mechanism", statement 4, "The transport's direct close"). For a cut
            // Logout the step's arm wins unless the timer expires first, and the call
            // is kept so that a live transport is closed on this route as it is where
            // an escalation ends the grace wait: by this close() and by the
            // close_async() that follows. The cut step begins no write, so nothing is
            // unwound by the call there. What the pair is on TLS: this close() closes
            // the socket and skips the TLS shutdown while an SSL operation is
            // suspended, and the close_async() after it finds the transport closed and
            // sends nothing, so the peer gets an abortive close with no close-notify
            // (B&L B-348-1 and its second amendment). On the plain transport the two
            // calls do not differ.
            if (timer_arm_won || logout_cut_) {
                if (auto live = live_transport_shared_()) {
                    // FQ-G: if phase 1 wedges behind write_gate_ / async_write,
                    // force-close the transport so the blocked writer unwinds and
                    // phase 2 can drain the gate without deadlocking close(graceful).
                    (void)live->close();
                }
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
    // collapsed to "phase 1 done" in the scripted scope). It reaches what is
    // bound to root_cancellation_slot() (see `root_cancel_`; cell RS-1); transport I/O, mutex
    // acquires, cancellable_dispatch and the parser→fromApp chain are not.
    // terminal reaches phase 2 immediately (phase 1 skipped).
    //
    // fixpp#536 (.specify/536-537-538-recovery-liveness.md, D-536-1 and D-536-2),
    // superseding US4 (T047)'s Clock::cancel_sleeps() call here. close() does not
    // sweep the clock: the sessions of an engine share it, so the sweep also ended
    // the liveness loop of every other session. The emit below is what ends this
    // session's liveness loop. The Clock contract has sleep_until honour its
    // awaiter's cancellation slot (clock.hpp), and the loop accepts total
    // cancellation at each of its sleeps (run_liveness_loop). Disconnected is written
    // above with no suspension before the emit, so a loop that is not in a sleep now
    // finds the state changed when it next reaches one.
    root_cancel_.emit(asio::cancellation_type::total);
    // 105 (research D1 item 6; contract C-8.3): a close frame spawned before this point runs
    // close_async() on the same transport; close() waits for it, by post, as it joins the
    // liveness loop below, so the two never run close_async on one transport at once.
    FIXPP_JOIN_COUNTERS(connection_close_counter_);
    if (auto live = live_transport_shared_()) {
        // #348 — close_async(), not close(). The Engine's read pump can still
        // have a read in flight at this instant: the emit above does not reach it
        // (see `root_cancel_`; cell RS-1).
        // With a read in flight the synchronous close() takes its suspended-op
        // path and skips the close-notify, so the peer of a cleanly closing fixpp
        // session reads an OS-level error. close_async cancels and JOINS the
        // in-flight op first, then writes the alert, bounded by
        // Config::tls_close_timeout and falling back to the abortive close if it
        // cannot quiesce — so the worst case is the old behaviour.
        (void)co_await live->close_async();
    }
    // T045: clear the session_local<trace_context> slot at close completion
    // (FR-014). Reached in BOTH graceful and terminal once the two phases
    // resolve; the slot stays valid until here (seam 17: never read through
    // a destroyed slot — the slot lives in the Session, drained, not freed).
    trace_slot_.clear();

    // FQ-A (gate-b/r2): wait for the liveness loop to exit before the seqnum drain.
    // root_cancel_.emit() above has already fired total cancellation (which cancels
    // the liveness loop's sleep_until through its cancellation slot; fixpp#536), and
    // the live transport is synchronously closed above so any in-progress liveness
    // write completes with error before the drain waits on write_gate_. We yield the
    // executor until liveness_counter_ reaches 0, meaning the liveness coroutine has
    // fully exited run_liveness_loop. This ensures registry_.clear() cannot destroy
    // the Session while the liveness coroutine is still touching Session members.
    // [feedback_detached_cospawn_write_not_in_join_counter; FQ-A D-6 F4]
    // Each post yields one step; the liveness loop's try/catch converts cancellation to a clean
    // return, then the RAII guard decrements the counter.
    FIXPP_JOIN_COUNTERS(liveness_counter_, connection_bound_counter_);
    // 103 (research D4.4): the too-high Logout's bound, joined as the liveness loop is. Its
    // wake came from the claim at the first commit of closing.
    FIXPP_JOIN_COUNTERS(peer_logout_bound_counter_);

    // 104-sequence-reset-fidelity (contract RR-2 item 4, RT-5; data-model "`Session::close`'s
    // teardown"), superseding 093's event-driven wait (contract C-6 "close()"; plan OD-1),
    // which sat inside the predicate block below and had one waker, the unit's completion:
    // close() waits, after the joins above and before the teardown predicate, whatever the
    // knobs, while a received Logout is `pending` short of its reset point or a reset unit's
    // store operation is in flight (a receipt arm's or a Logon arm's). The too-high Logout's
    // bound was joined above, so its receipt and unit are done by now. The wait is a poll by
    // post, as the joins above are: each pass re-reads both terms and compares
    // effective_clock_'s steady time with the bound, logon_timeout_ms from the wait's start
    // (saturating), and the condition is tested before the first post, so a close() with
    // nothing outstanding makes no pass. It needs no waker, so no exit from the condition can
    // be missed. On expiry it records session_event_close_reset_wait_expired and proceeds.
    // With no clock there is no wait.
    if (effective_clock_) {
        // Saturates at steady_time_point::max() when not representable.
        auto const bound = detail::deadline_after(effective_clock_->steady_now(),
                                                  std::chrono::milliseconds{cfg_.logon_timeout_ms});
        while (logout_seen_ == receipt_marker::pending || reset_unit_in_flight_) {
            if (effective_clock_->steady_now() >= bound) {
                emit_event(session_event_close_reset_wait_expired{});
                break;
            }
            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        }
    }
    // 105 (research D2 item 9; data-model E-2 I-4): the ending Logout's bound, joined after the
    // transport's close, then the close frame's counter again, which collects a frame the bound
    // spawned after the first join. It sits after 104's wait, whose bound is read at that wait's
    // start: a bound whose Logout is held in a store call runs on to its own deadline, so a join
    // before 104's wait would start that wait's bound late.
    FIXPP_JOIN_COUNTERS(ending_logout_bound_counter_);
    FIXPP_JOIN_COUNTERS(connection_close_counter_);
    // 104 (contract RR-2 item 4): the close-out. A receipt still `pending` here (the wait
    // expired, or there was no clock to wait with) never reached its reset point, so it is
    // not reset under reset_on_logout (RR-1). It is set to `reached`, so that the arm, when it
    // resumes, runs no unit and issues no further store call (RR-2 item 5).
    if (logout_seen_ == receipt_marker::pending) {
        logout_seen_ = receipt_marker::reached;
        // 104 (contract RR-5 item 1): the freeze, in the turn of the marker's `reached`.
        seqnum_mgr_.freeze(SeqnumManager::freeze_key{});
    }
    assert(seqnum_mgr_.frozen(SeqnumManager::freeze_key{}) ==
               (logout_seen_ == receipt_marker::reached) &&
           "104 RR-5 item 4: after close()'s close-out, frozen differs from the marker reached");

    // 024 T014 — teardown reset (reset_on_disconnect).
    // Placement: BEFORE write_gate_.cancel_and_drain() (the first drain).
    // Binding constraint (analyze F1): reset_seqnums_to_one_durable() acquires the
    // seqnum async-mutex via reset_to_one(). If seqnum_mgr_.drain() has already run,
    // the mutex is drained and reset_to_one() would return session_already_closed,
    // silently no-oping without resetting counters. Must therefore run before BOTH
    // drains (write_gate_.cancel_and_drain() and seqnum_mgr_.drain()).
    //
    // Single-fire guard (C5.1): teardown_reset_done_ prevents a double trigger (a
    // re-entered close()) from calling store_->reset() twice. FileStore::reset() is
    // non-idempotent I/O (full atomic-rename + fdatasync + dir-fsync per call).
    //
    // Predicate: !teardown_reset_done_ && cfg_.reset_on_disconnect. 104-sequence-reset-fidelity
    // (contract RR-2 item 4, RT-1) supersedes 024 C3.1's reset_on_logout term by name: a
    // received Logout resets at its arm's reset point, and the teardown never resets a
    // `pending` receipt (it was closed out above, with no reset). The one case the teardown
    // would own under reset_on_logout, a receipt `reached` whose unit has not started, has no
    // input, because the reset point is one strand turn (contract RR-2 item 3), so no term
    // is coded for it. A change that splits that turn must add the term, and a way to observe
    // "unit not started", which the three-valued marker does not have (contract RR-2 item 4;
    // cell R-1u).
    //   - reset_on_disconnect: fires on ANY close() — graceful, terminal, or
    //     abnormal read-pump EOF → close(terminal). [C4.1, C4.2]
    //
    // Disposition: logged-then-proceed (I-07) — teardown store failure is not fatal;
    // the session should still complete close() normally. [contracts C4.1]
    //
    // [contracts/reset-knobs.md C4.1, C4.2, C5.1; plan.md Complexity row 3]
    if (!teardown_reset_done_ && cfg_.reset_on_disconnect) {
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
    // [FQ-A D-6 F1/F3; transport.hpp's [2h §4.1] RC#3 in-flight contract]
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

// FrameHeader + scan_frame_header are now defined in scan_frame_header.hpp
// (040 US1 Phase 3: moved from anonymous namespace to enable direct unit testing).
// Bring them into scope for all callers in this TU via a using declaration.
using fixpp::session::detail::FrameHeader;
using fixpp::session::detail::scan_frame_header;

// fixpp#426: the Length+Data pairs a Session's own field scanners split by —
// its dictionary once open() has built `inbound_tv_`, else the standard table
// alone. The result aliases `*tv`, which the Session owns for its lifetime.
[[nodiscard]] fixpp::wire::dict_hooks session_hooks(
    std::shared_ptr<const fixpp::dict::table_view> const& tv) noexcept {
    return tv ? fixpp::wire::dict_hooks::for_table_view(*tv) : fixpp::wire::dict_hooks::none();
}

// fixpp#421: the tag of an outbound field — non-empty, ASCII digits, no leading
// zero, at most 65535 — or nullopt. Stricter than the inbound scanners, which
// accept zero padding (wire/tag_scan.hpp): an outbound tag is written as is, so a
// non-canonical one is read as a different tag ("052" and "65588" both as 52).
[[nodiscard]] std::optional<std::uint16_t> parse_outbound_tag(std::string_view digits) noexcept {
    if (digits.empty() || digits.front() == '0') return std::nullopt;
    std::uint32_t tag = 0;
    for (const char c : digits) {
        const auto u = static_cast<unsigned char>(c);
        if (u < '0' || u > '9' || !fixpp::wire::accumulate_tag_digit(tag, u)) {
            return std::nullopt;
        }
    }
    return static_cast<std::uint16_t>(tag);
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

    // `value` aliases `buf`, so the struct is NOT trivially copyable: a copy/move
    // must RE-POINT value into the destination's own buf. Returning this struct by
    // value only worked under NRVO (which elides the copy entirely); MSVC debug
    // builds disable NRVO, so the implicit move copied value as a pointer into the
    // source's buf, which was then destroyed — value dangled and read back as 0xCC
    // (uninitialized-stack fill). These explicit special members make the alias
    // NRVO-independent on every toolchain.
    SendingTimeStamp() = default;
    SendingTimeStamp(const SendingTimeStamp& o) noexcept
        : buf(o.buf), value(buf.data(), o.value.size()) {}
    SendingTimeStamp(SendingTimeStamp&& o) noexcept
        : buf(o.buf), value(buf.data(), o.value.size()) {}
    // NOLINTNEXTLINE(cert-oop54-cpp) -- self-assignment copies buf onto itself; safe
    SendingTimeStamp& operator=(const SendingTimeStamp& o) noexcept {
        buf = o.buf;
        value = std::string_view{buf.data(), o.value.size()};
        return *this;
    }
    SendingTimeStamp& operator=(SendingTimeStamp&& o) noexcept {
        buf = o.buf;
        value = std::string_view{buf.data(), o.value.size()};
        return *this;
    }
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
// reply: copy every original field (preserving MsgSeqNum 34), insert
// PossDupFlag(43)=Y and OrigSendingTime(122)=<the stored SendingTime(52)> at
// the header/body boundary, and recompute BodyLength(9)/CheckSum(10) via
// fixpp::wire::Writer. The replayed message keeps its ORIGINAL sequence number
// and does NOT advance the live outbound counter (resend semantics). Stack-only;
// the 9=/10= source fields are skipped (the Writer rebuilds them on commit).
//
// fixpp#420 (ruling 2026-09-14) — SendingTime(52) is RESTAMPED to
// `resend_sending_time`, the time of retransmission; 122 keeps the stored 52.
// FIX-SL 2020 §4.8.4: the retransmitting processor "must modify ... SendingTime(52)
// set to the current sending time • OrigSendingTime(122) set to the SendingTime(52)
// from the original message". FR-010's "the store is the authority" governs where
// 122 comes from, not 52. Copying the stored 52 made every replay older than the
// peer's MaxLatency fail its latency check (QuickFIX-J CheckLatency=Y/120 s, and
// fixpp's own inbound guard). An empty `resend_sending_time` (clock-less Session)
// keeps the stored 52, so this never emits an empty 52=.
// fixpp#424 — a stored frame with no 52: emit 52 = the new stamp and 122 = that
// same value (StandardHeader: "If data is not available set to same value as
// SendingTime"), never an empty 122=. An EMPTY stored 52 is not available either:
// restamped in place like every stored 52, nothing inserted, and 122 (which
// follows the first stored 52) takes the new stamp. A failure is returned,
// never a partial frame; replay_outbound_range_ gap-fills an unbuildable slot (D4a).
//
// #419 supersedes 037's tail placement (43/122 appended after the full stored
// body, groups included): 43 and 122 are standard-header fields and MUST
// precede every body field, or a peer validating field order (QuickFIX-J with
// UseDataDictionary=Y) rejects the replayed frame (373=14). 037's spec.md
// Assumptions section called tail placement "order-safe"; that claim was never
// checked against a strict peer.
//
// Header-tag set S = {8,34,35,49,52,56} (9/10/43/122 are skipped before
// classification by the `continue` below, so they never reach it). Deviates
// from the issue's Fix bullet ("the stored frame's header set comes from the
// canonical header partition, not a positional guess") deliberately: S is
// NOT the full FIX standard header, and is not meant to be. The insertion
// point below is the first stored tag NOT in S. Correctness needs only that
// S is a SUBSET of the real standard header, in every FIX version from 4.0 to
// FIXT.1.1 — every real body tag then lies outside S, so the insertion point
// is at or before the first body field, and every field before it is a
// header field. This holds even when the stored payload carries a header
// tag outside S (115, 128, 97, NoHops, …) that `send_impl` does not forbid:
// such a tag is classified as "body" by S and 43/122 land before it — still
// header-before-body, just earlier within the header than that tag. It also
// holds for the degenerate case (nothing outside S — the fallback below).
// A wider S (the true standard header) would be neutral for interop —
// neither QuickFIX-J nor QuickFIX-cpp validates the relative order of header
// fields after the mandatory 8,9,35 preamble —
// and strictly worse here: it would need a dictionary or a private,
// FIXT-scoped table, for no behavioural gain. Keep S as it is.
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_replay_frame(
    std::span<std::byte> out, std::span<const std::byte> stored,
    std::string_view resend_sending_time, fixpp::wire::dict_hooks const& hooks) noexcept {
    fixpp::wire::Writer w(out, ::fixpp::detail::arena_upstream());
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    const std::size_t n = stored.size();
    // Erratum fixpp#421 (supersedes 040 US3 FR-008's "justified exclusion" of this
    // scanner): the stored frame need not have passed `send_impl`'s tag checks — an
    // older build or a custom MessageStore wrote it — so a tag above 65535 or with a
    // leading zero would alias (65588 → 52 through append_raw's uint16, 052 → 52).
    // Such a frame is not rebuilt: build_replay_frame fails and the slot is gap-filled.

    // Parses one "<tag>=<value>" field at stored[i..], advancing `i` past it
    // (including the terminating SOH). A field with no '=' or a non-digit tag is
    // `malformed` and skipped; a digit-only tag that parse_outbound_tag rejects is
    // `bad_tag`. Shared by the pre-scan pass and the write loop below so the two
    // never diverge.
    //
    // fixpp#426: a Data value counted by the Length field just before it is read
    // by that count, so a SOH inside it stays in the value and is re-emitted
    // verbatim (append_raw copies bytes); the Length was emitted just before it,
    // so the pair stays adjacent. A count that runs past the stored frame or is not
    // followed by SOH is `bad_count`: the frame cannot be rebuilt faithfully, so
    // the slot is gap-filled, like `bad_tag` (design §4). Each pass threads its own
    // `carry`.
    enum class FieldStatus : std::uint8_t { ok, malformed, bad_tag, bad_count };
    struct FieldScan {
        FieldStatus status;
        std::uint16_t tag;
        std::span<const std::byte> value;
    };
    const auto scan_field = [&](std::size_t& i,
                                fixpp::wire::length_data_carry& carry) -> FieldScan {
        const std::size_t tag_start = i;
        bool digits = true;
        while (i < n && stored[i] != EQ && stored[i] != SOH) {
            const auto c = static_cast<unsigned char>(stored[i]);
            if (c < '0' || c > '9') digits = false;
            ++i;
        }
        const auto tag = parse_outbound_tag(
            {reinterpret_cast<const char*>(stored.data() + tag_start), i - tag_start});
        if (i >= n || stored[i] != EQ || !tag) {
            const FieldStatus status = (i < n && stored[i] == EQ && digits)
                                           ? FieldStatus::bad_tag
                                           : FieldStatus::malformed;
            carry.reset();
            while (i < n && stored[i] != SOH) ++i;
            if (i < n) ++i;
            return {.status = status, .tag = 0, .value = {}};
        }
        ++i;  // skip '='
        const std::size_t vstart = i;
        const auto value = carry.read_value(stored, vstart, *tag, hooks);
        if (!value) return {.status = FieldStatus::bad_count, .tag = *tag, .value = {}};
        i = value->end;
        std::span<const std::byte> val{stored.data() + vstart, i - vstart};
        if (i < n) ++i;  // skip SOH
        return {.status = FieldStatus::ok, .tag = *tag, .value = val};
    };

    const auto as_bytes = [](std::string_view sv) {
        return std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    };

    // Pre-scan pass: capture SendingTime(52) BEFORE the write loop runs, so
    // inserting 43/122 at the header/body boundary (the first body tag) never
    // depends on 52 having already been walked by that point. [#419: the old
    // single-pass code appended 122 only after the whole loop, so it could rely
    // on `orig_sending_time` being set by then; inserting mid-loop cannot.]
    // This scan `break`s at the first tag==52 it finds, and `send_impl`'s
    // canonical header always emits 52 at a fixed early position -- so on any
    // frame `send_impl` produced, the scan is bounded by the header prefix,
    // not by the frame's total size (it degenerates to a full scan only if
    // 52 is absent, which `send_impl` cannot produce).
    std::string_view orig_sending_time;
    bool stored_has_52 = false;  // separate from emptiness: an empty 52 is still restamped in place
    {
        fixpp::wire::length_data_carry carry;
        std::size_t i = 0;
        while (i < n) {
            auto fr = scan_field(i, carry);
            if (fr.status == FieldStatus::bad_tag) {  // before a missing 52 can be reported
                return std::unexpected(fixpp::core::error::wire_tag_out_of_range);
            }
            if (fr.status == FieldStatus::bad_count) {
                return std::unexpected(fixpp::core::error::wire_invalid_field_format);
            }
            if (fr.status != FieldStatus::ok) continue;
            if (fr.tag == 52) {
                stored_has_52 = true;
                orig_sending_time = std::string_view{reinterpret_cast<const char*>(fr.value.data()),
                                                     fr.value.size()};
                break;
            }
        }
    }

    // #420: 52 := the retransmission stamp (stored 52 when there is none).
    // #424: 122 := the stored 52, or the new 52 when the store has none.
    const std::string_view sending_time =
        resend_sending_time.empty() ? orig_sending_time : resend_sending_time;
    if (sending_time.empty()) {
        return std::unexpected(fixpp::core::error::wire_required_field_missing);
    }

    // Emits PossDupFlag(43)=Y + OrigSendingTime(122), preceded by SendingTime(52)
    // when the stored frame has none to restamp (#424). Shared by the
    // header/body-boundary insertion point and the degenerate no-body fallback
    // below, so the two emit sites cannot silently diverge.
    const auto append_possdup = [&]() -> fixpp::core::expected_t<void> {
        if (!stored_has_52) {
            if (auto r = w.append_raw(52, as_bytes(sending_time)); !r) {
                return std::unexpected(r.error());
            }
        }
        if (auto r = w.append_raw(43, as_bytes("Y")); !r) return std::unexpected(r.error());
        const std::string_view orig = orig_sending_time.empty() ? sending_time : orig_sending_time;
        if (auto r = w.append_raw(122, as_bytes(orig)); !r) return std::unexpected(r.error());
        return {};
    };

    constexpr std::array<std::uint32_t, 6> kReplayHeaderTags = {8, 34, 35, 49, 52, 56};
    bool inserted_pd = false;
    fixpp::wire::length_data_carry carry;
    std::size_t i = 0;
    while (i < n) {
        auto fr = scan_field(i, carry);
        if (fr.status == FieldStatus::bad_tag) {
            return std::unexpected(fixpp::core::error::wire_tag_out_of_range);
        }
        if (fr.status == FieldStatus::bad_count) {
            return std::unexpected(fixpp::core::error::wire_invalid_field_format);
        }
        if (fr.status != FieldStatus::ok) continue;
        if (fr.tag == 9 || fr.tag == 10 || fr.tag == 43 || fr.tag == 122)
            continue;  // 9/10 recomputed; 43/122 re-inserted below (037 FR-004 dedup)
        if (fr.tag == 52) fr.value = as_bytes(sending_time);  // #420 restamp

        // #419: insert PossDupFlag(43)=Y + OrigSendingTime(122) at the
        // header/body boundary — before the first tag NOT in the header set —
        // instead of after the loop (which placed them after the full body,
        // groups included). Guarded by `!inserted_pd`, so this linear scan
        // over the small fixed-size `kReplayHeaderTags` runs at most once per
        // header field, never once the insertion point has been passed.
        if (!inserted_pd &&
            std::ranges::find(kReplayHeaderTags, fr.tag) == kReplayHeaderTags.end()) {
            if (auto r = append_possdup(); !r) return std::unexpected(r.error());
            inserted_pd = true;
        }
        if (auto r = w.append_raw(fr.tag, fr.value); !r) return std::unexpected(r.error());
    }
    // Fallback for a stored frame with no tag outside S: the insertion point
    // above is never reached (nothing to insert BEFORE), so emit 43/122 here.
    // For a frame with no body, "at the header/body boundary" and "at the
    // tail" are the same position, so this is not a special case of the rule
    // above — it follows from it. Reachable: `Session::send("35=D\x01")`
    // (a bare MsgType, no other field) stores exactly 8,9,35,34,49,52,56,10 —
    // see tests/session/test_resend_answer_field_order.cpp
    // Replay_NoBodyFallback_StillCarries43And122.
    if (!inserted_pd) {
        if (auto r = append_possdup(); !r) return std::unexpected(r.error());
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

// 101-inbound-reject-fidelity D7 (FR-007, FR-008): the inbound reply slot and the echo policy of
// the inbound-fed Reject and BusinessMessageReject sites. Every build here runs after the site's
// peek_outbound() and SendingTime stamp, which it is passed, and before the site's first
// callback and assign_outbound(), so no attempt re-numbers, re-stamps or calls back twice.
namespace {

// D7 steps 1 and 2, shared by both inbound reply builds: the reply built in its site's buffer;
// when that build runs out of room (wire_field_value_truncated), rebuilt in the slot `hold`
// takes. Any other result of the first build is returned as it is.
template <class Hold, class Build>
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_in_site_then_slot(
    std::span<std::byte> site, Hold& hold, Build const& build) noexcept {
    auto r = build(site);
    if (r || r.error() != fixpp::core::error::wire_field_value_truncated) {
        return r;
    }
    return build(hold.take());
}

}  // namespace

std::span<std::byte> Session::inbound_reply_slot_hold::take() noexcept {
    // Every frame rebuilt in the slot is one the resend replay can capture.
    static_assert(kSendFrameBytes <= CaptureVisitor::kCapBufSize);
    if (!taken_) {
        FIXPP_ASSERT_INBOUND_REPLY_SLOT_FREE();
        s_->inbound_reply_slot_held_ = true;
        taken_ = true;
    }
    return std::span<std::byte>{s_->inbound_reply_slot_};
}

fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_reject_(
    std::span<std::byte> site, inbound_reply_slot_hold& hold, seqnum_t seq, seqnum_t ref_seq,
    int ref_tag_id, std::string_view ref_msg_type, int reason, std::string_view sending_time,
    std::string_view text) noexcept {
    auto const build = [&](std::span<std::byte> out, std::string_view rmt) {
        return fixpp::session::build_reject_with_text(
            out, seq, cfg_.sender_comp_id, cfg_.target_comp_id, ref_seq, ref_tag_id, rmt, reason,
            cfg_.begin_string, sending_time, text);
    };
    auto r = build_in_site_then_slot(
        site, hold, [&](std::span<std::byte> out) { return build(out, ref_msg_type); });
    if (r || r.error() != fixpp::core::error::wire_field_value_truncated) {
        return r;
    }
    if (!ref_msg_type.empty()) {
        // 372 is optional in a Reject: above the ceiling it is sent without it; Text(58) stays.
        r = build(hold.take(), std::string_view{});
    }
    // A Reject still over the ceiling, an empty MsgType's among them, is not sent: the record.
    if (!r && r.error() == fixpp::core::error::wire_field_value_truncated) {
        note_reply_over_ceiling_(/*business_reject=*/false, ref_msg_type.size());
    }
    return r;
}

fixpp::core::expected_t<std::span<std::byte>> Session::build_inbound_business_reject_(
    std::span<std::byte> site, inbound_reply_slot_hold& hold, seqnum_t seq, seqnum_t ref_seq,
    std::string_view ref_msg_type, std::string_view sending_time) noexcept {
    auto const build = [&](std::span<std::byte> out) {
        return fixpp::session::build_business_message_reject(
            out, seq, cfg_.sender_comp_id, cfg_.target_comp_id, ref_seq,
            ref_msg_type,  // RefMsgType(372)
            0,             // BusinessRejectReason(380) = Other
            cfg_.begin_string, sending_time);
    };
    // 372 is required in a BusinessMessageReject: there is no step without it.
    auto r = build_in_site_then_slot(site, hold, build);
    if (!r && r.error() == fixpp::core::error::wire_field_value_truncated) {
        note_reply_over_ceiling_(/*business_reject=*/true, ref_msg_type.size());
    }
    return r;
}

void Session::note_reply_over_ceiling_(bool business_reject,
                                       std::size_t ref_msg_type_len) noexcept {
    auto const unlogged = admit_log_record_(reply_ceiling_log_gate_, 1);
    if (!unlogged) {
        return;
    }
    auto const len = fixpp::log::ArgValue::from_u64(ref_msg_type_len);
    auto const suppressed = fixpp::log::ArgValue::from_u64(*unlogged);
    FIXPP_SLOG(logger_.get(), warn, get_trace_context(), fixpp::log::cat::session,
               "inbound reply over the outbound ceiling not sent: msg_type={} "
               "ref_msg_type_len={} suppressed={}",
               business_reject ? FIXPP_SLIT("j") : FIXPP_SLIT("3"), len, suppressed);
}

// 104 (data-model "The too-low Logout text"; research D5 "The text"): QuickFIX's too-low Logout
// text, "MsgSeqNum too low, expecting N but received M", written by one builder for 103's Arm B
// (on_inbound_active_out_of_sequence_) and the Logon arms, so that they cannot diverge.
// N and M are seqnum_t parameters, so the static_assert below (two phrases and two seqnum_t
// numbers at their widest) bounds the text, which cannot truncate. The numbers are written by
// std::to_chars; the peer's bytes are not echoed.
namespace {

constexpr std::string_view kTooLowPhrase1 = "MsgSeqNum too low, expecting ";
constexpr std::string_view kTooLowPhrase2 = " but received ";
using too_low_text_buffer = std::array<char, 96>;
static_assert(kTooLowPhrase1.size() + kTooLowPhrase2.size() +
                  (2 * (std::numeric_limits<seqnum_t>::digits10 + 1)) <=
              too_low_text_buffer{}.size());

// Writes the text for NextNumIn `expected` and MsgSeqNum `received` into `buf`; returns its length.
[[nodiscard]] std::size_t write_too_low_text(too_low_text_buffer& buf, seqnum_t expected,
                                             seqnum_t received) noexcept {
    char* const end = buf.data() + buf.size();
    char* out = std::ranges::copy(kTooLowPhrase1, buf.data()).out;
    out = std::to_chars(out, end, expected).ptr;
    out = std::ranges::copy(kTooLowPhrase2, out).out;
    out = std::to_chars(out, end, received).ptr;
    return static_cast<std::size_t>(out - buf.data());
}

}  // namespace

// 070-fix44-closeout S-029 — a Logon arm's refusal with a Logout, first written for a 464
// posture mismatch (or malformed 464): emit Logout(35=5) carrying reason_text, then
// Disconnected — the session never reaches Active. Its callers:
// `git grep -n 'refuse_logon_with_logout_(' -- src`. Mirrors the
// Logon-time Logout+disconnect disposition
// at session.cpp (SendingTime-accuracy path). Defined after the file-local
// stamp_sending_time helper so it is in scope. [FR-002; data-model D-F]
asio::awaitable<fixpp::core::expected_t<void>> Session::refuse_logon_with_logout_(
    std::string_view reason_text, fsm_state arm, bool arms_wait) noexcept {
    std::array<std::byte, 256> lo_buf{};
    const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
    // gate-b/r2 FQ-5: guard the clock deref — a clock-less direct-Session posture
    // refusal must not null-deref; mirrors the guarded ternary at ~L3034.
    const auto st = effective_clock_
                        ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                        : SendingTimeStamp{};
    // 105 (research D2 item 11): an R-2 ending before Active; its write starts here, and the
    // order is: writing, the write, armed, then the refusal's own Disconnected (E-3 rows 1, 3,
    // 6). A bound that cannot be spawned leaves the Logout unwritten (row 2a).
    // 106 D11 (row 3): the ending claim first; a refused claim writes no Logout.
    bool const write_refused =
        !claim_ending_(ending_claim::ending_logout) ||
        enter_ending_write_(ending_kind::r2, co_await asio::this_coro::executor) ==
            ending_entry::refused;
    auto lo_result = fixpp::session::build_logout(
        std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, reason_text, cfg_.begin_string, st.value);
    bool transmitted = false;
    if (lo_result && !write_refused) {
        ending_write_unwind_guard const leave_guard{.s = this};
        // toAdmin before transmit; a throwing callback → terminal-close +
        // app_callback_threw.
        if (!fire_to_admin_(*lo_result)) {
            (void)leave_ending_write_(false, false);
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            (void)leave_ending_write_(false, false);
            record_disconnected_unless_already_();
            co_return std::unexpected(assign_r.error());
        }
        auto emit_r =
            co_await store_then_emit(lo_seq, *lo_result, arm, true, wire_reset_exemption::none,
                                     &transmitted, write_class::logout);
        // What store_then_emit returns and when it throws: its declaration in session.hpp.
        (void)emit_r;
    }
    // arms_wait false: the Logout is limited, and no wait follows it (the strict-reset refusal).
    (void)leave_ending_write_(transmitted && arms_wait, false);
    // 093 plan OD-26: the Logout's store and write are suspensions.
    disconnect_unless_superseded_(arm);
    co_return fixpp::core::expected_t<void>{};
}

// 104: the too-low Logout's text is built here, in this coroutine's own frame (its declaration
// in session.hpp has the condition), then sent by refuse_logon_with_logout_. NextNumIn is read
// at the body's start, which runs inline in the caller's turn, before any suspension.
asio::awaitable<fixpp::core::expected_t<void>> Session::refuse_too_low_logon_(
    seqnum_t received, fsm_state arm) noexcept {
    too_low_text_buffer text_buf{};
    const std::string_view text{
        text_buf.data(), write_too_low_text(text_buf, seqnum_mgr_.next_inbound_unsafe(), received)};
    co_return co_await refuse_logon_with_logout_(text, arm);
}

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
// Returns expected_t<void>{} on success, or when the Reject is not sent because it exceeds the
// outbound ceiling even without 372 (101-inbound-reject-fidelity D7's outbound-ceiling ending; a
// Reject that overflows only this site's buffer is rebuilt in the inbound reply slot and sent).
// Returns unexpected(error) on assign_outbound / store_then_emit failure, after
// recording Disconnected. Caller should propagate with co_return std::unexpected(…).
// [019-app-callbacks T011/T016; FR-005; D4; INV-4]
asio::awaitable<fixpp::core::expected_t<void>> Session::emit_session_reject_(
    seqnum_t ref_seq, std::string_view ref_msg_type) noexcept {
    std::array<std::byte, kInboundReplySiteBytes> rj_buf{};
    inbound_reply_slot_hold hold{*this};
    const auto rj_st52 = effective_clock_
                             ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                             : SendingTimeStamp{};
    const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
    auto rj_r = build_inbound_reject_(rj_buf, hold, rj_seq, ref_seq,
                                      0,                // RefTagID: n/a for MsgType/veto rejection
                                      ref_msg_type, 3,  // SessionRejectReason = 3
                                      rj_st52.value, std::string_view{});
    if (rj_r) {
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            record_disconnected_unless_already_();
            co_return std::unexpected(assign_r.error());
        }
        // 036 T015: ARM-1 — toAdmin observation before transmit (FR-001/FR-002).
        // Throw arm: Disconnected + app_callback_threw (FR-003).
        // Success path: no Disconnected recorded here (caller decides FSM state).
        if (!fire_to_admin_(*rj_r)) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto emit_r = co_await store_then_emit(rj_seq, *rj_r);
        if (!emit_r) {
            // 106 D11: a cut write or another ending's close returns here (FR-017).
            record_disconnected_unless_already_();
            co_return std::unexpected(emit_r.error());
        }
    }
    co_return fixpp::core::expected_t<void>{};
}

// ── emit_session_reject_ (with reason + ref_tag_id) ──────────────────────────
//
// 041-validation-gate-wiring T010 — overload that threads the mapped
// SessionRejectReason (373) and an optional offending RefTagID (371) through
// to the Reject builder. 092-garbled-frame-reject (research R-5): it builds via
// build_reject_with_text and carries `text` as Text(58); an empty text (the
// default) omits 58, byte-identical to build_reject.
//
// validate() returns a wire_* error slot; the caller maps it via
// wire_error_to_session_reject_reason() (T011) and passes the resulting reason
// here. ref_tag_id == 0 → 371 omitted from the outbound Reject frame. As of
// 075 T020a, validate() threads per-tag provenance out via a required
// ref_tag_out parameter (set by validate_inbound_ below); ref_tag_id remains
// 0 only when the failure site genuinely has no single offending tag (e.g.
// Step-0 header-order).
//
// Identical Disconnected-on-failure handling to the zero-arg overload.
// [041-validation-gate-wiring T010; data-model E-4; RC-C; FR-004]
asio::awaitable<fixpp::core::expected_t<void>> Session::emit_session_reject_(
    seqnum_t ref_seq, std::string_view ref_msg_type, int reason, int ref_tag_id,
    std::string_view text, std::optional<fsm_state> arm) noexcept {
    std::array<std::byte, kInboundReplySiteBytes> rj_buf{};
    inbound_reply_slot_hold hold{*this};
    const auto rj_st52 = effective_clock_
                             ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                             : SendingTimeStamp{};
    const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
    auto rj_r = build_inbound_reject_(rj_buf, hold, rj_seq, ref_seq, ref_tag_id, ref_msg_type,
                                      reason, rj_st52.value, text);
    if (rj_r) {
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            record_disconnected_unless_already_();
            co_return std::unexpected(assign_r.error());
        }
        // 036 T015: ARM-1 — toAdmin observation before transmit (FR-001/FR-002).
        if (!fire_to_admin_(*rj_r)) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto emit_r = co_await store_then_emit(rj_seq, *rj_r, arm);
        if (!emit_r) {
            disconnect_unless_superseded_(arm);
            co_return std::unexpected(emit_r.error());
        }
    }
    co_return fixpp::core::expected_t<void>{};
}

namespace {

// 101-inbound-reject-fidelity (research D3, FR-003): Text(58) of a validation-gate Reject, a
// function of its SessionRejectReason(373): QuickFIX C++'s SessionRejectReason_*_TEXT literal
// (src/C++/Values.h at the commit the spec cites), empty for any other reason. The list is
// derived, not kept: re-derive it from the reasons wire_error_to_session_reject_reason returns
// from a named case, plus those refuse_active_frame_ and validate_inbound_'s 1128 arm set; a
// reason found there that is missing here, other than the map's fail-closed default, is a defect.
[[nodiscard]] std::string_view session_reject_reason_text(int reason) noexcept {
    switch (reason) {
        case 0:
            return "Invalid tag number";
        case 1:
            return "Required tag missing";
        case 2:
            return "Tag not defined for this message type";
        case 4:
            return "Tag specified without a value";
        case 5:
            return "Value is incorrect (out of range) for this tag";
        case 6:
            return "Incorrect data format for value";
        case 11:
            return "Invalid MsgType";
        case 13:
            return "Tag appears more than once";
        case 14:
            return "Tag specified out of required order";
        default:
            return {};
    }
}

// One parse of an ApplVerID value, for 1128, 1137 and 1130 (data-model E-7, E-10). A profile's
// default answers only an empty value (resolve_application_version), and this one's is Unknown, so
// an empty value fails and a non-empty one reads no default.
[[nodiscard]] fixpp::core::expected_t<fixpp::dict::application_version> resolve_ref_appl_ver_id(
    std::string_view value) noexcept {
    return fixpp::dict::resolve_application_version(
        fixpp::dict::version_profile{.session = fixpp::dict::session_version::vt11,
                                     .default_appl = fixpp::dict::application_version::Unknown,
                                     .has_per_message_override = false,
                                     ._reserved = 0},
        value);
}

}  // namespace

// 099-fixt-appl-version-validation (data-model E-7, FR-001, FR-013): the frame's route. The step
// numbers are E-7's. A non-FIXT session takes step 1, on a value fixed at open(), and reads
// inbound_tv_ and validator_, whatever open() did: their owner pointer is never null. So
// does a FIXT session whose route block did not complete, which on_inbound_active_ lets go no
// further.
Session::inbound_route Session::select_route_(std::span<const std::byte> frame,
                                              bool establishment_arm) const noexcept {
    if (!fixt_routing_ || session_route_.owner == nullptr) {
        return inbound_route{.entry = {.owner = &inbound_tv_, .validator = validator_.get()}};
    }
    inbound_route route{.entry = session_route_};
    fixpp::session::detail::fixt_route_peek const peek =
        fixpp::session::detail::peek_fixt_route(frame, session_hooks(inbound_tv_), *inbound_tv_);
    if (!peek.readable) {
        return route;  // step 2: the arm's fault and garble dispositions run on S's route
    }
    route.application = !peek.admin;
    route.header_1128 = peek.header_1128;
    if (peek.admin) {
        route.entry = transport_route_;  // step 2
        return route;
    }
    if (establishment_arm) {
        return route;  // step 3: the negotiated slot is not read
    }
    fixpp::dict::application_version version = route_negotiated_;
    if (peek.header_1128) {
        if (peek.appl_ver_id.empty()) {
            route.refusal = route_refusal::empty_value;  // step 4
            return route;
        }
        // Step 5. The value is not empty here, so no profile's default is read.
        auto const resolved = resolve_ref_appl_ver_id(peek.appl_ver_id);
        if (!resolved) {
            route.refusal = route_refusal::unknown_value;
            return route;
        }
        version = *resolved;
    } else if (!msg_type_defaults_.empty()) {
        // Step 6: the MsgType's entry, else the negotiated version. Empty, it costs this test.
        auto const entry = msg_type_default_for_(peek.msg_type);
        if (entry != fixpp::dict::application_version::Unknown) {
            version = entry;
        }
    }
    route.resolved = version;
    route_entry const& slot = app_routes_[static_cast<std::size_t>(version)];
    if (slot.owner != nullptr) {
        route.entry = slot;
    } else if (peek.header_1128) {
        route.refusal = route_refusal::no_dictionary;  // step 5
    }
    return route;  // step 6's Unknown and slot miss keep S's route
}

// ── 041-validation-gate-wiring FIX-2 + per-message-alloc fix:
//    validate_inbound_ (synchronous, no sub-coroutine frame)
//
// Extracted from the three verbatim validate-gate blocks (the NotConnected,
// Active and LogonSent arms). Each block built the same stack parse arena, re-framed,
// parsed, ran the validator's validate, and emitted a Reject. 093 (data-model E-2) moved the
// parse onto the session's parse buffer B(L), the one the dispatch sites use.
// Now collapsed here; emit_session_reject_ is inlined at each call site so the
// PASS path (returns validate_outcome::pass) is coroutine-frame-free and alloc-free.
//
// Returns validate_outcome::pass when validation passes.
// Returns validate_outcome::reject when a violation is found; caller emits:
//   co_return co_await emit_session_reject_(
//       parse_seqnum(hdr.msg_seq_num), hdr.msg_type, v.reject.reason, v.reject.ref_tag_id);
// Returns validate_outcome::parse_failed when the re-frame or the parse fails; the
// caller closes the session through close_on_late_parse_failure_ (092 contract C-6).
// Superseded decision: that case used to return nullopt, read by every caller as
// "validation passes"; 092-garbled-frame-reject (owner ruling O-2) replaced it.
//
// SYNCHRONOUS — no co_await anywhere. All arenas are stack-local.
// PRECONDITIONS (callers guard):
//   • cfg_.validate_inbound_messages && session_route_.validator must hold
//   • hdr.msg_type != "3" && hdr.msg_type != "5" (FR-004 no-reject-loop)
//   • no callback of this session is on the stack: this is a parse site over the
//     session's parse buffer, and on a build with assertions it asserts that before
//     it builds the resource (fixpp#564, `.specify/563-564-callback-send.md` §2.2)
// [041 T014; data-model E-4; SC-005; simplify-triage FIX-1/FIX-2; const §VIII.5]
Session::InboundValidation Session::validate_inbound_(
    std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& /*hdr*/,
    inbound_route const& route) const noexcept {
    FIXPP_ASSERT_NO_CALLBACK_AT_PARSE_SITE();
    // 093 (data-model E-2): over the session's parse buffer B(L), upstream the spill
    // witness, as the inbound dispatch sites parse.
    std::pmr::monotonic_buffer_resource vg_mr{inbound_parse_buf_.data(), inbound_parse_buf_.size(),
                                              &parse_spill_witness_};
    std::array<std::byte, 512> vg_carry_store{};
    std::pmr::monotonic_buffer_resource vg_carry_mr{vg_carry_store.data(), vg_carry_store.size(),
                                                    ::fixpp::detail::arena_upstream()};
    fixpp::wire::pmr_carry_buffer vg_carry{vg_carry_store.size(), &vg_carry_mr};
    fixpp::wire::Framer vg_framer;
    std::array<fixpp::wire::frame_view, 1> vg_out{};
    auto vg_feed = vg_framer.feed(frame, vg_carry, std::span<fixpp::wire::frame_view>{vg_out});
    if (!vg_feed || vg_feed->empty()) {
        return {.outcome = validate_outcome::parse_failed};
    }
    // fixpp#426 (design §3, item 10): dict-backed over the same table_view
    // the validator holds a copy of, so the OffsetTable this parse builds
    // splits Length+Data pairs (including any dictionary-declared custom
    // pair) the same way the validator's own field walk does. A non-null
    // `session_route_.validator` implies the route's table is set (open() sets
    // both, or refuses).
    //
    // 099-fixt-appl-version-validation (research R-7, FR-006, FR-012): the frame's route, whose
    // table is the one its validator holds.
    assert(*route.entry.owner != nullptr);
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> vg_parser{**route.entry.owner};
    std::array<std::byte, 512> vg_scratch_buf{};
    std::pmr::monotonic_buffer_resource vg_scratch_mr{vg_scratch_buf.data(), vg_scratch_buf.size(),
                                                      ::fixpp::detail::arena_upstream()};
    auto vg_mv_r =
        vg_parser.parse((*vg_feed)[0], &vg_mr,
                        fixpp::wire::OffsetTable::Config{.max_offset_entries = inbound_entry_cap_},
                        detail::parse_capacity::reserve_for(inbound_entry_cap_, frame.size()));
    if (!vg_mv_r) {
        return {.outcome = validate_outcome::parse_failed};
    }
    // 099 (data-model E-7's A3, FR-026): a 1128 past the header on an application route, read
    // ahead of the validator, whose walk has no rule for it. A non-FIXT route is not application.
    constexpr std::uint16_t kApplVerIdTag = 1128;
    if (route.application && !route.header_1128 && vg_mv_r->get(kApplVerIdTag).has_value()) {
        return {.outcome = validate_outcome::reject,
                .reject = RejectDecision{
                    .reason = 14, .ref_tag_id = kApplVerIdTag, .names_third_field = true}};
    }
    std::uint16_t vg_ref_tag = 0;
    auto val_r = route.entry.validator->validate(*vg_mv_r, &vg_scratch_mr, &vg_ref_tag,
                                                 route.entry.envelope);
    if (!val_r) {
        const int vg_reason = fixpp::wire::wire_error_to_session_reject_reason(val_r.error());
        return {.outcome = validate_outcome::reject,
                .reject = RejectDecision{.reason = vg_reason, .ref_tag_id = vg_ref_tag}};
    }
    return {.outcome = validate_outcome::pass};
}

// 099-fixt-appl-version-validation (data-model E-7's A1, A2; FR-009, FR-025): their Reject names
// the third field and the first 34 (`names_third_field`).
//
// 101-inbound-reject-fidelity D5 (FR-005) supersedes 099 FR-025's choice between 373=13 and 14 and
// its FIXT-only gate: the repeat arm runs on every validating session, on `hdr.msg_type_conflict`
// (101 D4). The walk reads every field with the route's hooks (the hooks `hdr`'s scan used; the
// owner ruling research D5 records) and classifies by S's header union; the answer is 14 when a
// later 35 lies past the header, else 13 when one lies in it, else 14. The 1128 arm stays
// FIXT-only.
Session::InboundValidation Session::refuse_active_frame_(
    std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
    inbound_route const& route) const noexcept {
    int reason = 0;
    int ref_tag = 0;
    if (hdr.msg_type_conflict) {
        auto const seen = fixpp::session::detail::fixt_header_repeats_msg_type(
            frame, session_hooks(*route.entry.owner), *inbound_tv_);
        reason = (seen.in_header && !seen.past_header) ? 13 : 14;
        ref_tag = 35;
    } else if (fixt_routing_ && route.refusal != route_refusal::none) {
        reason = route.refusal == route_refusal::empty_value ? 4 : 5;
        ref_tag = 1128;
    } else {
        return {.outcome = validate_outcome::pass};
    }
    return {.outcome = validate_outcome::reject,
            .reject =
                RejectDecision{.reason = reason, .ref_tag_id = ref_tag, .names_third_field = true}};
}

// 101-inbound-reject-fidelity D6 (FR-006): a frame with two MsgTypes is refused in establishment
// on every session, validating or not; the application-frame clause stays 099's.
bool Session::establishment_frame_refused_(
    inbound_route const& route, fixpp::session::detail::FrameHeader const& hdr) const noexcept {
    return hdr.msg_type_conflict ||
           (fixt_routing_ && cfg_.validate_inbound_messages && route.application);
}

bool Session::logon_reject_disconnects_(fixpp::session::detail::FrameHeader const& hdr,
                                        int ref_tag_id) const noexcept {
    return fixt_routing_ && hdr.msg_type == "A" &&
           (ref_tag_id == 1137 || ref_tag_id == 1130 || ref_tag_id == 372);
}

// data-model E-7's S3: the one 1137 decision both roles' blocks take, each at its own place in its
// sequence and under its own gate. Absent is 373=1. Empty, unparseable, or failing the registry,
// which is the test and not the slot, is 373=5. A validating session then reads the group's entries
// (E-10). Precondition: app_version_registry_ is set.
std::expected<fixpp::dict::application_version, Session::logon_group_refusal>
Session::logon_appl_version_(std::optional<std::string_view> default_appl_ver_id,
                             std::span<const std::byte> frame) const noexcept {
    constexpr int kDefaultApplVerIdTag = 1137;
    if (!default_appl_ver_id.has_value()) {
        return std::unexpected(logon_group_refusal{.reason = 1, .ref_tag = kDefaultApplVerIdTag});
    }
    auto const resolved = resolve_ref_appl_ver_id(*default_appl_ver_id);
    if (!resolved.has_value() || !app_version_registry_->get(*resolved).has_value()) {
        return std::unexpected(logon_group_refusal{.reason = 5, .ref_tag = kDefaultApplVerIdTag});
    }
    if (cfg_.validate_inbound_messages) {
        if (auto const group = refuse_msg_type_defaults_(frame)) {
            return std::unexpected(*group);
        }
    }
    return *resolved;
}

// data-model E-10's `**Endings, validating FIXT, either role**`, FR-031. Stores nothing. With two
// faults among the entries the first one read answers, as FR-010's double faults may.
std::optional<Session::logon_group_refusal> Session::refuse_msg_type_defaults_(
    std::span<const std::byte> frame) const noexcept {
    std::optional<logon_group_refusal> refusal;
    std::size_t counted = 0;
    detail::scan_logon_msg_type_defaults(
        frame, session_hooks(*transport_route_.owner),
        [this, &refusal, &counted](detail::logon_msg_type_instance const& e) noexcept {
            if (refusal) {
                return;
            }
            if (!e.msg_direction) {
                refusal = logon_group_refusal{.reason = 1, .ref_tag = 385};
                return;
            }
            std::optional<fixpp::dict::application_version> version;
            if (e.ref_appl_ver_id && !e.ref_appl_ver_id->empty()) {
                auto const resolved = resolve_ref_appl_ver_id(*e.ref_appl_ver_id);
                if (!resolved) {
                    refusal = logon_group_refusal{.reason = 5, .ref_tag = 1130};
                    return;
                }
                version = *resolved;
            }
            if (!e.counted()) {
                return;
            }
            if (++counted > kMaxMsgTypeDefaults) {
                refusal = logon_group_refusal{.reason = 5, .ref_tag = 384};
                return;
            }
            if (e.ref_appl_ver_id &&
                (!version || !app_version_registry_->get(*version).has_value())) {
                refusal = logon_group_refusal{.reason = 5, .ref_tag = 1130};
                return;
            }
            if (e.ref_msg_type.size() > kMaxMsgTypeDefaultBytes) {
                refusal = logon_group_refusal{.reason = 5, .ref_tag = 372};
            }
        });
    return refusal;
}

namespace {

// data-model E-10's lookup over the sorted entries, no heap: where `key` stands or would be
// inserted, and whether it stands there.
template <class Entries>
[[nodiscard]] auto find_msg_type_default(Entries& entries, std::string_view key) noexcept {
    auto const at = std::lower_bound(entries.begin(), entries.end(), key,
                                     [](auto const& d, std::string_view k) noexcept {
                                         return std::string_view{d.msg_type, d.len} < k;
                                     });
    return std::pair{at, at != entries.end() && std::string_view{at->msg_type, at->len} == key};
}

}  // namespace

// data-model E-10: each counted entry whose 1130 parses and whose 372 fits and is an application
// message, within the first kMaxMsgTypeDefaults counted; a later entry for one MsgType wins. The
// kept entries never pass the cap counted entries are held to, so this draws nothing past open()'s
// reservation.
void Session::record_msg_type_defaults_(std::span<const std::byte> frame) noexcept {
    msg_type_defaults_.clear();
    std::size_t counted = 0;
    detail::scan_logon_msg_type_defaults(
        frame, session_hooks(*transport_route_.owner),
        [this, &counted](detail::logon_msg_type_instance const& e) noexcept {
            if (!e.counted() || ++counted > kMaxMsgTypeDefaults) {
                return;
            }
            std::string_view const mt = e.ref_msg_type;
            if (mt.empty() || mt.size() > kMaxMsgTypeDefaultBytes ||
                fixpp::dict::is_fixt_admin_msgtype(mt) || !e.ref_appl_ver_id ||
                e.ref_appl_ver_id->empty()) {
                return;
            }
            auto const resolved = resolve_ref_appl_ver_id(*e.ref_appl_ver_id);
            if (!resolved) {
                return;
            }
            auto const [at, found] = find_msg_type_default(msg_type_defaults_, mt);
            if (found) {
                at->version = *resolved;
                return;
            }
            msg_type_default entry{};
            std::ranges::copy(mt, entry.msg_type);
            entry.len = static_cast<std::uint8_t>(mt.size());
            entry.version = *resolved;
            msg_type_defaults_.insert(at, entry);
        });
}

fixpp::dict::application_version Session::msg_type_default_for_(
    std::string_view msg_type) const noexcept {
    auto const [at, found] = find_msg_type_default(msg_type_defaults_, msg_type);
    return found ? at->version : fixpp::dict::application_version::Unknown;
}

namespace {

// 093-inbound-frame-dispositions (contract C-2 step 1): the summary for one frame
// whose third field is not MsgType(35): one region of kind wire_header_out_of_order
// covering the whole frame.
fixpp::wire::garble_summary header_out_of_order_garble(std::span<const std::byte> frame) noexcept {
    return {.regions = 1U,
            .first_kind = fixpp::core::error::wire_header_out_of_order,
            .discarded = frame.size()};
}

}  // namespace

// ── 092-garbled-frame-reject (fixpp#507): dispose_unparseable_ ──────────────
//
// Contract C-2, rows evaluated top to bottom, for a frame whose header scan
// recorded a fault. Reads only the fault record and the positional header
// identification (C-3 I-1).
// 093-inbound-frame-dispositions (contract C-2 step 1) supersedes 092's D-8: every
// arm that scans disregards a frame whose third field is not 35, faulty or not,
// before calling this disposer, so D-8, and D-1/D-2 for that shape, are unreachable
// here and this disposer only sees a faulty frame whose field 3 is 35.
//   D-1/D-2 (NotConnected, LogonSent): the Logon refusal, as when interpret_logon
//     refuses: Disconnected, nothing sent. 105 FR-007 (research D4): this Disconnected, and
//     D-3's below, owe the connection's close at the edge, so the connection closes once the
//     frame's arm returns, superseding 092's "D-1 and D-2 are a state transition, not a
//     close" and its D-3 twin (the close timing of 092 contract C-2 O-1).
//   D-9 (LogoutSent): disregarded; not the Logout reply, so the logout timeout runs.
//   D-7 (34 not read): disregarded.
//   D-3 (a Logon): silent Disconnected; no Reject, no Logout.
//   D-4 (a SequenceReset): Reject, no advance; NewSeqNo(36) never read.
//   D-5/D-6 (any other type): consume_rejected_seqnum_ (fixpp#423's rule: it advances
//     and persists only at the expected number, and a failed persist returns before
//     the Reject), then the Reject. No ResendRequest, no too-low Logout, and
//     PossDupFlag(43) is not read.
// The Reject: 45 = the first 34 read; 372 = field 3's value, verbatim (101-inbound-reject-
// fidelity D7, superseding 092 research R-5's bound: the echo rule of every inbound-fed
// Reject, so above the outbound ceiling it is omitted); 373 = 0 without 371 for a malformed
// tag, 373 = 5 with 371 = the Length tag for a Length/Data mismatch; 58 = the fixed
// Text for the fault kind.
// No row writes last_inbound_steady_: a faulty frame never refreshes inbound
// liveness (092 FR-018).
// [092 contract C-2; data-model E-1/E-2; research R-3/R-5]
asio::awaitable<fixpp::core::expected_t<void>> Session::dispose_unparseable_(
    FrameHeader const& hdr, fsm_state state) noexcept {
    switch (state) {
        case fsm_state::NotConnected:  // D-1
        case fsm_state::LogonSent:     // D-2
            record_state_transition_(fsm_state::Disconnected);
            co_return fixpp::core::expected_t<void>{};
        case fsm_state::LogonReceived:
        case fsm_state::Active:
            break;
        case fsm_state::LogoutSent:    // D-9
        case fsm_state::Disconnected:  // the Disconnected arm drains without a scan
            co_return fixpp::core::expected_t<void>{};
    }

    const seqnum_t ref_seq = parse_seqnum(hdr.fault_ref_seq_num);
    if (ref_seq == 0) {  // D-7: no 34 read before the fault
        co_return fixpp::core::expected_t<void>{};
    }
    if (hdr.fault_ref_msg_type == "A") {  // D-3; 105: a peer-frame silent ending
        end_session_silently_();
        co_return fixpp::core::expected_t<void>{};
    }
    if (hdr.fault_ref_msg_type != "4") {  // D-5 at the expected number; D-6 otherwise
        if (auto c = co_await consume_rejected_seqnum_(ref_seq, hdr.fault_ref_msg_type); !c) {
            co_return c;
        }
    }

    // D-4, D-5, D-6: the Reject.
    if (hdr.fault == fixpp::wire::field_fault::length_data_mismatch) {
        co_return co_await emit_session_reject_(
            ref_seq, hdr.fault_ref_msg_type, /*reason=*/5, hdr.fault_length_tag,
            fixpp::session::detail::kRejectTextLengthDataMismatch);
    }
    co_return co_await emit_session_reject_(ref_seq, hdr.fault_ref_msg_type, /*reason=*/0,
                                            /*ref_tag_id=*/0,
                                            fixpp::session::detail::kRejectTextMalformedTag);
}

// fixpp#544 (B35 Phase 4, `.specify/544-hot-path-zero-alloc.md` §2.2; orchestrator ruling B ->
// b3): the one definition of a NEW co_await boundary introduced by splitting
// on_inbound_frame into state arms, sub-arms and reply leaves.
//
// asio checks the chain's cancellation state at every co_await of an awaitable
// (awaitable_frame_base::await_transform) and throws operation_aborted when it is cancelled
// and throw_if_cancelled is set. A boundary the split adds would therefore move the first
// check ahead of whatever the moved block runs before its own first co_await (a toAdmin or
// toApp callback, a state transition). The caller reads throw_if_cancelled into `tic`, clears
// it, and awaits the callee; the callee's first statement restores `tic`. The first check is
// then the moved block's first original co_await, as before the split.
//
// Every split boundary uses FIXPP_INBOUND_SPLIT_AWAIT, and every callee it awaits starts with
// FIXPP_INBOUND_SPLIT_ENTRY. `call` must pass `tic` to the callee; `var` receives its result.
// Keep the macro three statements rather than one comma expression: the order a comma
// guarantees is not kept by every compiler's coroutine lowering under `co_return`. To check a
// compiler, build a coroutine that does `co_return (t = co_await read, co_await clear,
// co_await callee(t))` and print the `t` the callee receives.
// Witness: tests/session/test_544_inbound_split_cancel_cells.cpp. Mutant: reduce the first
// macro to `auto var = co_await (call)` and the second to `(void)(tic)`; each cell must then
// fail.
// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses): var is a declared name
#define FIXPP_INBOUND_SPLIT_AWAIT(var, tic, call)             \
    (tic) = co_await ::asio::this_coro::throw_if_cancelled(); \
    co_await ::asio::this_coro::throw_if_cancelled(false);    \
    auto var = co_await (call)
#define FIXPP_INBOUND_SPLIT_ENTRY(tic) co_await ::asio::this_coro::throw_if_cancelled(tic)
// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

// fixpp#544 §2.2 class 2: reply leaves. Each owns the buffer its frame is built in, because
// store_then_emit transmits the caller's span after the store suspends; a Reject or
// BusinessMessageReject that overflows that buffer is rebuilt in the inbound reply slot, which
// the leaf's holder keeps until its tail returns (101-inbound-reject-fidelity D7). A leaf is
// awaited directly by its arm, so the views it takes (sending_time, ref_msg_type, test_req_id)
// point into storage that outlives the await.

// Reject(35=3); assign_outbound before toAdmin. A failed assign or a throwing toAdmin
// disconnects and returns the error; the emit's returned error is discarded (store_then_emit's
// declaration in session.hpp says which errors those are).
asio::awaitable<fixpp::core::expected_t<void>> Session::inbound_reject_leaf_(
    bool tic, seqnum_t ref_seq, int ref_tag_id, std::string_view ref_msg_type, int reason,
    std::string_view sending_time, std::optional<fsm_state> arm) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    std::array<std::byte, kInboundReplySiteBytes> rj_buf{};
    inbound_reply_slot_hold hold{*this};
    const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
    auto rj_r = build_inbound_reject_(rj_buf, hold, rj_seq, ref_seq, ref_tag_id, ref_msg_type,
                                      reason, sending_time, std::string_view{});
    if (rj_r) {
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            record_disconnected_unless_already_();
            co_return std::unexpected(assign_r.error());
        }
        // [[feedback_admin_emit_bypasses_fire_to_admin]]: toAdmin before transmit.
        if (!fire_to_admin_(*rj_r)) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto emit_r = co_await store_then_emit(rj_seq, *rj_r, arm);
        // What store_then_emit returns and when it throws: its declaration in session.hpp.
        (void)emit_r;
    }
    co_return fixpp::core::expected_t<void>{};
}

// Reject(35=3, RefTagID omitted, SessionRejectReason=3) answering a fromAdmin reject.
// Best-effort: a failed assign skips the emit; only a throwing toAdmin returns an error.
asio::awaitable<fixpp::core::expected_t<void>> Session::inbound_reject_best_effort_leaf_(
    bool tic, seqnum_t ref_seq, std::string_view ref_msg_type,
    std::string_view sending_time) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    const seqnum_t rj_seq = seqnum_mgr_.peek_outbound();
    std::array<std::byte, kInboundReplySiteBytes> rj_buf{};
    inbound_reply_slot_hold hold{*this};
    auto rj_r = build_inbound_reject_(rj_buf, hold, rj_seq, ref_seq, 0, ref_msg_type, 3,
                                      sending_time, std::string_view{});
    if (rj_r) {
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (assign_r) {
            // 036 ARM-1 — toAdmin before transmit (FR-001/FR-002). Throw arm: Disconnected +
            // app_callback_threw.
            if (!fire_to_admin_(*rj_r)) {
                record_state_transition_(fsm_state::Disconnected);
                co_return std::unexpected(fixpp::core::error::app_callback_threw);
            }
            (void)co_await store_then_emit(rj_seq, *rj_r);
        }
    }
    co_return fixpp::core::expected_t<void>{};
}

// Logout(35=5); toAdmin before assign_outbound. It carries Text(58) when `text` is not
// empty, which is the deadline of an unfilled resend gap (fixpp#537, D-537-5); every other
// caller passes an empty text and its Logout has none. A frame that cannot be built is not
// sent, and the leaf returns success. A throwing toAdmin or a failed assign disconnects and
// returns the error; the emit's returned error is discarded (store_then_emit's declaration
// in session.hpp says which errors those are).
asio::awaitable<fixpp::core::expected_t<void>> Session::inbound_logout_leaf_(
    bool tic, std::string_view sending_time, std::string_view text, ending_kind kind, bool* armed,
    bool claimed_mark) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 103 (research D4.6): the Logout owner, tested and set before the stamp's use, the
    // number peek and the first suspension. With a Logout already in flight, or the session
    // no longer Active, the leaf writes none and returns success. The only clear is
    // record_state_transition_'s, on the edge into Disconnected. 105: R-1 claims the owner at
    // its decision (run_liveness_loop_body_), so its leaf tests the state alone.
    if ((kind != ending_kind::r1 && logout_in_flight_) || fsm_state_ != fsm_state::Active) {
        if (kind == ending_kind::r1) {
            (void)leave_ending_write_(false, false);
        }
        co_return fixpp::core::expected_t<void>{};
    }
    // 106 D11 (row 3): an ending Logout other than R-1 takes the ending claim before the owner;
    // a refused claim writes nothing. R-1's Logout writes under the give-up's claim.
    if (kind != ending_kind::r1 && !claim_ending_(ending_claim::ending_logout)) {
        co_return fixpp::core::expected_t<void>{};
    }
    logout_in_flight_ = true;
    // 105 (research D2 item 11): the write starts at the owner claim; R-1's started at its
    // decision. A bound that cannot be spawned, and a write the limit cut while R-1 waited for
    // the replay gate, leave the Logout unwritten.
    if (kind != ending_kind::r1) {
        if (enter_ending_write_(kind, co_await asio::this_coro::executor) ==
            ending_entry::refused) {
            co_return fixpp::core::expected_t<void>{};
        }
    } else if (ending_wait_.cut) {
        (void)leave_ending_write_(false, false);
        co_return fixpp::core::expected_t<void>{};
    }
    // 105 (data-model E-3): the leaf leaves the ending state machine on its own path, and by
    // this guard while an exception unwinds the frame.
    ending_write_unwind_guard const leave_guard{.s = this};
    std::array<std::byte, 256> lo_buf{};
    const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
    auto lo_result = fixpp::session::build_logout(
        std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, text, cfg_.begin_string, sending_time);
    bool transmitted = false;
    if (lo_result) {
        // 019 T014: toAdmin before transmitting Logout. [FR-008/010]
        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
        if (!fire_to_admin_(*lo_result)) {
            (void)leave_ending_write_(false, false);
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            (void)leave_ending_write_(false, false);
            record_disconnected_unless_already_();
            co_return std::unexpected(assign_r.error());
        }
        auto emit_r =
            co_await store_then_emit(lo_seq, *lo_result, std::nullopt, true,
                                     wire_reset_exemption::none, &transmitted, write_class::logout);
        // What store_then_emit returns and when it throws: its declaration in session.hpp.
        (void)emit_r;
    }
    bool const is_armed = leave_ending_write_(transmitted, claimed_mark);
    if (armed != nullptr) {
        *armed = is_armed;
    }
    co_return fixpp::core::expected_t<void>{};
}

// LogonSent's Logout-with-Text(58); assign_outbound before toAdmin, and the store_then_emit
// carries the LogonSent arm (093 plan OD-25).
asio::awaitable<fixpp::core::expected_t<void>> Session::inbound_logon_sent_logout_leaf_(
    bool tic, std::string_view text, std::string_view sending_time) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 105 (research D2 item 11): an R-2 ending before Active; its write starts here, before the
    // number assignment and the store call. Its caller records Disconnected after the leaf, so
    // the order is: writing, the write, armed, the refusal's own Disconnected (E-3 rows 1, 3, 6).
    // 106 D11 (row 3): the ending claim first; a refused claim writes no Logout.
    if (!claim_ending_(ending_claim::ending_logout) ||
        enter_ending_write_(ending_kind::r2, co_await asio::this_coro::executor) ==
            ending_entry::refused) {
        co_return fixpp::core::expected_t<void>{};
    }
    ending_write_unwind_guard const leave_guard{.s = this};
    std::array<std::byte, 256> lo_buf{};
    const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
    auto lo_result = fixpp::session::build_logout(
        std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, text, cfg_.begin_string, sending_time);
    bool transmitted = false;
    if (lo_result) {
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            (void)leave_ending_write_(false, false);
            record_disconnected_unless_already_();
            co_return std::unexpected(assign_r.error());
        }
        // 036 T023: ARM-1 — toAdmin observation before transmit (FR-001/FR-002).
        // Throw arm: Disconnected + app_callback_threw (FR-003).
        if (!fire_to_admin_(*lo_result)) {
            (void)leave_ending_write_(false, false);
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto emit_r =
            co_await store_then_emit(lo_seq, *lo_result, fsm_state::LogonSent, true,
                                     wire_reset_exemption::none, &transmitted, write_class::logout);
        // What store_then_emit returns and when it throws: its declaration in session.hpp.
        (void)emit_r;
    }
    (void)leave_ending_write_(transmitted, false);
    co_return fixpp::core::expected_t<void>{};
}

// ResendRequest(2){BeginSeqNo=begin_seqno, EndSeqNo=0}; toAdmin before assign_outbound. A
// failed assign disconnects and returns success (the session stays in its arm per FR-009);
// a failed emit disconnects and returns the error.
// fixpp#537 (D-537-7) supersedes the success this leaf returned for a request it could not
// build: the peer never got the request, so the session ends, with the disposition
// emit_gapfill_async gives a GapFill that cannot be built.
asio::awaitable<fixpp::core::expected_t<void>> Session::inbound_resend_request_leaf_(
    bool tic, seqnum_t begin_seqno, std::string_view sending_time) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 104 (contract RR-5 item 6, "The leaf entry test"; inbound_logout_leaf_'s pattern, B41
    // D-OUT-2): this leaf calls toAdmin before its number, and its frame can reach it after a
    // suspension in which a reset point ran. Once the receipt marker is `reached` it writes
    // nothing: no toAdmin, no number, no store call, no state write. The condition: nothing
    // suspends from this test to assign_outbound (the build and fire_to_admin_ are
    // synchronous, the grant inline), so a leaf past it meets an unfrozen manager. It reads
    // the marker, not the state and not the manager's flag.
    if (logout_seen_ == receipt_marker::reached) {
        co_return fixpp::core::expected_t<void>{};
    }
    std::array<std::byte, 256> rr_buf{};
    const seqnum_t rr_seq = seqnum_mgr_.peek_outbound();
    auto rr_result = fixpp::session::build_resend_request(
        std::span<std::byte>{rr_buf.data(), rr_buf.size()}, rr_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, begin_seqno, 0U, cfg_.begin_string, sending_time);
    if (rr_result) {
        // 019 T014: toAdmin before transmitting ResendRequest. [FR-008/010]
        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
        if (!fire_to_admin_(*rr_result)) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            record_disconnected_unless_already_();
        } else {
            auto emit_r = co_await store_then_emit(rr_seq, *rr_result);
            if (!emit_r) {
                // 106 D11: a cut write or another ending's close returns here (FR-017).
                record_disconnected_unless_already_();
                co_return std::unexpected(emit_r.error());
            }
        }
    } else {
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(fixpp::core::error::dispatch_aborted);
    }
    co_return fixpp::core::expected_t<void>{};
}

// Heartbeat(0) answering a TestRequest, echoing TestReqID(112); toAdmin before
// assign_outbound. A throwing toAdmin, a failed assign or a failed emit disconnects and
// returns the error.
asio::awaitable<fixpp::core::expected_t<void>> Session::inbound_heartbeat_reply_leaf_(
    bool tic, std::string_view test_req_id, std::string_view sending_time) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    std::array<std::byte, 256> hb_buf{};
    const seqnum_t hb_seq = seqnum_mgr_.peek_outbound();
    auto hb_result = fixpp::session::build_heartbeat(
        std::span<std::byte>{hb_buf.data(), hb_buf.size()}, hb_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, test_req_id, cfg_.begin_string, sending_time);
    if (hb_result) {
        // 019 T014: toAdmin before Heartbeat reply. [FR-008/010]
        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
        if (!fire_to_admin_(*hb_result)) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            // Overflow or closed: session-fatal per 005 data-model.md's E3.
            // Do NOT emit with unassigned seq — skip and disconnect.
            record_disconnected_unless_already_();
            co_return std::unexpected(assign_r.error());
        }
        auto emit_r = co_await store_then_emit(hb_seq, *hb_result);
        if (!emit_r) {
            // 106 D11: a cut write or another ending's close returns here (FR-017).
            record_disconnected_unless_already_();
            co_return std::unexpected(emit_r.error());
        }
    }
    co_return fixpp::core::expected_t<void>{};
}

// BusinessMessageReject(35=j) answering a fromApp reject. toApp runs before assign_outbound;
// a veto suppresses the emit and returns success, a throwing toApp closes the session
// terminally and returns the error.
asio::awaitable<fixpp::core::expected_t<void>> Session::inbound_business_reject_leaf_(
    bool tic, seqnum_t ref_seq, std::string_view ref_msg_type,
    std::string_view sending_time) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    const seqnum_t bmr_seq = seqnum_mgr_.peek_outbound();
    std::array<std::byte, kInboundReplySiteBytes> bmr_buf{};
    inbound_reply_slot_hold hold{*this};
    auto bmr_r =
        build_inbound_business_reject_(bmr_buf, hold, bmr_seq, ref_seq, ref_msg_type, sending_time);
    if (bmr_r) {
        // 036 C2: route 35=j through toApp before assign_outbound.
        // toApp fires BEFORE seqnum assignment so a veto consumes
        // no outbound seqnum (C2/INV-COV-5/FR-004).
        // Veto (app_do_not_send): set suppressed=true and fall through
        // to persist_inbound_advance_(), which the caller
        // on_inbound_active_from_app_failed_ runs after this leaf returns —
        // do NOT early-return (under-persist hazard:
        // [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]).
        // Throw: terminal close + early return (persist moot under close).
        // [036 tasks T031; contracts C2; data-model.md INV-COV-5]
        bool suppressed = false;
        if (engine_.application != nullptr) {
            auto to_app_r = parse_and_dispatch_(*bmr_r, kSendParseArena, [&](auto& mv, auto& sid) {
                return engine_.application->toApp(mv, sid);
            });
            if (!to_app_r) {
                if (to_app_r.error() == fixpp::core::error::app_callback_threw) {
                    (void)co_await close(close_mode::terminal);
                    co_return std::unexpected(fixpp::core::error::app_callback_threw);
                }
                // app_do_not_send: suppress the BMR emit; still fall through to
                // persist_inbound_advance_(), which the caller
                // on_inbound_active_from_app_failed_ runs after this leaf returns.
                suppressed = true;
            }
        }
        if (!suppressed) {
            auto assign_r = co_await seqnum_mgr_.assign_outbound();
            if (!assign_r) {
                record_disconnected_unless_already_();
                co_return std::unexpected(assign_r.error());
            }
            auto emit_r = co_await store_then_emit(bmr_seq, *bmr_r);
            if (!emit_r) {
                // 106 D11: a cut write or another ending's close returns here (FR-017).
                record_disconnected_unless_already_();
                co_return std::unexpected(emit_r.error());
            }
        }
    }
    co_return fixpp::core::expected_t<void>{};
}

// The acceptor's reply Logon: build, assign_outbound, toAdmin and store_then_emit. Writes the
// pre-reply next-outbound into n_pre_outbound (031). A failed build, assign or emit
// disconnects and returns the error; a throwing toAdmin returns app_callback_threw.
asio::awaitable<fixpp::core::expected_t<void>> Session::inbound_acceptor_reply_logon_leaf_(
    bool tic, bool peer_sent_reset, int heartbt_sec, std::string_view reply_sending_time_view,
    seqnum_t& n_pre_outbound) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    std::array<std::byte, 256> reply_buf{};
    // RC#A (gate-b/r1-green): peek via manager (not bare field).
    // RC#B: gate the advance on build success; gate Active on emit success.
    // RC#C-2 (gate-b/r2): bilateral_lenient also mirrors 141=Y in reply —
    // it is the defining behavior of bilateral_lenient (FR-017:148-149).
    // unilateral: outbound 141 is config-driven, NOT mirror-driven (FR-017:149).
    // peer_sent_reset is passed in by on_inbound_not_connected_, which captured it from its
    // seqnum-check block.
    // [spec.md FR-017: bilateral_strict + bilateral_lenient mirror 141=Y in reply]
    const bool acpt_reset_seqnum =
        (cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict ||
         cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_lenient) &&
        peer_sent_reset;
    const seqnum_t reply_seq = seqnum_mgr_.peek_outbound();
    n_pre_outbound = reply_seq;  // 031: capture N_pre before the reply consumes it
    // 027 T013 I-NEX-1, E-OBO: acceptor reply is built AFTER check_inbound (in
    // on_inbound_not_connected_, before it awaits this leaf) which already advanced
    // next_inbound_. Advertise plain next_inbound_unsafe() — NO +1 (E-OBO). Value is
    // cause-dependent under 141 reset (data-model Reset table). [contract C2, I-NEX-1, E-OBO]
    const std::optional<fixpp::session::seqnum_t> acpt_next_expected =
        cfg_.enable_next_expected_msg_seq_num
            ? std::optional<fixpp::session::seqnum_t>{seqnum_mgr_.next_inbound_unsafe()}
            : std::nullopt;
    // 033 T017/T023: thread the FIXT-only Logon fields (1137 + optional 553/554) into
    // the acceptor reply — all-nullopt for FIX.4.x → byte-identical (INV-FIXT-1/W4);
    // the acceptor advertises its OWN config (FR-002 / R1).
    const auto acpt_fixt = derive_logon_fixt_fields(cfg_);
    // 070-fix44-closeout: advertise 464=Y when local posture==test (S-029).
    // posture unset ⇒ test_message_indicator false ⇒ no 464 ⇒ byte-identical
    // baseline (FR-012). 383/384 advertise lands per-story (US2/US3).
    auto reply_logon = fixpp::session::build_logon(
        std::span<std::byte>{reply_buf.data(), reply_buf.size()}, reply_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, cfg_.begin_string, heartbt_sec, reply_sending_time_view,
        acpt_reset_seqnum, acpt_next_expected, acpt_fixt.default_appl_ver_id, acpt_fixt.username,
        acpt_fixt.password,
        fixpp::session::logon_advertise_options{
            .max_message_size = cfg_.advertised_max_message_size,
            .test_message_indicator = (cfg_.posture == session_posture::test),
            .supported_msg_types = cfg_.supported_msg_types});
    if (!reply_logon) {
        // Build failed (oversized IDs → wire_frame_too_large).
        // RC#B: must NOT reach Active — Disconnected, propagate error.
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(reply_logon.error());
    }
    // Advance outbound counter through manager (RC#A: was ++next_outbound_seq_).
    auto assign_r = co_await seqnum_mgr_.assign_outbound();
    if (!assign_r) {
        record_disconnected_unless_already_();
        co_return std::unexpected(assign_r.error());
    }
    // 019 T014: toAdmin before transmitting the acceptor reply Logon. [FR-008/010]
    if (!fire_to_admin_(*reply_logon)) {
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(fixpp::core::error::app_callback_threw);
    }
    // 093 plan OD-25: the store await is a suspension; store_then_emit
    // tests the predicate after it, before the transmit.
    auto emit_r = co_await store_then_emit(reply_seq, *reply_logon, fsm_state::LogonReceived);
    if (!emit_r) {
        // Emit failed (transport error). RC#B: Disconnected, not Active
        // (093 plan OD-26: unless the arm was superseded during the emit).
        disconnect_unless_superseded_(fsm_state::LogonReceived);
        co_return std::unexpected(emit_r.error());
    }
    co_return fixpp::core::expected_t<void>{};
}

// T024/T025 (US1, Phase 3) + T032/T034/T035 (US2, Phase 4) +
// T056 (US5, Phase 7): Inbound FSM dispatch.
//
// 092-garbled-frame-reject (fixpp#507, contract C-1): in every state arm, right after
// the header scan and before step (1), a frame whose scan recorded a fault goes to
// dispose_unparseable_ and the arm returns. The check is an inline compare, so a
// fault-free frame pays no coroutine frame for it.
//
// Guard precedence per data-model.md matrix preamble (T056 adds steps 1/3/5):
//   (1) parse/type recognised → else session Reject; for a well-formed frame the
//       no-loop-guard exempts Reject(35=3) and Logout(35=5) from triggering a Reject.
//       A faulty frame never reaches step (1): the check above disposes of it under
//       092 contract C-2 whatever its MsgType, so a faulty Reject or Logout can be
//       Rejected.
//   (2) CompID/BeginString gate (post-logon states)
//   (3) SendingTime(52) MaxLatency vs effective clock (Q3):
//       established session → Reject(reason=10, refTag=52) → Logout → Disconnect
//       Logon path         → logout-with-error, no standalone Reject (D-3)
//   (4) seqnum class (too-low / too-high / in-seq) — T035
//   (5) message-type-for-state: unrecognized app msg type → session Reject;
//       session stays Active (no loop for Reject/Logout).
//
// Seqnum check (T031/T032/T035):
//   Too-low  → session_seqnum_too_low (69)              → fatal: Disconnected, after
//              QuickFIX's too-low Logout: in Active unless 43=Y (103, research D9; a
//              too-low Heartbeat included, which supersedes T020-A's silent drop); in
//              NotConnected and LogonSent whatever its 43, once the identity gates pass
//              (104, research D5)
//   Too-high → 013 FR-009: AwaitingResend + ResendRequest(2) via reconnect_fsm_
//              (slot 70 session_seqnum_gap_unrecoverable deleted per 013 T006a;
//               state owned by ReconnectFsm per data-model §E-1 / Fix1 T023)
//   In-seq   → advance counter, proceed
//
// For the NotConnected/Logon path the peer's first Logon carries seq=1.
// If seq != 1 → too-low or too-high → fatal.
//
// Inbound ordering: deliver-then-persist (D-8 supersedes I-3 / [2e §7.6] store-before-deliver).
// 029 T010 wires persist_inbound_advance_() AFTER the fromAdmin/fromApp callback returns,
// not before — at-least-once delivery (INV-H2); seqnum gate is here.
//
// LogoutSent / Disconnected: all inbound silently drained (defined cells).
//
// fixpp#544 (B35 Phase 4, §2.2; orchestrator ruling A -> a2): on_inbound_frame is a
// non-coroutine and adds no frame. It returns the LogonReceived/Active arm's awaitable, and
// that arm reads fsm_state_ when it first resumes, on the executor that awaits it, as the
// coroutine this replaces did; any other state is handed to on_inbound_cold_.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_frame(
    std::span<const std::byte> frame) noexcept {
    return on_inbound_active_(frame);
}

// The arm for a state other than LogonReceived/Active. A non-coroutine: called from the
// Active arm after its state read, so it adds no frame of its own.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_cold_(
    bool tic, std::span<const std::byte> frame) noexcept {
    switch (fsm_state_) {
        case fsm_state::NotConnected:
            return on_inbound_not_connected_(tic, frame);
        case fsm_state::LogoutSent:
            return on_inbound_logout_sent_(tic, frame);
        case fsm_state::LogonSent:
            return on_inbound_logon_sent_(tic, frame);
        case fsm_state::LogonReceived:
        case fsm_state::Active:
            break;
        case fsm_state::Disconnected:
            // 105 (research D2 item 3; C-2 W-1 before Active): reached only while a wait armed
            // before Active is open. A well-formed Logout with the right identity ends it; every
            // other frame is drained. Nothing is delivered, advanced or reset.
            if (ending_wait_.phase == ending_wait_state::phase_t::armed &&
                !ending_wait_.from_active()) {
                auto const hdr = scan_frame_header(frame, session_hooks(inbound_tv_));
                if (hdr.fault == fixpp::wire::field_fault::none && hdr.msg_type_is_third &&
                    hdr.msg_type == "5" && hdr.begin_string == cfg_.begin_string &&
                    (!cfg_.check_comp_id || (hdr.sender_comp_id == cfg_.target_comp_id &&
                                             hdr.target_comp_id == cfg_.sender_comp_id))) {
                    // The wake only cancels the bound's timer, so the owe after it is
                    // recorded before the bound runs.
                    resolve_ending_wait_();
                    owe_connection_end_();
                }
            }
            break;
    }
    // Disconnected row: all inbound cells `ignored` per matrix.
    return on_inbound_drained_(tic);
}

asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_drained_(bool tic) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    co_return fixpp::core::expected_t<void>{};
}

// NotConnected arm (acceptor: the peer's first Logon).
// NOLINTNEXTLINE(readability-function-size,hicpp-function-size)
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_not_connected_(
    bool tic, std::span<const std::byte> frame) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 092 (data-model E-2, research R-3): the arm's one header scan, hoisted
    // here; later code in this arm reads `hdr` rather than scanning again.
    // A frame the scan could not read is refused (C-2 D-1).
    //
    // 099-fixt-appl-version-validation (data-model E-7, research R-7): the scan uses the frame's
    // route, selected here so that it lives in this frame.
    inbound_route const route = select_route_(frame, /*establishment_arm=*/true);
    auto hdr = scan_frame_header(frame, session_hooks(*route.entry.owner));
    // 093 (contract C-2 step 1): a frame whose third field is not MsgType(35),
    // faulty or not, is disregarded as garbled: counted, evented and logged, with
    // no Reject, NextNumIn kept and no liveness refresh. It runs before 092's
    // fault branch, superseding 092's D-8, and D-1/D-2 for this shape.
    if (!hdr.msg_type_is_third) {
        note_garbles_(header_out_of_order_garble(frame));
        co_return fixpp::core::expected_t<void>{};
    }
    // 093 (contract C-2 step 2; spec FR-030, FR-041; fixpp#523): once close() has
    // begun, or Engine::stop()'s step 1 has set the engine-stop flag on this strand
    // (data-model E-13), this arm acts on nothing more. The check is the arm's own
    // predicate, so a frame delivered after stop's step 1 meets it before any
    // effect. Garbled-frame accounting above is a transport observation, not an
    // arm effect, so it still runs (plan OD-7).
    if (logon_arm_superseded_(fsm_state::NotConnected)) {
        co_return fixpp::core::expected_t<void>{};
    }
    if (hdr.fault != fixpp::wire::field_fault::none) {
        co_return co_await dispose_unparseable_(hdr, fsm_state::NotConnected);
    }

    // 099-fixt-appl-version-validation (data-model E-7's S1, FR-029, FR-005): on a validating FIXT
    // session an application frame ends here with nothing sent; 101 D6 (FR-006): so does a frame
    // with two MsgTypes, on every session.
    if (establishment_frame_refused_(route, hdr)) {
        record_state_transition_(fsm_state::Disconnected);
        co_return fixpp::core::expected_t<void>{};
    }

    // ── 041-validation-gate-wiring T014: validate-first gate ──────────────
    // Run BEFORE interpret_logon so a dict-invalid Logon produces a Reject
    // instead of a silent Disconnect (C-2 rows a/c-i/validate-first ordering).
    // Guard: outer cfg_.validate_inbound_messages + inner non-null session_route_.validator
    // (set at open(); on a FIXT session it is non-null whether validating or not).
    // No-reject-loop: skip validate for 35=3 (Reject) and 35=5 (Logout) —
    // these are drained silently on this arm anyway (FR-004 no-loop guard).
    // Only a well-formed frame reaches here: a faulty one is refused above
    // (092 contract C-2 D-1).
    // Seqnum is NOT advanced on validate failure (validate fires before
    // check_inbound — C-3 invariant). [041 T014; data-model E-4; SC-005]
    // Buffer: the gate parses over the session's parse buffer B(L), as dispatch
    // does, so it never under-parses relative to dispatch (093, data-model E-2).
    if (cfg_.validate_inbound_messages && session_route_.validator) {
        if (hdr.msg_type != "3" && hdr.msg_type != "5") {
            auto const v = validate_inbound_(frame, hdr, route);
            // 092 contract C-6: a late parse failure closes the session.
            if (v.outcome == validate_outcome::parse_failed) {
                co_return co_await close_on_late_parse_failure_();
            }
            if (v.outcome == validate_outcome::reject) {
                // 106 D1 supersedes 104 DQ-6 / L-104-3 (answers before the gates): the Reject,
                // decided here, is held until the identity is known (research D1 item 2). A
                // refused identity ends the arm with nothing written. Where the Reject ends the
                // arm (logon_reject_disconnects_), the consuming gate admits the identity before
                // the write (N-13.2); where it stays in the arm, nothing is consumed, so the next
                // Logon meets the full gate (F-2).
                if (logon_identity_refuses_(frame) ||
                    (logon_reject_disconnects_(hdr, v.reject.ref_tag_id) &&
                     !logon_identity_admitted_())) {
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }
                auto rj = co_await emit_session_reject_(parse_seqnum(hdr.msg_seq_num), hdr.msg_type,
                                                        v.reject.reason, v.reject.ref_tag_id,
                                                        session_reject_reason_text(v.reject.reason),
                                                        fsm_state::NotConnected);
                // 099 (data-model E-7's S2, FR-010): any other Reject stays in the arm.
                if (rj && logon_reject_disconnects_(hdr, v.reject.ref_tag_id)) {
                    disconnect_unless_superseded_(fsm_state::NotConnected);
                }
                co_return rj;
            }
        }
    }

    // First message must be a Logon. interpret_logon validates:
    //   MsgType==A, BeginString==cfg_.begin_string,
    //   SenderCompID==cfg_.target_comp_id (peer's sender = our target),
    //   TargetCompID==cfg_.sender_comp_id (peer's target = our sender),
    //   HeartBtInt present and ≥ 0.
    auto result = fixpp::session::interpret_logon(
        frame,
        cfg_.target_comp_id,  // expected_sender: peer's 49= is our target
        cfg_.sender_comp_id,  // expected_target: peer's 56= is our sender
        cfg_.begin_string, session_hooks(interpret_logon_table_()));

    if (!result) {
        // Refusal — BeginString/CompID mismatch or not-Logon.
        // Per matrix NotConnected row (005 data-model.md):
        //   inbound Logon (refused)   → Disconnected (FR-006 / RC#3)
        //   inbound non-Logon (first) → Disconnected
        // No MsgType discrimination: every refusal on this row lands in
        // Disconnected. Phase-3 "stays NotConnected" compromise removed
        // by T014 [US3] per spec.md FR-006 + opus_pr81_1_triage.md RC#3.
        record_state_transition_(fsm_state::Disconnected);
        co_return fixpp::core::expected_t<void>{};
    }

    // 106 D1 supersedes 104 DQ-6 / L-104-3 (answers before the gates): the identity gates run
    // here, once, right after interpret_logon and before hydration (research D1 item 4): the mTLS
    // CompID-authorisation gate (logon_identity_admitted_(), whose comment has its arms), then
    // the credentials gate of 033 T023. A refusal records Disconnected and returns success with
    // nothing written and no number changed, in memory or in the store; posture, SendingTime,
    // strict, reset_on_logon, the sequence check, the too-low Logout, 1137 and the reply follow.
    if (!logon_identity_admitted_() || !logon_credentials_admitted_(*result)) {
        record_state_transition_(fsm_state::Disconnected);
        co_return fixpp::core::expected_t<void>{};
    }

    // Valid Logon: read seqnum + 013 T027 ResetSeqNumFlag(141) from the arm's
    // header scan (hoisted to the top of the arm by 092).
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
    // 029 gate-b/r1: hoisted to case scope so the persist guard at the
    // Logon persist site (below) can read it. Set to chk.has_value() after
    // the classification; false on the behind-side tolerated path (chk failure)
    // and stays false on any early-return before the classification executes.
    // [029 INV-H1 fix; triage root-cause #1/#2; contracts C3.1]
    // 106 D2: it means "classified in sequence; consumed at the commit".
    bool logon_inbound_advanced = false;
    {
        peer_789_raw = hdr.next_expected_msg_seq_num;
        peer_789_present = hdr.next_expected_present;
        // 070-fix44-closeout S-030 (FR-007): capture the peer's advertised
        // MaxMessageSize(383) from its inbound Logon (observability only).
        peer_advertised_max_message_size_ = parse_u32_opt(hdr.max_message_size);
        const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
        if (seq == 0) {
            // Cannot parse seq — treat as invalid (fatal for protocol safety).
            record_state_transition_(fsm_state::Disconnected);
            co_return fixpp::core::expected_t<void>{};
        }

        // 024/030: capture peer_sent_reset before check_inbound. The reset SITE is
        // cause-dependent (originally a /speckit-verify FR-001 finding; 030 then
        // corrected the received-141 inbound post-state 1→2, so BOTH arms now end at
        // next_inbound==2, matching QuickFIX reset-then-increment):
        //   - knob-driven (reset_on_logon): reset BEFORE check_inbound so a fresh
        //     peer Logon at seq=1 is admitted even when local next_inbound > 1
        //     (Gate A note (a)); post-state next_inbound == 2 (witness 6).
        //   - 013-only received-141 (no knob): reset AFTER check_inbound (below), then
        //     030 restores the consumed seq-1 reset Logon's advance → next_inbound == 2
        //     (reset_seqnum_policy_matrix acceptor cells; FR-017:150; 030 FR-001). The
        //     OUTBOUND reply still stays seq 1 (independent counter; 030 FR-003).
        // The two are mutually exclusive (the post-check arm guards on !reset_on_logon),
        // so exactly one store reset fires per path (C5.1 / witness 7).
        // [024 data-model acceptor row; Gate A notes (a)/(d); C2.2/C2.6; FR-001; 030
        // FR-001]
        peer_sent_reset = (hdr.reset_seqnum_flag == "Y");

        // 029 T007/T011 — call site 2 (acceptor): hydrate at the first counter
        // touch (lazy, D-1/D-6), AFTER the peer_sent_reset header pre-scan and
        // BEFORE the reset_on_logon block + check_inbound (C2.4/C2.5).
        // Decide-then-apply (RC-1 / C2.4): WITHHOLD the inbound seed when the peer
        // is announcing a reset (141=Y) OR reset_on_logon is set — leave next_inbound
        // at seqnum_min so check_inbound(1) is in-sequence and the existing reset arm
        // (received-141 at the post-check block / reset_on_logon below) owns the
        // post-state; a hydrated next_inbound never pre-empts a peer reset Logon into
        // a too-low fatal (INV-H5). The OUTBOUND seed is applied unconditionally and
        // runs BEFORE the reset_on_logon reset so the 024 reset still wins (INV-H5).
        // One-shot latch makes reconnect a no-op (INV-H3).
        // [029 tasks T007/T011 call-site 2; contracts C2.4/C2.5/C2.6; data-model
        // INV-H3/H5/RC-1]
        {
            const bool withhold_inbound = peer_sent_reset || cfg_.reset_on_logon;
            // 025 T006 — refresh_on_logon: re-read store on each logon (store-wins).
            // Same suppression gate as call-site 1: bilateral_strict → force=false; under
            // strict the Logon always resets, so a refresh has nothing to set (103 D2.2).
            // [025 contracts C3.1–C3.3; FR-002/008/009]
            const bool refresh_active =
                cfg_.refresh_on_logon && cfg_.reset_seqnum_policy_field !=
                                             fixpp::session::reset_seqnum_policy::bilateral_strict;
            auto h_r = co_await ensure_hydrated_(/*apply_inbound_seed=*/!withhold_inbound,
                                                 /*force=*/refresh_active, fsm_state::NotConnected);
            if (!h_r) {
                // ensure_hydrated_ already transitioned to Disconnected (C2.3),
                // unless the arm was superseded during the read (093 OD-26).
                co_return std::unexpected(h_r.error());
            }
        }
        // fixpp#518: a close() may have run while hydrate yielded.
        if (logon_arm_superseded_(fsm_state::NotConnected)) {
            co_return fixpp::core::expected_t<void>{};
        }

        // 070-fix44-closeout S-029: TestMessageIndicator(464) posture-mismatch
        // refusal on the acceptor's inbound Logon. Opt-in — cfg_.posture unset
        // ⇒ inert, byte-identical baseline (FR-012). Mismatch/malformed ⇒
        // Logout+disconnect. [FR-002]
        //
        // Guard placement (Gate B PR #189 New P2 / FQ-3): AFTER ensure_hydrated_
        // (block above) — same rationale as the 038 SendingTime guard immediately
        // below — so seqnum_mgr_.peek_outbound() inside refuse_logon_with_logout_
        // returns the durable next-outbound N (not the un-hydrated default) when
        // building the refusal Logout for a persistent/reconnect acceptor. Still
        // BEFORE reset_on_logon/check_inbound so inbound is NOT advanced on reject.
        if (cfg_.posture.has_value() &&
            should_refuse_posture(*cfg_.posture, hdr.test_message_indicator)) {
            co_return co_await refuse_logon_with_logout_("TestMessageIndicator posture mismatch",
                                                         fsm_state::NotConnected);
        }

        // ── 038 T006: US1 — AcceptorLogon SendingTime(52) MaxLatency guard ──
        //
        // Guard placement: AFTER ensure_hydrated_ (line above) so the hydrated
        // outbound counter is available (peek_outbound returns the durable N) and
        // BEFORE reset_on_logon/check_inbound so inbound is NOT advanced on reject.
        // Contract C-1: absent/empty/malformed/stale-past/stale-future all → reason=10.
        // Pre-establishment shape: Reject only, NO Logout (contrast with Q3 established
        // path which emits Reject then Logout). [038 spec FR-002/FR-003/FR-004; data-model
        // INV-4; contracts/acceptor-logon-sendingtime.md C-1]
        // [[feedback_admin_emit_bypasses_fire_to_admin]]: fire_to_admin_ before emit.
        // [[feedback_fixed_buffer_build_failure_silent_success]]: ≥512B buffer;
        // fail-closed. Guard is skipped when effective_clock_ is null (analogous to Q3
        // established-session SendingTime MaxLatency guard, T055/T056).
        if (effective_clock_) {
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
                    sending_time_ok = fixpp::session::check_sending_time(
                                          *parse_r, effective_clock_->now(), max_lat)
                                          .has_value();
                }
                // parse failure: sending_time_ok stays false → path fires.
            }
            // absent (empty string_view) or empty (present but zero-length value):
            // sending_time_ok stays false → path fires.

            if (!sending_time_ok) {
                // Emit Reject(35=3, RefTagID=52, SessionRejectReason=10) then
                // Disconnected. NO Logout (pre-establishment; peer has not yet
                // been accepted into Active).
                const auto rj_st52 =
                    stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
                {
                    FIXPP_INBOUND_SPLIT_AWAIT(
                        r, tic,
                        inbound_reject_leaf_(tic, parse_seqnum(hdr.msg_seq_num),
                                             52,   // RefTagID = 52 (SendingTime)
                                             "A",  // RefMsgType = Logon
                                             10,  // SessionRejectReason = 10 (SendingTime accuracy)
                                             rj_st52.value, fsm_state::NotConnected));
                    if (!r) {
                        co_return r;
                    }
                }
                // Fail-closed: Disconnected whether rj_r succeeded or not
                // (093 plan OD-26: unless the arm was superseded meanwhile).
                disconnect_unless_superseded_(fsm_state::NotConnected);
                co_return fixpp::core::expected_t<void>{};
            }
        }

        // 103 (research D2.4; FR-004): bilateral_strict refuses a Logon without 141=Y with
        // a Logout carrying kStrictResetText, at the posture guard's position: after
        // ensure_hydrated_ (the Logout's 34 is the durable next outbound number) and the
        // SendingTime guard, before the reset_on_logon block and check_inbound, so an
        // out-of-sequence Logon is refused the same way, NextNumIn does not move, and a
        // strict acceptor with reset_on_logon does not reset its store for a Logon it
        // refuses. refuse_logon_with_logout_ records Disconnected unless the arm was
        // superseded meanwhile, and does not send a Logout that does not fit its buffer.
        // Its error is returned; a superseded arm returns success (D-CAN-5); otherwise
        // the mismatch. The helper has just recorded Disconnected, and a completed close()
        // also leaves Disconnected, so the FSM alone cannot tell them apart: the arm is
        // superseded when a close() has begun or completed, Engine::stop() has noted the
        // stop, or the FSM holds a state other than Disconnected.
        if (!peer_sent_reset &&
            cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict) {
            static constexpr std::string_view kStrictResetText = "ResetSeqNumFlag(141)=Y required";
            // 105 (spec.md Clarifications, the Phase 4 implementation session): exempt from
            // the R-2 wait. The arm's error closes the connection at once (data-model E-3 row
            // 5b), so its Logout arms no wait.
            auto refused = co_await refuse_logon_with_logout_(
                kStrictResetText, fsm_state::NotConnected, /*arms_wait=*/false);
            if (!refused) {
                co_return refused;
            }
            if (closing_or_stopped_() || fsm_state_ != fsm_state::Disconnected) {
                co_return fixpp::core::expected_t<void>{};
            }
            co_return std::unexpected(fixpp::core::error::session_seqnum_reset_mismatch);
        }

        if (cfg_.reset_on_logon) {
            // Knob-driven → fatal: a store failure blocks Active (C2.6).
            auto rst_r = co_await reset_seqnums_to_one_durable(reset_disposition::fatal);
            if (!rst_r) {
                disconnect_unless_superseded_(fsm_state::NotConnected);  // 093 OD-26
                co_return std::unexpected(rst_r.error());
            }
        }
        // fixpp#518: a close() may have run while the reset_on_logon reset yielded.
        if (logon_arm_superseded_(fsm_state::NotConnected)) {
            co_return fixpp::core::expected_t<void>{};
        }

        // 106 D2: classified here; the advance is committed after the 1137 block.
        auto chk = co_await seqnum_mgr_.classify_inbound(seq);
        if (!chk) {
            // 027 T016 — behind-side tolerance (formulation A, I-NEX-5/D-7):
            // When the knob is on AND the failure is too-high (NOT too-low),
            // do NOT take the fatal branch. Leave next_inbound_ at X (the
            // classification does not advance it, and no commit runs). Do NOT call
            // set_next_inbound. Emit no at-logon ResendRequest. Proceed toward Active so the peer's
            // proactive resend [X, peer_N-1] arrives in-sequence through the Active path. [contract
            // C5, data-model I-NEX-5/12, research D-7]
            //
            // A too-low result takes the 104 branch below; any other refusal ends the
            // session (I-2/[FIX-SL §4.1]).
            if (cfg_.enable_next_expected_msg_seq_num &&
                chk.error() == fixpp::core::error::session_seqnum_too_high) {
                // Behind-side: tolerate. next_inbound_ stays at X (not advanced).
                // Fall through to the existing acceptor reply-build path below.
                // logon_inbound_advanced stays false (not in sequence: no commit).
            } else if (chk.error() == fixpp::core::error::session_seqnum_too_low) {
                // 104 (FR-012 to FR-014; research D5, D6; contract T2, T8) supersedes 103's
                // spec row "A too-low frame in NotConnected or LogonSent still ends silently":
                // a too-low Logon draws QuickFIX's too-low Logout, whatever its 43, then
                // Disconnected. 106 D1 supersedes 104 T8 (the too-low branches' own gate calls):
                // one gate site per arm; the identity gates ran right after interpret_logon, so
                // the identity reaching here is admitted. 1137 is not reached. The Logout carries
                // Text(58) only and the session's next outbound number; NextNumIn is not advanced;
                // refuse_logon_with_logout_ gives the endings of a Logout that is not sent, and
                // sets no receipt marker.
                co_return co_await refuse_too_low_logon_(seq, fsm_state::NotConnected);
            } else {
                // Too-high with the knob off, or a drained mutex: session-fatal, silent
                // (I-2/I-4/[FIX-SL §4.1]).
                record_state_transition_(fsm_state::Disconnected);
                co_return fixpp::core::expected_t<void>{};
            }
        } else {
            // Classified in sequence; consumed at the commit below (106 D2).
            // Record this so the commit and the persist guard below can fire correctly.
            // [029 gate-b/r1; INV-H1 fix; triage root-cause #1/#2]
            logon_inbound_advanced = true;
        }

        // T027 FR-017 — ResetSeqNumFlag(141) policy (Clarifications Q1=A).
        // bilateral_strict's refusal of a Logon without 141=Y runs before the
        // reset_on_logon block and check_inbound (103 D2.4, above).
        // bilateral_lenient: if peer sends 141=Y → honour; if not → accept.
        // unilateral: always honour any peer 141=Y.
        // All modes: if peer sends 141=Y → emit session_event_sequence_numbers_reset.
        // [spec.md FR-017; data-model.md §E-4; Clarifications Q1=A]

        // peer_sent_reset: event emission deferred to after the reply-Logon block
        // so the event fires with consistent post-reset counters (FR-018) and the
        // reply Logon is stamped at seq=1 (FR-017). The reset itself has already
        // run above via reset_seqnums_to_one_durable() when reset_on_logon.
    }

    // 106 D1 supersedes 013 T036's and 104 T8's position of the mTLS CompID-authorisation gate
    // (after the sequence check, before 1137): it runs once, right after interpret_logon (above),
    // still before the FSM leaves NotConnected (FR-019/FR-024).

    // 033 T018 — FIXT acceptor: validate DefaultApplVerID(1137) (C4/C5).
    // Gate: only for FIXT sessions AND when the serviceability registry is set.
    // FIX.4.x sessions (is_fixt()==false) pass through byte-identical (INV-FIXT-1).
    // [033 contracts/fixt-logon-establishment.md C4/C5; data-model.md E2; FR-004/FR-004a;
    //  research R2/R7; INV-FIXT-2]
    if (cfg_.is_fixt() && app_version_registry_) {
        // Determine the conformant disposition for the peer's DefaultApplVerID(1137):
        //   (a) absent           → Reject 373=1 RequiredTagMissing  (C4/FR-004; R7)
        //   (c) unserviceable    → Reject 373=5 ValueIsIncorrect    (C5/FR-004a; R2)
        //   (d) serviceable      → record negotiated_appl_version_   (C3/E2; INV-FIXT-2)
        // Both reject arms carry RefTagID(371)=1137 and differ ONLY in 373 — emitted by
        // the single parameterized block below (W2 asserts 373=1; W3 asserts
        // 373=5+371=1137).
        // 099 (data-model E-7's S3, E-10, FR-031): the decision also reads the group's entries.
        auto const appl_version = logon_appl_version_(result->default_appl_ver_id, frame);
        if (appl_version.has_value()) {
            // (d) Set-once (FSM NotConnected gate → once per session).
            negotiated_appl_version_ = *appl_version;
            route_negotiated_ = *appl_version;  // 099 (FR-027): what selection reads
            record_msg_type_defaults_(frame);
        } else {
            // Session-level Reject(35=3, 371=reject_tag, 373=*reject_reason) + Disconnected.
            // toAdmin hook fires before store_then_emit (mirrors the reply-Logon path;
            // [[feedback_admin_emit_bypasses_fire_to_admin]]). As of 036 the shared
            // emit_session_reject_ helper ALSO fires fire_to_admin_ (the 1137 path is kept
            // inline only because it carries the distinct RefTagID=1137/RefMsgType="A"
            // fields — not because the helper is observation-less). [036 FR-008/T033]
            const auto rj_st52 = effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                       cfg_.sending_time_precision)
                                                  : SendingTimeStamp{};
            {
                FIXPP_INBOUND_SPLIT_AWAIT(
                    r, tic,
                    inbound_reject_leaf_(
                        tic, parse_seqnum(hdr.msg_seq_num),
                        appl_version.error().ref_tag,  // 1137, or data-model E-10's
                        "A",                           // RefMsgType = Logon
                        appl_version.error().reason, rj_st52.value, fsm_state::NotConnected));
                if (!r) {
                    co_return r;
                }
            }
            disconnect_unless_superseded_(fsm_state::NotConnected);  // 093 OD-26
            co_return fixpp::core::expected_t<void>{};
        }
    }

    // 106 D1 supersedes 033 T023's position of the credentials gate (after the 1137 block): it
    // runs once, right after the mTLS gate (above), so a refused credential draws no 1137 Reject.

    // 106 D2: the advance is committed here, so every refusal before it keeps NextNumIn (closes
    // L-104-8); the reply's 789 reads the advanced value (F-4). INV-106-1 (data-model E-2) holds
    // while nothing from the arm's entry to here changes next_inbound_ but a reset unit, the knob's
    // reset and hydration; re-derive by listing the calls of set_next_inbound, reset_to(,
    // ensure_hydrated_(, run_reset_unit_(, reset_seqnums_to_one_durable( and hydrate( between the
    // entry and this point. A refused commit (the manager frozen or its lock drained) ends the arm
    // Disconnected unless superseded, silently.
    if (logon_inbound_advanced) {
        if (commit_freeze_hook_) {
            commit_freeze_hook_();
        }
        if (auto const commit = co_await seqnum_mgr_.check_inbound(parse_seqnum(hdr.msg_seq_num));
            !commit) {
            disconnect_unless_superseded_(fsm_state::NotConnected);
            co_return fixpp::core::expected_t<void>{};
        }
    }

    // Valid Logon + in-seq: transition to LogonReceived, then emit
    // the acceptor's own Logon reply and transition to Active.
    // [spec.md FR-005 §US2 AC2; 005 data-model.md's `NotConnected` row; F1 Round-A drift
    // fix] RC#B (gate-b/r1-green): gate the LogonReceived→Active transition on successful
    // reply build AND emit. Build/emit failure → Disconnected. [009 spec.md FR-005; 005
    // data-model.md's `NotConnected` row "reply Logon, agreed HeartBtInt"]
    record_state_transition_(fsm_state::LogonReceived);

    // Emit the acceptor reply Logon using the same admin-builder path
    // as the initiator's open() Logon. [009 spec.md FR-005]
    //
    // 031: the PRE-reply next-outbound (N_pre) the peer's 789 is compared against in
    // the honor below (RC#4 runs honor AFTER the reply consumed a seq). Set from
    // reply_seq inside the block (= peek_outbound() before assign_outbound, post any
    // 141 reset), so it is correct under all paths. [FR-001/FR-007, contract C-031-CS]
    seqnum_t n_pre_outbound = 0;
    {
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

        if (peer_sent_reset && !cfg_.reset_on_logon) {
            // 093 (contract C-6; spec FR-041; fixpp#524) supersedes 030 T010/T011's
            // reset, then restore, then persist, and fixpp#518's in-unit
            // teardown_reset_done_ stop: the 141=Y reset unit. Step 1, the targets
            // (research R-6): next-in 2 when the reset Logon was consumed (a
            // surviving net-advance, 030 T011), else 1; next-out 1, because the
            // reply has not been sent. They go to every store, volatile ones
            // included (plan OD-9).
            const seqnum_t unit_in = logon_inbound_advanced ? seqnum_min + 1 : seqnum_min;
            const seqnum_t unit_out = seqnum_min;
            // Steps 3 and 4; plan OD-25 replaces step 2's shield with the store
            // operation's empty cancellation slot (run_reset_unit_). No return
            // path, so the restore below always runs.
            auto const unit = co_await run_reset_unit_(unit_in, unit_out);
            // Step 5: the restore. A cancellation recorded on this thread while
            // the unit ran is dropped: the state is fresh.
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
            // Step 6: the existing dispositions. A manager error is fatal; a store
            // error is fatal when the store is persistent (030 T010, FR-010) and
            // logged otherwise (024 I-07).
            // Plan OD-26: each write follows the unit's store operation.
            if (!unit.manager) {
                disconnect_unless_superseded_(fsm_state::LogonReceived);
                co_return std::unexpected(unit.manager.error());
            }
            if (!unit.store && store_is_persistent_) {
                disconnect_unless_superseded_(fsm_state::LogonReceived);
                co_return std::unexpected(unit.store.error());
            }
        }
        // Step 7 (093 contract C-6) and fixpp#518: a close() may have begun, or
        // Engine::stop()'s step 1 reached this strand, while the unit's store
        // operation yielded.
        if (logon_arm_superseded_(fsm_state::LogonReceived)) {
            co_return fixpp::core::expected_t<void>{};
        }

        // FR-018: emit the reset event once, after post-reset state is consistent,
        // when any reset happened (knob-driven OR received-141). by_peer_request
        // reflects whether the peer requested it via 141=Y.
        if (cfg_.reset_on_logon || peer_sent_reset) {
            emit_event(fixpp::session::session_event_sequence_numbers_reset{.by_peer_request =
                                                                                peer_sent_reset});
        }

        // fixpp#544 §2.2 class 2: the reply Logon's build and emit run in a leaf that owns the
        // reply buffer.
        {
            FIXPP_INBOUND_SPLIT_AWAIT(
                r, tic,
                inbound_acceptor_reply_logon_leaf_(tic, peer_sent_reset, heartbt_sec,
                                                   reply_sending_time_view, n_pre_outbound));
            if (!r) {
                co_return r;
            }
        }
        // fixpp#518: a close() may have run during the reply write.
        if (logon_arm_superseded_(fsm_state::LogonReceived)) {
            co_return fixpp::core::expected_t<void>{};
        }
    }

    // 027 T014/T021 — acceptor 789 honor (RC#4 ordering: AFTER reply store_then_emit).
    // Delegates to honor_peer_next_expected_() — single implementation.
    // [contract C4/C6, data-model I-NEX-2/3/4/9/11, D-6/D-10]
    if (cfg_.enable_next_expected_msg_seq_num && peer_789_present) {
        // 031: compare against the PRE-reply outbound (n_pre_outbound), NOT the live
        // post-reply peek_outbound() — the reply Logon already consumed a seq here.
        auto h789 = co_await honor_peer_next_expected_(peer_789_raw, peer_789_present,
                                                       n_pre_outbound, fsm_state::LogonReceived);
        if (!h789) co_return std::unexpected(h789.error());
        if (*h789 != logon_789_outcome::in_sync_continue) {
            co_return fixpp::core::expected_t<void>{};
        }
    }

    // fixpp#518: a close() may have run during the 789 honour's writes.
    if (logon_arm_superseded_(fsm_state::LogonReceived)) {
        co_return fixpp::core::expected_t<void>{};
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
        (void)co_await close(fixpp::session::close_mode::terminal);
        co_return std::unexpected(fixpp::core::error::app_callback_threw);
    }
    // 029 T010 / gate-b/r1 — PERSIST: acceptor Logon in-seq → durable advance.
    // Guard: only when check_inbound net-advanced the manager AND no reset rewound
    // the counter (INV-H1 fix, triage root-cause #1/#2):
    //   logon_inbound_advanced false → behind-side tolerance path (chk failure,
    //     manager stayed at X) → skip; store stays X, INV-H1 holds.
    //   peer_sent_reset true → reset_seqnums_to_one_durable already set
    //     store=manager=1; an additional +1 would put store ahead → skip.
    //   reset_on_logon true → knob-driven reset ran before check_inbound, then
    //     check_inbound advanced 1→2, reset_seqnums_to_one ran BEFORE check_inbound
    //     so the advance was real — BUT reset_on_logon is excluded here as an
    //     INV-H1-safe under-persist: store lags by ≤1, re-delivers at-least-once.
    //   All three false → normal in-seq Logon: persist fires → store==manager. ✓
    // [029 INV-H1; triage root-cause #1/#2; contracts C3.1; data-model §Persist matrix]
    if (logon_inbound_advanced && !peer_sent_reset && !cfg_.reset_on_logon) {
        auto p_r = co_await persist_inbound_advance_(fsm_state::Active);  // 093 OD-26
        if (!p_r) co_return std::unexpected(p_r.error());
    }

    // fixpp#518: a close() may have run while the persist yielded, including one
    // posted from onLogon.
    if (logon_arm_superseded_(fsm_state::Active)) {
        co_return fixpp::core::expected_t<void>{};
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

// LogonReceived/Active arm. It also receives every frame for a cold state and hands it to
// on_inbound_cold_ after reading fsm_state_. A rarely-taken branch records its sub-arm in
// `slow` and leaves the one-pass loop; the sub-arm is awaited once, after the loop. That keeps
// one b3 boundary for all of them, so the frame cycled on an in-sequence Heartbeat or
// application message holds only what that path uses (§2.2 target; the gate is W-A/W-B).
// The loop body contains no loop or switch of its own, so each `break` leaves the loop.
//
// Ruling R-14 (`.specify/536-537-538-recovery-liveness.md` §9, D-CAN-1 to D-CAN-5): the
// whole body is one `try`, and this is the one coroutine every inbound frame enters, in
// every state. It supersedes the route the cells of fixpp#544's b3 ruling pinned as a throw
// out of the frame: an exception a co_await of the inbound path throws (asio's
// operation_aborted, when a cancellation is already recorded on the thread; or anything
// else) ends the session here, and on_inbound_frame returns a value. The handler only notes
// which kind it caught, because a handler may not co_await; the statements after it are
// reached only from the handler, since every path inside the `try` returns.
// NOLINTNEXTLINE(readability-function-size,hicpp-function-size)
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_(
    std::span<const std::byte> frame) noexcept {
    // 103 (research D1 step 3): the inbound frame is counted for its whole processing, an
    // exception and the frame's destruction included; it is this coroutine's first
    // statement, ahead of the try.
    inbound_depth_holder const inbound_depth{*this};
    // D-CAN-5: the state the frame entered in. The last awaits of a Logon arm run with the
    // session already Active, so "a Logon arm" is told from this value.
    const fsm_state entry_state = fsm_state_;
    // D-CAN-5's predicate, "superseded": the frame entered a Logon arm, and a close() is in
    // flight or has completed, or Engine::stop() has set the engine-stop flag. It differs
    // from logon_arm_superseded_ in that it holds after a completed close and does not test
    // the state the arm expects, which the entry does not hold.
    static constexpr auto superseded = [](const Session& s, fsm_state entered) noexcept {
        return (entered == fsm_state::NotConnected || entered == fsm_state::LogonSent) &&
               (s.state_ == lifecycle::closing || s.state_ == lifecycle::closed_drained ||
                s.engine_stop_requested_);
    };
    bool caught_cancellation = false;
    try {
        // 099-fixt-appl-version-validation (data-model E-7's step 1): ahead of the state test,
        // since this is the one way into every arm, and ahead of any await.
        if (fixt_routes_unbound_()) {
            co_return fixpp::core::expected_t<void>{};
        }
        // 103 (research D4.6's last row): while the bound acknowledges a too-high Logout,
        // inbound frames are drained, as the LogoutSent row drains. One flag read per frame.
        if (peer_logout_.phase == peer_logout_mark::phase_t::acking) {
            co_return fixpp::core::expected_t<void>{};
        }
        // 093-inbound-frame-dispositions (FR-013, contract C-2) supersedes 070's S-030
        // check here, which disconnected an Active session whose inbound frame exceeded
        // the advertised MaxMessageSize(383) and exempted every frame before Active. The
        // Framer now refuses a frame over L (the advertised 383 when set) in every state,
        // below this function, and the read pump closes the session terminally.
        // The saved throw_if_cancelled value each FIXPP_INBOUND_SPLIT_AWAIT boundary passes on.
        bool tic = false;
        if (fsm_state_ != fsm_state::LogonReceived && fsm_state_ != fsm_state::Active) {
            FIXPP_INBOUND_SPLIT_AWAIT(r, tic, on_inbound_cold_(tic, frame));
            // D-CAN-5, the returned timing: a superseded Logon arm whose store call a
            // cancellation ended has recorded nothing, and returns success, as it does on
            // the thrown timing below.
            if (!r && r.error() == fixpp::core::error::store_cancelled &&
                superseded(*this, entry_state)) {
                co_return fixpp::core::expected_t<void>{};
            }
            co_return r;
        }
        // T056 (US5) guard-precedence ordering:
        // (1) parse/type recognised → else session Reject (no-loop-guard)
        // (2) CompID/BeginString gate
        // (3) SendingTime MaxLatency (Q3)
        // (4) seqnum class
        // (5) message-type-for-state

        // 099-fixt-appl-version-validation (data-model E-7, research R-7): the frame's route,
        // selected past the state test above, in this frame, which outlives every sub-arm it
        // awaits.
        inbound_route const route = select_route_(frame, /*establishment_arm=*/false);
        auto hdr = scan_frame_header(frame, session_hooks(*route.entry.owner));
        inbound_slow_t slow{};
        do {
            // 093 (contract C-2 step 1): a frame whose third field is not MsgType(35),
            // faulty or not, is disregarded as garbled: counted, evented and logged, with
            // no Reject, NextNumIn kept and no liveness refresh. It runs before 092's
            // fault branch, superseding 092's D-8, and D-1/D-2 for this shape.
            if (!hdr.msg_type_is_third) {
                note_garbles_(header_out_of_order_garble(frame));
                co_return fixpp::core::expected_t<void>{};
            }
            // 092 (contract C-1 step 3): a frame the scan could not read goes to C-2
            // rows D-7 … D-6 and nothing below runs. The MaxMessageSize(383) guard
            // above the state switch has already run (C-1 step 1b).
            if (hdr.fault != fixpp::wire::field_fault::none) {
                co_return co_await dispose_unparseable_(hdr, fsm_state_);
            }

            // 093 (contract C-2 step 4, C-5; spec FR-020): the one per-frame inbound
            // liveness writer. Every frame past the two checks above is neither garbled
            // nor faulty and proves the peer is alive, so it refreshes before the validate
            // gate and every early return below. It supersedes the Active-only writer
            // that sat at the end of this arm. A refresh in LogonReceived is harmless: the
            // liveness loop runs only in Active, and both roles seed the value on
            // entering it. The null check matches open()'s seed: a session may run
            // without a clock.
            if (effective_clock_) {
                last_inbound_steady_ = effective_clock_->steady_now();
            }

            // ── 041-validation-gate-wiring T014: dictionary-driven validate gate ─
            // Runs after scan_frame_header (hdr.msg_type available for the 35=3, 35=5 exemption)
            // and BEFORE check_inbound. Erratum fixpp#423: an in-sequence rejected message
            // consumes its MsgSeqNum; 041 C-3's "seqnum NOT advanced" holds only out of
            // sequence. #423's row 4 ("garbled (unparseable) → no Reject, no advance") is
            // superseded by 092 contract C-2 (fixpp#507; research R-13; the owner
            // ruling of 2026-09-27 revising row 4). claim-ok: the date names the ruling
            // A frame the scan cannot read never reaches this gate.
            // No-reject-loop: 35=3 and 35=5 exempt (FR-004), for a well-formed frame
            // only; a faulty Reject or Logout is Rejected under C-2. [041 T014; E-4]
            // Buffer: the session's parse buffer B(L), as dispatch (093).
            if (cfg_.validate_inbound_messages && session_route_.validator) {
                // 099 (data-model E-7's A1, A2), widened by 101 D5 to every validating session:
                // ahead of the exemption of Reject and Logout and of the validator, so a frame with
                // two MsgTypes is refused whichever its first 35 is.
                InboundValidation v = refuse_active_frame_(frame, hdr, route);
                if (v.outcome == validate_outcome::pass && hdr.msg_type != "3" &&
                    hdr.msg_type != "5") {
                    v = validate_inbound_(frame, hdr, route);
                }
                if (v.outcome != validate_outcome::pass) {
                    slow.k = inbound_slow_t::kind::validate_failed;
                    slow.v = v;
                    break;
                }
            }

            // ── Guard (2): CompID/BeginString gate (scenarios 2i/2k) ──────────
            // BeginString(8) mismatch → always session-fatal → Disconnected.
            // SenderCompID(49)/TargetCompID(56) mismatch → fatal ONLY when
            // cfg_.check_comp_id is true (default); skipped when false
            // (028 T005 / S1, data-model S1, contract C1, FR-003).
            // Steady-state only: Logon-establishment CompID check (interpret_logon
            // at NotConnected) is untouched (FR-012 / I-VCT-6).
            // 105 (research D2 item 4): the peer-frame silent endings go through
            // end_session_silently_, absorbed while an ending is in progress (C-2 W-8).
            if (hdr.begin_string != cfg_.begin_string) {
                end_session_silently_();
                co_return fixpp::core::expected_t<void>{};
            }
            if (cfg_.check_comp_id && (hdr.sender_comp_id != cfg_.target_comp_id ||
                                       hdr.target_comp_id != cfg_.sender_comp_id)) {
                end_session_silently_();
                co_return fixpp::core::expected_t<void>{};
            }

            // 105 (research D2 item 3; C-2 W-1, C-2.9; clarification D-a): the response
            // test. While a wait armed after Active is open, a well-formed Logout past the
            // identity checks is the peer's response whatever its MsgSeqNum, ahead of the
            // Reset-mode SequenceReset bypass, MsgSeqNum 0, the too-high branch, the PossDup
            // stage-1 arm and check_inbound. The sub-arm decides its route (at NextNumIn the
            // in-sequence one), so this arm awaits nothing here; the one-byte phase is tested
            // first.
            if (ending_wait_.phase == ending_wait_state::phase_t::armed && hdr.msg_type == "5" &&
                ending_wait_.from_active()) {
                slow.k = inbound_slow_t::kind::ending_response;
                break;
            }

            // ── Guard (3): SendingTime MaxLatency (Q3, T055/T056) ─────────────
            // Check |inbound_sending_time − effective_now| ≤ MaxLatency (D-8: 120 s).
            // No-reject-loop guard: Reject(35=3) and Logout(35=5) are exempt per I-5.
            // Only a well-formed frame reaches this guard; a faulty one was disposed
            // of above under 092 contract C-2.
            // Established session: Reject(reason=10, refTag=52) → Logout → Disconnect.
            // FR-007: missing SendingTime (empty) → Reject-Logout-Disconnect.
            // FR-008: malformed SendingTime (parse failure) → Reject-Logout-Disconnect.
            // T016 [US3] RC#5: both empty AND parse-failure fall through to the path.
            // Note: the Logon-path special case (D-3) is handled in LogonSent below.
            if (hdr.msg_type != "3" && hdr.msg_type != "5" && effective_clock_) {
                if (!inbound_sending_time_ok_(hdr)) {
                    slow.k = inbound_slow_t::kind::sending_time_reject;
                    break;
                }
            }

            // ── Inbound SequenceReset(35=4) — Reset mode (GapFillFlag ≠ Y) ───
            // FIX-SL §4.8.6: a Reset-mode SequenceReset is processed REGARDLESS
            // of its own MsgSeqNum — it bypasses the seqnum gate (its purpose as
            // an admin recovery tool). Mirrors QuickFIX verify(msg, false, false).
            // GapFill mode (123=Y) is handled AFTER the gate (it IS subject to
            // ordering). [S-023; QuickFIX Session::nextSequenceReset]
            if (hdr.msg_type == "4" && hdr.gap_fill_flag != "Y") {
                slow.k = inbound_slow_t::kind::sequence_reset;
                break;
            }

            // ── Guard (4): seqnum check (T035 / 013 T026 AwaitingResend) ─────
            {
                const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
                if (seq == 0) {
                    // Cannot parse seq — session-fatal (105: a peer-frame silent ending).
                    end_session_silently_();
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
                // 028 T008 (S2): guard on validate_sequence_numbers.
                // When the knob is off, skip AwaitingResend/ResendRequest entirely
                // and fall through to S4 (deliver-without-advance).
                // [data-model S2, contract C2.2, FR-006, research D-3]
                if (seq > next_expected && !reconnect_fsm_.is_awaiting_resend() &&
                    cfg_.validate_sequence_numbers) {
                    slow.k = inbound_slow_t::kind::too_high;
                    slow.next_expected = next_expected;
                    slow.seq = seq;
                    break;
                }

                // 021 T008 Stage-1 — PossDup OrigSendingTime validation (Arms C/D/E).
                // Runs for any 43=Y non-SequenceReset frame, AFTER the too-high arm
                // (forward gaps still ResendRequest per engine parity — user decision
                // 2026-06-04) and BEFORE check_inbound. claim-ok: the date names the decision
                // Erratum fixpp#423: an at-expected rejected dup consumes its MsgSeqNum (Arms
                // C/D and RC#1 below); 021 FR-004's "MUST NOT advance" holds only for a
                // too-low one.
                // data-model.md §1 Stage 1 rows 0–4; contracts/session-possdup.md C1.
                // Arm E (row 0): 35=4 (SequenceReset) is exempt — guard below.
                if (hdr.poss_dup_flag == "Y" && hdr.msg_type != "4") {
                    {
                        FIXPP_INBOUND_SPLIT_AWAIT(r, tic, on_inbound_active_poss_dup_(tic, hdr));
                        if (r.has_value()) {
                            co_return *r;
                        }
                    }
                }
                // Stage-1 passed (or not a 43=Y non-35=4 frame). Proceed to check_inbound.

                // In sequence → advance. Any refusal goes to
                // on_inbound_active_out_of_sequence_, which has the dispositions:
                // too low without PossDup is session-fatal (not recoverable per I-4),
                // and a frame above NextNumIn while a resend is outstanding does not
                // advance and is ignored (fixpp#537, D-537-1 and D-537-2), a
                // ResendRequest and a Logout excepted (103, D4.1).
                auto chk = co_await seqnum_mgr_.check_inbound(seq);
                if (!chk) {
                    slow.k = inbound_slow_t::kind::out_of_sequence;
                    slow.chk_error = chk.error();
                    break;
                }

                // Gap close check: if we filled through the gap endpoint, exit AwaitingResend.
                note_inbound_advance_();
            }

            // ── Inbound SequenceReset(35=4) — GapFill mode (GapFillFlag = Y) ─
            // Subject to seqnum ordering: Guard 4 above advanced the counter by
            // 1 for the in-seq GapFill (MsgSeqNum = gap start); now jump it to
            // NewSeqNo(36) to skip the filled span and exit AwaitingResend.
            // Reset mode was handled before Guard 4. [S-023]
            if (hdr.msg_type == "4") {  // GapFillFlag == "Y" (Reset handled before gate)
                slow.k = inbound_slow_t::kind::gap_fill;
                break;
            }

            // T046 (US4): inbound Logout in Active/LogonReceived state.
            // Per data-model.md matrix:
            //   Active row:        inbound Logout → emit Logout, → Disconnected.
            //   LogonReceived row: inbound Logout → Disconnected ([FIX-SL §4.6]).
            // T019: emit session_event_sequence_numbers_reset{by_peer_request=false}
            //   when Active receives peer Logout (peer-initiated clean termination
            //   implies seqnum context is being reset at next Logon). [spec FR-018]
            if (hdr.msg_type == "5") {  // Logout (35=5)
                slow.k = inbound_slow_t::kind::logout;
                break;
            }

            // I-5: a well-formed inbound Reject(35=3) is logged and accepted; never
            // re-rejected. A faulty Reject never reaches here: 092 contract C-2
            // disposes of it above, and can Reject it.
            if (hdr.msg_type == "3") {  // Reject (35=3)
                // Active row: session-level log, no Reject-of-a-Reject (I-5).
                // 029 T010 — PERSIST: check_inbound advanced next_inbound for the
                // inbound Reject. No fromAdmin dispatch (I-5). Persist before return.
                // [029 tasks T010; contracts C3.1; data-model §Persist matrix Reject site]
                {
                    auto p_r = co_await persist_inbound_advance_();
                    if (!p_r) co_return std::unexpected(p_r.error());
                }
                // Remain in current state.
                co_return fixpp::core::expected_t<void>{};
            }

            // T041 (US3): in the Active state, handle liveness-specific message types
            // (Heartbeat / TestRequest). Inbound liveness was refreshed at the top of
            // this arm (093).
            if (fsm_state_ == fsm_state::Active) {
                // ── 019 T011: fromAdmin dispatch for admin-typed messages ────
                // Called here (top of Active-only handling) for ALL admin MsgTypes
                // that reach this point after FSM + seqnum validation.
                // Note: Logout(35=5) and SequenceReset(35=4) return early ABOVE
                // this block; they are NOT dispatched to fromAdmin in this slice
                // (deferred to US3/US2 wiring). Heartbeat/TestRequest/ResendRequest/
                // Reject are dispatched here.
                // [research D3/D4; FR-004; INV-6]
                if (engine_.application != nullptr && detail::is_admin_msgtype(hdr.msg_type)) {
                    // T019 note: parse_and_dispatch_'s callback_dispatch_scope ends
                    // before it returns, so the depth of all callbacks is back to its
                    // entry value when record_state_transition_ below enters onLogout.
                    auto cb_r = parse_and_dispatch_(
                        frame, inbound_parse_buffer, route, [&](auto& mv, auto& sid) {
                            return engine_.application->fromAdmin(mv, sid);
                        });
                    // 092 contract C-6: a late parse failure closes the session.
                    if (!cb_r || *cb_r == dispatch_outcome::parse_failed) {
                        slow.k = inbound_slow_t::kind::from_admin_failed;
                        slow.cb_r = cb_r;
                        break;
                    }
                }

                // T041 US3 / T018-D: Active row — inbound Heartbeat → liveness.
                // If we had an outstanding TestRequest:
                //   - inbound Heartbeat TestReqID matches ours → clear (TR answered)
                //   - inbound Heartbeat TestReqID does NOT match ours → mismatch →
                //     session_testreqid_mismatch(118) → Disconnected
                //     [spec.md FR-006; data-model.md §E-1; T018-D]
                // Inbound Heartbeat (35=0): per FIX a Heartbeat is NEVER answered
                // (005 data-model.md's `Active` row → "advance counter (liveness)", no
                // emit). If it carries a TestReqID matching our outstanding
                // TestRequest it answers that TR (clear the pending flag); a
                // mismatched TestReqID is session_testreqid_mismatch(118) →
                // Disconnected (FR-006). No outbound frame is emitted in response —
                // the retired "T020-A echo" emitted a Heartbeat here, which storms
                // at RTT cadence when two fixpp sessions are paired (each echoes
                // the other's beat). [self-paired heartbeat-storm fix]
                if (hdr.msg_type == "0") {  // Heartbeat (35=0)
                    // With a TestRequest outstanding: 105 (ruling D-d; contract C-5.4)
                    // supersedes the plain comparison: an earlier TestRequest's id neither
                    // answers nor ends; an id this connection never sent is
                    // session_testreqid_mismatch(118), a peer-frame silent ending, after which
                    // answer_pending_test_request_ returns false [spec.md FR-006; T018-D].
                    if (!pending_test_req_id_.empty() &&
                        !answer_pending_test_request_(hdr.test_req_id)) {
                        co_return fixpp::core::expected_t<void>{};
                    }
                    // 029 T010 — PERSIST: inbound Heartbeat (35=0).
                    // check_inbound advanced next_inbound; fromAdmin dispatched above.
                    // [029 tasks T010; contracts C3.1; data-model §Persist matrix Heartbeat]
                    {
                        auto p_r = co_await persist_inbound_advance_();
                        if (!p_r) co_return std::unexpected(p_r.error());
                    }
                    // Remain in Active — liveness tick.
                    co_return fixpp::core::expected_t<void>{};
                }

                // T041 US3 / T042 retirement (US4): Active row — inbound
                // TestRequest (35=1) → emit Heartbeat echoing TestReqID(112).
                if (hdr.msg_type == "1") {  // TestRequest (35=1)
                    slow.k = inbound_slow_t::kind::test_request;
                    break;
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
                    slow.k = inbound_slow_t::kind::resend_request;
                    break;
                }

                // ── Guard (5): message-type-for-state (T056 US5) ─────────────
                // Session admin types silently passed-through in Active: 0/1/3/5
                // (Heartbeat / TestRequest / Reject / Logout — handled above OR
                // dispatched via the in-seq path). Any other MsgType in Active →
                // session-level Reject(35=3) with SessionRejectReason and RefMsgType.
                // Session stays Active. No-reject-loop: guard (type == "3" || type
                // == "5") exempted above, for a well-formed frame (a faulty one is
                // disposed of under 092 contract C-2 before any guard).
                //
                // 010 F4 / W3.3-final fix (codex + QuickFIX-cpp + QuickFIX/J survey
                // 2026-05-23). claim-ok: the date names the survey
                // "A" (dup-Logon) IS NOT in is_session_admin — per 005
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
                            slow.k = inbound_slow_t::kind::unsupported;
                            break;
                        }
                        // else: Application registered → falls through to fromApp dispatch below.
                    }
                }

                // ── 019 T011: fromApp dispatch for app-typed messages ─────────
                // Reached ONLY when engine_.application != nullptr (guard above
                // lets app messages fall through only if Application is registered).
                // Admin messages are dispatched via fromAdmin at the top of the
                // Active block and return early; they never reach here.
                // FR-003; research D3/D4/D8; [const §VIII.5] (no heap: the parse buffer
                // open() allocated, 093).
                if (engine_.application != nullptr) {
                    // T019 note: parse_and_dispatch_'s callback_dispatch_scope ends
                    // before it returns, so the depth of all callbacks is back to its
                    // entry value. (FR-003; research D3/D4/D8; [const §VIII.5])
                    auto cb_r = parse_and_dispatch_(
                        frame, inbound_parse_buffer, route,
                        [&](auto& mv, auto& sid) { return engine_.application->fromApp(mv, sid); });
                    // 092 contract C-6: a late parse failure closes the session.
                    if (!cb_r || *cb_r == dispatch_outcome::parse_failed) {
                        slow.k = inbound_slow_t::kind::from_app_failed;
                        slow.cb_r = cb_r;
                        break;
                    }
                }
            }

            // 029 T010 — PERSIST: in-sequence app message (and resend-fill PossDup in-seq).
            // check_inbound advanced next_inbound; fromApp/fromAdmin dispatched above.
            // This also covers resend-fill replayed in-seq app (PossDup arriving in-sequence)
            // — those advance the counter like any in-seq message (C3.1 / New-2).
            // [029 tasks T010; contracts C3.1; data-model §Persist matrix in-seq app + resend-fill]
            {
                auto p_r = co_await persist_inbound_advance_();
                if (!p_r) co_return std::unexpected(p_r.error());
            }

            // In-sequence: counter advanced. Remain in current state.
            co_return fixpp::core::expected_t<void>{};
        } while (false);
        FIXPP_INBOUND_SPLIT_AWAIT(r, tic, on_inbound_active_slow_(tic, frame, hdr, slow, route));
        co_return r;
    } catch (const asio::system_error& e) {
        caught_cancellation = e.code() == asio::error::operation_aborted;
    } catch (...) {  // NOLINT(bugprone-empty-catch): noted as "not a cancellation" above
    }
    // D-CAN-3: the thread is left as the returned timing leaves it, with no cancellation
    // recorded and asio's default filter, which is what a lock sequence leaves after a
    // cancelled wait. This await is never pre-checked, so it cannot throw.
    co_await asio::this_coro::reset_cancellation_state();
    // D-CAN-5, the thrown timing: a superseded Logon arm records nothing; the close() or
    // the Engine::stop() that superseded it ends the session.
    const bool arm_superseded = superseded(*this, entry_state);
    if (!arm_superseded && fsm_state_ != fsm_state::Disconnected) {
        record_state_transition_(fsm_state::Disconnected);
    }
    if (caught_cancellation) {
        if (arm_superseded) {
            co_return fixpp::core::expected_t<void>{};
        }
        // D-CAN-4: the code the returned timing of a cancelled store call returns.
        co_return std::unexpected(fixpp::core::error::store_cancelled);
    }
    // D-CAN-2: any other exception, superseded or not.
    co_return std::unexpected(fixpp::core::error::dispatch_aborted);
}

// The sub-arm the Active arm's `slow` names. A non-coroutine: the Active arm's one
// FIXPP_INBOUND_SPLIT_AWAIT awaits the sub-arm it returns.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_slow_(
    bool tic, std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
    inbound_slow_t const& slow, inbound_route const& route) noexcept {
    using kind = inbound_slow_t::kind;
    switch (slow.k) {
        case kind::validate_failed:
            return on_inbound_active_validate_failed_(tic, hdr, slow.v);
        case kind::sending_time_reject:
            return on_inbound_active_sending_time_reject_(tic, hdr);
        case kind::sequence_reset:
            return on_inbound_active_sequence_reset_(tic, frame, hdr, route);
        case kind::too_high:
            return on_inbound_active_too_high_(tic, hdr, slow.next_expected, slow.seq);
        case kind::out_of_sequence:
            return on_inbound_active_out_of_sequence_(tic, frame, hdr, slow.chk_error, route);
        case kind::gap_fill:
            return on_inbound_active_gap_fill_(tic, frame, hdr, route);
        case kind::logout:
            return on_inbound_active_logout_(tic, frame, hdr, route);
        case kind::ending_response:
            return on_inbound_ending_response_(tic, frame, hdr, route);
        case kind::from_admin_failed:
            return on_inbound_active_from_admin_failed_(tic, hdr, slow.cb_r);
        case kind::test_request:
            return on_inbound_active_test_request_(tic, hdr);
        case kind::resend_request:
            return on_inbound_active_resend_request_(tic, hdr);
        case kind::unsupported:
            return on_inbound_active_unsupported_(tic, hdr);
        case kind::from_app_failed:
            return on_inbound_active_from_app_failed_(tic, hdr, slow.cb_r);
        case kind::none:
            break;
    }
    // Unreachable: the Active arm leaves its loop only after naming a sub-arm.
    return on_inbound_drained_(tic);
}

// The Active arm's SendingTime(52) MaxLatency check (Q3, T055/T056): present AND parseable
// AND in range. Synchronous.
bool Session::inbound_sending_time_ok_(
    fixpp::session::detail::FrameHeader const& hdr) const noexcept {
    // Determine if SendingTime is valid: present AND parseable AND in-range.
    bool sending_time_ok = false;
    if (!hdr.sending_time.empty()) {
        auto parse_r = fixpp::core::fix_string_to_utc_time(
            std::span<const char>{hdr.sending_time.data(), hdr.sending_time.size()});
        if (parse_r) {
            const auto max_lat =
                cfg_.sending_time_threshold.has_value()
                    ? std::chrono::duration_cast<std::chrono::seconds>(*cfg_.sending_time_threshold)
                    : std::chrono::seconds{120};  // D-8 default 120 s
            auto chk_st =
                fixpp::session::check_sending_time(*parse_r, effective_clock_->now(), max_lat);
            sending_time_ok = chk_st.has_value();
        }
        // parse failure: !parse_r → sending_time_ok stays false → path fires.
    }
    // sending_time absent (empty) → sending_time_ok stays false → path fires.
    return sending_time_ok;
}

// fixpp#537 (`.specify/536-537-538-recovery-liveness.md` §4.3, D-537-5; §4.7): the deadline
// of an unfilled resend gap. Called where a frame can leave the Active arm without ending
// the session; the three tests are here and not at the call sites, so that a call site
// cannot fire on a frame that advances NextNumIn. 103 (research D6, fixpp#581) supersedes
// ruling R-11's "above NextNumIn": the deadline acts on every frame that does not advance
// NextNumIn. Returns nullopt when the deadline
// has not expired, and the caller goes on. On expiry it sends a Logout whose Text(58)
// names the unanswered resend and the expected MsgSeqNum, through send_ending_logout_, and
// returns the result its caller returns at once: success, or the Logout leaf's error, which
// the leaf has already recorded Disconnected for. 105-transport-liveness (R-2, contract C-2)
// supersedes ruling R-12's "records Disconnected whether or not that Logout could be built":
// a transmitted Logout arms the wait and the session stays Active until the peer's Logout or
// the bound, unless the frame at the deadline is itself the peer's Logout (T143c), and one
// that was not transmitted records Disconnected as before. While a wait is armed the deadline
// does not fire (W-7). No handler stands around the leaf: a cancelled store call
// of the Logout ends the frame at on_inbound_active_'s catch (§10, D-OUT-2 and D-OUT-4).
asio::awaitable<std::optional<fixpp::core::expected_t<void>>> Session::resend_gap_deadline_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 105 (C-2 W-7): while the wait is armed the deadline does not fire; the wait's own ending
    // is already running.
    if (ending_wait_.phase == ending_wait_state::phase_t::armed) {
        co_return std::nullopt;
    }
    // 1: the frame will not advance NextNumIn. Every call site runs before the frame's
    // consume, so "advances" is consume_rejected_seqnum_'s rule: at NextNumIn, and not a
    // Logon or a SequenceReset. A MsgSeqNum(34) that does not parse reads as 0.
    const seqnum_t next_expected = seqnum_mgr_.next_inbound_unsafe();
    if (parse_seqnum(hdr.msg_seq_num) == next_expected && hdr.msg_type != "A" &&
        hdr.msg_type != "4") {
        co_return std::nullopt;
    }
    // 2: a resend is outstanding.
    if (!reconnect_fsm_.is_awaiting_resend()) {
        co_return std::nullopt;
    }
    // 3: the bound is on, and the clock is at or past it.
    if (!effective_clock_) {
        co_return std::nullopt;
    }
    const auto deadline =
        detail::resend_gap_deadline(reconnect_fsm_.current_resend_state().started_at,
                                    cfg_.heartbeat_interval.value_or(kDefaultHeartBtInt));
    if (!deadline || effective_clock_->steady_now() < *deadline) {
        co_return std::nullopt;
    }

    // The text is built in this frame, which outlives the leaf's await.
    static constexpr std::string_view kPhrase = "ResendRequest unanswered, expecting MsgSeqNum ";
    std::array<char, 64> text_buf{};
    static_assert(kPhrase.size() + std::numeric_limits<seqnum_t>::digits10 + 1 <= text_buf.size());
    char* const digits = std::ranges::copy(kPhrase, text_buf.data()).out;
    const char* const text_end =
        std::to_chars(digits, text_buf.data() + text_buf.size(), next_expected).ptr;
    const std::string_view text{text_buf.data(),
                                static_cast<std::size_t>(text_end - text_buf.data())};
    const auto st52 = stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
    // 105 (research D2 item 1): an R-2 ending, through send_ending_logout_, which claims a
    // pending mark (103 research D4.6) and records Disconnected unless the wait was armed; the
    // frame at the deadline decides the kind there (T143c).
    FIXPP_INBOUND_SPLIT_AWAIT(r, tic, send_ending_logout_(tic, hdr, st52.value, text));
    co_return r;
}

// fixpp#537 (D-537-6, D-537-8): the in-sequence TestReqID(112) disposition, for a Heartbeat
// that does not advance NextNumIn. 105 (ruling D-d) supersedes "another one ends the
// session": an empty or matching TestReqID answers the pending TestRequest, an earlier one of
// this connection does neither, and any other ends the session silently, as in sequence, and
// false is returned. Whether a
// TestRequest is pending is each caller's test, not this routine's.
bool Session::answer_pending_test_request_(std::string_view test_req_id) noexcept {
    switch (classify_test_req_id_(test_req_id)) {
        case test_req_id_class::mismatch:
            // 105 (research D2 item 4): a peer-frame silent ending.
            end_session_silently_();
            return false;
        case test_req_id_class::answers:
            pending_test_req_id_.clear();
            return true;
        case test_req_id_class::earlier:
            // 105 (ruling D-d; contract C-5.4): an earlier TestRequest's id neither answers the
            // pending one nor ends the session.
            return true;
    }
    return true;
}

Session::test_req_id_class Session::classify_test_req_id_(
    std::string_view test_req_id) const noexcept {
    if (test_req_id.empty() || test_req_id == pending_test_req_id_) {
        return test_req_id_class::answers;
    }
    if (detail::is_earlier_test_req_id(test_req_id, test_request_id_floor_,
                                       next_test_request_id_)) {
        return test_req_id_class::earlier;
    }
    return test_req_id_class::mismatch;
}

// fixpp#544 §2.2: the Active arm's sub-arms. Each is a branch of the arm moved verbatim
// behind a FIXPP_INBOUND_SPLIT_AWAIT boundary; `hdr` and `frame` view the arm's storage.

// validate_inbound_ did not pass: a late parse failure, or a Reject.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_validate_failed_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr, InboundValidation v) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 092 contract C-6: a late parse failure closes the session; the
    // number is not consumed.
    if (v.outcome == validate_outcome::parse_failed) {
        co_return co_await close_on_late_parse_failure_();
    }
    if (v.outcome == validate_outcome::reject) {
        // fixpp#537 (D-537-5): when the gap's deadline fires here, no consume and no Reject.
        {
            FIXPP_INBOUND_SPLIT_AWAIT(expired, tic, resend_gap_deadline_(tic, hdr));
            if (expired) {
                co_return *expired;
            }
        }
        // 099 (data-model E-7): a refuse_active_frame_ refusal names the frame by its third field
        // and first 34 (101 D5: on every validating session).
        bool const named = v.reject.names_third_field;
        const seqnum_t rej_seq = parse_seqnum(named ? hdr.fault_ref_seq_num : hdr.msg_seq_num);
        std::string_view const rej_type = named ? hdr.fault_ref_msg_type : hdr.msg_type;
        if (auto c = co_await consume_rejected_seqnum_(rej_seq, rej_type); !c) {
            co_return c;
        }
        co_return co_await emit_session_reject_(rej_seq, rej_type, v.reject.reason,
                                                v.reject.ref_tag_id,
                                                session_reject_reason_text(v.reject.reason));
    }
    co_return fixpp::core::expected_t<void>{};
}

// The SendingTime(52) guard fired: Reject(reason=10, refTag=52) → Logout → Disconnect.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_sending_time_reject_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // Q3 established-session path: Reject(reason=10, refTag=52) → Logout →
    // Disconnect. fixpp#423: an in-sequence message still consumes its
    // MsgSeqNum, so a reconnect does not ask for it again.
    const seqnum_t ref_seq = parse_seqnum(hdr.msg_seq_num);
    if (auto c = co_await consume_rejected_seqnum_(ref_seq, hdr.msg_type); !c) {
        co_return c;
    }
    const auto st52 = stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
    // Step 1: emit Reject(35=3, RefTagID=52, reason=10).
    {
        FIXPP_INBOUND_SPLIT_AWAIT(
            r, tic,
            inbound_reject_leaf_(tic, ref_seq,
                                 52,  // RefTagID = 52 (SendingTime)
                                 hdr.msg_type,
                                 10,  // SessionRejectReason = 10 (SendingTime accuracy)
                                 st52.value));
        if (!r) {
            co_return r;
        }
    }
    // Step 2: the Logout, and Step 3 the ending: 105 (research D2 item 1), an R-2 ending
    // through send_ending_logout_, which records Disconnected unless the wait was armed.
    FIXPP_INBOUND_SPLIT_AWAIT(r, tic, send_ending_logout_(tic, hdr, st52.value, {}));
    co_return r;
}

// Inbound SequenceReset(35=4) in Reset mode (GapFillFlag ≠ Y).
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_sequence_reset_(
    bool tic, std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
    inbound_route const& route) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 019 T016 — fromAdmin completeness fix (FR-004):
    // Wire fromAdmin for inbound SequenceReset(Reset mode) AFTER the
    // FSM acts on it (apply_inbound_sequence_reset below applies the
    // new seqno). We fire BEFORE here since apply_inbound_sequence_reset
    // may co_return early on Reject — the message was accepted by
    // on_inbound_active_'s FSM guards before it selected this sub-arm and
    // we fire consistent with US1 wiring (after
    // seqnum/FSM validation accepts). A fromAdmin reject emits
    // session Reject(35=3) per FR-005/D4.
    // [019-app-callbacks T016; FR-004; research D3/D4]
    if (engine_.application != nullptr) {
        auto cb_r = parse_and_dispatch_(
            frame, inbound_parse_buffer, route,
            [&](auto& mv, auto& sid) { return engine_.application->fromAdmin(mv, sid); });
        // 092 contract C-6: a late parse failure closes the session.
        if (cb_r && *cb_r == dispatch_outcome::parse_failed) {
            co_return co_await close_on_late_parse_failure_();
        }
        if (!cb_r && cb_r.error() == fixpp::core::error::app_callback_threw) {
            (void)co_await close(close_mode::terminal);
            co_return std::unexpected(cb_r.error());
        }
        if (!cb_r) {
            // fromAdmin reject → emit session Reject(35=3). Not consumed
            // (fixpp#423): consume_rejected_seqnum_ excludes SequenceReset.
            // Best-effort: proceed even if assign or emit fails
            // (session still applies the SequenceReset below — co_return ok).
            const seqnum_t rj_ref = parse_seqnum(hdr.msg_seq_num);
            const auto rj_st52 = effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                       cfg_.sending_time_precision)
                                                  : SendingTimeStamp{};
            {
                FIXPP_INBOUND_SPLIT_AWAIT(
                    r, tic,
                    inbound_reject_best_effort_leaf_(tic, rj_ref, hdr.msg_type, rj_st52.value));
                if (!r) {
                    co_return r;
                }
            }
            // fixpp#537 (D-537-5): the gap's deadline, after the arm's own Reject.
            {
                FIXPP_INBOUND_SPLIT_AWAIT(expired, tic, resend_gap_deadline_(tic, hdr));
                if (expired) {
                    co_return *expired;
                }
            }
            co_return fixpp::core::expected_t<void>{};
        }
    }
    // 028 T010 (S6): when validate_sequence_numbers=false, bypass
    // apply_inbound_sequence_reset — the frame was already delivered to
    // fromAdmin above; counter is left unchanged (deliver-without-advance).
    // apply_inbound_sequence_reset is UNCHANGED; only whether it is called
    // is gated. [data-model S6, I-VCT-11, contract C2.7, FR-013]
    if (!cfg_.validate_sequence_numbers) {
        co_return fixpp::core::expected_t<void>{};
    }
    {
        auto applied = co_await apply_inbound_sequence_reset(parse_seqnum(hdr.new_seqno),
                                                             parse_seqnum(hdr.msg_seq_num));
        if (!applied) {
            co_return applied;
        }
    }
    // fixpp#537 (D-537-5): the gap's deadline, after the arm's own outcome. The site has no
    // test of its own for "moved nothing": a reset that moved NextNumIn has ended the
    // resend, which the helper tests.
    {
        FIXPP_INBOUND_SPLIT_AWAIT(expired, tic, resend_gap_deadline_(tic, hdr));
        if (expired) {
            co_return *expired;
        }
    }
    co_return fixpp::core::expected_t<void>{};
}

// Too-high MsgSeqNum: AwaitingResend and a ResendRequest(2); the session stays in its arm.
// 103 (research D4.1 route (a)): a too-high ResendRequest is answered first, and a
// too-high Logout sets the pending-Logout mark.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_too_high_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr, seqnum_t next_expected,
    seqnum_t seq) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // fixpp#537 (D-537-8, ruling R-9): a Heartbeat that opens the gap answers a pending
    // TestRequest first, by the in-sequence TestReqID rule. On a mismatch the session has
    // ended: AwaitingResend is not entered and no ResendRequest goes out.
    if (hdr.msg_type == "0" && !pending_test_req_id_.empty() &&
        !answer_pending_test_request_(hdr.test_req_id)) {
        co_return fixpp::core::expected_t<void>{};
    }
    // 103 (research D4.1 route (a); FR-008, fixpp#577): a too-high ResendRequest is answered
    // as an in-sequence one is, consuming and persisting nothing, and the retransmission goes
    // before fixpp's own ResendRequest below (FIX-SL Table 3, TC 20). A failed answer has
    // recorded Disconnected inside the helper. No fromAdmin: no out-of-sequence frame reaches
    // it.
    if (hdr.msg_type == "2") {
        FIXPP_INBOUND_SPLIT_AWAIT(ans, tic,
                                  answer_resend_request_(tic, hdr, /*in_sequence=*/false));
        if (!ans) {
            co_return ans;
        }
    }
    // 103 (research D4.2; FR-010, fixpp#577): a too-high Logout. With T = 0 or no clock it is
    // acknowledged at once, with no ResendRequest; otherwise the mark is set before fixpp's own
    // ResendRequest below.
    if (hdr.msg_type == "5") {
        FIXPP_INBOUND_SPLIT_AWAIT(lo, tic, on_inbound_active_too_high_logout_(tic, seq));
        if (lo) {
            co_return *lo;
        }
    }
    // Too-high: enter AwaitingResend and emit ResendRequest(2).
    // reconnect_fsm_.enter_awaiting_resend() owns state; we emit
    // ResendRequest inline (requires seqnum_mgr_ + store_then_emit).
    auto enter_r = co_await reconnect_fsm_.enter_awaiting_resend(next_expected, seq - 1U);
    (void)enter_r;  // state set; emit inline below
    // fixpp#537 (D-537-5): the instant the gap's deadline counts from.
    if (effective_clock_) {
        reconnect_fsm_.mark_resend_progress(effective_clock_->steady_now());
    }

    // Emit ResendRequest(2){BeginSeqNo=next_expected, EndSeqNo=0}
    // via the shared admin builder into a stack buffer (no heap —
    // [const §VIII.5]). EndSeqNo=0 → "through current" per FIX-SL §4.3.2.
    const auto st52 = effective_clock_
                          ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                          : SendingTimeStamp{};
    {
        FIXPP_INBOUND_SPLIT_AWAIT(r, tic,
                                  inbound_resend_request_leaf_(tic, next_expected, st52.value));
        if (!r) {
            co_return r;
        }
    }
    // Remain in Active (not Disconnected) per FR-009.
    co_return fixpp::core::expected_t<void>{};
}

// 103 (research D4.2; FR-010, fixpp#577): see session.hpp.
asio::awaitable<std::optional<fixpp::core::expected_t<void>>>
Session::on_inbound_active_too_high_logout_(bool tic, seqnum_t seq) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 104 (contract RR-2, INV-T): a 35=5 frame reaches this arm in the strand turn of
    // on_inbound_active_'s acking drain test, so the too-high Logout's bound is not acking here.
    assert(peer_logout_.phase != peer_logout_mark::phase_t::acking);
    // 103 (spec FR-021's crossing clause; FIX-SL §4.6, §4.10.2): while the session is itself
    // ending, the bound could not acknowledge: its claim refuses while a Logout of the session
    // is in flight (the owner, research D4.6), and its "live" test while a close has begun or
    // Engine::stop() has noted the stop. So that Logout is answered at once, as the in-sequence
    // one is, with no mark, no bound and no ResendRequest; NextNumIn is not moved.
    if (cfg_.logout_disconnect_timeout_ms == 0 || !effective_clock_ || logout_in_flight_ ||
        closing_or_stopped_()) {
        // 104 (contract RR-2 items 1 and 2): with reset_on_logout set, this immediate
        // acknowledgement is a receipt, `pending` and owned by this frame until its reset point
        // in acknowledge_peer_logout_ or its exit. The spawn and ignored branches below write
        // nothing to the marker: the bound's owed receipt is the 103 mark.
        pending_receipt_owner const receipt_owner{*this};
        FIXPP_INBOUND_SPLIT_AWAIT(ack, tic, acknowledge_peer_logout_(tic));
        co_return ack;
    }
    // A second too-high Logout while the mark is set is ignored, as under B-537-1.
    if (peer_logout_.phase == peer_logout_mark::phase_t::none) {
        peer_logout_ = peer_logout_mark{.seq = seq,
                                        .at = effective_clock_->steady_now(),
                                        .phase = peer_logout_mark::phase_t::pending};
        // The join counter is incremented before the spawn (liveness_counter_'s FQ-C), by
        // the hold the bound's frame then owns. A spawn that throws leaves no mark set.
        peer_logout_bound_join join{peer_logout_bound_counter_};
        try {
            asio::co_spawn(co_await asio::this_coro::executor,
                           run_peer_logout_bound_(std::move(join), conn_gen_), asio::detached);
        } catch (...) {
            peer_logout_.phase = peer_logout_mark::phase_t::none;
            throw;
        }
    }
    co_return std::nullopt;
}

// 103 (research D4.5): see session.hpp. The frame is inbound_logout_leaf_'s, which owns the
// session's one Logout (D4.6).
asio::awaitable<fixpp::core::expected_t<void>> Session::acknowledge_peer_logout_(
    bool tic) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    const auto st52 = effective_clock_
                          ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                          : SendingTimeStamp{};
    {
        FIXPP_INBOUND_SPLIT_AWAIT(r, tic, inbound_logout_leaf_(tic, st52.value, {}));
        if (!r) {
            co_return r;
        }
    }
    emit_event(fixpp::session::session_event_sequence_numbers_reset{.by_peer_request = false});
    if (fsm_state_ != fsm_state::Disconnected) {
        record_state_transition_(fsm_state::Disconnected);
    }
    // 104 (contract RR-1; RR-3, its Disconnected and reset-point steps): arm B's reset point,
    // after its reply, its event and its Disconnected. Shared by the immediate acknowledgement
    // (a pump frame, which set the marker `pending`) and the bound (the 103 mark `acking`; the
    // marker `none`).
    if (cfg_.reset_on_logout) {
        co_await asio::this_coro::reset_cancellation_state();
        if (enter_receipt_reset_point_()) {
            // RT-4: a failure of the unit is logged and not retried. A returned one leaves the
            // arm's ending as it is without it; a thrown one leaves the arm (fixpp#708).
            co_await run_receipt_reset_unit_();
        }
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
    }
    co_return fixpp::core::expected_t<void>{};
}

namespace {
// A bound's backstop for the wake it publishes: it clears *wake only while an exception unwinds
// the frame, so a suspended frame destroyed unresumed touches no Session state; the bound's
// explicit clear disarms it. Declared after the timer the wake cancels.
struct wake_clearer {
    std::function<void()>* wake = nullptr;
    int unwinding_at = std::uncaught_exceptions();
    bool armed = true;
    ~wake_clearer() {
        if (armed && std::uncaught_exceptions() > unwinding_at) {
            *wake = nullptr;
        }
    }
};
}  // namespace

// 103 (research D4.4, amended by data-model "Pending-Logout mark", its fields paragraph and
// exception rows): the bound. Its join counter is held by its `join` parameter, which touches
// only the counter, so the frame may outlive the Session (FQ-C). The wake is published, then
// "live" tested; the race's outcome is read from session state afterwards; the body clears the
// wake once the race is over, and a frame-local clearer clears it while an exception unwinds
// the frame. An exception that reaches the handler ends the bound closed.
asio::awaitable<void> Session::run_peer_logout_bound_([[maybe_unused]] peer_logout_bound_join join,
                                                      std::uint32_t gen) noexcept {
    try {
        co_await run_peer_logout_bound_body_(gen);
    } catch (...) {  // NOLINT(bugprone-empty-catch): the body handles its own; a cancellation
                     // met at this await leaves the release below to decide
    }
    // 105 (research D1 item 7): a detached root; its exit releases the connection-end latch,
    // the join still held.
    release_connection_end_();
}

asio::awaitable<void> Session::run_peer_logout_bound_body_(std::uint32_t gen) noexcept {
    try {
        if (peer_logout_bound_hold_first_run_) {
            co_await peer_logout_bound_hold_first_run_();
        }
        const auto live = [this, gen]() noexcept {
            assert(conn_gen_ == gen && "the too-high Logout's bound outlived its connection");
            (void)gen;
            return peer_logout_.phase == peer_logout_mark::phase_t::pending &&
                   fsm_state_ == fsm_state::Active && !closing_or_stopped_();
        };
        asio::steady_timer woken{co_await asio::this_coro::executor,
                                 asio::steady_timer::time_point::max()};
        // The exception path's backstop. Declared after `woken`, so an exception's unwinding
        // clears the wake before the timer it calls is destroyed and before the handler's
        // Disconnected edge calls the wake. It acts only while an exception unwinds the
        // frame: a suspended frame destroyed unresumed touches no Session state here, since
        // its Session may be gone (research D4.4's reason for no exit guard).
        // The body's two clears disarm it.
        wake_clearer wake_clear{.wake = &peer_logout_wake_};
        peer_logout_wake_ = [&woken] { woken.cancel(); };
        if (!live()) {
            peer_logout_wake_ = nullptr;
            wake_clear.armed = false;
            co_return;
        }
        const bool passed = seqnum_mgr_.next_inbound_unsafe() > peer_logout_.seq;
        if (peer_logout_bound_hold_before_race_) {
            co_await peer_logout_bound_hold_before_race_();
        }
        if (!passed) {
            using namespace asio::experimental::awaitable_operators;
            const auto deadline = detail::deadline_after(
                peer_logout_.at, std::chrono::milliseconds{cfg_.logout_disconnect_timeout_ms});
            (void)co_await (woken.async_wait(asio::as_tuple(asio::use_awaitable)) ||
                            detail::await_deadline(*effective_clock_, deadline));
        }
        peer_logout_wake_ = nullptr;
        wake_clear.armed = false;
        if (!live()) {
            co_return;
        }
        // The shared hold for the acknowledgement's emit (research D5′.4); "live" is tested
        // again after every wake.
        while (replay_gate_.exclusive) {
            co_await replay_gate_.wait_for_wake();
            if (!live()) {
                co_return;
            }
        }
        // The claim (research D4.6), synchronously, before the first suspension.
        if (logout_in_flight_) {
            peer_logout_.phase = peer_logout_mark::phase_t::none;
            co_return;
        }
        peer_logout_.phase = peer_logout_mark::phase_t::acking;
        {
            const detail::replay_gate::shared_hold hold{replay_gate_};
            assert(!logout_in_flight_ && fsm_state_ == fsm_state::Active &&
                   "the bound's acknowledgement would be skipped by its leaf");
            (void)co_await acknowledge_peer_logout_(true);
        }
        assert(conn_gen_ == gen && "the too-high Logout's bound outlived its connection");
        peer_logout_.phase = peer_logout_mark::phase_t::none;
    } catch (...) {
        // An exception from any await of the body, store_then_emit's operation_aborted for
        // a cancelled store call among them, ends the bound closed, as send_body_'s handler
        // ends a send: no mark is left `pending` or `acking`, and the session is
        // Disconnected (that edge also clears the Logout owner and wakes the replay gate).
        // The wake is already clear: wake_clear's destructor ran during the unwinding.
        assert(conn_gen_ == gen && "the too-high Logout's bound outlived its connection");
        peer_logout_.phase = peer_logout_mark::phase_t::none;
        if (fsm_state_ != fsm_state::Disconnected) {
            record_state_transition_(fsm_state::Disconnected);
        }
    }
}

// ── 105-transport-liveness: the ending Logout (research D2; data-model E-3) ──────────────

// E-3 row 1 (row 2 with no clock or a 0 ms timeout, row 2a when the spawn throws): the
// deadline is read once, here, at the write's start; the bound is spawned before the write, on
// the entering coroutine's executor, its join the coroutine's parameter. The catch does not
// rethrow (unlike on_inbound_active_too_high_logout_'s): a bound that cannot be spawned fails
// closed, the Logout not written (clarification D-g).
Session::ending_entry Session::enter_ending_write_(ending_kind kind,
                                                   asio::any_io_executor const& ex) noexcept {
    if (!effective_clock_ || cfg_.logout_disconnect_timeout_ms == 0) {
        return ending_entry::unlimited;
    }
    ending_wait_ =
        ending_wait_state{.phase = ending_wait_state::phase_t::writing,
                          .kind = kind,
                          .entered_from = fsm_state_,
                          .cut = false,
                          .deadline = detail::deadline_after(
                              effective_clock_->steady_now(),
                              std::chrono::milliseconds{cfg_.logout_disconnect_timeout_ms})};
    try {
        ending_logout_bound_join join{ending_logout_bound_counter_};
        if (ending_bound_spawn_fails_ && ending_bound_spawn_fails_()) {
            throw std::bad_alloc{};
        }
        asio::co_spawn(ex, run_ending_logout_bound_(std::move(join), conn_gen_), asio::detached);
    } catch (...) {
        ending_wait_.phase = ending_wait_state::phase_t::none;
        return ending_entry::refused;
    }
    return ending_entry::limited;
}

// E-3, "Leaving writing": on the strand, with no suspension before the caller's next step.
bool Session::leave_ending_write_(bool transmitted, bool claimed_mark) noexcept {
    if (ending_wait_.phase != ending_wait_state::phase_t::writing) {
        return false;
    }
    bool const arm = ending_wait_.kind == ending_kind::r2 && transmitted && !ending_wait_.cut &&
                     !claimed_mark && fsm_state_ == ending_wait_.entered_from;
    ending_wait_.phase = arm ? ending_wait_state::phase_t::armed : ending_wait_state::phase_t::none;
    wake_ending_logout_bound_();
    return arm;
}

// E-3, "The bound's loop": the live predicate by phase.
bool Session::ending_bound_live_(std::uint32_t gen) const noexcept {
    switch (ending_wait_.phase) {
        case ending_wait_state::phase_t::writing:
            assert(conn_gen_ == gen && "the ending Logout's bound outlived its connection");
            return conn_gen_ == gen;
        case ending_wait_state::phase_t::armed:
            if (conn_gen_ != gen || reconnect_drain_in_flight_) {
                return false;
            }
            if (ending_wait_.from_active()) {
                return !engine_stop_requested_ && fsm_state_ == fsm_state::Active;
            }
            return state_ == lifecycle::open;
        case ending_wait_state::phase_t::none:
        case ending_wait_state::phase_t::resolved:
            break;
    }
    return false;
}

// E-3, "The bound's exits", the expiry exit (rows 4, 10, 11, 14).
void Session::expire_ending_wait_(std::uint32_t gen) noexcept {
    if (ending_wait_.phase == ending_wait_state::phase_t::writing) {
        // Row 4: the write limit. The leaf clears the phase when its write returns. A give-up
        // waiting for the replay gate (T059a) re-tests at this wake and finds the cut.
        ending_wait_.cut = true;
        replay_gate_.wake_all();
        if (conn_gen_ == gen) {
            spawn_connection_close_(live_transport_shared_());
        }
        return;
    }
    // Rows 10, 11, 14: a live armed wait.
    ending_wait_.phase = ending_wait_state::phase_t::resolved;
    if (ending_wait_.from_active()) {
        record_state_transition_(fsm_state::Disconnected);
    } else {
        owe_connection_end_();
    }
    if (conn_gen_ == gen && state_ == lifecycle::open) {
        spawn_connection_close_(live_transport_shared_());
        connection_end_owed_.reset();
    }
}

// E-3, "The bound's loop" and "The bound's exits". A wake is a re-test; a deadline arm is a
// re-test once its own clock has reached its instant; a deadline arm that completed before its
// instant can only be an exception detail::await_deadline absorbed, the failure exit. Its join
// counter is held by its parameter, which touches only the counter.
asio::awaitable<void> Session::run_ending_logout_bound_(
    [[maybe_unused]] ending_logout_bound_join join, std::uint32_t gen) noexcept {
    enum class exit_t : std::uint8_t { ordinary, expiry, failure };
    exit_t how = exit_t::ordinary;
    try {
        asio::steady_timer woken{co_await asio::this_coro::executor,
                                 asio::steady_timer::time_point::max()};
        // wake_clearer's backstop; the explicit clear below disarms it.
        wake_clearer wake_clear{.wake = &ending_logout_wake_};
        ending_logout_wake_ = [&woken] { woken.cancel(); };
        for (;;) {
            if (!ending_bound_live_(gen)) {
                break;
            }
            auto const deadline = ending_wait_.deadline;
            bool const capped =
                establishment_cap_.has_value() && !ending_wait_.from_active() && engine_.clock;
            if (effective_clock_->steady_now() >= deadline ||
                (capped && engine_.clock->steady_now() >= *establishment_cap_)) {
                how = exit_t::expiry;
                break;
            }
            if (ending_bound_race_throws_ && ending_bound_race_throws_()) {
                throw std::bad_alloc{};
            }
            woken.expires_at(asio::steady_timer::time_point::max());
            using namespace asio::experimental::awaitable_operators;
            if (capped) {
                auto const cap = *establishment_cap_;
                auto const r = co_await (woken.async_wait(asio::as_tuple(asio::use_awaitable)) ||
                                         detail::await_deadline(*effective_clock_, deadline) ||
                                         detail::await_deadline(*engine_.clock, cap));
                if ((r.index() == 1 && effective_clock_->steady_now() < deadline) ||
                    (r.index() == 2 && engine_.clock->steady_now() < cap)) {
                    how = exit_t::failure;
                    break;
                }
            } else {
                auto const r = co_await (woken.async_wait(asio::as_tuple(asio::use_awaitable)) ||
                                         detail::await_deadline(*effective_clock_, deadline));
                if (r.index() == 1 && effective_clock_->steady_now() < deadline) {
                    how = exit_t::failure;
                    break;
                }
            }
        }
        ending_logout_wake_ = nullptr;
        wake_clear.armed = false;
    } catch (...) {
        how = exit_t::failure;
    }
    // The expiry exit, which the failure exit takes for the phase it finds while that phase is
    // writing or a live armed; never release_connection_end_() from those (row 16).
    if (how != exit_t::ordinary &&
        (ending_wait_.phase == ending_wait_state::phase_t::writing ||
         (ending_wait_.phase == ending_wait_state::phase_t::armed && ending_bound_live_(gen)))) {
        expire_ending_wait_(gen);
        co_return;
    }
    // The ordinary exit.
    if (ending_wait_.phase == ending_wait_state::phase_t::armed) {
        ending_wait_.phase = ending_wait_state::phase_t::resolved;
    }
    release_connection_end_();
}

// ── 106-logon-guards-write-bounds: the connection bound (research D3; data-model E-3) ───────

// The spawn (research D3, "The primitive"): its join built before co_spawn, as
// enter_ending_write_'s; the shipping-clock test once, here. A spawn that cannot be made returns
// false and the caller fails closed (research D3, "Spawn failure").
bool Session::spawn_connection_bound_(asio::any_io_executor const& ex) noexcept {
    try {
        connection_bound_join join{connection_bound_counter_};
        if (connection_bound_spawn_fails_ && connection_bound_spawn_fails_()) {
            throw std::bad_alloc{};
        }
        connection_bound_.running = true;
        connection_bound_.spawned = true;
        connection_bound_.loop_left = false;
        connection_bound_.armed_until = fixpp::core::steady_time_point::max();
        connection_bound_.gen = conn_gen_;
        connection_bound_.shipping = dynamic_cast<fixpp::core::system_clock_source const*>(
                                         effective_clock_.get()) != nullptr;
        asio::co_spawn(ex, run_connection_bound_(std::move(join), conn_gen_), asio::detached);
    } catch (...) {
        connection_bound_.running = false;
        return false;
    }
    return true;
}

// The one waiting callee (research D3, "The primitive"; data-model E-3, "The wait"). On the
// shipping clock the bound's own timer carries the deadline (system_clock_source's steady clock is
// the timer's), and no Clock member is named; on any other Clock, 105's idiom, the timer parked as
// the wake. A deadline arm that completed before its instant is the failure exit.
template <class Timer>
asio::awaitable<bool> Session::connection_bound_wait_(Timer& timer,
                                                      fixpp::core::steady_time_point deadline) {
    if (connection_bound_.shipping) {
        timer.expires_at(deadline);
        (void)co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        co_return true;
    }
    timer.expires_at(Timer::time_point::max());
    using namespace asio::experimental::awaitable_operators;
    auto const r = co_await (timer.async_wait(asio::as_tuple(asio::use_awaitable)) ||
                             detail::await_deadline(*effective_clock_, deadline));
    co_return !(r.index() == 1 && effective_clock_->steady_now() < deadline);
}

// The bound's loop (data-model E-3, the rows in their order): exit on its terms; with the loop's
// emit in flight and liveness_may_originate_(), the give-up at G = L + 3T + 1 ns; with a limited
// write in flight, its expiry at its start + write_timeout_ms; otherwise wait, to the earlier
// of the two or parked. Each re-test reads the clock once and tests every row against that
// reading, the give-up row first (N-7). A wake is a re-test. Its join counter is held by its
// parameter. No cancellation slot and no lock: teardown is a wake plus the exit test.
// 106 D4 supersedes 105 E-4's tr_written_at count for an emit not returned
// (ruling R1; claim-ok: R1 names a Gate A ruling): while the loop's own emit has not returned the
// instant is held_test_request_cap's.
asio::awaitable<void> Session::run_connection_bound_([[maybe_unused]] connection_bound_join join,
                                                     std::uint32_t gen) noexcept {
    bool failed = false;
    try {
        asio::steady_timer timer{co_await asio::this_coro::executor,
                                 asio::steady_timer::time_point::max()};
        // wake_clearer's backstop; the explicit clear below disarms it.
        wake_clearer wake_clear{.wake = &connection_bound_.wake};
        connection_bound_.wake = [&timer] { timer.cancel(); };
        for (;;) {
            if (connection_bound_exits_(gen)) {
                break;
            }
            auto const now = effective_clock_->steady_now();
            auto deadline = fixpp::core::steady_time_point::max();
            if (emit_in_flight_ && liveness_may_originate_()) {
                auto const threshold = connection_bound_.threshold;
                auto const g = detail::held_test_request_cap(last_inbound_steady_, threshold);
                if (now >= g) {
                    (void)decide_give_up_(threshold, co_await asio::this_coro::executor);
                    break;
                }
                deadline = g;
            }
            if (write_in_flight_ && cfg_.write_timeout_ms != 0) {
                auto const due = detail::deadline_after(
                    write_started_at_, std::chrono::milliseconds{cfg_.write_timeout_ms});
                if (now >= due) {
                    expire_write_();
                    break;
                }
                deadline = (std::min)(deadline, due);
            }
            if (connection_bound_wait_throws_ && connection_bound_wait_throws_()) {
                throw std::bad_alloc{};
            }
            connection_bound_.armed_until = deadline;
            bool const waited = co_await connection_bound_wait_(timer, deadline);
            connection_bound_.armed_until = fixpp::core::steady_time_point::max();
            if (!waited) {
                failed = true;
                break;
            }
        }
        connection_bound_.wake = nullptr;
        wake_clear.armed = false;
    } catch (...) {
        failed = true;
    }
    connection_bound_.running = false;
    // The failure exit (research D3, "Wait failure"): with the claim free, the claim, Disconnected
    // and one close; with it taken, nothing (the claimant owns the ending,
    // R10; claim-ok: R10 names a Gate A ruling). No Logout, no
    // reason record.
    if (failed && conn_gen_ == gen && claim_ending_(ending_claim::internal)) {
        record_disconnected_unless_already_();
        if (state_ == lifecycle::open) {
            spawn_connection_close_(live_transport_shared_());
            connection_end_owed_.reset();
        }
    }
}

// The write limit's expiry (research D5, "Expiry"; data-model E-5's steps, in their order): the
// claim, refused → nothing (the claimant owns the ending); the warn record (data-model E-8); the
// cut epoch, so the cut write returns session_write_timeout; Disconnected (onLogout once from
// Active, none before); one close. No Logout (owner ruling).
void Session::expire_write_() noexcept {
    if (!claim_ending_(ending_claim::write_timeout)) {
        return;
    }
    FIXPP_SLOG(logger_.get(), warn, get_trace_context(), fixpp::log::cat::session,
               "session ended: write timed out: limit_ms={} error={}",
               fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(cfg_.write_timeout_ms)),
               fixpp::log::ArgValue::from_u64(
                   static_cast<std::uint64_t>(fixpp::core::error::session_write_timeout)));
    write_cut_epoch_ = write_epoch_;
    record_disconnected_unless_already_();
    if (state_ == lifecycle::open) {
        spawn_connection_close_(live_transport_shared_());
        connection_end_owed_.reset();
    }
}

// research D3, "Spawn failure" and "One close": the connection's ending when its bound cannot be
// spawned (the liveness loop's start, a limited write's start) or respawned (drive_reconnect's
// failed drain). The claim taken when free, Disconnected, the close here rather than at a root's
// exit, whose release defers while an inbound frame is in its frame (inbound_depth_). No reason
// record.
void Session::fail_closed_without_bound_() noexcept {
    (void)claim_ending_(ending_claim::internal);
    record_disconnected_unless_already_();
    if (state_ == lifecycle::open) {
        spawn_connection_close_(live_transport_shared_());
        connection_end_owed_.reset();
    }
}

// The decision (research D3, "The decision"; data-model E-3), the loop's give-up block's and the
// bound's. 105 (T059a): R-1 claims the Logout owner and enters the ending state machine at the
// decision, so its budget counts from it; a bound that cannot be spawned writes no Logout
// (clarification D-g), and then this function closes at the decision itself: when the bound
// decides, the loop is suspended in its emit and reaches its exit only after that emit returns,
// which on a blocked write needs the close. The Text is built here, from the TestRequest the
// give-up names, so an answer during the gate wait does not change it; with none pending,
// "TestRequest unanswered" without the TestReqID clause (Q2).
bool Session::decide_give_up_(std::chrono::milliseconds threshold,
                              asio::any_io_executor const& ex) noexcept {
    if (!claim_ending_(ending_claim::give_up)) {
        return false;
    }
    FIXPP_SLOG(logger_.get(), warn, get_trace_context(), fixpp::log::cat::session,
               "session ended: TestRequest unanswered: test_req_id={} threshold_ms={} "
               "error={}",
               fixpp::log::ArgValue::from_inline(pending_test_req_id_),
               fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(threshold.count())),
               fixpp::log::ArgValue::from_u64(static_cast<std::uint64_t>(
                   fixpp::core::error::session_test_request_unanswered)));
    give_up_ = give_up_state{};
    constexpr std::string_view kText = "TestRequest unanswered";
    constexpr std::string_view kClause = ", TestReqID ";
    static_assert(
        std::tuple_size_v<decltype(give_up_.text_buf)> >= kText.size() + kClause.size() + 2 + 10,
        "the give-up Text's buffer holds its longest content");
    char* const begin = give_up_.text_buf.data();
    char* out = std::ranges::copy(kText, begin).out;
    if (!pending_test_req_id_.empty()) {
        out = std::ranges::copy(kClause, out).out;
        std::size_t const room = give_up_.text_buf.size() - static_cast<std::size_t>(out - begin);
        out = std::copy_n(pending_test_req_id_.data(),
                          (std::min)(pending_test_req_id_.size(), room), out);
    }
    give_up_.text_len = static_cast<std::size_t>(out - begin);
    give_up_.owner_claimed = !logout_in_flight_;
    if (give_up_.owner_claimed) {
        logout_in_flight_ = true;
        if (enter_ending_write_(ending_kind::r1, ex) == ending_entry::refused) {
            give_up_.closed = true;
            record_disconnected_unless_already_();
            if (state_ == lifecycle::open) {
                spawn_connection_close_(live_transport_shared_());
                connection_end_owed_.reset();
            }
        }
    }
    return true;
}

// Research D2 item 1, after Active. The Disconnected writes of its callers' tails are its own:
// recorded unless the wait was armed.
asio::awaitable<fixpp::core::expected_t<void>> Session::send_ending_logout_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr, std::string_view sending_time,
    std::string_view text) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // W-8: a further ending during an ending in progress is absorbed.
    if (ending_in_progress_()) {
        co_return fixpp::core::expected_t<void>{};
    }
    // 105 T143c (spec.md Clarifications, the Phase 9 implementation session; contract C-2 "Which
    // Logouts"): the kind of an ending an inbound frame draws after Active is decided here, for
    // every caller (`git grep -n 'send_ending_logout_(' -- src`). When that frame is the peer's
    // Logout, the frame the R-2 wait would wait for has arrived, so the Logout is the leaf's reply
    // kind: its write is limited and it arms no wait. The predicate is the response test's
    // (on_inbound_active_), MsgType(35) 5, the frame's first 35 for every reader.
    auto const kind = hdr.msg_type == "5" ? ending_kind::reply : ending_kind::r2;
    // 103 (research D4.6): the ending claims a pending mark; its own Logout is the one, and a
    // claimed mark arms no wait (C-2.8).
    bool const claimed = peer_logout_.phase == peer_logout_mark::phase_t::pending;
    claim_peer_logout_none_();
    bool armed = false;
    FIXPP_INBOUND_SPLIT_AWAIT(r, tic,
                              inbound_logout_leaf_(tic, sending_time, text, kind, &armed, claimed));
    if (!r) {
        co_return r;
    }
    if (!armed) {
        record_disconnected_unless_already_();
    }
    co_return fixpp::core::expected_t<void>{};
}

// Research D2 item 3. At NextNumIn, the in-sequence route. Off NextNumIn,
// acknowledge_peer_logout_'s shape, with no number consumed, no persist and no fromAdmin. The leaf
// would write nothing (the owner is set), so it is not called.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_ending_response_(
    bool tic, std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
    inbound_route const& route) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // At NextNumIn the in-sequence route, as the Active arm's guard (4) takes it: check_inbound,
    // then the Logout arm, or the out-of-sequence arm when check_inbound refuses.
    const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
    if (seq != 0 && seq == seqnum_mgr_.next_inbound_unsafe()) {
        auto chk = co_await seqnum_mgr_.check_inbound(seq);
        if (!chk) {
            FIXPP_INBOUND_SPLIT_AWAIT(
                r, tic, on_inbound_active_out_of_sequence_(tic, frame, hdr, chk.error(), route));
            co_return r;
        }
        note_inbound_advance_();
        FIXPP_INBOUND_SPLIT_AWAIT(r, tic, on_inbound_active_logout_(tic, frame, hdr, route));
        co_return r;
    }
    // Off NextNumIn:
    pending_receipt_owner const receipt_owner{*this};
    resolve_ending_wait_();
    emit_event(fixpp::session::session_event_sequence_numbers_reset{.by_peer_request = false});
    record_disconnected_unless_already_();
    if (cfg_.reset_on_logout) {
        co_await asio::this_coro::reset_cancellation_state();
        if (enter_receipt_reset_point_()) {
            co_await run_receipt_reset_unit_();
        }
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
    }
    co_return fixpp::core::expected_t<void>{};
}

// PossDup Stage-1 (Arms C/D, RC#1). Returns the arm's result when an Arm answered the frame,
// or nullopt when OrigSendingTime(122) validated and the arm continues to check_inbound.
asio::awaitable<std::optional<fixpp::core::expected_t<void>>> Session::on_inbound_active_poss_dup_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    if (hdr.orig_sending_time.empty()) {
        // Arm C (row 2): OrigSendingTime(122) absent → Reject(35=3),
        // 371=122 (RefTagID=OrigSendingTime), 373=1 (RequiredTagMissing).
        // Session survives (no disconnect). data-model INV-3; research D4.
        // fixpp#537 (D-537-5): when the gap's deadline fires here, no consume and no Reject.
        {
            FIXPP_INBOUND_SPLIT_AWAIT(expired, tic, resend_gap_deadline_(tic, hdr));
            if (expired) {
                co_return expired;
            }
        }
        const seqnum_t rj_ref_c = parse_seqnum(hdr.msg_seq_num);
        if (auto c = co_await consume_rejected_seqnum_(rj_ref_c, hdr.msg_type); !c) {
            co_return c;
        }
        const auto st52_c = effective_clock_
                                ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                                : SendingTimeStamp{};
        {
            FIXPP_INBOUND_SPLIT_AWAIT(
                r, tic,
                inbound_reject_leaf_(tic, rj_ref_c,
                                     122,  // RefTagID = 122 (OrigSendingTime)
                                     hdr.msg_type,
                                     1,  // SessionRejectReason = 1 (RequiredTagMissing)
                                     st52_c.value));
            if (!r) {
                co_return r;
            }
        }
        // Arm C: survive — do NOT disconnect.
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
        auto parse_122 = fixpp::core::fix_string_to_utc_time(
            std::span<const char>{hdr.orig_sending_time.data(), hdr.orig_sending_time.size()});
        // RC#1: present-but-unparseable 122 → Arm C (RequiredTagMissing, survive).
        if (!parse_122) {
            // 122 is non-empty (Arm C's empty-check above already handled empty),
            // so it is present but malformed — treat as RequiredTagMissing.
            // fixpp#537 (D-537-5): the gap's deadline, as at Arm C.
            {
                FIXPP_INBOUND_SPLIT_AWAIT(expired, tic, resend_gap_deadline_(tic, hdr));
                if (expired) {
                    co_return expired;
                }
            }
            const seqnum_t rj_ref_rc1 = parse_seqnum(hdr.msg_seq_num);
            if (auto c = co_await consume_rejected_seqnum_(rj_ref_rc1, hdr.msg_type); !c) {
                co_return c;
            }
            const auto st52_rc1 = effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                        cfg_.sending_time_precision)
                                                   : SendingTimeStamp{};
            {
                FIXPP_INBOUND_SPLIT_AWAIT(
                    r, tic,
                    inbound_reject_leaf_(tic, rj_ref_rc1,
                                         122,  // RefTagID = 122 (OrigSendingTime)
                                         hdr.msg_type,
                                         1,  // SessionRejectReason = 1 (RequiredTagMissing)
                                         st52_rc1.value));
                if (!r) {
                    co_return r;
                }
            }
            co_return fixpp::core::expected_t<void>{};
        }
        auto parse_52 = fixpp::core::fix_string_to_utc_time(
            std::span<const char>{hdr.sending_time.data(), hdr.sending_time.size()});
        if (parse_122 && parse_52 && *parse_122 > *parse_52) {
            // Arm D — strict 122 > 52.
            const seqnum_t rj_ref = parse_seqnum(hdr.msg_seq_num);
            if (auto c = co_await consume_rejected_seqnum_(rj_ref, hdr.msg_type); !c) {
                co_return c;
            }
            const auto st52_d = effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                      cfg_.sending_time_precision)
                                                 : SendingTimeStamp{};
            // Step 1: emit Reject(35=3, 371=122, 373=10).
            {
                FIXPP_INBOUND_SPLIT_AWAIT(
                    r, tic,
                    inbound_reject_leaf_(
                        tic, rj_ref,
                        122,  // RefTagID = 122 (OrigSendingTime — research D5)
                        hdr.msg_type,
                        10,  // SessionRejectReason = 10 (SendingTimeAccuracyProblem)
                        st52_d.value));
                if (!r) {
                    co_return r;
                }
            }
            // Step 2: the Logout, and Step 3 the ending: 105 (research D2 item 1), an R-2
            // ending through send_ending_logout_, which arms no wait when this frame is the
            // peer's Logout (T143c).
            FIXPP_INBOUND_SPLIT_AWAIT(r, tic, send_ending_logout_(tic, hdr, st52_d.value, {}));
            co_return r;
        }
        // else: equal or 122 < 52 → validated, fall through.
        // (parse failure of 52 while 122 parsed: only 35=3/5 reach here since
        // Guard-3 kills malformed-52 for all other msg_types; leave as
        // fall-through.)
    }
    co_return std::nullopt;
}

// check_inbound refused the MsgSeqNum: too low; overflow; with the knob off, any frame out
// of sequence; above NextNumIn while a resend is outstanding (the frame that opens a gap
// goes to on_inbound_active_too_high_ instead); or a sequence-number mutex that was drained.
// fixpp#537 (`.specify/536-537-538-recovery-liveness.md` §4.3, D-537-1 and D-537-2)
// supersedes the route that ended the session, in "Arm B" below, at a second frame above
// NextNumIn: that frame is ignored, and an unfilled gap ends by its deadline (D-537-5).
// 103 (research D4.1, fixpp#577) excepts a ResendRequest and a Logout, with sequence
// validation on: a too-high ResendRequest is answered, consuming and persisting nothing,
// and a too-high Logout sets the pending-Logout mark; neither sends a second
// ResendRequest.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_out_of_sequence_(
    bool tic, std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
    fixpp::core::error chk_error, inbound_route const& route) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 092 FR-019: an in-sequence message at NextNumIn = seqnum_max cannot be
    // consumed. Tested first: the Heartbeat, PossDup and knob-off arms below
    // would each keep the session at a NextNumIn that cannot advance. Silent
    // Disconnected, and the error is returned, matching the disposition of an
    // assign_outbound overflow.
    if (chk_error == fixpp::core::error::store_seqnum_overflow) {
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(chk_error);
    }
    // fixpp#537 (D-537-5): the gap's deadline, for any frame that reaches this function (none
    // advances NextNumIn; 103 research D6). It does not read chk_error (§4.7), and it runs
    // ahead of Arm B and of the Heartbeat
    // and PossDup arms, so ahead of the PossDup redelivery's fromApp.
    {
        FIXPP_INBOUND_SPLIT_AWAIT(expired, tic, resend_gap_deadline_(tic, hdr));
        if (expired) {
            co_return *expired;
        }
    }
    // 103 (research D6, fixpp#581): only a Heartbeat above NextNumIn, and any out-of-sequence
    // Heartbeat with the knob off, return here. A Heartbeat below NextNumIn goes on: with 43=Y
    // the PossDup arm ignores it, without it Arm B sends the too-low Logout.
    if (hdr.msg_type == "0" && (chk_error == fixpp::core::error::session_seqnum_too_high ||
                                !cfg_.validate_sequence_numbers)) {
        // fixpp#537 (D-537-6, ruling R-6): while a resend is outstanding, a Heartbeat above
        // NextNumIn answers a pending TestRequest by the in-sequence TestReqID rule. 105
        // (research D5, #582; contract C-5.1) adds the knob-off route: with sequence
        // validation off any out-of-sequence Heartbeat does too. A mismatch has ended the
        // session inside the routine.
        if (!pending_test_req_id_.empty() &&
            ((chk_error == fixpp::core::error::session_seqnum_too_high &&
              reconnect_fsm_.is_awaiting_resend()) ||
             !cfg_.validate_sequence_numbers)) {
            (void)answer_pending_test_request_(hdr.test_req_id);
        }
        // No advance, no persist, no fromAdmin, no echo.
        co_return fixpp::core::expected_t<void>{};
    }
    // 103 (research D4.1 route (b); FR-008, FR-009, fixpp#577): a ResendRequest above
    // NextNumIn while a resend is outstanding is answered, and no second ResendRequest goes
    // out. Ahead of the PossDup arm, whose admin row would drop one carrying 43=Y. With the
    // knob off the frame keeps the knob-off arm's delivery below.
    if (cfg_.validate_sequence_numbers &&
        chk_error == fixpp::core::error::session_seqnum_too_high && hdr.msg_type == "2") {
        FIXPP_INBOUND_SPLIT_AWAIT(ans, tic,
                                  answer_resend_request_(tic, hdr, /*in_sequence=*/false));
        co_return ans;
    }
    // 103 (research D4.1 route (b), D4.2; FR-009, FR-010): a too-high Logout while a resend
    // is outstanding sets the mark, or with T = 0 or no clock is acknowledged at once; it sends
    // no second ResendRequest, and a second one while the mark is set is ignored.
    if (cfg_.validate_sequence_numbers &&
        chk_error == fixpp::core::error::session_seqnum_too_high && hdr.msg_type == "5") {
        FIXPP_INBOUND_SPLIT_AWAIT(
            lo, tic, on_inbound_active_too_high_logout_(tic, parse_seqnum(hdr.msg_seq_num)));
        if (lo) {
            co_return *lo;
        }
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
            // at the 019 T011 in-sequence fromApp dispatch. No seqnum advance
            // (INV-1).
            auto cb_r = parse_and_dispatch_(
                frame, inbound_parse_buffer, route,
                [&](auto& mv, auto& sid) { return engine_.application->fromApp(mv, sid); });
            // 092 contract C-6: a late parse failure closes the session.
            if (cb_r && *cb_r == dispatch_outcome::parse_failed) {
                co_return co_await close_on_late_parse_failure_();
            }
            if (!cb_r) {
                if (cb_r.error() == fixpp::core::error::app_callback_threw) {
                    (void)co_await close(close_mode::terminal);
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
    // 028 T009 (S4): deliver-without-advance when knob is off.
    // Catches BOTH too-low AND too-high frames that fell through S2
    // (complexity tracking hazard 1 — both arrive here when validation off).
    // Counter is NOT advanced (check_inbound already returned false; no
    // explicit increment here). Session stays Active.
    // Discriminate via is_admin_msgtype (as the in-sequence path does at S4):
    //   admin → fromAdmin, app → fromApp. Reuse parse_and_dispatch_.
    // [data-model S4, contract C2.2/C2.3, FR-006, research D-3]
    if (!cfg_.validate_sequence_numbers) {
        if (engine_.application != nullptr) {
            const bool admin = detail::is_admin_msgtype(hdr.msg_type);
            auto cb_r =
                parse_and_dispatch_(frame, inbound_parse_buffer, route, [&](auto& mv, auto& sid) {
                    return admin ? engine_.application->fromAdmin(mv, sid)
                                 : engine_.application->fromApp(mv, sid);
                });
            // 092 contract C-6: a late parse failure closes the session.
            if (cb_r && *cb_r == dispatch_outcome::parse_failed) {
                co_return co_await close_on_late_parse_failure_();
            }
            if (!cb_r && cb_r.error() == fixpp::core::error::app_callback_threw) {
                (void)co_await close(close_mode::terminal);
                co_return std::unexpected(cb_r.error());
            }
        }
        // Stay Active, counter unchanged (no advance). C2.3.
        co_return fixpp::core::expected_t<void>{};
    }
    // fixpp#537 (D-537-1, D-537-2): a frame above NextNumIn while a resend is outstanding
    // is ignored, a ResendRequest and a Logout excepted above (103, D4.1). The outstanding
    // request runs to 0, so it covers the frame. Every other refusal stays fatal below.
    if (chk_error == fixpp::core::error::session_seqnum_too_high) {
        co_return fixpp::core::expected_t<void>{};
    }
    // Arm B: a frame below NextNumIn without 43=Y ends the session (row 5). 103 (research
    // D9; FR-018, fixpp#581): with QuickFIX's too-low Logout first, for a too-low refusal
    // only; any other refusal that reaches here (a drained sequence-number mutex) stays a
    // silent Disconnected. The text names N, read here before any advance, and M as parsed;
    // the peer's bytes are not echoed. A Logout that does not fit the leaf's buffer is not
    // sent, and the session still ends. 105-transport-liveness (R-2, contract C-2) supersedes
    // "ends the session" for a Logout that is transmitted: the session stays Active through the
    // wait and ends at the peer's Logout or the bound.
    if (chk_error == fixpp::core::error::session_seqnum_too_low) {
        // 104: the shared builder above refuse_logon_with_logout_.
        too_low_text_buffer text_buf{};
        const std::string_view text{text_buf.data(),
                                    write_too_low_text(text_buf, seqnum_mgr_.next_inbound_unsafe(),
                                                       parse_seqnum(hdr.msg_seq_num))};
        const auto st52 = effective_clock_
                              ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                              : SendingTimeStamp{};
        // 105 (research D2 item 1): an R-2 ending through send_ending_logout_, which claims a
        // pending mark (103 research D4.6) and records Disconnected unless the wait was armed.
        // 105 T143b, T143c (spec.md Clarifications, the Phase 9 implementation session; contract
        // C-2 "Which Logouts"): the peer's own Logout below NextNumIn draws the Logout the base
        // writes for it and arms no wait, as QuickFIX C++ and QuickFIX/J (nextLogout) and
        // QuickFIX/Go (handleLogout) skip the too-low check for a Logout; send_ending_logout_
        // makes that decision from the frame.
        FIXPP_INBOUND_SPLIT_AWAIT(r, tic, send_ending_logout_(tic, hdr, st52.value, text));
        co_return r;
    }
    // Any other refusal (a drained sequence-number mutex) is an internal failure: through the
    // edge (research D2 item 4).
    record_state_transition_(fsm_state::Disconnected);
    co_return fixpp::core::expected_t<void>{};
}

// Inbound SequenceReset(35=4) in GapFill mode, after the seqnum gate.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_gap_fill_(
    bool tic, std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
    inbound_route const& route) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 028 T011 (S7): when validate_sequence_numbers=false, bypass
    // apply_inbound_sequence_reset. An exact-match gapfill 35=4 has already
    // advanced the counter by 1 via S5 (ordinary exact-match path); NewSeqNo
    // is still not applied (the +1 is a fixpp ordering artifact, not QFJ-parity).
    // An out-of-order gapfill with knob off never reaches here (S4 returned it
    // before this point). Deliver to fromAdmin (35=4 is an admin msgtype).
    // [data-model S7, I-VCT-11, contract C2.7, FR-013]
    if (!cfg_.validate_sequence_numbers) {
        if (engine_.application != nullptr) {
            auto cb_r = parse_and_dispatch_(
                frame, inbound_parse_buffer, route,
                [&](auto& mv, auto& sid) { return engine_.application->fromAdmin(mv, sid); });
            // 092 contract C-6: a late parse failure closes the session.
            if (cb_r && *cb_r == dispatch_outcome::parse_failed) {
                co_return co_await close_on_late_parse_failure_();
            }
            if (!cb_r && cb_r.error() == fixpp::core::error::app_callback_threw) {
                (void)co_await close(close_mode::terminal);
                co_return std::unexpected(cb_r.error());
            }
        }
        // 029 T010 — PERSIST: validate-off exact-match GapFill (35=4, S7).
        // check_inbound +1-advanced at S5 (the GapFill frame is in-sequence);
        // NewSeqNo NOT applied (deliver-then-persist ordering, C3.4 case 1).
        // [029 tasks T010; contracts C3.4 case 1; data-model §Persist matrix RC-B]
        {
            auto p_r = co_await persist_inbound_advance_();
            if (!p_r) co_return std::unexpected(p_r.error());
        }
        // Counter already advanced by S5; NewSeqNo not applied. Stay Active.
        co_return fixpp::core::expected_t<void>{};
    }
    // 029 T010 — PERSIST: validate-ON GapFill (35=4, 123=Y) — persist the
    // check_inbound +1 advance (C3.1/C3.4 case 3 boundary).
    // check_inbound at S5 advanced the manager (e.g., seq=3 → next_inbound=4).
    // apply_inbound_sequence_reset (below) then performs the absolute jump
    // (set_next_inbound, NOT +1) which is NOT persisted (INV-H1/D-5).
    // So: persist the +1 here, then jump — durable stays at the +1 boundary.
    // [029 tasks T010; contracts C3.1, C3.4; data-model §Persist matrix]
    {
        auto p_r = co_await persist_inbound_advance_();
        if (!p_r) co_return std::unexpected(p_r.error());
    }
    co_return co_await apply_inbound_sequence_reset(parse_seqnum(hdr.new_seqno),
                                                    parse_seqnum(hdr.msg_seq_num));
}

// Inbound Logout(35=5) in Active/LogonReceived.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_logout_(
    bool tic, std::span<const std::byte> frame, fixpp::session::detail::FrameHeader const& hdr,
    inbound_route const& route) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 104 (contract RR-2, INV-T): a 35=5 frame reaches this arm in the strand turn of
    // on_inbound_active_'s acking drain test, so the too-high Logout's bound is not acking here.
    assert(peer_logout_.phase != peer_logout_mark::phase_t::acking);
    // 104 (contract RR-2 items 1 and 2), superseding 024 T013's site (b): with reset_on_logout
    // set, the receipt is `pending` from this arm's entry, owned by this frame, until its reset
    // point or its exit. In Active and LogonReceived alike.
    pending_receipt_owner const receipt_owner{*this};
    bool from_admin_rejected = false;
    // 105 (research D2 item 3): the peer's in-sequence response ends a wait armed after Active
    // at this arm's entry; the leaf below writes nothing (the owner is set).
    if (ending_wait_.phase == ending_wait_state::phase_t::armed) {
        resolve_ending_wait_();
    }

    if (fsm_state_ == fsm_state::Active) {
        // 103 (research D4.6): the in-sequence Logout claims a pending mark; its own
        // acknowledgement is the one.
        claim_peer_logout_none_();
        // Active → emit confirming Logout → Disconnected.
        // Emit a confirming Logout via store_then_emit.
        // Use a stack buffer (I-7: no heap on inbound-dispatch path).
        const auto st52 = effective_clock_
                              ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                              : SendingTimeStamp{};
        {
            FIXPP_INBOUND_SPLIT_AWAIT(r, tic, inbound_logout_leaf_(tic, st52.value, {}));
            if (!r) {
                co_return r;
            }
        }
        // T019 FR-018: emit SessionEvent so operators can observe the
        // logout-driven sequence-reset context (by_peer_request=false
        // because it's an inbound Logout, not an inbound 141=Y reset).
        emit_event(fixpp::session::session_event_sequence_numbers_reset{.by_peer_request = false});
    }
    // 019 T016 — fromAdmin completeness fix (FR-004):
    // Wire fromAdmin for inbound Logout (35=5) AFTER the FSM acts
    // (confirming Logout emitted above). Fires on Active→Disconnected
    // transition, while still Active, consistent with US1 wiring.
    // A fromAdmin reject here emits session Reject(35=3) per FR-005/D4
    // but the session still disconnects (Logout has been confirmed).
    if (engine_.application != nullptr) {
        // T019 note: parse_and_dispatch_'s callback_dispatch_scope ends
        // before it returns, so the depth of all callbacks is back to its
        // entry value when record_state_transition_ below enters onLogout.
        auto cb_r = parse_and_dispatch_(
            frame, inbound_parse_buffer, route,
            [&](auto& mv, auto& sid) { return engine_.application->fromAdmin(mv, sid); });
        // 092 contract C-6: a late parse failure closes the session.
        if (cb_r && *cb_r == dispatch_outcome::parse_failed) {
            // 104 (contract RR-2 item 2): the receipt ends here, before the close() this frame
            // calls, so that close() does not wait for its own caller.
            release_pending_receipt_();
            co_return co_await close_on_late_parse_failure_();
        }
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
            // 104 (contract RR-1): a fromAdmin reject ends the receipt before its reset point.
            from_admin_rejected = true;
            const seqnum_t rj_ref = parse_seqnum(hdr.msg_seq_num);
            const auto rj_st52 = effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                       cfg_.sending_time_precision)
                                                  : SendingTimeStamp{};
            {
                FIXPP_INBOUND_SPLIT_AWAIT(
                    r, tic,
                    inbound_reject_best_effort_leaf_(tic, rj_ref, hdr.msg_type, rj_st52.value));
                if (!r) {
                    co_return r;
                }
            }
        }
    }
    // 029 T010 — PERSIST: inbound Logout (35=5) in Active state.
    // check_inbound advanced next_inbound (the Logout is in-sequence).
    // Persist AFTER fromAdmin returns but BEFORE record_state_transition_
    // (Disconnected) so store_ is still live (C3.1). Skipped in
    // LogonReceived (check_inbound advanced, but LogonReceived only for
    // the edge case where the peer disconnected before we finished — still
    // correct to persist: check_inbound advanced in both states).
    // [029 tasks T010; contracts C3.1; data-model §Persist matrix Logout site]
    {
        auto p_r = co_await persist_inbound_advance_();
        if (!p_r) {
            // persist failed → already Disconnected inside helper.
            co_return std::unexpected(p_r.error());
        }
    }
    // Both Active and LogonReceived → Disconnected.
    // onLogout fires inside record_state_transition_ (INV-7 guard).
    // 104 (contract RR-5 item 6): the best-effort Reject may have skipped a refused number
    // after a Disconnected edge, so the write is made only when the state is not that.
    record_disconnected_unless_already_();
    // 104 (contract RR-1; RR-3, its Disconnected and reset-point steps), superseding 024 C3.1's
    // teardown reset for a received Logout: arm A's reset point, reached once the reply was
    // attempted, the event emitted, fromAdmin returned with no throw, reject or late parse
    // failure, and the persist succeeded. Disconnected is recorded first, so onLogout sees the
    // pre-reset numbers.
    if (!from_admin_rejected) {
        if (cfg_.reset_on_logout) {
            co_await asio::this_coro::reset_cancellation_state();
            if (enter_receipt_reset_point_()) {
                // RT-4: a failure of the unit is logged and not retried. A returned one leaves the
                // arm's ending as it is without it; a thrown one leaves the arm (fixpp#708).
                co_await run_receipt_reset_unit_();
            }
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
        }
    }
    co_return fixpp::core::expected_t<void>{};
}

// fromAdmin did not accept an Active admin frame: a late parse failure, a throw, or a reject.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_from_admin_failed_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr,
    fixpp::core::expected_t<dispatch_outcome> cb_r) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 092 contract C-6: a late parse failure closes the session.
    if (cb_r && *cb_r == dispatch_outcome::parse_failed) {
        co_return co_await close_on_late_parse_failure_();
    }
    if (!cb_r) {
        if (cb_r.error() == fixpp::core::error::app_callback_threw) {
            (void)co_await close(close_mode::terminal);
            co_return std::unexpected(cb_r.error());
        }
        // fromAdmin reject → session Reject(35=3). (INV-4; D4)
        // Disconnected-on-failure for assign_outbound + store_then_emit.
        // fixpp#423: Guard (4) consumed the seqnum; persist it before this
        // early return, as the delivering path does.
        if (auto p_r = co_await persist_inbound_advance_(); !p_r) {
            co_return p_r;
        }
        co_return co_await emit_session_reject_(parse_seqnum(hdr.msg_seq_num), hdr.msg_type);
    }
    co_return fixpp::core::expected_t<void>{};
}

// Inbound TestRequest(35=1) in Active: the Heartbeat reply, then persist.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_test_request_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    const auto st52 = effective_clock_
                          ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                          : SendingTimeStamp{};
    {
        FIXPP_INBOUND_SPLIT_AWAIT(r, tic,
                                  inbound_heartbeat_reply_leaf_(tic, hdr.test_req_id, st52.value));
        if (!r) {
            co_return r;
        }
    }
    // 029 T010 — PERSIST: inbound TestRequest (35=1).
    // check_inbound advanced next_inbound; fromAdmin dispatched by on_inbound_active_ before it
    // selected this sub-arm.
    // [029 tasks T010; contracts C3.1; data-model §Persist matrix TestRequest]
    {
        auto p_r = co_await persist_inbound_advance_();
        if (!p_r) co_return std::unexpected(p_r.error());
    }
    // Remain in Active.
    co_return fixpp::core::expected_t<void>{};
}

// Inbound ResendRequest(35=2) in Active: replay, then persist.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_resend_request_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // Guard (4)'s check_inbound has advanced NextNumIn: the request is in sequence.
    FIXPP_INBOUND_SPLIT_AWAIT(r, tic, answer_resend_request_(tic, hdr, /*in_sequence=*/true));
    co_return r;
}

// 103 (research D3; FR-012, FR-013): the answer to a ResendRequest, shared with D4's too-high
// route. An absent BeginSeqNo(7) is refused with Reject 373=1 (Required tag missing), 371=7 and
// QuickFIX C++'s text, and no walk; in sequence, Guard (4) has already consumed the number, so
// the advance is persisted before the Reject (fixpp#423's order, as on_inbound_active_unsupported_
// does). A present 7 that is empty or not a number parses to 0 and is answered as 7=1
// (replay_outbound_range_ clamps it). fromAdmin's order is unchanged: on_inbound_active_ has
// already dispatched it.
asio::awaitable<fixpp::core::expected_t<void>> Session::answer_resend_request_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr, bool in_sequence) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    if (!hdr.begin_seqno_present) {
        if (in_sequence) {
            auto p_r = co_await persist_inbound_advance_();
            if (!p_r) co_return std::unexpected(p_r.error());
        }
        co_return co_await emit_session_reject_(parse_seqnum(hdr.msg_seq_num), "2", /*reason=*/1,
                                                /*ref_tag_id=*/7, session_reject_reason_text(1));
    }
    // 027 T006: delegate to replay_outbound_range_() — the
    // extracted walk helper (T005). Caller owns the FSM
    // Disconnected transition on any unexpected return.
    // Two-value end model: rr_end==0 → end_is_through_current=true.
    // [research D-5, contracts C3, data-model I-NEX-3]
    const seqnum_t rr_begin = parse_seqnum(hdr.begin_seqno);
    const seqnum_t rr_end = parse_seqnum(hdr.end_seqno);
    auto replay_r =
        co_await replay_outbound_range_(rr_begin, rr_end, /*end_is_through_current=*/(rr_end == 0));
    if (!replay_r) {
        if (fsm_state_ != fsm_state::Disconnected) {
            record_state_transition_(fsm_state::Disconnected);
        }
        co_return std::unexpected(replay_r.error());
    }
    // 029 T010 — PERSIST: inbound ResendRequest (35=2).
    // check_inbound advanced next_inbound (ResendRequest is in-seq);
    // fromAdmin dispatched by on_inbound_active_ before it selected this sub-arm. Persist after
    // handling.
    // [029 tasks T010; contracts C3.1; data-model §Persist matrix ResendRequest]
    if (in_sequence) {
        auto p_r = co_await persist_inbound_advance_();
        if (!p_r) co_return std::unexpected(p_r.error());
    }
    // Remain in Active after responding to ResendRequest.
    co_return fixpp::core::expected_t<void>{};
}

// An application MsgType in Active with no Application registered: persist, then Reject.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_unsupported_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // No Application registered — pre-019 behaviour: Reject.
    // Unknown / app-type MsgType in Active →
    // Reject(reason=session_msg_type_invalid_for_state=3).
    // SessionRejectReason 3 = unsupported message type per [FIX-SL §4.5.4].
    // Disconnected-on-failure for assign_outbound + store_then_emit.
    // fixpp#423: Guard (4) consumed the seqnum; persist it before this
    // early return, as the delivering path does.
    if (auto p_r = co_await persist_inbound_advance_(); !p_r) {
        co_return p_r;
    }
    co_return co_await emit_session_reject_(parse_seqnum(hdr.msg_seq_num), hdr.msg_type);
}

// fromApp did not accept the frame: a late parse failure, a throw, or a reject answered by a
// BusinessMessageReject, then the arm's final persist (copied from the arm's tail).
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_active_from_app_failed_(
    bool tic, fixpp::session::detail::FrameHeader const& hdr,
    fixpp::core::expected_t<dispatch_outcome> cb_r) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // 092 contract C-6: a late parse failure closes the session.
    if (cb_r && *cb_r == dispatch_outcome::parse_failed) {
        co_return co_await close_on_late_parse_failure_();
    }
    if (!cb_r) {
        if (cb_r.error() == fixpp::core::error::app_callback_threw) {
            (void)co_await close(close_mode::terminal);
            co_return std::unexpected(cb_r.error());
        }
        // fromApp reject → BusinessMessageReject(35=j). (D4; FR-005)
        // NOT merged with the 35=3 Reject helper — different builder/fields.
        const seqnum_t ref_seq = parse_seqnum(hdr.msg_seq_num);
        const auto st52 = effective_clock_
                              ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                              : SendingTimeStamp{};
        {
            FIXPP_INBOUND_SPLIT_AWAIT(
                r, tic, inbound_business_reject_leaf_(tic, ref_seq, hdr.msg_type, st52.value));
            if (!r) {
                co_return r;
            }
        }
    }
    {
        auto p_r = co_await persist_inbound_advance_();
        if (!p_r) co_return std::unexpected(p_r.error());
    }
    co_return fixpp::core::expected_t<void>{};
}

// LogoutSent arm.
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_logout_sent_(
    bool tic, std::span<const std::byte> frame) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    // Per matrix LogoutSent row:
    //   inbound Logout → Disconnected (confirm)
    //   all other inbound → (drained) — silently accepted, no FSM change
    //     (seqnum NOT advanced, no fromAdmin/fromApp dispatch)
    // 099-fixt-appl-version-validation (data-model E-7): scanned with the route's hooks, so a body
    // pair a route declares is not read as the reply. This arm gains no step.
    inbound_route const route = select_route_(frame, /*establishment_arm=*/false);
    auto hdr = scan_frame_header(frame, session_hooks(*route.entry.owner));
    // 093 (contract C-2 step 1): a frame whose third field is not MsgType(35),
    // faulty or not, is disregarded as garbled: counted, evented and logged, with
    // no Reject, NextNumIn kept and no liveness refresh. It runs before 092's
    // fault branch, superseding 092's D-8, and D-1/D-2 for this shape.
    if (!hdr.msg_type_is_third) {
        note_garbles_(header_out_of_order_garble(frame));
        co_return fixpp::core::expected_t<void>{};
    }
    // 092 (contract C-2 D-9): a frame the scan could not read is disregarded,
    // even one carrying 35=5, so it is never taken as the Logout reply.
    if (hdr.fault != fixpp::wire::field_fault::none) {
        co_return co_await dispose_unparseable_(hdr, fsm_state::LogoutSent);
    }
    if (hdr.msg_type == "5") {  // Logout(35=5) confirms our Logout
        // 104 (contract RR-2 items 1 and 2): with reset_on_logout set, the confirming Logout is
        // a receipt, `pending` and owned by this frame until its reset point or its exit. Any
        // other frame here leaves the marker untouched.
        pending_receipt_owner const receipt_owner{*this};
        record_state_transition_(fsm_state::Disconnected);
        // 104 (contract RR-1; RR-3, its Disconnected, reset-point and phase-1 wake steps),
        // superseding 024 C3.1's teardown reset for an answered Logout: arm L's reset point,
        // after Disconnected and before phase 1 is told. The frame's MsgSeqNum is not read and
        // NextNumIn is not advanced.
        if (cfg_.reset_on_logout) {
            co_await asio::this_coro::reset_cancellation_state();
            if (enter_receipt_reset_point_()) {
                // RT-4: a failure of the unit is logged and not retried. A returned one leaves the
                // arm's ending as it is without it; a thrown one leaves the arm (fixpp#708).
                co_await run_receipt_reset_unit_();
            }
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
        }
        logout_confirmed_ = true;  // signal run_logout_phase1 coroutine
        // fixpp#536 (.specify/536-537-538-recovery-liveness.md, D-536-3), superseding
        // the Clock::cancel_sleeps() call that stood here: end run_logout_phase1's
        // wait through this session's own wake, which is non-null only while that
        // wait is in flight. A sweep of the clock also ended the liveness loop of
        // every other session on it.
        if (logout_wait_wake_) {
            logout_wait_wake_();
        }
    }
    // All other inbound frames: drained (no FSM change, no seqnum advance).
    co_return fixpp::core::expected_t<void>{};
}

// 014 T015 US2 / 013 T036 US2: the Logon path's CompID authorization, run BEFORE the FSM
// transition to Active (FR-006/FR-007/FR-008/FR-019/FR-020/FR-021/FR-024
// symmetric initiator path).
// Asserted CompID = peer SenderCompID(49) = cfg_.target_comp_id, in both roles.
//
// Two-arm guard (015 T020 removed the seam arm from the T015 form):
//   (1) live_peer_id_ set (the live handshake identity, T014/T015, T020) →
//       authorize with the real handshake peer_id, making the
//       already-fail-CLOSED mTLS gate *operable* with a live identity
//       (admit on-list; fail-close off-list/absent). FR-006/FR-008.
//   (2) mTLS + no identity available → fail CLOSED. RC#A.
//   (3) Non-mTLS → skip (backward compat, permissive). FR-019.
//
// live_peer_id_ is stored by install_reconnected_transport (T015) on the initiator and by
// attach_accepted_transport on the acceptor (T020/T-041 CLOSED), and reset() one-shot
// below whether arm (1) admits or refuses it — both roles are symmetric, the seam is gone.
// [[feedback_half_restructure_symmetric_api]]: symmetric one-pass fix.
// [data-model §E-2; contracts C2; FR-006; FR-007; FR-008; FR-009]
//
// 104 (contract RR-1 "Arm S's branch and the knob"; research D1 item 3): extracted from
// on_inbound_logon_sent_ so that arm S's Logout branch runs this same gate, both arms. It
// returns false when the identity is refused, having emitted the gate's event and consumed a
// live identity; the caller then records Disconnected. It returns true when the identity is
// admitted, or when the session is not mTLS. The asserted CompID is cfg_.target_comp_id in both
// roles. 106 D1 supersedes 104 T8's call sites: each Logon arm runs it once, right after
// interpret_logon, and before a held validate-first Reject that ends the arm (research D1 items 2
// and 4); arm S keeps its own call.
// The credentials gate of 033 T023, one member: the acceptor runs it once, right after the mTLS
// gate, and logon_identity_refuses_() runs it for a held validate-first Reject (106 D1).
bool Session::logon_credentials_admitted_(logon_interpret_result const& logon) const {
    fixpp::session::logon_credentials creds;
    if (logon.username.has_value()) {
        creds.username = std::string{*logon.username};
    }
    if (logon.password.has_value()) {
        creds.password = std::string{*logon.password};
    }
    // asserted_compid = peer SenderCompID(49) = cfg_.target_comp_id.
    return cfg_.compid_authorization_policy.authorize_logon(cfg_.target_comp_id, creds);
}

bool Session::mtls_gate_applies_() const noexcept {
    return cfg_.security_profile.k == fixpp::session::SecurityProfile::kind::mtls_ca ||
           cfg_.security_profile.k == fixpp::session::SecurityProfile::kind::mtls_pinned;
}

fixpp::core::expected_t<fixpp::session::bound_principal> Session::authorize_live_identity_()
    const noexcept {
    return cfg_.compid_authorization_policy.authorize(*live_peer_id_, cfg_.target_comp_id);
}

bool Session::logon_identity_admitted_() noexcept {
    const bool is_mtls = mtls_gate_applies_();

    if (live_peer_id_.has_value() && is_mtls) {
        // (1) A live handshake identity: use the real handshake peer_id, set by
        // install_reconnected_transport on the initiator and by attach_accepted_transport on
        // the acceptor. Only active when mTLS is configured (binding gate applicable).
        // Non-mTLS sessions skip to arm (4) permissive (no client cert).
        // FR-006: the identity source is the real handshake_result.peer_id
        // (no fabricated/stand-in identity on this path). FR-008: removes
        // the residual fabricated auth payload from the live path.
        const fixpp::tls::peer_identity& auth_pid = *live_peer_id_;
        const std::string_view asserted_compid = cfg_.target_comp_id;
        auto auth_r = authorize_live_identity_();
        if (!auth_r) {
            // Fail-closed: emit the event and return false; the caller records
            // Disconnected. On the acceptor this is the refusal of an identity the policy
            // does not bind to the asserted CompID. On the initiator the reconnect path's
            // own authorisation runs in reconnect_fsm.cpp step 7, before this gate.
            // [contracts C2; FR-007]
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
            return false;
        }
        // Authorization succeeded: emit peer_identity_bound event.
        // cn EMPTY: live_peer_id_.reset() below frees the backing store;
        // owned-cn deferred — see verify doc. sha256_fingerprint (owned
        // std::array) and bound_compid (config-stable) are safe. Matches
        // the failure-arm precedent (gate-b/r2 FQ-2). claim-ok: moved unchanged, names a ruling
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
        // (2) mTLS + no peer_identity → fail CLOSED.
        const std::string_view asserted_compid = cfg_.target_comp_id;
        emit_event(fixpp::session::session_event_compid_authorization_failed{
            .cn = {},
            .asserted_compid = asserted_compid,
            .expected_compids = {},
            .principal_source = fixpp::session::bound_principal::source::CN,
        });
        return false;
    }
    // (4) Non-mTLS (one_way_ca): no client cert → gate skipped.
    return true;
}

// 106 (research D1 item 2; data-model E-1's per-half table): the decision a validate-first Reject
// is held behind, made before the Reject is written. The halves are independent; one that does not
// apply admits and consumes nothing, and the result admits only when every half that applies
// admits. The mTLS half reads the same live identity and policy as the consuming gate, through
// authorize_live_identity_(), so the two agree (nothing on one frame assigns live_peer_id_ between
// them). The credentials half calls interpret_logon here, so its result stays in this frame, and
// treats any interpret_logon refusal as unreadable credentials: refused only when a validator would
// have judged them (B-106-1).
bool Session::logon_identity_refuses_(std::span<const std::byte> frame) {
    bool refused = mtls_gate_applies_() &&
                   (!live_peer_id_.has_value() || !authorize_live_identity_().has_value());
    if (!refused && cfg_.role == session_role::acceptor) {
        auto const logon = fixpp::session::interpret_logon(frame, cfg_.target_comp_id,
                                                           cfg_.sender_comp_id, cfg_.begin_string,
                                                           session_hooks(interpret_logon_table_()));
        refused = logon ? !logon_credentials_admitted_(*logon)
                        : cfg_.compid_authorization_policy.has_logon_validator();
    }
    if (refused) {
        // The consuming form, for its event and the one-shot consumption (data-model E-1): the
        // failure event when the mTLS half refused, the bound event when only the credentials
        // half did; nothing on a session the mTLS gate does not apply to.
        (void)logon_identity_admitted_();
    }
    return refused;
}

// LogonSent arm (initiator: the peer's Logon-ack).
// NOLINTNEXTLINE(readability-function-size,hicpp-function-size)
asio::awaitable<fixpp::core::expected_t<void>> Session::on_inbound_logon_sent_(
    bool tic, std::span<const std::byte> frame) noexcept {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
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

    // Header scan hoisted above interpret_logon so the validate gate (below)
    // can use hdr.msg_type for the 3/5 no-reject-loop exemption and
    // hdr.msg_seq_num for the RefSeqNum in any emitted Reject.
    // The hdr is reused for the SendingTime/seqnum guards below.
    // [041-validation-gate-wiring T014; data-model guard-precedence C-2]
    //
    // 099-fixt-appl-version-validation (data-model E-7, research R-7): the scan uses the frame's
    // route, selected here so that it lives in this frame.
    inbound_route const route = select_route_(frame, /*establishment_arm=*/true);
    auto hdr = scan_frame_header(frame, session_hooks(*route.entry.owner));
    // 093 (contract C-2 step 1): a frame whose third field is not MsgType(35),
    // faulty or not, is disregarded as garbled: counted, evented and logged, with
    // no Reject, NextNumIn kept and no liveness refresh. It runs before 092's
    // fault branch, superseding 092's D-8, and D-1/D-2 for this shape.
    if (!hdr.msg_type_is_third) {
        note_garbles_(header_out_of_order_garble(frame));
        co_return fixpp::core::expected_t<void>{};
    }
    // 093 (contract C-2 step 2; spec FR-030, FR-041; fixpp#523): once close() has
    // begun, or Engine::stop()'s step 1 has set the engine-stop flag on this strand
    // (data-model E-13), this arm acts on nothing more. The check is the arm's own
    // predicate, so a frame delivered after stop's step 1 meets it before any
    // effect. Garbled-frame accounting above is a transport observation, not an
    // arm effect, so it still runs (plan OD-7).
    if (logon_arm_superseded_(fsm_state::LogonSent)) {
        co_return fixpp::core::expected_t<void>{};
    }
    // 092 (contract C-2 D-2): a reply the scan could not read is refused.
    if (hdr.fault != fixpp::wire::field_fault::none) {
        co_return co_await dispose_unparseable_(hdr, fsm_state::LogonSent);
    }

    // 099-fixt-appl-version-validation (data-model E-7's S1, FR-029, FR-005) and 101 D6: as in the
    // acceptor's arm.
    if (establishment_frame_refused_(route, hdr)) {
        record_state_transition_(fsm_state::Disconnected);
        co_return fixpp::core::expected_t<void>{};
    }

    // ── 041-validation-gate-wiring T014: validate-first gate ──────────────
    // Run BEFORE interpret_logon: a dict-invalid Logon-ack produces a Reject
    // rather than a silent Disconnect (C-2 validate-first ordering, FR-003).
    // No-reject-loop: 35=3 and 35=5 exempt. C-3: seqnum NOT advanced (fixpp#423
    // consumes only in LogonReceived/Active; establishment arms are its Logon row).
    // A frame the scan cannot read never reaches this gate: #423's row 4 is
    // superseded by 092 contract C-2 (fixpp#507; research R-13; the owner
    // ruling of 2026-09-27 revising row 4). claim-ok: the date names the ruling
    // C-2 refuses such a frame above (D-2).
    // Buffer: the session's parse buffer B(L), as dispatch (093).
    // [041 T014; data-model E-4; contracts/validation-gate.md C-2/C-3]
    if (cfg_.validate_inbound_messages && session_route_.validator) {
        if (hdr.msg_type != "3" && hdr.msg_type != "5") {
            auto const v = validate_inbound_(frame, hdr, route);
            // 092 contract C-6: a late parse failure closes the session.
            if (v.outcome == validate_outcome::parse_failed) {
                co_return co_await close_on_late_parse_failure_();
            }
            if (v.outcome == validate_outcome::reject) {
                // 106 D1 supersedes 104 DQ-6 / L-104-3 (answers before the gates): as in the
                // acceptor's arm, the Reject is held until the identity is known (research D1
                // item 2); on the initiator only the mTLS half applies.
                if (logon_identity_refuses_(frame) ||
                    (logon_reject_disconnects_(hdr, v.reject.ref_tag_id) &&
                     !logon_identity_admitted_())) {
                    record_state_transition_(fsm_state::Disconnected);
                    co_return fixpp::core::expected_t<void>{};
                }
                auto rj = co_await emit_session_reject_(parse_seqnum(hdr.msg_seq_num), hdr.msg_type,
                                                        v.reject.reason, v.reject.ref_tag_id,
                                                        session_reject_reason_text(v.reject.reason),
                                                        fsm_state::LogonSent);
                // 099 (data-model E-7's S2, FR-010): any other Reject stays in the arm.
                if (rj && logon_reject_disconnects_(hdr, v.reject.ref_tag_id)) {
                    disconnect_unless_superseded_(fsm_state::LogonSent);
                }
                co_return rj;
            }
        }
    }

    // 104 arm S (contract RR-1 "Arm S's branch and the knob"; RR-2 items 1 and 2): with
    // reset_on_logout set, a Logout received here, past the guards above, is a receipt,
    // `pending` and owned by this frame until its reset point or its exit. With the knob off
    // the branch does not run and interpret_logon refuses the frame as a non-Logon.
    // On an mTLS session the Logon path's gate runs first, both its arms; a refused identity
    // draws its event and no reset. Either way the arm records Disconnected, sends no Logout,
    // runs no fromAdmin and returns success, as the base's ending does (RU-2, O19, W10).
    if (cfg_.reset_on_logout && hdr.msg_type == "5") {
        pending_receipt_owner const receipt_owner{*this};
        if (!logon_identity_admitted_()) {
            record_state_transition_(fsm_state::Disconnected);
            co_return fixpp::core::expected_t<void>{};
        }
        record_state_transition_(fsm_state::Disconnected);
        // Arm S's reset point (contract RR-3); the branch runs only with the knob.
        co_await asio::this_coro::reset_cancellation_state();
        if (enter_receipt_reset_point_()) {
            // RT-4: a failure of the unit is logged and not retried. A returned one leaves the
            // arm's ending as it is without it; a thrown one leaves the arm (fixpp#708).
            co_await run_receipt_reset_unit_();
        }
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
        co_return fixpp::core::expected_t<void>{};
    }

    auto result =
        fixpp::session::interpret_logon(frame, cfg_.target_comp_id, cfg_.sender_comp_id,
                                        cfg_.begin_string, session_hooks(interpret_logon_table_()));

    if (!result) {
        // Either a refused Logon (CompID/BeginString) OR a non-Logon
        // inbound (Heartbeat/TestRequest/Reject/out-of-scope admin /
        // invalid MsgType). Per matrix LogonSent row: every one of
        // these cells transitions to Disconnected (with Logout in US4).
        record_state_transition_(fsm_state::Disconnected);
        co_return fixpp::core::expected_t<void>{};
    }

    // 106 D1 supersedes 104 DQ-6 / L-104-3 (answers before the gates): the mTLS
    // CompID-authorisation gate runs here, once, right after interpret_logon and before posture
    // (research D1 item 4), through logon_identity_admitted_() (its comment has the arms). A
    // refused identity ends the arm Disconnected, returning success, with nothing written and no
    // number changed; posture, SendingTime, the sequence check, the too-low Logout, S3, strict,
    // both 141=Y units and the 789 honour follow.
    if (!logon_identity_admitted_()) {
        record_state_transition_(fsm_state::Disconnected);
        co_return fixpp::core::expected_t<void>{};
    }

    // 070-fix44-closeout S-029: symmetric posture-mismatch refusal on the
    // initiator's inbound Logon-ack (peer is the acceptor). Opt-in — cfg_.posture
    // unset ⇒ inert, byte-identical baseline (FR-012). [FR-002]
    if (cfg_.posture.has_value() &&
        should_refuse_posture(*cfg_.posture, hdr.test_message_indicator)) {
        co_return co_await refuse_logon_with_logout_("TestMessageIndicator posture mismatch",
                                                     fsm_state::LogonSent);
    }
    // 070-fix44-closeout S-030 (FR-007): capture the peer's advertised
    // MaxMessageSize(383) from its inbound Logon-ack (observability only).
    peer_advertised_max_message_size_ = parse_u32_opt(hdr.max_message_size);

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
                auto chk_st =
                    fixpp::session::check_sending_time(*parse_r, effective_clock_->now(), max_lat);
                if (!chk_st) {
                    sending_time_error = "SendingTime(52) accuracy";
                }
            }
        }

        if (!sending_time_error.empty()) {
            // Logon-path Q3: emit Logout only (no standalone Reject — D-3).
            const auto st52 = stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
            {
                FIXPP_INBOUND_SPLIT_AWAIT(
                    r, tic, inbound_logon_sent_logout_leaf_(tic, sending_time_error, st52.value));
                if (!r) {
                    co_return r;
                }
            }
            disconnect_unless_superseded_(fsm_state::LogonSent);  // 093 OD-26
            co_return fixpp::core::expected_t<void>{};
        }
    }

    // 104 (FR-009, FR-010; research D3 and D4, ruling O-2; data-model "LogonSent
    // acknowledgement: the 141 branch"): the acknowledgement's 141=Y and fixpp's own latch
    // are read before the sequence check. An acknowledgement with 141=Y that fixpp's Logon
    // did not ask for (the latch clear) is a reset, compared with 1 and not with NextNumIn:
    // it takes no check_inbound, it is consumed only at 34=1, and its unit runs after the
    // mTLS gate above. An asked one, and one without 141=Y, take the sequence check as before.
    // peer_ack_sent_reset_flag is at case scope for the persist guard below (029 INV-H1).
    const bool peer_ack_sent_reset_flag = (hdr.reset_seqnum_flag == "Y");
    const bool own_logon_sent_reset_flag = own_logon_sent_reset_flag_;
    const bool unasked_reset_ack = peer_ack_sent_reset_flag && !own_logon_sent_reset_flag;

    // ── Guard (4): seqnum check (T035 LogonSent row) ─────────────────
    const seqnum_t seq = parse_seqnum(hdr.msg_seq_num);
    if (seq == 0) {
        record_state_transition_(fsm_state::Disconnected);
        co_return fixpp::core::expected_t<void>{};
    }

    // 029 gate-b/r1: hoisted to LogonSent case scope so the persist guard
    // below can read it. Symmetric carried-bool approach (same as acceptor).
    // [029 INV-H1 fix; triage root-cause #1/#2; contracts C3.1]
    // 106 D2: it means "classified in sequence; consumed at the commit".
    bool logon_inbound_advanced_init = false;

    if (unasked_reset_ack) {
        // 104: consumed only when the acknowledgement is at 34=1 (ruling O-2).
        logon_inbound_advanced_init = seq == seqnum_min;
    } else if (auto chk = co_await seqnum_mgr_.classify_inbound(seq); !chk) {  // 106 D2
        // 027 T016 — behind-side tolerance (formulation A, I-NEX-5/D-7):
        // When the knob is on AND the failure is too-high (NOT too-low),
        // do NOT take the fatal branch. Leave next_inbound_ at X. Emit no
        // at-logon ResendRequest. Proceed toward Active so the peer's
        // proactive resend [X, peer_N-1] is admitted in-sequence.
        // [contract C5, data-model I-NEX-5/12, research D-7]
        //
        // A too-low result takes the 104 branch below; any other refusal ends the session.
        if (cfg_.enable_next_expected_msg_seq_num &&
            chk.error() == fixpp::core::error::session_seqnum_too_high) {
            // Behind-side: tolerate. Fall through to Active transition below.
            // logon_inbound_advanced_init stays false (manager not advanced).
        } else if (chk.error() == fixpp::core::error::session_seqnum_too_low) {
            // 104 (FR-012 to FR-014; research D5, D6; contract T1, T8) supersedes 103's spec
            // row "A too-low frame in NotConnected or LogonSent still ends silently": a too-low
            // acknowledgement draws QuickFIX's too-low Logout, whatever its 43, then
            // Disconnected. 106 D1 supersedes 104 T8 (the too-low branches' own gate calls): one
            // gate site per arm; the mTLS gate ran right after interpret_logon, so the identity
            // reaching here is admitted. 1137, the strict test, the unit and 789 are not reached.
            // The Logout carries Text(58) only and the session's next outbound number; NextNumIn
            // is not advanced; refuse_logon_with_logout_ gives the endings of a Logout that is not
            // sent, and sets no receipt marker.
            co_return co_await refuse_too_low_logon_(seq, fsm_state::LogonSent);
        } else {
            // Too-high with knob off, or a drained mutex → fatal, silent (I-2/I-4).
            record_state_transition_(fsm_state::Disconnected);
            co_return fixpp::core::expected_t<void>{};
        }
    } else {
        // Classified in sequence; consumed at the commit before Active (106 D2).
        // [029 gate-b/r1; INV-H1 fix; triage root-cause #1/#2]
        logon_inbound_advanced_init = true;
    }

    // 099-fixt-appl-version-validation (data-model E-7's S3, FR-010, `[const §XII.9]`): a
    // validating initiator tests DefaultApplVerID as the acceptor does, on the registry and not the
    // slot. Here and not at the record site below, behind the 141=Y reset unit: a reply with
    // `141=Y` and a bad 1137 would durably reset both stores and then be refused.
    if (fixt_routing_ && cfg_.validate_inbound_messages) {
        auto const appl_version = logon_appl_version_(result->default_appl_ver_id, frame);
        if (!appl_version.has_value()) {
            const auto rj_st52 = effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                       cfg_.sending_time_precision)
                                                  : SendingTimeStamp{};
            {
                FIXPP_INBOUND_SPLIT_AWAIT(
                    r, tic,
                    inbound_reject_leaf_(
                        tic, parse_seqnum(hdr.msg_seq_num), appl_version.error().ref_tag, "A",
                        appl_version.error().reason, rj_st52.value, fsm_state::LogonSent));
                if (!r) {
                    co_return r;
                }
            }
            disconnect_unless_superseded_(fsm_state::LogonSent);
            co_return fixpp::core::expected_t<void>{};
        }
    }

    // RC#C (gate-b/r1): bilateral_strict initiator path — symmetric to acceptor.
    // We sent 141=Y in our outbound Logon (see open() above). If the peer's
    // Logon-ack omits 141=Y, reject with session_seqnum_reset_mismatch(116).
    // [spec.md FR-017; triage RC#C(b); [[feedback_half_restructure_symmetric_api]]]
    // peer_ack_sent_reset_flag is read above, before Guard (4) (104).
    {
        if (!peer_ack_sent_reset_flag &&
            cfg_.reset_seqnum_policy_field == reset_seqnum_policy::bilateral_strict) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::session_seqnum_reset_mismatch);
        }
        // 106 D1 supersedes 104 N-8 (the asked path's unit before the gate): the asked
        // acknowledgement's unit keeps its place after 099's S3 test, now after the mTLS gate
        // (above); so does the unasked one's (below).
        if (peer_ack_sent_reset_flag && !unasked_reset_ack) {
            // 032 T010(a): one-shot clear the latch, snapshotted above before Guard (4).
            // All logic below uses the local (independent of reset/persist success — RC1
            // unconditional-assign invariant). [032 contract C4, data-model, RC1]
            own_logon_sent_reset_flag_ = false;
            // 032 T010(b): capture pre-reset outbound BEFORE reset_seqnums_to_one_durable.
            // reset_before_send := (n_pre_outbound == seqnum_min+1) means fixpp's own
            // Logon consumed the first post-reset seq (seq=1). [032 contract C1]
            const seqnum_t n_pre_outbound = seqnum_mgr_.peek_outbound();
            // 093 (contract C-6; spec FR-041; fixpp#524) supersedes RC#C-1's and
            // 030 T015/T016's reset, then inbound restore, then 032 T010(c)'s
            // outbound restore, and fixpp#518's in-unit teardown_reset_done_ stops:
            // the 141=Y reset unit. Step 1, the targets (research R-6): next-in 2
            // when the reset-ack Logon was consumed (logon_inbound_advanced_init),
            // else 1; next-out 2 only when fixpp sent 141=Y and its Logon consumed
            // the first post-reset number (032 C1: the latch AND reset_before_send),
            // else 1. They go to every store, volatile ones included (plan OD-9).
            const seqnum_t unit_in = logon_inbound_advanced_init ? seqnum_min + 1 : seqnum_min;
            const seqnum_t unit_out = own_logon_sent_reset_flag && n_pre_outbound == seqnum_min + 1
                                          ? seqnum_min + 1
                                          : seqnum_min;
            // Steps 3 and 4; plan OD-25 replaces step 2's shield with the store
            // operation's empty cancellation slot (run_reset_unit_). No return
            // path, so the restore below always runs.
            auto const unit = co_await run_reset_unit_(unit_in, unit_out);
            // Step 5: the restore. A cancellation recorded on this thread while
            // the unit ran is dropped: the state is fresh.
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
            // Step 6: the existing dispositions. A manager error is fatal; a store
            // error is fatal when the store is persistent (030 T015, FR-010) and
            // logged otherwise (024 I-07).
            // Plan OD-26: each write follows the unit's store operation.
            if (!unit.manager) {
                disconnect_unless_superseded_(fsm_state::LogonSent);
                co_return std::unexpected(unit.manager.error());
            }
            if (!unit.store && store_is_persistent_) {
                disconnect_unless_superseded_(fsm_state::LogonSent);
                co_return std::unexpected(unit.store.error());
            }
            // Step 7 (093 contract C-6) and fixpp#518: a close() may have begun,
            // or Engine::stop()'s step 1 reached this strand, while the unit's
            // store operation yielded.
            if (logon_arm_superseded_(fsm_state::LogonSent)) {
                co_return fixpp::core::expected_t<void>{};
            }
            // 032 T010(d): FR-018 mode mapping — use the latch alone (C4 gate).
            // by_peer_request=false iff fixpp sent 141=Y (own_logon_sent_reset_flag).
            // C4 (latch alone) and C1 (latch && reset_before_send) are DISTINCT:
            // bilateral_strict-at-N has latch=true, reset_before_send=false →
            // by_peer_request=false (fixpp initiated) but no outbound restore.
            // The pre-032 code used (policy==bilateral_strict) which was wrong for
            // reset_on_logon initiators with non-strict policy. [032 contract C4, FR-006]
            const bool we_initiated = own_logon_sent_reset_flag;
            emit_event(fixpp::session::session_event_sequence_numbers_reset{.by_peer_request =
                                                                                !we_initiated});
        }
    }

    // 106 D1 supersedes 014 T015's and 104 N-8's position of the mTLS CompID-authorisation gate
    // (after the asked 141=Y unit): it runs once, right after interpret_logon (above), still
    // before the FSM transition to Active.

    // 104 (clarification N-8; contract U1, U2, U4m, U7): the unasked acknowledgement's unit,
    // after the mTLS gate (106: right after interpret_logon), so a refused identity makes no reset.
    // Its targets are next-in 2 when the acknowledgement was consumed, else 1, and next-out 1: 032
    // C1's next-out 2 needs the latch, which is clear here. The restore, the dispositions and the
    // superseded test are the asked unit's (093 contract C-6). The 789 honour below compares the
    // peer's 789 with the number after the reset (research D3.2).
    if (unasked_reset_ack) {
        const seqnum_t unit_in = logon_inbound_advanced_init ? seqnum_min + 1 : seqnum_min;
        auto const unit = co_await run_reset_unit_(unit_in, seqnum_min);
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
        if (!unit.manager) {
            disconnect_unless_superseded_(fsm_state::LogonSent);
            co_return std::unexpected(unit.manager.error());
        }
        if (!unit.store && store_is_persistent_) {
            disconnect_unless_superseded_(fsm_state::LogonSent);
            co_return std::unexpected(unit.store.error());
        }
        if (logon_arm_superseded_(fsm_state::LogonSent)) {
            co_return fixpp::core::expected_t<void>{};
        }
        emit_event(fixpp::session::session_event_sequence_numbers_reset{.by_peer_request = true});
    }

    // T-041 CLOSED (015 US4): the acceptor guard above now binds the live
    // handshake identity from attach_accepted_transport and fails CLOSED
    // symmetrically; the per-config peer-identity test seam is removed in
    // production AND tests (T020/T021, SC-006/FR-009). Both roles bind a
    // real identity — no asymmetry remains. [FR-008/009; data-model §E-2; C2]

    // 033 T018 — FIXT initiator: read peer 1137 from Logon-ack; record
    // negotiated_appl_version_. FR-004a: initiator does NOT refuse on any
    // value — record whatever the peer advertises. No reject on this arm.
    // Gate: is_fixt() — registry present is not required (FR-004a is acceptor-only).
    // [033 contracts C3; data-model E2; FR-004a; research R1]
    //
    // 099-fixt-appl-version-validation (FR-027, FR-011): "does NOT refuse" holds for a
    // non-validating initiator; a validating one refused above. route_negotiated_ is what selection
    // reads, written on every Logon, Unknown when this reply resolves nothing.
    // negotiated_appl_version_ stays as the previous reply left it in that case, which is why the
    // two are separate.
    route_negotiated_ = fixpp::dict::application_version::Unknown;
    if (fixt_routing_) {
        record_msg_type_defaults_(frame);  // data-model E-10: per connection, validating or not
    }
    if (cfg_.is_fixt() && result->default_appl_ver_id.has_value()) {
        auto const resolved = resolve_ref_appl_ver_id(*result->default_appl_ver_id);
        if (resolved.has_value()) {
            // Set-once: negotiated_appl_version_ starts Unknown; set here.
            negotiated_appl_version_ = *resolved;
            route_negotiated_ = *resolved;
        }
        // Unknown wire value → leave Unknown (cannot map to application_version).
    }

    // 027 T015/T021 — initiator 789 honor (BEFORE Active transition).
    // [gate-b/r1 FQ-1: mirror the acceptor's honor_peer_next_expected_ ordering —
    // C6/C8/D-0] On X>N or invalid-789, honor_peer_next_expected_() records Disconnected
    // and returns false/*h789==false; the session MUST NOT enter Active first.
    // On X<N (resend) or X==N (no-op), returns true and we proceed to Active.
    // [contract C4/C6/C8, data-model I-NEX-2/3/4/9/11, D-6/D-10]
    if (cfg_.enable_next_expected_msg_seq_num && hdr.next_expected_present) {
        // 031: the initiator emits NO reply Logon on this arm, so the comparison
        // reference is the current peek_outbound() — byte-identical to 027 (the peer's
        // reply 789 = target+1 already matches fixpp's post-own-Logon outbound). [FR-008]
        auto h789 = co_await honor_peer_next_expected_(
            hdr.next_expected_msg_seq_num, hdr.next_expected_present, seqnum_mgr_.peek_outbound(),
            fsm_state::LogonSent);
        if (!h789) co_return std::unexpected(h789.error());
        if (*h789 != logon_789_outcome::in_sync_continue) {
            co_return fixpp::core::expected_t<void>{};
        }
    }

    // fixpp#518: a close() may have run during the 789 honour's writes.
    if (logon_arm_superseded_(fsm_state::LogonSent)) {
        co_return fixpp::core::expected_t<void>{};
    }

    // 106 D2: the advance is committed here, so every refusal before it keeps NextNumIn (closes
    // L-104-8). A reset-ack is not committed: its unit wrote absolute targets from the
    // classification. INV-106-1 (data-model E-2) holds while nothing from the arm's entry to here
    // changes next_inbound_ but a reset unit; re-derive as at the acceptor's commit. A refused
    // commit (the manager frozen or its lock drained) ends the arm Disconnected unless superseded,
    // silently.
    if (logon_inbound_advanced_init && !peer_ack_sent_reset_flag) {
        if (commit_freeze_hook_) {
            commit_freeze_hook_();
        }
        if (auto const commit = co_await seqnum_mgr_.check_inbound(seq); !commit) {
            disconnect_unless_superseded_(fsm_state::LogonSent);
            co_return fixpp::core::expected_t<void>{};
        }
    }

    // Valid Logon-ack + in-seq → Active (initiator handshake complete).
    record_state_transition_(fsm_state::Active);
    // 019 T016: if onLogon threw, terminal-close the session.
    if (lifecycle_cb_threw_) {
        lifecycle_cb_threw_ = false;
        (void)co_await close(fixpp::session::close_mode::terminal);
        co_return std::unexpected(fixpp::core::error::app_callback_threw);
    }

    // 029 T010 / gate-b/r1 — PERSIST: initiator Logon-ack (LogonSent, in-seq).
    // Guard: only when check_inbound net-advanced the manager AND no reset
    // rewound the counter (INV-H1 fix, triage root-cause #1/#2):
    //   logon_inbound_advanced_init false → behind-side tolerance (chk failure,
    //     manager stayed at X) → skip; store stays X, INV-H1 holds.
    //   peer_ack_sent_reset_flag true → reset already set store=manager=1;
    //     an additional +1 would put store ahead → skip.
    //   Both false → normal in-seq Logon-ack: persist fires → store==manager. ✓
    // [029 INV-H1; triage root-cause #1/#2; contracts C3.1]
    if (logon_inbound_advanced_init && !peer_ack_sent_reset_flag) {
        auto p_r = co_await persist_inbound_advance_(fsm_state::Active);  // 093 OD-26
        if (!p_r) co_return std::unexpected(p_r.error());
    }

    // fixpp#518: a close() may have run while the persist yielded, including one
    // posted from onLogon.
    if (logon_arm_superseded_(fsm_state::Active)) {
        co_return fixpp::core::expected_t<void>{};
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

    co_return fixpp::core::expected_t<void>{};
}

// T008 (US1 / FR-001): Session::send outbound pipeline.
//
// Pipeline per FR-001 + [2e §4.1] durable-before-transmit: build_send_frame_ validates the
// payload, stamps SendingTime(52), peeks MsgSeqNum(34) and frames the bytes into the
// session's frame slot, or into the fallback leaf's own buffer (fixpp#544 §2.5). Then toApp;
// then assign_outbound (RC#A), after toApp so a veto consumes no seqnum; then store_then_emit
// (I-3), which stores before transport_send_.
//
// Frame layout: 8=<begin_string>\x01 9=<NNN>\x01 34=<seq>\x01 49=<sender>\x01
//               52=<time>\x01 56=<target>\x01 <app_payload> 10=<CCC>\x01
//
// BodyLength (9=): bytes from after "9=NNN\x01" through end of last field before "10=".
// CheckSum (10=): byte-sum mod 256 over all bytes from start through end of "10=CCC\x01" body.
// Per [FIX-SL §4.2]: 9= and 10= computed here; all other session fields stamped inline.
//
// Frame buffer: kSendFrameBytes. A frame that does not fit it returns wire_frame_too_large.
// [const §VIII.5]: no heap allocation on the primary path. A send that finds the frame slot
// held takes send_fallback_leaf_, whose frame owns its buffer and is allocated (fixpp#544
// §2.5, rulings R-10 and R-6).
//
// gate-b/r1 FQ-1 (RC#1): the persistent store-retain fatal class, matching
// store_then_emit's durability-classified gate (is_persistent_retain_fatal) rather
// than the closed 3-code set the pre-fix guard hard-coded. Range [56,65) is
// EVERY store_* code except store_cancelled(65), which is not a retain failure: a
// cancelled store call has its own ending in store_then_emit (D-OUT-2). Sound
// only because of error.hpp's FR-021 store_* contiguity static_assert (10 variants
// 56..65) — an inserted 11th store code would shift store_cancelled and this
// range must move with it. [contracts/store-then-emit-disposition.md item 3]
static bool is_persistent_retain_fatal(fixpp::core::error e) noexcept {
    using fixpp::core::error;
    return e >= error::store_io_failure && e < error::store_cancelled;
}

namespace {
// fixpp#544 (B35, `.specify/544-hot-path-zero-alloc.md` §2.5; ruling R-10): one Session::send
// invocation's hold on the session's frame slot. It takes the slot only when the flag is
// clear, and clears only a flag it set itself, so a send that found the slot held, and a
// flag a test set, are left alone. The flag is cleared in its destructor and never by hand
// (re-check with `grep -n "in_use_ = false" src/session/session.cpp`), so the clear runs on
// every exit of the invocation: a co_return, a throw, and the destruction of a suspended
// frame.
class send_slot_holder {
public:
    explicit send_slot_holder(bool& in_use) noexcept : in_use_(in_use), held_(!in_use) {
        if (held_) in_use_ = true;
    }
    ~send_slot_holder() {
        if (held_) in_use_ = false;
    }
    send_slot_holder(const send_slot_holder&) = delete;
    send_slot_holder(send_slot_holder&&) = delete;
    send_slot_holder& operator=(const send_slot_holder&) = delete;
    send_slot_holder& operator=(send_slot_holder&&) = delete;
    [[nodiscard]] bool held() const noexcept { return held_; }

private:
    bool& in_use_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    bool held_;
};
}  // namespace

// fixpp#563 (`.specify/563-564-callback-send.md` §2.1 "Reader"): send is a non-coroutine
// and adds no frame. It returns the awaitable of its body, send_body_, with the depth
// test applied.
asio::awaitable<fixpp::core::expected_t<void>> Session::send(
    std::span<const std::byte> app_payload) noexcept {
    return send_body_(app_payload, send_depth_test::apply);
}

asio::awaitable<fixpp::core::expected_t<void>> Session::send_body_(
    std::span<const std::byte> app_payload, send_depth_test depth_test) noexcept {
    using fixpp::core::error;
    // 105 (research D1 item 7; contract C-1.c): Session::send and Engine::send's session body
    // end here; at every exit, a refused send included, the connection-end latch is released.
    connection_end_releaser const release_at_exit{*this};

    // FR-005 / D-3: FSM precondition — Session::send is only valid in Active.
    // spec.md US1 ACs all premise Active; sending while in LogonSent/NotConnected/
    // LogonReceived/LogoutSent/Disconnected is a programmer error.
    // Returns session_invalid_state_for_send (=77) — not session_invalid_logon —
    // to give the caller a semantically distinct diagnosis. [FR-005 / D-3]
    // 103 (research D4.7; FR-011): while a too-high Logout's mark is set, a send is refused.
    // 103 (FR-021, Phase 8 addendum): so is a send while a Logout of the session is in
    // flight (the owner, research D4.6), for the whole ending until Disconnected, whichever
    // emitter owns it, so no application frame is numbered after the Logout.
    if (fsm_state_ != fsm_state::Active || peer_logout_.phase != peer_logout_mark::phase_t::none ||
        logout_in_flight_) {
        co_return std::unexpected(error::session_invalid_state_for_send);
    }

    // fixpp#563 (§2.1 "Reader"; ruling R-1): no emit body begins while this session has
    // toApp or toAdmin on the stack, so a send started there takes the deferred path. The
    // test sits before the frame slot is taken. The await sits outside the try below, so
    // nothing the deferred path returns is converted to Disconnected here.
    if (depth_test == send_depth_test::apply && outbound_callback_depth_ != 0) {
        co_return co_await send_deferred_(app_payload);
    }

    // 103 (research D5′.2, D5′.4; FR-014, FR-015): while a resend answer holds the replay gate,
    // wait for its end, outside the try below. The pre-check is a co_await's, applied by hand in
    // this frame, so a cancelled caller returns before the wait and nothing is consumed. After a
    // wake the send is refused unless it could still be sent (the re-tests run only here, on the
    // contended path). A wait its caller's cancellation ends returns dispatch_aborted, the state
    // unchanged, the cancellation left pending.
    while (replay_gate_.exclusive) {
        if ((co_await asio::this_coro::throw_if_cancelled()) &&
            (co_await asio::this_coro::cancellation_state).cancelled() !=
                asio::cancellation_type::none) {
            co_return std::unexpected(error::dispatch_aborted);
        }
        co_await replay_gate_.wait_for_wake();
        if ((co_await asio::this_coro::cancellation_state).cancelled() !=
            asio::cancellation_type::none) {
            co_return std::unexpected(error::dispatch_aborted);
        }
        if (fsm_state_ != fsm_state::Active || state_ == lifecycle::closing ||
            engine_stop_requested_ || peer_logout_.phase != peer_logout_mark::phase_t::none ||
            logout_in_flight_) {
            co_return std::unexpected(error::session_invalid_state_for_send);
        }
    }

    // F5 (Round-A drift): wrap the entire send body in try/catch to absorb
    // asio::system_error{operation_aborted} thrown when the async_mutex awaitable
    // is cancelled (the send's own cancellation slot; the Session's root slot does not
    // reach a send; see `root_cancel_`, cell RS-1).
    // The try is for the ending: any exception of the send body, store_then_emit's
    // operation_aborted for a cancelled store call among them, ends the send with
    // dispatch_aborted and the session Disconnected, and leaves the caller's thread as
    // the D-OUT-9 block below says (.specify/536-537-538-recovery-liveness.md §10).
    // [F5 drift fix;
    // [[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]]
    try {
        // 103 (research D5′.4): the shared hold, the try's first statement, ahead of the slot
        // holder; it spans the frame's stamp and peek, toApp, the number and store_then_emit.
        const detail::replay_gate::shared_hold gate_hold{replay_gate_};
        // gate-b/r2 FQ-1: disconnect_required carries PROVENANCE out of send_impl —
        // set true ONLY at the two commit-region producer sites (assign_outbound
        // overflow, store_then_emit fatal). Transition on origin, not on the
        // returned error VALUE: a toApp passthrough can carry the identical
        // store-block error value (session.hpp `Application::toApp` contract
        // allows any error::* return) without having reached the commit region,
        // and must stay Active per INV-5/SC-004.
        bool disconnect_required = false;
        // fixpp#544 §2.5 (R-10): the holder is declared inside this try, so a throw releases
        // the slot before a catch below runs, and it outlives send_impl, whose
        // store_then_emit reads the slot until it returns. Session::send -> send_impl is
        // the boundary that predates the slot, so it stays a plain co_await. The leaf is a
        // new boundary, so it goes through the b3 sequence (FIXPP_INBOUND_SPLIT_AWAIT): the
        // first cancellation check of a leaf send stays at send_impl's own co_awaits.
        const send_slot_holder slot{send_slot_in_use_};
        fixpp::core::expected_t<void> impl_r{};
        if (slot.held()) {
            impl_r = co_await send_impl(app_payload, send_frame_slot_, disconnect_required);
        } else {
            bool tic = true;
            FIXPP_INBOUND_SPLIT_AWAIT(leaf_r, tic,
                                      send_fallback_leaf_(tic, app_payload, disconnect_required));
            impl_r = leaf_r;
        }
        // F9 (Round-A drift): a dispatch_aborted that send_impl RETURNS transitions to
        // Disconnected per US1 AC3 (its sources: the `dispatch_aborted` returns of
        // store_then_emit). A cancelled store call is not among them: store_then_emit
        // leaves by operation_aborted for it, and the conversion to dispatch_aborted is
        // this function's own catch, below (D-OUT-2, D-OUT-9).
        // dispatch_aborted(55) is outside is_persistent_retain_fatal's [56,65) range, so
        // it is not covered by disconnect_required — kept as a value residual.
        // Residual (documented, not fixed — out of 059's scope): a toApp returning
        // unexpected(dispatch_aborted) still spuriously disconnects here; pre-existing
        // since before 059 (the guard was always `== dispatch_aborted`), and
        // dispatch_aborted is an internal cancellation sentinel an app should never
        // return. [gate-b/r2 FQ-1; contracts/store-then-emit-disposition.md item 3]
        // 106 (research D5): a write the limit cut returns session_write_timeout, taken here as
        // dispatch_aborted is.
        if (disconnect_required || (!impl_r && (impl_r.error() == error::dispatch_aborted ||
                                                impl_r.error() == error::session_write_timeout))) {
            // [spec.md US1 AC3; F9 drift fix; gate-b/r2 FQ-1] claim-ok: a pre-existing citation
            // 106 D11: a cut write or another
            // ending's close returns here (FR-017).
            record_disconnected_unless_already_();
        }
        co_return impl_r;  // value un-coerced
    } catch (...) {        // NOLINT(bugprone-empty-catch): the statements below end the call
    }
    // D-OUT-9 (.specify/536-537-538-recovery-liveness.md §10). Every exit of the try is a
    // co_return, so these statements run only when the handler ran: for every exception that
    // ends a send, a cancellation met at an await of send_impl among them, and
    // store_then_emit's for a cancelled store call. The reset leaves the caller's thread as
    // a cancelled lock wait leaves it (D-CAN-3; fixpp#583), so the two timings of a
    // cancelled store do not differ in it. This await is never pre-checked, so it cannot
    // throw. The state is tested because store_then_emit has recorded Disconnected on its
    // own route, and every record adds an entry to the state ring.
    // [spec.md US1 AC3; I-09]
    co_await asio::this_coro::reset_cancellation_state();
    if (fsm_state_ != fsm_state::Disconnected) {
        record_state_transition_(fsm_state::Disconnected);
    }
    co_return std::unexpected(error::dispatch_aborted);
}

// fixpp#563 (`.specify/563-564-callback-send.md` §2.1 "The deferred path"): the path of a
// send started inside toApp or toAdmin. It is one coroutine whose frame owns the payload
// copy and awaits the send, so the copy outlives the send. No step tests Active or takes
// the frame slot: send_body_ does both after the hop.
asio::awaitable<fixpp::core::expected_t<void>> Session::send_deferred_(
    std::span<const std::byte> app_payload) {
    // 105 (research D1 item 7): as send_body_'s, for the exits before send_body_ is reached.
    connection_end_releaser const release_at_exit{*this};
    // "The payload": the build runs after the callback has returned, when the buffer the
    // caller's span points into may be gone.
    const std::vector<std::byte> payload(app_payload.begin(), app_payload.end());
    // The hop that takes the send off the callback's stack: a post onto the session's
    // executor ("When the post does not move the send out of the callback (R-7)").
    co_await asio::post(exec_.underlying(), asio::deferred);
    // "Cancellation at the hop (OD-1)": a cancellation emitted during the post is recorded
    // and not thrown there. This is the test a co_await applies when it begins, applied by
    // hand, so that a send which has consumed nothing returns an error and leaves the
    // session's state as it was.
    if ((co_await asio::this_coro::throw_if_cancelled()) &&
        (co_await asio::this_coro::cancellation_state).cancelled() !=
            asio::cancellation_type::none) {
        co_return std::unexpected(fixpp::core::error::dispatch_aborted);
    }
    FIXPP_ASSERT_NO_OUTBOUND_CALLBACK_AT_DEFERRED_SEND();
    // Step 5: a close() has begun. fsm_state_ stays Active while a graceful close emits
    // its Logout, so the Active check of send_body_ would admit this send behind that
    // Logout. A close that has finished is refused by that check.
    if (state_ == lifecycle::closing) {
        co_return std::unexpected(fixpp::core::error::session_invalid_state_for_send);
    }
    // The depth test is skipped: where the assertion above is compiled out and the depth
    // is still non-zero, a send that deferred again would post again each time it resumed.
    co_return co_await send_body_(payload, send_depth_test::skip);
}

// ── 020 T010: Opaque-payload validation (FR-016; INV-8; research.md D1) ──────
// Validate app_payload BEFORE stamping SendingTime, peeking seqnum, or building
// the frame. Any rejection returns app_payload_malformed (131) with NO seqnum
// consumption, NO transmit.
//
// Validation rules (boundary-aware token matching):
//   (1) payload must not be empty;
//   (2) payload must begin with the bytes '3','5','=' (i.e. "35=" at offset 0);
//   (3) no DUPLICATE 35= field, and
//   (4) no session header/trailer field (tags 8, 9, 34, 49, 52, 56, 10) after it.
//   Both are checked on real fields only, by build_send_frame_'s per-field walk: a Data
//   value counted by its Length is one field (fixpp#426), so `<SOH>34=` inside
//   EncodedText is neither a second field nor a MsgSeqNum.

// fixpp#422's fixed send-side header set (`kSendHeaderTags`) and its predicate
// `is_send_header_tag` live in send_header_tags.hpp (098-dialect-overlay, research
// R-7), with the reason for their content.
using fixpp::session::detail::is_send_header_tag;

fixpp::core::expected_t<Session::send_frame_built> Session::build_send_frame_(
    std::span<const std::byte> app_payload, std::span<std::byte> out) {
    using fixpp::core::error;

    // ── T010: Opaque-payload validation — BEFORE seqnum peek/assign/stamp ──────
    // [020-g2 research.md D1; data-model.md INV-8; FR-016]
    {
        // Cast to string_view for boundary-token scanning. No allocation.
        std::string_view pv{reinterpret_cast<const char*>(app_payload.data()), app_payload.size()};

        // (1) Empty payload is malformed.
        if (pv.empty()) {
            return std::unexpected(error::app_payload_malformed);
        }

        // (2) Payload must lead with "35=".
        if (pv.size() < 3 || pv[0] != '3' || pv[1] != '5' || pv[2] != '=') {
            return std::unexpected(error::app_payload_malformed);
        }

        // (2a) Payload must end with SOH so the last field is terminated before
        //      checksum append. A trailing-SOH-less payload would cause the checksum
        //      field to be appended without a field boundary. [RC#2: gate-b/r1]
        // cppcheck-suppress containerOutOfBounds  // FP: the pv.empty() guard above returns first
        if (pv.back() != '\x01') {
            return std::unexpected(error::app_payload_malformed);
        }

        // (2b) MsgType value must be non-empty: the first SOH must be at offset > 3
        //      (i.e. at least one byte between "35=" and the SOH terminator).
        //      "35=\x01" has first_soh==3 → empty MsgType → malformed. [RC#2: gate-b/r1]
        {
            const std::size_t fst = pv.find('\x01');
            // fst != npos is guaranteed because pv.back() == '\x01' was verified above.
            if (fst <= 3U) {
                return std::unexpected(error::app_payload_malformed);
            }
        }

        // (3)/(4) — a second 35= and an embedded header/trailer tag — are checked
        // by the per-field walk below, on real fields only (fixpp#426).
    }

    // ── 022 T008+T009: Per-field scanner + AllowPosDup excision ─────────────────
    // Anchors: research.md D2/D5/D6; data-model.md §2 (INV-1..5); contracts §C2.1–C2.6.
    //
    // T008 — Scanner: walk every post-35= field and validate it is
    //   <non-empty digit-only tag, no leading zero, <= 65535>=<non-empty value>\x01
    //   with a tag that is neither 35 (020 rule 3) nor a session header/trailer tag
    //   (020 rule 4). On the FIRST malformed field → return app_payload_malformed=131,
    //   no seqnum, no transmit.
    //
    // fixpp#426 — a Data value counted by the Length field just before it is read
    //   by that count, so a SOH inside it is not a field boundary and nothing inside
    //   it is checked, moved or excised as a field. A count that runs past the
    //   payload or is not followed by SOH is malformed; the payload is the caller's,
    //   so it fails closed (design §4). The pairs are the session's dictionary, else
    //   the standard table.
    //
    // T009 — Excision + header partition (only over a fully-validated payload):
    //   copy the leading 35= field, then the header-class post-35= fields
    //   (is_send_header_tag, or since 098 a tag the session's dictionary declares in
    //   its header), then the remaining fields, each group in original
    //   order, into strip_buf; rebind app_payload to that buffer (fixpp#422).
    //   A counted Data value goes to the same group as its Length, so the pair stays
    //   adjacent even when the two tags classify differently.
    //   allow_pos_dup==false (default) skips 43 and 122; allow_pos_dup==true keeps
    //   their values verbatim, at their header position.
    //   INV-1: 35= (field 0) never touched.
    //   INV-2: only complete, real 43=..\x01 / 122=..\x01 fields removed.
    //   INV-3: stripped payload remains 35=-leading SOH-delimited.
    //   INV-4: no heap — ONE scratch strip_buf, session-owned (sized same as body_buf).
    //   INV-5: build_replay_frame is NOT on this path.

    // strip_buf names the session-owned send_strip_scratch_ (fixpp#544 §2.5), so it
    // outlives the scanner+excision block and remains valid when the framing block
    // below reads app_payload (which may be rebound to point into it). [research.md D6; INV-4]
    auto& strip_buf = send_strip_scratch_;
    std::size_t strip_len = 0;

    {
        std::string_view pv{reinterpret_cast<const char*>(app_payload.data()), app_payload.size()};
        const std::span<const std::byte> pb = app_payload;
        const fixpp::wire::dict_hooks hooks = session_hooks(inbound_tv_);

        // Skip the leading 35=<value>\x01 field (field 0 — never modified).
        // pv.back()=='\x01' is guaranteed by the 020 floor; find('\x01') always succeeds.
        const std::size_t lead_soh = pv.find('\x01');

        struct PayloadField {
            std::uint16_t tag;
            std::size_t start;  // first byte of the tag
            std::size_t end;    // one past the terminating SOH
            bool counted;       // a Data value read by its Length's count
        };
        // Steps over the field at `pos`; nullopt when it is malformed. One `carry`
        // is threaded through each walk.
        const auto next_field =
            [&](std::size_t pos,
                fixpp::wire::length_data_carry& carry) -> std::optional<PayloadField> {
            const std::size_t eq = pv.find_first_of("=\x01", pos);
            // No '=' before the next SOH, including an empty field.
            if (eq == std::string_view::npos || pv[eq] != '=') return std::nullopt;
            const auto tag = parse_outbound_tag(pv.substr(pos, eq - pos));
            if (!tag) return std::nullopt;
            const std::size_t vstart = eq + 1;
            const auto value = carry.read_value(pb, vstart, *tag, hooks);
            // A malformed count, or an empty value (the payload ends with SOH).
            if (!value || value->end == vstart) return std::nullopt;
            return PayloadField{
                .tag = *tag, .start = pos, .end = value->end + 1, .counted = value->counted};
        };

        // --- T008: Scanner pass ---------------------------------------------
        {
            static constexpr std::array<std::uint16_t, 8> kRefusedTags = {8,  9,  10, 34,
                                                                          35, 49, 52, 56};
            fixpp::wire::length_data_carry carry;
            std::size_t pos = lead_soh + 1;
            while (pos < pv.size()) {
                const auto field = next_field(pos, carry);
                if (!field || std::ranges::find(kRefusedTags, field->tag) != kRefusedTags.end()) {
                    return std::unexpected(error::app_payload_malformed);
                }
                pos = field->end;
            }
        }

        // --- T009: Excision + header partition pass (fixpp#422) -------------
        // Only over a scanner-validated payload.
        {
            // Lambda: append bytes into strip_buf; returns false on overflow.
            const auto wstrip = [&](std::string_view sv) -> bool {
                if (strip_len + sv.size() > strip_buf.size()) return false;
                for (char c : sv) strip_buf[strip_len++] = static_cast<std::byte>(c);
                return true;
            };

            // INV-1: always copy the leading 35=<value>\x01 field verbatim.
            if (!wstrip(pv.substr(0, lead_soh + 1))) {
                return std::unexpected(error::wire_frame_too_large);
            }

            // Pass 1 copies the header-class fields, pass 2 the rest, each in the
            // caller's order, so no header field follows a body field on the wire.
            for (const bool header_pass : {true, false}) {
                fixpp::wire::length_data_carry carry;
                bool prev_header = false;
                std::size_t pos = lead_soh + 1;
                while (pos < pv.size()) {
                    const auto field = next_field(pos, carry);
                    if (!field) {  // the scanner pass accepted every field
                        return std::unexpected(error::app_payload_malformed);
                    }
                    // 098-dialect-overlay (FR-022, research R-7): a tag the fixed set
                    // misses is header-class when the session's dictionary declares it
                    // in its header. Asked only after that miss; a `noexcept` read of
                    // `header_bits_`, built by `Dictionary::as_table_view()` and
                    // adopted by open(), so no allocation
                    // ([const §XV.1]). A counted Data value still follows its
                    // Length's class.
                    const bool header =
                        field->counted ? prev_header
                                       : (is_send_header_tag(field->tag) ||
                                          (inbound_tv_ && inbound_tv_->is_header_tag(field->tag)));
                    // INV-2: excise ONLY a real 43 or 122 field.
                    const bool excised = !field->counted && !cfg_.allow_pos_dup &&
                                         (field->tag == 43 || field->tag == 122);
                    if (!excised && header == header_pass) {
                        // Copy the field (including its terminating SOH).
                        if (!wstrip(pv.substr(field->start, field->end - field->start))) {
                            return std::unexpected(error::wire_frame_too_large);
                        }
                    }
                    prev_header = header;
                    pos = field->end;
                }
            }

            // Rebind app_payload to the rebuilt buffer — the existing framing below
            // consumes it unchanged (INV-3; contracts §C2.6).
            // strip_buf is session-owned, so it outlives the framing below.
            app_payload = std::span<const std::byte>(strip_buf.data(), strip_len);
        }
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
    //   4. Build the BODY into the session-owned scratch buffer `body_buf`:
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
        return std::unexpected(error::app_payload_malformed);
    }
    std::string_view msgtype_field = pv.substr(0, first_soh + 1);  // "35=D\x01"
    std::string_view rest_payload = pv.substr(first_soh + 1);      // remaining fields

    // (4) Build BODY into a second scratch buffer, session-owned (send_body_scratch_).
    // Body layout: 35=<value>\x01  34=<seq>\x01  49=<sender>\x01  52=<time>\x01
    //              56=<target>\x01  <rest_payload>
    auto& body_buf = send_body_scratch_;
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
        return std::unexpected(error::wire_frame_too_large);
    }
    // 34=<seq>\x01
    {
        char nbuf[12];
        auto [end, ec] = std::to_chars(nbuf, nbuf + sizeof(nbuf), static_cast<std::uint32_t>(seq));
        (void)ec;
        if (!wfield_body("34=", std::string_view{nbuf, static_cast<std::size_t>(end - nbuf)})) {
            return std::unexpected(error::wire_frame_too_large);
        }
    }
    // 49=<sender>\x01
    if (!wfield_body("49=", cfg_.sender_comp_id)) {
        return std::unexpected(error::wire_frame_too_large);
    }
    // 52=<time>\x01
    if (!wfield_body("52=", st52.value)) {
        return std::unexpected(error::wire_frame_too_large);
    }
    // 56=<target>\x01
    if (!wfield_body("56=", cfg_.target_comp_id)) {
        return std::unexpected(error::wire_frame_too_large);
    }
    // rest of app_payload (business fields after the 35= field)
    if (bpos + rest_payload.size() > body_buf.size()) {
        return std::unexpected(error::wire_frame_too_large);
    }
    if (!wsv_body(rest_payload)) {
        return std::unexpected(error::wire_frame_too_large);
    }

    const std::size_t body_len = bpos;  // exact body length, no padding

    // (5) Build the wire frame into `buf`, the caller's frame buffer.
    // Frame: "8=<begin>\x01" "9=" <body_len digits> "\x01" <body_buf[0..body_len)>
    //        "10=" <CCC> "\x01"
    const std::span<std::byte> buf = out;
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
        return std::unexpected(error::wire_frame_too_large);
    }

    // 9=<body_len digits (no padding)>\x01
    {
        char bl_buf[12];
        auto [bl_end, bl_ec] = std::to_chars(bl_buf, bl_buf + sizeof(bl_buf), body_len);
        (void)bl_ec;
        if (!wfield("9=", std::string_view{bl_buf, static_cast<std::size_t>(bl_end - bl_buf)})) {
            return std::unexpected(error::wire_frame_too_large);
        }
    }

    // Append the pre-built body (35=…34=…49=…52=…56=…rest).
    if (pos + body_len > buf.size()) {
        return std::unexpected(error::wire_frame_too_large);
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
            return std::unexpected(error::wire_frame_too_large);
        }
    }

    return send_frame_built{.size = pos, .seq = seq};
}

// send_impl: the actual send pipeline, called from the noexcept wrapper above.
// May throw asio::system_error on cancellation of the store awaitable.
// Separated so the outer noexcept wrapper can catch and convert to expected_t.
// [F5 Round-A drift fix: noexcept-throw trap separation]
asio::awaitable<fixpp::core::expected_t<void>> Session::send_impl(
    std::span<const std::byte> app_payload, std::span<std::byte> frame_buf,
    bool& disconnect_required) {
    using fixpp::core::error;

    // fixpp#544 §2.5: the build is synchronous and calls no Application callback; toApp
    // below runs after it, over the frame it wrote into frame_buf.
    const auto built = build_send_frame_(app_payload, frame_buf);
    if (!built) {
        co_return std::unexpected(built.error());
    }
    const std::size_t pos = built->size;
    const seqnum_t seq = built->seq;

    // ── 019 T013: toApp inspection/veto (FR-006/007; US2 AC1/AC2; research D6) ──
    // Called AFTER the complete frame is built so the MessageView passed to toApp
    // contains all FIX fields including MsgType (now correctly at field-3).
    // Run BEFORE store_then_emit so a vetoed/aborted send is not stored or transmitted.
    // A veto does NOT consume the seqnum (assign_outbound called below, after this check).
    // [research D6; spec.md US2 AC1/AC2; FR-006/007; data-model.md INV-5]
    if (engine_.application != nullptr) {
        std::span<const std::byte> built_frame{frame_buf.data(), pos};
        auto cb_r = parse_and_dispatch_(built_frame, kSendParseArena, [&](auto& mv, auto& sid) {
            return engine_.application->toApp(mv, sid);
        });
        if (!cb_r) {
            if (cb_r.error() == fixpp::core::error::app_callback_threw) {
                (void)co_await close(close_mode::terminal);
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
            // gate-b/r2 FQ-1: commit-region producer site — set provenance flag here,
            // not classified by value at the Session::send caller.
            disconnect_required = is_persistent_retain_fatal(assign_r.error());
            co_return std::unexpected(assign_r.error());  // store_seqnum_overflow (I-8)
        }
        // The assigned value must equal the peeked seq (single-strand, no race).
        (void)assign_r;
    }

    // (4) store(outbound) BEFORE transport ([2e §4.1]).
    // Pass the stamped seqnum explicitly (RC#A: next_outbound_seq_ removed).
    // gate-b/r2 FQ-1: commit-region producer site — set provenance flag from the
    // store's own error before returning it un-coerced.
    auto emit_r = co_await store_then_emit(seq, std::span<const std::byte>(frame_buf.data(), pos));
    disconnect_required = !emit_r.has_value() && is_persistent_retain_fatal(emit_r.error());
    co_return emit_r;  // store's own error, un-coerced
}

// fixpp#544 §2.5 (R-10): a send that found the frame slot held. The buffer is a local of
// this separate coroutine because a coroutine frame's size is fixed at compile time: a
// buffer declared in Session::send or send_impl would size their frames on every call.
asio::awaitable<fixpp::core::expected_t<void>> Session::send_fallback_leaf_(
    bool tic, std::span<const std::byte> app_payload, bool& disconnect_required) {
    FIXPP_INBOUND_SPLIT_ENTRY(tic);
    std::array<std::byte, kSendFrameBytes> buf{};
    co_return co_await send_impl(app_payload, buf, disconnect_required);
}

fsm_state Session::state() const noexcept { return fsm_state_; }

// ── T039/T041 (US3): Liveness loop ───────────────────────────────────────────
//
// run_liveness_loop() — the heartbeat / test-request / unanswered-TR timer
// coroutine. Co_spawned (asio::detached) when the session first enters Active.
// Runs on the session executor; cancelled via the root cancellation slot when
// Session::close() fires.
//
// Algorithm (105, research D6 and data-model E-4, superseding 005 research D-8's
// 1 × HeartBtInt steps):
//   1. If HeartBtInt <= 0 → return immediately (all liveness disabled).
//   2. T = test_request_threshold, or 1.2 × HeartBtInt when unset, computed once.
//   3. Loop while liveness_may_originate_(): sleep once, to the earliest instant of the sleep
//      set (the body's comment states it), then act on the eligible instant reached:
//      a. a TestRequest pending, no inbound frame since it, and its give-up instant (the later
//         of 2T + 1 ns after the last inbound frame and T + 1 ns after its write; the owner's
//         fresh-window ruling), or a TestRequest that fell due, with one pending, still held
//         behind a resend answer strictly after three thresholds after the last inbound frame
//         (the owner's cap ruling) → the give-up: 105 R-1's record, Logout with Text,
//         Disconnected → stop;
//      b. else T of inbound silence → a TestRequest (its TestReqID becomes the pending one);
//      c. else HeartBtInt of outbound silence → a Heartbeat.
//      A TestRequest's TestReqID is answered by on_inbound_frame (the TestReqID rule of
//      research D5), which clears the pending one.
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
// Default: 30s if not set; HeartBtInt <= 0 disables.
asio::awaitable<void> Session::run_liveness_loop() noexcept {
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
    bool armed = false;
    try {
        co_await run_liveness_loop_body_(armed);
    } catch (...) {  // NOLINT(bugprone-empty-catch): the body handles its own; a cancellation
                     // met at this await leaves the release below to decide
    }
    // 106 (data-model E-3, the joiner row "teardown without close()"): the exit of a loop that
    // spawned or adopted the connection bound ends it. Past that point every exit of the body
    // is an ending (Disconnected, closing, a claim taken) or a cancellation (a teardown, close(),
    // a drain); a loop that leaves at its HeartBtInt-0 or no-clock guard, before that point,
    // leaves a bound a limited write spawned timing the connection's writes. Re-derive: each
    // co_return and catch of run_liveness_loop_body_ after its spawn point, read for the state
    // it leaves. A teardown with no close() that releases the loop's clock sleep
    // (Clock::cancel_sleeps()) reaches the bound only through this wake: the bound's own timer
    // is not registered with the clock.
    if (armed) {
        connection_bound_.loop_left = true;
        wake_connection_bound_();
    }
    // 105 (research D1 item 7): a detached root; its exit releases the connection-end latch,
    // while the loop is still counted.
    release_connection_end_();
}

asio::awaitable<void> Session::run_liveness_loop_body_(bool& armed) noexcept {
    using namespace std::chrono_literals;

    // [feedback_asio_cospawn_total_cancellation_default]: reset the coroutine's
    // cancellation filter to accept total cancellation. The root slot was bound
    // at co_spawn time (via bind_cancellation_slot), so this only changes the
    // filter — it does not reassign the slot's backing storage.
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});

    // Resolve HeartBtInt from config (D-8 default: 30s; 0 = disabled).
    std::chrono::seconds heartbt_int = kDefaultHeartBtInt;
    if (cfg_.heartbeat_interval.has_value()) {
        heartbt_int = *cfg_.heartbeat_interval;
    }

    // 105 (owner ruling on SI-2; data-model E-4): the loop is off at HeartBtInt <= 0.
    if (heartbt_int.count() <= 0) {
        co_return;
    }

    if (!effective_clock_) {
        co_return;  // No clock (should not happen post-open(), but guard).
    }

    // 105 (research D6; data-model E-4), superseding 005 research D-8's 1 × HeartBtInt: T is
    // the configured test_request_threshold, or 1.2 × HeartBtInt, computed once.
    const std::chrono::milliseconds threshold =
        cfg_.test_request_threshold.value_or(detail::default_test_request_threshold(heartbt_int));

    // 106 D3 (research D3, "The primitive"; data-model E-3): the connection bound, spawned once
    // per connection, here at the loop's start on the loop's executor unless a limited write
    // spawned it first, which the loop then adopts. A spawn that fails fails closed: the loop
    // takes the claim, records Disconnected, closes and ends.
    connection_bound_.threshold = threshold;
    if (!connection_bound_.running &&
        !spawn_connection_bound_(co_await asio::this_coro::executor)) {
        fail_closed_without_bound_();
        co_return;
    }
    armed = true;
    // 106 D3: while one of the loop's emits has not returned, the bound times it (emit_in_flight_),
    // so the give-up is decided at its instant whether or not the emit's write is blocked.
    // The mark is cleared explicitly at each normal exit of an emit (clear()), and by the
    // destructor only while an exception unwinds the frame: a frame destroyed unresumed (an
    // io_context torn down with no close()) touches nothing, as ending_write_unwind_guard.
    struct emit_in_flight_guard {
        Session& s;
        int unwinding_at = std::uncaught_exceptions();
        explicit emit_in_flight_guard(Session& session) noexcept : s{session} {
            s.emit_in_flight_ = true;
            s.wake_connection_bound_();
        }
        emit_in_flight_guard(emit_in_flight_guard const&) = delete;
        emit_in_flight_guard& operator=(emit_in_flight_guard const&) = delete;
        void clear() const noexcept {
            s.emit_in_flight_ = false;
            s.wake_connection_bound_();
        }
        ~emit_in_flight_guard() {
            if (std::uncaught_exceptions() > unwinding_at) {
                clear();
            }
        }
    };
    // Set when the give-up was decided (by the loop, or by the bound during an emit) and its
    // Logout is the loop's to write, after the loop below.
    bool write_give_up = false;

    // The pending TestRequest's two readings of the clock (data-model E-4). tr_issued_at: when its
    // TestReqID became the pending one, before its number, store and write, so "an inbound frame
    // since the TestRequest" (last_inbound_steady_ > tr_issued_at) counts a frame that arrives
    // during them. tr_written_at: when its store and write returned, the instant the give-up
    // counts T from (the owner's fresh-window ruling, spec.md Clarifications).
    fixpp::core::steady_time_point tr_issued_at = effective_clock_->steady_now();
    fixpp::core::steady_time_point tr_written_at = tr_issued_at;
    // Whether the Heartbeat and TestRequest instants are eligible: no TestRequest pending, or an
    // inbound frame since it. Read before each sleep and again on waking.
    const auto answerable_now = [this, &tr_issued_at]() noexcept {
        return pending_test_req_id_.empty() || last_inbound_steady_ > tr_issued_at;
    };

    // Liveness loop (data-model E-4, "One sleep per iteration"):
    //   - the Heartbeat instant is last_outbound + HeartBtInt; the TestRequest instant is
    //     last_inbound + T; both are eligible when no TestRequest is pending, or one is and an
    //     inbound frame arrived since it;
    //   - the give-up instant is the later of last_inbound + T + T + 1 ns and the pending
    //     TestRequest's write + T + 1 ns, every step saturating; it is eligible when a TestRequest
    //     is pending and no inbound frame arrived since it. The owner's fresh-window ruling
    //     (spec.md Clarifications), superseding the Phase 6 implementation
    //     clarification for a TestRequest held behind a resend answer: a TestRequest that falls
    //     due is written before any give-up, after the hold when one holds it, and the peer then
    //     has T from that write to answer. With T > 0 the TestRequest instant is at or before
    //     the give-up instant (equal only when both saturate at steady_time_point::max()),
    //     so while the TestRequest instant is eligible the sleep set holds that one;
    //   - the owner's cap ruling (spec.md Clarifications: the held TestRequest's cap): a
    //   TestRequest
    //     that falls due while a resend answer holds the gate waits for the answer's end, and,
    //     with a TestRequest pending, for at most its cap, last_inbound + T + T + T + 1 ns,
    //     saturating; at the cap, still held, it is given up there, naming the pending one;
    //   - each iteration sleeps once, to the earliest instant of the sleep set: the Heartbeat
    //     instant when eligible or not yet reached (a frame during the sleep can make it
    //     eligible, and nothing on the inbound path wakes the sleep), the TestRequest and the
    //     give-up instants when eligible. A Heartbeat instant reached while ineligible is left
    //     out, or every sleep would return at once;
    //   - on waking it acts on the eligible instant reached: the give-up (105 R-1), else the
    //     TestRequest, else the Heartbeat.
    try {
        // 105 (research D2 item 6; data-model E-4): the loop originates only while
        // liveness_may_originate_() holds (Active, and no ending Logout in progress), tested as the
        // loop condition and after every suspension.
        while (liveness_may_originate_()) {
            const auto set_now = effective_clock_->steady_now();
            const bool set_answerable = answerable_now();
            const auto set_hb = detail::deadline_after(last_outbound_steady_, heartbt_int);
            auto deadline = fixpp::core::steady_time_point::max();
            if (set_answerable || set_hb > set_now) {
                deadline = std::min(deadline, set_hb);
            }
            if (set_answerable) {
                deadline =
                    std::min(deadline, detail::deadline_after(last_inbound_steady_, threshold));
            } else {
                deadline = std::min(deadline, detail::test_request_give_up_instant(
                                                  last_inbound_steady_, tr_written_at, threshold));
            }

            // fixpp#536 (.specify/536-537-538-recovery-liveness.md, D-536-2): before each
            // sleep the loop leaves if the session is no longer Active, and then resets its
            // cancellation filter to total. Here the state test is the loop condition above;
            // nothing suspends between it and this sleep.
            //   The reset. close() emits total. The reset at the top of this function
            //   does not last: every emit below takes the sequence-number lock, and the
            //   lock sequence leaves the whole coroutine thread with a terminal filter
            //   (FIXPP_DETAIL_CO_AWAIT_LOCK, part 4, async_mutex.hpp). Whether the lock
            //   should restore its caller's filter is fixpp#583; this reset stands on
            //   what the lock does today.
            //   The state test. close() writes Disconnected and then emits, with no
            //   suspension between the two, and an emit that meets a terminal filter is
            //   not recorded. A loop that was inside an emit then finds the state
            //   changed when it reaches a sleep.
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
            // Sleep until that deadline (or until cancellation fires).
            co_await effective_clock_->sleep_until(deadline);

            // Check if we're still Active after the sleep.
            if (!liveness_may_originate_()) {
                co_return;
            }

            // Eligibility tested again on waking: a frame during the sleep changes it.
            const auto now = effective_clock_->steady_now();
            const bool answerable = answerable_now();
            const bool test_request =
                answerable && now >= detail::deadline_after(last_inbound_steady_, threshold);
            // 105, the owner's cap ruling (spec.md Clarifications: the held TestRequest's cap;
            // data-model E-4): a TestRequest that fell due, with one pending and an inbound frame
            // since it, and is still held behind a resend answer at its cap is given up there.
            // 106 D3 supersedes 105 T119b's implementation reading: capped with none pending too
            // (the owner's text: a TestRequest still held, not yet written, at L + 3T).
            const bool held_cap =
                test_request && replay_gate_.exclusive &&
                now >= detail::held_test_request_cap(last_inbound_steady_, threshold);
            const bool give_up =
                held_cap || (!pending_test_req_id_.empty() && !answerable &&
                             now >= detail::test_request_give_up_instant(last_inbound_steady_,
                                                                         tr_written_at, threshold));
            const bool heartbeat =
                answerable && now >= detail::deadline_after(last_outbound_steady_, heartbt_int);

            // 103 (research D5′.4): behind a resend answer the Heartbeat and the TestRequest wait
            // for its end and go back to the top, which recomputes every instant. 105, the owner's
            // cap ruling: with a TestRequest pending, that wait also ends at the held TestRequest's
            // cap, where the top gives up. The join's arms both complete without throwing, so the
            // loop tests its own cancellation after it.
            // 106 D3: the wait ends at the cap with none pending too.
            if (!give_up && (heartbeat || test_request) && replay_gate_.exclusive) {
                using namespace asio::experimental::awaitable_operators;
                (void)co_await (replay_gate_.wait_for_wake() ||
                                detail::await_deadline(*effective_clock_,
                                                       detail::held_test_request_cap(
                                                           last_inbound_steady_, threshold)));
                if ((co_await asio::this_coro::cancellation_state).cancelled() !=
                    asio::cancellation_type::none) {
                    co_return;
                }
                continue;
            }

            // The give-up: a TestRequest pending and the give-up instant reached, or the held cap.
            if (give_up) {
                // 105 R-1 (research D3; contract C-3; [FIX-SL §4.5.5]), superseding the
                // 536-537-538 note's R-6 ("Not chosen: … a Logout on the dead-peer ending"):
                // the slot-74 record first, so a write that never completes cannot lose it
                // (ruling D-e); then, under a replay-gate shared hold as this loop's other
                // emits take one, one Logout whose Text(58) names the TestRequest; then
                // Disconnected (the write half, after this loop).
                // 106 D3 supersedes 103 D5′.4 / 105 D3 (the loop alone decides the give-up): the
                // connection bound decides while the loop is suspended in its own emit, under one
                // claim (D11); both decide through decide_give_up_(). With the claim held
                // elsewhere this block records nothing and exits (N-12); with 105's ending bound
                // refused, decide_give_up_() has closed and no Logout is written.
                if (!decide_give_up_(threshold, co_await asio::this_coro::executor) ||
                    give_up_.closed) {
                    co_return;
                }
                write_give_up = true;
                break;
            } else if (test_request) {
                // 106 D3 bounds the inline emits of .specify/536-537-538-recovery-liveness.md §3:
                // they stay inline, timed by the connection bound while they have not returned.
                // 103 (research D5′.4): a shared hold per emit, taken with the gate open (the wait
                // above); it covers the TestReqID's assignment and the emit.
                const detail::replay_gate::shared_hold tr_hold{replay_gate_};
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
                std::string_view const tr_id{id_buf.data(),
                                             static_cast<std::size_t>(end - id_buf.data())};

                // T042 (US4 retirement): emit the TestRequest frame via
                // store_then_emit (I-3 outbound half). Stack buffer; noexcept.
                std::array<std::byte, 256> tr_buf{};
                const auto st52 =
                    stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
                const seqnum_t tr_seq = seqnum_mgr_.peek_outbound();
                auto tr_result = fixpp::session::build_test_request(
                    std::span<std::byte>{tr_buf.data(), tr_buf.size()}, tr_seq, cfg_.sender_comp_id,
                    cfg_.target_comp_id, tr_id, cfg_.begin_string, st52.value);
                // 105 (contract C-1 row 8): a TestRequest that does not fit its buffer ends the
                // session as the loop's other emission failures do. 106 D4 supersedes 105 C-1 row
                // 8's stated reason (a give-up the bound decides names a TestRequest whose write
                // did not complete); the rule stays: such a TestRequest is never pending.
                if (!tr_result) {
                    record_disconnected_unless_already_();
                    co_return;
                }
                // Its TestReqID is the pending one from here, before the first suspension (the
                // number's lock), so an answer during the number, the store or the write clears it.
                pending_test_req_id_.assign(tr_id);
                tr_issued_at = now;
                // 106 D4 supersedes 105 E-4's tr_written_at count for an emit not returned (ruling
                // R1; claim-ok: R1 names a Gate A ruling): from here until the emit returns the
                // bound times it, its instant L + 3T + 1 ns.
                emit_in_flight_guard const in_flight{*this};
                // 019 T014: toAdmin before TestRequest. [FR-008/010]
                // A throw → terminal-close + co_return.
                if (!fire_to_admin_(*tr_result)) {
                    in_flight.clear();
                    record_state_transition_(fsm_state::Disconnected);
                    co_return;
                }
                auto assign_r = co_await seqnum_mgr_.assign_outbound();
                // 106 D3: a give-up the bound decided during the emit takes the loop's write half.
                if (ending_claim_ == ending_claim::give_up) {
                    in_flight.clear();
                    if (give_up_.closed) co_return;
                    write_give_up = true;
                    break;
                }
                if (!assign_r) {
                    in_flight.clear();
                    // Overflow or closed: session-fatal per 005 data-model.md §E3
                    // (Sequence-number state). Liveness loop is fire-and-forget (no expected_t
                    // return): log by transitioning to Disconnected and stopping the loop.
                    record_disconnected_unless_already_();
                    co_return;
                }
                auto emit_r = co_await store_then_emit(tr_seq, *tr_result);
                in_flight.clear();
                if (ending_claim_ == ending_claim::give_up) {
                    if (give_up_.closed) co_return;
                    write_give_up = true;
                    break;
                }
                if (!emit_r) {
                    // 106 D11: a cut write or another ending's close returns here (FR-017).
                    record_disconnected_unless_already_();
                    co_return;
                }
                tr_written_at = effective_clock_->steady_now();
            } else if (heartbeat) {
                // 103 (research D5′.4): as the TestRequest's.
                const detail::replay_gate::shared_hold hb_hold{replay_gate_};
                // 106 D4: the Heartbeat emit not returned is timed by the bound, as the
                // TestRequest's (data-model E-4 row 3).
                emit_in_flight_guard const in_flight{*this};
                std::array<std::byte, 256> hb_buf{};
                const auto st52_hb =
                    stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
                const seqnum_t hb_seq = seqnum_mgr_.peek_outbound();
                auto hb_result = fixpp::session::build_heartbeat(
                    std::span<std::byte>{hb_buf.data(), hb_buf.size()}, hb_seq, cfg_.sender_comp_id,
                    cfg_.target_comp_id, {}, cfg_.begin_string, st52_hb.value);
                if (hb_result) {
                    // 019 T014: toAdmin before liveness Heartbeat. [FR-008/010]
                    // A throw → terminal-close + co_return.
                    if (!fire_to_admin_(*hb_result)) {
                        in_flight.clear();
                        record_state_transition_(fsm_state::Disconnected);
                        co_return;
                    }
                    auto assign_r = co_await seqnum_mgr_.assign_outbound();
                    if (ending_claim_ == ending_claim::give_up) {
                        in_flight.clear();
                        if (give_up_.closed) co_return;
                        write_give_up = true;
                        break;
                    }
                    if (!assign_r) {
                        in_flight.clear();
                        record_disconnected_unless_already_();
                        co_return;
                    }
                    auto emit_r = co_await store_then_emit(hb_seq, *hb_result);
                    in_flight.clear();
                    if (ending_claim_ == ending_claim::give_up) {
                        if (give_up_.closed) co_return;
                        write_give_up = true;
                        break;
                    }
                    if (!emit_r) {
                        // 106 D11: a cut write or another ending's close returns here (FR-017).
                        record_disconnected_unless_already_();
                        co_return;
                    }
                }
                in_flight.clear();
            }

            // After an emit, or with no eligible instant reached, the loop sleeps again.
        }
        // 106 D3 (data-model E-3, "The write half stays in the loop"): the give-up decided (by the
        // block above, or by the connection bound while an emit had not returned), its Logout
        // written here under a shared hold once the replay gate is open, then Disconnected. The
        // Text and the owner are the decision's (decide_give_up_()).
        if (write_give_up) {
            std::string_view const text{give_up_.text_buf.data(), give_up_.text_len};
            bool const owner_claimed = give_up_.owner_claimed;
            while (replay_gate_.exclusive) {
                co_await replay_gate_.wait_for_wake();
                if (fsm_state_ != fsm_state::Active || ending_wait_.cut) {
                    break;
                }
            }
            // A give-up the write limit cut, or whose session another root ended, while
            // the walk still holds the gate: no shared hold and no Logout ("Logout not
            // sent"), then Disconnected.
            if (replay_gate_.exclusive) {
                if (owner_claimed) {
                    (void)leave_ending_write_(false, false);
                }
                record_disconnected_unless_already_();
                co_return;
            }
            {
                const detail::replay_gate::shared_hold lo_hold{replay_gate_};
                claim_peer_logout_none_();
                const auto st52 =
                    stamp_sending_time(*effective_clock_, cfg_.sending_time_precision);
                // What the leaf writes or refuses is its own; the ending is recorded
                // below either way (contract C-3.2).
                // With the owner held by another Logout, the leaf would write none.
                if (owner_claimed) {
                    (void)co_await inbound_logout_leaf_(true, st52.value, text, ending_kind::r1);
                }
            }
            record_disconnected_unless_already_();
            co_return;
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
    // 105 (data-model E-3): a give-up that entered writing at its decision and left the loop by
    // an exception (a cancellation of its replay-gate wait) leaves the ending state machine.
    if (ending_wait_.phase == ending_wait_state::phase_t::writing &&
        ending_wait_.kind == ending_kind::r1) {
        (void)leave_ending_write_(false, false);
    }
}

// ── US4 T046: store_then_emit ─────────────────────────────────────────────────
//
// Durable-before-transmit (I-3 outbound half):
//   1. If store_ is available: co_await store_->store(stamped_seq, frame, outbound).
//   2. AFTER store returns: call transport_send_(frame) if non-null.
// What a store() call that did not succeed ends as:
//   - A cancelled call, whether the store returned store_cancelled or let
//     operation_aborted out, on any store: the frame is not transmitted, its number is
//     taken back, Disconnected is recorded here unless the caller's Logon arm is
//     superseded, and the function leaves by asio::system_error{operation_aborted}.
//     Rulings R-15 and R-16 (.specify/536-537-538-recovery-liveness.md §10, D-OUT-1 to
//     D-OUT-3), superseding decision D7 of feature 059
//     (specs/059-outbound-store-fail-closed/research.md), under which a returned
//     store_cancelled was transmitted over.
//   - Any other returned error on a persistent store: fail closed (059). The number is
//     reconciled from the store and the error is returned, with nothing transmitted.
//   - Any other returned error on a store that is not persistent, and an exception of
//     the store's body that is not a cancellation: logged-then-proceed (I-07).
// A frame whose number is no longer below the session's next outbound number when its
// store call has returned is not transmitted either (D-OUT-10, ruling R-19).
// stamped_seq: the MsgSeqNum already written into `frame` by the builder — passed
//   explicitly (RC#A: next_outbound_seq_ removed; RC#B: transport errors surfaced).
// [gate-b/r1-green: RC#A removes next_outbound_seq_ - 1U arithmetic;
//  gate-b/r1-green: RC#B surfaces transport throws as dispatch_aborted]
asio::awaitable<fixpp::core::expected_t<void>> Session::store_then_emit(
    seqnum_t stamped_seq, std::span<const std::byte> frame, std::optional<fsm_state> arm,
    bool transmit, wire_reset_exemption exemption, bool* transmitted, write_class wclass) {
    // 034 T006 (C2 / R4): credential redaction at the single store boundary.
    // Mask the Password(554) value in a PRIVATE copy before it is persisted; the
    // wire path (Step 2) transmits the caller's ORIGINAL unmasked `frame`.
    //
    // Gate order matters (R4): cheap 554-presence scan FIRST — the overwhelming
    // majority of outbound frames carry no 554, so they take the no-copy / no-
    // alloc path and `span_to_store` stays == `frame` (byte-identical, FR-007).
    // Only if a genuine 554 is present do we confirm MsgType(35)=="A". That
    // MsgType=A gate is the LOAD-BEARING safety boundary, not belt-and-suspenders:
    // admin Logon frames are folded into a SequenceReset-GapFill on resend (never
    // replayed verbatim), so the masked stored copy can never reach the wire;
    // app frames ARE replayed verbatim from stored bytes, so masking a non-admin
    // 554 would put the mask on the wire on resend → peer desync. Masking is a
    // synchronous transform that completes BEFORE the co_await store, so I-3
    // (durable-before-transmit) and the noexcept/cancellation handling below are
    // unchanged. [FR-001/002/006/009; INV-034-1/2/3/5; data-model E2; research R4]
    std::span<const std::byte> span_to_store = frame;  // default: today's behavior
    bool skip_store = false;
    std::array<std::byte, kMaxMaskableLogonBytes> mask_buf{};  // coroutine-frame copy
    const fixpp::wire::dict_hooks frame_hooks = session_hooks(inbound_tv_);
    if (fixpp::session::frame_has_genuine_tag554(frame, frame_hooks) &&
        scan_frame_header(frame, frame_hooks).msg_type == "A") {
        if (frame.size() > kMaxMaskableLogonBytes) {
            // Over-bound: FAIL CLOSED — never persist cleartext. Skip the store
            // write for this frame (logged-then-proceed, I-07, mirroring the
            // existing store-error absorption below); the original frame is still
            // transmitted in Step 2. Wire-safe: a skipped 35=A store is wire-
            // identical to a stored one (admin→GapFill on resend, session.cpp
            // resend store-walk). Production-UNREACHABLE — kMaxMaskableLogonBytes
            // == build_logon's logon_buf (emit_initiator_logon_) / reply_buf (on_inbound_frame)
            // and build_logon already fails-closed (wire_frame_too_large) above
            // it; the T007 open()-time credential-length guard adds config-time
            // defense for both roles. This branch is reachable ONLY via the
            // test-only session_test_access::store_then_emit seam (T010 earns its
            // BRDA by injecting a hand-crafted >256-byte 35=A frame; see plan.md
            // ## Gate A deviation #1). [C2 step-2 / R3 / I-07;
            // [[feedback_symmetric_api_claim_unreachable_arm]]]
            skip_store = true;
        } else {
            std::memcpy(mask_buf.data(), frame.data(), frame.size());
            (void)fixpp::session::mask_tag554_same_length_inplace(
                std::span<std::byte>{mask_buf.data(), frame.size()}, frame_hooks);
            span_to_store = std::span<const std::byte>{mask_buf.data(), frame.size()};
        }
    }

    // Step 1: store (durable-before-transmit).
    if (store_ && !skip_store) {
        // 104 (contract RR-4, "By construction"): once the receipt marker is `reached` (a
        // reset point has run, or close() closed a receipt out), no new store call is issued:
        // the number is taken back, as D-OUT-1 does for a cancelled call, and the call ends
        // as one does, dispatch_aborted with no frame. A call issued before the marker became
        // `reached` was not tested here and runs to its end (RR-2 item 5).
        if (logout_seen_ == receipt_marker::reached) {
            seqnum_mgr_.take_back_outbound(stamped_seq);
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
        // The try takes what the store awaitable lets out: operation_aborted, when a
        // cancellation is met before the store's lock, and any exception of a store's
        // body.
        // 059 T006/T010: disposition on the returned store_r (contracts/
        // store-then-emit-disposition.md item 3). `fatal_err` is set ONLY on a
        // genuine failure (not store_cancelled) on a persistent store
        // (store_is_persistent_); success and volatile-store failures fall through
        // unchanged (logged-then-proceed, I-07/FR-003).
        // D-OUT-2: a cancelled store call only sets `store_call_cancelled`, at its
        // returned timing in the try and at its thrown timing in the handler. The block
        // after the try is the one ending of both.
        std::optional<fixpp::core::error> fatal_err;
        bool store_call_cancelled = false;
        try {
            auto store_r =
                co_await store_->store(stamped_seq, span_to_store, direction_t::outbound);
            // Ahead of the persistence gate: the ending is the same for any store.
            if (!store_r.has_value() && store_r.error() == fixpp::core::error::store_cancelled) {
                store_call_cancelled = true;
            } else if (!store_r.has_value() && store_is_persistent_) {
                // Genuine persistent-store failure: capture the error FIRST
                // (NEW-P3, D2) — before the best-effort reconcile read — so a
                // reconcile-time throw cannot pre-empt the intended error code.
                // The reconcile runs in its OWN nested try/catch: a throw there
                // must be absorbed WITHOUT escaping to the outer catch(...) below,
                // which would otherwise fall through to Step 2 and transmit the
                // un-retained frame (fail-closed violation). [D2/D4/D7]
                fatal_err = store_r.error();
                try {
                    auto dk = co_await store_->next_seqnum(direction_t::outbound, false);
                    if (dk.has_value()) {
                        (void)co_await seqnum_mgr_.set_next_outbound(*dk);
                    }
                } catch (...) {  // NOLINT(bugprone-empty-catch) — reconcile is
                    // best-effort (D4); reconcile failure does not change the
                    // already-captured fatal_err, and the disconnect still proceeds.
                }
            }
            // else: success, a store_cancelled noted above, or a volatile-store
            // failure (FR-003), which is logged-then-proceed, unchanged.
        } catch (const asio::system_error& e) {
            if (e.code() == asio::error::operation_aborted) {
                // The thrown timing of a cancelled store call: the cancellation was met
                // before the store held its lock.
                store_call_cancelled = true;
            }
            // Non-abort system_error: absorb (I-07 logged-then-proceed on store path).
        } catch (...) {  // NOLINT(bugprone-empty-catch) — any other exception from the
            // store awaitable is absorbed (I-07).
        }
        if (store_call_cancelled) {
            // D-OUT-1 to D-OUT-3. The call stored nothing (the store contract's sentence
            // on store_cancelled, message_store.hpp), so the frame's number is taken
            // back, with no await. The function leaves by the exception the thrown timing
            // carried, so that a caller that discards results cannot go on past a
            // cancelled store. It does not reset the thread's cancellation state: that is
            // the entry point's, after its handler (D-CAN-3, D-OUT-9, D-OUT-5).
            seqnum_mgr_.take_back_outbound(stamped_seq);
            disconnect_unless_superseded_(arm);
            throw asio::system_error(asio::error::operation_aborted);
        }
        if (fatal_err.has_value()) {
            // Fail closed BEFORE Step 2 transmit (D3): same shape as the existing
            // transport-write-failure return in `send_impl` (co_return emit_r) — no internal
            // record_state_transition_; the Disconnected transition is caller-owned.
            // [contracts/store-then-emit-disposition.md item 3;
            //  feedback_mirror_existing_failclosed_disposition]
            co_return std::unexpected(*fatal_err);
        }
        // D-OUT-10 (ruling R-19). A caller passes a number it assigned, which stays below
        // the next outbound number unless a writer lowered that number while this frame
        // was in its store call (the writers: `grep -n 'next_outbound_'
        // include/fixpp/session/seqnum_manager.hpp src/session/seqnum_manager.cpp`). Such
        // a frame is not transmitted. The test follows 059's return above, so that a
        // frame the store refused keeps 059's code; it is inside this branch, so that a
        // frame sent with no store call is not tested; and it is ahead of the superseded
        // test below. It returns a value: this frame's own store call was not cancelled.
        // 104 (contract RR-6 item 3, loop 5's N-1 ruling): while a reset point has run on this
        // connection, a frame refused here ends as one held back at the transmit does, with
        // success and nothing transmitted, before the time of the last outbound frame;
        // fixpp's own Logout in a crossing carries the exemption and is transmitted (RR-6
        // item 4). While wire_closed_by_reset_ is clear, the refusal keeps dispatch_aborted:
        // the lowerings that need no reset point (a reconcile, a take-back) are R-19's.
        // The other lowering writers, read against "phase 1 has numbered its Logout":
        // `git grep -n -E
        // 'seqnum_mgr_\.(take_back_outbound|hydrate|reset_to_one|set_next_outbound)\(|reset_seqnums_to_one_durable\(|run_reset_unit_\(|run_receipt_reset_unit_\('
        // -- src/session/session.cpp`.
        if (stamped_seq >= seqnum_mgr_.peek_outbound()) {
            if (!wire_closed_by_reset_) {
                co_return std::unexpected(fixpp::core::error::dispatch_aborted);
            }
            if (exemption != wire_reset_exemption::crossing_logout) {
                co_return fixpp::core::expected_t<void>{};
            }
        }
    }

    // 093 plan OD-25: a Logon arm's caller passes its expected state. The store await
    // above may have yielded the strand; a close() or Engine::stop()'s step 1 that ran
    // there stops the transmit. The caller's own predicate check follows.
    if (arm && logon_arm_superseded_(*arm)) {
        co_return fixpp::core::expected_t<void>{};
    }

    // fixpp#590 (`.specify/563-564-callback-send.md` §2.6 "The mechanism",
    // statement 3): the caller decided, before the store call, that this frame is
    // stored and not transmitted. The test sits after the arm test above and before
    // Step 2, so the time of the last outbound frame is not updated either.
    if (!transmit) {
        co_return fixpp::core::expected_t<void>{};
    }

    // Step 2: transmit (ONLY after store completes — I-3).
    //
    // FQ-A (gate-b/r2): for a LIVE transport, route through live_write_serialized_()
    // which acquires write_gate_ across the async_write completion, holds a
    // shared_ptr<Transport> keepalive, and returns an error if the write fails.
    // This satisfies three invariants simultaneously:
    //   (a) serialization: write_gate_ ensures ≤1 async_write in-flight
    //       (transport.hpp's [2h §4.1] RC#3 in-flight contract);
    //   (b) error propagation: write error → dispatch_aborted → caller disconnects;
    //   (c) lifetime safety: shared_ptr keepalive prevents UAF (Q-1 fix).
    // [transport.hpp's [2h §4.1] RC#3 in-flight contract; FQ-A D-6; realized-behavior.md C1/C2]
    //
    // Pre-live (config-time transport_send_): sync std::function set from
    // cfg_.transport_send at open() — used by direct-Session tests.
    {
        if (live_transport_shared_()) {
            // Live path: serialized write through write_gate_.
            auto write_r = co_await live_write_serialized_(frame, exemption, wclass);
            if (!write_r.has_value()) {
                // 106 (research D5, N-3): a write the limit cut keeps session_write_timeout;
                // every other live-write failure stays dispatch_aborted.
                co_return std::unexpected(write_r.error() ==
                                                  fixpp::core::error::session_write_timeout
                                              ? fixpp::core::error::session_write_timeout
                                              : fixpp::core::error::dispatch_aborted);
            }
            // 104 (contract RR-6 item 3): held back after a reset point; success with no
            // transmit, as the transmit=false exit returns.
            if (*write_r == live_write_outcome::held_back) {
                co_return fixpp::core::expected_t<void>{};
            }
            // 105 (research D2 item 1): the live transport accepted the frame.
            if (transmitted != nullptr) {
                *transmitted = true;
            }
        } else if (transport_send_) {
            // 104 (contract RR-6 item 3): the config-time sink's site of ruling W's test.
            if (wire_closed_by_reset_ && exemption != wire_reset_exemption::crossing_logout) {
                co_return fixpp::core::expected_t<void>{};
            }
            // Config-time sync sink (pre-live / test path).
            try {
                transport_send_(frame);
            } catch (const asio::system_error&) {
                co_return std::unexpected(fixpp::core::error::dispatch_aborted);
            } catch (...) {
                co_return std::unexpected(fixpp::core::error::dispatch_aborted);
            }
            // 105 (research D2 item 1): the direct sink accepted the frame.
            if (transmitted != nullptr) {
                *transmitted = true;
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
            // 104 (contract RR-5 item 6): reached after close()'s seqnum_mgr_.drain().
            record_disconnected_unless_already_();
            co_return std::unexpected(sr.error());
        }
        // If this completes an outstanding resend span, leave recovery.
        if (reconnect_fsm_.is_awaiting_resend()) {
            reconnect_fsm_.exit_awaiting_resend();
        }
        // 103 (research D4.3): N may have passed a too-high Logout's S.
        note_inbound_advance_();
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
                record_disconnected_unless_already_();
                co_return std::unexpected(assign_r.error());
            }
            // 036 T012: ARM-1 — toAdmin observation before transmit (FR-001/FR-002).
            if (!fire_to_admin_(*rj_result)) {
                record_state_transition_(fsm_state::Disconnected);
                co_return std::unexpected(fixpp::core::error::app_callback_threw);
            }
            auto emit_r = co_await store_then_emit(rj_seq, *rj_result);
            // What store_then_emit returns and when it throws: its declaration in session.hpp.
            (void)emit_r;
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
//      Ruling R-17 (.specify/536-537-538-recovery-liveness.md §10, D-OUT-6), superseding
//      "store-side errors: logged-then-proceed" for this emit: when the Logout was not
//      sent, whatever the reason, the phase records Disconnected (when the state is not
//      that already), returns the error at once and skips steps 2 to 4. A reply cannot
//      come to a Logout that was not sent.
//   2. Transition FSM to LogoutSent.
//   3. Wait up to cfg_.logout_disconnect_timeout_ms on the session's clock for the
//      peer's confirming Logout. fixpp#536 (.specify/536-537-538-recovery-liveness.md,
//      D-536-3), superseding the bare Clock::sleep_until that the LogoutSent arm ended
//      with Clock::cancel_sleeps(): the wait is a race between this session's own
//      wake (logout_wait_wake_, which the LogoutSent arm calls) and
//      detail::await_deadline, which re-arms on a sweep of the clock that is not its
//      own cancellation.
//   4. If confirmed → ok.
//      If timeout → transition to Disconnected, return session_logout_timeout.
//
// The cut route (fixpp#590; `.specify/563-564-callback-send.md` §2.6 "The
// mechanism", rulings R-8 and R-9) changes stage 1 and replaces stages 2 to 4 when
// a terminal close() was started inside the Logout's toAdmin, which stage 1 calls
// before the Logout takes its number. Then the step writes Disconnected at once,
// then numbers the Logout and stores it as in stage 1, where the session has a
// store, with its transmit off, and returns without LogoutSent and without the
// sleep.
//
// Called from close(graceful) phase 1 ONLY. The root cancellation_state fires
// phase-2 total AFTER this coroutine returns (either path).
//
// [feedback_asio_cospawn_total_cancellation_default]: this coroutine is called
// via co_await from close(), which disabled cancellation on its coroutine first;
// the root slot does not reach it (see `root_cancel_`; cell RS-1). We do NOT reset here — the
// caller (close) owns the cancellation state. The wait for the confirmation is step 3's race
// (D-536-3).
asio::awaitable<fixpp::core::expected_t<void>> Session::run_logout_phase1() noexcept {
    using namespace std::chrono_literals;
    using std::chrono::seconds;

    // 103 (research D5′.4; FR-014): behind a resend answer the Logout waits for its end, before
    // its stamp and its peek. After a wake, a state that is no longer Active or LogonReceived, or
    // the caller's own cancellation (the grace timer winning the race), takes D-OUT-6's "not
    // sent" exit below.
    bool gate_refused = false;
    while (replay_gate_.exclusive) {
        if ((co_await asio::this_coro::throw_if_cancelled()) &&
            (co_await asio::this_coro::cancellation_state).cancelled() !=
                asio::cancellation_type::none) {
            gate_refused = true;
            break;
        }
        co_await replay_gate_.wait_for_wake();
        if ((co_await asio::this_coro::cancellation_state).cancelled() !=
                asio::cancellation_type::none ||
            (fsm_state_ != fsm_state::Active && fsm_state_ != fsm_state::LogonReceived)) {
            gate_refused = true;
            break;
        }
    }
    if (gate_refused) {
        if (fsm_state_ != fsm_state::Disconnected) {
            record_state_transition_(fsm_state::Disconnected);
        }
        co_return std::unexpected(fixpp::core::error::dispatch_aborted);
    }
    // 103 (research D4.6): the Logout owner, tested and set before the stamp and the peek. A
    // Logout already in flight is the session's one Logout: phase 1 sends none.
    if (logout_in_flight_) {
        co_return fixpp::core::expected_t<void>{};
    }
    logout_in_flight_ = true;
    // The shared hold: from here, before the stamp and the peek, until store_then_emit returns.
    std::optional<detail::replay_gate::shared_hold> gate_hold;
    gate_hold.emplace(replay_gate_);

    // Build the Logout frame into a stack buffer.
    std::array<std::byte, 256> buf{};
    const auto st52 = effective_clock_
                          ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                          : SendingTimeStamp{};
    const seqnum_t logout_seq = seqnum_mgr_.peek_outbound();
    auto logout_result = fixpp::session::build_logout(
        std::span<std::byte>{buf.data(), buf.size()}, logout_seq, cfg_.sender_comp_id,
        cfg_.target_comp_id, {}, cfg_.begin_string, st52.value);

    // D-OUT-6: what became of the Logout. It holds a value only when store_then_emit
    // returned success for it, which on the cut route (below) it does with its transmit
    // skipped.
    fixpp::core::expected_t<void> sent = std::unexpected(fixpp::core::error::dispatch_aborted);
    // fixpp#590 (`.specify/563-564-callback-send.md` §2.6 "The mechanism", ruling R-8):
    // set below when a terminal close was started inside this Logout's toAdmin. The
    // Logout is then cut: numbered, stored where the session has a store, and not
    // transmitted.
    bool cut = false;
    if (!logout_result) {
        // Build failure (unlikely): no call is made, and the builder's error is the
        // outcome.
        sent = std::unexpected(logout_result.error());
    } else {
        // Emit the Logout frame (store first, then transport_send — I-3).
        // 019 T014: toAdmin before graceful-close Logout. [FR-008/010]
        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
        // §2.6, statement 2: the count of terminal closes that have taken close()'s wait
        // is read here and again when toAdmin has returned. fire_to_admin_ is a plain
        // call, so nothing suspends between the two reads. The test is "moved", not
        // "non-zero". A toAdmin that throws leaves by the return below, before the
        // second read.
        const std::uint64_t waited_before = waited_terminal_closes_;
        if (!fire_to_admin_(*logout_result)) {
            record_state_transition_(fsm_state::Disconnected);
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        cut = waited_terminal_closes_ != waited_before;
        if (cut) {
            // §2.6, statement 4, "The record": written at once, with no suspension
            // since toAdmin returned, so that the terminal close reads it when its
            // turn on the session's executor comes. It is never cleared.
            logout_cut_ = true;
            // §2.6, statement 4, "The state, at the decision point": Disconnected is
            // written here, before the Logout takes its number and before its store
            // call, so that nothing that tests the state while that call is in flight
            // finds Active. onLogout runs by this write, on the edge out of Active.
            // The outbound callback depth is zero here, because the raise around
            // toAdmin ends before fire_to_admin_ returns. assign_outbound() and
            // store_then_emit with no arm do not read the state, so the Logout is
            // still numbered and stored.
            record_state_transition_(fsm_state::Disconnected);
        }
        auto assign_r = co_await seqnum_mgr_.assign_outbound();
        if (!assign_r) {
            // Overflow or closed: session-fatal per 005 data-model.md §E3 (Sequence-number state).
            // Abort logout, force-disconnect with propagated error.
            // §2.6, statement 4, "The exit": on the cut route the state is Disconnected
            // already, and record_state_transition_ adds an entry to the state history
            // on every call, so the write is made only when the state is not that
            // already.
            if (fsm_state_ != fsm_state::Disconnected) {
                record_state_transition_(fsm_state::Disconnected);
            }
            co_return std::unexpected(assign_r.error());
        }
        // The handler takes any exception out of the emit: store_then_emit's, for a
        // cancelled store call, among them. `sent` then keeps dispatch_aborted. Nothing
        // is awaited in the handler, nor after it on the path that leaves early.
        try {
            // 104 (contract RR-6 item 4): the exemption's one origin, fixpp's own Logout in
            // a crossing. Census by category: `git grep -n 'crossing_logout' -- src include`
            // (the declaration; this origin; the pass-through to live_write_serialized_; the
            // comparisons, D-OUT-10's lift and the sink test in store_then_emit, the grant
            // test in live_write_serialized_).
            sent =
                co_await store_then_emit(logout_seq, *logout_result, std::nullopt,
                                         /*transmit=*/!cut, wire_reset_exemption::crossing_logout,
                                         nullptr, write_class::logout);
        } catch (...) {  // NOLINT(bugprone-empty-catch): the outcome is in `sent`
        }
    }
    gate_hold.reset();

    // D-OUT-6: the Logout was not sent, so no reply to it can come. The phase leaves at
    // once and arms no wait; close() goes on to its terminal part. Ruling R-24's "a Logout
    // that was not sent is not a Logout" for reset_on_logout is subsumed by 104: no Logout
    // fixpp sends, sent or not, changes the receipt marker (data-model RO-2).
    if (!sent) {
        if (fsm_state_ != fsm_state::Disconnected) {
            record_state_transition_(fsm_state::Disconnected);
        }
        co_return sent;
    }

    // The cut route's exit (fixpp#590; `.specify/563-564-callback-send.md` §2.6 "The
    // mechanism", statement 4, "The exit"; ruling R-8). A Logout that was stored and not
    // transmitted is not a Logout that was sent, and no reply to it can come. So the
    // step leaves at once: it does not record LogoutSent, arms no wait and posts nothing.
    // The write of Disconnected is kept for a state that is not that already; on this
    // route the decision point above made it. close() discards the value returned.
    if (cut) {
        if (fsm_state_ != fsm_state::Disconnected) {
            record_state_transition_(fsm_state::Disconnected);
        }
        co_return std::unexpected(fixpp::core::error::dispatch_aborted);
    }

    // 104-sequence-reset-fidelity (FR-002, data-model RO-2) supersedes 024 T013's site (a)
    // and 024 contracts/reset-knobs.md C3.1's sent direction by name: the Logout this phase
    // sends no longer marks logout_seen_, so a graceful close resets under reset_on_logout
    // only when the peer's Logout reaches its reset point (arm L), and an unanswered one
    // makes no reset.

    // 103 (research D4.6): an inbound ending recorded Disconnected while this Logout was in
    // its store call, so it was that ending's reply: no LogoutSent and no wait for one.
    if (fsm_state_ == fsm_state::Disconnected) {
        co_return fixpp::core::expected_t<void>{};
    }

    // Transition to LogoutSent.
    record_state_transition_(fsm_state::LogoutSent);
    logout_confirmed_ = false;

    if (!effective_clock_) {
        // No clock (should not happen post-open): force-disconnect immediately.
        record_state_transition_(fsm_state::Disconnected);
        co_return std::unexpected(fixpp::core::error::session_logout_timeout);
    }

    // Wait until the configurable graceful-close timeout (cfg_.logout_disconnect_timeout_ms)
    // for the peer's confirming Logout. Default 2000 ms (matching QuickFIX/J
    // SessionState.logoutTimeoutMs). Wired from SessionConfig per FR-008 / RC#D.
    //
    // fixpp#536 (D-536-3): the wait is a race between a wake of this session's own and
    // the deadline.
    //   - `confirmed` never expires by itself. The LogoutSent arm cancels it through
    //     logout_wait_wake_, and as_tuple turns that cancel into the arm's completion.
    //   - The deadline arm is await_deadline, so a Clock::cancel_sleeps() by anyone
    //     re-arms it and does not read as a logout timeout.
    //   - The outcome is read from logout_confirmed_ after the race, never from which
    //     arm won: a cancelled wake completes as a fired one does.
    //   - The wake is cleared on every exit from the wait, a throw and the destruction
    //     of this frame included, so the LogoutSent arm cannot call into a wait that
    //     has ended.
    //   - The race sits inside a try. When this coroutine was cancelled while its
    //     Logout emit was in flight and the emit still succeeded, the co_await of the
    //     race throws operation_aborted before either arm starts, and the exits of the
    //     wait below must still run.
    auto deadline = effective_clock_->steady_now() +
                    std::chrono::milliseconds{cfg_.logout_disconnect_timeout_ms};
    {
        using namespace asio::experimental::awaitable_operators;
        asio::steady_timer confirmed{co_await asio::this_coro::executor,
                                     asio::steady_timer::time_point::max()};
        struct wake_clear {
            std::function<void()>* wake;
            ~wake_clear() { *wake = nullptr; }
        } const clear_on_exit{&logout_wait_wake_};
        logout_wait_wake_ = [&confirmed] { confirmed.cancel(); };
        try {
            (void)co_await (confirmed.async_wait(asio::as_tuple(asio::use_awaitable)) ||
                            detail::await_deadline(*effective_clock_, deadline));
        } catch (const std::system_error&) {  // NOLINT(bugprone-empty-catch) — this coroutine's
                                              // own cancellation, met at the co_await above. The
                                              // `logout_confirmed_` test after this block is the
                                              // control-flow decision.
        } catch (...) {  // NOLINT(bugprone-empty-catch) — noexcept-window absorption per FR-15.
            // Any other exception: absorb (noexcept window).
        }
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
// Extracted from the inline ResendRequest-reply walk that used to live in on_inbound_frame.
// Replays [begin, requested_end] (or through-current when end_is_through_current)
// using the same two-value end model as the original inline block.
//
// TWO-VALUE END MODEL (data-model I-NEX-3, research D-5, contracts C3):
//   eff_end = (end_is_through_current || (our_last > 0 && requested_end > our_last))
//                 ? our_last : requested_end
//   empty/short-store GapFill NewSeqNo =
//               end_is_through_current ? peek_outbound() : (requested_end + 1)
//
// 104 (research D3.3a; contract U6; data-model "`replay_outbound_range_`: the end model")
// supersedes 027 contracts/next-expected-msgseqnum.md C3's explicit-end case by name: after
// the gate's wait and the store tail read,
//   end_is_through_current = end_is_through_current || requested_end >= next
// where `next` is the tail read's next outbound number, or peek_outbound() with no store. A
// request whose end reaches the next outbound number is answered as one through the current
// number, as QuickFIX C++ clamps EndSeqNo, so a GapFill's NewSeqNo never passes that number.
//
// The helper NEVER calls record_state_transition_ — the CALLER owns all FSM
// Disconnected transitions. Returns std::unexpected on all failure paths so
// the caller can detect and handle them.
//
// Returns:
//   replay_outcome::completed                   — success (resend complete)
//   replay_outcome::superseded                  — `arm` was superseded before an effect
//   unexpected(app_callback_threw)              — a GapFill toAdmin threw
//   unexpected(dispatch_aborted)                — transport write error; a stored frame
//                                                 too large to capture; a GapFill that
//                                                 could not be built
//   unexpected(<the store's own code>)          — fixpp#538 (D-538-4): a store read of
//                                                 the walk failed. The tail read: any
//                                                 error. A slot's retrieve: any code but
//                                                 store_seqnum_gap, store_seqnum_invalid
//                                                 and store_invalid_range, which fold.

asio::awaitable<fixpp::core::expected_t<Session::replay_outcome>> Session::replay_outbound_range_(
    seqnum_t begin, seqnum_t requested_end, bool end_is_through_current,
    std::optional<fsm_state> arm) noexcept {
    // 093 plan OD-25: on a Logon arm's 789 path, the predicate is tested immediately
    // before each effect of the walk: a GapFill's toAdmin, a replay frame's write, and
    // the gap-filled-slot event. Each suspension (the store read, each retrieve, each
    // write) is followed by one of those or by a return; on an error return,
    // honor_peer_next_expected_ tests the predicate before its Disconnected write.
    const auto superseded = [this, arm] { return arm && logon_arm_superseded_(*arm); };
    const auto st52_sr = effective_clock_
                             ? stamp_sending_time(*effective_clock_, cfg_.sending_time_precision)
                             : SendingTimeStamp{};

    // Transmit a single replay/gapfill frame; returns false on write error.
    // Uses the live-write path when a transport is attached; sync transport_send_
    // for pre-live/test paths.
    const auto transmit_async = [&](std::span<const std::byte> f) -> asio::awaitable<bool> {
        if (live_transport_shared_()) {
            // A frame held back by ruling W is a success (104, contract RR-6 item 3).
            auto wr = co_await live_write_serialized_(f);
            co_return wr.has_value();
        }
        if (!transport_send_) {
            co_return true;
        }
        // 104 (contract RR-6 item 3): ruling W's test at the sink; it takes no exemption.
        if (wire_closed_by_reset_) {
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
        return mt == "0" || mt == "1" || mt == "2" || mt == "3" || mt == "4" || mt == "5" ||
               mt == "A";
    };

    const auto emit_gapfill_async =
        [&](seqnum_t at_seq,
            seqnum_t new_seqno) -> asio::awaitable<fixpp::core::expected_t<replay_outcome>> {
        if (superseded()) {
            co_return replay_outcome::superseded;
        }
        std::array<std::byte, 256> gf_buf{};
        auto gf = fixpp::session::build_sequence_reset_gapfill(
            std::span<std::byte>{gf_buf.data(), gf_buf.size()}, at_seq, cfg_.sender_comp_id,
            cfg_.target_comp_id, new_seqno, cfg_.begin_string, st52_sr.value);
        if (!gf) {
            // Build failure (buffer too small for configured CompIDs) — fail closed.
            // Mirrors build_logon fail-closed precedent (emit_initiator_logon_'s reset_on_logon
            // arm): an outbound admin frame that cannot be constructed must NOT report success.
            // Silent success here would leave the peer's ResendRequest silently unfilled
            // (data-loss).
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
        // 019 T014: toAdmin before SequenceReset-GapFill. [FR-008/010]
        // FIX-3 (gate-b/r1): throw → terminal-close + app_callback_threw.
        if (!fire_to_admin_(*gf)) {
            co_return std::unexpected(fixpp::core::error::app_callback_threw);
        }
        if (!co_await transmit_async(*gf)) {
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
        co_return replay_outcome::completed;
    };

    // 103 (research D3; FR-012, SC-003): a request from 0 is answered as one from 1. The one
    // clamp is here, where an answer is numbered, so no caller of the walk can write 34=0.
    begin = std::max(begin, seqnum_min);

    // 103 (research D5′.3; FR-014): the walk holds the replay gate exclusive from here, before
    // its first suspension, to every ending, and waits for the open shared holds before the
    // store tail read, so a frame numbered before the answer is stored and written before the
    // tail read sets the answer's end. The guard's release wakes the waiting emitters.
    //   The caller's own cancellation, before or during the wait, ends the walk as a cancelled
    //   store read does (research D5′.3 and its erratum; `.specify/536-537-538-recovery-
    //   liveness.md` §9, D-CAN-3, D-CAN-4): store_cancelled, which the caller turns into the
    //   same ending, so the timing of one cancellation is not observable. Each pass of the wait
    //   therefore runs, in this frame, the cancellation parts 1 to 4 of the lock sequence a
    //   store's lock wait runs (FIXPP_DETAIL_CO_AWAIT_LOCK, async_mutex.hpp): the pre-check, the
    //   filter set to total for the wait, the wait, and the state reset to terminal, read
    //   first. A pass's exit is then the one a cancelled lock wait leaves: no cancellation
    //   recorded, the terminal filter. The gate itself resets nothing. A wake by the
    //   Disconnected edge, or a superseded arm, is not a cancellation: dispatch_aborted.
    //   Part 1 has no reachable hit while the walk is entered only through a plain co_await,
    //   whose await_transform makes the same test first, with no await before this loop, and
    //   part 4 resets with no suspension before the next pass's part 1. It stays so that part 2
    //   cannot erase a cancellation that a later await before the loop would record.
    const detail::replay_gate::exclusive_guard gate_exclusive{replay_gate_};
    while (replay_gate_.shared != 0) {
        if ((co_await asio::this_coro::throw_if_cancelled()) &&
            (co_await asio::this_coro::cancellation_state).cancelled() !=
                asio::cancellation_type::none) {
            co_await asio::this_coro::reset_cancellation_state(
                asio::enable_terminal_cancellation{});
            co_return std::unexpected(fixpp::core::error::store_cancelled);
        }
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
        co_await replay_gate_.wait_for_wake();
        const bool wait_cancelled = (co_await asio::this_coro::cancellation_state).cancelled() !=
                                    asio::cancellation_type::none;
        co_await asio::this_coro::reset_cancellation_state(asio::enable_terminal_cancellation{});
        if (wait_cancelled) {
            co_return std::unexpected(fixpp::core::error::store_cancelled);
        }
        if (fsm_state_ == fsm_state::Disconnected || superseded()) {
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }
    }

    // Resolve the effective end: through-current or clamped to our last stored
    // sequence number. Mirrors the original rr_end resolution.
    seqnum_t our_last = 0;
    // 104 (research D3.3a): the next outbound number the end model's clamp reads, the tail
    // read's when there is a store, else the manager's.
    seqnum_t next_outbound = seqnum_mgr_.peek_outbound();
    if (store_) {
        auto ns = co_await store_->next_seqnum(direction_t::outbound, false);
        // fixpp#538 (.specify/536-537-538-recovery-liveness.md, D-538-4, "The tail
        // read"; rulings R-7, R-13): this read has no argument to refuse, so its failure
        // is a read the store did not make, a cancelled one among them. The walk
        // returns the store's code before it writes anything. Superseded: the failure
        // was dropped, and the whole range became one GapFill.
        if (!ns) {
            co_return std::unexpected(ns.error());
        }
        our_last = (*ns > 0) ? (*ns - 1U) : 0;
        next_outbound = *ns;
    }
    // 104 (research D3.3a): the end model's clamp (the function header's formula).
    end_is_through_current = end_is_through_current || requested_end >= next_outbound;
    const seqnum_t eff_end = (end_is_through_current || (our_last > 0 && requested_end > our_last))
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
        co_return co_await emit_gapfill_async(begin, new_seq_no);
    }

    // Per-slot store-walk over [begin, eff_end]. Accumulate absent, admin and
    // unbuildable (fixpp#424) runs into one GapFill; flush before each replay.
    static constexpr std::size_t kRpBufSize =
        CaptureVisitor::kCapBufSize + 256;  // capture + replay-tag overhead
    // 101-inbound-reject-fidelity (research D7, "The capture invariant"): a captured frame, up to
    // kCapBufSize, is rebuilt here by build_replay_frame, which adds PossDupFlag(43)=Y and
    // OrigSendingTime(122) and restamps or inserts SendingTime(52). Each timestamp is at most the
    // widest UTCTimestamp utc_time_to_fix_string writes, its nanos form in core/fix_time.hpp, so
    // the growth is bounded by the two added fields plus a whole 52, and the headroom must hold
    // it. A frame built at kSendFrameBytes therefore replays past that ceiling by at most this.
    static constexpr std::size_t kMaxUtcTimestampChars = sizeof("YYYYMMDD-HH:MM:SS.sssssssss") - 1;
    static constexpr std::size_t kReplayGrowthBytes =
        (sizeof("43=Y\x01") - 1) + (sizeof("122=\x01") - 1) + kMaxUtcTimestampChars +
        (sizeof("52=\x01") - 1) + kMaxUtcTimestampChars;
    static_assert(kRpBufSize - CaptureVisitor::kCapBufSize >= kReplayGrowthBytes,
                  "the replay buffer's headroom holds a replay's growth");
    bool gap_open = false;
    seqnum_t gap_start = 0;
    for (seqnum_t k = begin; k <= eff_end; ++k) {
        CaptureVisitor cv;
        auto rr = co_await store_->retrieve(k, k, direction_t::outbound, cv);

        // fixpp#538 (.specify/536-537-538-recovery-liveness.md, D-538-4; rulings R-7,
        // R-10, R-13), by the code the read returned. A number the store does not
        // retain (store_seqnum_gap), and an argument it refused (store_seqnum_invalid,
        // store_invalid_range: the codes retrieve()'s contract assigns to the request),
        // join the GapFill run below. Every other code is a read the store did not
        // make, store_cancelled among them: the answer stops here, an open GapFill run
        // is not flushed, and the caller gets the store's own code. Superseded: every
        // failed read was folded. Rejected: gap-filling a slot the store failed to read
        // and recording an event (a GapFill is permanent on the wire).
        if (!rr && rr.error() != fixpp::core::error::store_seqnum_gap &&
            rr.error() != fixpp::core::error::store_seqnum_invalid &&
            rr.error() != fixpp::core::error::store_invalid_range) {
            co_return std::unexpected(rr.error());
        }

        // Truncated frame: disconnect rather than silently losing data. [RC#B]
        if (cv.truncated) {
            co_return std::unexpected(fixpp::core::error::dispatch_aborted);
        }

        // 092 R-12: the header scan stops at its first fault, so a stored frame with a
        // fault before its 35 scans with no MsgType. Such a frame is not an
        // application message (it may be admin), so it joins the GapFill run below
        // rather than being rebuilt and resent [FIX-SL §4.8.3].
        bool app_present = false;
        if (rr && cv.captured) {
            const std::string_view stored_msg_type =
                scan_frame_header(std::span<const std::byte>{cv.buf.data(), cv.len},
                                  session_hooks(inbound_tv_))
                    .msg_type;
            app_present = !stored_msg_type.empty() && !is_admin_type(stored_msg_type);
        }
        if (app_present) {
            // #420: stamped per replayed message — SendingTime(52) is the time
            // this frame is sent, not the time the resend answer started.
            const auto st52_rp = effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                       cfg_.sending_time_precision)
                                                  : SendingTimeStamp{};
            std::array<std::byte, kRpBufSize> rp_buf{};
            auto rp = build_replay_frame(std::span<std::byte>{rp_buf.data(), rp_buf.size()},
                                         std::span<const std::byte>{cv.buf.data(), cv.len},
                                         st52_rp.value, session_hooks(inbound_tv_));
            if (rp) {
                // Built first, flushed second: an unbuildable slot must be able
                // to join the open gap run below instead of splitting it.
                if (gap_open) {
                    auto g = co_await emit_gapfill_async(gap_start, k);
                    if (!g || *g == replay_outcome::superseded) co_return g;
                    gap_open = false;
                }
                if (superseded()) {
                    co_return replay_outcome::superseded;
                }
                if (!co_await transmit_async(*rp)) {
                    co_return std::unexpected(fixpp::core::error::dispatch_aborted);
                }
                continue;
            }
            // fixpp#424 (D4a): an unbuildable replay is gap-filled, not skipped —
            // a skipped number leaves the peer's gap open [FIX-SL §4.8.3]. Rejected:
            // failing the whole resend (leaves the gap open too). A frame too large
            // to CAPTURE stays loud (the cv.truncated disconnect above, D5).
            if (superseded()) {
                co_return replay_outcome::superseded;
            }
            emit_event(session_event_resend_slot_gap_filled{.seq = k, .code = rp.error()});
        }
        // Absent slot, admin message, or unbuildable replay → fold into a GapFill run.
        if (!gap_open) {
            gap_open = true;
            gap_start = k;
        }
    }
    if (gap_open) {
        co_return co_await emit_gapfill_async(gap_start, eff_end + 1U);
    }
    // Remain in Active after responding to ResendRequest / 789 honor.
    co_return replay_outcome::completed;
}

// ── 027 — honor_peer_next_expected_ ──────────────────────────────────────────
//
// Shared body for the 789-honor dispatch; used by both the acceptor
// (NotConnected handler) and initiator (LogonSent handler).
//
// Integrity guard ordering (D-10): invalid-X FIRST, then X>N, then X<N.
// [contract C4/C6/C8, data-model I-NEX-2/3/4/9/11, D-6/D-10]
//
// Returns:
//   in_sync_continue     — X==N or X<N resend succeeded; caller continues.
//   ended_disconnected   — X==0 or X>N: Logout emitted + Disconnected recorded;
//                          caller MUST co_return expected_t<void>{}.
//   superseded           — 093 plan OD-25: logon_arm_superseded_(arm) held after a
//                          suspension, before the next effect; nothing more written,
//                          no state recorded; caller MUST co_return expected_t<void>{}.
//   unexpected(err)      — X<N resend failed; Disconnected recorded;
//                          caller MUST co_return std::unexpected(err).
asio::awaitable<fixpp::core::expected_t<Session::logon_789_outcome>>
Session::honor_peer_next_expected_(std::string_view raw_789, bool /*present_789*/,
                                   seqnum_t next_outbound_ref, fsm_state arm) noexcept {
    const seqnum_t x789 = parse_seqnum(raw_789);
    // 031: compare the peer's 789 against the comparison reference (the acceptor passes
    // its PRE-reply next-outbound; the initiator passes current peek_outbound()), NOT the
    // live post-reply peek_outbound() — on the acceptor arm honor runs after the reply
    // Logon consumed a seq, so peek_outbound() here is N_post = N_pre+1 and an in-sync
    // peer (789 = N_pre) would mis-classify as behind-by-one. The resend RANGE below
    // still reads the live peek_outbound() (INV-NEX-RANGE, below in this function).
    const seqnum_t n789 = next_outbound_ref;
    if (x789 == 0 || x789 > n789) {
        // X == 0: present-but-invalid (parse→0: empty / non-digit / overflow).
        // Evaluated FIRST (D-10): a parse→0 value must never reach the X<N branch
        // (which would clamp begin to 1 and replay [1,N-1]).
        // [contract C6, I-NEX-9, D-10]
        // X > N: peer claims to have received frames we haven't sent yet.
        // Sequence-integrity violation: Logout(text) + disconnect.
        // [contract C6, I-NEX-4, D-6]
        // The two endings differ only in the Logout's Text(58).
        char text_buf[80];
        std::string_view text_sv = "NextExpectedMsgSeqNum invalid";
        if (x789 != 0) {
            // Build "NextExpectedMsgSeqNum too high, expecting N but received X".
            // Stack-only: use a fixed-size char buffer (N and X are seqnum_t ≤10 digits each).
            char* tp = text_buf;
            const char* prefix = "NextExpectedMsgSeqNum too high, expecting ";
            for (const char* p = prefix; *p; ++p) *tp++ = *p;
            auto [n_end, n_ec] = std::to_chars(tp, text_buf + sizeof(text_buf) - 20, n789);
            if (n_ec == std::errc{}) tp = n_end;
            const char* mid = " but received ";
            for (const char* p = mid; *p; ++p) *tp++ = *p;
            auto [x_end, x_ec] = std::to_chars(tp, text_buf + sizeof(text_buf), x789);
            if (x_ec == std::errc{}) tp = x_end;
            text_sv = std::string_view{text_buf, static_cast<std::size_t>(tp - text_buf)};
        }
        {
            std::array<std::byte, 256> lo_buf{};
            const auto lo_st52 = effective_clock_ ? stamp_sending_time(*effective_clock_,
                                                                       cfg_.sending_time_precision)
                                                  : SendingTimeStamp{};
            const seqnum_t lo_seq = seqnum_mgr_.peek_outbound();
            auto lo_result = fixpp::session::build_logout(
                std::span<std::byte>{lo_buf.data(), lo_buf.size()}, lo_seq, cfg_.sender_comp_id,
                cfg_.target_comp_id, text_sv, cfg_.begin_string, lo_st52.value);
            // 105 (ruling R-789; research D2 item 11): an R-2 ending before Active, entered
            // here; armed before the Disconnected below (E-3 rows 1, 3, 6).
            // 106 D11 (row 3): the ending claim first; a refused claim writes no Logout.
            bool const write_refused =
                !claim_ending_(ending_claim::ending_logout) ||
                enter_ending_write_(ending_kind::r2, co_await asio::this_coro::executor) ==
                    ending_entry::refused;
            bool transmitted = false;
            if (lo_result && !write_refused) {
                ending_write_unwind_guard const leave_guard{.s = this};
                // 019 FR-008/010: toAdmin before every engine-originated admin emit.
                // [gate-b/r1 FQ-2: mirror the file-wide fire_to_admin_-before-assign_outbound
                // admin-emit ordering]
                if (!fire_to_admin_(*lo_result)) {
                    (void)leave_ending_write_(false, false);
                    record_state_transition_(fsm_state::Disconnected);
                    co_return std::unexpected(fixpp::core::error::app_callback_threw);
                }
                auto assign_r = co_await seqnum_mgr_.assign_outbound();
                if (assign_r) {
                    auto emit_r = co_await store_then_emit(lo_seq, *lo_result, arm, true,
                                                           wire_reset_exemption::none, &transmitted,
                                                           write_class::logout);
                    // What store_then_emit returns and when it throws: its declaration in
                    // session.hpp.
                    (void)emit_r;
                }
            }
            (void)leave_ending_write_(transmitted, false);
        }
        // 093 plan OD-25: the Logout's store and write are suspensions.
        if (logon_arm_superseded_(arm)) {
            co_return logon_789_outcome::superseded;
        }
        record_state_transition_(fsm_state::Disconnected);
        co_return logon_789_outcome::ended_disconnected;
    } else if (x789 < n789) {
        // X < N: proactively resend [X, N-1].
        // [contract C4/C8, I-NEX-2/3]
        // 031 INV-NEX-RANGE: the resend range endpoint reads the LIVE peek_outbound()-1
        // (= N_pre on the acceptor arm), NOT n789-1 (= next_outbound_ref-1). Behaviorally
        // inert at this call site (end_is_through_current=true forces eff_end=our_last,
        // per replay_outbound_range_'s eff_end formula), but written explicitly for
        // contract-fidelity and robustness.
        auto rr789 = co_await replay_outbound_range_(x789, seqnum_mgr_.peek_outbound() - 1U,
                                                     /*end_is_through_current=*/true, arm);
        if (!rr789) {
            // 093 plan OD-25: the failure followed a suspension of the walk.
            if (logon_arm_superseded_(arm)) {
                co_return logon_789_outcome::superseded;
            }
            if (fsm_state_ != fsm_state::Disconnected) {
                record_state_transition_(fsm_state::Disconnected);
            }
            co_return std::unexpected(rr789.error());
        }
        if (*rr789 == replay_outcome::superseded) {
            co_return logon_789_outcome::superseded;
        }
    }
    // X == N: in sync, no resend.
    co_return logon_789_outcome::in_sync_continue;
}

}  // namespace fixpp::session
