// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_070_xmlnonfix_passthrough_test.cpp
//
// 070-fix44-closeout US4 (A-034) — XMLnonFIX(35=n) opaque passthrough.
//
// Discriminating witness (FR-009/FR-010/FR-011, tasks.md T020):
//   (a) an inbound 35=n carrying XmlDataLen(212)/XmlData(213) whose XML payload
//       contains raw SOH (0x01) bytes is delivered to Application::fromApp (NOT
//       fromAdmin, NOT rejected), and tag 213 reads back BYTE-EXACT incl. the
//       embedded SOH (the length-delimited data field is parsed SOH-safely).
//   (b) with opt-in dictionary validation ENABLED, a well-formed 35=n is STILL
//       accepted (fromApp, not rejected) — using the real shipped FIX44 dictionary
//       where 212/213 are header fields valid for the empty-bodied XMLnonFIX.
//
// No production source change: 35=n already routes to fromApp (msgtype_classifier
// admin allow-list excludes "n") and the 212/213 pair already parses SOH-safe.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdio>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fstream>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// Where a site in this file is migrated it uses `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The site label passed to `run_window_then_ready` is the FORCING SEAM: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

namespace fixpp::session::test {
namespace {

using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::SessionId;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

// Load the REAL shipped FIX44 dictionary (has XMLnonFIX + 212/213 as header fields).
std::shared_ptr<const fixpp::dict::Dictionary> load_fix44_dictionary() {
    const std::string path = std::string(FIXPP_TEST_SOURCE_DIR) + "/dictionaries/FIX44.xml";
    std::ifstream in(path, std::ios::binary);
    std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Oversized initial arena; monotonic_buffer_resource grows via the default
    // upstream if exceeded. Co-owned by the shared_ptr deleter.
    constexpr std::size_t kBufSize = 4u * 1024u * 1024u;
    auto buf = std::make_unique<std::vector<std::byte>>(kBufSize);
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    fixpp::dict::Dictionary d = fixpp::dict::XmlLoader{}.load_from_string(xml, mr);
    auto* raw_dict = new fixpp::dict::Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](const fixpp::dict::Dictionary* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};
}

// Application that captures fromApp/fromAdmin and, in fromApp, the exact bytes of
// tag 213 (XmlData).
class XmlCaptureApp : public Application {
public:
    int from_app_calls = 0;
    int from_admin_calls = 0;
    std::string captured_213;
    bool got_213 = false;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& msg,
                             const SessionId& /*id*/) override {
        ++from_app_calls;
        if (auto fv = msg.get(213); fv) {
            auto b = fv->bytes();
            captured_213.assign(reinterpret_cast<const char*>(b.data()), b.size());
            got_213 = true;
        }
        return {};
    }
    expected_t<void> fromAdmin(const MessageView<access_mode::Index>& /*msg*/,
                               const SessionId& /*id*/) override {
        ++from_admin_calls;
        return {};
    }
};

std::vector<std::byte> to_frame(const std::string& full) {
    std::vector<std::byte> f;
    f.reserve(full.size());
    for (char c : full) {
        f.push_back(static_cast<std::byte>(c));
    }
    return f;
}

std::string finalize(std::string body) {
    std::string full = "8=FIX.4.4\x01";
    full += "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    return full;
}

std::vector<std::byte> make_logon_frame() {
    std::string body = "35=A\x01" "34=1\x01" "49=TW\x01" "52=20240101-00:00:00.000\x01"
                       "56=ISLD\x01" "98=0\x01" "108=30\x01";
    return to_frame(finalize(body));
}

// 35=n with 212=<len>/213=<xml> where xml contains embedded SOH bytes.
std::vector<std::byte> make_xmlnonfix_frame(std::uint32_t seq, std::string_view xml) {
    std::string body = "35=n\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01" "52=20240101-00:00:00.000\x01" "56=ISLD\x01";
    body += "212=" + std::to_string(xml.size()) + "\x01";
    body += "213=";
    body.append(xml.data(), xml.size());  // raw bytes incl. embedded SOH
    body += "\x01";
    return to_frame(finalize(body));
}

struct Fixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    std::vector<std::vector<std::byte>> captured_frames;

    Fixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(std::shared_ptr<const fixpp::dict::Dictionary> dict, bool validate) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = std::chrono::seconds{0};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = std::move(dict);
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = validate;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    void open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value());
        auto logon = make_logon_frame();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    void feed(Session& s, const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "Fixture::feed")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "Fixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "Fixture::feed";
            return;
        }
        (void)fut.get();
    }

    bool any_reject_emitted() const {
        for (const auto& f : captured_frames) {
            std::string w(reinterpret_cast<const char*>(f.data()), f.size());
            if (w.find("35=3\x01") != std::string::npos || w.find("35=j\x01") != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

// XML payload carrying two embedded SOH (0x01) bytes (17 bytes total).
constexpr std::string_view kXml = "<x>\x01<y>1</y>\x01</x>";

// (a) validation OFF (shipped default): 35=n → fromApp, byte-exact 213, no reject.
TEST(XmlNonFixPassthrough, DeliveredToFromAppByteExact) {
    auto app = std::make_shared<XmlCaptureApp>();
    Fixture f;
    f.engine.application = app;
    auto cfg = f.make_cfg(load_fix44_dictionary(), /*validate=*/false);
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    f.feed(sess, make_xmlnonfix_frame(2, kXml));

    EXPECT_EQ(app->from_app_calls, 1) << "35=n must be delivered on fromApp";
    EXPECT_EQ(app->from_admin_calls, 0) << "35=n must NOT be delivered on fromAdmin";
    EXPECT_FALSE(f.any_reject_emitted()) << "35=n must not be rejected";
    ASSERT_TRUE(app->got_213) << "tag 213 must be readable in fromApp";
    EXPECT_EQ(app->captured_213, std::string(kXml)) << "213 must be byte-exact incl. embedded SOH";
    EXPECT_EQ(app->captured_213.size(), kXml.size());
}

// (b) FR-011: validation ENABLED with the real FIX44 dict ⇒ still accepted.
TEST(XmlNonFixPassthrough, AcceptedUnderInboundValidation) {
    auto app = std::make_shared<XmlCaptureApp>();
    Fixture f;
    f.engine.application = app;
    auto cfg = f.make_cfg(load_fix44_dictionary(), /*validate=*/true);
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    f.feed(sess, make_xmlnonfix_frame(2, kXml));

    EXPECT_EQ(app->from_app_calls, 1) << "35=n must reach fromApp even with validation on (FR-011)";
    EXPECT_FALSE(f.any_reject_emitted()) << "validator must accept a well-formed 35=n (FR-011)";
}

}  // namespace
}  // namespace fixpp::session::test
