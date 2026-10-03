// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/inbound_frame_dispositions_test.cpp
//
// 093-inbound-frame-dispositions — session-level cells (quickstart.md §1). Later
// tasks add their cells here.
//
// Q-13 (L half; data-model E-2): the session's one inbound limit L follows an
// advertised MaxMessageSize(383) inside [4096, 262144] when set, and is 65536 when
// unset. Read after open() through session_test_access (fixpp#511). The refusal of a
// value outside that range is a separate cell.
//
// Q-13 (timeout half; data-model E-7): logon_timeout_ms == 0 is refused by
// Engine::register_session and by Session::open().
//
// Q-1 (T026; contract C-1, C-2 step 1; FR-001, FR-003, FR-004): the FIX-TC 2020 rows
// 2d, 2m, 2t, 3b, 3c and 3e, each written to an Active session through the real read
// pump between two good frames. The garbled bytes are disregarded and the session
// carries on. The session TC corpus (tests/session/conformance/) cannot drive these
// rows: it holds QuickFIX-oracle scenarios fed past the pump.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/framer.hpp>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "plain_engine_rig.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

namespace fixpp::session::test {
namespace {

class InboundFrameDispositions : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_acceptor_cfg(std::optional<std::uint32_t> advertised_max) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = session_role::acceptor;
        cfg.advertised_max_message_size = advertised_max;
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "InboundFrameDispositions::open_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "InboundFrameDispositions::open_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "InboundFrameDispositions::open_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }
};

// ── Q-13, the L half ─────────────────────────────────────────────────────────

TEST_F(InboundFrameDispositions, Q13_InboundLimitIs65536WhenNoMaxMessageSizeIsAdvertised) {
    Session sess(engine, make_acceptor_cfg(std::nullopt));
    ASSERT_TRUE(open_sync(sess).has_value());
    EXPECT_EQ(session_test_access::inbound_limit(sess), 65536U);
}

TEST_F(InboundFrameDispositions, Q13_InboundLimitFollowsAnAdvertisedMaxMessageSizeInRange) {
    // The range's two ends and a value between them that is not the unset default.
    for (std::uint32_t const advertised : {4096U, 100000U, 262144U}) {
        SCOPED_TRACE(advertised);
        Session sess(engine, make_acceptor_cfg(advertised));
        ASSERT_TRUE(open_sync(sess).has_value());
        EXPECT_EQ(session_test_access::inbound_limit(sess), advertised);
    }
}

// ── Q-13, the timeout half (T042; data-model E-7) ───────────────────────────
//
// logon_timeout_ms == 0 is refused with invalid_session_config by
// Engine::register_session and by Session::open(); 1, the smallest legal value, is
// accepted by both, so the refusal is keyed on zero and not on the field.

TEST_F(InboundFrameDispositions, Q13_LogonTimeoutZeroIsRefusedByRegisterSessionAndOneIsAccepted) {
    fixpp::core::EngineConfig ec;
    ec.executor = ioc.get_executor();
    ec.clock = clock;
    fixpp::session::Engine eng{ioc.get_executor(), std::move(ec)};

    auto zero = make_acceptor_cfg(std::nullopt);
    zero.logon_timeout_ms = 0;
    auto const refused = eng.register_session(zero);
    auto one = make_acceptor_cfg(std::nullopt);
    one.logon_timeout_ms = 1;
    auto const accepted = eng.register_session(one);

    auto stop_fut = asio::co_spawn(ioc, eng.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "InboundFrameDispositions::Q13_LogonTimeoutZero register")) {
        return;
    }
    stop_fut.get();

    ASSERT_FALSE(refused.has_value()) << "register_session accepted logon_timeout_ms == 0";
    EXPECT_EQ(refused.error(), fixpp::core::error::invalid_session_config);
    EXPECT_TRUE(accepted.has_value()) << "register_session refused logon_timeout_ms == 1";
}

TEST_F(InboundFrameDispositions, Q13_LogonTimeoutZeroIsRefusedByOpenAndOneIsAccepted) {
    auto zero = make_acceptor_cfg(std::nullopt);
    zero.logon_timeout_ms = 0;
    Session refused(engine, zero);
    auto const r = open_sync(refused);
    ASSERT_FALSE(r.has_value()) << "open() accepted logon_timeout_ms == 0";
    EXPECT_EQ(r.error(), fixpp::core::error::invalid_session_config);
    EXPECT_FALSE(refused.is_open()) << "a refused open() left the session open";
    auto close_fut = asio::co_spawn(ioc, refused.close(close_mode::terminal), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(
            ioc, close_fut, std::chrono::milliseconds{200},
            "InboundFrameDispositions::Q13_LogonTimeoutZero close")) {
        fixpp::test_support::cancel_and_drain_or_report(
            ioc, *clock, "InboundFrameDispositions::Q13_LogonTimeoutZero close");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "InboundFrameDispositions::Q13_LogonTimeoutZero close";
        return;
    }
    auto const c = close_fut.get();
    ASSERT_FALSE(c.has_value());
    EXPECT_EQ(c.error(), fixpp::core::error::session_already_closed)
        << "a refused open() is a never-opened session";

    auto one = make_acceptor_cfg(std::nullopt);
    one.logon_timeout_ms = 1;
    Session accepted(engine, one);
    EXPECT_TRUE(open_sync(accepted).has_value()) << "open() refused logon_timeout_ms == 1";
}

// ── Garble observation, shared by the pump cells below ──────────────────────
//
// The format of the record a garble writes (data-model E-12). Spelled out here rather
// than taken from the session, so a change to the record fails these cells.
constexpr char kGarbleRecordFormat[] =
    "inbound garbled frame disregarded: kind={} discarded_bytes={} suppressed_since_last={}";

namespace plain_rig = fixpp::test_support::plain_rig;

class CaptureSink final : public fixpp::log::Sink {
public:
    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const& rec) noexcept override {
        std::scoped_lock lk{mu_};
        records_.push_back(rec);
    }
    void flush(std::chrono::milliseconds /*deadline*/) noexcept override {}
    void close() noexcept override {}
    [[nodiscard]] std::vector<fixpp::log::Record> records() const {
        std::scoped_lock lk{mu_};
        return records_;
    }

private:
    mutable std::mutex mu_;
    std::vector<fixpp::log::Record> records_;
};

// A Logger over one CaptureSink, for SessionConfig::logger_override.
struct LogCapture {
    CaptureSink* sink = nullptr;  // owned by `logger`
    std::shared_ptr<fixpp::log::Logger> logger;

    LogCapture() {
        auto owned = std::make_unique<CaptureSink>();
        sink = owned.get();
        std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks(std::pmr::get_default_resource());
        sinks.push_back(std::move(owned));
        logger = std::make_shared<fixpp::log::Logger>(fixpp::log::LoggerConfig{}, std::move(sinks));
    }

    // The garble records the session wrote. The logger drains on its own thread, so
    // this shuts it down first: every record enqueued before the call is then counted.
    [[nodiscard]] std::vector<fixpp::log::Record> garble_records() {
        (void)logger->shutdown();
        std::vector<fixpp::log::Record> out;
        for (auto const& r : sink->records()) {
            if (r.format_id == FIXPP_FORMAT_ID(kGarbleRecordFormat)) out.push_back(r);
        }
        return out;
    }
};

// A trace context no session would carry by default, so a record that carries it was
// written with the session's own.
fixpp::otel::trace_context known_trace() {
    fixpp::otel::trace_context tc{};
    tc.trace_id.fill(std::byte{0x5A});
    tc.span_id.fill(std::byte{0xC3});
    return tc;
}

// The session-side record of garbles: its counter and its garble events.
struct GarbleObservation {
    std::uint64_t count = 0;
    std::vector<session_event_garbled_frame> events;
};

std::uint64_t garbled_count(Session const& s) { return s.garbled_frame_count(); }

GarbleObservation observe_garbles(Session const& s) {
    GarbleObservation o;
    o.count = garbled_count(s);
    for (auto const& ev : s.recent_events()) {
        if (auto const* g = std::get_if<session_event_garbled_frame>(&ev)) o.events.push_back(*g);
    }
    return o;
}

struct WantGarble {
    fixpp::core::error kind;
    std::size_t bytes;
};

// One garbled region of `want.kind` discarding `want.bytes`: counted once, evented
// once, logged once with the session's trace context and nothing suppressed.
void expect_one_garble(GarbleObservation const& o, std::vector<fixpp::log::Record> const& records,
                       WantGarble const& want, std::string_view row) {
    EXPECT_EQ(o.count, 1U) << row << ": garbled_frame_count()";
    ASSERT_EQ(o.events.size(), 1U) << row << ": session_event_garbled_frame events";
    EXPECT_EQ(o.events[0].first_kind, want.kind) << row << ": event first_kind";
    EXPECT_EQ(o.events[0].frames, 1U) << row << ": event frames";
    EXPECT_EQ(o.events[0].discarded_bytes, want.bytes) << row << ": event discarded_bytes";
    ASSERT_EQ(records.size(), 1U) << row << ": garble log records";
    auto const& r = records[0];
    EXPECT_EQ(r.level, fixpp::log::Level::warn) << row << ": record level";
    EXPECT_EQ(r.category, fixpp::log::cat::session) << row << ": record category";
    ASSERT_EQ(r.arg_count, 3U) << row << ": record args";
    EXPECT_EQ(r.args[0].u64, static_cast<std::uint64_t>(want.kind)) << row << ": record kind";
    EXPECT_EQ(r.args[1].u64, want.bytes) << row << ": record discarded bytes";
    EXPECT_EQ(r.args[2].u64, 0U) << row << ": record suppressed count";
    auto const tc = known_trace();
    EXPECT_EQ(r.trace_id, (reinterpret_cast<std::array<std::uint8_t, 16> const&>(tc.trace_id)))
        << row << ": the record carries the session's trace_id";
}

// What a resync-mode Framer makes of `bytes`, fed whole then drained: the regions it
// opens, the first one's kind, the bytes it discards and the frames it yields. A
// cell checks its garbled bytes trip the Framer arm it names before driving the pump.
struct FramerView {
    std::uint32_t regions = 0;
    fixpp::core::error first_kind{};
    std::size_t discarded = 0;
    std::size_t frames = 0;
};

FramerView resync_framer_view(std::string const& bytes) {
    fixpp::wire::Framer::Config c;
    c.resync_on_garble = true;
    fixpp::wire::Framer framer{c};
    fixpp::wire::pmr_carry_buffer carry{bytes.size() + 1, std::pmr::new_delete_resource()};
    std::array<fixpp::wire::frame_view, 1> out{};
    FramerView v;
    auto const raw = plain_rig::to_bytes(bytes);
    std::span<const std::byte> in{raw};
    for (;;) {
        auto r = framer.feed(in, carry, std::span<fixpp::wire::frame_view>{out});
        in = {};
        auto const g = framer.last_garbles();
        if (v.regions == 0U && g.regions != 0U) v.first_kind = g.first_kind;
        v.regions += g.regions;
        v.discarded += g.discarded;
        if (!r.has_value() || r->empty()) break;
        v.frames += r->size();
    }
    return v;
}

// ── Q-1 (T026): FIX-TC 2020 garbled rows in Active ──────────────────────────
//
// Each cell: the peer logs on, then writes Heartbeat(34=2) ‖ the row's garbled bytes ‖
// Heartbeat(next) in one write. The garbled bytes are disregarded and the session
// carries on (FR-001, FR-004). Where they carry MsgSeqNum 3 (2m, 2t, 3b, 3e), the next
// good frame is 34=4 and draws a ResendRequest whose BeginSeqNo(7) is 3, and NextNumIn
// stays 3. Where they carry none (2d, 3c), the next good frame is 34=3 and is processed.
// The session's counter reads 1, one session_event_garbled_frame carries the row's kind
// and bytes, and one log record carries the same (FR-003, E-4, E-5, E-12).
//
// The observations are taken before Engine::stop(), and none of them is fatal until
// stop() has returned: a fatal assertion between start() and a completed stop() aborts
// in ~Engine instead of failing the cell.
struct Q1Row {
    const char* name;
    // The bytes between the two good frames, stamped on the rig's clock.
    std::function<std::string(plain_rig::Rig const&)> garbled;
    bool numbered;  // the garbled bytes are MsgSeqNum 3
    fixpp::core::error kind;
    bool framer_detects;  // false: the session's criterion 3 (C-2 step 1), not the Framer
};

std::uint32_t next_inbound(Session& s) {
    return session_test_access::seqnum_mgr(s).next_inbound_unsafe();
}

void run_q1(Q1Row const& row) {
    LogCapture log;
    plain_rig::Rig rig;
    auto cfg = rig.cfg();
    cfg.logger_override = log.logger;
    cfg.initial_trace_context = known_trace();
    bool const up = rig.start(std::move(cfg)) && rig.to_active();

    std::uint32_t const next_seq = row.numbered ? 4U : 3U;
    std::uint32_t const want_next_in = row.numbered ? 3U : 4U;
    std::string garbled;
    std::string tail;
    bool delivered = false;
    if (up) {
        garbled = row.garbled(rig);
        tail = rig.heartbeat(next_seq);
        delivered = rig.deliver(rig.heartbeat(2) + garbled + tail);
        (void)rig.run_until([&] {
            auto const s = rig.session();
            return s && next_inbound(*s) == want_next_in &&
                   (!row.numbered || !plain_rig::frames_of_type(rig.peer.received, "2").empty());
        });
        rig.settle();
    }

    // Snapshots, before stop().
    std::optional<fsm_state> state;
    std::optional<std::uint32_t> next_in;
    GarbleObservation garbles;
    if (auto const s = rig.session()) {
        state = s->state();
        next_in = next_inbound(*s);
        garbles = observe_garbles(*s);
    }
    bool const peer_read_ended = rig.peer.read_ended;
    auto const resends = plain_rig::frames_of_type(rig.peer.received, "2");
    rig.stop();
    auto const records = log.garble_records();

    ASSERT_TRUE(up) << row.name << ": the session did not reach Active";
    ASSERT_TRUE(delivered) << row.name << ": the peer's write did not complete";
    if (row.framer_detects) {
        auto const fv = resync_framer_view(garbled + tail);
        EXPECT_EQ(fv.regions, 1U) << row.name << ": the garbled bytes must be one Framer region";
        EXPECT_EQ(fv.first_kind, row.kind) << row.name << ": the Framer arm this row names";
        EXPECT_EQ(fv.discarded, garbled.size()) << row.name << ": the Framer discards the row";
        EXPECT_EQ(fv.frames, 1U) << row.name << ": the good frame after the row is framed";
    }
    EXPECT_EQ(state, std::optional<fsm_state>{fsm_state::Active})
        << row.name << ": a garbled frame must not end the session";
    EXPECT_FALSE(peer_read_ended) << row.name << ": the connection must stay open";
    EXPECT_EQ(next_in, std::optional<std::uint32_t>{want_next_in})
        << row.name << ": NextNumIn reflects only the good frames";
    if (row.numbered) {
        ASSERT_EQ(resends.size(), 1U) << row.name << ": the lost number draws one ResendRequest";
        EXPECT_EQ(plain_rig::field_value(resends[0], "7"), "3")
            << row.name << ": BeginSeqNo(7) is the lost number";
    } else {
        EXPECT_TRUE(resends.empty()) << row.name << ": no number is lost, so no ResendRequest";
    }
    expect_one_garble(garbles, records, {row.kind, garbled.size()}, row.name);
}

// A Heartbeat at 34=3 as text, for a row to corrupt.
std::string hb3(plain_rig::Rig const& rig) { return rig.heartbeat(3); }

// `s` with its trailing `10=NNN<SOH>` rewritten by `f`, which gets the three digits.
template <class F>
std::string with_trailer(std::string s, F f) {
    auto const at = s.rfind("10=");
    std::string const digits = s.substr(at + 3, 3);
    s.erase(at);
    return s + f(digits);
}

TEST(InboundFrameDispositionsTc, TC002_2d_LeadingJunkBeforeAGoodFrame_DisregardedAndContinues) {
    run_q1({"TC002_2d",
            [](plain_rig::Rig const&) { return std::string{"GARBLED-LEADING-BYTES\x01"}; }, false,
            fixpp::core::error::wire_framing_resync, true});
}

TEST(InboundFrameDispositionsTc, TC002_2m_BodyLengthWrong_DisregardedAndContinues) {
    run_q1({"TC002_2m",
            [](plain_rig::Rig const& rig) {
                // 9=<n> rewritten as 9=<n - 5>: `10=` is then not at the counted offset.
                std::string s = hb3(rig);
                auto const at = s.find(
                                    "\x01"
                                    "9=") +
                                3;
                auto const end = s.find('\x01', at);
                s.replace(at, end - at, std::to_string(std::stoi(s.substr(at, end - at)) - 5));
                return s;
            },
            true, fixpp::core::error::wire_invalid_body_length, true});
}

TEST(InboundFrameDispositionsTc, TC002_2t_MsgTypeNotThird_DisregardedAndContinues) {
    run_q1({"TC002_2t",
            [](plain_rig::Rig const& rig) {
                return plain_rig::frame("FIX.4.2",
                                        "49=TW\x01"
                                        "35=0\x01"
                                        "34=3\x01"
                                        "52=" +
                                            rig.sending_time() +
                                            "\x01"
                                            "56=ISLD\x01");
            },
            true, fixpp::core::error::wire_header_out_of_order, false});
}

TEST(InboundFrameDispositionsTc, TC003_3b_CheckSumWrong_DisregardedAndContinues) {
    run_q1({"TC003_3b",
            [](plain_rig::Rig const& rig) {
                return with_trailer(hb3(rig), [](std::string const& d) {
                    std::array<char, 4> wrong{};
                    std::snprintf(wrong.data(), wrong.size(), "%03d", (std::stoi(d) + 1) % 256);
                    return "10=" + std::string{wrong.data(), 3} + "\x01";
                });
            },
            true, fixpp::core::error::wire_checksum_mismatch, true});
}

TEST(InboundFrameDispositionsTc,
     TC003_3c_GarbledBeginStringBeforeAGoodFrame_DisregardedAndContinues) {
    // A BeginString value longer than the cap, with no SOH (contract C-1 W-2).
    run_q1({"TC003_3c",
            [](plain_rig::Rig const&) { return std::string{"8=FIXGARBLEDBEGINSTRING"}; }, false,
            fixpp::core::error::wire_framing_resync, true});
}

TEST(InboundFrameDispositionsTc, TC003_3e_CheckSumNotLast_DisregardedAndContinues) {
    // A field follows CheckSum and BodyLength counts it, so `10=` is not where the count
    // says: reported as a BodyLength failure (contract L-2).
    run_q1({"TC003_3e_not_last",
            [](plain_rig::Rig const& rig) {
                std::string const inner =
                    "35=0\x01"
                    "34=3\x01"
                    "49=TW\x01"
                    "52=" +
                    rig.sending_time() +
                    "\x01"
                    "56=ISLD\x01"
                    "10=000\x01"
                    "58=TRAILING\x01";
                return "8=FIX.4.2\x01"
                       "9=" +
                       std::to_string(inner.size()) + "\x01" + inner;
            },
            true, fixpp::core::error::wire_invalid_body_length, true});
}

TEST(InboundFrameDispositionsTc, TC003_3e_CheckSumNotThreeDigits_DisregardedAndContinues) {
    run_q1({"TC003_3e_not_three_digits",
            [](plain_rig::Rig const& rig) {
                return with_trailer(
                    hb3(rig), [](std::string const& d) { return "10=" + d.substr(1) + "\x01"; });
            },
            true, fixpp::core::error::wire_checksum_mismatch, true});
}

TEST(InboundFrameDispositionsTc, TC003_3e_CheckSumNotSohTerminated_DisregardedAndContinues) {
    run_q1({"TC003_3e_not_soh_terminated",
            [](plain_rig::Rig const& rig) {
                return with_trailer(hb3(rig), [](std::string const& d) { return "10=" + d + "X"; });
            },
            true, fixpp::core::error::wire_checksum_mismatch, true});
}

// ── Q-5 (T028): accounting across feeds, ordering, the log rate, the ring ────
//
// Every cell runs through the real pump on the mock engine clock. "A feed" is one
// Framer::feed call in the pump. A cell that needs two writes read by two feeds waits
// for an effect only the first feed can cause before it issues the second write; a
// settle window is not such a barrier. Base RED: the session closes at the first
// garbled byte.

// A garbled region of the resync kind that discards exactly its own bytes: it holds no
// "8=FIX" and does not end in a proper prefix of it.
std::string junk(std::size_t n = 12) { return std::string(n, 'Q') + "\x01"; }

struct PumpOptions {
    std::shared_ptr<Application> app;
    std::optional<std::chrono::seconds> heartbeat = std::chrono::seconds{30};
    std::string begin_string = "FIX.4.2";
    std::uint32_t logout_disconnect_timeout_ms = 500;
};

struct PumpCell {
    LogCapture log;
    plain_rig::Rig rig;
    bool up = false;

    explicit PumpCell(PumpOptions opt = {}) : rig{std::move(opt.app)} {
        rig.begin_string = std::move(opt.begin_string);
        auto cfg = rig.cfg();
        cfg.logger_override = log.logger;
        cfg.initial_trace_context = known_trace();
        cfg.heartbeat_interval = opt.heartbeat;
        cfg.logout_disconnect_timeout_ms = opt.logout_disconnect_timeout_ms;
        up = rig.start(std::move(cfg));
    }

    // The garble state, before stop().
    [[nodiscard]] GarbleObservation observe() const {
        auto const s = rig.session();
        return s ? observe_garbles(*s) : GarbleObservation{};
    }
};

// A region split across two reads is one region: counted once, evented once (by the
// feed that opened it; the continuing feed's summary has regions == 0 and emits no
// event), with the opening feed's discarded bytes.
TEST(InboundFrameDispositionsQ5, RegionSplitAcrossReadsCountsOnceAndTheContinuingFeedEmitsNothing) {
    PumpCell c;
    bool const active = c.up && c.rig.to_active();
    std::string const first = std::string(40, 'Q');
    // The barrier: the region is counted only once a feed has read the first write.
    bool const d1 = active && c.rig.deliver(first) && c.rig.run_until([&] {
        auto const s = c.rig.session();
        return s && garbled_count(*s) == 1U;
    });
    bool const d2 = d1 && c.rig.deliver(std::string(25, 'Q') + "\x01" + c.rig.heartbeat(2));
    bool const processed = d2 && c.rig.run_until([&] {
        auto const s = c.rig.session();
        return s && next_inbound(*s) == 3U;
    });
    auto const o = c.observe();
    auto const st = c.rig.state();
    c.rig.stop();
    auto const records = c.log.garble_records();

    ASSERT_TRUE(active && d1 && d2) << "setup";
    EXPECT_TRUE(processed) << "the good frame after the split region must be processed";
    EXPECT_EQ(st, fsm_state::Active);
    expect_one_garble(o, records, {fixpp::core::error::wire_framing_resync, first.size()},
                      "split region");
}

// Application that records, at each fromAdmin call, the session's garble count and
// whether its event ring already holds a garble event.
class CountAtFromAdmin final : public Application {
public:
    fixpp::session::Engine* engine = nullptr;
    SessionId id;
    std::vector<std::uint64_t> counts;
    std::vector<bool> evented;

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) override {
        auto const s = engine ? engine->lookup(id) : nullptr;
        counts.push_back(s ? garbled_count(*s) : ~std::uint64_t{0});
        evented.push_back(s && !observe_garbles(*s).events.empty());
        return {};
    }
};

// Garbles a feed reports precede every frame it produces (C-1 Ordering), and the pump
// accounts them before it delivers that frame: the Heartbeat's own fromAdmin already
// sees the count. recent_events() is a membership ring, not an order, so the order is
// read inside the frame's callback instead.
TEST(InboundFrameDispositionsQ5, GarblesBeforeAFrameInOneFeedAreCountedBeforeThatFramesEffects) {
    auto app = std::make_shared<CountAtFromAdmin>();
    PumpCell c{{.app = app}};
    app->engine = c.rig.engine.get();
    app->id = c.rig.id;
    bool const active = c.up && c.rig.to_active();
    auto const before = app->counts.size();
    bool const d = active && c.rig.deliver(junk() + c.rig.heartbeat(2));
    bool const processed = d && c.rig.run_until([&] { return app->counts.size() == before + 1U; });
    auto const counts = app->counts;
    auto const evented = app->evented;
    c.rig.stop();

    ASSERT_TRUE(active && d) << "setup";
    ASSERT_TRUE(processed) << "the Heartbeat must reach fromAdmin";
    EXPECT_EQ(counts.back(), 1U)
        << "the garble ahead of the Heartbeat is counted before its fromAdmin";
    EXPECT_TRUE(evented.back())
        << "the garble ahead of the Heartbeat is evented before its fromAdmin";
}

// One garble (leading junk) followed by the next good Heartbeat, as one write.
bool garble_then_heartbeat(PumpCell& c, std::uint32_t seq) {
    return c.rig.deliver(junk() + c.rig.heartbeat(seq)) && c.rig.run_until([&] {
        auto const s = c.rig.session();
        return s && next_inbound(*s) == seq + 1U;
    });
}

// The rest of a region an earlier feed opened, then the next good Heartbeat.
bool garble_then_heartbeat_continuing(PumpCell& c, std::uint32_t seq) {
    return c.rig.deliver(std::string(25, 'Q') + "\x01" + c.rig.heartbeat(seq)) &&
           c.rig.run_until([&] {
               auto const s = c.rig.session();
               return s && next_inbound(*s) == seq + 1U;
           });
}

// The suppressed count each garble record carries, in record order.
std::vector<std::uint64_t> suppressed_counts(std::vector<fixpp::log::Record> const& records) {
    std::vector<std::uint64_t> out;
    for (auto const& r : records) out.push_back(r.arg_count == 3U ? r.args[2].u64 : ~0ULL);
    return out;
}

// HeartBtInt = 30 s: at most one record per 30 s. A garble 1 ms before the interval
// has elapsed is counted and evented but not logged; the next one, at the interval,
// writes a record carrying the one suppressed since the previous record.
TEST(InboundFrameDispositionsQ5, LogRecordsAreRateBoundedToOnePerHeartBtInt) {
    using namespace std::chrono_literals;
    PumpCell c;
    bool ok = c.up && c.rig.to_active();
    ok = ok && garble_then_heartbeat(c, 2);
    // KIND A (ci/mock-clock-staging-sweep.sh): a time stamp; the next garble reads it
    // synchronously (note_garbles_'s steady_now, the Heartbeat's SendingTime). No waiter.
    c.rig.clock->advance(30s - 1ms);
    ok = ok && garble_then_heartbeat(c, 3);
    // KIND A (ci/mock-clock-staging-sweep.sh): a time stamp; the next garble reads it
    // synchronously (note_garbles_'s steady_now, the Heartbeat's SendingTime). No waiter.
    c.rig.clock->advance(1ms);
    ok = ok && garble_then_heartbeat(c, 4);
    auto const o = c.observe();
    c.rig.stop();
    auto const records = c.log.garble_records();

    ASSERT_TRUE(ok) << "setup: each garble must be followed by its processed Heartbeat";
    EXPECT_EQ(o.count, 3U) << "every garble is counted";
    EXPECT_EQ(o.events.size(), 3U) << "every garble is evented";
    EXPECT_EQ(suppressed_counts(records), (std::vector<std::uint64_t>{0U, 1U}))
        << "one record at the first garble, none 1 ms before the interval, one at it";
}

// HeartBtInt = 0 is legal and disables liveness; the 1 s floor still bounds the log.
// The two edges are pinned (no record at 999 ms, a record at 1000 ms), then a sustained
// stream of garbles every 100 ms writes one record per second.
TEST(InboundFrameDispositionsQ5, HeartBtIntZeroStillBoundsTheLogToOneRecordPerSecond) {
    using namespace std::chrono_literals;
    PumpCell c{{.heartbeat = std::chrono::seconds{0}}};
    bool ok = c.up && c.rig.to_active();
    std::uint32_t seq = 2;
    ok = ok && garble_then_heartbeat(c, seq++);  // t = 0: logged
    // KIND A (ci/mock-clock-staging-sweep.sh): a time stamp; the next garble reads it
    // synchronously (note_garbles_'s steady_now, the Heartbeat's SendingTime). No waiter.
    c.rig.clock->advance(999ms);
    ok = ok && garble_then_heartbeat(c, seq++);  // t = 0.999 s: suppressed
    // KIND A (ci/mock-clock-staging-sweep.sh): a time stamp; the next garble reads it
    // synchronously (note_garbles_'s steady_now, the Heartbeat's SendingTime). No waiter.
    c.rig.clock->advance(1ms);
    ok = ok && garble_then_heartbeat(c, seq++);  // t = 1 s: logged, 1 suppressed
    for (int i = 0; i < 10; ++i) {               // t = 1.1 s … 2.0 s
        // KIND A (ci/mock-clock-staging-sweep.sh): a time stamp; the next garble reads it
        // synchronously (note_garbles_'s steady_now, the Heartbeat's SendingTime). No waiter.
        c.rig.clock->advance(100ms);
        ok = ok && garble_then_heartbeat(c, seq++);
    }
    auto const o = c.observe();
    c.rig.stop();
    auto const records = c.log.garble_records();

    ASSERT_TRUE(ok) << "setup: each garble must be followed by its processed Heartbeat";
    EXPECT_EQ(o.count, 13U) << "every garble is counted";
    EXPECT_EQ(suppressed_counts(records), (std::vector<std::uint64_t>{0U, 1U, 9U}))
        << "records at 0 s, 1 s and 2 s; 1 suppressed before the second, 9 before the third";
}

// The suppressed count's unit is the garbled region, the counter's unit (plan OD-21):
// a record carries the regions counted since the previous record that no record named,
// i.e. every region of each rate-suppressed summary and the triggering summary's own
// regions after its first. So the sum over records of (1 + suppressed) equals
// garbled_frame_count() when the last record is written. Here: a summary of three
// regions is logged (2 unnamed); then, inside the interval, a region opened by one
// feed (suppressed) and continued by the next (regions == 0: nothing added, nothing
// written), and one more suppressed region; then a record at the interval.
TEST(InboundFrameDispositionsQ5, LogRecordsReconcileWithTheCounterRegionByRegion) {
    using namespace std::chrono_literals;
    PumpCell c;
    bool ok = c.up && c.rig.to_active();
    // Three structurally complete frames with a wrong CheckSum, back to back: each is
    // one region, and the byte after each is a search position that finds the next.
    auto const wrong = [&](std::uint32_t seq) {
        return with_trailer(c.rig.heartbeat(seq), [](std::string const& d) {
            return "10=" + std::string{d == "000" ? "001" : "000"} + "\x01";
        });
    };
    ok = ok && c.rig.deliver(wrong(2) + wrong(2) + wrong(2) + c.rig.heartbeat(2)) &&
         c.rig.run_until([&] {
             auto const s = c.rig.session();
             return s && next_inbound(*s) == 3U;
         });
    auto const after_first = c.observe().count;
    ok = ok && c.rig.deliver(std::string(40, 'Q'));     // opens a region: suppressed
    ok = ok && garble_then_heartbeat_continuing(c, 3);  // continues it: adds nothing
    auto const after_continuation = c.observe().count;
    ok = ok && garble_then_heartbeat(c, 4);  // suppressed
    c.rig.clock->advance(30s);
    ok = ok && garble_then_heartbeat(c, 5);  // logged
    auto const o = c.observe();
    c.rig.stop();
    auto const records = c.log.garble_records();

    ASSERT_TRUE(ok) << "setup: each garble must be followed by its processed Heartbeat";
    EXPECT_EQ(after_first, 3U) << "the first write opens three regions";
    EXPECT_EQ(after_continuation, 4U) << "the continuing feed adds nothing";
    EXPECT_EQ(o.count, 6U);
    auto const suppressed = suppressed_counts(records);
    EXPECT_EQ(suppressed, (std::vector<std::uint64_t>{2U, 2U}))
        << "the first record leaves its summary's two later regions unnamed; the second "
           "carries the two suppressed regions";
    std::uint64_t reconciled = 0;
    for (auto const n : suppressed) reconciled += 1U + n;
    EXPECT_EQ(reconciled, o.count) << "the records reconcile with the counter";
}

// L-9: more garble events than the event ring holds. The ring keeps its capacity
// (include/fixpp/session/session_event.hpp) and evicts; the counter stays exact.
TEST(InboundFrameDispositionsQ5, AGarbleFloodLargerThanTheEventRingLeavesTheCounterExact) {
    constexpr std::uint32_t kGarbles = (2U * kSessionEventRingCapacity) + 3U;
    PumpCell c;
    bool ok = c.up && c.rig.to_active();
    for (std::uint32_t i = 0; i < kGarbles; ++i) ok = ok && garble_then_heartbeat(c, 2U + i);
    auto const o = c.observe();
    auto const st = c.rig.state();
    c.rig.stop();

    ASSERT_TRUE(ok) << "setup: each garble must be followed by its processed Heartbeat";
    EXPECT_EQ(st, fsm_state::Active);
    EXPECT_EQ(o.count, kGarbles) << "the counter is the durable signal (L-9)";
    EXPECT_EQ(o.events.size(), kSessionEventRingCapacity) << "the ring holds only its capacity";
}

// ── Q-8 (T029): garbled and 35-not-third frames before Active ──────────────
//
// C-2 step 1 applies in every arm except Disconnected: a frame whose third field is not
// MsgType(35) is disregarded and accounted as one garble of kind
// wire_header_out_of_order (garbled_frame_count(), an event, a log record), whatever
// the state. Before Active such a frame was refused (NotConnected, LogonSent) or
// processed; in LogoutSent (092's D-9) it was disregarded and not accounted. The pump
// accounts a Framer garble in every state, Disconnected included, because it runs
// while the transport is open.
//
// The cases below for step 1 drive a Session directly (no Engine), since the step is
// the arm's own decision; the Framer cases drive the real pump through plain_rig.
// LogonReceived is left inside the on_inbound_frame call that entered it, and the pump
// awaits that call, so no Framer garble reaches the pump while the session is in it.
// Its cases for step 1 sit with 092's parked-Logon-reply cases in
// unparseable_frame_disposition_test.cpp.
// Each cell ends by its own bound: a later frame moves the session on, or the logout
// timeout ends it, since the establishment timeout lands with User Story 2.

constexpr std::string_view kFixedSendingTime = "20240101-00:00:00.000";
constexpr std::string_view kLogonFields =
    "98=0\x01"
    "108=30\x01";

// A frame from the peer at the mock clock's start, fields in the usual order.
std::string direct_msg(std::string_view msg_type, std::uint32_t seq, std::string_view extra = {},
                       std::string_view begin_string = "FIX.4.2") {
    return plain_rig::message(begin_string, msg_type, seq, "TW", "ISLD", kFixedSendingTime, extra);
}

// The same frame with SenderCompID(49) third and MsgType(35) fourth: fault-free, but
// MsgType is not the third field (FIX-SL §4.5.2 criterion 3).
std::string not_third(std::string_view msg_type, std::uint32_t seq, std::string_view extra = {}) {
    return plain_rig::frame("FIX.4.2",
                            "49=TW\x01"
                            "35=" +
                                std::string{msg_type} +
                                "\x01"
                                "34=" +
                                std::to_string(seq) +
                                "\x01"
                                "52=" +
                                std::string{kFixedSendingTime} +
                                "\x01"
                                "56=ISLD\x01" +
                                std::string{extra});
}

struct DirectFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    LogCapture log;
    std::vector<std::string> sent;  // every outbound frame, as text

    DirectFixture() {
        using namespace std::chrono;
        clock = std::make_shared<fixpp::core::mock_clock>(
            system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
            ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig cfg(session_role role) {
        SessionConfig c;
        c.sender_comp_id = "ISLD";
        c.target_comp_id = "TW";
        c.begin_string = "FIX.4.2";
        c.heartbeat_interval = std::chrono::seconds{30};
        c.security_profile = fixpp::test_support::make_minimal_security_profile();
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.executor_override = ioc.get_executor();
        c.role = role;
        c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        c.logger_override = log.logger;
        c.initial_trace_context = known_trace();
        c.transport_send = [this](std::span<const std::byte> f) {
            sent.emplace_back(reinterpret_cast<const char*>(f.data()), f.size());
        };
        return c;
    }

    [[nodiscard]] bool run(asio::awaitable<fixpp::core::expected_t<void>> a, const char* site) {
        auto fut = asio::co_spawn(ioc, std::move(a), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(ioc, fut, fixpp::test_support::kPumpBudget,
                                                   site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << site;
            return false;
        }
        (void)fut.get();
        return true;
    }

    [[nodiscard]] bool open(Session& s) { return run(s.open(), "DirectFixture::open"); }
    [[nodiscard]] bool feed(Session& s, std::string const& f) {
        sent.clear();
        auto const bytes = plain_rig::to_bytes(f);
        return run(s.on_inbound_frame(bytes), "DirectFixture::feed");
    }
};

// One garble of kind wire_header_out_of_order covering `frame`, recorded by the arm.
void expect_step1_garble(DirectFixture& f, Session const& s, std::string const& frame,
                         std::string_view row) {
    auto const o = observe_garbles(s);
    expect_one_garble(o, f.log.garble_records(),
                      {fixpp::core::error::wire_header_out_of_order, frame.size()}, row);
}

TEST(InboundFrameDispositionsQ8, NotConnected_MsgTypeNotThird_DisregardedAndTheLogonThenProcessed) {
    DirectFixture f;
    Session s{f.engine, f.cfg(session_role::acceptor)};
    ASSERT_TRUE(f.open(s));
    ASSERT_EQ(s.state(), fsm_state::NotConnected);
    auto const bad = not_third("A", 1, kLogonFields);
    ASSERT_TRUE(f.feed(s, bad));
    EXPECT_EQ(s.state(), fsm_state::NotConnected) << "disregarded: not processed, not refused";
    EXPECT_TRUE(f.sent.empty()) << "a disregard sends nothing";
    EXPECT_TRUE(s.is_open());
    ASSERT_TRUE(f.feed(s, direct_msg("A", 1, kLogonFields)));
    EXPECT_EQ(s.state(), fsm_state::Active) << "the session still takes the Logon that follows";
    expect_step1_garble(f, s, bad, "NotConnected");
}

TEST(InboundFrameDispositionsQ8, LogonSent_MsgTypeNotThird_DisregardedAndTheReplyThenProcessed) {
    DirectFixture f;
    Session s{f.engine, f.cfg(session_role::initiator)};
    ASSERT_TRUE(f.open(s));
    ASSERT_EQ(s.state(), fsm_state::LogonSent);
    auto const bad = not_third("A", 1, kLogonFields);
    ASSERT_TRUE(f.feed(s, bad));
    EXPECT_EQ(s.state(), fsm_state::LogonSent) << "disregarded: not processed, not refused";
    EXPECT_TRUE(f.sent.empty()) << "a disregard sends nothing";
    ASSERT_TRUE(f.feed(s, direct_msg("A", 1, kLogonFields)));
    EXPECT_EQ(s.state(), fsm_state::Active) << "the session still takes the reply that follows";
    expect_step1_garble(f, s, bad, "LogonSent");
}

// D-9: in LogoutSent a frame whose third field is not 35 stays disregarded (it is not
// taken as the Logout reply, so the logout timeout ends the session) and is now
// counted, evented and logged. The logout timeout is below the heartbeat interval, so
// advancing past it cannot start the liveness exchange, and the close pump's budget is
// half that timeout, so the real-time close_grace timer (armed for the same duration)
// cannot complete the close within it: only the mock-clock logout timeout can.
TEST(InboundFrameDispositionsQ8, LogoutSent_MsgTypeNotThird_CountedAndNotTakenAsTheReply) {
    DirectFixture f;
    auto c = f.cfg(session_role::initiator);
    c.logout_disconnect_timeout_ms = 20000;
    auto const timeout = std::chrono::milliseconds{c.logout_disconnect_timeout_ms};
    Session s{f.engine, c};
    ASSERT_TRUE(f.open(s));
    ASSERT_TRUE(f.feed(s, direct_msg("A", 1, kLogonFields)));
    ASSERT_EQ(s.state(), fsm_state::Active);

    auto close_fut = asio::co_spawn(f.ioc, s.close(close_mode::graceful), asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until(
        f.ioc, [&s] { return s.state() == fsm_state::LogoutSent; }, timeout / 2))
        << "the graceful close must reach LogoutSent";

    auto const bad = not_third("5", 2);
    ASSERT_TRUE(f.feed(s, bad));
    EXPECT_EQ(s.state(), fsm_state::LogoutSent) << "not taken as the Logout reply";
    EXPECT_NE(close_fut.wait_for(std::chrono::seconds{0}), std::future_status::ready)
        << "close(graceful) still awaits the reply or the logout timeout";
    EXPECT_TRUE(f.sent.empty()) << "a disregard sends nothing";
    auto const o = observe_garbles(s);

    f.clock->advance(timeout + std::chrono::milliseconds{1});
    ASSERT_TRUE(fixpp::test_support::pump_until_ready(f.ioc, close_fut, timeout / 2))
        << "the logout timeout must end the close";
    (void)close_fut.get();
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
    expect_one_garble(o, f.log.garble_records(),
                      {fixpp::core::error::wire_header_out_of_order, bad.size()}, "LogoutSent");
}

// Disconnected ignores every frame without a scan (C-2): a 35-not-third frame there is
// not accounted at all. Regression guard; mutant: run step 1 in Disconnected too,
// read through garbled_frame_count().
TEST(InboundFrameDispositionsQ8, Disconnected_MsgTypeNotThird_IsNotScannedOrCounted) {
    DirectFixture f;
    Session s{f.engine, f.cfg(session_role::acceptor)};
    ASSERT_TRUE(f.open(s));
    ASSERT_TRUE(f.feed(s, direct_msg("0", 1)));  // a first frame that is not a Logon
    ASSERT_EQ(s.state(), fsm_state::Disconnected);
    ASSERT_TRUE(f.feed(s, not_third("0", 2)));
    auto const o = observe_garbles(s);
    EXPECT_EQ(o.count, 0U) << "Disconnected does not scan, so it does not count";
    EXPECT_TRUE(o.events.empty());
    EXPECT_TRUE(f.log.garble_records().empty());
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
}

// ── Q-8 through the pump: Framer garbles before Active, and in Disconnected ──

// NotConnected: an acceptor's first frame whose third field is not 35 still carries
// matching CompIDs, so the session is built and the frame delivered; it is disregarded
// and the session stays NotConnected with the pump running. A Framer garble then
// counts too, and the Logon that follows is processed.
TEST(InboundFrameDispositionsQ8Pump, NotConnected_FramerGarbleCountedAndTheLogonThenProcessed) {
    PumpCell c;
    bool const connected = c.up && c.rig.connect_peer();
    auto const first = connected ? plain_rig::frame("FIX.4.2",
                                                    "49=TW\x01"
                                                    "35=A\x01"
                                                    "34=1\x01"
                                                    "52=" +
                                                        c.rig.sending_time() +
                                                        "\x01"
                                                        "56=ISLD\x01" +
                                                        std::string{kLogonFields})
                                 : std::string{};
    bool const d1 = connected && c.rig.deliver(first);
    bool const published = d1 && c.rig.run_until([&] { return c.rig.session() != nullptr; });
    auto const state_after_first = c.rig.state();
    bool const d2 = published && c.rig.deliver(junk());
    auto const o_garble = c.observe();
    auto const state_after_garble = c.rig.state();
    bool const d3 = d2 && c.rig.deliver(c.rig.logon());
    bool const active = d3 && c.rig.run_until([&] { return c.rig.state() == fsm_state::Active; });
    bool const read_ended = c.rig.peer.read_ended;
    c.rig.stop();

    ASSERT_TRUE(connected && d1 && published && d2 && d3) << "setup";
    EXPECT_EQ(state_after_first, fsm_state::NotConnected) << "the first frame is disregarded";
    EXPECT_EQ(state_after_garble, fsm_state::NotConnected) << "the garble is disregarded";
    EXPECT_EQ(o_garble.count, 2U) << "the first frame (step 1) and the Framer garble";
    ASSERT_EQ(o_garble.events.size(), 2U);
    EXPECT_TRUE(std::ranges::any_of(o_garble.events, [](auto const& e) {
        return e.first_kind == fixpp::core::error::wire_framing_resync;
    })) << "the Framer garble's event";
    EXPECT_TRUE(active) << "the Logon after the garbles is processed";
    EXPECT_FALSE(read_ended) << "the connection stays open";
}

// LogonSent: the initiator's pump counts a Framer garble ahead of the peer's Logon
// reply, which is then processed.
TEST(InboundFrameDispositionsQ8Pump, LogonSent_FramerGarbleCountedAndTheReplyThenProcessed) {
    LogCapture log;
    plain_rig::Rig rig;
    auto cfg = rig.cfg(session_role::initiator);
    cfg.logger_override = log.logger;
    bool const up = rig.start(std::move(cfg));
    bool const logon_sent = up && rig.run_until([&] {
        return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
               rig.state() == fsm_state::LogonSent;
    });
    bool const d1 = logon_sent && rig.deliver(junk());
    auto const state_after_garble = rig.state();
    GarbleObservation o;
    if (auto const s = rig.session()) o = observe_garbles(*s);
    bool const d2 = d1 && rig.deliver(rig.logon());
    bool const active = d2 && rig.run_until([&] { return rig.state() == fsm_state::Active; });
    rig.stop();

    ASSERT_TRUE(up && logon_sent && d1 && d2) << "setup";
    EXPECT_EQ(state_after_garble, fsm_state::LogonSent) << "the garble is disregarded";
    EXPECT_EQ(o.count, 1U);
    ASSERT_EQ(o.events.size(), 1U);
    EXPECT_EQ(o.events[0].first_kind, fixpp::core::error::wire_framing_resync);
    EXPECT_EQ(o.events[0].discarded_bytes, junk().size());
    EXPECT_TRUE(active) << "the reply after the garble is processed";
}

// LogoutSent: a Framer garble while the session awaits the peer's Logout reply is
// counted and changes nothing: the pump keeps reading, so the reply that follows the
// garble confirms the Logout and ends the session at once. logout_disconnect_timeout_ms
// is far above every real-time budget here, so neither the real close_grace timer nor
// the mock-clock logout timeout (never advanced before the reply) can end it instead.
TEST(InboundFrameDispositionsQ8Pump, LogoutSent_FramerGarbleCountedAndTheReplyStillConfirms) {
    constexpr std::uint32_t kLogoutTimeoutMs = 20000;
    PumpCell c{{.logout_disconnect_timeout_ms = kLogoutTimeoutMs}};
    bool const active = c.up && c.rig.to_active();
    auto const s = c.rig.session();
    bool logout_sent = false;
    if (active && s) {
        asio::co_spawn(s->executor().underlying(), s->close(close_mode::graceful), asio::detached);
        logout_sent = c.rig.run_until([&] { return s->state() == fsm_state::LogoutSent; },
                                      std::chrono::milliseconds{200});
    }
    bool const d = logout_sent && c.rig.deliver(junk());
    auto const state_after = s ? s->state() : fsm_state::NotConnected;
    auto const o = s ? observe_garbles(*s) : GarbleObservation{};
    bool const r = d && c.rig.deliver(c.rig.msg("5", 2));
    bool const confirmed =
        r && c.rig.run_until([&] { return s->state() == fsm_state::Disconnected; },
                             std::chrono::seconds{2});
    // If the reply did not end it, the mock-clock logout timeout does, so Engine::stop()
    // does not wait on the graceful close.
    if (logout_sent && !confirmed) {
        c.rig.clock->advance(std::chrono::milliseconds{kLogoutTimeoutMs + 1U});
        (void)c.rig.run_until([&] { return s->state() == fsm_state::Disconnected; },
                              std::chrono::seconds{2});
    }
    c.rig.stop();

    ASSERT_TRUE(active && logout_sent && d && r) << "setup";
    EXPECT_EQ(state_after, fsm_state::LogoutSent) << "the garble is not the Logout reply";
    EXPECT_EQ(o.count, 1U);
    ASSERT_EQ(o.events.size(), 1U);
    EXPECT_EQ(o.events[0].first_kind, fixpp::core::error::wire_framing_resync);
    EXPECT_TRUE(confirmed) << "the pump read on past the garble, so the reply confirms the Logout";
}

// Disconnected with the transport open: an acceptor whose first frame is not a Logon
// refuses it into Disconnected and the pump keeps reading. A Framer garble there is
// counted, evented and logged (C-2's note: only the arm reads field 3).
TEST(InboundFrameDispositionsQ8Pump, Disconnected_FramerGarbleIsCountedWhileThePumpRuns) {
    PumpCell c;
    bool const connected = c.up && c.rig.connect_peer();
    bool const d1 = connected && c.rig.deliver(c.rig.heartbeat(1));
    bool const refused =
        d1 && c.rig.run_until([&] { return c.rig.state() == fsm_state::Disconnected; });
    bool const d2 = refused && c.rig.deliver(junk());
    auto const o = c.observe();
    bool const read_ended = c.rig.peer.read_ended;
    c.rig.stop();
    auto const records = c.log.garble_records();

    ASSERT_TRUE(connected && d1 && refused && d2) << "setup";
    EXPECT_FALSE(read_ended) << "a garble does not close the transport";
    expect_one_garble(o, records, {fixpp::core::error::wire_framing_resync, junk().size()},
                      "Disconnected");
}

// ── Q-10 (T031): BeginString(8) on each side of the W-2 cap ─────────────────
//
// A value within the cap keeps today's handling: in Active the session ends in
// Disconnected without a Logout, and before Active it refuses (regression guards). A
// value longer than the cap is a garble: disregarded and counted. A session whose own
// configured BeginString is longer than every supported identifier frames and
// processes its own frames (regression guard; T039's cap mutant turns it RED).

TEST(InboundFrameDispositionsQ10, WithinCapMismatchInActive_DisconnectsWithoutALogout) {
    PumpCell c;
    bool const active = c.up && c.rig.to_active();
    auto const before = c.rig.peer.received.size();
    bool const d = active && c.rig.deliver(plain_rig::message("FIX.4.4", "0", 2, "TW", "ISLD",
                                                              c.rig.sending_time()));
    bool const ended =
        d && c.rig.run_until([&] { return c.rig.state() == fsm_state::Disconnected; });
    auto const after = c.rig.peer.received.substr(before);
    auto const o = c.observe();
    c.rig.stop();

    ASSERT_TRUE(active && d) << "setup";
    EXPECT_TRUE(ended) << "a well-framed BeginString mismatch ends the session, as today";
    EXPECT_TRUE(plain_rig::frames_of_type(after, "5").empty()) << "no Logout is sent";
    EXPECT_EQ(o.count, 0U) << "a mismatch within the cap is not a garble";
}

TEST(InboundFrameDispositionsQ10, WithinCapMismatchBeforeActive_Refused) {
    DirectFixture f;
    Session s{f.engine, f.cfg(session_role::initiator)};
    ASSERT_TRUE(f.open(s));
    ASSERT_TRUE(f.feed(s, direct_msg("A", 1, kLogonFields, "FIX.4.4")));
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << "the mismatched reply is refused, as today";
    EXPECT_EQ(observe_garbles(s).count, 0U) << "a mismatch within the cap is not a garble";
}

TEST(InboundFrameDispositionsQ10, LongerThanTheCapInActive_IsAGarbleDisregardedAndCounted) {
    PumpCell c;
    bool const active = c.up && c.rig.to_active();
    std::string const long_bs = "FIX.4.2.TOO-LONG";
    auto const garbled =
        active ? plain_rig::message(long_bs, "0", 2, "TW", "ISLD", c.rig.sending_time())
               : std::string{};
    bool const d = active && c.rig.deliver(garbled + c.rig.heartbeat(2));
    bool const processed = d && c.rig.run_until([&] {
        auto const s = c.rig.session();
        return s && next_inbound(*s) == 3U;
    });
    auto const o = c.observe();
    auto const st = c.rig.state();
    c.rig.stop();
    auto const records = c.log.garble_records();

    ASSERT_TRUE(active && d) << "setup";
    ASSERT_GT(long_bs.size(), fixpp::wire::Framer::Config{}.max_begin_string_bytes)
        << "the value must exceed the default cap";
    EXPECT_EQ(st, fsm_state::Active) << "a garble does not end the session";
    EXPECT_TRUE(processed) << "the good frame after it is processed";
    expect_one_garble(o, records, {fixpp::core::error::wire_framing_resync, garbled.size()},
                      "BeginString longer than the cap");
}

TEST(InboundFrameDispositionsQ10, AConfiguredLongBeginStringFramesAndProcessesItsOwnFrames) {
    PumpCell c{{.begin_string = "FIX.4.2.CUSTOM-ID"}};
    bool const active = c.up && c.rig.to_active();
    bool const d = active && c.rig.deliver(c.rig.heartbeat(2));
    bool const processed = d && c.rig.run_until([&] {
        auto const s = c.rig.session();
        return s && next_inbound(*s) == 3U;
    });
    auto const o = c.observe();
    c.rig.stop();

    ASSERT_GT(std::string_view{"FIX.4.2.CUSTOM-ID"}.size(),
              fixpp::wire::Framer::Config{}.max_begin_string_bytes)
        << "the configured value must exceed the default cap";
    EXPECT_TRUE(active) << "the session reaches Active on its own BeginString";
    EXPECT_TRUE(processed) << "and processes its own frames";
    EXPECT_EQ(o.count, 0U) << "its own frames are not garbles";
}

}  // namespace
}  // namespace fixpp::session::test
