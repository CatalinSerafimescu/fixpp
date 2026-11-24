// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_presession_log.cpp — 105-transport-liveness (tasks.md T003).
//
// Pre-session rejects and the listener records (tasks.md T102; contract C-7; research D8): an
// acceptor writes one record for each connection it refuses before a Session exists, and one
// for a listener that cannot bind.
//
// The rig: a plaintext acceptor on a mock-clock Engine (tests/session/plain_engine_rig.hpp) with
// a capturing logger as the entry's logger_override, and raw TCP peers; the two TLS reasons run
// on the Engine over TLS (tests/session/engine_loopback_harness.hpp). Each cell drives one or
// more connections to their refusal, waits for the record, stops the engine, then reads the
// records of the format it names. The expected format strings are spelled out here, apart from
// the production's, so a changed format fails the cell. The wall-clock timers: the TLS
// handshake timeout (the TLS cells' handshakes complete or fail before it); the first-frame
// deadline and the establishment deadline run on the mock clock.
//
// No ASSERT_* runs between an engine's start() and its stop() (tasks.md, Execution rules).
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session_config.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "session/engine_loopback_harness.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/test_105_support.hpp"
#include "support/pump_until_ready.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::session::SessionConfig;
namespace pr = fixpp::test_support::plain_rig;
namespace t105 = fixpp::test_support::t105;

constexpr char kRejectFormat[] =
    "acceptor connection refused before a session: reason={} begin_string={} sender_comp_id={} "
    "target_comp_id={} error={} suppressed_since_last={}";
constexpr char kBindFormat[] =
    "acceptor listener could not bind: begin_string={} sender_comp_id={} target_comp_id={} "
    "error={}";

// How long a cell waits for a record to reach the capturing sink before it reports the miss.
// The writers of these formats, which the wait would also observe: git grep -n "acceptor
// connection refused before\|acceptor listener could not bind" -- src.
constexpr auto kRecordWait = std::chrono::seconds{2};

std::string arg_text(fixpp::log::ArgValue const& a) {
    using K = fixpp::log::ArgValue::Kind;
    switch (a.kind) {
        case K::inline_str:
            return std::string{a.inl.data, a.inl.len};
        case K::static_str:
            return a.static_ptr ? std::string{a.static_ptr} : std::string{};
        case K::u64:
            return std::to_string(a.u64);
        case K::i64:
            return std::to_string(a.i64);
        default:
            return "<kind " + std::to_string(static_cast<int>(a.kind)) + ">";
    }
}

// The record as a line: each {} of the format replaced by its argument, in order.
std::string render(fixpp::log::Record const& r, std::string_view fmt) {
    std::string out;
    std::size_t arg = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
            out += arg < r.arg_count ? arg_text(r.args[arg]) : std::string{"<missing>"};
            ++arg;
            ++i;
        } else {
            out += fmt[i];
        }
    }
    if (arg != r.arg_count) out += " <arg_count " + std::to_string(r.arg_count) + ">";
    return out;
}

std::string error_value(error e) { return std::to_string(static_cast<std::uint64_t>(e)); }

std::size_t sink_count(t105::LogCapture const& log, char const* fmt) {
    std::size_t n = 0;
    for (auto const& r : log.sink->records()) {
        if (r.format_id == FIXPP_FORMAT_ID(fmt)) ++n;
    }
    return n;
}

// A raw TCP client of the acceptor: writes what a cell sends, and reads until its read ends.
// Every coroutine holds it by shared_ptr.
class RawPeer : public std::enable_shared_from_this<RawPeer> {
public:
    bool connected = false;
    bool connect_failed = false;
    bool read_ended = false;

    [[nodiscard]] static std::shared_ptr<RawPeer> connect(asio::io_context& ioc,
                                                          std::uint16_t port) {
        auto p = std::shared_ptr<RawPeer>(new RawPeer{ioc});
        asio::co_spawn(ioc, p->run(port), asio::detached);
        return p;
    }
    void send(std::string bytes) {
        asio::co_spawn(
            sock_.get_executor(),
            [self = shared_from_this(), b = std::move(bytes)]() -> asio::awaitable<void> {
                std::error_code ec;
                co_await asio::async_write(self->sock_, asio::buffer(b),
                                           asio::redirect_error(asio::use_awaitable, ec));
            },
            asio::detached);
    }
    // Closes with SO_LINGER 0, so the acceptor's read ends with a reset.
    void reset() {
        std::error_code ec;
        (void)sock_.set_option(asio::socket_base::linger{true, 0}, ec);
        (void)sock_.close(ec);
    }

private:
    explicit RawPeer(asio::io_context& ioc) : sock_{ioc} {}
    asio::awaitable<void> run(std::uint16_t port) {
        auto self = shared_from_this();
        std::error_code ec;
        co_await sock_.async_connect(
            asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            connect_failed = true;
            co_return;
        }
        connected = true;
        std::array<char, 1024> buf{};
        for (;;) {
            (void)co_await sock_.async_read_some(asio::buffer(buf),
                                                 asio::redirect_error(asio::use_awaitable, ec));
            if (ec) break;
        }
        read_ended = true;
    }
    asio::ip::tcp::socket sock_;
};

// The plaintext acceptor of these cells: plain_engine_rig.hpp's acceptor ("ISLD", peer "TW",
// HeartBtInt 30 s) with the capturing logger.
struct PlainCell {
    pr::Rig rig;
    t105::LogCapture log;
    bool up = false;

    explicit PlainCell(std::function<void(SessionConfig&)> const& tweak = nullptr) {
        auto c = rig.cfg();
        c.logger_override = log.logger;
        if (tweak) tweak(c);
        up = rig.start(std::move(c));
    }
    [[nodiscard]] std::uint16_t port() const {
        return rig.engine ? rig.engine->acceptor_bound_endpoint(rig.id).port : 0;
    }
    [[nodiscard]] std::shared_ptr<RawPeer> connect() {
        auto p = RawPeer::connect(rig.ioc, port());
        (void)rig.run_until([&] { return p->connected || p->connect_failed; });
        return p;
    }
    // A Logon from the sender to the target named.
    [[nodiscard]] std::string logon_from(std::string_view sender, std::string_view target) const {
        return pr::message("FIX.4.2", "A", 1, sender, target, rig.sending_time(),
                           "98=0\x01"
                           "108=30\x01");
    }
    // One connection refused after it sent the bytes given: connect, send, and wait for the
    // acceptor to close it.
    bool refuse_one(std::string bytes) {
        auto p = connect();
        if (!p->connected) return false;
        p->send(std::move(bytes));
        return rig.run_until([&] { return p->read_ended; });
    }
    bool await_records(char const* fmt, std::size_t n) {
        return rig.run_until([&] { return sink_count(log, fmt) >= n; }, kRecordWait);
    }
    std::vector<fixpp::log::Record> finish(char const* fmt) {
        rig.stop();
        return log.records_of(FIXPP_FORMAT_ID(fmt));
    }
};

void expect_one_reject(std::vector<fixpp::log::Record> const& recs, std::string const& line,
                       char const* site) {
    ASSERT_EQ(recs.size(), 1U) << site << ": the refusal record";
    EXPECT_EQ(recs.front().level, fixpp::log::Level::warn) << site;
    EXPECT_EQ(render(recs.front(), kRejectFormat), line) << site;
}

// The record line C-7.1 states, built from its parts.
std::string reject_line(std::string_view reason, std::string_view bs, std::string_view sender,
                        std::string_view target, std::string const& err,
                        std::uint64_t suppressed = 0) {
    return "acceptor connection refused before a session: reason=" + std::string{reason} +
           " begin_string=" + std::string{bs} + " sender_comp_id=" + std::string{sender} +
           " target_comp_id=" + std::string{target} + " error=" + err +
           " suppressed_since_last=" + std::to_string(suppressed);
}

// ── Plaintext reasons ────────────────────────────────────────────────────────────────────────

TEST(PreSessionLog105, FirstFrameTimeout_OneRecord) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    bool closed = false;
    if (c.up) {
        auto p = c.connect();
        EXPECT_TRUE(p->connected) << "setup: no connection";
        // The first-frame read's deadline (5 s, below the default establishment time) on the
        // mock clock; the peer sends nothing.
        for (int i = 0; i < 60 && !p->read_ended; ++i) {
            // Staging (ci/mock-clock-staging-sweep.sh): KIND D, the first-frame deadline is stored
            // at the accept, which predates these advances; each step is followed by a bounded
            // run_until, and closed is asserted after the loop's cap.
            c.rig.clock->advance(100ms);
            (void)c.rig.run_until([&] { return p->read_ended; }, 20ms);
        }
        closed = p->read_ended;
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    EXPECT_TRUE(closed) << "the silent connection was not closed";
    expect_one_reject(
        recs,
        reject_line("first_frame", "", "", "", error_value(error::transport_handshake_timeout)),
        "FirstFrameTimeout");
}

// Over budget: more than 4096 bytes read with no complete frame, a declared BodyLength within the
// inbound limit.
TEST(PreSessionLog105, FirstFrameOverBudget_OneRecord) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    bool closed = false;
    if (c.up) {
        closed = c.refuse_one(std::string{"8=FIX.4.2\x01"
                                          "9=6000\x01"
                                          "35=A\x01"} +
                              std::string(4200, 'x'));
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    EXPECT_TRUE(closed) << "the connection was not closed";
    expect_one_reject(
        recs, reject_line("first_frame", "", "", "", error_value(error::wire_frame_too_large)),
        "FirstFrameOverBudget");
}

// Over the inbound limit: a declared BodyLength above the session's limit L (its advertised
// MaxMessageSize, here the floor 4096), refused by the first-frame read's Framer before the
// budget is reached.
TEST(PreSessionLog105, FirstFrameOverLimit_OneRecord) {
    PlainCell c{[](SessionConfig& s) { s.advertised_max_message_size = 4096; }};
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    bool closed = false;
    if (c.up) {
        closed = c.refuse_one(
            "8=FIX.4.2\x01"
            "9=60000\x01"
            "35=A\x01");
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    EXPECT_TRUE(closed) << "the connection was not closed";
    expect_one_reject(
        recs, reject_line("first_frame", "", "", "", error_value(error::wire_frame_too_large)),
        "FirstFrameOverLimit");
}

// A read error: the peer resets the connection inside its first frame.
TEST(PreSessionLog105, FirstFrameReadError_OneRecord) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    if (c.up) {
        auto p = c.connect();
        EXPECT_TRUE(p->connected) << "setup: no connection";
        p->send(
            "8=FIX.4.2\x01"
            "9=60\x01"
            "35=A\x01");
        c.rig.settle();
        p->reset();
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    expect_one_reject(
        recs, reject_line("first_frame", "", "", "", error_value(error::transport_read_error)),
        "FirstFrameReadError");
}

TEST(PreSessionLog105, NoIds_OneRecord) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    bool closed = false;
    if (c.up) {
        // No SenderCompID(49).
        closed = c.refuse_one(pr::frame("FIX.4.2",
                                        "35=A\x01"
                                        "34=1\x01"
                                        "56=ISLD\x01"
                                        "52=" +
                                            c.rig.sending_time() +
                                            "\x01"
                                            "98=0\x01"
                                            "108=30\x01"));
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    EXPECT_TRUE(closed) << "the connection was not closed";
    expect_one_reject(recs, reject_line("no_ids", "FIX.4.2", "", "ISLD", "0"), "NoIds");
}

TEST(PreSessionLog105, NoSession_OneRecord) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    bool closed = false;
    if (c.up) {
        closed = c.refuse_one(c.logon_from("XX", "ISLD"));
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    EXPECT_TRUE(closed) << "the connection was not closed";
    expect_one_reject(recs, reject_line("no_session", "FIX.4.2", "XX", "ISLD", "0"), "NoSession");
}

TEST(PreSessionLog105, NoSession_RecordNamesCompIds) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    if (c.up) {
        EXPECT_TRUE(c.refuse_one(c.logon_from("OTHER", "ELSEWHERE")));
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    ASSERT_EQ(recs.size(), 1U);
    EXPECT_EQ(arg_text(recs.front().args[2]), "OTHER") << "sender_comp_id";
    EXPECT_EQ(arg_text(recs.front().args[3]), "ELSEWHERE") << "target_comp_id";
}

// C-7.2: at most one record per max(HeartBtInt, 1 s) of the engine clock (30 s here); the next
// carries the refusals not written since the last one. Both sides of the window are read.
TEST(PreSessionLog105, Flood_BoundedWithSuppressedCount) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    std::size_t in_window = 0;
    std::size_t before_edge = 0;
    if (c.up) {
        for (int i = 0; i < 5; ++i) EXPECT_TRUE(c.refuse_one(c.logon_from("XX", "ISLD")));
        (void)c.await_records(kRejectFormat, 1);
        c.rig.settle();
        in_window = sink_count(c.log, kRejectFormat);
        // Staging (ci/mock-clock-staging-sweep.sh): KIND A, the record's rate window is read from
        // the clock by the next refusal's record, with no waiter.
        c.rig.clock->advance(30s - 1ms);
        EXPECT_TRUE(c.refuse_one(c.logon_from("XX", "ISLD")));
        c.rig.settle();
        before_edge = sink_count(c.log, kRejectFormat);
        // Staging (ci/mock-clock-staging-sweep.sh): KIND A, the record's rate window is read from
        // the clock by the next refusal's record, with no waiter.
        c.rig.clock->advance(1ms);
        EXPECT_TRUE(c.refuse_one(c.logon_from("XX", "ISLD")));
        (void)c.await_records(kRejectFormat, 2);
    }
    auto const recs = c.finish(kRejectFormat);
    EXPECT_EQ(in_window, 1U) << "records written for five refusals inside one window";
    EXPECT_EQ(before_edge, 1U) << "a record 1 ms before the window ends";
    ASSERT_EQ(recs.size(), 2U);
    EXPECT_EQ(render(recs[0], kRejectFormat),
              reject_line("no_session", "FIX.4.2", "XX", "ISLD", "0", 0));
    EXPECT_EQ(render(recs[1], kRejectFormat),
              reject_line("no_session", "FIX.4.2", "XX", "ISLD", "0", 5));
}

// L-105-d: each CompID is cut at 15 bytes.
TEST(PreSessionLog105, LongCompIds_CutAt15Bytes) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    if (c.up) {
        EXPECT_TRUE(c.refuse_one(c.logon_from("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ISLD0123456789XYZ")));
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    expect_one_reject(
        recs, reject_line("no_session", "FIX.4.2", "ABCDEFGHIJKLMNO", "ISLD0123456789X", "0"),
        "LongCompIds");
}

// C-7.1: a byte of the CompIDs outside printable ASCII is written as '?', so an unauthenticated
// peer cannot put a line break or another control byte into the record.
TEST(PreSessionLog105, ControlBytesInCompIds_WrittenAsQuestionMarks) {
    PlainCell c;
    EXPECT_TRUE(c.up) << "setup: the acceptor did not start";
    if (c.up) {
        EXPECT_TRUE(c.refuse_one(c.logon_from("T\nW\x07Z", "IS\rLD")));
        (void)c.await_records(kRejectFormat, 1);
    }
    auto const recs = c.finish(kRejectFormat);
    expect_one_reject(recs, reject_line("no_session", "FIX.4.2", "T?W?Z", "IS?LD", "0"),
                      "ControlBytes");
    if (!recs.empty()) {
        EXPECT_EQ(render(recs.front(), kRejectFormat).find_first_of("\r\n\x07"), std::string::npos)
            << "a control byte in the record";
    }
}

// C-7.3: a listener that cannot bind (its port is in use) writes one error record with the
// system error's value.
TEST(PreSessionLog105, ListenerBindFails_OneRecord) {
    pr::Rig rig;
    t105::LogCapture log;
    asio::ip::tcp::acceptor holder{rig.ioc,
                                   asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0}};
    auto const port = holder.local_endpoint().port();
    auto c = rig.cfg();
    c.logger_override = log.logger;
    c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
    fixpp::core::EngineConfig ec;
    ec.executor = rig.ioc.get_executor();
    ec.clock = rig.clock;
    rig.engine = std::make_unique<fixpp::session::Engine>(rig.ioc.get_executor(), std::move(ec));
    rig.id = fixpp::session::SessionId::from_config(c);
    bool const registered = rig.engine->register_session(std::move(c)).has_value();
    bool const started = registered && rig.engine->start().has_value();
    EXPECT_TRUE(started) << "setup: the engine did not start";
    if (started) {
        (void)rig.run_until([&] { return sink_count(log, kBindFormat) >= 1; }, kRecordWait);
    }
    rig.stop();
    auto const recs = log.records_of(FIXPP_FORMAT_ID(kBindFormat));
    ASSERT_EQ(recs.size(), 1U) << "the listener record";
    EXPECT_EQ(recs.front().level, fixpp::log::Level::error);
    EXPECT_EQ(render(recs.front(), kBindFormat),
              "acceptor listener could not bind: begin_string=FIX.4.2 sender_comp_id=ISLD "
              "target_comp_id=TW error=" +
                  std::to_string(std::error_code{asio::error::address_in_use}.value()));
}

// ── TLS reasons ──────────────────────────────────────────────────────────────────────────────

// The Engine over TLS, acceptor ACCEPTOR for peer INITIATOR, with the capturing logger and the
// mock clock as the engine clock.
struct TlsCell {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock = t105::make_mock_clock(ioc.get_executor());
    std::unique_ptr<fixpp::test_support::EngineLoopbackHarness> h;
    t105::LogCapture log;

    [[nodiscard]] bool build(std::function<void(SessionConfig&)> const& tweak = nullptr) {
        fixpp::core::EngineConfig ec;
        ec.executor = ioc.get_executor();
        ec.clock = clock;
        h = fixpp::test_support::EngineLoopbackHarness::build(
            ioc.get_executor(), std::move(ec), true, [&](SessionConfig& s) {
                fixpp::session::CompIdAuthorizationPolicy authz;
                authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");
                s.compid_authorization_policy = authz;
                s.logger_override = log.logger;
                if (tweak) tweak(s);
            });
        return h != nullptr;
    }
    [[nodiscard]] bool bound() {
        return fixpp::test_support::pump_until(
            ioc, [&] { return h->server_endpoint().port != 0; }, 5s, 5ms,
            "PreSessionLog105::TlsCell/bind");
    }
    bool await_records(std::size_t n) {
        return fixpp::test_support::pump_until(
            ioc, [&] { return sink_count(log, kRejectFormat) >= n; }, kRecordWait, 5ms, nullptr);
    }
};

TEST(PreSessionLog105, TlsHandshake_OneRecord) {
    TlsCell c;
    if (!c.build()) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    bool closed = false;
    {
        bool const started = c.h->engine().start().has_value();
        fixpp::test_support::engine_stop_guard guard{*c.h, c.ioc};
        EXPECT_TRUE(started && c.bound()) << "setup: the acceptor did not bind";
        if (started) {
            auto p = RawPeer::connect(c.ioc, c.h->server_endpoint().port);
            (void)fixpp::test_support::pump_until(
                c.ioc, [&] { return p->connected || p->connect_failed; }, 5s, 5ms, nullptr);
            // Not a TLS ClientHello: the handshake fails at its first record.
            p->send("GET / HTTP/1.0\r\n\r\n");
            closed = fixpp::test_support::pump_until(
                c.ioc, [&] { return p->read_ended; }, 5s, 5ms, nullptr);
            (void)c.await_records(1);
        }
    }
    auto const recs = c.log.records_of(FIXPP_FORMAT_ID(kRejectFormat));
    EXPECT_TRUE(closed) << "the connection was not closed";
    expect_one_reject(
        recs,
        reject_line("tls_handshake", "", "", "", error_value(error::transport_handshake_failed)),
        "TlsHandshake");
}

// An mTLS client that connects first and handshakes when the cell says so, so the establishment
// time can run out on the mock clock between the accept and the handshake.
class DelayedTlsPeer : public std::enable_shared_from_this<DelayedTlsPeer> {
public:
    bool connected = false;
    bool failed = false;
    bool done = false;  // the handshake failed, or it succeeded and the read then ended

    [[nodiscard]] static std::shared_ptr<DelayedTlsPeer> connect(asio::io_context& ioc,
                                                                 std::string const& dir,
                                                                 std::uint16_t port) {
        auto p = std::shared_ptr<DelayedTlsPeer>(new DelayedTlsPeer{ioc, dir});
        asio::co_spawn(ioc, p->run_connect(port), asio::detached);
        return p;
    }
    void handshake() { asio::co_spawn(*ioc_, run_handshake(), asio::detached); }

private:
    DelayedTlsPeer(asio::io_context& ioc, std::string const& dir)
        : ioc_{&ioc}, ctx_{make_context(dir)}, stream_{ioc, ctx_} {}
    static asio::ssl::context make_context(std::string const& dir) {
        asio::ssl::context c{asio::ssl::context::tls_client};
        c.load_verify_file(dir + "/ca.pem");
        c.use_certificate_chain_file(dir + "/leaf_rsa2048.pem");
        c.use_private_key_file(dir + "/leaf_rsa2048.key", asio::ssl::context::pem);
        c.set_verify_mode(asio::ssl::verify_none);
        return c;
    }
    asio::awaitable<void> run_connect(std::uint16_t port) {
        auto self = shared_from_this();
        std::error_code ec;
        co_await stream_.lowest_layer().async_connect(
            asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            failed = done = true;
            co_return;
        }
        connected = true;
    }
    asio::awaitable<void> run_handshake() {
        auto self = shared_from_this();
        std::error_code ec;
        co_await stream_.async_handshake(asio::ssl::stream_base::client,
                                         asio::redirect_error(asio::use_awaitable, ec));
        if (!ec) {
            std::array<char, 256> buf{};
            for (;;) {
                (void)co_await stream_.async_read_some(
                    asio::buffer(buf), asio::redirect_error(asio::use_awaitable, ec));
                if (ec) break;
            }
        }
        done = true;
    }
    asio::io_context* ioc_;
    asio::ssl::context ctx_;
    asio::ssl::stream<asio::ip::tcp::socket> stream_;
};

TEST(PreSessionLog105, NoTimeLeft_OneRecord) {
    TlsCell c;
    if (!c.build([](SessionConfig& s) { s.logon_timeout_ms = 1000; })) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    bool ended = false;
    {
        bool const started = c.h->engine().start().has_value();
        fixpp::test_support::engine_stop_guard guard{*c.h, c.ioc};
        EXPECT_TRUE(started && c.bound()) << "setup: the acceptor did not bind";
        if (started) {
            auto p =
                DelayedTlsPeer::connect(c.ioc, FIXPP_TLS_FIXTURE_DIR, c.h->server_endpoint().port);
            (void)fixpp::test_support::pump_until(
                c.ioc, [&] { return p->connected || p->failed; }, 5s, 5ms, nullptr);
            EXPECT_TRUE(p->connected) << "setup: no connection";
            // The acceptor has accepted and waits for the ClientHello; the establishment time
            // runs out on the mock clock before the handshake.
            c.ioc.run_for(50ms);
            c.ioc.restart();
            c.clock->advance(1001ms);
            p->handshake();
            ended =
                fixpp::test_support::pump_until(c.ioc, [&] { return p->done; }, 5s, 5ms, nullptr);
            (void)c.await_records(1);
        }
    }
    auto const recs = c.log.records_of(FIXPP_FORMAT_ID(kRejectFormat));
    EXPECT_TRUE(ended) << "the connection was not ended";
    expect_one_reject(recs, reject_line("no_time_left", "", "", "", "0"), "NoTimeLeft");
}

}  // namespace
