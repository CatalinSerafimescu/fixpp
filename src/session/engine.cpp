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
// T011 (D5/E-5/INV-7): asio_tls_transport_test_access for the INV-7 debug
// assert (transport.socket().get_executor() == session_strand). This is the
// R8 lynchpin: every construction site must be audited and asserted.
// Allowed under [arch §5.3] engine-bootstrap carve-out (engine.cpp already
// includes asio_listener.hpp — same concession for concrete transport types).
#include "transport/asio_tls_transport.hpp"

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
      send_counter_{std::make_shared<std::atomic<int>>(0)},
      // T023 (E-7/D-SNAP/INV-9): initialize the reader snapshot to a non-null
      // empty Snapshot so readers never load null even before start(). The first
      // real publish happens after register_session() inserts a registry entry.
      // [data-model E-7; research D-SNAP; tasks T023]
      reader_snapshot_{std::make_shared<const ReaderSnapshot>()}

{}

// ── dtor ──────────────────────────────────────────────────────────────────────
// STRICT precondition: assert(stopped()).
// No synchronous best-effort teardown — synchronous dtor cannot drain
// in-flight coroutines holding raw Session* → UAF (Gate A Codex-9 / E-7).

Engine::~Engine() {
    // T004/INV-8: stopped_ is std::atomic<bool>; load(acquire) pairs with
    // stop()'s sequentially-consistent write, ensuring the assertion sees the
    // final stop() write before the dtor proceeds.
    assert(stopped_.load(std::memory_order_acquire) &&
           "Engine destroyed without calling co_await stop() first");

#ifndef NDEBUG
    // T025 (INV-9a/FR-014/R7): debug-only assertion that no outstanding lookup()
    // handles remain at destruction time. A caller holding a handle past ~Engine
    // would UAF-dereference Session's const EngineConfig& engine_ ref.
    //
    // This is a DEBUG ASSERT + CALLER OBLIGATION — NEVER a stop() drain (R7):
    // draining on app-held leases would hang stop(); the hard send_counter_
    // barrier guards the UAF on the send path. Keep the two mechanisms separate.
    // [data-model INV-9a; research R7; tasks T025]
    assert(lease_counter_.load(std::memory_order_acquire) == 0 &&
           "Engine destroyed with outstanding lookup() handles — "
           "all shared_ptr<Session> handles obtained from lookup() must be "
           "released before ~Engine (INV-9a: Session borrows EngineConfig& "
           "from Engine; a dangling handle is a UAF). [T025/R7]");
#endif
}

// ── publish_reader_snapshot_unlocked_ — T023 (E-7/D-SNAP/INV-9) ─────────────
// Builds a fresh immutable ReaderSnapshot from the current registry_ and
// listener_endpoints_ state, then atomically publishes it via reader_snapshot_.
//
// MUST be called on the control strand (or pre-start single-thread) AFTER
// every control-plane mutation. "Unlocked" because the caller already holds
// the serialization guarantee (control strand). [E-7/INV-9/D-SNAP]
//
// Called from:
//   register_session()         — after registry_ insert (pre-start, single-thread)
//   publish_entry()            — after entry.session/live_transport published
//   unpublish_entry()          — after entry.live_transport reset
//   run_accept_loop map write  — after listener_endpoints_/listeners_ written
//   stop() inner coroutine     — after registry_.clear() + listener_endpoints_.clear()
// [data-model E-7; research D-SNAP; tasks T023/T024]

void Engine::publish_reader_snapshot_unlocked_() {
    // Build a fresh Snapshot from current control-plane state.
    auto snap = std::make_shared<ReaderSnapshot>();

    // Populate sessions map: copy shared_ptr<Session> from each registry entry.
    // Only entries whose session field is non-null appear in the snapshot
    // (pre-publish entries have entry.session == nullptr → lookup() returns null
    // for them, per Gate A New-3). [E-7/INV-9]
    snap->sessions.reserve(registry_.size());
    for (auto const& [id, entry] : registry_) {
        if (entry.session) {
            snap->sessions.emplace(id, entry.session);
        }
    }

    // Populate endpoints map: copy Endpoint by value from listener_endpoints_.
    snap->endpoints.reserve(listener_endpoints_.size());
    for (auto const& [id, ep] : listener_endpoints_) {
        snap->endpoints.emplace(id, ep);
    }

    // Atomically publish: store(release) pairs with readers' load(acquire).
    // The old snapshot is released here (shared_ptr ref-count drop → may free).
    reader_snapshot_.store(std::move(snap), std::memory_order_release);
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

    // T023 (E-7/D-SNAP): NO snapshot republish needed here. A registered entry has
    // entry.session == null, so it contributes nothing to the reader snapshot (the
    // snapshot only carries non-null sessions + bound endpoints). The first
    // observable reader content for this id appears when publish_entry writes
    // entry.session / the accept loop writes its endpoint — each republishes then.
    // The ctor already seeds a non-null empty snapshot, so lookup() safely returns
    // nullptr for a registered-but-not-open id. [/simplify: removed redundant
    // O(N^2) pre-start republish; data-model E-7; INV-9; B-015-3.]
    return {};
}

// ── lookup (Gate A New-3 / T024 D-SNAP) ──────────────────────────────────────
// T024 (FR-008/SC-004/D-SNAP): changed from Session* to std::shared_ptr<Session>.
// Reads the atomically-published reader_snapshot_ (any-thread-safe, no strand,
// no std::mutex, no block). Returns a shared_ptr<Session> drawn from the snapshot's
// sessions map — the handle outlives a concurrent registry_.clear() WHILE THE ENGINE
// IS ALIVE (bounded keepalive, INV-9). Returns nullptr (empty) if id is not in the
// snapshot or the entry's session has not been published yet (Gate A New-3).
//
// T025 (INV-9a): in DEBUG builds, wraps the raw shared_ptr in an aliasing
// shared_ptr backed by a Lease control block (ctor increments lease_counter_,
// dtor decrements) so ~Engine can assert zero outstanding handles. Release builds
// return a plain shared_ptr (no lease overhead, no counter). [R7 — separate from
// send_counter_; the lease is NEVER a stop() drain] [data-model E-7/INV-9/INV-9a]

std::shared_ptr<Session> Engine::lookup(SessionId const& id) const {
    // Load the current snapshot — any-thread-safe acquire load. The snapshot
    // was built from registry_ state as of the last control-strand mutation;
    // its sessions map holds shared_ptr<Session> entries that keep each Session
    // alive independently of a concurrent registry_.clear(). [E-7/D-SNAP]
    auto snap = reader_snapshot_.load(std::memory_order_acquire);
    if (!snap) return nullptr;  // defensive: never null post-ctor, but guard anyway

    auto it = snap->sessions.find(id);
    if (it == snap->sessions.end()) return nullptr;

    // it->second is the shared_ptr<Session> from the snapshot (may be null if
    // the entry exists but entry.session was not yet published). Return it as-is.
    std::shared_ptr<Session> raw_handle = it->second;
    if (!raw_handle) return nullptr;

#ifndef NDEBUG
    // T025 (INV-9a/R7): wrap raw_handle in a LeasedHandle shared_ptr so that:
    //   (a) the Session is kept alive as long as ANY copy of the returned handle
    //       exists (LeasedHandle::session holds raw_handle; its refcount keeps the
    //       Session alive independently of the snapshot), and
    //   (b) ~Engine can debug-assert no outstanding handles remain (destructor
    //       decrements lease_counter_ when the LAST copy is destroyed).
    //
    // Using an aliasing ptr backed by the lease's control block alone (without
    // holding raw_handle) is incorrect: the Session would be freed when the
    // snapshot is cleared, while the aliasing ptr is still alive → UAF. [INV-9]
    //
    // This is STRICTLY SEPARATE from send_counter_ (R7): the lease counter is
    // a debug assert + caller obligation only; it is NEVER drained by stop().
    // Draining on app-held leases would hang stop() indefinitely. [R7/INV-9a]
    struct LeasedHandle {
        std::shared_ptr<Session> session;  // keeps Session alive across clear() [INV-9]
        std::atomic<std::uint64_t>* counter;
        ~LeasedHandle() noexcept { counter->fetch_sub(1, std::memory_order_release); }
    };
    std::atomic<std::uint64_t>* lease_ctr_ptr = &lease_counter_;  // mutable (debug-only)
    lease_ctr_ptr->fetch_add(1, std::memory_order_relaxed);  // new handle issued

    auto leased = std::make_shared<LeasedHandle>(raw_handle, lease_ctr_ptr);
    // Return an aliasing shared_ptr<Session> that SHARES the LeasedHandle's control
    // block while pointing at the Session. Destroying the last copy decrements
    // lease_counter_ and releases leased->session (which may then free the Session
    // if no other copies exist). [INV-9/INV-9a]
    return std::shared_ptr<Session>{leased, leased->session.get()};
#else
    // Release: plain shared_ptr, no lease overhead, no counter. [R7]
    return raw_handle;
#endif
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

// ── INV-7 debug assert helper (D5/E-5/T011/R8) ──────────────────────────────
// Verifies that transport.socket().get_executor() == session_strand.
// In DEBUG builds: asserts and returns the check result.
// In RELEASE builds: compiles away to a no-op (zero cost).
// The comparison uses asio::any_io_executor::operator==, which compares the
// type-erased executor targets — two any_io_executors wrapping the same
// asio::strand<any_io_executor> object are equal iff they share the same strand.
//
// Called after every engine-managed transport construction site (R8 lynchpin):
//   (1) accept path: after raw_listener->async_accept() returns a transport.
//   (2) connect path: after session->drive_reconnect() installs the transport.
// The assert witnesses that no construction site samples the bare exec_ member
// instead of the loop-local session strand.
[[maybe_unused]] void assert_transport_on_session_strand(
    fixpp::transport::Transport& transport,
    const asio::strand<asio::any_io_executor>& session_strand) noexcept {
#ifndef NDEBUG
    // Downcast to asio_tls_transport to access socket_executor().
    // The engine exclusively uses asio_tls_transport (via asio_tls_transport_factory).
    // A null downcast means a non-standard transport was injected (test-seam or future
    // transport type); skip the assert in that case (the V-10 gtest cell covers all sites).
    auto* tls = dynamic_cast<fixpp::transport::asio_tls_transport*>(&transport);
    if (tls != nullptr) {
        // INV-7: the socket's executor MUST equal the session strand.
        // Failure = a construction site sampled bare exec_ (R8 — the silent lynchpin).
        assert(tls->socket_executor() == asio::any_io_executor{session_strand} &&
               "INV-7: transport socket executor != session_strand "
               "(T011/D5/R8: a ctor site sampled bare exec_ instead of the strand)");
    }
#else
    (void)transport;
    (void)session_strand;
#endif
}

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

// ── T013 — D-PUB: awaited publication helpers ─────────────────────────────────
//
// publish_entry: runs on the control strand; checks stopped_ first (INV-2a).
//   - If stopped_ is true: does NOT publish a live transport (stopped disposition);
//     returns false. The caller must close/return without entering the read pump.
//   - If not stopped: publishes entry.session and entry.live_transport; returns true.
//
// unpublish_entry: runs on the control strand; resets entry.live_transport to null
//   (entry.session is retained — owned by the entry, observable via lookup() until
//   registry_.clear()). Called on EVERY loop exit path (normal return, cancellation,
//   error) before the entry can be cleared. [data-model E-2/INV-2]
//
// Both are co_spawned with co_spawn(control_strand_, fn, use_awaitable) from the
// session-strand role loop — a non-blocking post across two distinct strands, so
// no deadlock even if the session strand is currently occupied. [D-PUB/C-2/R6]
//
// Anchors: research.md D-PUB; data-model E-2/INV-2/INV-2a; contract C-6.

namespace {

// Publish helper — returns true (alive) or false (stopped disposition).
// Called from run_accept_loop and run_connect_loop via a wrapper lambda that
// also calls engine.publish_reader_snapshot_unlocked_() after this returns true.
// [T023/D-SNAP: snapshot republish is the caller's responsibility]
asio::awaitable<bool> publish_entry(std::atomic<bool>& stopped_, SessionEntry& entry,
                                    std::shared_ptr<Session> session_ptr,
                                    fixpp::transport::Transport* live_transport_ptr) {
    // INV-2a: check stopped_ FIRST on the control strand. If stop() is already
    // in progress, do NOT publish a live transport for pumping — return false.
    // This closes the stop-before-publish ordering hole: a transport created just
    // before this awaited publish is never pumped once stop() has begun. [D-PUB]
    if (stopped_.load(std::memory_order_acquire)) {
        // Stopped disposition: leave entry.live_transport == nullptr so stop()
        // (which may already be past its iteration) cannot close a stale handle.
        // The session pointer is not published either — the loop will return without
        // entering the read pump.
        co_return false;
    }
    // Not stopped: publish both handles. These are the fields stop() reads.
    entry.session = std::move(session_ptr);
    entry.live_transport = live_transport_ptr;
    co_return true;
}

// Unpublish helper — resets entry.live_transport to null on the control strand.
// Must be called on EVERY exit path of the role loop (normal, cancel, error).
// The reset is a no-op if the entry was never published (stopped disposition).
// [T023/D-SNAP: snapshot republish is the caller's responsibility after this]
asio::awaitable<void> unpublish_entry(SessionEntry& entry) {
    // Reset ONLY entry.live_transport — the load-bearing D-PUB hazard. After the
    // role loop exits, the transport is being torn down; a stale live_transport
    // raw pointer would let stop() step 2 close a freed transport. Nulling it makes
    // stop() correctly skip an already-exited session (entry.live_transport==nullptr).
    //
    // entry.session is DELIBERATELY retained (not reset). It is a shared_ptr OWNED by
    // the SessionEntry, never a dangling read, and lives until registry_.clear() in
    // stop() step 5. Keeping it preserves the terminal-state-visibility contract that
    // lookup() returns a disconnected/auth-failed session while the engine is alive
    // (asserted by the pre-existing 015 engine_acceptor_failclosed cells, which V-4
    // forbids rewriting). stop() step 4 still close()s it — idempotent on an already-
    // closed session. [data-model E-2/INV-2 refinement; preserves SC-003/FR-007/V-4]
    entry.live_transport = nullptr;
    co_return;
}

}  // namespace

// ── run_accept_loop — T012/T013 production body ───────────────────────────────
// Still inside namespace fixpp::session (the outer `namespace fixpp::session {`
// opened at the top of this file). Defined here (not in the anonymous namespace)
// so Engine can declare it as a friend — C++ friend declarations only work with
// non-anonymous-namespace linkage. [dcl.friend]
//
// Builds asio_listener from reconnect_endpoint (repurposed as bind endpoint),
// then loops: async_accept → async_handshake → bounded first-frame read →
// reversed-CompID resolve → attach → T013 awaited publish → deliver first Logon →
// pump → T013 unpublish on all exit paths.
// [data-model E-2/E-7; FR-005/006/014; C1/C7; T012; T013; T-041 acceptor path]
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

    // T018 (D0/INV-0): store listener and bound endpoint on the CONTROL STRAND
    // so these writes are serialized with stop()'s reads and clears.  The listener
    // was built on the session strand (preserving its session-strand executor for
    // accepted sockets — V-10/D5/R8); only the two map writes are hopped to the
    // control strand, mirroring the publish_entry pattern.
    //
    // The co_await suspends this session-strand coroutine until the control strand
    // completes the writes, then resumes (non-blocking across distinct strands —
    // no deadlock, R6/C-2).  A copy of the session_id and bound_ep are captured by
    // value; listener is moved into the lambda (unique_ptr move).
    //
    // NOTE (DD-2026-06-06): the original FIXPP_TEST_SEAMS one-sided park seam
    // that previously appeared here has been removed.  The seam was misleading:
    // the pre-existing join-before-clear (outstanding_counter_) makes the loop's
    // listeners_ write HB-ordered before stop()'s clear() — the park merely
    // delayed both, so TSan never fired.  The genuine HB-free control-plane races
    // are public synchronous readers (lookup()/acceptor_bound_endpoint()) vs the
    // map write/clear — witnessed by V-8 (T016/T017) without any production seam.
    // (The FIXPP_TEST_SEAMS CMake option was removed in the /simplify pass — it
    // gated nothing once the seam was deleted.)
    // [DD-2026-06-06 / research/reviews/codex_023-engine-session-strand_gate_a_v8_retarget.md]
    // Capture raw_listener pointer BEFORE moving into the control-strand lambda,
    // so we never read back from the engine map on the session strand.
    auto* raw_listener = static_cast<fixpp::transport::asio_listener*>(listener.get());
    auto bound_ep = listener->bound_endpoint();

    bool write_ok = co_await asio::co_spawn(
        engine.control_strand_,
        [&engine, session_id, bound_ep,
         lptr = std::move(listener)]() mutable -> asio::awaitable<bool> {
            // Check stopped_ first: if stop() already ran, skip the write (the maps
            // are cleared or in the process of being cleared). [INV-2a parallel for map writes]
            if (engine.stopped_.load(std::memory_order_acquire)) {
                co_return false;
            }
            engine.listener_endpoints_[session_id] = bound_ep;
            engine.listeners_[session_id] = std::move(lptr);
            // T023 (E-7/D-SNAP): republish the reader snapshot so
            // acceptor_bound_endpoint() sees the new endpoint. Called on the
            // control strand (we are already here via co_spawn(control_strand_,...)).
            engine.publish_reader_snapshot_unlocked_();
            co_return true;
        },
        asio::use_awaitable);

    if (!write_ok) {
        // stop() is already in progress; the maps are cleared or will be cleared
        // without our entries (we never wrote them). Exit cleanly.
        co_return;
    }

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

        // T011/INV-7 (D5/E-5/R8): verify the accepted transport's socket is bound
        // to the session strand. Auto-satisfied because:
        //   - The loop runs on *entry.session_strand (T010 — co_spawn on strand).
        //   - asio_listener was constructed with `exec` = co_await this_coro::executor
        //     = the session strand (run_accept_loop:433).
        //   - async_accept() creates accepted_socket{exec_} = session strand.
        //   - make_accepted() adopts accepted_socket.get_executor() = session strand.
        // This assert fires if any site regresses to bare exec_ (R8 silent lynchpin).
        // session_strand is invariantly emplaced in start() before the loop spawns (T005).
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        assert_transport_on_session_strand(*transport, *entry.session_strand);

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
        // T013: hold the session locally until the control-strand publish —
        // do NOT write entry.session directly from the session strand.
        auto local_session = std::make_shared<Session>(engine_cfg, entry.config);
        {
            auto res = co_await local_session->open();
            if (!res.has_value()) {
                transport->close();
                co_return;
            }
        }
        Session* session = local_session.get();

        // Step 7: attach the live transport (T011).
        // Happens-before invariant (Gate A New-1 / E-4): live_peer_id_ is set
        // here, STRICTLY-BEFORE the first on_inbound_frame call below.
        // Capture raw pointer BEFORE the move — session owns the transport for
        // the pump's whole lifetime (the pump is co_awaited inline below, joining
        // before co_return, so no UAF). [T015 locked design decision #2]
        fixpp::transport::Transport* raw = transport.get();
        session->attach_accepted_transport(std::move(transport), std::move(hr));

        // Step 7a: T013 awaited publication — publish entry.session and
        // entry.live_transport ON the control strand, BEFORE entering the read pump.
        // [data-model E-2/INV-2; research D-PUB; contract C-6]
        //
        // This is a non-blocking post from the session strand → control strand
        // (two distinct strands over the same io_context → no deadlock, R6/C-2).
        // The co_await suspends this session-strand coroutine until the control
        // strand runs the publish, then resumes.
        //
        // Lifetime: a COPY of local_session is passed to the publish coroutine so
        // the caller retains its own owning reference regardless of the disposition.
        // If stop is in progress, the publish does not write entry.session and the
        // coroutine's copy is destroyed when it returns false; local_session keeps
        // the Session alive here so `session` remains a valid raw pointer. [INV-2a]
        //
        // INV-2a (stop-before-publish): the publish checks stopped_ first; if stop()
        // is already in progress, it does NOT publish a live transport (stopped
        // disposition → returns false). The loop then closes and returns without
        // entering async_read_some. [research D-PUB; data-model INV-2a; contract C-6]
        bool published = co_await asio::co_spawn(
            engine.control_strand_,
            [&engine, &entry, local_session, raw]() -> asio::awaitable<bool> {
                bool ok = co_await publish_entry(engine.stopped_, entry, local_session, raw);
                if (ok) {
                    // T023 (E-7/D-SNAP): republish the reader snapshot so lookup()
                    // sees the newly-published session. Called on the control strand.
                    // Engine& is a friend → private access is allowed here. [INV-9]
                    engine.publish_reader_snapshot_unlocked_();
                }
                co_return ok;
            },
            asio::use_awaitable);

        if (!published) {
            // Stopped disposition: stop() is already in progress. The transport
            // was already moved into the session (step 7). local_session still
            // holds the only owning reference (entry.session was not written).
            // Close the session terminally (which closes its transport) and return
            // without ever entering the read pump. [INV-2a; contract C-6]
            (void)co_await local_session->close(fixpp::session::close_mode::terminal);
            co_return;
        }

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

        // Step 10 (T013): unpublish on normal exit — reset entry.live_transport
        // (entry.session retained for lookup() terminal-visibility) on the control
        // strand BEFORE this loop entry can be cleared by stop(). Also reached on
        // read-pump EOF (normal) and the acceptor auth-fail close.
        // The unpublish is co_awaited so it completes before the counter_guard
        // decrements (which signals stop()'s join that this loop has exited). [INV-2]
        co_await asio::co_spawn(
            engine.control_strand_,
            [&engine, &entry]() -> asio::awaitable<void> {
                co_await unpublish_entry(entry);
                // T023 (E-7/D-SNAP): republish the snapshot after live_transport reset.
                // entry.session remains for terminal-state visibility; lookup() keeps
                // returning the session in its post-teardown state. [E-7/INV-9]
                engine.publish_reader_snapshot_unlocked_();
            },
            asio::use_awaitable);

        // Re-spin the accept loop to serve the next peer (C5 — loop continuously).
        // For the current static-registry model (R2), the session stays live for
        // one connection; on disconnect US2 T015/T016 will handle reconnect.
        // For now, break after the first successful peer to keep US1 simple.
        co_return;
    }
    // Loop exited cleanly (stopped_=true, async_accept cancelled): no session was
    // published (the loop never reached a match after the stop started), so no
    // unpublish is needed here. The guard's counter decrement signals stop()'s join.
}

// ── run_connect_loop — T013 production body ──────────────────────────────────
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
//   4. T013 awaited publish on control strand BEFORE the read pump. [D-PUB/INV-2]
//   5. run_read_pump on the live transport until EOF → close(terminal).
//   6. T013 unpublish on ALL exit paths. [INV-2]
// [data-model E-1a; FR-003/004; C2/C2i/C5; T013; T016(b)-(e); SC-010 (7)/(8)]
//
// T013: Engine& engine added (was: engine_cfg + entry only) to access
// control_strand_ and stopped_ for the D-PUB awaited publish/unpublish.
// Declared in fixpp::session namespace (not anonymous, not static) so Engine
// can declare it as a friend for private-member access. [dcl.friend]
asio::awaitable<void> run_connect_loop(fixpp::core::EngineConfig const& engine_cfg, Engine& engine,
                                       SessionEntry& entry, outstanding_t counter) {
    counter_guard guard{counter};

    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // Step 1: engine-managed lazy-connect — defer the at-open Logon (T016(d)).
    entry.config.engine_managed = true;
    auto local_session = std::make_shared<Session>(engine_cfg, entry.config);

    // Step 2: open() — no Logon emitted (engine_managed initiator arm is a no-op).
    auto res = co_await local_session->open();
    if (!res.has_value()) {
        co_return;
    }

    Session* session = local_session.get();

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

    // T011/INV-7 (D5/E-5/R8): verify the connect-path transport's socket is bound
    // to the session strand. Auto-satisfied because:
    //   - The loop runs on *entry.session_strand (T010 — co_spawn on strand).
    //   - drive_reconnect() → drive_reconnect_attempt() → co_await this_coro::executor
    //     = the session strand (reconnect_fsm.cpp:117) → factory_->make(exec, ...) uses it.
    //   - The factory-path ctor stores exec as socket_'s executor.
    // This assert fires if reconnect_fsm.cpp regresses to bare exec_ (R8 lynchpin).
    // session_strand is invariantly emplaced in start() before the loop spawns (T005).
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    assert_transport_on_session_strand(session->live_transport(), *entry.session_strand);

    // Step 4 (T013): awaited publication — publish entry.session and
    // entry.live_transport ON the control strand, BEFORE entering the read pump.
    // Non-blocking post from session strand → control strand (distinct strands,
    // no deadlock). co_await suspends until control strand completes the publish. [D-PUB]
    //
    // Lifetime: a COPY of local_session is passed to the publish coroutine so the
    // caller retains its owning reference regardless of the disposition. If stop is
    // in progress, the coroutine destroys its copy and returns false; local_session
    // still keeps the Session alive so `session` remains a valid raw pointer. [INV-2a]
    fixpp::transport::Transport* live_tp = &session->live_transport();
    bool published = co_await asio::co_spawn(
        engine.control_strand_,
        [&engine, &entry, local_session, live_tp]() -> asio::awaitable<bool> {
            bool ok = co_await publish_entry(engine.stopped_, entry, local_session, live_tp);
            if (ok) {
                // T023 (E-7/D-SNAP): republish the reader snapshot so lookup()
                // sees the newly-published session. Called on the control strand.
                engine.publish_reader_snapshot_unlocked_();
            }
            co_return ok;
        },
        asio::use_awaitable);

    if (!published) {
        // Stopped disposition: stop() is already in progress. local_session still
        // holds the only owning reference (entry.session was not written).
        // Close the session terminally and return without entering the read pump. [INV-2a]
        (void)co_await local_session->close(fixpp::session::close_mode::terminal);
        co_return;
    }

    // Step 5: run the read-pump inline on the live transport until EOF.
    // Inline co_await keeps the pump in the counter_guard scope (stop()'s
    // total-cancel propagates into async_read_some; the counter decrements only
    // after the pump co_returns — mirror of run_accept_loop step 9). [T015/T016(e)]
    co_await run_read_pump(session->live_transport(), *session, entry.config);

    // Step 6 (T013): unpublish on normal exit (read-pump EOF / error unwind). [INV-2]
    co_await asio::co_spawn(
        engine.control_strand_,
        [&engine, &entry]() -> asio::awaitable<void> {
            co_await unpublish_entry(entry);
            // T023 (E-7/D-SNAP): republish the snapshot after live_transport reset.
            engine.publish_reader_snapshot_unlocked_();
        },
        asio::use_awaitable);
    co_return;
}

// ── start (FR-001/FR-003 / data-model "Public surface") ──────────────────────
// Non-blocking. co_spawns one per-role loop per registered session on exec_.
// Each loop co_awaits open() itself — cannot run in this synchronous void.

void Engine::start() {
    auto counter = std::make_shared<std::atomic<int>>(0);

    // gate-b/r1 #2 (TOCTOU fix): publish outstanding_counter_ BEFORE spawning any
    // loop.  A concurrent stop() between the first spawn and the old post-loop
    // assignment would observe outstanding_counter_==null, skip the join, then
    // registry_.clear() while a spawned loop still holds SessionEntry& → UAF.
    // With the assignment here, stop()'s join always finds a valid counter; if the
    // loop incremented the counter between the spawn and stop()'s load, stop() waits
    // for it; if stop() runs before start() finishes, it may drain a counter of 0
    // and still proceed safely because start() must not be called concurrently with
    // stop() (documented contract — full control-strand routing of start() is a
    // future improvement; the race window is bounded by the caller not overlapping
    // start() and stop() on the same engine). [INV-4a/C-0/E-7]
    outstanding_counter_ = counter;

    for (auto& [id, entry] : registry_) {
        // T005 (E-1/INV-1): create the per-session strand BEFORE the loop spawn.
        // One strand per session — INV-1 (never shared across sessions).
        // Created-but-not-yet-bound here; US1 (T009/T010) binds the role loop,
        // Session, and transport to this strand. [E-1/E-2/D1]
        entry.session_strand.emplace(asio::make_strand(exec_));

        // T009 (D3-B / E-3 / INV-3a): set the engine-only adopt-strand seam so
        // Session::open() stores the pre-created strand directly (strand_wrapped=true)
        // instead of re-wrapping in a second make_strand (the D1 anti-pattern).
        // This seam is NOT the public `already_serialized_executor` flag — D3-B
        // explicitly forbids inferring adoption from it (a user may set it under
        // per_session_strand and the flag does not guarantee the executor is a strand).
        entry.config.engine_adopt_strand = asio::any_io_executor{*entry.session_strand};

        ++(*counter);
        if (entry.session_role == SessionEntry::role::acceptor) {
            auto& scope_sig = accept_scope_signals_[id];  // default-constructs
            // T010 (E-1/INV-1/D2): spawn the accept loop ON the session strand so the
            // whole role loop (accept/handshake/read-pump/both teardown closes) runs
            // serialized on the per-session strand. The socket created inside the loop
            // via co_await this_coro::executor will inherit the strand executor —
            // satisfying D5/E-5/INV-7 (transport socket on session strand) without any
            // explicit socket rebind. [research D2; data-model E-1/INV-1; tasks T010/T011]
            asio::co_spawn(
                *entry.session_strand,
                run_accept_loop(engine_cfg_, *this, id, entry, scope_sig, counter),
                asio::bind_cancellation_slot(entry.session_cancel.slot(), asio::detached));
        } else {
            // T010: same for the connect loop — spawned on the per-session strand.
            // T013: pass *this so run_connect_loop can access control_strand_ and
            // stopped_ for the D-PUB awaited publish/unpublish. [data-model E-2/INV-2]
            asio::co_spawn(
                *entry.session_strand, run_connect_loop(engine_cfg_, *this, entry, counter),
                asio::bind_cancellation_slot(entry.session_cancel.slot(), asio::detached));
        }
    }
}

// ── stop (FR-011 / C5 / E-7; T014 teardown ordering) ─────────────────────────
// Idempotent total-cancellation teardown.
//
// T014 two-close ordering (data-model E-4/INV-4a/4b/5/6/6a; contract C-6):
//   Step 1. Guard + stopped_=true + total-cancel all loops.
//   Step 2. Per-session: dispatch transport.close() on session_strand BEFORE join.
//           (Wakes the idle in-flight read; serialized with its completion — BIO fix.)
//   Step 3. JOIN: yield until all outstanding loops exit + drain send_counter_.
//   Step 4. Per-session: dispatch Session::close(terminal) on session_strand AFTER
//           join + send-drain, BEFORE registry_.clear(). (Drains run_liveness_loop.)
//   Step 5. Clear registry (stable, no in-flight loops remain).
//
// Every per-session dispatch is a non-blocking co_spawn(session_strand, …,
// use_awaitable) — INV-5; never inline dispatch or blocking wait on a strand.
// The registry_ is iterated over a STABLE snapshot (no insert/erase between
// stopped_=true and clear() because stopped_ gates new entry, and the loops
// exit before clear()). [INV-6/INV-6a]

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

    // T018 (D0/INV-0): run the ENTIRE teardown body on the control strand so all
    // control-plane reads (registry_ iteration, entry.live_transport, entry.session,
    // counters) are serialized with the control-strand writes from publish_entry /
    // run_accept_loop map writes.  This closes race (b): publish_entry writes
    // entry.live_transport on the control strand; stop() step-2 reading it on the
    // same strand means no concurrent access.
    //
    // Two-strand topology: control_strand_ and each session_strand are DISTINCT
    // strands over the same io_context.  Posting from the control strand onto a
    // session strand (steps 2/4 below) or from a session strand onto the control
    // strand (publish_entry / run_accept_loop map-write) is always non-blocking —
    // no deadlock by construction (R6/C-2).
    //
    // F2 continuation: the inner coroutine re-disables cancellation so the shield
    // holds across the strand hop. [INV-0/D0/R6]
    co_await asio::co_spawn(
        control_strand_,
        [this]() -> asio::awaitable<void> {
            // Re-apply the cancellation shield on the control-strand frame.
            // The outer frame already disabled cancellation, but the inner co_spawn'd
            // coroutine gets a fresh cancellation state — disable it again so the
            // teardown runs to completion even if the calling context is cancelled.
            co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});

            // ── Step 1: set stopped_ true + total-cancel all loops ───────────────
            // Authoritative write on the control strand. sequentially-consistent
            // default store pairs with the acquire reads at the accept-loop gate and
            // Engine::send fast-fail. [INV-8]
            stopped_ = true;

            for (auto& [id, entry] : registry_)
                entry.session_cancel.emit(asio::cancellation_type::total);
            for (auto& [id, sig] : accept_scope_signals_) sig.emit(asio::cancellation_type::total);

            // ── Step 2 (T014/INV-4a): dispatch transport.close() on each session_strand ──
            // An established session's read-pump is blocked in async_read_some with no
            // peer EOF; total-cancel alone does not break the in-flight SSL read (see
            // BIO_ctrl crash in [[project_business_roundtrip_bio_ctrl_segv]]). The socket
            // MUST be closed to wake the read-pump. By dispatching close() on the session
            // strand we serialize it with the in-flight read's completion (BIO fix —
            // INV-4a).
            //
            // T013 D-PUB / T018: entry.live_transport is only written on the control
            // strand (publish_entry), and this step now also runs on the control strand —
            // so the read of entry.live_transport here is serialized with the write by
            // the control strand. Race (b) eliminated. [INV-0]
            //
            // The snapshot of live_transport taken here (before dispatching) is valid:
            //   - The publish sets it before the pump runs (INV-2).
            //   - The unpublish runs AFTER the pump exits, which is after the join (step 3).
            //   - So while we are here (pre-join), a published live_transport is stable.
            //
            // Each dispatch is a non-blocking co_spawn(session_strand, ...) — control
            // strand and session strand are distinct, so no deadlock. [INV-5/C-2]
            for (auto& [id, entry] : registry_) {
                if (entry.live_transport != nullptr && entry.session_strand.has_value()) {
                    // Capture the raw pointer BEFORE the co_spawn so the lambda owns a
                    // local copy (the entry reference remains valid across the co_await
                    // since the registry_ is stable between stopped_=true and clear()).
                    // [INV-6]
                    fixpp::transport::Transport* tp = entry.live_transport;
                    co_await asio::co_spawn(
                        *entry.session_strand,
                        [tp]() -> asio::awaitable<void> {
                            // close() is synchronous + idempotent. Running on the session
                            // strand serializes it with the in-flight async_read_some
                            // completion (the BIO_ctrl touch in map_error_code). [INV-4a]
                            tp->close();
                            co_return;
                        },
                        asio::use_awaitable);
                }
            }

            // ── Step 3: JOIN + send-drain ─────────────────────────────────────────
            // JOIN: yield until all loops have co_return'd.
            // The steady_timer uses this_coro::executor = control_strand_, which is
            // valid (timers can run on any executor). [INV-0]
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
            // the currently-bumped ones to decrement. [spec.md FR-012; R7]
            {
                asio::steady_timer t2{co_await asio::this_coro::executor};
                while (send_counter_->load(std::memory_order_acquire) > 0) {
                    t2.expires_after(std::chrono::milliseconds{0});
                    co_await t2.async_wait(asio::use_awaitable);
                }
            }

            // ── Step 4 (T014/INV-4b): dispatch Session::close(terminal) on each session_strand ──
            // All loops have exited (step 3 join). Now drain the per-session liveness loop:
            //   When Engine::stop() total-cancels a role loop, the loop's `co_await
            //   session.close()` throws operation_aborted BEFORE close() is entered, so a
            //   parked run_liveness_loop sleep_until is never joined — it survives to
            //   io_context shutdown, where its system_clock_source dereg guard touches the
            //   (freed) clock pimpl: a heap-use-after-free. This stop() coroutine is NOT
            //   cancelled (F2 + inner disable above), so close() runs to completion here,
            //   draining the liveness loop + write/seqnum gates. close() is idempotent
            //   (session_already_closed if a loop already drained it). [INV-4b]
            //
            // Dispatched on the session strand (non-blocking co_spawn) so it runs inside
            // the same domain as the read-pump and both teardown closes (INV-1/INV-5).
            // All loops have exited so the session strand is idle — the close runs
            // promptly.  Must precede registry_.clear() so no Session* is dereferenced
            // after free. [INV-6]
            for (auto& [id, entry] : registry_) {
                if (entry.session && entry.session_strand.has_value()) {
                    // Capture the shared_ptr so the session stays alive for the co_spawn
                    // duration (the entry reference is stable because the registry_ is not
                    // mutated between stopped_=true and the clear() in step 5). [INV-6]
                    std::shared_ptr<Session> sess = entry.session;
                    co_await asio::co_spawn(
                        *entry.session_strand,
                        [sess]() -> asio::awaitable<void> {
                            // co_await inside the session strand — runs on the same strand
                            // as run_liveness_loop, so the liveness sleep_until is drained
                            // before this co_return. [INV-4b]
                            (void)co_await sess->close(fixpp::session::close_mode::terminal);
                            co_return;
                        },
                        asio::use_awaitable);
                }
            }

            // ── Step 5: clear registry ────────────────────────────────────────────
            // Safe now: all loops have exited; Session objects may be freed.
            accept_scope_signals_.clear();
            listeners_.clear();
            listener_endpoints_.clear();
            registry_.clear();

            // T023 (E-7/D-SNAP): publish an empty snapshot after all maps are
            // cleared.  Any reader loading after this point sees an empty snapshot
            // (no sessions, no endpoints) — the correct post-stop() state. Called
            // on the control strand (we are already here via co_spawn(control_strand_,...)).
            publish_reader_snapshot_unlocked_();
        },
        asio::use_awaitable);

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

// ── Engine::send — T012 (023-engine-session-strand US1) ───────────────────────
//
// Any-thread-safe public outbound send entry point. [FR-006/007/013; research D6]
//
// T012 two-hop design (research D0/D4/R1/R7; data-model E-0; contract C-2; FR-012):
//   Step A: hop onto control_strand_ (NOT bare exec_) so registry reads + stopped_
//           check + send_counter_ bump are serialized with stop()'s mutations on
//           the control strand (D0/INV-0). Using exec_ would leave stop()'s
//           registry_.clear() racing send's registry_.find() under MT.
//           [[feedback_asio_post_resume_bounces_to_spawn_executor]]
//   Step B: on control_strand_ — registry lookup, keepalive capture (null-check only —
//           NOT Active check; fsm_state_ is single-writer on session strand C-1),
//           stopped_ check. If stopped_ → fail-fast (session_invalid_state_for_send).
//           Bump send_counter_ + RAII counter_guard to enroll in the send-drain
//           domain. The guard fires on co_return AND exception/cancel unwind so
//           a total-cancel from stop() cannot leave the counter positive. [R7]
//   Step C: hop to session_strand for Active check (fsm_state_ owned here, C-1),
//           toApp + Session::send. counter_guard dtor decrements after the hop
//           completes (or unwinds). [gate-b/r1 #1: Active check moved here]
//
//   Both hops are non-blocking co_spawn(strand, use_awaitable) — never dispatch or
//   blocking wait — so a callback-issued send (session→control→session) cannot
//   deadlock (FR-006/C-2). [research R1]
//
//   stop() drains send_counter_ BEFORE registry_.clear() so no send coroutine
//   can dereference engine_ after Engine::~Engine() runs. [R7]
//   Backpressure = the awaited result ([const §XV.15]).
asio::awaitable<core::expected_t<void>> Engine::send(SessionId const& id,
                                                     std::span<const std::byte> app_payload) {
    // Copy payload into a coroutine-frame-local buffer so the caller's span
    // (potentially stack-allocated) stays valid across all co_await suspensions.
    std::vector<std::byte> payload_copy(app_payload.begin(), app_payload.end());

    // T012/Step A: first-hop onto control_strand_ so registry reads and stopped_
    // check are serialized with stop()'s control-plane mutations (D0/INV-0).
    // Non-blocking co_spawn — the re-entrant case (session→control→session) is
    // safe because the session strand and control strand are distinct: posting
    // from the session strand onto the control strand never blocks. [C-2/R1]
    core::expected_t<void> result = co_await asio::co_spawn(
        control_strand_,
        [this, id,
         payload_copy = std::move(payload_copy)]() -> asio::awaitable<core::expected_t<void>> {
            // ── Step B: on control_strand_ — safe to read registry + stopped_ ──
            // All engine-global state (registry_, stopped_, send_counter_) is
            // serialized with stop()'s mutations through this strand. [D0/E-0]

            // T004/INV-8: acquire load — pairs with stop()'s write on the
            // control strand.
            if (stopped_.load(std::memory_order_acquire)) {
                co_return std::unexpected(core::error::session_invalid_state_for_send);
            }

            // Registry lookup — serialized with registry_.clear() by control_strand_.
            auto it = registry_.find(id);
            if (it == registry_.end()) {
                co_return std::unexpected(core::error::session_invalid_argument);
            }

            // Capture strong keepalive before any co_await (UAF guard).
            std::shared_ptr<Session> kl = it->second.session;

            // Session null (loop not yet published) → reject on the control strand.
            // NOTE: kl->state() (fsm_state_) is single-writer on the per-session
            // strand ([session.hpp:556]); reading it here (control strand) would be a
            // data race under MT. The Active check is moved entirely into Step C
            // (session-strand lambda) where fsm_state_ is owned.
            // [#1 gate-b/r1: data race fix — spec.md §C-1/C-0/D0]
            if (!kl) {
                co_return std::unexpected(core::error::session_invalid_state_for_send);
            }

            // R7: enroll this in-flight send in the send-drain domain with an
            // RAII guard so the decrement fires on co_return AND on
            // exception/total-cancel unwind. Keep the send_counter_ mechanism
            // STRICTLY SEPARATE from the US3 lookup() lease (R7 — draining on
            // app-held leases would hang stop(); weakening the barrier would
            // re-open the send-path UAF). [spec.md FR-012]
            ++(*send_counter_);
            counter_guard send_guard{send_counter_};

            // Reset cancellation state on the control-strand frame so total
            // cancellation has a defined policy for the bump→guard window.
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

            // ── Step C: hop to session_strand for toApp + Session::send ──
            // Non-blocking post onto the session strand — distinct from the
            // control strand, so no deadlock even for re-entrant sends. [C-2]
            auto strand_exec = kl->executor().underlying();
            core::expected_t<void> send_result = co_await asio::co_spawn(
                strand_exec,
                // payload_copy is captured by copy: the enclosing lambda's capture is
                // const (non-mutable lambda), so a std::move here would be a no-op.
                [kl, payload_copy]() -> asio::awaitable<core::expected_t<void>> {
                    // Enable total cancellation so stop()'s
                    // cancellation_type::total reaches Session::send.
                    // [[feedback_asio_cospawn_total_cancellation_default]]
                    co_await asio::this_coro::reset_cancellation_state(
                        asio::enable_total_cancellation());
                    // Active-state check on the session strand where fsm_state_ is
                    // owned (single-writer on session strand, C-1).  The control strand
                    // checks only for null session (not yet published); the
                    // fsm_state_ read is ONLY legal here. [gate-b/r1 #1 fix; C-0/C-1]
                    if (kl->state() != fsm_state::Active) {
                        co_return std::unexpected(core::error::session_invalid_state_for_send);
                    }
                    co_return co_await kl->send(
                        std::span<const std::byte>(payload_copy.data(), payload_copy.size()));
                },
                asio::use_awaitable);

            co_return send_result;
        },
        asio::use_awaitable);

    co_return result;
}

// ── acceptor_bound_endpoint (SC-010 delta #6 / T024 D-SNAP) ──────────────────
// Returns the OS-resolved bound endpoint of the acceptor's listener for `id`.
// Returns Endpoint{} (port==0) if the id is not a registered acceptor or the
// listener has not been built yet.
//
// T024 (D-SNAP): reads the atomically-published reader_snapshot_ (any-thread-
// safe, no strand, no std::mutex, no block). Signature UNCHANGED (Endpoint by
// value) — only the internal implementation switches from direct map access to
// snapshot load. [E-7/INV-9/D-SNAP; research D8 "acceptor_bound_endpoint()
// keeps its Endpoint-by-value signature"]

fixpp::transport::Endpoint Engine::acceptor_bound_endpoint(SessionId const& id) const {
    // Load the current snapshot — any-thread-safe acquire load. [E-7/D-SNAP]
    auto snap = reader_snapshot_.load(std::memory_order_acquire);
    if (!snap) return fixpp::transport::Endpoint{};  // defensive: never null post-ctor

    auto it = snap->endpoints.find(id);
    if (it == snap->endpoints.end()) return fixpp::transport::Endpoint{};
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
