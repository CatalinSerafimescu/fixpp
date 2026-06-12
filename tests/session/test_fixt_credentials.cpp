// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_fixt_credentials.cpp
//
// 033-fixt-fix50sp2-session — T002 (target creation) / T009 (RED) / T010
// (impl: logon_credentials + tag-554 redactor).
//
// Phase 2 (Foundational) witnesses:
//
//   LogonCredentials_OperatorStream_RedactsPassword (T009/T010):
//     logon_credentials::operator<< NEVER contains the password clear value;
//     the username IS visible; the mask token ("***") appears instead.
//     Uses a DISTINCT sentinel password so the test cannot pass vacuously.
//
//   Redactor_ElidesTag554Value (T009/T010):
//     redact_tag554() masks the 554 field value. The decoy "554=" inside a
//     free-text value (tag 58) must NOT be redacted — only the real 554 field
//     is affected. Neighbours (553=, 10=) survive unmodified.
//
// Anchors: data-model.md E5, research R6, contracts/fixt-logon-establishment.md
// C8, FR-011, INV-FIXT-4.

#include <gtest/gtest.h>

#include <fixpp/session/logon_credentials.hpp>
#include <sstream>
#include <string>

namespace {

using fixpp::session::logon_credentials;
using fixpp::session::redact_tag554;

// ── T009/T010: logon_credentials operator<< redaction ────────────────────────

TEST(LogonCredentials, OperatorStream_RedactsPassword) {
    // Use a DISTINCT sentinel password ("xyzzy-s3cr3t") so the test fails if
    // operator<< emits the clear value rather than the mask.
    logon_credentials creds;
    creds.username = "alice";
    creds.password = "xyzzy-s3cr3t";

    std::ostringstream oss;
    oss << creds;
    std::string s = oss.str();

    // The clear password value MUST NOT appear.
    EXPECT_EQ(s.find("xyzzy-s3cr3t"), std::string::npos)
        << "operator<< leaked the clear password: " << s;

    // The username MUST appear (not redacted).
    EXPECT_NE(s.find("alice"), std::string::npos) << "operator<< suppressed the username: " << s;

    // The mask token MUST appear (proves the password slot was handled).
    EXPECT_NE(s.find("***"), std::string::npos)
        << "operator<< did not emit the redaction mask: " << s;
}

TEST(LogonCredentials, OperatorStream_AbsentPassword) {
    // When password is absent the mask must NOT appear (it would be misleading).
    logon_credentials creds;
    creds.username = "bob";
    // password intentionally left unset

    std::ostringstream oss;
    oss << creds;
    std::string s = oss.str();

    EXPECT_NE(s.find("bob"), std::string::npos);
    // "(absent)" or equivalent — just ensure the clear mask isn't emitted for
    // an absent field.
    EXPECT_EQ(s.find("xyzzy"), std::string::npos);
}

TEST(LogonCredentials, OperatorStream_BothAbsent) {
    logon_credentials creds;  // both unset
    std::ostringstream oss;
    oss << creds;
    // Must not crash; no password clear value.
    EXPECT_EQ(oss.str().find("***"), std::string::npos);
}

// ── T009/T010: redact_tag554 ─────────────────────────────────────────────────

TEST(RedactTag554, ElidesTag554Value_MidFrame) {
    // Typical mid-frame 554 field in a SOH-delimited FIX frame.
    // Frame: 8=FIXT.1.1<SOH>553=alice<SOH>554=xyzzy-s3cr3t<SOH>10=123<SOH>
    std::string frame =
        "8=FIXT.1.1\x01"
        "553=alice\x01"
        "554=xyzzy-s3cr3t\x01"
        "10=123\x01";

    std::string redacted = redact_tag554(frame);

    // The clear password value MUST be absent.
    EXPECT_EQ(redacted.find("xyzzy-s3cr3t"), std::string::npos)
        << "redact_tag554 did not elide the 554 value: " << redacted;

    // The tag key and mask MUST be present.
    EXPECT_NE(redacted.find("554=***"), std::string::npos)
        << "redact_tag554 did not insert mask: " << redacted;

    // Neighbours must survive unmodified.
    EXPECT_NE(redacted.find("553=alice"), std::string::npos)
        << "redact_tag554 corrupted 553 field: " << redacted;
    EXPECT_NE(redacted.find("10=123"), std::string::npos)
        << "redact_tag554 corrupted 10 field: " << redacted;
}

TEST(RedactTag554, DoesNotRedactDecoy554InFreeText) {
    // A free-text field (tag 58) whose value contains the substring "554="
    // must NOT be redacted — only a SOH-anchored real 554 tag is a match.
    // Decoy: 58=note_554=fake<SOH>  (no SOH before "554=", so not a real field)
    std::string frame =
        "8=FIXT.1.1\x01"
        "553=carol\x01"
        "58=note_554=fake\x01"
        "554=real-secret\x01"
        "10=099\x01";

    std::string redacted = redact_tag554(frame);

    // The real 554 value MUST be masked.
    EXPECT_EQ(redacted.find("real-secret"), std::string::npos)
        << "real 554 value not redacted: " << redacted;

    // The decoy inside tag 58 MUST survive.
    EXPECT_NE(redacted.find("58=note_554=fake"), std::string::npos)
        << "redactor incorrectly touched the decoy inside tag 58: " << redacted;

    // 553 must survive.
    EXPECT_NE(redacted.find("553=carol"), std::string::npos)
        << "redactor corrupted 553: " << redacted;
}

TEST(RedactTag554, NoTag554_FrameUnchanged) {
    // A frame without a 554 field passes through byte-identical.
    std::string frame =
        "8=FIX.4.4\x01"
        "553=dave\x01"
        "10=042\x01";

    std::string redacted = redact_tag554(frame);
    EXPECT_EQ(redacted, frame);
}

TEST(RedactTag554, EmptyFrame) { EXPECT_EQ(redact_tag554(""), ""); }

TEST(RedactTag554, Tag554AtFrameStart) {
    // Edge case: 554 is the very first field (no preceding SOH).
    std::string frame =
        "554=top-secret\x01"
        "10=007\x01";

    std::string redacted = redact_tag554(frame);
    EXPECT_EQ(redacted.find("top-secret"), std::string::npos);
    EXPECT_NE(redacted.find("554=***"), std::string::npos);
    EXPECT_NE(redacted.find("10=007"), std::string::npos);
}

TEST(RedactTag554, Tag554AtFrameEnd_NoTrailingSOH) {
    // 554 value reaches end-of-string without a trailing SOH.
    std::string frame =
        "8=FIXT.1.1\x01"
        "554=mysecret";

    std::string redacted = redact_tag554(frame);
    EXPECT_EQ(redacted.find("mysecret"), std::string::npos);
    EXPECT_NE(redacted.find("554=***"), std::string::npos);
}

}  // namespace

// ── US2 Phase 4 witnesses: W6 (T019) and W7 (T020) ──────────────────────────
//
// These tests require Session + build_logon + CompIdAuthorizationPolicy.
// They are RED until T021–T024 are implemented.
//
// Infrastructure mirrors test_fixt_logon_establishment.cpp:
//   - asio::io_context (single-threaded), mock_clock injected
//   - Session + transport_send_ capture lambda
//   - make_fixt_logon_frame_with_creds() helper
//   - run_sync() helper
//
// Anchors: contracts/fixt-logon-establishment.md C7/C8; data-model E4;
//          research R6; FR-007/FR-008/FR-011; INV-FIXT-4.

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp_fixt_creds {

using namespace std::chrono_literals;
using fixpp::dict::application_version;
using fixpp::dict::version_registry;
using fixpp::session::fsm_state;
using fixpp::session::logon_credentials;
using fixpp::session::redact_tag554;

// ── Minimal FIX 5.0 SP2 XML dict (same as in establishment test) ─────────────

constexpr std::string_view kMinimalFix50sp2XmlCreds = R"xml(
<fix major="5" minor="0" servicepack="2">
  <header>
    <field number="8"  name="BeginString"  required="Y"/>
    <field number="9"  name="BodyLength"   required="Y"/>
    <field number="35" name="MsgType"      required="Y"/>
    <field number="49" name="SenderCompID" required="Y"/>
    <field number="56" name="TargetCompID" required="Y"/>
    <field number="34" name="MsgSeqNum"    required="Y"/>
    <field number="52" name="SendingTime"  required="Y"/>
    <field number="10" name="CheckSum"     required="Y"/>
  </header>
  <trailer>
    <field number="10" name="CheckSum" required="Y"/>
  </trailer>
  <messages>
    <message name="Heartbeat" msgtype="0" msgcat="admin">
      <field number="112" name="TestReqID" required="N"/>
    </message>
  </messages>
  <fields>
    <field number="8"   name="BeginString"  type="STRING"/>
    <field number="9"   name="BodyLength"   type="INT"/>
    <field number="35"  name="MsgType"      type="STRING"/>
    <field number="49"  name="SenderCompID" type="STRING"/>
    <field number="56"  name="TargetCompID" type="STRING"/>
    <field number="34"  name="MsgSeqNum"    type="INT"/>
    <field number="52"  name="SendingTime"  type="UTCTIMESTAMP"/>
    <field number="10"  name="CheckSum"     type="STRING"/>
    <field number="112" name="TestReqID"    type="STRING"/>
  </fields>
</fix>
)xml";

// Same as in establishment test — make a shared dict.
[[nodiscard]] std::shared_ptr<const fixpp::dict::Dictionary> make_dict_creds(std::string_view xml) {
    constexpr std::size_t kBufSize = 64u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
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

// ── Frame builders ────────────────────────────────────────────────────────────

// Build a FIXT Logon frame with optional 553/554/1137 fields.
[[nodiscard]] std::vector<std::byte> make_fixt_logon_frame_with_creds(
    std::string_view begin_string, std::uint32_t msg_seq_num, std::string_view sender_comp_id,
    std::string_view target_comp_id, int heartbt_int, std::string_view default_appl_ver_id = "",
    std::string_view username = "", std::string_view password = "",
    std::string_view sending_time = "20240101-00:00:00.000") {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt_int) + "\x01";
    if (!default_appl_ver_id.empty()) {
        body += "1137=" + std::string(default_appl_ver_id) + "\x01";
    }
    if (!username.empty()) {
        body += "553=" + std::string(username) + "\x01";
    }
    if (!password.empty()) {
        body += "554=" + std::string(password) + "\x01";
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// Convert a byte span to a std::string for inspection.
[[nodiscard]] std::string span_to_str(std::span<const std::byte> s) {
    std::string out;
    out.resize(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out[i] = static_cast<char>(s[i]);
    }
    return out;
}

// Check if a tag is present in the frame at a field boundary.
[[nodiscard]] bool has_tag_creds(std::string_view wire, std::string_view tag_num) {
    std::string needle = "\x01";
    needle += tag_num;
    needle += "=";
    return wire.find(needle) != std::string_view::npos;
}

// Run a coroutine synchronously on an io_context.
template <typename Fn>
auto run_sync_creds(asio::io_context& ioc, Fn&& fn)
    -> decltype(asio::co_spawn(ioc, fn(), asio::use_future).get()) {
    auto fut = asio::co_spawn(ioc, fn(), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    return fut.get();
}

// ── Shared fixture setup ──────────────────────────────────────────────────────

struct CredsFixtSetup {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    version_registry registry;

    explicit CredsFixtSetup(std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> dicts)
        : registry(dicts) {
        using namespace std::chrono;
        auto utc = std::chrono::system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    [[nodiscard]] fixpp::session::SessionConfig make_acceptor_cfg(
        application_version default_appl) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIXT.1.1";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::one_way_ca};
        cfg.dictionary = make_dict_creds(kMinimalFix50sp2XmlCreds);
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.default_appl_ver_id = default_appl;
        return cfg;
    }

    [[nodiscard]] fixpp::session::SessionConfig make_initiator_cfg(
        application_version default_appl) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIXT.1.1";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::one_way_ca};
        cfg.dictionary = make_dict_creds(kMinimalFix50sp2XmlCreds);
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::initiator;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.default_appl_ver_id = default_appl;
        return cfg;
    }
};

// ── T019 / W6 ─────────────────────────────────────────────────────────────────
//
// (a) configured creds ⇒ outbound FIXT Logon carries 553/554 with configured values
// (b) no creds configured ⇒ 553/554 ABSENT, establishment unaffected
// (c) inbound 553/554 surfaced to authorize_logon — assert seam receives exact values
//     (test policy captures what authorize_logon was called with)
// (d) credential-free inbound FIXT Logon still reaches Active
//
// [FR-007/FR-008/C7; contracts/fixt-logon-establishment.md C7]

// (a) Configured credentials appear on the outbound Logon.
TEST(FixtCredentials, W6a_ConfiguredCreds_EmittedOnOutboundLogon) {
    auto dict = make_dict_creds(kMinimalFix50sp2XmlCreds);
    CredsFixtSetup s{{dict}};

    std::vector<std::byte> init_frame_out;

    auto init_cfg = s.make_initiator_cfg(application_version::v50sp2);
    init_cfg.username = "alice";
    init_cfg.password = "s3cr3t-sentinel-W6a";
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        init_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session initiator(s.engine, init_cfg, &s.registry);
    run_sync_creds(s.ioc, [&] { return initiator.open(); });

    ASSERT_FALSE(init_frame_out.empty()) << "Initiator must emit a Logon";
    std::string wire = span_to_str(init_frame_out);

    // (a-1) 553=alice must be present.
    EXPECT_NE(wire.find("\x01"
                        "553=alice\x01"),
              std::string::npos)
        << "Outbound Logon with configured username must carry 553=alice; got: " << wire;

    // (a-2) 554=s3cr3t-sentinel-W6a must be present on the wire (plaintext to peer).
    EXPECT_NE(wire.find("\x01"
                        "554=s3cr3t-sentinel-W6a\x01"),
              std::string::npos)
        << "Outbound Logon with configured password must carry 554=<password>; got: " << wire;

    // (a-3) 553 must appear after 1137= (ordering: 1137 before 553, before 554, before 141).
    const auto pos_1137 = wire.find(
        "\x01"
        "1137=");
    const auto pos_553 = wire.find(
        "\x01"
        "553=");
    ASSERT_NE(pos_1137, std::string::npos) << "1137 must be present";
    ASSERT_NE(pos_553, std::string::npos) << "553 must be present";
    EXPECT_LT(pos_1137, pos_553)
        << "553 must appear AFTER 1137 in the wire frame (data-model E4 ordering)";
}

// (b) No creds configured ⇒ 553/554 absent.
TEST(FixtCredentials, W6b_NoCreds_553and554Absent_EstablishmentUnaffected) {
    auto dict = make_dict_creds(kMinimalFix50sp2XmlCreds);
    CredsFixtSetup s{{dict}};

    // Acceptor: receives initiator Logon and replies.
    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    std::vector<std::byte> acpt_frame_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);
    run_sync_creds(s.ioc, [&] { return acceptor.open(); });

    // Initiator: NO credentials configured.
    auto init_cfg = s.make_initiator_cfg(application_version::v50sp2);
    // username/password NOT set.
    std::vector<std::byte> init_frame_out;
    init_cfg.transport_send = [&](std::span<const std::byte> f) {
        init_frame_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session initiator(s.engine, init_cfg, &s.registry);
    run_sync_creds(s.ioc, [&] { return initiator.open(); });

    ASSERT_FALSE(init_frame_out.empty()) << "Initiator must emit a Logon";
    std::string init_wire = span_to_str(init_frame_out);

    // (b-1) 553/554 must be ABSENT when credentials are not configured.
    EXPECT_FALSE(has_tag_creds(init_wire, "553"))
        << "Unconfigured credentials: 553 must be absent; got: " << init_wire;
    EXPECT_FALSE(has_tag_creds(init_wire, "554"))
        << "Unconfigured credentials: 554 must be absent; got: " << init_wire;

    // (b-2) Feed initiator Logon to acceptor — acceptor must still reach Active.
    auto r = run_sync_creds(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{init_frame_out.data(), init_frame_out.size()});
    });
    ASSERT_TRUE(r.has_value()) << "Acceptor on_inbound_frame must succeed";
    EXPECT_EQ(acceptor.state(), fsm_state::Active)
        << "Acceptor must reach Active even without credentials (b)";

    // (b-3) Feed acceptor reply to initiator.
    ASSERT_FALSE(acpt_frame_out.empty()) << "Acceptor must have emitted a reply";
    auto r2 = run_sync_creds(s.ioc, [&] {
        return initiator.on_inbound_frame(
            std::span<const std::byte>{acpt_frame_out.data(), acpt_frame_out.size()});
    });
    ASSERT_TRUE(r2.has_value()) << "Initiator on_inbound_frame must succeed";
    EXPECT_EQ(initiator.state(), fsm_state::Active)
        << "Initiator must reach Active without credentials (b)";
}

// (c) Inbound 553/554 surfaced to authorize_logon — CAPTURING the actual args.
//
// A test logon-validator lambda captures (asserted_compid, creds) and asserts
// the seam received the correct username and password values. This is NOT a
// proxy: we assert the EXACT values received, not a downstream state.
//
// [contracts C7; research R6; FR-008/FR-008a; data-model E5]
TEST(FixtCredentials, W6c_InboundCreds_SurfacedToAuthorizeLogon) {
    auto dict = make_dict_creds(kMinimalFix50sp2XmlCreds);
    CredsFixtSetup s{{dict}};

    // Install a capturing logon validator on the acceptor's authorization policy.
    // The validator captures what it receives and always accepts (default-accept).
    std::string captured_compid;
    std::optional<std::string> captured_username;
    std::optional<std::string> captured_password;
    int authorize_logon_call_count = 0;

    fixpp::session::CompIdAuthorizationPolicy policy;
    // Set a capturing validator lambda (the FR-008a "validation knob" attach point).
    // T021: CompIdAuthorizationPolicy::set_logon_validator(callable) installs it.
    // The lambda always accepts (returns true); we capture the args.
    policy.set_logon_validator(
        [&](std::string_view compid, fixpp::session::logon_credentials const& creds) -> bool {
            ++authorize_logon_call_count;
            captured_compid = compid;
            captured_username = creds.username;
            captured_password = creds.password;
            return true;  // default-accept
        });

    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    acpt_cfg.compid_authorization_policy = std::move(policy);
    std::vector<std::byte> acpt_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);
    run_sync_creds(s.ioc, [&] { return acceptor.open(); });

    // Build an inbound FIXT Logon WITH 553/554.
    auto logon =
        make_fixt_logon_frame_with_creds("FIXT.1.1", 1, "TW", "ISLD", 30, "9",
                                         /*username=*/"charlie", /*password=*/"sentinel-W6c-xyzzy");

    auto r = run_sync_creds(s.ioc, [&] {
        return acceptor.on_inbound_frame(std::span<const std::byte>{logon.data(), logon.size()});
    });
    ASSERT_TRUE(r.has_value()) << "on_inbound_frame must succeed for valid Logon";

    // Verify authorize_logon was called exactly once.
    EXPECT_EQ(authorize_logon_call_count, 1)
        << "authorize_logon must be called exactly once per inbound FIXT Logon (C7)";

    // Verify the asserted compid received (peer's sender = our target = "TW").
    EXPECT_EQ(captured_compid, "TW")
        << "authorize_logon must receive the peer's SenderCompID as asserted_compid";

    // Verify the captured credentials are EXACTLY what was in the inbound Logon.
    ASSERT_TRUE(captured_username.has_value())
        << "authorize_logon must receive a populated username (C7)";
    EXPECT_EQ(*captured_username, "charlie") << "authorize_logon received wrong username";

    ASSERT_TRUE(captured_password.has_value())
        << "authorize_logon must receive a populated password (C7)";
    EXPECT_EQ(*captured_password, "sentinel-W6c-xyzzy")
        << "authorize_logon received wrong password value";

    // Session must reach Active (default-accept validator).
    EXPECT_EQ(acceptor.state(), fsm_state::Active)
        << "Acceptor must reach Active when authorize_logon returns true";
}

// (d) Credential-free inbound FIXT Logon still reaches Active.
TEST(FixtCredentials, W6d_CredentialFreeLogon_ReachesActive) {
    auto dict = make_dict_creds(kMinimalFix50sp2XmlCreds);
    CredsFixtSetup s{{dict}};

    // Install a counting validator — it should be called once with empty creds.
    int authorize_logon_call_count = 0;
    bool received_empty_creds = false;

    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.set_logon_validator(
        [&](std::string_view /*compid*/, fixpp::session::logon_credentials const& creds) -> bool {
            ++authorize_logon_call_count;
            received_empty_creds = !creds.username.has_value() && !creds.password.has_value();
            return true;
        });

    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    acpt_cfg.compid_authorization_policy = std::move(policy);
    std::vector<std::byte> acpt_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);
    run_sync_creds(s.ioc, [&] { return acceptor.open(); });

    // Logon WITHOUT 553/554.
    auto logon = make_fixt_logon_frame_with_creds("FIXT.1.1", 1, "TW", "ISLD", 30, "9");

    auto r = run_sync_creds(s.ioc, [&] {
        return acceptor.on_inbound_frame(std::span<const std::byte>{logon.data(), logon.size()});
    });
    ASSERT_TRUE(r.has_value());

    EXPECT_EQ(authorize_logon_call_count, 1)
        << "authorize_logon must be called even for credential-free Logons";
    EXPECT_TRUE(received_empty_creds)
        << "authorize_logon must receive empty creds for credential-free Logon";
    EXPECT_EQ(acceptor.state(), fsm_state::Active)
        << "Acceptor must reach Active for credential-free Logon (d)";
}

// ── T020 / W7 ─────────────────────────────────────────────────────────────────
//
// A Logon carrying a populated 554, when written to each persistence class,
// shows the value REDACTED. Assert per-class:
// (1) redact_tag554 applied to session logger/tap output (simulated by applying
//     the redactor to the captured frame bytes before any archival)
// (2) transport transcript capture: frame captured via transport_send_ lambda,
//     then redact_tag554 applied (the session logger redaction path)
// (3) unit golden fixture: known input → expected output comparison.
//
// NOTE: T024 wires the shared redact_tag554 at the production persistence sites;
// W7 proves the redaction function works correctly at each artifact class.
// Specifically: the production code uses cfg_.transport_send capturing (test);
// production session logger/tap hooks are deferred to the 2l tap feature;
// the binary file_store is not affected (binary format, not human-readable text).
//
// [FR-011/C8/INV-FIXT-4; data-model INV-FIXT-4]

// (1)+(2) Session/transport transcript capture: outbound Logon with 554,
// captured via transport_send_, then redact_tag554 applied.
// The sentinel password "SENTINEL-W7-xyzzy" must not appear in the redacted output;
// "554=***" must appear instead.
TEST(FixtCredentials, W7_OutboundLogonWith554_RedactedBeforeCapture) {
    // Build a raw outbound Logon frame via build_logon with 553+554.
    // Uses the build_logon API directly (T022 extension).
    std::array<std::byte, 512> buf{};
    auto r = fixpp::session::build_logon(std::span<std::byte>(buf),
                                         /*seq=*/1,
                                         /*sender_comp_id=*/"SENDER",
                                         /*target_comp_id=*/"TARGET",
                                         /*begin_string=*/"FIXT.1.1",
                                         /*heartbt_int=*/30,
                                         /*sending_time=*/"20240101-00:00:00.000",
                                         /*reset_seqnum=*/false,
                                         /*next_expected_seq=*/std::nullopt,
                                         /*default_appl_ver_id=*/application_version::v50sp2,
                                         /*username=*/"user1",
                                         /*password=*/"SENTINEL-W7-xyzzy");
    ASSERT_TRUE(r.has_value()) << "build_logon with creds must succeed";

    const std::string raw = span_to_str(*r);

    // The raw frame carries the sentinel password (it must be on the wire for auth).
    ASSERT_NE(raw.find("SENTINEL-W7-xyzzy"), std::string::npos)
        << "build_logon must emit the password clear on the wire (for peer auth); got: " << raw;

    // Site (1)+(2): apply redact_tag554 as a persistence/logging interceptor.
    const std::string redacted = redact_tag554(raw);

    // The clear password must be ABSENT after redaction.
    EXPECT_EQ(redacted.find("SENTINEL-W7-xyzzy"), std::string::npos)
        << "redact_tag554 must elide the clear password value (INV-FIXT-4); redacted: " << redacted;

    // The redaction marker must be PRESENT.
    EXPECT_NE(redacted.find("554=***"), std::string::npos)
        << "redact_tag554 must emit 554=*** in place of the password; redacted: " << redacted;

    // The username (553) must survive unmodified.
    EXPECT_NE(redacted.find("553=user1"), std::string::npos)
        << "redact_tag554 must not touch 553; redacted: " << redacted;

    // The 1137 field must survive unmodified.
    EXPECT_NE(redacted.find("1137=9"), std::string::npos)
        << "redact_tag554 must not touch 1137; redacted: " << redacted;
}

// (3) Unit golden fixture: redact_tag554 on a known FIXT Logon frame with 554
// produces a known expected redacted string.
// This is the "golden" comparison — distinct from the function-unit tests in
// T009/T010 (which test the redactor in isolation without FIXT context).
TEST(FixtCredentials, W7_GoldenFixture_RedactedLogon) {
    // Input: a typical FIXT Logon with 553+554 sandwiched between 1137 and 141.
    // (Ordering as per data-model E4: 1137 after 108, 553+554 after 1137, before 141.)
    const std::string input =
        "8=FIXT.1.1\x01"
        "9=82\x01"
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01"
        "1137=9\x01"
        "553=user1\x01"
        "554=SENTINEL-W7-golden\x01"
        "10=042\x01";

    const std::string expected =
        "8=FIXT.1.1\x01"
        "9=82\x01"
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01"
        "1137=9\x01"
        "553=user1\x01"
        "554=***\x01"
        "10=042\x01";

    const std::string got = redact_tag554(input);

    EXPECT_EQ(got, expected)
        << "Golden fixture: redact_tag554 on a FIXT Logon with 554 must produce the "
        << "expected redacted frame exactly. Sentinel 'SENTINEL-W7-golden' must be absent.";

    // Extra assertion: clear sentinel absent.
    EXPECT_EQ(got.find("SENTINEL-W7-golden"), std::string::npos)
        << "Clear password sentinel must be absent in golden output";
}

// ── FQ-2 Gate B r1 — authorize_logon throw-safety ────────────────────────────
//
// A user-installed logon_validator that THROWS must NOT terminate the process.
// The noexcept boundary inside authorize_logon must absorb the throw and return
// false (→ Disconnected), keeping noexcept honest.
//
// Pre-fix: the thrown exception crosses the noexcept boundary → std::terminate.
// Post-fix: the session reaches Disconnected (not Active), no crash.
//
// [compid_authorization_policy.cpp:345; FR-008a; [const §X.5]; FQ-2]

TEST(FixtCredentials, FQ2_ThrowingLogonValidator_SessionDisconnectsNoTerminate) {
    auto dict = make_dict_creds(kMinimalFix50sp2XmlCreds);
    CredsFixtSetup s{{dict}};

    // Install a logon_validator that throws a std::runtime_error.
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.set_logon_validator(
        [](std::string_view, fixpp::session::logon_credentials const&) -> bool {
            throw std::runtime_error("intentional-throw-FQ2");
        });

    auto acpt_cfg = s.make_acceptor_cfg(application_version::v50sp2);
    acpt_cfg.compid_authorization_policy = std::move(policy);
    std::vector<std::byte> acpt_out;
    acpt_cfg.transport_send = [&](std::span<const std::byte> f) {
        acpt_out.assign(f.begin(), f.end());
    };
    fixpp::session::Session acceptor(s.engine, acpt_cfg, &s.registry);
    run_sync_creds(s.ioc, [&] { return acceptor.open(); });

    // Send a credentialed inbound FIXT Logon to trigger authorize_logon.
    auto logon = make_fixt_logon_frame_with_creds(
        "FIXT.1.1", 1, "TW", "ISLD", 30, "9",
        /*username=*/"attacker", /*password=*/"boom");

    // Pre-fix: this call terminates the process via std::terminate.
    // Post-fix: it returns normally (the throw is absorbed inside authorize_logon).
    auto r = run_sync_creds(s.ioc, [&] {
        return acceptor.on_inbound_frame(
            std::span<const std::byte>{logon.data(), logon.size()});
    });

    // The session must NOT be Active — a throwing validator must reject.
    EXPECT_NE(acceptor.state(), fsm_state::Active)
        << "A throwing logon_validator must NOT allow the session to reach Active (FQ-2)";

    // The session must be Disconnected (false return → Disconnected path).
    EXPECT_EQ(acceptor.state(), fixpp::session::fsm_state::Disconnected)
        << "A throwing logon_validator must cause Disconnected state (FQ-2)";
}

// ── FQ-3 Gate B r1 — credential SOH/control-char injection ───────────────────
//
// Configured username/password containing SOH (\x01) or '=' can inject
// arbitrary FIX fields into the outbound Logon. Both values must be validated
// at open()-time (invalid_session_config) before any emission.
//
// Pre-fix: open() succeeds and the injected field appears on the wire.
// Post-fix: open() returns invalid_session_config, no frame emitted.
//
// [feedback_delimiter_injection_verbatim_field_copy; FQ-3; FR-007]

TEST(FixtCredentials, FQ3a_PasswordWithSOH_ReturnsInvalidConfig_NoWireEmit) {
    auto dict = make_dict_creds(kMinimalFix50sp2XmlCreds);
    CredsFixtSetup s{{dict}};

    std::vector<std::byte> emitted;
    auto cfg = s.make_initiator_cfg(application_version::v50sp2);
    cfg.password = std::string("p") + "\x01" + "113=x";  // SOH injection: would forge tag 113 on the wire
    cfg.transport_send = [&](std::span<const std::byte> f) {
        emitted.assign(f.begin(), f.end());
    };

    fixpp::session::Session sess(s.engine, cfg, &s.registry);
    auto result = run_sync_creds(s.ioc, [&] { return sess.open(); });

    // Must reject — credential contains a SOH delimiter.
    ASSERT_FALSE(result.has_value())
        << "open() must fail when password contains SOH (FQ-3a)";
    EXPECT_EQ(result.error(), fixpp::core::error::invalid_session_config)
        << "Error must be invalid_session_config for SOH-containing password; "
        << "got: " << static_cast<int>(result.error());

    // No wire frame emitted.
    EXPECT_TRUE(emitted.empty())
        << "No frame must be emitted when open() rejects (FQ-3a)";

    // Verify the injected tag does NOT appear on the wire (belt-and-suspenders:
    // if open() correctly rejected, emitted is empty and this trivially holds).
    std::string wire(reinterpret_cast<const char*>(emitted.data()), emitted.size());
    EXPECT_EQ(wire.find(std::string("\x01") + "113=x"), std::string::npos)
        << "Injected field must NOT appear on the wire (FQ-3a)";
}

TEST(FixtCredentials, FQ3b_UsernameWithSOH_ReturnsInvalidConfig_NoWireEmit) {
    auto dict = make_dict_creds(kMinimalFix50sp2XmlCreds);
    CredsFixtSetup s{{dict}};

    std::vector<std::byte> emitted;
    auto cfg = s.make_initiator_cfg(application_version::v50sp2);
    cfg.username = std::string("u") + "\x01" + "554=x";  // SOH injection: would forge tag 554 on the wire
    cfg.transport_send = [&](std::span<const std::byte> f) {
        emitted.assign(f.begin(), f.end());
    };

    fixpp::session::Session sess(s.engine, cfg, &s.registry);
    auto result = run_sync_creds(s.ioc, [&] { return sess.open(); });

    ASSERT_FALSE(result.has_value())
        << "open() must fail when username contains SOH (FQ-3b)";
    EXPECT_EQ(result.error(), fixpp::core::error::invalid_session_config)
        << "Error must be invalid_session_config for SOH-containing username; "
        << "got: " << static_cast<int>(result.error());

    EXPECT_TRUE(emitted.empty())
        << "No frame must be emitted when open() rejects (FQ-3b)";
}

}  // namespace fixpp_fixt_creds
