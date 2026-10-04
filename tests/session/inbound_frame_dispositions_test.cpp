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
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/pmr_arena_upstream.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <functional>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "plain_engine_rig.hpp"
#include "session/parse_capacity.hpp"  // 093 E-2: N(L) and the overlay term
#include "session/session_engine_access.hpp"  // 093 E-13: the engine-stop flag (Q-9 stop)
#include "support/fix44_dictionary.hpp"
#include "support/frame_view_factory.hpp"
#include "support/hooked_store.hpp"
#include "support/log_capture.hpp"
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

// ── Q-13, the 383 half (T055a; FR-010, plan OD-2) ───────────────────────────
//
// An advertised MaxMessageSize(383) below 4096 or above 262144 is refused with
// invalid_session_config by Engine::register_session and by Session::open(); the two
// ends of the range are accepted, so each refusal is keyed on its own bound.

TEST_F(InboundFrameDispositions, Q13_MaxMessageSizeOutsideTheRangeIsRefusedByRegisterSession) {
    fixpp::core::EngineConfig ec;
    ec.executor = ioc.get_executor();
    ec.clock = clock;
    fixpp::session::Engine eng{ioc.get_executor(), std::move(ec)};

    struct Row {
        std::uint32_t advertised;
        bool accepted;
        std::string comp;  // a distinct SenderCompID per row, so no row is a duplicate
    };
    std::vector<Row> const rows{{4095U, false, "R4095"},
                                {262145U, false, "R262145"},
                                {4096U, true, "R4096"},
                                {262144U, true, "R262144"}};
    std::vector<fixpp::core::expected_t<void>> results;
    for (auto const& row : rows) {
        auto cfg = make_acceptor_cfg(row.advertised);
        cfg.sender_comp_id = row.comp;
        results.push_back(eng.register_session(cfg));
    }

    auto stop_fut = asio::co_spawn(ioc, eng.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "InboundFrameDispositions::Q13_MaxMessageSize register")) {
        return;
    }
    stop_fut.get();

    for (std::size_t i = 0; i < rows.size(); ++i) {
        SCOPED_TRACE(rows[i].advertised);
        if (rows[i].accepted) {
            EXPECT_TRUE(results[i].has_value()) << "register_session refused an in-range 383";
        } else {
            ASSERT_FALSE(results[i].has_value()) << "register_session accepted an out-of-range 383";
            EXPECT_EQ(results[i].error(), fixpp::core::error::invalid_session_config);
        }
    }
}

TEST_F(InboundFrameDispositions, Q13_MaxMessageSizeOutsideTheRangeIsRefusedByOpen) {
    for (std::uint32_t const advertised : {4095U, 262145U}) {
        SCOPED_TRACE(advertised);
        Session refused(engine, make_acceptor_cfg(advertised));
        auto const r = open_sync(refused);
        ASSERT_FALSE(r.has_value()) << "open() accepted an out-of-range 383";
        EXPECT_EQ(r.error(), fixpp::core::error::invalid_session_config);
        EXPECT_FALSE(refused.is_open()) << "a refused open() left the session open";
    }
    for (std::uint32_t const advertised : {4096U, 262144U}) {
        SCOPED_TRACE(advertised);
        Session accepted(engine, make_acceptor_cfg(advertised));
        EXPECT_TRUE(open_sync(accepted).has_value()) << "open() refused an in-range 383";
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

using fixpp::test_support::LogCapture;

// The garble records the session wrote.
std::vector<fixpp::log::Record> garble_records(LogCapture& log) {
    return log.records_of(FIXPP_FORMAT_ID(kGarbleRecordFormat));
}

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
    auto const records = garble_records(log);

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
    auto const records = garble_records(c.log);

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
    auto const records = garble_records(c.log);

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
    auto const records = garble_records(c.log);

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
        return plain_rig::with_wrong_checksum(c.rig.heartbeat(seq));
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
    auto const records = garble_records(c.log);

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
    expect_one_garble(o, garble_records(f.log),
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
    expect_one_garble(o, garble_records(f.log),
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
    EXPECT_TRUE(garble_records(f.log).empty());
    EXPECT_EQ(s.state(), fsm_state::Disconnected);
}

// ── Q-9 (tasks.md T076; contract C-2, its steps 1 and 2; spec FR-030) ─────────
//
// close(graceful) from NotConnected or LogonSent yields only in its store flush, so the
// session's store is a HookedStore whose flush holds until the cell releases it. While
// close() is under way, a Heartbeat whose third field is not 35 reaches the arm: step 1
// counts, events and logs it as a garble (not an arm effect, so a closing session still
// accounts it), and the frame has no further effect: no frame is sent and the state
// is unchanged.
// Then the flush is released and close() ends the session.
void run_q9(session_role role, fsm_state expected, std::string_view row) {
    DirectFixture f;
    auto const factory = std::make_shared<fixpp::test_support::HookedStoreFactory>();
    auto release = std::make_shared<bool>(false);
    factory->hooks.flush_until = [release] { return *release; };
    auto const log = factory->log;
    auto cfg = f.cfg(role);
    cfg.store_factory = factory;
    Session s{f.engine, cfg};
    ASSERT_TRUE(f.open(s));
    ASSERT_EQ(s.state(), expected) << row;

    auto close_fut = asio::co_spawn(f.ioc, s.close(close_mode::graceful), asio::use_future);
    bool const flushing = fixpp::test_support::pump_until(
        f.ioc, [&] { return log->flushes_begun == 1; }, std::chrono::milliseconds{fixpp::test_support::kHoldBound} / 2,
        fixpp::test_support::kPumpSlice, "Q9/flush");
    EXPECT_TRUE(flushing) << row << ": close(graceful) never reached its store flush";
    auto const bad = not_third("0", 1);
    if (flushing) {
        EXPECT_TRUE(f.feed(s, bad)) << row;
        EXPECT_EQ(s.state(), expected) << row << ": the frame must not move the state";
        EXPECT_TRUE(f.sent.empty()) << row << ": the frame must draw nothing";
    }

    *release = true;
    if (!fixpp::test_support::pump_until_ready(f.ioc, close_fut, fixpp::test_support::kPumpBudget,
                                               "Q9/close")) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock, "Q9/close");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "Q9/close";
        return;
    }
    EXPECT_TRUE(close_fut.get().has_value()) << row << ": close()";
    EXPECT_FALSE(log->flush_hold_timed_out) << row << ": the flush hold waited out its bound";
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << row;
    if (flushing) {
        expect_step1_garble(f, s, bad, row);
    }
}

TEST(InboundFrameDispositionsQ9, NotConnected_MsgTypeNotThirdAfterCloseBeganIsOnlyCounted) {
    run_q9(session_role::acceptor, fsm_state::NotConnected, "NotConnected");
}

TEST(InboundFrameDispositionsQ9, LogonSent_MsgTypeNotThirdAfterCloseBeganIsOnlyCounted) {
    run_q9(session_role::initiator, fsm_state::LogonSent, "LogonSent");
}

// ── Q-9, Engine::stop() (contract C-2 step 2; data-model E-13; spec FR-030, FR-041) ──
//
// Engine::stop()'s step 1 sets the engine-stop flag on the session's strand before any
// close() runs. A frame that reaches the NotConnected or LogonSent arm after it has no
// arm effect, as after close() began: C-2's second step checks the arm's
// logon_arm_superseded_, which reads the flag. Each cell sets the flag through
// session_engine_access, then feeds a frame whose arm, were that step to test `closing`
// alone, runs an effect before the arm's first later logon_arm_superseded_ check:
//   - NotConnected, a first frame that is not a Logon: the refusal writes Disconnected;
//   - NotConnected, a Logon carrying MaxMessageSize(383): the arm records the peer's 383;
//   - LogonSent, a reply whose SendingTime(52) is stale: the Logout's MsgSeqNum is
//     assigned, then toAdmin and the store write run (store_then_emit's own check
//     stops only the transmit);
//   - LogonSent, a well-formed reply: check_inbound advances NextNumIn.
// To check that a cell can fail, in a scratch copy replace that step's
// logon_arm_superseded_ call with `state_ == lifecycle::closing` in both arms of
// Session::on_inbound_frame: every cell must fail.
void note_engine_stop(Session& s) { fixpp::session::session_engine_access::note_engine_stop(s); }

TEST(InboundFrameDispositionsQ9Stop, NotConnected_NonLogonAfterStopStep1_NoStateWrite) {
    DirectFixture f;
    Session s{f.engine, f.cfg(session_role::acceptor)};
    ASSERT_TRUE(f.open(s));
    ASSERT_EQ(s.state(), fsm_state::NotConnected);
    note_engine_stop(s);
    ASSERT_TRUE(f.feed(s, direct_msg("0", 1)));
    EXPECT_EQ(s.state(), fsm_state::NotConnected) << "the refusal must not write Disconnected";
    EXPECT_TRUE(f.sent.empty()) << "the frame must draw nothing";
}

TEST(InboundFrameDispositionsQ9Stop, NotConnected_LogonAfterStopStep1_PeerMaxMessageSizeNotRecorded) {
    DirectFixture f;
    Session s{f.engine, f.cfg(session_role::acceptor)};
    ASSERT_TRUE(f.open(s));
    ASSERT_EQ(s.state(), fsm_state::NotConnected);
    note_engine_stop(s);
    ASSERT_TRUE(f.feed(s, direct_msg("A", 1, std::string{kLogonFields} + "383=8192\x01")));
    EXPECT_FALSE(s.peer_max_message_size().has_value()) << "the arm must not record the peer's 383";
    EXPECT_EQ(s.state(), fsm_state::NotConnected);
    EXPECT_TRUE(f.sent.empty()) << "the frame must draw nothing";
}

TEST(InboundFrameDispositionsQ9Stop, LogonSent_StaleReplyAfterStopStep1_NoLogout) {
    DirectFixture f;
    Session s{f.engine, f.cfg(session_role::initiator)};
    ASSERT_TRUE(f.open(s));
    ASSERT_EQ(s.state(), fsm_state::LogonSent);
    auto const next_out = session_test_access::seqnum_mgr(s).peek_outbound();
    note_engine_stop(s);
    ASSERT_TRUE(f.feed(s, plain_rig::message("FIX.4.2", "A", 1, "TW", "ISLD",
                                             "20200101-00:00:00.000", kLogonFields)));
    EXPECT_EQ(session_test_access::seqnum_mgr(s).peek_outbound(), next_out)
        << "the arm must not assign the Logout a MsgSeqNum";
    EXPECT_TRUE(f.sent.empty()) << "the arm must send no Logout";
    EXPECT_EQ(s.state(), fsm_state::LogonSent) << "the arm must not write Disconnected";
}

TEST(InboundFrameDispositionsQ9Stop, LogonSent_ReplyAfterStopStep1_NextNumInNotAdvanced) {
    DirectFixture f;
    Session s{f.engine, f.cfg(session_role::initiator)};
    ASSERT_TRUE(f.open(s));
    ASSERT_EQ(s.state(), fsm_state::LogonSent);
    ASSERT_EQ(next_inbound(s), 1U);
    note_engine_stop(s);
    ASSERT_TRUE(f.feed(s, direct_msg("A", 1, kLogonFields)));
    EXPECT_EQ(next_inbound(s), 1U) << "check_inbound must not advance NextNumIn";
    EXPECT_EQ(s.state(), fsm_state::LogonSent) << "the reply must not establish the session";
    EXPECT_TRUE(f.sent.empty()) << "the frame must draw nothing";
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
    auto const records = garble_records(c.log);

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
    auto const records = garble_records(c.log);

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

// ── US3 (T054–T060): a large well-formed message is never lost ──────────────
//
// The session's inbound limit L, the carry and the per-session parse buffer B(L) that
// open() allocates (data-model E-2; contract C-1, C-3). Each figure an assertion
// compares against is spelled out here from the bundle's formula, not read from the
// session: a change to the formula or to a constant must be written here too.

// The pump's read size R (data-model E-2), as the carry block's term.
constexpr std::size_t kReadSize = 4096;
// MSVC debug's container proxy, per pmr container (research R-3, T014).
constexpr std::size_t kContainerSlack = 16;

// The carry block open() draws from SessionConfig::framer_carry_arena.
constexpr std::size_t expected_carry_block(std::uint32_t limit) {
    return std::size_t{limit} + kReadSize + kContainerSlack;
}

// B(L) = 12·N(L) + 4·overlay_cap_for(N(L)) + kAlignPad + kCallbackReadHeadroom + the
// container slack, with N(L) = ⌊L/3⌋ + 1 (data-model E-2). overlay_cap_for is the next
// power of two at or above 1.25·n + 1, at least 8 (src/wire/offset_table.cpp). The
// slack term counts every pmr container one parse constructs (research R-3), each a
// proxy plus its alignment padding.
std::size_t expected_parse_buffer(std::uint32_t limit) {
    std::size_t const n = std::size_t{limit} / 3U + 1U;
    std::size_t cap = 8;
    while (cap < (n * 5U) / 4U + 1U) cap <<= 1U;
    // Each block's alignment minus one, worked by hand:
    // (alignof(OffsetTable::entry) - 1) + (alignof(std::uint32_t) - 1).
    constexpr std::size_t kAlignPad = 6;
    constexpr std::size_t kCallbackReadHeadroom = 16384;
    constexpr std::size_t kParseContainers = 10;
    // A proxy is pointer-aligned: alignof(void*) - 1, worked by hand.
    constexpr std::size_t kProxyPad = 7;
    return 12U * n + 4U * cap + kAlignPad + kCallbackReadHeadroom +
           kParseContainers * (kContainerSlack + kProxyPad);
}

// A memory resource that forwards to new_delete until a budget is spent, then refuses
// with bad_alloc, and counts what it served.
class BudgetResource final : public std::pmr::memory_resource {
public:
    std::size_t budget = static_cast<std::size_t>(-1);
    std::size_t served = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        if (bytes > budget - served) throw std::bad_alloc{};
        void* p = std::pmr::new_delete_resource()->allocate(bytes, align);
        served += bytes;
        return p;
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& o) const noexcept override {
        return this == &o;
    }
};

// An Application that counts the receive callbacks and keeps the field count of the
// last fromApp view; `on_app` runs inside fromApp on the view, for lazy reads.
class FromAppProbe final : public Application {
public:
    int from_app = 0;
    int from_admin = 0;
    std::size_t last_fields = 0;
    std::function<void(fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const&)> on_app;

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) override {
        ++from_admin;
        return {};
    }
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const SessionId& /*id*/) override {
        ++from_app;
        last_fields = msg.offsets().size();
        if (on_app) on_app(msg);
        return {};
    }
};

std::size_t soh_count(std::string_view f) {
    return static_cast<std::size_t>(std::ranges::count(f, '\x01'));
}

// A plaintext acceptor with a FromAppProbe, its 383 set to `advertised` (unset when
// nullopt), started and logged on. No logger: no cell using it reads a record, and the
// Q-33 death-test child then starts no logger thread after the fork.
struct LargeCell {
    std::shared_ptr<FromAppProbe> app = std::make_shared<FromAppProbe>();
    plain_rig::Rig rig{app};
    bool up = false;

    explicit LargeCell(std::optional<std::uint32_t> advertised,
                       std::shared_ptr<const fixpp::dict::Dictionary> dict = nullptr,
                       std::string begin_string = "FIX.4.2") {
        rig.begin_string = std::move(begin_string);
        auto cfg = rig.cfg();
        cfg.advertised_max_message_size = advertised;
        if (dict) cfg.dictionary = std::move(dict);
        up = rig.start(std::move(cfg)) && rig.to_active();
    }

    [[nodiscard]] std::uint32_t next_in() const {
        auto const s = rig.session();
        return s ? next_inbound(*s) : 0U;
    }
};

// ── Q-11 (T054): a dense frame of exactly L parses, is delivered, and fits B(L) ──
//
// The densest layout, "1=<SOH>" fields, filling a NewOrderSingle to exactly L bytes,
// at the unset default and at a configured 383 at each end of its range. The frame is
// delivered to fromApp with every field indexed, nothing spilled past the parse buffer
// (the spill witness, contract C-3 I-2), and the buffer is B(L). A spill would be a
// parse failure where the witness's upstream is null and a recorded spill where it is
// not, so "delivered and not spilled" is "the parse's peak fits B(L)" on every lane.
// Base RED: the parse fails at the 16 KiB stack arena or the entry cap (T007).
void run_q11(std::optional<std::uint32_t> advertised, std::uint32_t limit) {
    LargeCell c{advertised};
    std::string const f = c.up ? c.rig.msg_of_size("D", 2, {}, limit, "1", true) : std::string{};
    bool const built = !f.empty();
    bool const d = built && c.rig.deliver(f);
    bool const delivered = d && c.rig.run_until([&] { return c.app->from_app == 1; });
    bool const processed = delivered && c.rig.deliver(c.rig.heartbeat(3)) &&
                           c.rig.run_until([&] { return c.next_in() == 4U; });
    auto const s = c.rig.session();
    std::size_t const buffer = s ? session_test_access::parse_buffer_bytes(*s) : 0U;
    std::uint64_t const spills = s ? session_test_access::parse_spills(*s) : ~std::uint64_t{0};
    auto const st = c.rig.state();
    std::size_t const fields = c.app->last_fields;
    c.rig.stop();

    ASSERT_TRUE(c.up && built && d) << "setup";
    EXPECT_TRUE(delivered) << "the dense frame of exactly L reaches fromApp";
    EXPECT_EQ(fields, soh_count(f)) << "every field is indexed";
    EXPECT_TRUE(processed) << "the session processes the next frame";
    EXPECT_EQ(st, fsm_state::Active);
    EXPECT_EQ(spills, 0U) << "nothing spilled past the parse buffer";
    EXPECT_EQ(buffer, expected_parse_buffer(limit)) << "the parse buffer is B(L)";
}

TEST(InboundFrameDispositionsQ11, DenseFrameOfExactlyTheDefaultLimitIsDelivered) {
    run_q11(std::nullopt, 65536U);
}
TEST(InboundFrameDispositionsQ11, DenseFrameOfExactlyTheFloorLimitIsDelivered) {
    run_q11(4096U, 4096U);
}
TEST(InboundFrameDispositionsQ11, DenseFrameOfExactlyTheCeilingLimitIsDelivered) {
    run_q11(262144U, 262144U);
}

// B(L)'s overlay term equals what the offset table assigns for N(L) entries: the table's
// overlay rule is private (OffsetTable::overlay_cap_for), and parse_capacity reads it
// as a friend. Parsed at the wire level over a logging resource, a frame of exactly
// N(L) fields draws one overlay block, the one request that is neither a whole number
// of 12-byte entries nor a 16-byte proxy; its size must be the budgeted term's.
class RequestLog final : public std::pmr::memory_resource {
public:
    std::vector<std::size_t> sizes;

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        sizes.push_back(bytes);
        return std::pmr::new_delete_resource()->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& o) const noexcept override {
        return this == &o;
    }
};

TEST(InboundFrameDispositionsQ11, TheBufferBudgetsTheOverlayTheTableAssigns) {
    for (std::uint32_t const limit : {4096U, 65536U, 262144U}) {
        SCOPED_TRACE(limit);
        std::size_t const n = detail::parse_capacity::entry_cap_for(limit);
        // 8, 9, 35, 10 and n - 4 "1=" fields: n fields.
        std::string body = "35=D\x01";
        for (std::size_t i = 0; i + 4U < n; ++i) body += "1=\x01";
        std::string const f = plain_rig::frame("FIX.4.2", body);
        auto const bytes = plain_rig::to_bytes(f);
        auto const fv = fixpp::wire::test::make_frame_view(bytes);
        ASSERT_TRUE(fv.has_value());
        RequestLog log;
        fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{};
        auto const mv =
            parser.parse(*fv, &log, fixpp::wire::OffsetTable::Config{.max_offset_entries = n}, n);
        ASSERT_TRUE(mv.has_value());
        ASSERT_EQ(mv->offsets().size(), n);
        std::vector<std::size_t> overlay;
        for (std::size_t const b : log.sizes) {
            if (b % sizeof(fixpp::wire::OffsetTable::entry) != 0U && b != 16U) overlay.push_back(b);
        }
        ASSERT_EQ(overlay.size(), 1U) << "one overlay block";
        EXPECT_EQ(overlay[0], sizeof(std::uint32_t) * detail::parse_capacity::overlay_cap_for(n));
    }
}

// ── SC-007's measurement (T054): what open() draws from each arena ──────────
//
// framer_carry_arena serves the carry block, and the session arena serves B(L), both
// once at open(). Each arena is a BudgetResource with no budget, read before and after
// open(). The figures are recorded as test properties for the evidence file.
void run_open_draws(std::optional<std::uint32_t> advertised, std::uint32_t limit) {
    DirectFixture f;
    BudgetResource carry_arena;
    BudgetResource session_arena;
    auto cfg = f.cfg(session_role::acceptor);
    cfg.advertised_max_message_size = advertised;
    cfg.framer_carry_arena = &carry_arena;
    cfg.session_arena = &session_arena;
    Session s{f.engine, cfg};
    std::size_t const carry_before = carry_arena.served;
    std::size_t const session_before = session_arena.served;
    ASSERT_TRUE(f.open(s));
    ASSERT_TRUE(s.is_open());
    std::size_t const carry_drawn = carry_arena.served - carry_before;
    std::size_t const session_drawn = session_arena.served - session_before;
    ::testing::Test::RecordProperty("carry_arena_bytes_at_open", std::to_string(carry_drawn));
    ::testing::Test::RecordProperty("session_arena_bytes_at_open", std::to_string(session_drawn));
    EXPECT_EQ(carry_drawn, expected_carry_block(limit)) << "the carry block";
    EXPECT_EQ(session_drawn, expected_parse_buffer(limit)) << "B(L)";
}

TEST(InboundFrameDispositionsSc007, OpenDrawsTheCarryAndBOfLAtTheDefaultLimit) {
    run_open_draws(std::nullopt, 65536U);
}
TEST(InboundFrameDispositionsSc007, OpenDrawsTheCarryAndBOfLAtTheCeilingLimit) {
    run_open_draws(262144U, 262144U);
}

// ── Q-14 (T055a, the carry half; T056, the session-arena half) ──────────────
//
// open() with an arena one byte short of what it must allocate returns an open()
// error and leaves the session never-opened; the exact amount opens. Base RED: open()
// draws nothing from either arena (the carry is built by the pump, and no B(L)
// exists), so it succeeds.
enum class Arena : std::uint8_t { carry, session };

void run_short_arena(Arena which, std::uint32_t limit, std::optional<std::uint32_t> advertised) {
    std::size_t const need =
        which == Arena::carry ? expected_carry_block(limit) : expected_parse_buffer(limit);
    for (std::size_t const budget : {need - 1U, need}) {
        SCOPED_TRACE(budget);
        DirectFixture f;
        BudgetResource arena;
        auto cfg = f.cfg(session_role::acceptor);
        cfg.advertised_max_message_size = advertised;
        (which == Arena::carry ? cfg.framer_carry_arena : cfg.session_arena) = &arena;
        Session s{f.engine, cfg};
        arena.budget = arena.served + budget;  // what construction drew stays drawn
        auto fut = asio::co_spawn(f.ioc, s.open(), asio::use_future);
        ASSERT_TRUE(fixpp::test_support::pump_until_ready(f.ioc, fut, "Q14::open"));
        auto const r = fut.get();
        if (budget < need) {
            ASSERT_FALSE(r.has_value()) << "open() succeeded over a short arena";
            EXPECT_EQ(r.error(), fixpp::core::error::out_of_memory);
            EXPECT_FALSE(s.is_open()) << "a failed open() left the session open";
        } else {
            EXPECT_TRUE(r.has_value()) << "open() failed over an arena of exactly its need";
        }
    }
}

TEST(InboundFrameDispositionsQ14, CarryArenaOneByteShortIsAnOpenError) {
    run_short_arena(Arena::carry, 65536U, std::nullopt);
}
TEST(InboundFrameDispositionsQ14, CarryArenaOneByteShortIsAnOpenErrorAtAConfiguredLimit) {
    run_short_arena(Arena::carry, 4096U, 4096U);
}
TEST(InboundFrameDispositionsQ14, SessionArenaOneByteShortIsAnOpenError) {
    run_short_arena(Arena::session, 65536U, std::nullopt);
}
TEST(InboundFrameDispositionsQ14, SessionArenaOneByteShortIsAnOpenErrorAtAConfiguredLimit) {
    run_short_arena(Arena::session, 4096U, 4096U);
}

// Through the engine: an acceptor registered with a carry arena too small for its
// carry is refused at open(), so the peer's Logon is never answered and the connection
// closes. Run under EXPECT_EXIT, so that a pump terminating on the undersized arena is
// a recorded failure, not a crashed binary. Exit codes: 0 = refused and closed with no
// Logon reply; 1 = the session was published or answered the Logon; 2 = setup failed;
// 3 = the connection stayed open.
[[noreturn]] void short_carry_arena_through_the_engine() {
    BudgetResource arena;
    arena.budget = expected_carry_block(65536U) - 1U;
    plain_rig::Rig rig;
    auto cfg = rig.cfg();
    cfg.framer_carry_arena = &arena;
    if (!rig.start(std::move(cfg)) || !rig.connect_peer()) std::exit(2);
    rig.peer.send(rig.logon());
    bool const ended = rig.run_until([&] { return rig.peer.read_ended; });
    bool const answered = !plain_rig::frames_of_type(rig.peer.received, "A").empty();
    bool const published = rig.session() != nullptr;
    rig.stop();
    if (answered || published) std::exit(1);
    std::exit(ended ? 0 : 3);
}

TEST(InboundFrameDispositionsQ14, ThroughTheEngineAShortCarryArenaRefusesTheConnection) {
    EXPECT_EXIT(short_carry_arena_through_the_engine(), ::testing::ExitedWithCode(0), "");
}

// ── Q-6 (T055): a frame over L closes, with no guard or handler reached ─────
//
// A configured 383 of 4096, so L = 4096. Each over-L shape closes the session
// terminally: the transport is closed, the state is Disconnected, and one record
// carries the failure kind and L. No callback runs, no Reject is sent and NextNumIn
// does not move. Run in Active and in each state the pump reads in before Active:
// NotConnected (an acceptor whose first frame was disregarded), LogonSent (an
// initiator) and Disconnected with the transport open (an acceptor whose first frame
// was refused). The record's format is spelled out here.
constexpr char kOverLimitRecordFormat[] =
    "inbound frame over the limit closed the session: kind={} limit={}";
constexpr std::uint32_t kQ6Limit = 4096;

enum class OverL : std::uint8_t {
    frame,
    bad_checksum,
    body_length_at_candidate,
    malformed_tag,         // a scan fault the session would Reject, were it parsed
    length_data_mismatch,  // likewise
};

std::string_view over_l_name(OverL k) {
    switch (k) {
        case OverL::frame:
            return "a frame of L+1";
        case OverL::bad_checksum:
            return "a frame of L+1 with a bad CheckSum";
        case OverL::body_length_at_candidate:
            return "an over-L BodyLength at a resync candidate";
        case OverL::malformed_tag:
            return "a frame of L+1 carrying a malformed tag";
        case OverL::length_data_mismatch:
            return "a frame of L+1 carrying a Length/Data mismatch";
    }
    return "?";
}

// The over-L bytes the peer sends, framed as `rig` frames them.
std::string over_l_bytes(plain_rig::Rig const& rig, OverL k, std::uint32_t seq) {
    switch (k) {
        case OverL::frame:
            return rig.msg_of_size("D", seq, {}, kQ6Limit + 1U, "58", false);
        case OverL::bad_checksum:
            return plain_rig::with_wrong_checksum(
                rig.msg_of_size("D", seq, {}, kQ6Limit + 1U, "58", false));
        case OverL::body_length_at_candidate:
            return junk() + "8=" + rig.begin_string + "\x01" +
                   "9=" + std::to_string(kQ6Limit + 1U) + "\x01" + "35=0\x01";
        case OverL::malformed_tag:
            return rig.msg_of_size("D", seq, "9x9=1\x01", kQ6Limit + 1U, "58", false);
        case OverL::length_data_mismatch:
            return rig.msg_of_size("D", seq,
                                   "90=2\x01"
                                   "91=xyz\x01",
                                   kQ6Limit + 1U, "58", false);
    }
    return {};
}

std::vector<fixpp::log::Record> over_limit_records(LogCapture& log) {
    return log.records_of(FIXPP_FORMAT_ID(kOverLimitRecordFormat));
}

enum class Q6State : std::uint8_t { active, not_connected, logon_sent, disconnected };

std::string_view q6_state_name(Q6State s) {
    switch (s) {
        case Q6State::active:
            return "Active";
        case Q6State::not_connected:
            return "NotConnected";
        case Q6State::logon_sent:
            return "LogonSent";
        case Q6State::disconnected:
            return "Disconnected";
    }
    return "?";
}

void run_q6(Q6State at, OverL kind) {
    std::string const row = std::string{q6_state_name(at)} + ", " + std::string{over_l_name(kind)};
    LogCapture log;
    auto app = std::make_shared<FromAppProbe>();
    plain_rig::Rig rig{app};
    auto cfg =
        rig.cfg(at == Q6State::logon_sent ? session_role::initiator : session_role::acceptor);
    cfg.logger_override = log.logger;
    cfg.initial_trace_context = known_trace();
    cfg.advertised_max_message_size = kQ6Limit;
    bool reached = rig.start(std::move(cfg));
    fsm_state want{};
    switch (at) {
        case Q6State::active:
            reached = reached && rig.to_active();
            want = fsm_state::Active;
            break;
        case Q6State::not_connected:
            // A first frame whose third field is not 35: disregarded, the pump runs.
            reached = reached && rig.connect_peer() &&
                      rig.deliver(plain_rig::frame(rig.begin_string,
                                                   "49=TW\x01"
                                                   "35=A\x01"
                                                   "34=1\x01"
                                                   "52=" +
                                                       rig.sending_time() +
                                                       "\x01"
                                                       "56=ISLD\x01" +
                                                       std::string{kLogonFields})) &&
                      rig.run_until([&] { return rig.session() != nullptr; });
            want = fsm_state::NotConnected;
            break;
        case Q6State::logon_sent:
            reached = reached && rig.run_until([&] {
                return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
                       rig.state() == fsm_state::LogonSent;
            });
            want = fsm_state::LogonSent;
            break;
        case Q6State::disconnected:
            // A first frame that is not a Logon: refused into Disconnected, the pump runs.
            reached = reached && rig.connect_peer() && rig.deliver(rig.heartbeat(1)) &&
                      rig.run_until([&] { return rig.state() == fsm_state::Disconnected; });
            want = fsm_state::Disconnected;
            break;
    }
    auto const s = rig.session();
    auto const state_before = rig.state();
    std::uint32_t const next_before = s ? next_inbound(*s) : 0U;
    int const app_before = app->from_app;
    int const admin_before = app->from_admin;
    std::size_t const rejects_before = plain_rig::frames_of_type(rig.peer.received, "3").size();
    bool const open_before = !rig.peer.read_ended;

    std::string const bytes = reached ? over_l_bytes(rig, kind, next_before) : std::string{};
    bool const sent = !bytes.empty() && rig.deliver(bytes);
    bool const closed = sent && rig.run_until([&] { return rig.peer.read_ended; });
    auto const state_after = rig.state();
    bool const is_open_after = s && s->is_open();
    std::uint32_t const next_after = s ? next_inbound(*s) : 0U;
    int const app_calls = app->from_app - app_before;
    int const admin_calls = app->from_admin - admin_before;
    std::size_t const rejects =
        plain_rig::frames_of_type(rig.peer.received, "3").size() - rejects_before;
    rig.stop();
    auto const records = over_limit_records(log);

    ASSERT_TRUE(reached && s && open_before && sent) << row << ": setup";
    ASSERT_EQ(state_before, want) << row << ": the state the cell runs in";
    EXPECT_TRUE(closed) << row << ": the transport closes";
    EXPECT_EQ(state_after, fsm_state::Disconnected) << row;
    EXPECT_FALSE(is_open_after) << row << ": a terminal close, not only a Disconnected state";
    EXPECT_EQ(app_calls, 0) << row << ": no fromApp";
    EXPECT_EQ(admin_calls, 0) << row << ": no fromAdmin";
    EXPECT_EQ(rejects, 0U) << row << ": no Reject";
    EXPECT_EQ(next_after, next_before) << row << ": NextNumIn unchanged";
    ASSERT_EQ(records.size(), 1U) << row << ": one over-limit record";
    ASSERT_EQ(records[0].arg_count, 2U) << row;
    EXPECT_EQ(records[0].args[0].u64,
              static_cast<std::uint64_t>(fixpp::core::error::wire_frame_too_large))
        << row << ": the record's kind";
    EXPECT_EQ(records[0].args[1].u64, kQ6Limit) << row << ": the record's L";
    auto const tc = known_trace();
    EXPECT_EQ(records[0].trace_id,
              (reinterpret_cast<std::array<std::uint8_t, 16> const&>(tc.trace_id)))
        << row << ": the record carries the session's trace_id";
}

TEST(InboundFrameDispositionsQ6, Active_FrameOfLPlusOneCloses) {
    run_q6(Q6State::active, OverL::frame);
}
TEST(InboundFrameDispositionsQ6, Active_FrameOfLPlusOneWithABadCheckSumCloses) {
    run_q6(Q6State::active, OverL::bad_checksum);
}
TEST(InboundFrameDispositionsQ6, Active_OverLBodyLengthAtAResyncCandidateCloses) {
    run_q6(Q6State::active, OverL::body_length_at_candidate);
}
TEST(InboundFrameDispositionsQ6, NotConnected_FrameOfLPlusOneCloses) {
    run_q6(Q6State::not_connected, OverL::frame);
}
TEST(InboundFrameDispositionsQ6, NotConnected_FrameOfLPlusOneWithABadCheckSumCloses) {
    run_q6(Q6State::not_connected, OverL::bad_checksum);
}
TEST(InboundFrameDispositionsQ6, NotConnected_OverLBodyLengthAtAResyncCandidateCloses) {
    run_q6(Q6State::not_connected, OverL::body_length_at_candidate);
}
TEST(InboundFrameDispositionsQ6, LogonSent_FrameOfLPlusOneCloses) {
    run_q6(Q6State::logon_sent, OverL::frame);
}
TEST(InboundFrameDispositionsQ6, LogonSent_FrameOfLPlusOneWithABadCheckSumCloses) {
    run_q6(Q6State::logon_sent, OverL::bad_checksum);
}
TEST(InboundFrameDispositionsQ6, LogonSent_OverLBodyLengthAtAResyncCandidateCloses) {
    run_q6(Q6State::logon_sent, OverL::body_length_at_candidate);
}
TEST(InboundFrameDispositionsQ6, Disconnected_FrameOfLPlusOneCloses) {
    run_q6(Q6State::disconnected, OverL::frame);
}
TEST(InboundFrameDispositionsQ6, Disconnected_FrameOfLPlusOneWithABadCheckSumCloses) {
    run_q6(Q6State::disconnected, OverL::bad_checksum);
}
TEST(InboundFrameDispositionsQ6, Disconnected_OverLBodyLengthAtAResyncCandidateCloses) {
    run_q6(Q6State::disconnected, OverL::body_length_at_candidate);
}

// The two scan-fault shapes 092 Rejects in Active, carried by a frame over L: the frame
// is refused at framing, so no Reject is sent and no callback runs. These replace
// unparseable_frame_disposition_test.cpp's MaxMessageSize_OversizedFaulty_* controls,
// which fed the frame below the Framer to 070's deleted session-level check.
TEST(InboundFrameDispositionsQ6, Active_FrameOfLPlusOneCarryingAMalformedTagCloses) {
    run_q6(Q6State::active, OverL::malformed_tag);
}
TEST(InboundFrameDispositionsQ6, Active_FrameOfLPlusOneCarryingALengthDataMismatchCloses) {
    run_q6(Q6State::active, OverL::length_data_mismatch);
}

// Control: a frame of exactly L, in Active with the same 383, is delivered.
TEST(InboundFrameDispositionsQ6, Control_FrameOfExactlyLIsDelivered) {
    LargeCell c{kQ6Limit};
    std::string const f =
        c.up ? c.rig.msg_of_size("D", 2, {}, kQ6Limit, "58", false) : std::string{};
    bool const d = !f.empty() && c.rig.deliver(f);
    bool const delivered = d && c.rig.run_until([&] { return c.app->from_app == 1; });
    bool const read_ended = c.rig.peer.read_ended;
    auto const st = c.rig.state();
    c.rig.stop();

    ASSERT_TRUE(c.up && d) << "setup";
    EXPECT_TRUE(delivered);
    EXPECT_FALSE(read_ended);
    EXPECT_EQ(st, fsm_state::Active);
}

// ── Q-33 (T060): lazy reads at headroom exhaustion inside a callback ────────
//
// A FIX 4.4 NewOrderSingle of exactly L = 65536 bytes: a NoPartyIDs(453) group whose
// slices outgrow the callback headroom, then "2=<SOH>" fields (tag 2 is not a
// NewOrderSingle field, so each is an unknown field) to fill the frame and the
// overlay. Inside fromApp the callback reads group_slices(453), then unknown_fields().
// The assertions branch at runtime on the library's own condition, the parse arena's
// upstream (fixpp::detail::arena_upstream()); no arm is skipped:
//   - null upstream: each read reports its exhaustion (an empty span, an empty view);
//   - forwarding upstream (MSVC debug): each read succeeds and the spill is recorded.
// Either way the session stays Active and processes the next frame. The C arms are
// T091's (Phase 8); the C cursor shells are L-17 (fixpp#541).
//
// The frame's density is chosen for B(L): on the base, whose callback reads draw on a
// 16 KiB stack arena, the frame does not parse at all, so the base run uses a scratch
// variant sized for that arena.
constexpr std::size_t kQ33PartyInstances = 2000;

std::string q33_party_group() {
    std::string g = "453=" + std::to_string(kQ33PartyInstances) + "\x01";
    for (std::size_t i = 0; i < kQ33PartyInstances; ++i) {
        g += "448=P\x01"
             "447=D\x01"
             "452=1\x01";
    }
    return g;
}

// The Q-33 frame, a FIX 4.4 NewOrderSingle of exactly `size` bytes.
std::string q33_frame(plain_rig::Rig const& rig, std::uint32_t seq, std::size_t size) {
    return rig.msg_of_size("D", seq,
                           "11=ORD1\x01"
                           "21=1\x01"
                           "55=X\x01"
                           "54=1\x01"
                           "60=20240101-00:00:00.000\x01"
                           "40=1\x01" +
                               q33_party_group(),
                           size, "2", true);
}

bool q33_null_upstream() {
    return fixpp::detail::arena_upstream() == std::pmr::null_memory_resource();
}

// Delivers the Q-33 frame to a FIX 4.4 acceptor whose fromApp runs `read`, then a
// Heartbeat, and reports what the cell asserts on.
struct Q33Run {
    bool up = false;
    bool delivered = false;
    bool next = false;
    fsm_state state{};
    std::uint64_t spills = 0;
};

template <class Read>
Q33Run run_q33(Read read) {
    Q33Run r;
    LargeCell c{std::nullopt, fixpp::test_support::make_fix44_dictionary(), "FIX.4.4"};
    c.app->on_app = read;
    std::string const f = c.up ? q33_frame(c.rig, 2, 65536U) : std::string{};
    bool const d = !f.empty() && c.rig.deliver(f);
    r.up = c.up && d;
    r.delivered = d && c.rig.run_until([&] { return c.app->from_app == 1; });
    r.next = r.delivered && c.rig.deliver(c.rig.heartbeat(3)) &&
             c.rig.run_until([&] { return c.next_in() == 4U; });
    auto const s = c.rig.session();
    r.spills = s ? session_test_access::parse_spills(*s) : 0U;
    r.state = c.rig.state();
    c.rig.stop();
    return r;
}

// group_slices(): an empty span where the upstream is null; on the forwarding lane the
// read succeeds and the spill is recorded. A regression guard on the base, pinning
// today's report.
TEST(InboundFrameDispositionsQ33, GroupSlicesAtHeadroomExhaustion) {
    std::size_t slices = ~std::size_t{0};
    auto const r = run_q33([&](auto const& mv) { slices = mv.offsets().group_slices(453).size(); });
    ASSERT_TRUE(r.up) << "setup";
    ASSERT_TRUE(r.delivered) << "the frame reaches fromApp";
    EXPECT_TRUE(r.next) << "the session processes the next frame";
    EXPECT_EQ(r.state, fsm_state::Active);
    if (q33_null_upstream()) {
        EXPECT_EQ(slices, 0U) << "group_slices() reports exhaustion as an empty span";
    } else {
        EXPECT_EQ(slices, kQ33PartyInstances) << "the read succeeds from the heap";
        EXPECT_GT(r.spills, 0U) << "and the spill witness records the spill";
    }
}

// unknown_fields(), after group_slices() has spent what it could, under EXPECT_EXIT:
// a terminate (fixpp#540) is a recorded failure of this cell, not a crashed binary.
// Exit codes: 0 = the lane's expected outcome; 1 = the unknown-field view was not the
// lane's; 2 = the session did not carry on; 3 = setup failed; 4 = no spill recorded on
// the forwarding lane.
[[noreturn]] void unknown_fields_at_headroom_exhaustion() {
    std::size_t unknown = ~std::size_t{0};
    auto const r = run_q33([&](auto const& mv) {
        (void)mv.offsets().group_slices(453);
        std::size_t n = 0;
        auto const uf = mv.unknown_fields();
        for (auto it = uf.begin(); !(it == uf.end()); ++it) ++n;
        unknown = n;
    });
    if (!r.up || !r.delivered) std::exit(3);
    if (!r.next || r.state != fsm_state::Active) std::exit(2);
    if (q33_null_upstream()) std::exit(unknown == 0U ? 0 : 1);
    if (unknown == 0U) std::exit(1);
    std::exit(r.spills > 0U ? 0 : 4);
}

TEST(InboundFrameDispositionsQ33, UnknownFieldsAtHeadroomExhaustion) {
    EXPECT_EXIT(unknown_fields_at_headroom_exhaustion(), ::testing::ExitedWithCode(0), "");
}

// ── C-3 I-3 (T063): an inbound parse never nests inside another ─────────────
//
// One parse buffer per session is safe because no inbound parse runs inside another:
// callbacks are synchronous, and callback_dispatch_scope asserts that no second
// callback enters while one runs. A fromApp that starts the session's inbound path
// again on the running executor (co_spawn dispatches inline there) re-enters
// parse_and_dispatch_ and trips that assertion. The nested frame is the next expected
// one, so it is in sequence and reaches the parse. Debug builds only: NDEBUG compiles
// the assertion out.
#ifndef NDEBUG
// Exit codes: 2 = setup failed; 0 = the nested parse did not trip the assertion.
[[noreturn]] void reenter_an_inbound_parse_from_from_app() {
    DirectFixture f;
    auto app = std::make_shared<FromAppProbe>();
    f.engine.application = app;
    Session s(f.engine, f.cfg(session_role::acceptor));
    if (!f.open(s) || !f.feed(s, direct_msg("A", 1, kLogonFields))) std::exit(2);
    auto const nested = plain_rig::to_bytes(direct_msg("D", 3));
    app->on_app = [&](auto const& /*mv*/) {
        asio::co_spawn(f.ioc, s.on_inbound_frame(nested), asio::detached);
    };
    (void)f.feed(s, direct_msg("D", 2));
    std::exit(0);
}
#endif

TEST(InboundFrameDispositionsI3, AnInboundParseReenteredFromACallbackDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "callback_dispatch_scope's assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(reenter_an_inbound_parse_from_from_app(), "concurrent session callback entry");
#endif
}

}  // namespace
}  // namespace fixpp::session::test
