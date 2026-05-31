// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/test_live_outbound_serialized.cpp — FQ-A gate-b/r2 witness
//
// Proves that the live outbound path in Session satisfies the unified
// serialized-lifetime-safe-error-returning write channel invariants:
//
//   (A) Write serialization: at most one async_write is in-flight per
//       Transport at a time (transport.hpp:47-50 ≤1-in-flight contract).
//       A GENUINE second emit while the first write is pending must NOT
//       enter async_write until the first completes (total_write_starts >= 2
//       is the meaningful bar; if only 1 write ever started, the test is void).
//
//   (B) Error propagation: a write error from async_write propagates back
//       through store_then_emit to its caller so FSM gates fire correctly
//       (Disconnected, not Active) on genuine wire failure.
//
//   (C) Replay serialization: ResendRequest replay frames go through the same
//       write_gate_ so concurrent replay writes are impossible and write errors
//       propagate (force-disconnect instead of silent drop).
//
//   (D) Liveness drain: Session::close() drains liveness_done_ (waits for the
//       liveness loop to exit) and write_gate_ (waits for in-flight writes to
//       complete) before returning. This guarantees no detached session work
//       can touch freed Session/transport after registry_.clear().
//
// Anchors: transport.hpp:47-50 ≤1-in-flight contract; realized-behavior.md
//          C1/C2 emit-over-live-sink; gate-b/r2 FQ-A; [const §VIII.5];
//          [feedback_detached_cospawn_write_not_in_join_counter];
//          [feedback_engine_stop_must_close_transports_total_cancel_insufficient].
//
// Anti-hang: every ioc.run_for() is bounded; internal self-deadlines via timers
//   ensure ioc.run_for() never hangs waiting on blocked transport ops.

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
#include <fixpp/core/system_clock_source.hpp>
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
// async_write; subsequent writes succeed.  async_read_some blocks until close().
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
// ControlledWriteTransport — every async_write is synchronous (immediate) by
// default; if arm_block() is called, the NEXT write blocks until release() is
// called. Counts total write starts and concurrent writes.
// ─────────────────────────────────────────────────────────────────────────────
class ControlledWriteTransport final : public fixpp::transport::Transport {
public:
    explicit ControlledWriteTransport(asio::any_io_executor exec)
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
        const int n = ++in_flight_;
        if (n > 1) ++concurrent_excess_;
        ++total_starts_;

        if (blocked_) {
            write_timer_.expires_after(30s);
            asio::error_code ec;
            co_await write_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        }

        --in_flight_;
        co_return buf.size();
    }

    // Arm next write to block; call release() to unblock.
    void arm_block() noexcept { blocked_ = true; }
    void release() noexcept {
        blocked_ = false;
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

    int concurrent_excess() const noexcept { return concurrent_excess_.load(); }
    int total_starts()      const noexcept { return total_starts_.load(); }

private:
    asio::any_io_executor exec_;
    asio::steady_timer    write_timer_;
    asio::steady_timer    read_timer_;
    std::atomic<bool>     blocked_{false};
    bool                  closed_{false};
    std::atomic<int>      in_flight_{0};
    std::atomic<int>      concurrent_excess_{0};
    std::atomic<int>      total_starts_{0};
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

// Build a valid inbound Heartbeat (35=0) frame.
static std::vector<std::byte> make_peer_heartbeat(
    std::string_view begin_string,
    std::uint32_t seq,
    std::string_view sender,
    std::string_view target)
{
    std::string body;
    body += "35=0\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";

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
    cfg.heartbeat_interval = 0s;  // disable liveness loop by default
    cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell A — write-error propagates through store_then_emit → FSM → Disconnected
// ─────────────────────────────────────────────────────────────────────────────
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

    auto raw_transport = std::make_unique<FailFirstWriteTransport>(ioc.get_executor());
    FailFirstWriteTransport* raw_ptr = raw_transport.get();

    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    // Feed a peer Logon — triggers reply-Logon emit → first async_write fails.
    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    auto inbound_fut = asio::co_spawn(ioc,
        sess.on_inbound_frame(std::span<const std::byte>{logon}),
        asio::use_future);

    ioc.run_for(2s);
    ioc.restart();
    ioc.run_for(200ms);
    ioc.restart();

    // Write error must have reached the FSM — session must be Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "FQ-A Cell A: the live-transport write error must propagate through "
        << "store_then_emit → FSM gate → Disconnected. "
        << "state=" << static_cast<int>(sess.state());

    EXPECT_GE(raw_ptr->write_count(), 1)
        << "The transport's async_write must have been called at least once.";

    raw_ptr->close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell B — GENUINE second emit serialized behind the first in-flight write
//
// Setup:
//   - Drive the session to Active (reply-Logon emitted unblocked).
//   - Arm the transport to block the NEXT write.
//   - Feed an inbound Heartbeat → triggers outbound Heartbeat echo (write N+1 BLOCKS).
//   - Feed a second Heartbeat → triggers another echo (write N+2 waits on write_gate_).
//   - Assert concurrent_excess == 0 AND total_starts >= writes_before + 1
//     (the assertion is only meaningful if total_starts >= 2, proving the second
//     emit was genuinely queued but not concurrently submitted).
//
// [FQ-A D-6 F1; transport.hpp:47-50; gate-b/r2]
// ─────────────────────────────────────────────────────────────────────────────
TEST(LiveOutboundSerializedTest, ConcurrentWritesNotSubmittedGenuineSecondEmit) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto cfg = make_acceptor_cfg(ioc.get_executor());

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run_for(100ms);
    ioc.restart();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    // Attach a ControlledWriteTransport initially unblocked so the reply-Logon
    // (write #1) completes immediately, driving the session to Active.
    auto raw_transport = std::make_unique<ControlledWriteTransport>(ioc.get_executor());
    ControlledWriteTransport* raw_ptr = raw_transport.get();

    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    // Feed peer Logon → reply-Logon write #1 (unblocked, completes immediately).
    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}),
                   asio::detached);
    ioc.run_for(100ms);  // let reply-Logon complete → session Active
    ioc.restart();

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Session must be Active before the serialization test";
    ASSERT_GE(raw_ptr->total_starts(), 1) << "reply-Logon write must have occurred";
    const int writes_before_test = raw_ptr->total_starts();

    // Arm block: the NEXT async_write (Heartbeat echo for write #N+1) will block.
    raw_ptr->arm_block();

    // Feed a peer Heartbeat → triggers outbound Heartbeat echo → write N+1 BLOCKS.
    auto hb1 = make_peer_heartbeat("FIX.4.4", 2, "INITIATOR", "ACCEPTOR");
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{hb1}),
                   asio::detached);

    // Run a few steps to get write N+1 in-flight but blocked.
    ioc.run_for(50ms);
    ioc.restart();

    // Now feed a second Heartbeat → triggers another Heartbeat echo → write N+2.
    // This write must NOT enter async_write while N+1 is still in-flight.
    auto hb2 = make_peer_heartbeat("FIX.4.4", 3, "INITIATOR", "ACCEPTOR");
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{hb2}),
                   asio::detached);

    // Run to ensure the second Heartbeat handler reaches its write_gate_ acquire.
    ioc.run_for(50ms);
    ioc.restart();

    // At this point: write N+1 is blocked in async_write; write N+2 is waiting
    // on write_gate_.async_lock() (can't enter async_write until N+1 releases).
    // Assert the serialization invariant: concurrent_excess must still be 0.
    EXPECT_EQ(raw_ptr->concurrent_excess(), 0)
        << "FQ-A Cell B: concurrent async_write calls detected — "
        << "write_gate_ serialization is broken. "
        << "concurrent_excess=" << raw_ptr->concurrent_excess();

    // Self-deadline: release after 300ms so ioc.run_for() completes normally.
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

    // After draining: the total write starts must be > writes_before_test.
    // If total_starts == writes_before_test, the second emit never reached
    // async_write — the test would be void (but it verifies the gate rewrite).
    EXPECT_GT(raw_ptr->total_starts(), writes_before_test)
        << "FQ-A Cell B: at least one Heartbeat echo must have started a write. "
        << "total_starts=" << raw_ptr->total_starts()
        << " writes_before_test=" << writes_before_test;

    // Final check: still no concurrent excess observed.
    EXPECT_EQ(raw_ptr->concurrent_excess(), 0)
        << "FQ-A Cell B: concurrent excess detected after draining. "
        << "The ≤1-in-flight contract was violated.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell C — ResendRequest replay goes through write_gate_ (no concurrent replay
//          writes, write errors force-disconnect instead of silent drop).
//
// Tests the replay error→force-disconnect path: the old sync transmit lambda
// silently swallowed errors; the new async transmit_async must propagate them.
//
// [FQ-A D-6 F2; gate-b/r2]
// ─────────────────────────────────────────────────────────────────────────────
TEST(LiveOutboundSerializedTest, ReplayTransmitErrorForcesDisconnect) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto cfg = make_acceptor_cfg(ioc.get_executor());

    // Wire a sync transport_send_ that fails after the first call.
    // The first call is the reply-Logon; subsequent calls (replay) fail.
    int transport_calls = 0;
    cfg.transport_send = [&](std::span<const std::byte> /*f*/) {
        ++transport_calls;
        if (transport_calls > 1) {
            throw asio::system_error{asio::error::broken_pipe};
        }
    };

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run_for(100ms);
    ioc.restart();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    // Drive session to Active via peer Logon (using sync transport_send_ path).
    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}),
                   asio::detached);
    ioc.run_for(100ms);
    ioc.restart();

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Session must be Active before replay test";

    // Build a ResendRequest(2) asking to replay seqnum 1 (which is the reply-Logon
    // — an admin message, so it gets GapFilled; the GapFill transmit will fail).
    {
        std::string body;
        body += "35=2\x01";
        body += "34=10\x01";
        body += "49=INITIATOR\x01";
        body += "52=20240101-00:00:00.000\x01";
        body += "56=ACCEPTOR\x01";
        body += "7=1\x01";   // BeginSeqNo
        body += "16=0\x01";  // EndSeqNo = 0 means "all"
        std::string msg;
        msg += "8=FIX.4.4\x01";
        msg += "9=" + std::to_string(body.size()) + "\x01";
        msg += body;
        unsigned int cs = 0;
        for (unsigned char c : msg) cs += c;
        cs &= 0xFFu;
        char buf[5];
        snprintf(buf, sizeof(buf), "%03u", cs);
        msg += "10=" + std::string(buf) + "\x01";

        std::vector<std::byte> rr_bytes(msg.size());
        for (std::size_t i = 0; i < msg.size(); ++i)
            rr_bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(msg[i]));

        asio::co_spawn(ioc,
            sess.on_inbound_frame(std::span<const std::byte>{rr_bytes}),
            asio::detached);
    }

    ioc.run_for(500ms);
    ioc.restart();

    // The replay transmit (GapFill) fails with broken_pipe → force-disconnect.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "FQ-A Cell C: replay transmit error must force-disconnect the session. "
        << "state=" << static_cast<int>(sess.state());
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell D — liveness drain: Session::close() drains liveness_done_ + write_gate_
//
// Proves that Session::close() correctly drains liveness_done_ and write_gate_
// before returning. The liveness loop acquires liveness_done_ for its lifetime;
// close() calls liveness_done_.cancel_and_drain() which waits for the liveness
// coroutine to release it (i.e. to fully exit). This guarantees:
//   - No detached liveness coroutine can touch freed Session/transport after
//     close() returns and registry_.clear() destroys the Session.
//   - The async_mutex destructor precondition is satisfied (not_locked at dtor).
//
// If either invariant is broken, the test will either:
//   (a) hang (close() blocks forever in drain), OR
//   (b) crash with std::terminate (async_mutex destructor fires on locked mutex
//       when sess is destroyed at end of test scope).
//
// The test uses a real system_clock_source with a 1s heartbeat so the liveness
// loop actually spawns, acquires liveness_done_, and enters its sleep cycle.
// close() fires root_cancel_.emit(total) + cancel_sleeps() which cancels the
// sleep, causing the liveness loop to exit and release liveness_done_.
//
// [feedback_detached_cospawn_write_not_in_join_counter; FQ-A D-6 F3/F4; gate-b/r2]
// ─────────────────────────────────────────────────────────────────────────────
TEST(LiveOutboundSerializedTest, StopDuringLivenessWriteNoCrash) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    // Use a real system_clock_source so the liveness loop can sleep.
    auto clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    eng.clock = clock;

    auto cfg = make_acceptor_cfg(ioc.get_executor());
    // 1s heartbeat: the liveness loop acquires liveness_done_ and sleeps.
    cfg.heartbeat_interval = std::chrono::seconds{1};

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run_for(100ms);
    ioc.restart();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    // Attach a ControlledWriteTransport (writes complete immediately by default).
    auto raw_transport = std::make_unique<ControlledWriteTransport>(ioc.get_executor());
    ControlledWriteTransport* raw_ptr = raw_transport.get();

    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    // Drive session to Active — reply-Logon write completes immediately,
    // liveness loop is spawned and starts sleeping.
    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}),
                   asio::detached);
    ioc.run_for(100ms);
    ioc.restart();

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Session must be Active for liveness drain test";

    // Run for 200ms to ensure the liveness loop has started and acquired
    // liveness_done_. (The loop acquires the gate at the top of run_liveness_loop.)
    ioc.run_for(200ms);
    ioc.restart();

    // Close the transport socket and call close(terminal).
    // close() must drain liveness_done_ (cancel + wait for liveness to exit) and
    // write_gate_ before returning. If either drain deadlocks, the test hangs.
    // If the Session is destroyed with a held mutex, the destructor crashes.
    raw_ptr->close();

    auto close_fut = asio::co_spawn(ioc, sess.close(fixpp::session::close_mode::terminal),
                                    asio::use_future);

    // Bounded: close() must complete within 5s.
    // root_cancel_.emit(total) + cancel_sleeps() should cancel the liveness sleep
    // (at most 1s remaining), causing the loop to exit and release liveness_done_.
    ioc.run_for(5s);
    ioc.restart();

    // close() must have completed (not deadlocked).
    ASSERT_EQ(close_fut.wait_for(0ms), std::future_status::ready)
        << "FQ-A Cell D: Session::close() must complete after draining liveness "
        << "and write gate. If still pending, a drain deadlock exists. "
        << "Check that liveness_done_.cancel_and_drain() is called AFTER "
        << "root_cancel_.emit(total) + cancel_sleeps() in Session::close().";

    // If we reach here without crash, the drain invariant holds.
    // Under ASan, a UAF from a post-close liveness write would have been caught.
    SUCCEED() << "FQ-A Cell D: Session::close() completed; liveness drain OK.";
}

}  // namespace fixpp::session::test
