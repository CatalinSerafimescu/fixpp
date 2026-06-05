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

#include <array>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/engine.hpp>
// T044: full definitions needed to call shutdown() on lifecycle teardown.
// These headers are included only in the .cpp (not in engine.hpp) to keep
// the awaitable-header include-edge clean ([const §XV.9]).
#include <fixpp/log/logger.hpp>
#include <fixpp/otel/providers.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <fixpp/wire/framer.hpp>
#include <memory>
#include <span>
#include <vector>

// Internal concrete transport/listener types — only used in this .cpp.
// [arch §5.3 engine-bootstrap carve-out]
#include "transport/asio_listener.hpp"

namespace fixpp::session {

using core::error;
using core::expected_t;

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
    ~counter_guard() {
        if (counter) --(*counter);
    }
};

// ── ctor ─────────────────────────────────────────────────────────────────────

Engine::Engine(asio::any_io_executor exec, fixpp::core::EngineConfig cfg)
    : exec_{std::move(exec)},
      engine_cfg_{std::move(cfg)},
      // 017 owned amendment #2: seed the engine-level trace_context snapshot
      // from EngineConfig::engine_trace_context at construction time.
      // contracts/adjacent-amendments.md §2 / [2k App D §D.2].
      engine_trace_ctx_snapshot_{engine_cfg_.engine_trace_context},
      // T003 (E-0/D0/INV-0): control strand — single serialization domain for all
      // engine-global state. Constructed once over exec_. INV-0: distinct from
      // every per-session strand (each SessionEntry::session_strand is separate).
      // No routing through it yet; US1/US2 wire the routing.
      control_strand_{asio::make_strand(exec_)},
      // FIX-2 (gate-b/r1): initialize the in-flight send counter. Starts at
      // zero; Engine::send bumps/decrements it; stop() drains it before
      // registry_.clear() to prevent send coroutines from dereferencing engine_
      // after Engine::~Engine() runs.
      send_counter_{std::make_shared<std::atomic<int>>(0)}

{}

// ── dtor ──────────────────────────────────────────────────────────────────────
// STRICT precondition: assert(stopped()).
// No synchronous best-effort teardown — synchronous dtor cannot drain
// in-flight coroutines holding raw Session* → UAF (Gate A Codex-9 / E-7).

Engine::~Engine() {
    // T004/INV-8: stopped_ is std::atomic<bool>; load(acquire) pairs with
    // stop()'s sequentially-consistent write, ensuring the assertion sees the
    // final stop() write before the dtor proceeds.
    assert(stopped_.load(std::memory_order_acquire)
           && "Engine destroyed without calling co_await stop() first");
}

// ── register_session (FR-002 / E-1) ──────────────────────────────────────────
// Records config + role only; does NOT construct a Session (lazy — Gate A New-3).
// Duplicate SessionId::from_config(cfg) → session_invalid_argument (119 / R5).

expected_t<void> Engine::register_session(SessionConfig cfg) {
    SessionId id = SessionId::from_config(cfg);  // derive key BEFORE move
    if (registry_.contains(id)) return std::unexpected(error::session_invalid_argument);

    SessionEntry::role role = (cfg.role == session_role::acceptor) ? SessionEntry::role::acceptor
                                                                   : SessionEntry::role::initiator;

    // operator[] default-constructs in-place (SessionEntry contains
    // non-movable asio::cancellation_signal — cannot move-insert).
    auto& entry = registry_[id];  // id copied into map key
    entry.session_role = role;
    entry.config = std::move(cfg);
    return {};
}

// ── lookup (Gate A New-3) ─────────────────────────────────────────────────────

Session* Engine::lookup(SessionId const& id) const {
    auto it = registry_.find(id);
    return (it == registry_.end()) ? nullptr : it->second.session.get();
}

bool Engine::stopped() const noexcept {
    // T004/INV-8: acquire load — pairs with stop()'s write on the control strand.
    return stopped_.load(std::memory_order_acquire);
}

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

[[nodiscard]] FirstFrameIds scan_first_frame_ids(std::span<const std::byte> frame) noexcept {
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
        std::string_view val{reinterpret_cast<const char*>(frame.data() + vstart), i - vstart};
        if (i < n) ++i;  // skip SOH
        if (tag == 8) ids.begin_string = val;
        if (tag == 49) ids.sender_comp_id = val;
        if (tag == 56) ids.target_comp_id = val;
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
asio::awaitable<fixpp::core::expected_t<std::size_t>> read_first_frame_bounded(
    fixpp::transport::Transport& transport, std::vector<std::byte>& buf,
    std::chrono::milliseconds deadline, std::size_t max_bytes) {
    using namespace std::chrono_literals;
    using fixpp::core::error;

    auto exec = co_await asio::this_coro::executor;
    asio::steady_timer timer{exec};
    timer.expires_after(deadline);

    bool timed_out = false;
    // 015 /simplify (Q-2) — the deadline must CANCEL the in-flight async_read_some,
    // not merely set a flag the loop checks between reads: a peer that completes the
    // TLS handshake then stalls would otherwise block the read forever (FR-014 /
    // SC-011). transport.cancel() aborts the pending read → the read-error arm below
    // returns transport_handshake_timeout.
    timer.async_wait([&timed_out, &transport](const std::error_code& ec) {
        if (!ec) {
            timed_out = true;
            transport.cancel();
        }
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
            // Deadline (Q-2): the timer callback cancelled this read → the error
            // arm closes + reclaims (FR-014). The specific code is unobservable
            // here (no Session, no log surface) so it is not special-cased; the
            // between-reads deadline still returns transport_handshake_timeout below.
            timer.cancel();
            co_return std::unexpected(read_r.error());
        }
        std::size_t n = *read_r;
        buf.insert(buf.end(), read_buf.data(), read_buf.data() + n);

        if (buf.size() >= max_bytes) {
            timer.cancel();
            co_return std::unexpected(error::wire_frame_too_large);
        }

        // Feed ONLY the newly-read bytes into the stateful Framer — it accumulates
        // unconsumed bytes in `carry` across calls, so re-feeding the whole `buf`
        // would duplicate the already-carried prefix and corrupt a fragmented first
        // frame (a valid Logon split across TLS reads → rejected). Mirrors the
        // incremental feed in run_read_pump. [F-015-001]
        auto feed_r = framer.feed(std::span<const std::byte>{read_buf.data(), n}, carry,
                                  std::span<fixpp::wire::frame_view>{out_frames});
        if (!feed_r.has_value()) {
            timer.cancel();
            if (feed_r.error() == error::wire_frame_too_large)
                co_return std::unexpected(error::wire_frame_too_large);
            co_return std::unexpected(feed_r.error());
        }
        if (!feed_r->empty()) {
            // First complete frame available. Return its EXACT length so the caller
            // delivers ONLY the first frame (buf[0..len)) to on_inbound_frame and
            // carries any surplus (buf[len..], a coalesced next frame) into the
            // read-pump (F-015-002). buf accumulates raw bytes in arrival order, so
            // buf[0..len) is byte-for-byte the first frame the framer emitted.
            timer.cancel();
            co_return (*feed_r)[0].bytes().size();
        }
    }
    co_return std::unexpected(error::transport_handshake_timeout);
}

// ── Read-pump (US2 T015) ─────────────────────────────────────────────────────
// Co-awaited inline from run_accept_loop after the first Logon is delivered.
// Runs on the accept loop's executor (== session strand in the single-executor
// model used by the tests and the current engine). Reads subsequent frames,
// parses them with a session-lifetime Framer + pmr_carry_buffer, and delivers
// each complete frame to session.on_inbound_frame.
//
// Termination:
//   EOF / read-error   → close session terminal, stop pump.
//   wire_frame_too_large → close session terminal, stop pump (FR-012).
//   on_inbound_frame error → close session terminal, stop pump (FR-012).
//   total-cancel (stop()) → async_read_some returns transport_read_cancelled
//                           → error arm fires, close is a no-op on already-
//                           closing session, pump unwinds cleanly.
//
// Natural backpressure: no inbound queue; each on_inbound_frame call must
// complete before the next read_some is issued (SC-003 / US2 AC1).
//
// Capacity constant: must be < the 128 KiB over-size frame the T014 case sends
// (kOversizeBody = 128 KiB) so wire_frame_too_large fires in the framer, AND
// large enough to hold any valid FIX admin frame. 64 KiB is the right value.
// [tasks.md T015; FR-004/012; C2; [[feedback_asio_cospawn_total_cancellation_default]]]
constexpr std::size_t kReadPumpCarryCapacity = 64U * 1024U;  // 64 KiB

asio::awaitable<void> run_read_pump(fixpp::transport::Transport& transport,
                                    fixpp::session::Session& session,
                                    fixpp::session::SessionConfig const& cfg,
                                    std::span<const std::byte> initial_bytes = {}) {
    // MANDATORY total-cancel reset — required if this coroutine is ever
    // co_spawned (co_spawn defaults to terminal-only). Harmless when co_awaited
    // inline. [[feedback_asio_cospawn_total_cancellation_default]] / [const §XI.2]
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // Session-lifetime carry buffer. One allocation from the configured arena
    // (or new_delete if none supplied). Never reallocated; overflow → wire_frame_too_large.
    std::pmr::memory_resource* arena =
        cfg.framer_carry_arena ? cfg.framer_carry_arena : std::pmr::new_delete_resource();
    fixpp::wire::pmr_carry_buffer carry{kReadPumpCarryCapacity, arena};

    // Per-read scratch buffer — unrelated to the carry; plain stack array.
    // Size matches the default max_read_window_bytes on Transport::Config.
    std::array<std::byte, 4096> read_buf{};

    // Per-call output slot. We process one frame at a time to maintain
    // natural backpressure (no inbound queue, SC-003 / US2 AC1).
    std::array<fixpp::wire::frame_view, 1> out{};

    fixpp::wire::Framer framer;

    // Helper: close the session terminally on error/EOF, then stop the pump.
    // close(terminal) transitions FSM → Disconnected and fires root cancellation.
    // If close() was already called (stop() fired concurrently), the idempotent
    // three-state model returns session_already_closed — ignored here (no-op).
    // [data-model §E-5; FR-012; session.hpp close(terminal)]
    auto stop_pump = [&]() -> asio::awaitable<void> {
        (void)co_await session.close(fixpp::session::close_mode::terminal);
    };

    // Seed the framer with any surplus bytes carried over from the bounded
    // first-frame read (a coalesced Logon‖next-frame, F-015-002). Drain them
    // through the SAME framing path BEFORE the first socket read so no surplus
    // frame is dropped. Mirrors the read-loop drain below (kept as a separate
    // block to leave the proven read loop untouched).
    if (!initial_bytes.empty()) {
        std::span<const std::byte> incoming = initial_bytes;
        for (;;) {
            auto feed_r = framer.feed(incoming, carry, std::span<fixpp::wire::frame_view>{out});
            if (!feed_r.has_value()) {
                co_await stop_pump();
                co_return;
            }
            std::size_t const produced = feed_r->size();
            for (auto const& frame : *feed_r) {
                auto deliver_r = co_await session.on_inbound_frame(frame.bytes());
                if (!deliver_r.has_value()) {
                    co_await stop_pump();
                    co_return;
                }
            }
            incoming = {};
            if (produced < out.size()) break;
        }
    }

    while (true) {
        auto read_r = co_await transport.async_read_some(
            std::span<std::byte>{read_buf.data(), read_buf.size()});

        if (!read_r.has_value()) {
            // EOF (transport_read_eof) or read error (transport_read_cancelled on
            // total-cancel, or a transport I/O error). Existing disconnect handling:
            // close(terminal) → FSM → Disconnected. [FR-012]
            co_await stop_pump();
            co_return;
        }

        // Drain ALL complete frames this read produced before issuing the next
        // read_some. feed() emits at most out.size() frames per call and retains
        // the surplus in `carry`; multiple complete frames can arrive in a single
        // read (TLS/TCP coalescing), so we re-feed with an empty span (carry-only)
        // until no further complete frame is produced. Without this drain a
        // coalesced frame would sit in carry until the next read and be DROPPED on
        // EOF — violating exactly-once in-order delivery (SC-003 / US2 AC1).
        std::span<const std::byte> incoming{read_buf.data(), *read_r};
        for (;;) {
            auto feed_r = framer.feed(incoming, carry, std::span<fixpp::wire::frame_view>{out});

            if (!feed_r.has_value()) {
                // wire_frame_too_large or other framing error.
                // Per FR-012: no silent truncation; close and stop. [T015]
                co_await stop_pump();
                co_return;
            }

            std::size_t const produced = feed_r->size();
            for (auto const& frame : *feed_r) {
                auto deliver_r = co_await session.on_inbound_frame(frame.bytes());
                if (!deliver_r.has_value()) {
                    // Session-fatal error from FSM (e.g. seqnum overflow, store I/O).
                    // Per FR-012: close and stop. [T015]
                    co_await stop_pump();
                    co_return;
                }
            }

            incoming = {};                     // subsequent drains are carry-only
            if (produced < out.size()) break;  // framer withheld nothing more
        }
    }
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
asio::awaitable<void> run_accept_loop(fixpp::core::EngineConfig const& engine_cfg, Engine& engine,
                                      SessionId const& session_id, SessionEntry& entry,
                                      asio::cancellation_signal& /*accept_scope_signal*/,
                                      outstanding_t counter) {
    counter_guard guard{counter};

    // MANDATORY total-cancel reset (stop() emits total; co_spawn defaults to
    // terminal-only). [[feedback_asio_cospawn_total_cancellation_default]]
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

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
            // The engine must still map the deprecated-but-supported legacy profile.
            // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
            ssl_cfg.profile = fixpp::tls::SecurityProfile::one_way_ca;
        else  // mtls_ca (default for acceptors)
            ssl_cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;

        if (entry.config.transport_factory_override)
            ssl_cfg.cs = entry.config.transport_factory_override->cert_source_snapshot();
        ssl_cfg.clock = nullptr;  // skip cert expiry check (same as loopback fixture)
        ssl_cfg.caps = fixpp::tls::CertSourceCaps{};
    }

    // ── Build the asio_listener (bind/listen) ────────────────────────────────
    // reconnect_endpoint is repurposed as the bind endpoint (SC-010 delta #6).
    // Port 0 → OS-assigned; discoverable via acceptor_bound_endpoint().
    fixpp::transport::asio_listener::Config lcfg;
    lcfg.bind_endpoint = entry.config.reconnect_endpoint;
    lcfg.ssl_cfg = ssl_cfg;  // copy — listener stores its own config
    // FR-014: bound the TLS handshake with a short timeout so stalled or
    // non-TLS clients are rejected promptly (part of the bounded first-frame
    // window). 2s < the probe's 2s self-deadline but large enough for real TLS.
    lcfg.accepted_transport_config.tls_handshake_timeout = std::chrono::milliseconds{1500};

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
    auto* raw_listener =
        static_cast<fixpp::transport::asio_listener*>(engine.listeners_[session_id].get());

    // ── Accept loop ──────────────────────────────────────────────────────────
    // Session construction is LAZY + MATCH-GATED (FQ-2 / data-model C1 step 6):
    // the Session is constructed and open()'d ONLY after CompID resolution
    // confirms a match — no-match closes the transport and loops without
    // constructing any Session (lookup() stays nullptr until a peer matches).
    // [realized-behavior.md C1 step 6; engine.hpp:200-204; gate-b/r1]
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
        auto* tls_transport = dynamic_cast<fixpp::transport::TlsTransport*>(transport.get());
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
        std::size_t first_frame_len = 0;
        {
            auto read_r = co_await read_first_frame_bounded(
                *transport, frame_buf, kFirstFrameDeadline, kFirstFrameMaxBytes);
            if (!read_r.has_value()) {
                transport->close();
                continue;  // timeout / over-budget / read-error → reclaim
            }
            first_frame_len = *read_r;
        }
        // The first complete frame is frame_buf[0..first_frame_len); any bytes
        // beyond it are surplus (a coalesced next frame) that must reach the
        // read-pump rather than being delivered as part of the Logon (F-015-002).
        std::span<const std::byte> first_frame{frame_buf.data(), first_frame_len};
        std::span<const std::byte> surplus{frame_buf.data() + first_frame_len,
                                           frame_buf.size() - first_frame_len};

        // Step 4: parse CompIDs for reversed-CompID registry resolution.
        auto ids = scan_first_frame_ids(first_frame);
        if (ids.begin_string.empty() || ids.sender_comp_id.empty() || ids.target_comp_id.empty()) {
            transport->close();
            continue;  // malformed first frame → reclaim
        }

        // Step 5: registry resolution.
        // Acceptor key: {bs, sender=ME=logon_target, target=PEER=logon_sender}
        fixpp::session::SessionId resolved_id = fixpp::session::SessionId::reversed_from_logon(
            std::string{ids.begin_string}, ids.sender_comp_id, ids.target_comp_id);

        if (resolved_id != session_id) {
            // No registry match — unknown acceptor session (slot 121).
            // Per data-model C1 step 6 and engine.hpp lookup() contract:
            // close the transport and construct NO session. [FQ-2 / gate-b/r1]
            transport->close();
            continue;
        }

        // Step 6: construct + open the Session ONLY on a registry match.
        // (E-1 / Gate A New-3 / data-model C1 step 6 / FQ-2)
        // open() is awaitable — must run inside the loop, not in start().
        // On open() failure, close the transport and exit the loop (fatal).
        entry.session = std::make_shared<Session>(engine_cfg, entry.config);
        {
            auto res = co_await entry.session->open();
            if (!res.has_value()) {
                transport->close();
                entry.session.reset();
                co_return;
            }
        }
        Session* session = entry.session.get();

        // Step 7: attach the live transport (T011).
        // Happens-before invariant (Gate A New-1 / E-4): live_peer_id_ is set
        // here, STRICTLY-BEFORE the first on_inbound_frame call below.
        // Capture raw pointer BEFORE the move — session owns the transport for
        // the pump's whole lifetime (the pump is co_awaited inline below, joining
        // before co_return, so no UAF). [T015 locked design decision #2]
        fixpp::transport::Transport* raw = transport.get();
        session->attach_accepted_transport(std::move(transport), std::move(hr));

        // Publish the live transport so stop() can close the socket and unblock
        // the read-pump's in-flight async_read_some (idle established reads have
        // no peer EOF; total-cancel alone does not break the SSL read). [T018]
        entry.live_transport = raw;

        // Step 8: direct-deliver the first Logon (DR-7 / E-2) — ONLY the first
        // frame, never any coalesced surplus (F-015-002).
        {
            auto deliver_r = co_await session->on_inbound_frame(first_frame);
            (void)deliver_r;
        }

        // Step 9: run the real read-pump inline (T015).
        // Inline co_await means the pump is part of the counter_guard scope:
        // stop()'s total-cancel propagates into async_read_some, the pump unwinds,
        // and the counter only decrements after the pump co_returns. No second
        // counter or detached spawn needed. [T015 locked design decision #3]
        co_await run_read_pump(*raw, *session, entry.config, surplus);

        // Re-spin the accept loop to serve the next peer (C5 — loop continuously).
        // For the current static-registry model (R2), the session stays live for
        // one connection; on disconnect US2 T015/T016 will handle reconnect.
        // For now, break after the first successful peer to keep US1 simple.
        co_return;
    }
}

// ── run_connect_loop — T016 production body ──────────────────────────────────
// Initiator live connect path (Option A — single connect+pump; multi-cycle
// reconnect-respin DEFERRED per Clarifications 2026-05-31 / E-1a: close(terminal)
// is permanent and 014 has no tested multi-cycle reconnect — symmetric with US1's
// single-peer acceptor). Connect-then-Logon ordering (FR-003), grounded in
// QuickFIX-cpp setResponder→generateLogon and Fix8 connect→send(generate_logon):
//   1. Mark the config engine_managed so Session::open()'s initiator arm DEFERS
//      the Logon (no live transport yet — emitting now would violate FR-003).
//   2. open() — wires reconnect_fsm_ (owner/endpoint/tls/factory) but emits
//      nothing for the engine-managed initiator.
//   3. drive_reconnect() — connect + handshake + authorize + install (rebinds
//      transport_send_ to the live sink, re-enters LogonSent) + emit the initial
//      Logon POST-connect over that live sink.
//   4. run_read_pump on the live transport until EOF → close(terminal).
// [data-model E-1a; FR-003/004; C2/C2i/C5; T016(b)-(e); SC-010 (7)/(8)]
static asio::awaitable<void> run_connect_loop(fixpp::core::EngineConfig const& engine_cfg,
                                              SessionEntry& entry, outstanding_t counter) {
    counter_guard guard{counter};

    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // Step 1: engine-managed lazy-connect — defer the at-open Logon (T016(d)).
    entry.config.engine_managed = true;
    entry.session = std::make_shared<Session>(engine_cfg, entry.config);

    // Step 2: open() — no Logon emitted (engine_managed initiator arm is a no-op).
    auto res = co_await entry.session->open();
    if (!res.has_value()) {
        entry.session.reset();
        co_return;
    }

    Session* session = entry.session.get();

    // Step 3: connect + handshake + authorize + install + rebind + POST-connect
    // Logon. On exhaustion/cancel or Logon-emit failure the session is left
    // Disconnected; close terminally (idempotent) and unwind. [E-1a; FR-003]
    {
        auto drive_r = co_await session->drive_reconnect();
        if (!drive_r.has_value()) {
            (void)co_await session->close(fixpp::session::close_mode::terminal);
            co_return;
        }
    }

    // Publish the live transport so stop() can close the socket and unblock the
    // read-pump's in-flight async_read_some (see SessionEntry::live_transport). [T018]
    entry.live_transport = &session->live_transport();

    // Step 4: run the read-pump inline on the live transport until EOF.
    // Inline co_await keeps the pump in the counter_guard scope (stop()'s
    // total-cancel propagates into async_read_some; the counter decrements only
    // after the pump co_returns — mirror of run_accept_loop step 8). [T015/T016(e)]
    co_await run_read_pump(session->live_transport(), *session, entry.config);
    co_return;
}

// ── start (FR-001/FR-003 / data-model "Public surface") ──────────────────────
// Non-blocking. co_spawns one per-role loop per registered session on exec_.
// Each loop co_awaits open() itself — cannot run in this synchronous void.

void Engine::start() {
    auto counter = std::make_shared<std::atomic<int>>(0);

    for (auto& [id, entry] : registry_) {
        // T005 (E-1/INV-1): create the per-session strand BEFORE the loop spawn.
        // One strand per session — INV-1 (never shared across sessions).
        // Created-but-not-yet-bound here; US1 (T009/T010) binds the role loop,
        // Session, and transport to this strand. [E-1/E-2/D1]
        entry.session_strand = asio::make_strand(exec_);

        ++(*counter);
        if (entry.session_role == SessionEntry::role::acceptor) {
            auto& scope_sig = accept_scope_signals_[id];  // default-constructs
            asio::co_spawn(
                exec_, run_accept_loop(engine_cfg_, *this, id, entry, scope_sig, counter),
                asio::bind_cancellation_slot(entry.session_cancel.slot(), asio::detached));
        } else {
            asio::co_spawn(
                exec_, run_connect_loop(engine_cfg_, entry, counter),
                asio::bind_cancellation_slot(entry.session_cancel.slot(), asio::detached));
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

asio::awaitable<void> Engine::stop() {
    // F2 (Gate-B/r1): stop() is the teardown driver — it MUST run to completion once
    // it sets stopped_=true, else a later stop() short-circuits at the guard below
    // leaving the registry/sessions/liveness loops half-torn-down. Shield it from a
    // caller's cancellation (a slot-bound co_spawn) before mutating any state. The
    // production callers spawn stop() under use_future (no slot); this is defensive
    // hardening so the contract holds regardless of how stop() is awaited. [Codex P2]
    co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});
    // T004/INV-8: idempotency guard — acquire to observe any prior stop() write.
    if (stopped_.load(std::memory_order_acquire)) {
        co_return;
    }
    // Authoritative write on this (currently exec_-serialized) call path.
    // US2/T018 will move this onto control_strand_; sequentially-consistent
    // default store pairs with the acquire reads at the two read sites.
    stopped_ = true;

    for (auto& [id, entry] : registry_) entry.session_cancel.emit(asio::cancellation_type::total);
    for (auto& [id, sig] : accept_scope_signals_) sig.emit(asio::cancellation_type::total);

    // Close every live transport (the "closes transports" step in this function's
    // contract). An established session's read-pump is blocked in async_read_some
    // with no peer EOF; total-cancel does not break the in-flight SSL read, so the
    // socket MUST be closed for the read to fail and the pump to unwind. close()
    // is synchronous + idempotent; the transport is owned by the Session (alive
    // until the join-before-clear below). [T018; Gate A New-4; E-7]
    for (auto& [id, entry] : registry_)
        if (entry.live_transport != nullptr) entry.live_transport->close();

    // JOIN: yield until all loops have co_return'd.
    if (outstanding_counter_) {
        asio::steady_timer t{co_await asio::this_coro::executor};
        while (outstanding_counter_->load(std::memory_order_acquire) > 0) {
            t.expires_after(std::chrono::milliseconds{0});
            co_await t.async_wait(asio::use_awaitable);
        }
        outstanding_counter_.reset();
    }

    // FIX-2 (gate-b/r1): drain in-flight Engine::send coroutines BEFORE
    // registry_.clear(). A send coroutine holds a shared_ptr<Session> keepalive
    // (so the Session object is alive) but the Session stores a const EngineConfig&
    // engine_ ref — if Engine::~Engine() runs while a send is suspended on the
    // session strand, dereferencing engine_.application / clock / store is a UAF.
    // stopped_ is already true, so no new sends can enter; we just wait for
    // the currently-bumped ones to decrement. [spec.md FR-012]
    {
        asio::steady_timer t2{co_await asio::this_coro::executor};
        while (send_counter_->load(std::memory_order_acquire) > 0) {
            t2.expires_after(std::chrono::milliseconds{0});
            co_await t2.async_wait(asio::use_awaitable);
        }
    }

    // F2: drive each surviving session through its own close() to drain the
    // per-session liveness loop. The role loops (run_read_pump) DO call
    // session.close(terminal) on read EOF/error, but when Engine::stop() total-
    // cancels them the loop's `co_await session.close()` throws operation_aborted at
    // the cancelled await BEFORE close() is entered, so a parked run_liveness_loop
    // sleep_until is never joined — it survives to io_context shutdown, where its
    // system_clock_source dereg guard touches the (freed) clock pimpl: a heap-use-
    // after-free (first seen on the live QuickFIX-cpp interop cell). This stop()
    // coroutine is NOT cancelled, so close() runs to completion here, draining the
    // liveness loop + write/seqnum gates. close() is idempotent (session_already_
    // closed if a loop already drained it). Must precede registry_.clear() so no
    // Session* is dereferenced after free.
    for (auto& [id, entry] : registry_) {
        if (entry.session) {
            (void)co_await entry.session->close(fixpp::session::close_mode::terminal);
        }
    }

    // Safe now: all loops have exited; Session objects may be freed.
    accept_scope_signals_.clear();
    listeners_.clear();
    listener_endpoints_.clear();
    registry_.clear();

    // FR-014 / T044: flush sinks and shut down the OTel providers.
    // Ordering: sessions are torn down BEFORE provider shutdown so no
    // session-level span/metric emission races the provider Shutdown().
    // Logger flush: drain in-flight log records before releasing the logger.
    // Provider shutdown: flush + stop the SDK exporter workers.
    // Lifecycle only — NO session-FSM transition edit ([2k §6.6] / T044).
    if (engine_cfg_.logger) {
        // Use the no-arg overload so the Engine honors LoggerConfig::drain_timeout
        // (set by the operator) rather than a hardcoded 5s literal.
        // [2k §6.6]: Engine::close() calls logger->shutdown(LoggerConfig::drain_timeout).
        (void)engine_cfg_.logger->shutdown();
    }
    if (engine_cfg_.tracer) {
        engine_cfg_.tracer->shutdown();
    }
    if (engine_cfg_.meter) {
        engine_cfg_.meter->shutdown();
    }
}

// ── Engine::send — T013 (019-app-callbacks US2) ───────────────────────────────
//
// Any-thread-safe public outbound send entry point. [FR-006/007/013; research D6]
//
// FIX-1+FIX-2 (gate-b/r1) + FIX-A (gate-b/r2) design:
//   Step A: hop onto exec_ (the engine executor) so registry reads + stopped_
//           check + send_counter_ bump are serialized with stop()'s mutations.
//           [[feedback_asio_post_resume_bounces_to_spawn_executor]]
//   Step B: on exec_ — registry lookup, keepalive capture, Active check,
//           stopping check. If stopped_ → fail-fast (no send may outlive stop()).
//           Bump send_counter_ + RAII counter_guard to enroll in the send-drain
//           domain. The guard fires on co_return AND exception/cancel unwind so
//           a total-cancel from stop() cannot leave the counter positive. [FIX-2/A]
//   Step C: hop to session strand for toApp + Session::send. counter_guard dtor
//           decrements after the strand hop completes (or unwinds). [FIX-2/A]
//
//   stop() drains send_counter_ BEFORE registry_.clear() so no send coroutine
//   can dereference engine_ after Engine::~Engine() runs.
//   Backpressure = the awaited result ([const §XV.15]).
//   Re-entrant call from within an on-strand callback: asio::post is
//   non-blocking → no deadlock (FR-006 edge case).
asio::awaitable<core::expected_t<void>> Engine::send(SessionId const& id,
                                                     std::span<const std::byte> app_payload) {
    // Copy payload into a coroutine-frame-local buffer so the caller's span
    // (potentially stack-allocated) stays valid across all co_await suspensions.
    std::vector<std::byte> payload_copy(app_payload.begin(), app_payload.end());

    // Step A: first-hop onto exec_ so registry reads are serialized with
    // stop()'s registry_.clear(). [FIX-1: gate-b/r1]
    core::expected_t<void> result =
        co_await asio::co_spawn(
            exec_,
            [this, id, payload_copy = std::move(payload_copy)]()
                -> asio::awaitable<core::expected_t<void>> {
                // ── Step B: on exec_ — safe to read registry + stopped_ ──

                // FIX-2: fail-fast if stop() has begun.
                // T004/INV-8: acquire load — pairs with stop()'s write so a
                // multi-threaded executor observes the stopped_ write reliably.
                if (stopped_.load(std::memory_order_acquire)) {
                    co_return std::unexpected(core::error::session_invalid_state_for_send);
                }

                // Registry lookup (serialized with registry_.clear() by exec_).
                auto it = registry_.find(id);
                if (it == registry_.end()) {
                    co_return std::unexpected(core::error::session_invalid_argument);
                }

                // Capture strong keepalive before any co_await (UAF guard).
                std::shared_ptr<Session> kl = it->second.session;

                // Session null (loop not yet started) or not Active → reject.
                if (!kl || kl->state() != fsm_state::Active) {
                    co_return std::unexpected(core::error::session_invalid_state_for_send);
                }

                // FIX-2 / gate-b/r2 FIX-A: enroll this in-flight send in the
                // send-drain domain with an RAII guard so the decrement fires on
                // co_return AND on exception/total-cancel unwind.
                // The manual ++/-- pair was not exception-safe: if the inner
                // co_spawn(strand_exec) was total-cancelled (stop()'s teardown
                // path), operation_aborted propagated out and skipped the manual
                // --, leaving send_counter_ permanently positive and wedging
                // stop()'s drain loop. counter_guard (engine.cpp:61-69) is the
                // existing RAII pattern used by the accept/connect loops —
                // reusing it here closes the gap. [spec.md FR-012]
                ++(*send_counter_);
                counter_guard send_guard{send_counter_};

                // Also reset cancellation state on the outer exec_ frame so
                // total cancellation has a defined policy for the bump→guard
                // window (belt-and-suspenders; the guard is the load-bearing fix).
                co_await asio::this_coro::reset_cancellation_state(
                    asio::enable_total_cancellation());

                // ── Step C: hop to session strand for toApp + Session::send ──
                auto strand_exec = kl->executor().underlying();
                core::expected_t<void> send_result =
                    co_await asio::co_spawn(
                        strand_exec,
                        [kl, payload_copy = std::move(payload_copy)]()
                            -> asio::awaitable<core::expected_t<void>> {
                            // Enable total cancellation so stop()'s
                            // cancellation_type::total reaches Session::send.
                            // [[feedback_asio_cospawn_total_cancellation_default]]
                            co_await asio::this_coro::reset_cancellation_state(
                                asio::enable_total_cancellation());
                            // Re-check state on the strand (may have changed
                            // since the exec_ check above).
                            if (kl->state() != fsm_state::Active) {
                                co_return std::unexpected(
                                    core::error::session_invalid_state_for_send);
                            }
                            co_return co_await kl->send(std::span<const std::byte>(
                                payload_copy.data(), payload_copy.size()));
                        },
                        asio::use_awaitable);

                co_return send_result;
            },
            asio::use_awaitable);

    co_return result;
}

// ── acceptor_bound_endpoint (SC-010 delta #6) ─────────────────────────────────
// Returns the OS-resolved bound endpoint of the acceptor's listener for `id`.
// Returns Endpoint{} (port==0) if the id is not a registered acceptor or the
// listener has not been built yet.  The accept loop builds and stores the
// listener at the start of run_accept_loop; the endpoint is readable once the
// executor runs at least one step after start().

fixpp::transport::Endpoint Engine::acceptor_bound_endpoint(SessionId const& id) const {
    auto it = listener_endpoints_.find(id);
    if (it == listener_endpoints_.end()) return fixpp::transport::Endpoint{};
    return it->second;
}

// ── 017 owned amendment #2: engine_trace_context() / clock() ─────────────────
// contracts/adjacent-amendments.md §2 / [2k App D §D.2].

fixpp::otel::trace_context Engine::engine_trace_context() const noexcept {
    return engine_trace_ctx_snapshot_.load();
}

const std::shared_ptr<fixpp::core::Clock>& Engine::clock() const noexcept {
    return engine_cfg_.clock;
}

}  // namespace fixpp::session
