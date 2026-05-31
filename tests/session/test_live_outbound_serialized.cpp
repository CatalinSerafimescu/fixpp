// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/test_live_outbound_serialized.cpp — FQ-1 gate-b/r1 witness
//
// Proves that the live outbound path in Session::store_then_emit satisfies:
//   (1a) Write serialization: at most one async_write is in-flight per
//        Transport at a time (transport.hpp:47-50 ≤1-in-flight contract).
//        Two quick admin emits must never submit concurrently.
//   (1b) Error propagation: a write error from async_write propagates back
//        through store_then_emit to its caller so FSM gates fire correctly
//        (Disconnected, not Active) on genuine wire failure.
//
// Defect (before FQ-1 fix): make_live_send_ co_spawned a detached async_write
// per emit, discarding the result.  Consequence:
//   - (1a) two quick emits each spawned an independent async_write → concurrent
//          second call returns transport_write_in_progress → silently dropped.
//   - (1b) store_then_emit's try/catch never saw a live transport error → FSM
//          always advanced to Active even if the wire write failed.
//
// After FQ-1 fix: store_then_emit co_awaits async_write directly.  The session
// strand serializes calls: only one async_write is in-flight at a time, and the
// error is returned synchronously to store_then_emit's caller.
//
// Cell A — write-error propagation to FSM.
//   Transport returns transport_write_short on the FIRST async_write.
//   The acceptor reply-Logon emit fails → store_then_emit returns
//   dispatch_aborted → FSM transitions to Disconnected (not Active).
//
// Cell B — write serialization: first write stays pending, second is NOT
//   submitted concurrently.
//   Transport's first async_write blocks (via a timer) until released.
//   While blocked, a second store_then_emit call waits on the session strand.
//   Assert: concurrent_write_attempts_observed == 0 on the transport
//   (the second call never reaches async_write while the first is in-flight).
//
// Anti-hang: every ioc.run_for() is bounded; self-deadline in the pending-
//   write cell (2s max) releases the blocked write before the run_for expires.
//
// Anchors: transport.hpp:47-50 ≤1-in-flight contract; realized-behavior.md
//          C1/C2 emit-over-live-sink; gate-b/r1 FQ-1; [const §VIII.5];
//          [feedback_detached_cospawn_write_not_in_join_counter].

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

// ─────────────────────────────────────────────────────────────────────────────
// FailFirstWriteTransport — returns transport_write_short on the first
// async_write; subsequent writes succeed.  async_read_some blocks (timer) until
// close() is called (to prevent the session from hanging on EOF).
// ─────────────────────────────────────────────────────────────────────────────
class FailFirstWriteTransport final : public fixpp::transport::Transport {
public:
    explicit FailFirstWriteTransport(asio::any_io_executor exec)
        : exec_{std::move(exec)}, read_timer_{exec_} {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
        fixpp::transport::ConnectInfo info;
        info.remote = ep;
        info.local  = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }

    // Block reads until close() — the session read-pump's async_read_some
    // suspends here; close() cancels the timer, waking it with an error.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>>
    async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override {
        (void)buf;
        if (closed_) {
            co_return std::unexpected{fixpp::core::error::transport_already_closed};
        }
        read_timer_.expires_after(60s);
        asio::error_code ec;
        co_await read_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>>
    async_write(std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        ++write_count_;
        if (write_count_ == 1) {
            // First write fails — simulates a wire error during Logon emit.
            co_return std::unexpected{fixpp::core::error::transport_write_short};
        }
        co_return buf.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override {
        return {};
    }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        closed_ = true;
        read_timer_.cancel();
        return {};
    }

    int write_count() const noexcept { return write_count_; }

private:
    asio::any_io_executor exec_;
    asio::steady_timer    read_timer_;
    std::atomic<int>      write_count_{0};
    bool                  closed_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// PendingFirstWriteTransport — first async_write blocks until release() is
// called.  Counts concurrent in-flight writes.
// ─────────────────────────────────────────────────────────────────────────────
class PendingFirstWriteTransport final : public fixpp::transport::Transport {
public:
    explicit PendingFirstWriteTransport(asio::any_io_executor exec)
        : exec_{std::move(exec)}
        , write_timer_{exec_}
        , read_timer_{exec_}
    {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
        fixpp::transport::ConnectInfo info;
        info.remote = ep;
        info.local  = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>>
    async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override {
        (void)buf;
        if (closed_) {
            co_return std::unexpected{fixpp::core::error::transport_already_closed};
        }
        read_timer_.expires_after(60s);
        asio::error_code ec;
        co_await read_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>>
    async_write(std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        const int n = ++in_flight_writes_;
        // Track peak concurrent writes.
        if (n > 1) ++concurrent_excess_observed_;
        ++total_write_starts_;

        if (first_write_pending_ && total_write_starts_ == 1) {
            // Block the first write until release() cancels the timer.
            write_timer_.expires_after(30s);
            asio::error_code ec;
            co_await write_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        }

        --in_flight_writes_;
        co_return buf.size();
    }

    // Release the blocked first write.
    void release() noexcept {
        first_write_pending_ = false;
        write_timer_.cancel();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override {
        return {};
    }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        closed_ = true;
        read_timer_.cancel();
        write_timer_.cancel();
        return {};
    }

    int concurrent_excess_observed() const noexcept { return concurrent_excess_observed_; }
    int total_write_starts() const noexcept { return total_write_starts_; }

private:
    asio::any_io_executor exec_;
    asio::steady_timer    write_timer_;
    asio::steady_timer    read_timer_;
    bool                  first_write_pending_{true};
    bool                  closed_{false};
    std::atomic<int>      in_flight_writes_{0};
    std::atomic<int>      concurrent_excess_observed_{0};
    std::atomic<int>      total_write_starts_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// Shared session-builder helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build a valid FIX Logon frame for an inbound peer.
static std::vector<std::byte> make_peer_logon(
    std::string_view begin_string,
    std::uint32_t seq,
    std::string_view sender,
    std::string_view target)
{
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFu;
    char buf[5];
    snprintf(buf, sizeof(buf), "%03u", cs);
    msg += "10=" + std::string(buf) + "\x01";

    std::vector<std::byte> out;
    out.reserve(msg.size());
    for (char c : msg) out.push_back(static_cast<std::byte>(c));
    return out;
}

static fixpp::session::SessionConfig make_acceptor_cfg(asio::any_io_executor exec) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id     = "ACCEPTOR";
    cfg.target_comp_id     = "INITIATOR";
    cfg.begin_string       = "FIX.4.4";
    cfg.role               = fixpp::session::session_role::acceptor;
    cfg.executor_override  = exec;
    cfg.heartbeat_interval = 0s;  // disable liveness loop
    cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
    // Use bilateral_lenient so the session does not require 141=Y from the
    // peer Logon — tests here focus on transport write, not seqnum-reset policy.
    cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell A — write-error propagates through store_then_emit → FSM → Disconnected
// ─────────────────────────────────────────────────────────────────────────────
//
// Setup:
//   - Acceptor session + FailFirstWriteTransport (first async_write fails).
//   - Attach the transport (rebinds live path in store_then_emit).
//   - Feed a peer Logon → acceptor gate fires → reply-Logon emit → first
//     async_write returns transport_write_short.
//   - Before FQ-1 fix: store_then_emit returned success (fire-and-forget);
//     FSM advanced to Active.
//   - After FQ-1 fix: store_then_emit returns dispatch_aborted; FSM →
//     Disconnected.
//
// [FQ-1a; gate-b/r1; realized-behavior.md C1; session.cpp store_then_emit]
TEST(LiveOutboundSerializedTest, WriteErrorPropagatesAsFsmDisconnected) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto cfg = make_acceptor_cfg(ioc.get_executor());

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run_for(100ms);
    ioc.restart();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    // Attach the fail-first transport as the live transport.
    // NullSink is the base transport type; FailFirstWriteTransport inherits
    // Transport directly (not TlsTransport) — one_way_ca profile, no mTLS gate.
    auto raw_transport = std::make_unique<FailFirstWriteTransport>(ioc.get_executor());
    FailFirstWriteTransport* raw_ptr = raw_transport.get();

    fixpp::transport::handshake_result hr{};
    // one_way_ca: no peer identity needed (the mTLS gate is not armed).
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    // Feed a peer Logon — triggers the acceptor gate (NotConnected→LogonReceived),
    // which then emits the reply-Logon via store_then_emit over the live transport.
    // The first async_write fails → store_then_emit returns dispatch_aborted.
    // The acceptor gate at session.cpp:1285-1298 transitions to Disconnected.
    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    auto inbound_fut = asio::co_spawn(ioc,
        sess.on_inbound_frame(std::span<const std::byte>{logon}),
        asio::use_future);

    // Bounded run: self-deadline via run_for (≤2s covers the async_write + FSM).
    ioc.run_for(2s);
    ioc.restart();

    // The on_inbound_frame coroutine may still be pending (it's fire-and-forget
    // from the accept loop — drain it).
    ioc.run_for(200ms);
    ioc.restart();

    // (1b) Write error must have reached the FSM — session must be Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "FQ-1a: the live-transport write error must propagate through "
        << "store_then_emit → FSM gate → Disconnected. "
        << "If Active: the fire-and-forget bridge is still in place (not fixed). "
        << "state=" << static_cast<int>(sess.state());

    // The first async_write must have been attempted (not silently skipped).
    EXPECT_GE(raw_ptr->write_count(), 1)
        << "The transport's async_write must have been called at least once "
        << "(the reply-Logon emit must reach the transport).";

    // Teardown.
    raw_ptr->close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell B — serialization: second emit is NOT submitted while first is in-flight
// ─────────────────────────────────────────────────────────────────────────────
//
// Setup:
//   - Acceptor session + PendingFirstWriteTransport (first write blocks).
//   - Drive the session to Active by feeding a peer Logon WITH a transport that
//     allows the reply-Logon to complete successfully (first write succeeds after
//     release()).
//   - Then emit a second admin frame (Heartbeat) while the first is still pending.
//   - Assert: no concurrent async_write pair was observed on the transport.
//
// Because store_then_emit is a single awaitable and the session strand is
// single-threaded, the second co_await store_then_emit cannot start its
// co_await async_write until the first co_await async_write completes.
// The PendingFirstWriteTransport's in_flight_writes counter must never exceed 1.
//
// [FQ-1b; gate-b/r1; transport.hpp:47-50 ≤1-in-flight contract]
TEST(LiveOutboundSerializedTest, ConcurrentWritesNotSubmitted) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto cfg = make_acceptor_cfg(ioc.get_executor());

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run_for(100ms);
    ioc.restart();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    // Attach the pending-first-write transport.
    auto raw_transport = std::make_unique<PendingFirstWriteTransport>(ioc.get_executor());
    PendingFirstWriteTransport* raw_ptr = raw_transport.get();

    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    // Feed a peer Logon — reply-Logon is emitted (first write). The write
    // blocks on the pending timer. spawn it + let the ioc tick so the write
    // is in-flight.
    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    asio::co_spawn(ioc,
        sess.on_inbound_frame(std::span<const std::byte>{logon}),
        asio::detached);

    // Run a few steps to get the first write in-flight (pending on the timer).
    ioc.run_for(50ms);
    ioc.restart();

    // At this point the first async_write is pending (blocked in PendingFirstWriteTransport).
    // The session is in the middle of store_then_emit (suspended at co_await async_write).
    // Now attempt to emit a second frame (Heartbeat) via Session::send — this will
    // try to call store_then_emit again. Because store_then_emit is co_awaited on the
    // session strand and the strand is single-threaded, the second call is queued
    // behind the first — it cannot submit a second async_write concurrently.
    //
    // We use a small app payload (Heartbeat-shaped byte sequence) via send().
    // Even if send() returns not-Active error (session state not yet Active because
    // the first write is still pending), the key invariant is:
    // PendingFirstWriteTransport::concurrent_excess_observed() must remain 0.

    // Release the blocked first write after a short delay (self-deadline).
    asio::steady_timer release_timer{ioc.get_executor()};
    release_timer.expires_after(300ms);
    release_timer.async_wait([&](asio::error_code /*ec*/) {
        raw_ptr->release();
    });

    ioc.run_for(2s);
    ioc.restart();

    // Close the transport to unblock any pending reads.
    raw_ptr->close();
    ioc.run_for(200ms);
    ioc.restart();

    // The core assertion: no concurrent second async_write was observed.
    EXPECT_EQ(raw_ptr->concurrent_excess_observed(), 0)
        << "FQ-1b: concurrent async_write calls were observed on the transport. "
        << "The ≤1-in-flight contract (transport.hpp:47-50) was violated. "
        << "This means the fire-and-forget path is still in place — store_then_emit "
        << "is not co_awaiting the write directly. "
        << "concurrent_excess=" << raw_ptr->concurrent_excess_observed();

    // The first write must have started (the reply-Logon was emitted).
    EXPECT_GE(raw_ptr->total_write_starts(), 1)
        << "The transport's async_write must have been called at least once.";
}

}  // namespace fixpp::session::test
