// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_support.hpp — test-only helpers shared by 105-transport-liveness's own
// test files (test_105_*.cpp), in the shape of the 101, 103 and 104 feature headers.
//
// Part 1 (tasks.md T004): the peer-side observers of how the connection ended, on the three rigs
// research D10 names; the frozen-clock pump; the bounded drive; the capturing log sink; the wire
// reader; the both-roles instantiation. Part 2 (tasks.md T005): the counting Clock decorator, the
// held write, the deferred-completion transport, the store that holds or fails a call, and the TLS
// peer that stops reading.
//
// CONDITIONS this header keeps, checked by reading:
//   - it names no production symbol 105 adds, so it compiles on the base (tasks.md, Execution
//     rules, "Cells that cannot compile on the base"); a cell that reads one lives in
//     test_105_seams.cpp;
//   - no helper reports with ASSERT_*: a miss is an ADD_FAILURE plus a false or empty return,
//     so a helper called inside an Engine window never returns early past the engine's stop;
//   - no pump here is the run_for(W); restart(); get() shape (ci/pump-census.sh's population):
//     waits go through tests/support/pump_until_ready.hpp's readiness-checked helpers, or the
//     bounded drive below.
#pragma once

#include <gtest/gtest.h>

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/socket_base.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/error.hpp>
#include <asio/ssl/stream.hpp>
#include <asio/ssl/verify_mode.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/test/mock_transport.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "support/log_capture.hpp"
#include "support/pump_until_ready.hpp"
#include "transport/loopback_tls_fixture.hpp"

namespace fixpp::test_support::t105 {

// ════════════════════════════════════════════════════════════════════════════════════════
// Part 1 (tasks.md T004)
// ════════════════════════════════════════════════════════════════════════════════════════

// How the peer's read of the connection ended (research D10, "The close observable is the
// peer's"):
//   open         — the read has not ended;
//   eof          — plaintext: the read ended with asio::error::eof (the session's FIN);
//   close_notify — TLS: the read ended with transport_read_eof, which the TLS transport returns
//                  only when close_notify was received
//                  (tests/transport/test_close_truncated_mapping.cpp pins it apart from
//                  transport_read_truncated);
//   abortive     — any other ending (a reset, a truncated TLS stream). A cell pins
//                  transport_read_truncated itself, through the peer's recorded error, only where
//                  a reading has shown it: behind a peer that stopped reading the read can also
//                  end in a reset.
enum class peer_ending : std::uint8_t { open, eof, close_notify, abortive };

[[nodiscard]] inline const char* peer_ending_name(peer_ending e) noexcept {
    switch (e) {
        case peer_ending::open:
            return "open";
        case peer_ending::eof:
            return "eof";
        case peer_ending::close_notify:
            return "close_notify";
        case peer_ending::abortive:
            return "abortive";
    }
    return "?";
}

// The plaintext rig's peer (tests/session/plain_engine_rig.hpp): its Peer records the ending.
[[nodiscard]] inline peer_ending ending_of(fixpp::test_support::plain_rig::Peer const& peer) {
    if (!peer.read_ended) return peer_ending::open;
    return peer.read_ec == asio::error::eof ? peer_ending::eof : peer_ending::abortive;
}

// The plaintext peer's ending as one line of text: the ending's name and, once the read has
// ended, the read's error as value, category and message; then how many bytes the peer read and
// the last of them, with SOH shown as '|'.
[[nodiscard]] inline std::string describe_ending(fixpp::test_support::plain_rig::Peer const& peer) {
    std::string out = peer_ending_name(ending_of(peer));
    if (peer.read_ended) {
        out += " read_ec=" + std::to_string(peer.read_ec.value()) + " [" +
               peer.read_ec.category().name() + "] " + peer.read_ec.message();
    }
    constexpr std::size_t kTail = 160;
    std::string const& got = peer.received;
    out += "; " + std::to_string(got.size()) + " bytes read, ending with: ";
    for (std::size_t i = got.size() > kTail ? got.size() - kTail : 0; i < got.size(); ++i) {
        out += got[i] == '\x01' ? '|' : got[i];
    }
    return out;
}

// The EOF reading of a site that expects the plaintext peer's read to end in EOF: true when it
// did. When the read ended otherwise it prints the site and describe_ending() on stdout,
// so the cell's failure has the ending beside it. The line is not an assertion: the verdict is
// the caller's, on the value returned.
[[nodiscard]] inline bool ended_in_eof(fixpp::test_support::plain_rig::Peer const& peer,
                                       const char* site) {
    peer_ending const e = ending_of(peer);
    if (e == peer_ending::abortive) {
        std::cout << "[ ending   ] " << (site != nullptr ? site : "?")
                  << ": the peer's read ended, not in EOF: " << describe_ending(peer) << std::endl;
    }
    return e == peer_ending::eof;
}

// The ending of a read through a fixpp TLS Transport: transport_read_eof is close_notify.
[[nodiscard]] inline peer_ending ending_of_tls_read(bool ended,
                                                    std::optional<fixpp::core::error> err) {
    if (!ended) return peer_ending::open;
    return err == fixpp::core::error::transport_read_eof ? peer_ending::close_notify
                                                         : peer_ending::abortive;
}

// A TLS peer over a fixpp Transport, for the two TLS rigs:
//   - connect(): an mTLS client of the endpoint given, minted by LoopbackTlsFixture::make_client
//     and driven as engine_firstframe_test.cpp's probe_post_handshake drives it (async_connect,
//     then async_handshake with the fixture's SslCtxConfig). The Engine-over-TLS rig is
//     tests/session/engine_loopback_harness.hpp, whose acceptor's bound endpoint is
//     EngineLoopbackHarness::server_endpoint(); its cells run in the acceptor role (research D10).
//   - accept(): the server end of the fixture's own listener, for the direct-Session TLS rig
//     tests/session/loopback_tls_session_harness.hpp, whose initiator Session connects to
//     LoopbackTlsSessionHarness::server_endpoint().
// Once set up it reads until its read ends, recording every byte and the read's error, and
// writes what a cell sends, one write in flight at a time (the Transport contract). Every
// coroutine holds the peer by shared_ptr, so a completion never outlives it.
class TlsPeer : public std::enable_shared_from_this<TlsPeer> {
public:
    std::string received;                          // every byte read, in order
    bool ready = false;                            // the connection and the handshake succeeded
    bool setup_failed = false;                     // the connection or the handshake failed
    bool read_ended = false;                       // the read loop ended
    std::optional<fixpp::core::error> read_error;  // why it ended; set once read_ended
    std::size_t writes_issued = 0;
    std::size_t writes_done = 0;

    [[nodiscard]] static std::shared_ptr<TlsPeer> connect(
        asio::io_context& ioc, fixpp::transport::test::LoopbackTlsFixture& fixture,
        fixpp::transport::Endpoint endpoint) {
        auto p = std::shared_ptr<TlsPeer>(new TlsPeer{ioc, fixture});
        asio::co_spawn(ioc, p->run_connect(std::move(endpoint)), asio::detached);
        return p;
    }

    [[nodiscard]] static std::shared_ptr<TlsPeer> accept(
        asio::io_context& ioc, fixpp::transport::test::LoopbackTlsFixture& fixture) {
        auto p = std::shared_ptr<TlsPeer>(new TlsPeer{ioc, fixture});
        asio::co_spawn(ioc, p->run_accept(), asio::detached);
        return p;
    }

    [[nodiscard]] peer_ending ending() const { return ending_of_tls_read(read_ended, read_error); }

    // Queues the bytes; they are written once the handshake has succeeded.
    void send(std::string bytes) {
        ++writes_issued;
        pending_.push_back(std::move(bytes));
        start_writer();
    }
    [[nodiscard]] bool all_written() const noexcept { return writes_done == writes_issued; }

    // The peer's own close: the synchronous Transport::close(), with no close_notify.
    void close() {
        if (transport_) (void)transport_->close();
    }

private:
    TlsPeer(asio::io_context& ioc, fixpp::transport::test::LoopbackTlsFixture& fixture)
        : ioc_{&ioc}, fixture_{&fixture} {}

    asio::awaitable<void> run_connect(fixpp::transport::Endpoint endpoint) {
        auto self = shared_from_this();
        try {
            transport_ = fixture_->make_client(ioc_->get_executor());
        } catch (...) {
            setup_failed = true;
            co_return;
        }
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(transport_.get());
        if (tls == nullptr || !(co_await transport_->async_connect(endpoint)).has_value() ||
            !(co_await tls->async_handshake(fixture_->ssl_cfg())).has_value()) {
            setup_failed = true;
            co_return;
        }
        co_await on_ready();
    }

    asio::awaitable<void> run_accept() {
        auto self = shared_from_this();
        auto accepted = co_await fixture_->listener().async_accept();
        if (!accepted.has_value()) {
            setup_failed = true;
            co_return;
        }
        transport_ = std::move(*accepted);
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(transport_.get());
        if (tls == nullptr || !(co_await tls->async_handshake(fixture_->ssl_cfg())).has_value()) {
            setup_failed = true;
            co_return;
        }
        co_await on_ready();
    }

    asio::awaitable<void> on_ready() {
        ready = true;
        start_writer();
        std::array<std::byte, 4096> buf{};
        for (;;) {
            auto r = co_await transport_->async_read_some(std::span<std::byte>{buf});
            if (!r.has_value()) {
                read_error = r.error();
                read_ended = true;
                co_return;
            }
            for (std::size_t i = 0; i < *r; ++i) received.push_back(static_cast<char>(buf[i]));
        }
    }

    void start_writer() {
        if (!ready || writing_ || pending_.empty()) return;
        writing_ = true;
        asio::co_spawn(*ioc_, run_writer(), asio::detached);
    }

    asio::awaitable<void> run_writer() {
        auto self = shared_from_this();
        while (!pending_.empty()) {
            std::string const b = std::move(pending_.front());
            pending_.pop_front();
            (void)co_await transport_->async_write(
                std::span<const std::byte>{reinterpret_cast<const std::byte*>(b.data()), b.size()});
            ++writes_done;
        }
        writing_ = false;
    }

    asio::io_context* ioc_;
    fixpp::transport::test::LoopbackTlsFixture* fixture_;
    std::shared_ptr<fixpp::transport::Transport> transport_;
    std::deque<std::string> pending_;
    bool writing_ = false;
};

// The mock clock a TLS Engine cell passes in EngineConfig::clock, seeded as plain_engine_rig.hpp
// seeds its own. EngineLoopbackHarness::build installs system_clock_source only when
// EngineConfig::clock is null, so a cell that passes this one keeps every session-clock instant
// on the mock; the acceptor's SessionConfig is edited through build()'s configure_acceptor.
[[nodiscard]] inline std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(
    asio::any_io_executor exec) {
    using namespace std::chrono;
    return std::make_shared<fixpp::core::mock_clock>(
        system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
        std::move(exec));
}

// The "at once" pump (tasks.md, Execution rules): with the mock clock held still (this helper
// never advances it), drive the io_context until ended() holds or wall_cap of wall time passes.
// The cap is an assertion: a miss is an ADD_FAILURE naming the site, and the return is false.
[[nodiscard]] inline bool pump_until_peer_read_ends(asio::io_context& ioc,
                                                    std::function<bool()> ended,
                                                    std::chrono::milliseconds wall_cap,
                                                    const char* site) {
    bool const done =
        fixpp::test_support::pump_until(ioc, std::move(ended), wall_cap, kPumpSlice, site);
    if (!done) {
        ADD_FAILURE() << site << ": the peer's read did not end within the wall cap of "
                      << wall_cap.count() << " ms, with the mock clock held still";
    }
    return done;
}

// The bounded drive (research D10): runs at most max_handlers handlers of the io_context, each
// waited for at most per_handler of wall time, and stops at the first wait in which none ran. It
// never runs until idle, so a mutant whose sleeps all complete at once (a passed deadline on the
// mock clock) still returns, and its cell fails by an assertion rather than hanging. Returns the
// number of handlers run.
inline std::size_t drive_bounded(asio::io_context& ioc, std::size_t max_handlers,
                                 std::chrono::milliseconds per_handler = std::chrono::milliseconds{
                                     1}) {
    std::size_t ran = 0;
    if (ioc.stopped()) ioc.restart();
    while (ran < max_handlers) {
        if (ioc.run_one_for(per_handler) == 0) break;
        ++ran;
    }
    if (ioc.stopped()) ioc.restart();
    return ran;
}

// One mock-clock step: advance the clock by step, then the bounded drive.
inline std::size_t advance_and_drive(fixpp::core::mock_clock& clock, asio::io_context& ioc,
                                     std::chrono::nanoseconds step, std::size_t max_handlers) {
    clock.advance(step);
    return drive_bounded(ioc, max_handlers);
}

// The capturing log sink: tests/support/log_capture.hpp's LogCapture, a Logger over one
// in-memory Sink. Its logger goes in SessionConfig::logger_override for a session's records,
// or in EngineConfig::logger for the engine's own (the pre-session records of research D8).
using LogCapture = fixpp::test_support::LogCapture;

// The liveness guard of data-model E-4: the give-up instant is the second threshold step plus one
// nanosecond.
inline constexpr std::chrono::nanoseconds kLivenessGuard{1};

// The wire reader's mock-clock cap for a cell that reaches a liveness instant with
// test_request_threshold = HeartBtInt (tasks.md, Execution rules, "Liveness instants"): above
// 2.4 × HeartBtInt plus the guard by one step, so every instant up to the default give-up is
// reached on the base, at the US1 head and at the final head alike.
[[nodiscard]] constexpr std::chrono::nanoseconds liveness_wire_cap(
    std::chrono::nanoseconds heartbt_int, std::chrono::nanoseconds step) noexcept {
    return heartbt_int * 12 / 5 + kLivenessGuard + step;
}

// What the wire reader found: the frame, and the mock-clock time from the reader's start of the
// step at which it was first read.
struct WireRead {
    std::string frame;
    std::chrono::nanoseconds at{0};
};

// The wire reader: advances the clock by step until wire() holds more than seen_before complete
// frames of MsgType msg_type, running the bounded drive (at most handlers_per_step handlers)
// before the first test and after every step. It never advances past cap from its start;
// reaching the cap is an assertion (an ADD_FAILURE naming the site), and the return is empty.
[[nodiscard]] inline std::optional<WireRead> advance_until_msg_type(
    fixpp::core::mock_clock& clock, asio::io_context& ioc, std::function<std::string()> wire,
    std::string_view msg_type, std::size_t seen_before, std::chrono::nanoseconds step,
    std::chrono::nanoseconds cap, const char* site, std::size_t handlers_per_step = 64) {
    std::chrono::nanoseconds advanced{0};
    for (;;) {
        (void)drive_bounded(ioc, handlers_per_step);
        auto const frames = fixpp::test_support::plain_rig::frames_of_type(wire(), msg_type);
        if (frames.size() > seen_before) {
            return WireRead{.frame = frames[seen_before], .at = advanced};
        }
        if (advanced + step > cap) {
            ADD_FAILURE() << site << ": no MsgType(35)=" << msg_type << " frame past the "
                          << seen_before << " already read, within the mock-clock cap of "
                          << std::chrono::duration_cast<std::chrono::milliseconds>(cap).count()
                          << " ms";
            return std::nullopt;
        }
        clock.advance(step);
        advanced += step;
    }
}

// The test name of a both-roles cell, as 537's cells name theirs.
[[nodiscard]] inline std::string role_param_name(
    ::testing::TestParamInfo<fixpp::session::session_role> const& i) {
    return i.param == fixpp::session::session_role::initiator ? "initiator" : "acceptor";
}

// INSTANTIATE_TEST_SUITE_P over session_role, as 537's cells use it (research D10, "Both
// roles"): FIXPP_105_BOTH_ROLES(Suite) after Suite's TEST_P cells.
#define FIXPP_105_BOTH_ROLES(Suite)                                                     \
    INSTANTIATE_TEST_SUITE_P(BothRoles, Suite,                                          \
                             ::testing::Values(fixpp::session::session_role::initiator, \
                                               fixpp::session::session_role::acceptor), \
                             ::fixpp::test_support::t105::role_param_name)

// ════════════════════════════════════════════════════════════════════════════════════════
// Part 2 (tasks.md T005)
// ════════════════════════════════════════════════════════════════════════════════════════

// The counting Clock decorator over mock_clock, for SessionConfig::clock_override (research D10,
// the no-spin cell and the bound's absorbed exception). It counts every sleep_until call, by
// deadline as well, and forwards each to the mock. On request it throws std::bad_alloc from
// sleep_until for one named instant only, once: the throw happens when the returned awaitable is
// awaited, so detail::await_deadline's catch-all sees it, and every sleep at another instant (a
// liveness sleep) is forwarded untouched.
class CountingClock final : public fixpp::core::Clock {
public:
    explicit CountingClock(std::shared_ptr<fixpp::core::mock_clock> inner)
        : inner_{std::move(inner)} {}

    [[nodiscard]] fixpp::core::utc_time_point now() const noexcept override {
        return inner_->now();
    }
    [[nodiscard]] fixpp::core::steady_time_point steady_now() const noexcept override {
        return inner_->steady_now();
    }
    [[nodiscard]] asio::awaitable<void> sleep_until(
        fixpp::core::steady_time_point deadline) override {
        bool fail = false;
        {
            std::scoped_lock lk{mu_};
            ++calls_;
            ++calls_at_[deadline];
            if (throw_at_ && *throw_at_ == deadline) {
                throw_at_.reset();
                fail = true;
            }
        }
        if (fail) return throw_bad_alloc(true);
        return inner_->sleep_until(deadline);
    }
    void cancel_sleeps() noexcept override { inner_->cancel_sleeps(); }
    void forget_session(fixpp::session::Session* s) noexcept override { inner_->forget_session(s); }

    // The next sleep_until(deadline) call throws std::bad_alloc when awaited; later calls at that
    // deadline are forwarded.
    void throw_once_at(fixpp::core::steady_time_point deadline) {
        std::scoped_lock lk{mu_};
        throw_at_ = deadline;
    }
    [[nodiscard]] bool throw_pending() const {
        std::scoped_lock lk{mu_};
        return throw_at_.has_value();
    }
    [[nodiscard]] std::size_t sleep_calls() const {
        std::scoped_lock lk{mu_};
        return calls_;
    }
    [[nodiscard]] std::size_t sleep_calls_at(fixpp::core::steady_time_point deadline) const {
        std::scoped_lock lk{mu_};
        auto const it = calls_at_.find(deadline);
        return it == calls_at_.end() ? 0 : it->second;
    }
    [[nodiscard]] fixpp::core::mock_clock& mock() const noexcept { return *inner_; }

private:
    static asio::awaitable<void> throw_bad_alloc(bool fail) {
        if (fail) throw std::bad_alloc{};
        co_return;
    }

    std::shared_ptr<fixpp::core::mock_clock> inner_;
    mutable std::mutex mu_;
    std::size_t calls_ = 0;
    std::map<fixpp::core::steady_time_point, std::size_t> calls_at_;
    std::optional<fixpp::core::steady_time_point> throw_at_;
};

// The held write: the scripted transport's write_hold
// (include/fixpp/transport/test/mock_transport.hpp, Script::write_hold), set as
// test_103_resend_state_reconnect.cpp sets it, through MockReconnectFactory::tweak_script
// (tests/session/mock_reconnect_factory.hpp). While the hold is true and the transport is open, its
// async_write waits; a close() during the wait ends the write with transport_write_cancelled and
// nothing written.
struct HeldWrite {
    std::shared_ptr<std::atomic<bool>> hold = std::make_shared<std::atomic<bool>>(false);

    // A tweak_script that gives the hold to the which-th transport made (0 = the first) only.
    [[nodiscard]] std::function<void(fixpp::transport::test::Script&)> tweak(int which = 0) const {
        auto const made = std::make_shared<int>(0);
        return [h = hold, made, which](fixpp::transport::test::Script& sc) {
            if ((*made)++ == which) sc.write_hold = h;
        };
    }
    void arm() const { hold->store(true); }
    void release() const { hold->store(false); }
};

// The deferred-completion transport's state, shared with the test (data-model E-3,
// ending_wait_.cut; research D10, "The write completing after the bound has fired"). Its
// async_write commits the bytes to committed (the peer's view) at once, then, while
// hold_completion is set, holds its success completion until release_completion(); once released
// it returns success, even when the transport was closed or the writer cancelled meanwhile (the
// hold runs in a coroutine of its own with no cancellation slot, and the write awaits nothing
// after it). That is the one difference from mock_transport's write_hold, whose held write
// returns transport_write_cancelled at close. A write whose caller's cancellation was emitted
// before it began meets it at the hold's await, as asio meets it in any coroutine, and throws
// operation_aborted with its bytes already committed.
struct deferred_completion_state {
    std::string committed;           // every byte written, in write order: the peer's view
    bool hold_completion = false;    // the next write holds its completion
    bool completion_parked = false;  // a write's completion is held now
    std::size_t writes_committed = 0;
    std::size_t completions_returned = 0;
    bool closed = false;  // close() was called
    std::size_t close_calls = 0;
    std::optional<asio::steady_timer> parked_timer;  // the held completion's wait
    std::optional<asio::steady_timer> read_timer;    // the read's wait, until the close

    void release_completion() {
        hold_completion = false;
        if (parked_timer) parked_timer->cancel();
    }
};

class DeferredCompletionTransport final : public fixpp::transport::Transport {
public:
    DeferredCompletionTransport(asio::any_io_executor exec,
                                std::shared_ptr<deferred_completion_state> state)
        : state_{std::move(state)} {
        state_->parked_timer.emplace(exec, asio::steady_timer::time_point::max());
        state_->read_timer.emplace(std::move(exec), asio::steady_timer::time_point::max());
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& /*ep*/) override {
        co_return fixpp::transport::ConnectInfo{
            .remote = fixpp::transport::Endpoint{"127.0.0.1", 0},
            .local = fixpp::transport::Endpoint{"127.0.0.1", 0},
            .family = 2 /* AF_INET */};
    }

    // Waits until the transport is closed or cancelled; a direct Session reads nothing from it.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> /*buf*/) override {
        if (state_->closed) co_return std::unexpected(fixpp::core::error::transport_already_closed);
        // NOLINTBEGIN(bugprone-unchecked-optional-access): the constructor emplaces the timer
        state_->read_timer->expires_at(asio::steady_timer::time_point::max());
        std::error_code ec;
        co_await state_->read_timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
        // NOLINTEND(bugprone-unchecked-optional-access)
        if (state_->closed) co_return std::unexpected(fixpp::core::error::transport_already_closed);
        co_return std::unexpected(fixpp::core::error::transport_read_cancelled);
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> bytes) override {
        if (state_->closed) co_return std::unexpected(fixpp::core::error::transport_already_closed);
        for (std::byte const b : bytes) state_->committed.push_back(static_cast<char>(b));
        ++state_->writes_committed;
        if (state_->hold_completion) {
            co_await asio::co_spawn(
                co_await asio::this_coro::executor, hold_completion(state_),
                asio::bind_cancellation_slot(asio::cancellation_slot{}, asio::use_awaitable));
        }
        ++state_->completions_returned;
        co_return bytes.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override {
        if (state_->read_timer) state_->read_timer->cancel();
        return {};
    }

    // Closes; a held completion stays held (it returns success once released).
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        state_->closed = true;
        ++state_->close_calls;
        if (state_->read_timer) state_->read_timer->cancel();
        return {};
    }

private:
    // The held completion, in a coroutine of its own spawned with no cancellation slot, so
    // neither the writer's cancellation nor a close ends it: only release_completion() does.
    static asio::awaitable<void> hold_completion(std::shared_ptr<deferred_completion_state> s) {
        struct parked_flag {
            deferred_completion_state& st;
            explicit parked_flag(deferred_completion_state& state) : st{state} {
                st.completion_parked = true;
            }
            parked_flag(parked_flag const&) = delete;
            parked_flag& operator=(parked_flag const&) = delete;
            ~parked_flag() { st.completion_parked = false; }
        };
        parked_flag const flag{*s};
        // NOLINTBEGIN(bugprone-unchecked-optional-access): the constructor emplaces the timer
        s->parked_timer->expires_at(asio::steady_timer::time_point::max());
        while (s->hold_completion) {
            std::error_code ec;
            co_await s->parked_timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
        }
        // NOLINTEND(bugprone-unchecked-optional-access)
    }

    std::shared_ptr<deferred_completion_state> state_;
};

// The factory a direct Session's SessionConfig::transport_factory_override names; every transport
// it makes shares the state.
class DeferredCompletionTransportFactory final : public fixpp::transport::TransportFactory {
public:
    explicit DeferredCompletionTransportFactory(std::shared_ptr<deferred_completion_state> state)
        : state_{std::move(state)} {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++made;
        return std::make_unique<DeferredCompletionTransport>(std::move(exec), state_);
    }
    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }
    [[nodiscard]] fixpp::transport::transport_security_kind kind() const noexcept override {
        return fixpp::transport::transport_security_kind::plaintext;
    }

    std::size_t made = 0;

private:
    std::shared_ptr<deferred_completion_state> state_;
};

// The store call a GateStore can hold or fail.
enum class store_call : std::uint8_t {
    store_outbound,
    store_inbound,
    increment_outbound,  // next_seqnum(outbound, true)
    increment_inbound,   // next_seqnum(inbound, true)
    read_outbound,       // next_seqnum(outbound, false)
    read_inbound,        // next_seqnum(inbound, false)
    reset,
    reset_to,
    retrieve,
};

// A GateStore's controls and readings, shared with the test. hold_when and fail_when are asked
// at every call with the call's kind and its sequence number (store's seq, retrieve's begin, 0
// elsewhere). A held call waits until release_all(), then proceeds, or fails when fail_held is
// set; a failed call returns store_io_failure without reaching the inner store. A held call is
// not cut by its caller's cancellation (a store call is not cut, FR-008): the hold runs in a
// coroutine of its own with no cancellation slot. A cancellation emitted before the call, or
// during the hold, is met at the call's next await, as asio meets it in any coroutine: the call
// then ends with operation_aborted, a store call cancelled before it held its lock.
struct gate_store_state {
    std::function<bool(store_call, fixpp::session::seqnum_t)> hold_when;
    std::function<bool(store_call, fixpp::session::seqnum_t)> fail_when;
    bool fail_held = false;
    // 105 (tasks.md T046, T047): a store() call reaches the inner store first and is held after
    // it, so a later call's number does not find an earlier one missing in the inner MemoryStore.
    bool hold_after_inner = false;
    std::size_t calls_held = 0;     // calls that began a hold
    std::size_t parked = 0;         // calls holding now
    std::size_t calls_failed = 0;   // calls answered with store_io_failure
    std::vector<store_call> calls;  // every call, in issue order
    std::shared_ptr<fixpp::session::MemoryStore> inner =
        std::make_shared<fixpp::session::MemoryStore>(fixpp::session::MemoryStore::Config{
            .policy = fixpp::session::capacity_policy::unbounded});
    std::vector<asio::steady_timer*> waiting;  // the held calls' waits, each while it lives
    std::size_t releases = 0;                  // release_all() calls

    // Releases every call holding now; a call that begins a hold later waits for the next.
    void release_all() {
        ++releases;
        for (auto* t : waiting) t->cancel();
    }
};

// A MessageStore over a MemoryStore whose calls can be held and released, or failed on demand
// (tasks.md T005). Its factory reports it persistent, so a session hydrates from it and persists
// into it.
class GateStore final : public fixpp::session::MessageStore {
public:
    explicit GateStore(std::shared_ptr<gate_store_state> state)
        : fixpp::session::MessageStore(nullptr), state_{std::move(state)} {}

    asio::awaitable<fixpp::core::expected_t<void>> store(
        fixpp::session::seqnum_t seq, std::span<const std::byte> frame,
        fixpp::session::direction_t dir) noexcept override {
        auto const k = dir == fixpp::session::direction_t::outbound ? store_call::store_outbound
                                                                    : store_call::store_inbound;
        if (state_->hold_after_inner) {
            auto r = co_await state_->inner->store(seq, frame, dir);
            if (auto f = co_await gate(k, seq)) co_return std::unexpected(*f);
            co_return r;
        }
        if (auto f = co_await gate(k, seq)) co_return std::unexpected(*f);
        co_return co_await state_->inner->store(seq, frame, dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        fixpp::session::seqnum_t begin, fixpp::session::seqnum_t end,
        fixpp::session::direction_t dir,
        fixpp::session::retrieve_visitor& visitor) noexcept override {
        if (auto f = co_await gate(store_call::retrieve, begin)) co_return std::unexpected(*f);
        co_return co_await state_->inner->retrieve(begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> next_seqnum(
        fixpp::session::direction_t dir, bool increment) noexcept override {
        bool const out = dir == fixpp::session::direction_t::outbound;
        auto const k = increment
                           ? (out ? store_call::increment_outbound : store_call::increment_inbound)
                           : (out ? store_call::read_outbound : store_call::read_inbound);
        if (auto f = co_await gate(k, 0)) co_return std::unexpected(*f);
        co_return co_await state_->inner->next_seqnum(dir, increment);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        if (auto f = co_await gate(store_call::reset, 0)) co_return std::unexpected(*f);
        co_return co_await state_->inner->reset();
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_to(
        fixpp::session::seqnum_t next_in, fixpp::session::seqnum_t next_out) noexcept override {
        if (auto f = co_await gate(store_call::reset_to, 0)) co_return std::unexpected(*f);
        co_return co_await state_->inner->reset_to(next_in, next_out);
    }

private:
    // Empty when the call proceeds; the error to return when it fails.
    asio::awaitable<std::optional<fixpp::core::error>> gate(store_call k,
                                                            fixpp::session::seqnum_t seq) {
        state_->calls.push_back(k);
        if (state_->hold_when && state_->hold_when(k, seq)) {
            ++state_->calls_held;
            co_await asio::co_spawn(
                co_await asio::this_coro::executor, hold_until_released(state_),
                asio::bind_cancellation_slot(asio::cancellation_slot{}, asio::use_awaitable));
            if (state_->fail_held) {
                ++state_->calls_failed;
                co_return fixpp::core::error::store_io_failure;
            }
            co_return std::nullopt;
        }
        if (state_->fail_when && state_->fail_when(k, seq)) {
            ++state_->calls_failed;
            co_return fixpp::core::error::store_io_failure;
        }
        co_return std::nullopt;
    }

    // The hold itself, in a coroutine of its own spawned with no cancellation slot, so the
    // caller's cancellation does not reach its wait: only release_all() ends it. Its wait is
    // registered in state.waiting for exactly its lifetime, whatever ends the frame (a release,
    // or the frame's destruction with its io_context), so release_all() never meets a stale one.
    static asio::awaitable<void> hold_until_released(std::shared_ptr<gate_store_state> s) {
        asio::steady_timer t{co_await asio::this_coro::executor,
                             asio::steady_timer::time_point::max()};
        struct registration {
            gate_store_state& st;
            asio::steady_timer* timer;
            registration(gate_store_state& state, asio::steady_timer* w) : st{state}, timer{w} {
                st.waiting.push_back(timer);
                ++st.parked;
            }
            registration(registration const&) = delete;
            registration& operator=(registration const&) = delete;
            ~registration() {
                std::erase(st.waiting, timer);
                --st.parked;
            }
        };
        registration const reg{*s, &t};
        auto const releases = s->releases;
        while (s->releases == releases) {
            std::error_code ec;
            co_await t.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        }
    }

    std::shared_ptr<gate_store_state> state_;
};

// The factory for SessionConfig::store_factory (or EngineConfig::default_store_factory); every
// store it makes shares the state, so a second connection keeps the counters.
class GateStoreFactory final : public fixpp::session::MessageStoreFactory {
public:
    explicit GateStoreFactory(std::shared_ptr<gate_store_state> state) : state_{std::move(state)} {}

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        ++made;
        return fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>>{
            std::make_unique<GateStore>(state_)};
    }

    std::size_t made = 0;

private:
    std::shared_ptr<gate_store_state> state_;
};

// A TLS peer that stops reading (a zero window), for the write limit's TLS arm (research D10,
// "The write limit"; contract C-8.5): a raw asio::ssl::stream mTLS client of the endpoint given,
// built from the fixture directory's certificates as
// tests/transport/test_close_truncated_mapping.cpp builds its client, whose socket's receive buffer
// is set to receive_buffer bytes before it connects. After the handshake it reads nothing until
// start_reading(), so the session's writes fill the window. How much the session must write before
// a write blocks also depends on the session side's send buffer, which this peer does not set; a
// cell shows the block (a write that has not completed) before it relies on it. Its read is classed
// as the fixpp TLS transport's is: asio::error::eof is close_notify, any other ending abortive.
class StalledTlsPeer : public std::enable_shared_from_this<StalledTlsPeer> {
public:
    std::string received;
    bool ready = false;
    bool setup_failed = false;
    bool read_ended = false;
    std::error_code read_ec;

    [[nodiscard]] static std::shared_ptr<StalledTlsPeer> connect(
        asio::io_context& ioc, std::string const& fixture_dir, fixpp::transport::Endpoint endpoint,
        int receive_buffer = 1024) {
        auto p = std::shared_ptr<StalledTlsPeer>(new StalledTlsPeer{ioc, fixture_dir});
        asio::co_spawn(ioc, p->run_connect(std::move(endpoint), receive_buffer), asio::detached);
        return p;
    }

    [[nodiscard]] peer_ending ending() const {
        if (!read_ended) return peer_ending::open;
        return read_ec == asio::error::eof ? peer_ending::close_notify : peer_ending::abortive;
    }

    // Starts the read; from then on it reads until its read ends.
    void start_reading() {
        if (!ready || reading_) return;
        reading_ = true;
        asio::co_spawn(*ioc_, run_read(), asio::detached);
    }

    void close() {
        std::error_code ec;
        (void)stream_.lowest_layer().close(ec);
    }

    // The peer socket's receive and send buffer sizes as the socket reports them, -1 for a size
    // that could not be read.
    [[nodiscard]] std::pair<int, int> buffer_sizes() {
        std::error_code ec;
        asio::socket_base::receive_buffer_size rcv;
        asio::socket_base::send_buffer_size snd;
        (void)stream_.lowest_layer().get_option(rcv, ec);
        int const r = ec ? -1 : rcv.value();
        (void)stream_.lowest_layer().get_option(snd, ec);
        return {r, ec ? -1 : snd.value()};
    }

    // 105 (tasks.md T054): writes what a cell sends, one write in flight at a time, while the
    // peer reads nothing, so the peer can log on and drive the session with its window closed.
    void send(std::string bytes) {
        pending_.push_back(std::move(bytes));
        if (!ready || writing_) return;
        writing_ = true;
        asio::co_spawn(*ioc_, run_writer(), asio::detached);
    }
    std::size_t writes_done = 0;

private:
    asio::awaitable<void> run_writer() {
        auto self = shared_from_this();
        while (!pending_.empty()) {
            std::string const b = std::move(pending_.front());
            pending_.pop_front();
            std::error_code ec;
            co_await asio::async_write(stream_, asio::buffer(b),
                                       asio::redirect_error(asio::use_awaitable, ec));
            ++writes_done;
        }
        writing_ = false;
    }

    std::deque<std::string> pending_;
    bool writing_ = false;

    StalledTlsPeer(asio::io_context& ioc, std::string const& fixture_dir)
        : ioc_{&ioc}, ctx_{make_context(fixture_dir)}, stream_{ioc, ctx_} {}

    static asio::ssl::context make_context(std::string const& dir) {
        asio::ssl::context c{asio::ssl::context::tls_client};
        c.load_verify_file(dir + "/ca.pem");
        c.use_certificate_chain_file(dir + "/leaf_rsa2048.pem");
        c.use_private_key_file(dir + "/leaf_rsa2048.key", asio::ssl::context::pem);
        // The fixture certificate names no IP; the server still verifies this client's
        // certificate against the CA.
        c.set_verify_mode(asio::ssl::verify_none);
        return c;
    }

    asio::awaitable<void> run_connect(fixpp::transport::Endpoint endpoint, int receive_buffer) {
        auto self = shared_from_this();
        std::error_code ec;
        auto& sock = stream_.lowest_layer();
        (void)sock.open(asio::ip::tcp::v4(), ec);
        if (!ec) (void)sock.set_option(asio::socket_base::receive_buffer_size{receive_buffer}, ec);
        if (!ec) {
            co_await sock.async_connect(
                asio::ip::tcp::endpoint{asio::ip::make_address(endpoint.host), endpoint.port},
                asio::redirect_error(asio::use_awaitable, ec));
        }
        if (!ec) {
            co_await stream_.async_handshake(asio::ssl::stream_base::client,
                                             asio::redirect_error(asio::use_awaitable, ec));
        }
        if (ec) {
            setup_failed = true;
            co_return;
        }
        ready = true;
    }

    asio::awaitable<void> run_read() {
        auto self = shared_from_this();
        std::array<char, 4096> buf{};
        for (;;) {
            std::error_code ec;
            std::size_t const n = co_await stream_.async_read_some(
                asio::buffer(buf), asio::redirect_error(asio::use_awaitable, ec));
            if (ec) {
                read_ec = ec;
                read_ended = true;
                co_return;
            }
            received.append(buf.data(), n);
        }
    }

    asio::io_context* ioc_;
    asio::ssl::context ctx_;
    asio::ssl::stream<asio::ip::tcp::socket> stream_;
    bool reading_ = false;
};

}  // namespace fixpp::test_support::t105
