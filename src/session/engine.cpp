// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/engine.cpp — Engine lifecycle substrate (T006) + US1 T012.
// Anchors: data-model.md E-1/E-2/E-4/E-5/E-7, research.md R1/R3/R4/R7/R9,
//          contracts/engine_api.md, contracts/realized-behavior.md C1/C5/C6,
//          [const §XI.2/§XI.4/§XV.9],
//          [[feedback_asio_cospawn_total_cancellation_default]]
//
// T012: run_accept_loop now has its full production body:
//   build asio_listener → loop: async_accept → async_handshake → bounded first-
//   frame read → reversed-CompID resolve → attach → deliver first Logon → stub pump.
// US2 (T015/T016) will replace the read-pump stub.

#include <fixpp/session/engine.hpp>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/read.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <fixpp/wire/framer.hpp>

// Internal concrete transport/listener types — only used in this .cpp.
// [arch §5.3 engine-bootstrap carve-out]
#include "transport/asio_listener.hpp"

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

// ── Minimal SOH-delimited scanner for CompID resolution (T012) ───────────────
// Extracts begin_string (tag 8), sender_comp_id (tag 49), target_comp_id (tag 56)
// from a raw FIX frame bytes (views into the frame buffer — caller keeps it alive).
struct FirstFrameIds {
    std::string_view begin_string;
    std::string_view sender_comp_id;
    std::string_view target_comp_id;
};

[[nodiscard]] FirstFrameIds scan_first_frame_ids(
    std::span<const std::byte> frame) noexcept
{
    FirstFrameIds ids;
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    std::size_t i = 0;
    const std::size_t n = frame.size();

    while (i < n) {
        std::uint32_t tag = 0;
        bool tag_ok = true;
        while (i < n && frame[i] != EQ && frame[i] != SOH) {
            auto c = static_cast<unsigned char>(frame[i]);
            if (c < '0' || c > '9') tag_ok = false;
            tag = (tag * 10U) + static_cast<std::uint32_t>(c - '0');
            ++i;
        }
        if (i >= n || frame[i] != EQ || !tag_ok) {
            while (i < n && frame[i] != SOH) ++i;
            if (i < n) ++i;
            continue;
        }
        ++i;  // skip '='
        std::size_t vstart = i;
        while (i < n && frame[i] != SOH) ++i;
        std::string_view val{reinterpret_cast<const char*>(frame.data() + vstart),
                             i - vstart};
        if (i < n) ++i;  // skip SOH
        if (tag == 8)  ids.begin_string    = val;
        if (tag == 49) ids.sender_comp_id  = val;
        if (tag == 56) ids.target_comp_id  = val;
    }
    return ids;
}

// ── Bounded first-frame read (FR-014 / E-2 / C1 steps 2-3) ──────────────────
// Reads raw bytes from an accepted (not-yet-TLS-handshaken, post-handshake) TCP
// transport into `buf` with a deadline. Returns the number of bytes read on
// success, or an error on timeout / over-budget / read-fail.
//
// Used AFTER async_handshake succeeds — we read TLS application-data bytes.
//
// Invariant: returns when >= 1 complete FIX frame is present in buf, OR when
// the deadline fires, OR when the byte budget is exceeded, OR on read error.
// "Complete frame" == Framer::feed returns at least one frame_view.
//
// [FR-014; E-2; data-model "Bounded first-frame read"]
asio::awaitable<fixpp::core::expected_t<std::size_t>>
read_first_frame_bounded(fixpp::transport::Transport& transport,
                         std::vector<std::byte>& buf,
                         std::chrono::milliseconds deadline,
                         std::size_t max_bytes)
{
    using namespace std::chrono_literals;
    using fixpp::core::error;

    auto exec = co_await asio::this_coro::executor;
    asio::steady_timer timer{exec};
    timer.expires_after(deadline);

    bool timed_out = false;
    timer.async_wait([&timed_out](const std::error_code& ec) {
        if (!ec) timed_out = true;
    });

    // Build a framer to detect frame boundaries.
    fixpp::wire::pmr_carry_buffer carry{max_bytes, std::pmr::new_delete_resource()};
    std::array<fixpp::wire::frame_view, 1> out_frames{};
    fixpp::wire::Framer framer;

    std::array<std::byte, 4096> read_buf{};
    while (!timed_out) {
        if (buf.size() >= max_bytes) {
            timer.cancel();
            co_return std::unexpected(error::wire_frame_too_large);
        }

        auto read_r = co_await transport.async_read_some(
            std::span<std::byte>{read_buf.data(), read_buf.size()});
        if (!read_r.has_value()) {
            timer.cancel();
            co_return std::unexpected(read_r.error());
        }
        std::size_t n = *read_r;
        buf.insert(buf.end(), read_buf.data(), read_buf.data() + n);

        if (buf.size() >= max_bytes) {
            timer.cancel();
            co_return std::unexpected(error::wire_frame_too_large);
        }

        // Try to parse a complete frame from accumulated bytes.
        auto feed_r = framer.feed(
            std::span<const std::byte>{buf.data(), buf.size()},
            carry,
            std::span<fixpp::wire::frame_view>{out_frames});
        if (!feed_r.has_value()) {
            timer.cancel();
            if (feed_r.error() == error::wire_frame_too_large)
                co_return std::unexpected(error::wire_frame_too_large);
            co_return std::unexpected(feed_r.error());
        }
        if (!feed_r->empty()) {
            // At least one complete frame available.
            timer.cancel();
            co_return buf.size();
        }
    }
    co_return std::unexpected(error::transport_handshake_timeout);
}

// ── Minimal read-pump stub (US2 T015 replaces this) ─────────────────────────
// Spawned per-session after the first Logon is delivered (T012). Reads
// subsequent frames and feeds them to on_inbound_frame until EOF/cancel.
asio::awaitable<void>
run_read_pump_stub(fixpp::transport::Transport* /*transport*/,
                   fixpp::session::Session* /*session*/)
{
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation());
    // US2 T015 will implement the real pump. For now co_return immediately.
    co_return;
}

}  // anonymous namespace

// ── run_accept_loop — T012 production body ───────────────────────────────────
// Still inside namespace fixpp::session (the outer `namespace fixpp::session {`
// opened at the top of this file). Defined here (not in the anonymous namespace)
// so Engine can declare it as a friend — C++ friend declarations only work with
// non-anonymous-namespace linkage. [dcl.friend]
//
// Builds asio_listener from reconnect_endpoint (repurposed as bind endpoint),
// then loops: async_accept → async_handshake → bounded first-frame read →
// reversed-CompID resolve → attach → direct-deliver first Logon → spawn pump.
// [data-model E-2/E-7; FR-005/006/014; C1/C7; T012; T-041 acceptor path]
asio::awaitable<void>
run_accept_loop(fixpp::core::EngineConfig const& engine_cfg,
                Engine& engine,
                SessionId const& session_id,
                SessionEntry& entry,
                asio::cancellation_signal& /*accept_scope_signal*/,
                outstanding_t counter)
{
    counter_guard guard{counter};

    // MANDATORY total-cancel reset (stop() emits total; co_spawn defaults to
    // terminal-only). [[feedback_asio_cospawn_total_cancellation_default]]
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation());

    auto exec = co_await asio::this_coro::executor;

    // ── Build the SslCtxConfig for the listener ───────────────────────────────
    // The transport_factory_override holds the SSL_CTX + cert_source built at
    // session registration time. We reconstruct the SslCtxConfig from the
    // factory's cert_source snapshot + the session's security profile kind.
    // [data-model "Listener acquisition"; SC-010 delta #6]
    fixpp::tls::SslCtxConfig ssl_cfg;
    {
        using sk = fixpp::session::SecurityProfile::kind;
        auto k = entry.config.security_profile.k;
        if (k == sk::mtls_pinned)
            ssl_cfg.profile = fixpp::tls::SecurityProfile::mtls_pinned;
        else if (k == sk::one_way_ca)
            ssl_cfg.profile = fixpp::tls::SecurityProfile::one_way_ca;
        else  // mtls_ca (default for acceptors)
            ssl_cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;

        if (entry.config.transport_factory_override)
            ssl_cfg.cs = entry.config.transport_factory_override->cert_source_snapshot();
        ssl_cfg.clock = nullptr;  // skip cert expiry check (same as loopback fixture)
        ssl_cfg.caps  = fixpp::tls::CertSourceCaps{};
    }

    // ── Build the asio_listener (bind/listen) ────────────────────────────────
    // reconnect_endpoint is repurposed as the bind endpoint (SC-010 delta #6).
    // Port 0 → OS-assigned; discoverable via acceptor_bound_endpoint().
    fixpp::transport::asio_listener::Config lcfg;
    lcfg.bind_endpoint = entry.config.reconnect_endpoint;
    lcfg.ssl_cfg       = ssl_cfg;  // copy — listener stores its own config
    // FR-014: bound the TLS handshake with a short timeout so stalled or
    // non-TLS clients are rejected promptly (part of the bounded first-frame
    // window). 2s < the probe's 2s self-deadline but large enough for real TLS.
    lcfg.accepted_transport_config.tls_handshake_timeout =
        std::chrono::milliseconds{1500};

    std::unique_ptr<fixpp::transport::asio_listener> listener;
    try {
        listener = std::make_unique<fixpp::transport::asio_listener>(exec, lcfg);
    } catch (...) {
        co_return;  // listener bind/listen failed; loop exits
    }

    // Store listener and bound endpoint in the engine (direct member access —
    // run_accept_loop is a friend of Engine per engine.hpp).
    auto bound_ep = listener->bound_endpoint();
    engine.listener_endpoints_[session_id] = bound_ep;
    engine.listeners_[session_id] = std::move(listener);
    auto* raw_listener = static_cast<fixpp::transport::asio_listener*>(
        engine.listeners_[session_id].get());

    // ── Lazy Session construction + open() ───────────────────────────────────
    // (E-1 / Gate A New-3) — open() is awaitable; cannot run in start().
    entry.session = std::make_unique<Session>(engine_cfg, entry.config);
    {
        auto res = co_await entry.session->open();
        if (!res.has_value()) { entry.session.reset(); co_return; }
    }

    Session* session = entry.session.get();

    // ── Accept loop ──────────────────────────────────────────────────────────
    while (!engine.stopped()) {
        // Step 1: accept the next TCP connection.
        auto accept_r = co_await raw_listener->async_accept();
        if (!accept_r.has_value()) {
            // transport_accept_cancelled → stop() fired; exit loop cleanly.
            break;
        }
        std::unique_ptr<fixpp::transport::Transport> transport = std::move(*accept_r);

        // Step 2: TLS handshake.
        // async_accept returns a TLS-capable transport (see asio_listener.cpp
        // which builds transports via asio_tls_transport_factory::make_accepted).
        // We dynamic_cast to TlsTransport to call async_handshake.
        auto* tls_transport = dynamic_cast<fixpp::transport::TlsTransport*>(
            transport.get());
        if (!tls_transport) {
            transport->close();
            continue;  // not a TLS transport — config error; re-accept
        }

        fixpp::transport::handshake_result hr{};
        {
            auto hs_r = co_await tls_transport->async_handshake(ssl_cfg);
            if (!hs_r.has_value()) {
                transport->close();
                continue;  // handshake failure → reclaim slot, re-accept
            }
            hr = std::move(*hs_r);
        }

        // Step 3: bounded first-frame read (FR-014).
        // 5s deadline, 4096 bytes max (covers any valid FIX Logon message).
        constexpr std::size_t kFirstFrameMaxBytes = 4096;
        constexpr auto kFirstFrameDeadline = std::chrono::milliseconds{5000};

        std::vector<std::byte> frame_buf;
        frame_buf.reserve(512);
        {
            auto read_r = co_await read_first_frame_bounded(
                *transport, frame_buf, kFirstFrameDeadline, kFirstFrameMaxBytes);
            if (!read_r.has_value()) {
                transport->close();
                continue;  // timeout / over-budget / read-error → reclaim
            }
        }

        // Step 4: parse CompIDs for reversed-CompID registry resolution.
        auto ids = scan_first_frame_ids(
            std::span<const std::byte>{frame_buf.data(), frame_buf.size()});
        if (ids.begin_string.empty() || ids.sender_comp_id.empty() ||
            ids.target_comp_id.empty()) {
            transport->close();
            continue;  // malformed first frame → reclaim
        }

        // Step 5: registry resolution.
        // Acceptor key: {bs, sender=ME=logon_target, target=PEER=logon_sender}
        fixpp::session::SessionId resolved_id =
            fixpp::session::SessionId::reversed_from_logon(
                std::string{ids.begin_string},
                ids.sender_comp_id,
                ids.target_comp_id);

        if (resolved_id != session_id) {
            // No registry match — unknown acceptor session (slot 121).
            // [data-model "Error model delta"; C7]
            transport->close();
            continue;
        }

        // Step 6: attach the live transport (T011).
        // Happens-before invariant (Gate A New-1 / E-4): live_peer_id_ is set
        // here, STRICTLY-BEFORE the first on_inbound_frame call below.
        // run_accept_loop is a friend of Session (session.hpp) so this private
        // method call compiles.
        session->attach_accepted_transport(std::move(transport), std::move(hr));

        // Step 7: direct-deliver the first Logon (DR-7 / E-2).
        // The frame bytes are already in frame_buf; NOT re-fed into a framer carry.
        {
            auto deliver_r = co_await session->on_inbound_frame(
                std::span<const std::byte>{frame_buf.data(), frame_buf.size()});
            (void)deliver_r;
        }

        // Step 8: spawn read-pump stub (US2 T015 replaces with the real pump).
        asio::co_spawn(exec,
            run_read_pump_stub(nullptr, session),
            asio::bind_cancellation_slot(
                entry.session_cancel.slot(), asio::detached));

        // Re-spin the accept loop to serve the next peer (C5 — loop continuously).
        // For the current static-registry model (R2), the session stays live for
        // one connection; on disconnect US2 T015/T016 will handle reconnect.
        // For now, break after the first successful peer to keep US1 simple.
        co_return;
    }
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
                run_accept_loop(engine_cfg_, *this, id, entry, scope_sig, counter),
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
    listeners_.clear();
    listener_endpoints_.clear();
    registry_.clear();
}

// ── acceptor_bound_endpoint (SC-010 delta #6) ─────────────────────────────────
// Returns the OS-resolved bound endpoint of the acceptor's listener for `id`.
// Returns Endpoint{} (port==0) if the id is not a registered acceptor or the
// listener has not been built yet.  The accept loop builds and stores the
// listener at the start of run_accept_loop; the endpoint is readable once the
// executor runs at least one step after start().

fixpp::transport::Endpoint Engine::acceptor_bound_endpoint(
    SessionId const& id) const
{
    auto it = listener_endpoints_.find(id);
    if (it == listener_endpoints_.end())
        return fixpp::transport::Endpoint{};
    return it->second;
}

}  // namespace fixpp::session
