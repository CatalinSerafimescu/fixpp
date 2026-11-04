// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_config_check.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-8 (tasks.md T015): data-model E-9's
// check, a literal matrix at both callers, direct open() and Engine::register_session, every S
// shape with its non-validating twin, one clause each, and what a refusal leaves behind.
//
// The evidence file's T020 record says which cells moved at `H0`; M-94 is the one-way fallback's
// mutant.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/log/format_registry.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "support/log_capture.hpp"
#include "support/pump_until_ready.hpp"
#include "support/validation_test_dictionary.hpp"
#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

using fixpp::core::error;
using AV = dd::application_version;

// FIXT11 with one edit, each as the row names it.
DictPtr partial_fixt11() {
    static const DictPtr d = edited_vendored("FIXT11.xml", [](std::string xml) {
        // The admin message removed: TestRequest.
        const auto from = xml.find("<message name='TestRequest'");
        const auto to = xml.find("</message>", from);
        if (from != std::string::npos && to != std::string::npos) {
            xml.erase(from, to + std::string("</message>").size() - from);
        }
        return xml;
    });
    return d;
}

DictPtr fixt11_logon_without_1137() {
    static const DictPtr d = edited_vendored("FIXT11.xml", [](std::string xml) {
        const std::string line = "<field name='DefaultApplVerID' required='Y' />";
        const auto at = xml.find(line);
        if (at != std::string::npos) {
            xml.erase(at, line.size());
        }
        return xml;
    });
    return d;
}

DictPtr fixt11_1137_optional() {
    static const DictPtr d = edited_vendored("FIXT11.xml", [](std::string xml) {
        const std::string from = "name='DefaultApplVerID' required='Y'";
        const auto at = xml.find(from);
        if (at != std::string::npos) {
            xml.replace(at, from.size(), "name='DefaultApplVerID' required='N'");
        }
        return xml;
    });
    return d;
}

struct MatrixCell {
    const char* name;
    DictPtr (*s)();
    std::vector<DictPtr> (*engine)();
    AV default_appl;
    bool validating_refused;  // the literal: a validating session of this shape is refused
};

DictPtr s_t() { return fixt11(); }
DictPtr s_a() { return fix50sp2(); }
DictPtr s_l() { return latest(); }
DictPtr s_44() { return fix44(); }
DictPtr s_44_1137() { return fixpp::test_support::fix44_dictionary_with_1137(); }

std::vector<DictPtr> e_50() { return {fix50sp2()}; }
std::vector<DictPtr> e_latest() { return {latest()}; }
std::vector<DictPtr> e_t_50() { return {fixt11(), fix50sp2()}; }
std::vector<DictPtr> e_t_44() { return {fixt11(), fix44()}; }
std::vector<DictPtr> e_44() { return {fix44()}; }
std::vector<DictPtr> e_partial_50() { return {partial_fixt11(), fix50sp2()}; }
std::vector<DictPtr> e_no1137_50() { return {fixt11_logon_without_1137(), fix50sp2()}; }
std::vector<DictPtr> e_optional_50() { return {fixt11_1137_optional(), fix50sp2()}; }

const MatrixCell kMatrix[] = {
    {"T", s_t, e_50, AV::v50sp2, false},
    {"L", s_l, e_latest, AV::v50sp2, false},
    {"A_with_FIXT11", s_a, e_t_50, AV::v50sp2, false},
    {"A_without_FIXT11", s_a, e_50, AV::v50sp2, true},
    {"FIX44_with_FIXT11", s_44, e_t_44, AV::v44, false},
    {"FIX44_without_FIXT11", s_44, e_44, AV::v44, true},
    {"PartialFIXT11_as_S", partial_fixt11, e_50, AV::v50sp2, true},
    {"A_with_partial_FIXT11", s_a, e_partial_50, AV::v50sp2, true},
    {"FIXT11NoLogon1137_as_S", fixt11_logon_without_1137, e_50, AV::v50sp2, true},
    {"A_with_FIXT11NoLogon1137", s_a, e_no1137_50, AV::v50sp2, true},
    {"Fix44With1137_no_FIXT11", s_44_1137, e_50, AV::v50sp2, true},
    {"Fix44With1137_with_FIXT11", s_44_1137, e_t_50, AV::v50sp2, false},
    {"Optional1137_as_S", fixt11_1137_optional, e_50, AV::v50sp2, false},
    {"A_with_optional1137_transport", s_a, e_optional_50, AV::v50sp2, false},
    {"A_base_unregistered", s_a, e_t_44, AV::v44, false},
};

class W8Matrix : public testing::TestWithParam<MatrixCell> {};

INSTANTIATE_TEST_SUITE_P(W8, W8Matrix, testing::ValuesIn(kMatrix),
                         [](const testing::TestParamInfo<MatrixCell>& i) {
                             return std::string{i.param.name};
                         });

expected_t<void> direct_open(asio::io_context& ioc, Session& s) {
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    if (!pump_ready(ioc, fut, std::chrono::seconds{120})) {
        return std::unexpected(error::invalid_session_config);
    }
    return fut.get();
}

struct DirectRig {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    DirectRig() {
        const auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        const auto stp = fixpp::core::steady_time_point{} + std::chrono::seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }
};

constexpr int kNeverOpened = 0;  // Session::lifecycle::never_opened

TEST_P(W8Matrix, DirectOpen_Validating) {
    const MatrixCell& c = GetParam();
    DirectRig rig;
    auto registry = make_registry(c.engine());
    Session s{rig.engine, plain_fixt_config(rig.ioc, c.s(), true, c.default_appl), registry.get()};
    const auto r = direct_open(rig.ioc, s);
    if (c.validating_refused) {
        ASSERT_FALSE(r.has_value()) << "opened; the row refuses it";
        EXPECT_EQ(r.error(), error::invalid_session_config);
        EXPECT_EQ(session_test_access::lifecycle_value(s), kNeverOpened);
    } else {
        EXPECT_TRUE(r.has_value()) << "refused with " << static_cast<int>(r.error());
    }
}

TEST_P(W8Matrix, DirectOpen_NonValidatingTwinOpens) {
    const MatrixCell& c = GetParam();
    DirectRig rig;
    auto registry = make_registry(c.engine());
    Session s{rig.engine, plain_fixt_config(rig.ioc, c.s(), false, c.default_appl), registry.get()};
    const auto r = direct_open(rig.ioc, s);
    EXPECT_TRUE(r.has_value()) << "refused with " << static_cast<int>(r.error());
}

TEST_P(W8Matrix, Register_Validating) {
    const MatrixCell& c = GetParam();
    EngineProbe probe{c.engine()};
    const auto cfg = plain_fixt_config(probe.ioc, c.s(), true, c.default_appl);
    const auto r = probe.engine->register_session(cfg);
    if (c.validating_refused) {
        ASSERT_FALSE(r.has_value()) << "registered; the row refuses it";
        EXPECT_EQ(r.error(), error::invalid_session_config);
        // No registry entry: the same SessionId, valid this time, is not a duplicate.
        const auto again = probe.engine->register_session(
            plain_fixt_config(probe.ioc, c.s(), false, c.default_appl));
        EXPECT_TRUE(again.has_value()) << "a refused registration left an entry";
    } else {
        EXPECT_TRUE(r.has_value()) << "refused with " << static_cast<int>(r.error());
    }
}

TEST_P(W8Matrix, Register_NonValidatingTwinRegisters) {
    const MatrixCell& c = GetParam();
    EngineProbe probe{c.engine()};
    const auto r =
        probe.engine->register_session(plain_fixt_config(probe.ioc, c.s(), false, c.default_appl));
    EXPECT_TRUE(r.has_value()) << "refused with " << static_cast<int>(r.error());
}

// ── One clause each, at each caller that can reach it ───────────────────────────────────

TEST(W8Clause, NoDefault_DirectOpen) {
    DirectRig rig;
    auto registry = make_registry({fixt11(), fix50sp2()});
    Session s{rig.engine, plain_fixt_config(rig.ioc, fixt11(), true, std::nullopt), registry.get()};
    const auto r = direct_open(rig.ioc, s);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::invalid_session_config);
    EXPECT_EQ(session_test_access::lifecycle_value(s), kNeverOpened);
}

TEST(W8Clause, NoDefault_Register) {
    EngineProbe probe{{fixt11(), fix50sp2()}};
    const auto r =
        probe.engine->register_session(plain_fixt_config(probe.ioc, fixt11(), true, std::nullopt));
    ASSERT_FALSE(r.has_value()) << "registered with no default_appl_ver_id";
    EXPECT_EQ(r.error(), error::invalid_session_config);
}

TEST(W8Clause, SlotMiss_DirectOpen) {
    DirectRig rig;
    auto registry = make_registry({fixt11(), fix50sp2()});
    Session s{rig.engine, plain_fixt_config(rig.ioc, fixt11(), true, AV::v44), registry.get()};
    const auto r = direct_open(rig.ioc, s);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::invalid_session_config);
    EXPECT_EQ(session_test_access::lifecycle_value(s), kNeverOpened);
}

TEST(W8Clause, SlotMiss_Register) {
    EngineProbe probe{{fixt11(), fix50sp2()}};
    const auto r =
        probe.engine->register_session(plain_fixt_config(probe.ioc, fixt11(), true, AV::v44));
    ASSERT_FALSE(r.has_value()) << "registered on a slot miss";
    EXPECT_EQ(r.error(), error::invalid_session_config);
}

TEST(W8Clause, NullRegistry_DirectOpenOnly) {
    DirectRig rig;
    Session s{rig.engine, plain_fixt_config(rig.ioc, fixt11(), true, AV::v50sp2), nullptr};
    const auto r = direct_open(rig.ioc, s);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::invalid_session_config);
    EXPECT_EQ(session_test_access::lifecycle_value(s), kNeverOpened);
}

DictPtr overlay_of_fix44() {
    static const DictPtr d =
        overlay_on("FIX44.xml",
                   "<fix type='FIX' major='4' minor='4' servicepack='0'><fields>"
                   "<field number='5060' name='OtherOverlayTag' type='STRING' />"
                   "</fields></fix>");
    return d;
}

// data-model E-6: a set handed to a session must hold its S. No production path hands one that
// does not, and open() refuses rather than dereference a missing entry.
TEST(W8AdoptedSet, LackingS_Refused) {
    DirectRig rig;
    auto registry = make_registry({fixt11(), fix44()});
    Session s{rig.engine, plain_fixt_config(rig.ioc, overlay_of_fix44(), true, AV::v44),
              registry.get()};
    const auto set =
        detail::fixt_route_set::build(*registry, std::views::empty<const dd::Dictionary*>);
    ASSERT_TRUE(set.has_value());
    session_test_access::adopt_routes(s, *set);
    const auto r = direct_open(rig.ioc, s);
    ASSERT_FALSE(r.has_value()) << "opened on a set with no entry for its S";
    EXPECT_EQ(r.error(), error::invalid_session_config);
    EXPECT_EQ(session_test_access::lifecycle_value(s), kNeverOpened);
}

// ── The one-way fallback: `get` answers default 9 from the FIX Latest slot ──────────────

TEST(W8OneWayFallback, DirectOpen_Opens) {
    DirectRig rig;
    auto registry = make_registry({fixt11(), latest()});
    Session s{rig.engine, plain_fixt_config(rig.ioc, fixt11(), true, AV::v50sp2), registry.get()};
    const auto r = direct_open(rig.ioc, s);
    EXPECT_TRUE(r.has_value());
}

TEST(W8OneWayFallback, Register_Registers) {
    EngineProbe probe{{fixt11(), latest()}};
    const auto r =
        probe.engine->register_session(plain_fixt_config(probe.ioc, fixt11(), true, AV::v50sp2));
    EXPECT_TRUE(r.has_value());
}

// ── A refused registration leaves other entries running ─────────────────────────────────

TEST(W8Registration, RefusalLeavesTheOtherEntry) {
    EngineProbe probe{{fix50sp2()}};
    const auto good = plain_fixt_config(probe.ioc, fixt11(), true, AV::v50sp2, "GOOD");
    ASSERT_TRUE(probe.engine->register_session(good).has_value());
    const auto bad = probe.engine->register_session(
        plain_fixt_config(probe.ioc, fix50sp2(), true, AV::v50sp2, "BAD"));
    EXPECT_FALSE(bad.has_value()) << "registered shape A with no transport dictionary";
    // The first entry is still there: registering it again is the duplicate-id refusal.
    const auto dup = probe.engine->register_session(good);
    ASSERT_FALSE(dup.has_value());
    EXPECT_NE(dup.error(), error::invalid_session_config);
}

// ── The Log line ────────────────────────────────────────────────────────────────────────

// A non-validating bad config on an Engine with a logger set: registration takes it, the role
// loop's open() refuses it, and that is logged, not returned (owner item 5). The record's text is
// the format string of src/log/format_registry.cpp, spelled out here and not read from it, with
// its argument computed independently, so a string left out of the registry fails (M-56).
constexpr std::uint32_t kRoleLoopOpenFailed =
    FIXPP_FORMAT_ID("session role loop ended: Session::open() failed: error={}");

std::string role_loop_open_failed_text() {
    return "session role loop ended: Session::open() failed: error=" +
           std::to_string(static_cast<std::uint64_t>(error::invalid_session_config));
}

// A role loop's record, as this witness reads it: exactly one error-level record in the run, and
// it is that loop's, with that text.
void expect_one_role_loop_record(fixpp::test_support::LogCapture& log) {
    (void)log.logger->shutdown();
    std::vector<fixpp::log::Record> errors;
    for (auto const& rec : log.sink->records()) {
        if (rec.level >= fixpp::log::Level::error) {
            errors.push_back(rec);
        }
    }
    ASSERT_EQ(errors.size(), 1U) << "the role loop's failed open() must leave one error record";
    EXPECT_EQ(errors[0].format_id, kRoleLoopOpenFailed);
    EXPECT_EQ(fixpp::log::detail::format_record(errors[0]), role_loop_open_failed_text());
}

// A connect loop opens first thing, so no peer is needed.
TEST(W8LogLine, NonValidatingBadConfig_ConnectLoopLogsTheRefusal) {
    fixpp::test_support::LogCapture log;
    {
        EngineProbe probe{{fixt11()}, log.logger};
        auto cfg = plain_fixt_config(probe.ioc, fixt11(), false, std::nullopt);
        cfg.role = session_role::initiator;
        EXPECT_TRUE(probe.engine->register_session(std::move(cfg)).has_value());
        EXPECT_TRUE(probe.engine->start().has_value());
        for (int i = 0; i < 2000; ++i) {
            probe.ioc.restart();
            probe.ioc.poll();
        }
    }
    expect_one_role_loop_record(log);
}

// An accept loop opens once a connection has sent a first frame that names this session, and
// closes the connection when open() refuses, which is what the peer's read waits for.
TEST(W8LogLine, NonValidatingBadConfig_AcceptLoopLogsTheRefusal) {
    fixpp::test_support::LogCapture log;
    bool peer_saw_the_close = false;
    {
        EngineProbe probe{{fixt11()}, log.logger};
        auto cfg = plain_fixt_config(probe.ioc, fixt11(), false, std::nullopt);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        cfg.security_profile = SecurityProfile{SecurityProfile::kind::insecure_plain_tcp};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
        const SessionId id = SessionId::from_config(cfg);
        EXPECT_TRUE(probe.engine->register_session(std::move(cfg)).has_value());
        EXPECT_TRUE(probe.engine->start().has_value());
        fixpp::test_support::plain_rig::Peer peer{probe.ioc};
        const bool bound = fixpp::test_support::pump_until(
            probe.ioc, [&] { return probe.engine->acceptor_bound_endpoint(id).port != 0; },
            std::chrono::seconds{30}, std::chrono::milliseconds{1}, nullptr);
        EXPECT_TRUE(bound);
        if (bound) {
            const auto port = probe.engine->acceptor_bound_endpoint(id).port;
            peer.sock.async_connect(
                asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
                [&peer](std::error_code const& ec) {
                    if (!ec) {
                        peer.start_reading();
                        peer.send(logon_frame(Ctx{"TW", "ISLD", 1}, {}));
                    }
                });
            peer_saw_the_close = fixpp::test_support::pump_until(
                probe.ioc, [&] { return peer.read_ended; }, std::chrono::seconds{30},
                std::chrono::milliseconds{1}, nullptr);
        }
        peer.close();
    }
    EXPECT_TRUE(peer_saw_the_close) << "the accept loop never reached its open()";
    expect_one_role_loop_record(log);
}

}  // namespace
}  // namespace fixpp::session::test099
