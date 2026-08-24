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

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <future>
#include <memory>
#include <span>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

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
        info.local = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override {
        (void)buf;
        if (closed_) {
            co_return std::unexpected{fixpp::core::error::transport_already_closed};
        }
        read_timer_.expires_after(60s);
        asio::error_code ec;
        co_await read_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        ++write_count_;
        if (write_count_ == 1) {
            co_return std::unexpected{fixpp::core::error::transport_write_short};
        }
        co_return buf.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        closed_ = true;
        read_timer_.cancel();
        return {};
    }

    int write_count() const noexcept { return write_count_; }

private:
    asio::any_io_executor exec_;
    asio::steady_timer read_timer_;
    std::atomic<int> write_count_{0};
    bool closed_{false};
};

class FailNthWriteTransport final : public fixpp::transport::Transport {
public:
    FailNthWriteTransport(asio::any_io_executor exec, int fail_on_write)
        : exec_{std::move(exec)}, read_timer_{exec_}, fail_on_write_{fail_on_write} {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
        fixpp::transport::ConnectInfo info;
        info.remote = ep;
        info.local = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override {
        (void)buf;
        if (closed_) {
            co_return std::unexpected{fixpp::core::error::transport_already_closed};
        }
        read_timer_.expires_after(60s);
        asio::error_code ec;
        co_await read_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        const int current = ++write_count_;
        if (current == fail_on_write_) {
            co_return std::unexpected{fixpp::core::error::transport_write_short};
        }
        co_return buf.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        closed_ = true;
        read_timer_.cancel();
        return {};
    }

    int write_count() const noexcept { return write_count_.load(); }

private:
    asio::any_io_executor exec_;
    asio::steady_timer read_timer_;
    int fail_on_write_;
    std::atomic<int> write_count_{0};
    bool closed_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// ControlledWriteTransport — every async_write is synchronous (immediate) by
// default; if arm_block() is called, the NEXT write blocks until release() is
// called. Counts total write starts and concurrent writes.
// ─────────────────────────────────────────────────────────────────────────────
class ControlledWriteTransport final : public fixpp::transport::Transport {
public:
    explicit ControlledWriteTransport(asio::any_io_executor exec)
        : exec_{std::move(exec)}, write_timer_{exec_}, read_timer_{exec_} {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
        fixpp::transport::ConnectInfo info;
        info.remote = ep;
        info.local = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override {
        (void)buf;
        if (closed_) {
            co_return std::unexpected{fixpp::core::error::transport_already_closed};
        }
        read_timer_.expires_after(60s);
        asio::error_code ec;
        co_await read_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        const int n = ++in_flight_;
        if (n > 1) ++concurrent_excess_;
        ++total_starts_;

        if (blocked_) {
            write_timer_.expires_after(30s);
            asio::error_code ec;
            co_await write_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
            if (closed_) {
                --in_flight_;
                co_return std::unexpected{fixpp::core::error::transport_already_closed};
            }
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

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        closed_ = true;
        read_timer_.cancel();
        write_timer_.cancel();
        return {};
    }

    int concurrent_excess() const noexcept { return concurrent_excess_.load(); }
    int total_starts() const noexcept { return total_starts_.load(); }

private:
    asio::any_io_executor exec_;
    asio::steady_timer write_timer_;
    asio::steady_timer read_timer_;
    std::atomic<bool> blocked_{false};
    bool closed_{false};
    std::atomic<int> in_flight_{0};
    std::atomic<int> concurrent_excess_{0};
    std::atomic<int> total_starts_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// Shared session-builder helpers
// ─────────────────────────────────────────────────────────────────────────────

// Minimal VALID application payload for Session::send. The 020 send path
// validates the opaque payload fail-closed (FR-016): it must lead with a single
// 35= MsgType field and carry no session header/trailer tags. These outbound-
// serialization tests only need the send to reach the (blocked) write path.
static std::vector<std::byte> make_min_app_payload() {
    static const char kP[] = "35=D\x01";
    std::vector<std::byte> v;
    for (const char* p = kP; *p; ++p) v.push_back(static_cast<std::byte>(*p));
    return v;
}

// Build a valid FIX Logon frame for an inbound peer.
// Current wall-clock UTC as a FIX UTCTimestamp "YYYYMMDD-HH:MM:SS.mmm".
// Real-clock acceptor tests must stamp a *fresh* SendingTime(52) so the 038
// acceptor first-Logon SendingTime(52) MaxLatency guard admits the peer Logon
// (the legacy fixed "20240101-00:00:00.000" literal is now stale → rejected).
static std::string utc_now_fix_timestamp() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

static std::vector<std::byte> make_peer_logon(std::string_view begin_string, std::uint32_t seq,
                                              std::string_view sender, std::string_view target) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + utc_now_fix_timestamp() + "\x01";
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
static std::vector<std::byte> make_peer_heartbeat(std::string_view begin_string, std::uint32_t seq,
                                                  std::string_view sender,
                                                  std::string_view target) {
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

// Inbound TestRequest(35=1) with a TestReqID(112). The session replies with a
// Heartbeat echoing the 112 — the correct outbound-emit trigger (an inbound
// Heartbeat is never answered; data-model.md:22). Used to drive the live-write
// path that the retired Heartbeat-echo used to drive.
static std::vector<std::byte> make_peer_test_request(std::string_view begin_string,
                                                     std::uint32_t seq, std::string_view sender,
                                                     std::string_view target,
                                                     std::string_view test_req_id) {
    std::string body;
    body += "35=1\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "112=" + std::string(test_req_id) + "\x01";

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
    cfg.sender_comp_id = "ACCEPTOR";
    cfg.target_comp_id = "INITIATOR";
    cfg.begin_string = "FIX.4.4";
    cfg.role = fixpp::session::session_role::acceptor;
    cfg.executor_override = exec;
    cfg.heartbeat_interval = 0s;  // disable liveness loop by default
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
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
    auto inbound_fut = asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}),
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

TEST(LiveOutboundSerializedTest, TestRequestReplyWriteErrorDisconnectsSession) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto cfg = make_acceptor_cfg(ioc.get_executor());

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, open_fut, 2s))
        << "open() timed out";
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    auto raw_transport = std::make_unique<FailNthWriteTransport>(ioc.get_executor(), 2);
    auto* raw_ptr = raw_transport.get();
    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    auto logon_fut = asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}),
                                    asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, logon_fut, 2s))
        << "peer Logon timed out";
    ASSERT_TRUE(logon_fut.get().has_value()) << "peer Logon failed";
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active) << "must reach Active";

    auto test_req = make_peer_test_request("FIX.4.4", 2, "INITIATOR", "ACCEPTOR", "TR1");
    auto hb_fut = asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{test_req}),
                                 asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, hb_fut, 2s))
        << "TestRequest handling timed out";
    ASSERT_EQ(hb_fut.wait_for(0ms), std::future_status::ready) << "TestRequest handling timed out";
    auto hb_r = hb_fut.get();

    EXPECT_FALSE(hb_r.has_value())
        << "live Heartbeat-reply write failure must surface back to on_inbound_frame";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "live Heartbeat-reply write failure must force Disconnected";
    EXPECT_EQ(raw_ptr->write_count(), 2)
        << "exactly the reply-Logon and the failing Heartbeat reply should have written";

    raw_ptr->close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell B — GENUINE second emit serialized behind the first in-flight write
//
// Setup:
//   - Drive the session to Active (reply-Logon emitted unblocked).
//   - Arm the transport to block the NEXT write.
//   - Feed an inbound TestRequest(35=1) → triggers outbound Heartbeat reply
//     (write N+1 BLOCKS).
//   - Feed a second TestRequest(35=1) → triggers another Heartbeat reply
//     (write N+2 waits on write_gate_).
//   - Assert concurrent_excess == 0 AND total_starts >= writes_before + 2
//     (both Heartbeat replies must have started a write — the second proves it
//     was genuinely queued behind the first and not dropped).
//
// [FQ-A D-6 F1; transport.hpp:47-50; gate-b/r2; gate-b/r1]
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
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}), asio::detached);
    ioc.run_for(100ms);  // let reply-Logon complete → session Active
    ioc.restart();

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Session must be Active before the serialization test";
    ASSERT_GE(raw_ptr->total_starts(), 1) << "reply-Logon write must have occurred";
    const int writes_before_test = raw_ptr->total_starts();

    // Arm block: the NEXT async_write (Heartbeat reply for write #N+1) will block.
    raw_ptr->arm_block();

    // Feed a peer TestRequest → triggers outbound Heartbeat reply → write N+1 BLOCKS.
    auto hb1 = make_peer_test_request("FIX.4.4", 2, "INITIATOR", "ACCEPTOR", "TR1");
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{hb1}), asio::detached);

    // Run a few steps to get write N+1 in-flight but blocked.
    ioc.run_for(50ms);
    ioc.restart();

    // Now feed a second TestRequest → triggers another Heartbeat reply → write N+2.
    // This write must NOT enter async_write while N+1 is still in-flight.
    auto hb2 = make_peer_test_request("FIX.4.4", 3, "INITIATOR", "ACCEPTOR", "TR2");
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{hb2}), asio::detached);

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
    release_timer.async_wait([&](asio::error_code /*ec*/) { raw_ptr->release(); });

    ioc.run_for(2s);
    ioc.restart();

    // Close the transport to unblock any pending reads.
    raw_ptr->close();
    ioc.run_for(200ms);
    ioc.restart();

    // After draining: both Heartbeat replies must have started a write.
    // If total_starts < writes_before_test + 2, the second reply was dropped or
    // never reached async_write — the "genuine second emit" contract is unproven.
    EXPECT_GE(raw_ptr->total_starts(), writes_before_test + 2)
        << "FQ-A Cell B: both Heartbeat replies must have reached async_write. "
        << "total_starts=" << raw_ptr->total_starts()
        << " writes_before_test=" << writes_before_test
        << " (expected >= writes_before_test+2=" << (writes_before_test + 2) << ")";

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
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}), asio::detached);
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

        asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{rr_bytes}),
                       asio::detached);

        // rr_bytes MUST outlive run_for(): on_inbound_frame() takes a non-owning
        // span and the detached coroutine reads it when the executor resumes it
        // below (not at co_spawn() time). If rr_bytes dies first this is a
        // heap-use-after-free — invisible to the debug build, caught by TSan/ASan.
        ioc.run_for(500ms);
        ioc.restart();
    }

    // The replay transmit (GapFill) fails with broken_pipe → force-disconnect.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "FQ-A Cell C: replay transmit error must force-disconnect the session. "
        << "state=" << static_cast<int>(sess.state());
}

TEST(LiveOutboundSerializedTest, LivenessHeartbeatWriteErrorStopsLoop) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    eng.clock = clock;

    auto cfg = make_acceptor_cfg(ioc.get_executor());
    cfg.heartbeat_interval = std::chrono::seconds{1};

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, open_fut, 2s))
        << "open() timed out";
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    auto raw_transport = std::make_unique<FailNthWriteTransport>(ioc.get_executor(), 2);
    auto* raw_ptr = raw_transport.get();
    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    auto logon_fut = asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}),
                                    asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, logon_fut, 2s))
        << "peer Logon timed out";
    ASSERT_TRUE(logon_fut.get().has_value()) << "peer Logon failed";
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active) << "must reach Active";

    ioc.run_for(1500ms);
    ioc.restart();

    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "liveness Heartbeat write failure must disconnect the session";
    const int writes_after_failure = raw_ptr->write_count();
    EXPECT_EQ(writes_after_failure, 2)
        << "liveness loop should stop after the failing Heartbeat write";

    ioc.run_for(1500ms);
    ioc.restart();

    EXPECT_EQ(raw_ptr->write_count(), writes_after_failure)
        << "liveness loop must stop retrying writes after a live write failure";

    raw_ptr->close();
}

TEST(LiveOutboundSerializedTest, CloseCancelsBlockedPublicSend) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto cfg = make_acceptor_cfg(ioc.get_executor());

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, open_fut, 2s))
        << "open() timed out";
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    auto raw_transport = std::make_unique<ControlledWriteTransport>(ioc.get_executor());
    auto* raw_ptr = raw_transport.get();
    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    auto logon_fut = asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}),
                                    asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, logon_fut, 2s))
        << "peer Logon timed out";
    ASSERT_TRUE(logon_fut.get().has_value()) << "peer Logon failed";
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active) << "must reach Active";

    raw_ptr->arm_block();

    auto payload = make_min_app_payload();
    auto send_fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>{payload}), asio::use_future);
    ioc.run_for(50ms);
    ioc.restart();

    auto close_fut =
        asio::co_spawn(ioc, sess.close(fixpp::session::close_mode::terminal), asio::use_future);
    ioc.run_for(500ms);
    ioc.restart();

    EXPECT_EQ(close_fut.wait_for(0ms), std::future_status::ready)
        << "close() must close the live transport before draining write_gate_; "
        << "a blocked public send must not deadlock terminal close";
    EXPECT_EQ(send_fut.wait_for(0ms), std::future_status::ready)
        << "blocked send() must resolve once close() tears down the live transport";

    raw_ptr->close();
    ioc.run_for(100ms);
    ioc.restart();

    ASSERT_EQ(close_fut.wait_for(0ms), std::future_status::ready);
    auto close_r = close_fut.get();
    ASSERT_TRUE(close_r.has_value()) << "close() failed unexpectedly";

    ASSERT_EQ(send_fut.wait_for(0ms), std::future_status::ready);
    auto send_r = send_fut.get();
    ASSERT_FALSE(send_r.has_value()) << "blocked send() must fail when close() aborts it";
    EXPECT_EQ(send_r.error(), fixpp::core::error::dispatch_aborted)
        << "blocked send() must surface dispatch_aborted after terminal close";
}

TEST(LiveOutboundSerializedTest, GracefulCloseCancelsBlockedPublicSend) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto cfg = make_acceptor_cfg(ioc.get_executor());
    cfg.logout_disconnect_timeout_ms = 100;

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, open_fut, 2s))
        << "open() timed out";
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    auto raw_transport = std::make_unique<ControlledWriteTransport>(ioc.get_executor());
    auto* raw_ptr = raw_transport.get();
    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    auto logon_fut = asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}),
                                    asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, logon_fut, 2s))
        << "peer Logon timed out";
    ASSERT_TRUE(logon_fut.get().has_value()) << "peer Logon failed";
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active) << "must reach Active";

    raw_ptr->arm_block();

    auto payload = make_min_app_payload();
    auto send_fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>{payload}), asio::use_future);
    ioc.run_for(50ms);
    ioc.restart();

    auto close_fut =
        asio::co_spawn(ioc, sess.close(fixpp::session::close_mode::graceful), asio::use_future);
    ioc.run_for(500ms);
    ioc.restart();

    const bool close_ready = close_fut.wait_for(0ms) == std::future_status::ready;
    if (!close_ready) {
        // Cleanup for the unfixed behavior: force-release the parked write so the
        // test fails fast instead of leaving the runner wedged.
        raw_ptr->close();
        EXPECT_TRUE(fixpp::test_support::pump_until_ready(ioc, close_fut, 1s))
            << "cleanup pump for close_fut did not complete";
    }

    EXPECT_TRUE(close_ready)
        << "close(graceful) must bound phase-1 Logout behind blocked live writes; "
        << "current HEAD hangs here until the transport is manually closed";

    EXPECT_TRUE(fixpp::test_support::pump_until_ready(ioc, send_fut, 1s))
        << "cleanup pump for send_fut did not complete";

    ASSERT_EQ(close_fut.wait_for(0ms), std::future_status::ready);
    auto close_r = close_fut.get();
    ASSERT_TRUE(close_r.has_value()) << "close() failed unexpectedly";

    ASSERT_EQ(send_fut.wait_for(0ms), std::future_status::ready);
    auto send_r = send_fut.get();
    ASSERT_FALSE(send_r.has_value()) << "blocked send() must fail when graceful close aborts it";
    EXPECT_EQ(send_r.error(), fixpp::core::error::dispatch_aborted)
        << "blocked send() must surface dispatch_aborted after graceful close";
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
TEST(LiveOutboundSerializedTest, CloseBeforeLivenessStartsDoesNotLeaveQueuedUaf) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    eng.clock = clock;

    auto cfg = make_acceptor_cfg(ioc.get_executor());
    cfg.heartbeat_interval = std::chrono::seconds{1};

    auto sess = std::make_unique<fixpp::session::Session>(eng, cfg);
    auto open_fut = asio::co_spawn(ioc, sess->open(), asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, open_fut, 2s))
        << "open() timed out";
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    auto raw_transport = std::make_unique<ControlledWriteTransport>(ioc.get_executor());
    fixpp::transport::handshake_result hr{};
    sess->attach_accepted_transport(std::move(raw_transport), std::move(hr));

    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    auto close_then_destroy = [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
        auto inbound_r = co_await sess->on_inbound_frame(std::span<const std::byte>{logon});
        if (!inbound_r) {
            co_return std::unexpected(inbound_r.error());
        }
        EXPECT_EQ(sess->state(), fixpp::session::fsm_state::Active)
            << "Session must reach Active before the close race witness";
        co_return co_await sess->close(fixpp::session::close_mode::terminal);
    };
    auto close_fut = asio::co_spawn(ioc, close_then_destroy(), asio::use_future);

    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, close_fut, 1s))
        << "close() timed out";
    ASSERT_EQ(close_fut.wait_for(0ms), std::future_status::ready)
        << "close() must complete in the same executor turn even when liveness "
        << "has not started yet";
    auto close_r = close_fut.get();
    ASSERT_TRUE(close_r.has_value()) << "close() failed unexpectedly";

    sess.reset();

    ioc.run_for(200ms);
    ioc.restart();

    SUCCEED() << "Queued liveness coroutine did not touch freed Session after close().";
}

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
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}), asio::detached);
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

    auto close_fut =
        asio::co_spawn(ioc, sess.close(fixpp::session::close_mode::terminal), asio::use_future);

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

// ─────────────────────────────────────────────────────────────────────────────
// Cell E (F2 Gate-B/r1) — close() entered then CALLER-cancelled mid-drain must STILL
// complete (publish close_result_), so a concurrent second close() cannot hang.
//
// The P1 race: a peer EOF races Engine::stop(); the role loop ENTERS close(graceful),
// which sets state_=closing and suspends in phase-1 (Logout exchange, bounded by the
// logout-disconnect timeout). Engine::stop() then total-cancels the role loop. Without
// the cancellation-immune shield at the top of close(), that phase-1 await aborts and
// close() unwinds BEFORE publishing close_result_ — and the stop() post-join drain's
// second close() takes the `closing` branch and awaits a result nobody sets → HANG.
//
// Deterministic (no EOF/stop timing): phase-1 of a graceful close suspends for the
// logout timeout (the peer never ACKs), giving a reliable window to fire caller
// cancellation. Then a second (un-cancelled) close(terminal) — the Engine::stop()
// drain analogue — must complete. RED (second close hangs → bounded ASSERT fails)
// before the shield; GREEN after. [Codex Gate-B/r2 deterministic-regression sketch]
// ─────────────────────────────────────────────────────────────────────────────
TEST(LiveOutboundSerializedTest, CallerCancelledMidCloseDoesNotWedgeSecondClose) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();

    auto cfg = make_acceptor_cfg(ioc.get_executor());
    cfg.logout_disconnect_timeout_ms = 500;  // phase-1 suspends ~500ms (no peer ACK)

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, open_fut, 2s))
        << "open() timed out";
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    auto raw_transport = std::make_unique<ControlledWriteTransport>(ioc.get_executor());
    auto* raw_ptr = raw_transport.get();
    fixpp::transport::handshake_result hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(hr));

    auto logon = make_peer_logon("FIX.4.4", 1, "INITIATOR", "ACCEPTOR");
    asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}), asio::detached);
    ioc.run_for(100ms);
    ioc.restart();
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active) << "must reach Active";

    // Block the NEXT write so the phase-1 Logout emit blocks in async_write — this
    // deterministically suspends close #1 inside phase-1's cancellable
    // `run_logout_phase1() || close_grace` await (the unblocked path completes too
    // fast to be a meaningful witness). close()'s FQ-G force-close unblocks it at the
    // logout timeout, so close #1 still completes once it is allowed to.
    raw_ptr->arm_block();

    // close #1 (graceful) from a CANCELLABLE caller — enters phase-1 and suspends.
    asio::cancellation_signal sig;
    auto close1 = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            co_return co_await sess.close(fixpp::session::close_mode::graceful);
        },
        asio::bind_cancellation_slot(sig.slot(), asio::use_future));
    ioc.run_for(50ms);  // let close #1 enter phase-1 and suspend (no peer Logout ACK)
    ioc.restart();
    sig.emit(asio::cancellation_type::total);  // cancel the caller mid-close

    // close #2 (terminal, un-cancelled) — the Engine::stop() post-join drain analogue.
    auto close2 = asio::co_spawn(ioc, sess.close(fixpp::session::close_mode::terminal),
                                 asio::use_future);

    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, close1, 3s))
        << "close #1 timed out";
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, close2, 3s))
        << "close #2 timed out";

    EXPECT_EQ(close1.wait_for(0ms), std::future_status::ready)
        << "the caller-cancelled close(graceful) must still complete";
    ASSERT_EQ(close2.wait_for(0ms), std::future_status::ready)
        << "second close() hung: close #1 was aborted mid-drain without publishing "
        << "close_result_, so the `closing`-branch wait never resolves. close() must "
        << "be cancellation-immune once entered.";
    (void)close1.get();
    auto r2 = close2.get();
    EXPECT_TRUE(r2.has_value() || r2.error() == fixpp::core::error::session_already_closed)
        << "second close() must observe a completed first close (ok or already-closed)";

    raw_ptr->close();
    ioc.run_for(100ms);
    ioc.restart();
}

}  // namespace fixpp::session::test
