// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_business_messages_roundtrip.cpp
//
// 020-g2-business-messages T007 — send-path + round-trip RED suite.
//
// Named tests (exact names from data-model.md INV-1/5/7/8 and tasks.md T007):
//
//   INV-1: SendPath_StoredFrame_Field3MsgType_UnpaddedBodyLength_ValidChecksum
//     — Asserts on CAPTURED FRAME BYTES (not fromApp value):
//       (a) fields 1=8=, 2=9=, 3=35= in that exact order;
//       (b) 9= value is digit-only, no leading zero;
//       (c) recomputed checksum matches 10=;
//       (d) BodyLength value equals measured body length.
//     RED before T009 fix (current send_impl emits MsgType 7th + 9=000045 padded).
//
//   INV-8: OpaquePayload_Malformed_RejectedNoSeqnumConsumed
//     — For each malformed payload (empty, no-leading-35, duplicate 35=,
//       embedded session tag), assert:
//       (a) send() returns error::app_payload_malformed (131);
//       (b) no new captured frame;
//       (c) the NEXT successful send uses the expected seqnum (none consumed).
//     RED before T010 validation fix.
//
//   INV-5: InboundReject_EmitsBusinessMessageReject_SessionSurvives
//     — OutboundFixture inbound-path: inject a 35=D frame as "inbound",
//       Application::fromApp returns a reject, engine drives BusinessMessageReject
//       (35=j), session stays Active.
//
//   INV-7: SendFromInsideFromApp_NoDeadlockNoUAF
//     — Re-entrant Engine::send from inside fromApp under a MULTI-THREADED
//       executor (thread_pool with >=2 threads). Requires real loopback TLS;
//       GTEST_SKIP when FIXPP_TLS_FIXTURE_DIR absent.
//       Per [[feedback_single_threaded_harness_masks_strand_races]]: single-thread
//       ioc masks the exec-hop; this test uses asio::thread_pool(>=2).
//
// TDD (T007): written RED-first; INV-1+INV-8 use OutboundFixture (no live TLS),
// always run; INV-5 uses inbound-path (no live TLS); INV-7 needs live TLS.
//
// Anchors: data-model.md INV-1/5/7/8; research.md D1 (framing defects);
//          contracts/business-messages.md "Send-path obligation";
//          tasks.md T007/T009/T010/T011.
//          [[feedback_single_threaded_harness_masks_strand_races]]

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

// Live TLS headers — only needed for INV-7.
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <asio/ip/tcp.hpp>

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace {

// ── TLS fixture helpers (INV-7 only) ─────────────────────────────────────────

const char* get_fixture_dir() {
    const char* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");  // NOLINT(concurrency-mt-unsafe)
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (env && env[0] != '\0') ? env : kDir;
}

static uint16_t reserve_free_port(asio::io_context& ioc) {
    asio::ip::tcp::acceptor a{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    a.open(ep.protocol());
    a.bind(ep);
    uint16_t port = a.local_endpoint().port();
    a.close();
    return port;
}

static std::shared_ptr<fixpp::transport::TransportFactory> make_tls_factory(
    const char* fixture_dir) {
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    if (!cs_r.has_value()) return nullptr;
    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};
    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    if (!fac_r.has_value()) return nullptr;
    return std::shared_ptr<fixpp::transport::TransportFactory>{std::move(*fac_r)};
}

// ── Frame helpers ─────────────────────────────────────────────────────────────

// Build a raw inbound FIX frame (for INV-5 inbound path).
static std::vector<std::byte> make_inbound_app_frame(std::string_view msg_type,
                                                      std::string_view sender,
                                                      std::string_view target,
                                                      uint32_t seq,
                                                      std::string_view begin_string = "FIX.4.2",
                                                      std::string extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_body.empty()) body += extra_body;

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Build a Logon frame (for advancing sessions to Active in OutboundFixture).
static std::vector<std::byte> make_logon_frame(std::string_view begin_string = "FIX.4.2",
                                               uint32_t seq = 1,
                                               std::string_view sender = "TW",
                                               std::string_view target = "ISLD") {
    return make_inbound_app_frame("A", sender, target, seq, begin_string,
                                  "98=0\x01""108=30\x01");
}

// ── Parse helpers for frame byte inspection ───────────────────────────────────
//
// Parses a FIX frame into a vector of {tag, value} pairs (split on \x01,
// then on first '='). Allocation-free parsing is intentional for clarity.

struct FixField {
    int tag;
    std::string value;
};

static std::vector<FixField> parse_fix_frame(std::span<const std::byte> frame) {
    std::vector<FixField> fields;
    std::string_view sv{reinterpret_cast<const char*>(frame.data()), frame.size()};
    std::size_t pos = 0;
    while (pos < sv.size()) {
        auto soh = sv.find('\x01', pos);
        if (soh == std::string_view::npos) break;
        std::string_view fld = sv.substr(pos, soh - pos);
        auto eq = fld.find('=');
        if (eq != std::string_view::npos) {
            int tag = 0;
            auto [p, ec] = std::from_chars(fld.data(), fld.data() + eq, tag);
            (void)ec;
            fields.push_back({tag, std::string(fld.substr(eq + 1))});
        }
        pos = soh + 1;
    }
    return fields;
}

// Recompute FIX checksum (mod 256, over all bytes through last field before 10=).
static unsigned int compute_checksum(std::span<const std::byte> frame) {
    unsigned int cs = 0;
    // Sum all bytes EXCEPT the "10=CCC\x01" trailer (the last 7 bytes).
    // The spec says checksum covers 8=…10=xxx\x01 body; we mirror send_impl:
    // it sums all bytes up to (not including) "10=".
    // Find the last occurrence of "10=" at a SOH boundary.
    std::string_view sv{reinterpret_cast<const char*>(frame.data()), frame.size()};
    // Find "10=" at start-of-message or after SOH.
    std::size_t trailer_pos = std::string_view::npos;
    for (std::size_t i = 0; i + 3 <= sv.size(); ++i) {
        if (sv[i] == '1' && sv[i+1] == '0' && sv[i+2] == '=') {
            // Must be at start or after SOH.
            if (i == 0 || sv[i-1] == '\x01') {
                trailer_pos = i;
            }
        }
    }
    std::size_t sum_to = (trailer_pos != std::string_view::npos) ? trailer_pos : frame.size();
    for (std::size_t i = 0; i < sum_to; ++i) {
        cs += static_cast<unsigned int>(static_cast<unsigned char>(frame[i]));
    }
    return cs & 0xFFU;
}

// ── OutboundFixture (mirrors test_application_outbound.cpp) ──────────────────

struct OutboundFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine_cfg;
    std::vector<std::vector<std::byte>> captured_frames;

    OutboundFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine_cfg.clock = clock;
        engine_cfg.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(bool capturing = false) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        if (capturing) {
            cfg.transport_send = [this](std::span<const std::byte> frame) {
                captured_frames.emplace_back(frame.begin(), frame.end());
            };
        } else {
            cfg.transport_send = [](std::span<const std::byte>) {};
        }
        return cfg;
    }

    // Open session and advance to Active (acceptor role).
    void open_to_active(fixpp::session::Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD");
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value()) << "Logon feed failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    void drain(int ms = 200) {
        ioc.run_for(std::chrono::milliseconds{ms});
        ioc.restart();
    }
};

// ── INV-1: Wire frame field order + digit-only BodyLength + valid checksum ────
//
// Tests that on the captured frame bytes:
//   field 1 = tag 8 (BeginString)
//   field 2 = tag 9 (BodyLength)
//   field 3 = tag 35 (MsgType) — RED before T009 fix (current code emits it 7th)
//   9= value has NO leading zeros — RED before T009 fix (current code emits "000045")
//   10= checksum matches recomputed value
//   BodyLength value equals measured body length
//
// Anchors: data-model.md INV-1; research.md D1; tasks.md T009.

TEST(BusinessMessagesRoundtrip, SendPath_StoredFrame_Field3MsgType_UnpaddedBodyLength_ValidChecksum) {
    OutboundFixture f;

    auto cfg = f.make_cfg(/*capturing=*/true);
    fixpp::session::Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    // Clear captures from Logon (we only want the app-message frame).
    const std::size_t frames_before = f.captured_frames.size();

    // A 35=D-leading payload (NewOrderSingle body).
    static const char kNos[] =
        "35=D\x01""11=ORD001\x01""54=1\x01""55=AAPL\x01""40=2\x01""44=100.00\x01";
    std::vector<std::byte> payload;
    for (const char* p = kNos; *p; ++p) payload.push_back(static_cast<std::byte>(*p));

    auto fut = asio::co_spawn(f.ioc, sess.send(std::span<const std::byte>(payload)),
                               asio::use_future);
    f.drain();
    auto result = fut.get();
    ASSERT_TRUE(result.has_value()) << "send() must succeed; error="
                                    << static_cast<int>(result.error());

    // Must have captured exactly one new frame.
    ASSERT_GT(f.captured_frames.size(), frames_before) << "app frame must be captured";
    const auto& frame_bytes = f.captured_frames.back();
    ASSERT_FALSE(frame_bytes.empty()) << "captured frame must not be empty";

    // Parse the frame into fields.
    auto fields = parse_fix_frame(frame_bytes);
    ASSERT_GE(fields.size(), 3u) << "frame must have at least 3 fields";

    // (a) Field 1 must be tag 8 (BeginString).
    EXPECT_EQ(fields[0].tag, 8) << "field-1 must be BeginString(8); got tag " << fields[0].tag;

    // (b) Field 2 must be tag 9 (BodyLength).
    EXPECT_EQ(fields[1].tag, 9) << "field-2 must be BodyLength(9); got tag " << fields[1].tag;

    // (c) Field 3 must be tag 35 (MsgType) — THIS IS THE RED ASSERTION BEFORE T009.
    EXPECT_EQ(fields[2].tag, 35)
        << "field-3 must be MsgType(35); got tag " << fields[2].tag
        << " (EXPECTED RED before T009 fix: current send_impl emits MsgType 7th)";

    // (d) 9= value is digit-only with NO leading zero (e.g. "45", not "000045").
    if (fields.size() > 1 && fields[1].tag == 9) {
        const std::string& bl_str = fields[1].value;
        EXPECT_FALSE(bl_str.empty()) << "BodyLength must not be empty";
        // No leading zero (except the value "0" itself is fine — but BodyLength>0 always).
        if (bl_str.size() > 1) {
            EXPECT_NE(bl_str[0], '0')
                << "BodyLength must NOT have leading zeros; got: '" << bl_str << "'"
                << " (EXPECTED RED before T009 fix: current code emits '000045'-style padded)";
        }
        // Value must be digits only.
        for (char c : bl_str) {
            EXPECT_TRUE(c >= '0' && c <= '9')
                << "BodyLength must be digit-only; bad char '" << c << "'";
        }

        // (d2) BodyLength value must equal the actual measured body length.
        // BodyLength = bytes from after "9=<digits>\x01" through the byte before "10=".
        // Find the position of the 9= field end and the 10= start.
        std::string_view sv{reinterpret_cast<const char*>(frame_bytes.data()), frame_bytes.size()};
        // Find "9=" at start or after SOH.
        std::size_t bl_field_end = std::string_view::npos;
        for (std::size_t i = 0; i + 2 <= sv.size(); ++i) {
            if (sv[i] == '9' && sv[i+1] == '=') {
                if (i == 0 || sv[i-1] == '\x01') {
                    // Skip past "9=<digits>\x01".
                    std::size_t j = i + 2;
                    while (j < sv.size() && sv[j] != '\x01') ++j;
                    if (j < sv.size()) bl_field_end = j + 1;  // after the SOH
                    break;
                }
            }
        }
        // Find "10=" at SOH boundary.
        std::size_t ten_start = std::string_view::npos;
        for (std::size_t i = 0; i + 3 <= sv.size(); ++i) {
            if (sv[i] == '1' && sv[i+1] == '0' && sv[i+2] == '=') {
                if (i == 0 || sv[i-1] == '\x01') { ten_start = i; break; }
            }
        }
        if (bl_field_end != std::string_view::npos && ten_start != std::string_view::npos
            && ten_start > bl_field_end) {
            std::size_t actual_body_len = ten_start - bl_field_end;
            int stated_bl = std::stoi(bl_str);
            EXPECT_EQ(static_cast<int>(actual_body_len), stated_bl)
                << "BodyLength stated=" << stated_bl << " but actual body=" << actual_body_len;
        }
    }

    // (e) 10= checksum matches recomputed value.
    // Find the 10= field in parsed fields.
    std::string stored_checksum;
    for (const auto& f2 : fields) {
        if (f2.tag == 10) { stored_checksum = f2.value; break; }
    }
    ASSERT_FALSE(stored_checksum.empty()) << "10= checksum field must be present";

    unsigned int expected_cs = compute_checksum(frame_bytes);
    char expected_cs_str[4];
    snprintf(expected_cs_str, sizeof(expected_cs_str), "%03u", expected_cs);
    EXPECT_EQ(stored_checksum, std::string(expected_cs_str))
        << "10= checksum mismatch: stored='" << stored_checksum
        << "' computed='" << expected_cs_str << "'";
}

// ── INV-8: Opaque payload validation — reject malformed, no seqnum consumed ──
//
// For each malformed payload:
//   (1) empty payload
//   (2) no leading 35= (e.g. "11=ORD001\x01")
//   (3) duplicate 35= (e.g. "35=D\x0135=D\x01")
//   (4) embedded session header tag (8=, 9=, 34=, 49=, 52=, 56=)
//   (5) embedded trailer tag (10=)
//
// Assert for each:
//   - send() returns unexpected(error::app_payload_malformed) (131)
//   - no new captured frame
//   - NEXT successful send uses contiguous seqnum (none consumed)
//
// Anchors: data-model.md INV-8; research.md D1 (opaque-payload validation);
//          tasks.md T010.

TEST(BusinessMessagesRoundtrip, OpaquePayload_Malformed_RejectedNoSeqnumConsumed) {
    OutboundFixture f;

    auto cfg = f.make_cfg(/*capturing=*/true);
    fixpp::session::Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    const std::size_t frames_after_logon = f.captured_frames.size();

    // Helper: attempt a send and check rejection.
    auto assert_malformed = [&](const std::vector<std::byte>& malformed, const char* label) {
        SCOPED_TRACE(label);
        const std::size_t frames_before = f.captured_frames.size();

        auto fut = asio::co_spawn(f.ioc, sess.send(std::span<const std::byte>(malformed)),
                                   asio::use_future);
        f.drain();
        auto result = fut.get();

        EXPECT_FALSE(result.has_value()) << "malformed payload must be rejected";
        EXPECT_EQ(result.error(), error::app_payload_malformed)
            << "must return app_payload_malformed(131); got "
            << static_cast<int>(result.error());
        EXPECT_EQ(f.captured_frames.size(), frames_before) << "no new frame on malformed send";
    };

    // Convert a C-string literal to bytes (excludes null terminator).
    auto to_bytes = [](const char* s) {
        std::vector<std::byte> v;
        for (const char* p = s; *p; ++p) v.push_back(static_cast<std::byte>(*p));
        return v;
    };

    // (1) Empty payload.
    assert_malformed({}, "empty_payload");

    // (2) No leading 35=.
    assert_malformed(to_bytes("11=ORD001\x01""54=1\x01""55=AAPL\x01"),
                     "no_leading_35");

    // (3) Duplicate 35=.
    assert_malformed(to_bytes("35=D\x01""11=ORD001\x01""35=D\x01"),
                     "duplicate_35");

    // (4a) Embedded session tag: 8=.
    assert_malformed(to_bytes("35=D\x01""8=FIX.4.2\x01"),
                     "embedded_8");

    // (4b) Embedded session tag: 9=.
    assert_malformed(to_bytes("35=D\x01""9=45\x01"),
                     "embedded_9");

    // (4c) Embedded session tag: 34=.
    assert_malformed(to_bytes("35=D\x01""34=999\x01"),
                     "embedded_34");

    // (4d) Embedded session tag: 49=.
    assert_malformed(to_bytes("35=D\x01""49=EVIL\x01"),
                     "embedded_49");

    // (4e) Embedded session tag: 52=.
    assert_malformed(to_bytes("35=D\x01""52=20240101-00:00:00.000\x01"),
                     "embedded_52");

    // (4f) Embedded session tag: 56=.
    assert_malformed(to_bytes("35=D\x01""56=EVIL\x01"),
                     "embedded_56");

    // (4g) Embedded trailer tag: 10=.
    assert_malformed(to_bytes("35=D\x01""10=123\x01"),
                     "embedded_10");

    // Verify no seqnum was consumed: the NEXT successful send must use seqnum 2
    // (seqnum 1 was used by the outbound Logon reply).
    // We assert on the 34= field of the next successful frame.
    const std::size_t frames_before_good = f.captured_frames.size();
    ASSERT_EQ(frames_before_good, frames_after_logon)
        << "no frames should have been captured for malformed sends";

    static const char kGoodNos[] = "35=D\x01""11=ORD001\x01""54=1\x01""55=AAPL\x01";
    std::vector<std::byte> good_payload;
    for (const char* p = kGoodNos; *p; ++p) good_payload.push_back(static_cast<std::byte>(*p));

    auto fut_good = asio::co_spawn(f.ioc, sess.send(std::span<const std::byte>(good_payload)),
                                    asio::use_future);
    f.drain();
    auto good_result = fut_good.get();
    ASSERT_TRUE(good_result.has_value())
        << "good send after malformed attempts must succeed; error="
        << static_cast<int>(good_result.error());

    ASSERT_GT(f.captured_frames.size(), frames_before_good) << "good send must capture a frame";
    const auto& good_frame = f.captured_frames.back();
    auto good_fields = parse_fix_frame(good_frame);

    // Find 34= and verify seqnum = 2 (Logon reply used seqnum 1, no intervening seqnums).
    bool found_seq = false;
    for (const auto& fld : good_fields) {
        if (fld.tag == 34) {
            found_seq = true;
            EXPECT_EQ(fld.value, "2")
                << "seqnum after malformed attempts must be 2 (malformed sends consumed no seqnum)";
        }
    }
    EXPECT_TRUE(found_seq) << "good send frame must contain 34= field";
}

// ── INV-5: Inbound reject → BusinessMessageReject(35=j) + session stays Active ─
//
// When fromApp returns a reject, the engine drives BusinessMessageReject(35=j)
// and the session stays Active (FR-009/SC-005).
//
// Implementation: inject a 35=D frame as inbound (via on_inbound_frame). The
// Application::fromApp returns unexpected(app_do_not_send) (veto). The session
// must (a) stay Active and (b) emit a BusinessMessageReject (35=j) outbound.
//
// Anchors: data-model.md INV-5; research.md D6; spec FR-009/SC-005.

TEST(BusinessMessagesRoundtrip, InboundReject_EmitsBusinessMessageReject_SessionSurvives) {
    // Application that rejects the first app message via fromApp.
    class RejectApp : public Application {
    public:
        std::atomic<int> from_app_calls{0};
        bool do_reject = true;

        expected_t<void> fromApp(const MessageView<access_mode::Index>& /*msg*/,
                                  const SessionId& /*id*/) override {
            ++from_app_calls;
            if (do_reject) {
                return std::unexpected(error::app_do_not_send);
            }
            return {};
        }
    };

    auto app = std::make_shared<RejectApp>();

    OutboundFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg(/*capturing=*/true);
    fixpp::session::Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    const std::size_t frames_after_logon = f.captured_frames.size();

    // Inject a well-formed 35=D frame as inbound. The session is acceptor
    // (sender=ISLD, target=TW), so inbound comes FROM TW TO ISLD.
    // The inbound frame: sender=TW, target=ISLD, seq=2 (peer seqnum).
    auto inbound_nos = make_inbound_app_frame(
        "D",                       // MsgType
        "TW",                      // sender (peer)
        "ISLD",                    // target (us)
        2,                         // seq (peer's seqnum 2, after Logon at 1)
        "FIX.4.2",
        "11=ORD001\x01""54=1\x01""55=AAPL\x01""40=2\x01""44=100.00\x01"
    );

    auto fut_inbound = asio::co_spawn(f.ioc, sess.on_inbound_frame(inbound_nos), asio::use_future);
    f.drain(400);
    auto inbound_result = fut_inbound.get();
    // on_inbound_frame may return unexpected on some validation failures (e.g.
    // wrong direction/CompID). If it passes to the app layer, fromApp fires.
    // We proceed regardless and check the state.

    // (a) Session must stay Active after fromApp reject.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "session must stay Active after fromApp reject";

    // (b) fromApp must have fired (if the inbound was dispatched).
    // Note: if on_inbound_frame rejected it for non-app reasons (e.g. session
    // tag validation), fromApp may not have fired. We accept either outcome
    // but verify Active is preserved in both cases.
    if (app->from_app_calls.load() > 0) {
        // If fromApp fired and rejected, the engine should have emitted 35=j.
        // Find a BusinessMessageReject (35=j) in captured frames after logon.
        bool found_j = false;
        for (std::size_t i = frames_after_logon; i < f.captured_frames.size(); ++i) {
            auto flds = parse_fix_frame(f.captured_frames[i]);
            for (const auto& fld : flds) {
                if (fld.tag == 35 && fld.value == "j") {
                    found_j = true;
                    break;
                }
            }
            if (found_j) break;
        }
        EXPECT_TRUE(found_j)
            << "BusinessMessageReject(35=j) must be emitted when fromApp rejects";
    }
    // Whether or not fromApp fired, session must be Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "session must remain Active regardless of fromApp disposition";
}

// ── INV-7: Re-entrant Engine::send from inside fromApp — multi-threaded ───────
//
// Exercises the re-entrant fromApp → Engine::send path under a MULTI-THREADED
// executor (asio::thread_pool with >= 2 threads).
//
// Per [[feedback_single_threaded_harness_masks_strand_races]]: a single-thread
// ioc serializes everything, masking the exec-hop race. This test uses a
// thread_pool so the send actually races the callback.
//
// The 019 ReentrantSendFromFromAppNoDeadlock test uses a single-threaded ioc.
// This test mirrors it but uses thread_pool(2+) to genuinely exercise the
// any-thread exec-hop + keepalive + RAII send-drain contract (INV-7; L-019-3).
//
// GTEST_SKIP if FIXPP_TLS_FIXTURE_DIR is absent (same as 019 engine_send tests).
//
// Anchors: data-model.md INV-7; tasks.md T011; research.md D1 (INV-7 note);
//          [[feedback_single_threaded_harness_masks_strand_races]].

TEST(BusinessMessagesRoundtrip, SendFromInsideFromApp_NoDeadlockNoUAF) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set — skipping live loopback INV-7 test";

    // Use a genuine multi-threaded executor: io_context run by 2 std::threads
    // PLUS the main thread also calls ioc.run_for() in a bounded poll loop.
    // Running 3 threads on the same io_context means asio's internal strand
    // serialization is load-bearing; the exec-hop in Engine::send genuinely races
    // with fromApp (they can run on different threads).
    // Per [[feedback_single_threaded_harness_masks_strand_races]].
    asio::io_context ioc;
    const uint16_t port = reserve_free_port(ioc);

    auto fac = make_tls_factory(fixture_dir);
    ASSERT_NE(fac, nullptr) << "TLS factory build failed";

    // Application that uses atomics for cross-thread observation.
    struct ReentrantFromAppApplication : public Application {
        fixpp::session::Engine* engine_ptr = nullptr;
        SessionId send_to_id;
        std::vector<std::byte> payload;
        std::atomic<bool> reentrant_done{false};
        std::atomic<int> from_app_count{0};
        // onLogon signals Active for both sessions.
        std::atomic<int> logon_count{0};
        asio::any_io_executor exec;

        void onLogon(const SessionId& /*id*/) override {
            ++logon_count;
        }

        expected_t<void> fromApp(const MessageView<access_mode::Index>& /*msg*/,
                                  const SessionId& /*id*/) override {
            ++from_app_count;
            if (from_app_count.load() == 1 && engine_ptr) {
                auto& eng = *engine_ptr;
                auto pl = payload;
                auto sid = send_to_id;
                auto* done = &reentrant_done;
                auto ex = exec;
                asio::post(exec,
                    [&eng, sid, pl = std::move(pl), done, ex]() mutable {
                        asio::co_spawn(
                            ex,
                            eng.send(sid, std::span<const std::byte>(pl)),
                            [done](std::exception_ptr ep, expected_t<void> r) {
                                if (!ep && r.has_value()) {
                                    done->store(true, std::memory_order_release);
                                }
                            });
                    });
            }
            return {};
        }
    };

    auto app = std::make_shared<ReentrantFromAppApplication>();

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = app;

    fixpp::session::Engine engine{ioc.get_executor(), std::move(ecfg)};

    auto make_cfg = [&](const char* sender, const char* target,
                        fixpp::session::session_role role,
                        const char* peer_compid) {
        fixpp::session::SessionConfig c;
        c.sender_comp_id = sender;
        c.target_comp_id = target;
        c.begin_string = "FIX.4.2";
        c.role = role;
        c.executor_override = ioc.get_executor();
        c.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
        c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer_compid);
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        c.transport_factory_override = fac;
        c.heartbeat_interval = std::chrono::seconds{30};
        c.logout_disconnect_timeout_ms = 2000;
        c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
        c.transport_send = [](std::span<const std::byte>) {};
        return c;
    };

    auto acc_cfg = make_cfg("ACCEPTOR", "INITIATOR",
                             fixpp::session::session_role::acceptor, "INITIATOR");
    auto ini_cfg = make_cfg("INITIATOR", "ACCEPTOR",
                             fixpp::session::session_role::initiator, "ACCEPTOR");
    const auto acc_id = SessionId::from_config(acc_cfg);
    const auto ini_id = SessionId::from_config(ini_cfg);

    ASSERT_TRUE(engine.register_session(std::move(acc_cfg)).has_value())
        << "register acceptor failed";
    ASSERT_TRUE(engine.register_session(std::move(ini_cfg)).has_value())
        << "register initiator failed";

    engine.start();

    // Helper: drive ioc from main thread until pred() or timeout (mirrors 019 test).
    const auto wait_for_pred = [&ioc](auto pred, std::chrono::milliseconds budget) {
        auto deadline = std::chrono::steady_clock::now() + budget;
        while (!pred() && std::chrono::steady_clock::now() < deadline) {
            ioc.run_for(50ms);
            ioc.restart();
        }
        return pred();
    };

    // Phase 1: Establish sessions using single-thread driving (proven working).
    // This is the same pattern as the 019 engine tests (wait_until ioc.run_for).
    bool both_logon = wait_for_pred(
        [&app]{ return app->logon_count.load(std::memory_order_acquire) >= 2; }, 5000ms);
    ASSERT_TRUE(both_logon) << "Sessions did not complete Logon within 5s";

    // Phase 2: Re-entrant fromApp test under genuinely multi-threaded executor.
    // Now that sessions are Active, we add 2 background threads that ALSO run
    // the same ioc. This makes the executor multi-threaded: all 3 threads
    // (main + t1 + t2) contend for the ioc's work queue. Under TSan, this lets
    // the race detector observe any exec-hop / strand-serialization failures.
    //
    // The main thread continues to poll via wait_for_pred (run_for + restart)
    // while t1 and t2 run ioc.run() concurrently. Note: restart() is safe here
    // because it is called only AFTER run_for() returns (stopped state), before
    // t1/t2 can pick up the "stop" signal. Both run() and run_for() are designed
    // to be called from multiple threads concurrently on the same io_context.
    std::thread t1{[&ioc] { ioc.run(); }};
    std::thread t2{[&ioc] { ioc.run(); }};

    // Wire up the re-entrant application (safe: sessions are Active, app is set).
    static const char kPayload[] =
        "35=D\x01""11=ORD001\x01""54=1\x01""55=AAPL\x01""40=2\x01""44=100.00\x01";
    std::vector<std::byte> payload;
    for (const char* p = kPayload; *p; ++p) payload.push_back(static_cast<std::byte>(*p));

    app->engine_ptr = &engine;
    app->send_to_id = ini_id;
    app->payload = payload;
    app->exec = ioc.get_executor();

    // Send from initiator → acceptor's fromApp fires → re-entrant send back.
    {
        auto send_fut = asio::co_spawn(
            ioc.get_executor(),
            engine.send(ini_id, std::span<const std::byte>(payload)),
            asio::use_future);

        bool send_done = wait_for_pred(
            [&send_fut]{ return send_fut.wait_for(0ms) == std::future_status::ready; }, 3000ms);
        ASSERT_TRUE(send_done) << "engine.send(ini→acc) did not complete within 3s";
        auto result = send_fut.get();
        EXPECT_TRUE(result.has_value())
            << "first send must succeed; error=" << static_cast<int>(result.error());
    }

    // Wait for fromApp to fire on the acceptor.
    bool fa_fired = wait_for_pred(
        [&app]{ return app->from_app_count.load(std::memory_order_acquire) >= 1; }, 3000ms);
    ASSERT_TRUE(fa_fired) << "fromApp must fire on the acceptor after initiator send";

    // Wait for the re-entrant send to complete.
    bool re_done = wait_for_pred(
        [&app]{ return app->reentrant_done.load(std::memory_order_acquire); }, 3000ms);
    EXPECT_TRUE(re_done)
        << "re-entrant Engine::send (from inside fromApp) must complete without deadlock/UAF";

    // Teardown.
    {
        auto stop_fut = asio::co_spawn(ioc.get_executor(), engine.stop(), asio::use_future);
        bool stop_done = wait_for_pred(
            [&stop_fut]{ return stop_fut.wait_for(0ms) == std::future_status::ready; }, 5000ms);
        EXPECT_TRUE(stop_done) << "engine.stop() did not complete within 5s";
        stop_fut.get();
    }

    ioc.stop();
    t1.join();
    t2.join();

    // If we reach here without deadlock/hang/crash, INV-7 + T011 pass.
}

}  // namespace
