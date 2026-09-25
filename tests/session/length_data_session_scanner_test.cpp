// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/length_data_session_scanner_test.cpp — fixpp#426
//
// The dictionary-free session scanners must treat a counted Data value as one
// field. Each case puts `<SOH><tag>=<forged>` inside a standard Data value whose
// Length makes it well-formed, and asserts the scanner does not see the forged
// field. On the unfixed tree every scanner here splits the value at the SOH and
// keeps the LAST value per tag, so each case is RED.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/logon_credentials.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/parser.hpp>  // dict_hooks::for_table_view
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/scan_first_frame_ids.hpp"
#include "session/scan_frame_header.hpp"
#include "support/alloc_guard_markers.hpp"
#include "support/extract_tag.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

namespace fixpp::session::test {
namespace {

// `body` is everything between 9=… and 10=…; the checksum is not validated here.
std::vector<std::byte> make_frame(std::string const& body) {
    std::string const full =
        "8=FIX.4.4\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// "<length_tag>=<n>" SOH "<data_tag>=<value>" SOH, with n the value's byte count.
std::string counted(int length_tag, int data_tag, std::string_view value) {
    return std::to_string(length_tag) + "=" + std::to_string(value.size()) + "\x01" +
           std::to_string(data_tag) + "=" + std::string{value} + "\x01";
}

std::string_view as_sv(std::vector<std::byte> const& v) {
    return {reinterpret_cast<char const*>(v.data()), v.size()};
}

}  // namespace

TEST(LengthDataSessionScanner, ScanFrameHeaderIgnoresMsgSeqNumInsideEncodedText) {
    auto const frame = make_frame(
        "35=D\x01"
        "34=5\x01"
        "49=S\x01"
        "56=T\x01" +
        counted(354, 355,
                "x\x01"
                "34=99") +
        "58=ok\x01");
    auto const h = detail::scan_frame_header(std::span<const std::byte>{frame});
    EXPECT_EQ(h.msg_seq_num, "5");
}

TEST(LengthDataSessionScanner, ScanFrameHeaderIgnoresResetSeqNumFlagInsideSecureData) {
    auto const frame = make_frame(
        "35=A\x01"
        "34=1\x01" +
        counted(90, 91,
                "x\x01"
                "141=Y") +
        "49=S\x01"
        "56=T\x01");
    auto const h = detail::scan_frame_header(std::span<const std::byte>{frame});
    EXPECT_TRUE(h.reset_seqnum_flag.empty());
    EXPECT_EQ(h.sender_comp_id, "S");
}

// A Length that overruns the frame is malformed: the scanner stops rather than
// reading on, so nothing after the value can be forged (design §4).
TEST(LengthDataSessionScanner, ScanFrameHeaderStopsAtAnOverrunningLength) {
    auto const frame = make_frame(
        "35=D\x01"
        "34=5\x01"
        "354=999\x01"
        "355=x\x01"
        "34=99\x01");
    auto const h = detail::scan_frame_header(std::span<const std::byte>{frame});
    EXPECT_EQ(h.msg_seq_num, "5");
}

TEST(LengthDataSessionScanner, ScanFirstFrameIdsIgnoresSenderCompIdInsideRawData) {
    auto const frame = make_frame(
        "35=A\x01"
        "49=REAL\x01"
        "56=T\x01" +
        counted(95, 96,
                "x\x01"
                "49=EVIL"));
    auto const ids = detail::scan_first_frame_ids(std::span<const std::byte>{frame});
    EXPECT_EQ(ids.sender_comp_id, "REAL");
}

TEST(LengthDataSessionScanner, InterpretLogonIgnoresPasswordInsideRawData) {
    auto const frame = make_frame(
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01"
        "554=real\x01" +
        counted(95, 96,
                "x\x01"
                "554=forged"));
    auto const r = interpret_logon(std::span<const std::byte>{frame}, /*expected_sender=*/"TW",
                                   /*expected_target=*/"ISLD", /*expected_begin=*/"FIX.4.4");
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->password.has_value());
    EXPECT_EQ(*r->password, "real");
}

TEST(LengthDataSessionScanner, FrameHasGenuineTag554IgnoresACountedValue) {
    auto const frame = make_frame("35=A\x01" + counted(95, 96,
                                                       "x\x01"
                                                       "554=secret"));
    EXPECT_FALSE(frame_has_genuine_tag554(std::span<const std::byte>{frame}));
}

TEST(LengthDataSessionScanner, MaskTag554LeavesCountedValueBytesUnchanged) {
    auto frame = make_frame(
        "35=A\x01"
        "554=pw\x01" +
        counted(95, 96,
                "x\x01"
                "554=secret"));
    auto const before = frame;
    EXPECT_TRUE(mask_tag554_same_length_inplace(std::span<std::byte>{frame}));

    std::string const counted_part = counted(95, 96,
                                             "x\x01"
                                             "554=secret");
    auto const pos = as_sv(before).find(counted_part);
    ASSERT_NE(pos, std::string_view::npos);
    EXPECT_EQ(as_sv(frame).substr(pos, counted_part.size()), counted_part)
        << "the counted RawData value must not be masked";
    EXPECT_EQ(as_sv(frame).find("554=pw"), std::string_view::npos)
        << "the genuine Password must still be masked";
}

TEST(LengthDataSessionScanner, RedactTag554LeavesCountedValueUnchanged) {
    std::string const counted_part = counted(95, 96,
                                             "x\x01"
                                             "554=secret");
    std::string const frame =
        "8=FIX.4.4\x01"
        "35=A\x01" +
        counted_part + "10=000\x01";
    EXPECT_NE(redact_tag554(frame).find(counted_part), std::string::npos);
}

// A count that ends on a byte other than SOH is malformed too (design §4, rows 6-8):
// the scanner stops, so the bytes after the counted value are never read as a field.
TEST(LengthDataSessionScanner, ScanFrameHeaderStopsAtACountEndingOnANonSohByte) {
    auto const frame = make_frame(
        "35=D\x01"
        "34=5\x01"
        "354=2\x01"
        "355=x\x01"
        "Z34=99\x01");
    auto const h = detail::scan_frame_header(std::span<const std::byte>{frame});
    EXPECT_EQ(h.msg_seq_num, "5");
}

// Every field interpret_logon validates precedes the count, so the count is the only
// thing that can refuse this Logon (091 FR-020).
TEST(LengthDataSessionScanner, InterpretLogonRefusesAMalformedCount) {
    auto const frame = make_frame(
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01"
        "95=999\x01"
        "96=x\x01"
        "554=late\x01");
    auto const r = interpret_logon(std::span<const std::byte>{frame}, /*expected_sender=*/"TW",
                                   /*expected_target=*/"ISLD", /*expected_begin=*/"FIX.4.4");
    ASSERT_FALSE(r.has_value()) << "a Logon carrying a malformed 95 count was accepted";
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

// On a malformed count the Password walk cannot know where the value ends, so from that
// field on it masks every SOH-anchored `554=` (design §4, rows 10-12): a real Password
// is over-masked rather than disclosed.
TEST(LengthDataSessionScanner, Tag554ScannersFallBackToEveryAnchoredPasswordOnAMalformedCount) {
    auto frame = make_frame(
        "35=A\x01"
        "554=pw\x01"
        "95=999\x01"
        "96=x\x01"
        "554=inner\x01");
    EXPECT_TRUE(mask_tag554_same_length_inplace(std::span<std::byte>{frame}));
    EXPECT_NE(as_sv(frame).find("554=**\x01"), std::string_view::npos) << as_sv(frame);
    EXPECT_NE(as_sv(frame).find("554=*****\x01"), std::string_view::npos) << as_sv(frame);

    auto const only_after = make_frame(
        "35=A\x01"
        "95=999\x01"
        "96=x\x01"
        "554=secret\x01");
    EXPECT_TRUE(frame_has_genuine_tag554(std::span<const std::byte>{only_after}));
    EXPECT_EQ(redact_tag554(std::string{as_sv(only_after)}).find("secret"), std::string::npos);
}

TEST(LengthDataSessionScanner, MaskTag554SkipsAFieldWhoseTagIsNotDigits) {
    auto frame = make_frame(
        "35=A\x01"
        "5x4=1\x01"
        "554=pw\x01");
    EXPECT_TRUE(mask_tag554_same_length_inplace(std::span<std::byte>{frame}));
    EXPECT_NE(as_sv(frame).find("554=**\x01"), std::string_view::npos) << as_sv(frame);
}

// ── A malformed count must not hide a later EncryptMethod(98) (T070) ─────────
//
// A malformed count is a Length immediately followed by its paired Data whose counted
// extent reaches or passes the end of the whole framed message, or whose following
// byte is not SOH. Every cell below shares one
// Logon body and varies only the count digit L and the 98 value E, so a refusal of a
// malformed-count cell cannot come from anything but the count: the L=1 twins, one
// byte away, are the shape controls. With L=2 the counted value is "x" SOH and the
// byte after it is the '9' of "98=", not SOH. With L=999 the count runs past the frame.
// The expectation in each malformed-count cell is the [const §XII.7] refusal.
namespace {

std::string logon_body_with_count(std::string_view sender, std::string_view target,
                                  std::string_view length, std::string_view encrypt_method) {
    return "35=A\x01"
           "34=1\x01"
           "49=" +
           std::string{sender} +
           "\x01"
           "52=20240101-00:00:00.000\x01"
           "56=" +
           std::string{target} +
           "\x01"
           "108=30\x01"
           "95=" +
           std::string{length} +
           "\x01"
           "96=x\x01"
           "98=" +
           std::string{encrypt_method} + "\x01";
}

fixpp::core::expected_t<logon_interpret_result> interpret_count_cell(
    std::string_view length, std::string_view encrypt_method) {
    auto const frame = make_frame(logon_body_with_count("TW", "ISLD", length, encrypt_method));
    return interpret_logon(std::span<const std::byte>{frame}, /*expected_sender=*/"TW",
                           /*expected_target=*/"ISLD", /*expected_begin=*/"FIX.4.4");
}

}  // namespace

TEST(InterpretLogonMalformedCount, CountEndingOnANonSohByteDoesNotHideEncryptMethod) {
    auto const r = interpret_count_cell("2", "2");
    ASSERT_FALSE(r.has_value()) << "a Logon with 98=2 after a malformed 95 count was accepted";
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

TEST(InterpretLogonMalformedCount, CountRunningPastTheFrameDoesNotHideEncryptMethod) {
    auto const r = interpret_count_cell("999", "2");
    ASSERT_FALSE(r.has_value()) << "a Logon with 98=2 after an overrunning 95 count was accepted";
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

TEST(InterpretLogonMalformedCount, MalformedCountRefusesTheLogonWithNothingAfterIt) {
    auto const r = interpret_count_cell("2", "0");
    ASSERT_FALSE(r.has_value()) << "a Logon carrying a malformed 95 count was accepted";
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

TEST(InterpretLogonMalformedCount, TwinWellFormedCountWithNonZeroEncryptMethodIsRefused) {
    auto const r = interpret_count_cell("1", "2");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

TEST(InterpretLogonMalformedCount, TwinWellFormedCountWithZeroEncryptMethodIsAccepted) {
    auto const r = interpret_count_cell("1", "0");
    EXPECT_TRUE(r.has_value()) << "error " << (r ? 0 : static_cast<int>(r.error()));
}

TEST(InterpretLogonMalformedCount, TwinNoCountWithNonZeroEncryptMethodIsRefused) {
    auto const frame = make_frame(
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "108=30\x01"
        "98=2\x01");
    auto const r = interpret_logon(std::span<const std::byte>{frame}, "TW", "ISLD", "FIX.4.4");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

// ── An orphan Length: its next field is not its paired Data (FR-020) ────────
//
// The count cells' body with 96=x removed, so 95=999 is followed directly by 98. The
// carry applies a count only to the Length's paired Data, so 98 is read as a plain
// field and its value decides the Logon.
namespace {

std::vector<std::byte> orphan_length_logon(std::string_view encrypt_method) {
    std::string body = logon_body_with_count("TW", "ISLD", "999", encrypt_method);
    auto const data = body.find("96=x\x01");
    EXPECT_NE(data, std::string::npos) << "the count cells' body no longer carries 96=x";
    if (data != std::string::npos) {
        body.erase(data, std::string_view{"96=x\x01"}.size());
    }
    auto frame = make_frame(body);
    EXPECT_NE(as_sv(frame).find(std::string{"95=999\x01"
                                            "98="} +
                                std::string{encrypt_method} + "\x01"),
              std::string_view::npos)
        << "the Length must be followed directly by 98: " << as_sv(frame);
    return frame;
}

}  // namespace

TEST(InterpretLogonMalformedCount, OrphanOverrunningLengthDoesNotHideEncryptMethod) {
    auto const frame = orphan_length_logon("2");
    auto const r = interpret_logon(std::span<const std::byte>{frame}, /*expected_sender=*/"TW",
                                   /*expected_target=*/"ISLD", /*expected_begin=*/"FIX.4.4");
    ASSERT_FALSE(r.has_value()) << "a Logon with 98=2 after an orphan 95 was accepted";
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

TEST(InterpretLogonMalformedCount, TwinOrphanOverrunningLengthWithZeroEncryptMethodIsAccepted) {
    auto const frame = orphan_length_logon("0");
    auto const r = interpret_logon(std::span<const std::byte>{frame}, /*expected_sender=*/"TW",
                                   /*expected_target=*/"ISLD", /*expected_begin=*/"FIX.4.4");
    EXPECT_TRUE(r.has_value()) << "error " << (r ? 0 : static_cast<int>(r.error()));
}

// ── The equality boundary of a count, on the whole framed message (FR-020) ──
//
// Every field interpret_logon validates precedes 95, and RawData(96) is the last body
// field, so its counted extent runs into the trailer. R is the number of bytes from the
// first byte of 96's value to the end of the framed message, trailer included. The count
// N moves only BodyLength, which precedes 96, so R is taken from a first framing, and
// each cell re-measures R and re-reads N on its final framed bytes.
namespace {

std::string boundary_logon_body(std::size_t count) {
    return "35=A\x01"
           "34=1\x01"
           "49=TW\x01"
           "52=20240101-00:00:00.000\x01"
           "56=ISLD\x01"
           "108=30\x01"
           "98=0\x01"
           "95=" +
           std::to_string(count) +
           "\x01"
           "96=x\x01";
}

// Bytes from the first byte of 96's value to the end of `frame`; npos when the
// SOH-anchored 96= is missing or not unique.
std::size_t bytes_from_raw_data_value_to_end(std::vector<std::byte> const& frame) {
    auto const sv = as_sv(frame);
    auto const anchor = std::string_view{
        "\x01"
        "96="};
    auto const pos = sv.find(anchor);
    if (pos == std::string_view::npos || pos != sv.rfind(anchor)) {
        return std::string_view::npos;
    }
    return sv.size() - (pos + anchor.size());
}

// The count 95 carries in `frame`, parsed from the framed bytes; npos when absent.
std::size_t count_in_frame(std::vector<std::byte> const& frame) {
    auto const digits = fixpp::test_support::extract_tag(frame, 95);
    if (digits.empty()) {
        return std::string_view::npos;
    }
    return std::stoul(digits);
}

// Frames the boundary body with the count R - `below`, R taken from a first framing.
std::vector<std::byte> boundary_logon(std::size_t below) {
    auto const r = bytes_from_raw_data_value_to_end(make_frame(boundary_logon_body(0)));
    EXPECT_NE(r, std::string_view::npos);
    EXPECT_GT(r, below);
    return make_frame(boundary_logon_body(r - below));
}

}  // namespace

TEST(InterpretLogonMalformedCount, CountReachingTheFrameEndIsRefused) {
    auto const frame = boundary_logon(0);
    auto const n = count_in_frame(frame);
    ASSERT_NE(n, std::string_view::npos) << as_sv(frame);
    // R == N on the final framed bytes: the counted extent reaches the end of the frame.
    ASSERT_EQ(bytes_from_raw_data_value_to_end(frame), n) << as_sv(frame);
    auto const r = interpret_logon(std::span<const std::byte>{frame}, /*expected_sender=*/"TW",
                                   /*expected_target=*/"ISLD", /*expected_begin=*/"FIX.4.4");
    ASSERT_FALSE(r.has_value()) << "a Logon whose 95 count reaches the frame end was accepted";
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

TEST(InterpretLogonMalformedCount, TwinCountEndingOnTheFinalSohIsAccepted) {
    auto const frame = boundary_logon(1);
    auto const n = count_in_frame(frame);
    ASSERT_NE(n, std::string_view::npos) << as_sv(frame);
    // R == N + 1 on the final framed bytes: the byte after the counted extent is the
    // trailer's final SOH.
    ASSERT_EQ(bytes_from_raw_data_value_to_end(frame), n + 1) << as_sv(frame);
    ASSERT_EQ(as_sv(frame).back(), '\x01');
    auto const r = interpret_logon(std::span<const std::byte>{frame}, /*expected_sender=*/"TW",
                                   /*expected_target=*/"ISLD", /*expected_begin=*/"FIX.4.4");
    EXPECT_TRUE(r.has_value()) << "error " << (r ? 0 : static_cast<int>(r.error()));
}

// A dictionary whose custom pair (5001, 5002) is adjacent ONLY inside a <component>
// that Logon references: not adjacent in <fields> (Text(58) sits between them) and
// never a direct <field> child of a message. The shape of
// tests/wire/dict_hooks_custom_pair_test.cpp's kComponentOnlyPairXml, with Logon.
namespace {

constexpr std::string_view kLogonComponentPairXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='98' name='EncryptMethod' type='INT'/>)"
    R"(<field number='108' name='HeartBtInt' type='INT'/>)"
    R"(<field number='5001' name='CustomPairLen' type='LENGTH'/>)"
    R"(<field number='58' name='Text' type='STRING'/>)"
    R"(<field number='5002' name='CustomPairData' type='DATA'/>)"
    R"(</fields>)"
    R"(<components>)"
    R"(<component name='CustomPair'>)"
    R"(<field name='CustomPairLen' required='N'/>)"
    R"(<field name='CustomPairData' required='N'/>)"
    R"(</component>)"
    R"(</components>)"
    R"(<messages>)"
    R"(<message name='Logon' msgtype='A' msgcat='admin'>)"
    R"(<field name='EncryptMethod' required='Y'/>)"
    R"(<field name='HeartBtInt' required='Y'/>)"
    R"(<component name='CustomPair' required='N'/>)"
    R"(</message>)"
    R"(</messages></fix>)";

std::vector<std::byte> component_pair_logon(std::string_view length) {
    return make_frame(
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "108=30\x01"
        "5001=" +
        std::string{length} +
        "\x01"
        "5002=x\x01"
        "98=2\x01");
}

}  // namespace

TEST(InterpretLogonMalformedCount, ComponentOnlyPairCountDoesNotHideEncryptMethod) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto const tv =
        fixpp::dict::XmlLoader{}.load_from_string(kLogonComponentPairXml, &dict_mr).as_table_view();
    auto const hooks = fixpp::wire::dict_hooks::for_table_view(tv);
    ASSERT_EQ(hooks.data_tag_for_length(5001), 5002U)
        << "precondition: the component-only pair must be a pair under these hooks";

    auto const frame = component_pair_logon("2");
    auto const r =
        interpret_logon(std::span<const std::byte>{frame}, "TW", "ISLD", "FIX.4.4", hooks);
    ASSERT_FALSE(r.has_value())
        << "a Logon with 98=2 after a malformed 5001 count of a dictionary pair was accepted";
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

// Without the dictionary, 5001 and 5002 are plain fields, so the same frame's 98=2 is read.
TEST(InterpretLogonMalformedCount, TwinComponentPairFrameWithoutTheDictionaryIsRefused) {
    auto const frame = component_pair_logon("2");
    auto const r = interpret_logon(std::span<const std::byte>{frame}, "TW", "ISLD", "FIX.4.4",
                                   fixpp::wire::dict_hooks::none());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

TEST(InterpretLogonMalformedCount, TwinWellFormedComponentPairWithNonZeroEncryptMethodIsRefused) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto const tv =
        fixpp::dict::XmlLoader{}.load_from_string(kLogonComponentPairXml, &dict_mr).as_table_view();
    auto const hooks = fixpp::wire::dict_hooks::for_table_view(tv);
    ASSERT_EQ(hooks.data_tag_for_length(5001), 5002U);

    auto const frame = component_pair_logon("1");
    auto const r =
        interpret_logon(std::span<const std::byte>{frame}, "TW", "ISLD", "FIX.4.4", hooks);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

// ── The same cells through both Session arms that call interpret_logon ───────
//
// Acceptor: the peer's Logon arrives in NotConnected. Initiator: the peer's Logon
// ack arrives in LogonSent. Fixture shape and config mirror
// tests/session/logon_handshake_test.cpp's LogonHandshakeTest. On each arm the
// L=1/E=0 twin must reach Active and the L=1/E=2 twin must not, or the harness does
// not discriminate and the malformed-count cell means nothing.
class LogonArmMalformedCount : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        using namespace std::chrono;
        // The mock clock starts at the instant the frames carry in SendingTime(52).
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(session_role role) {
        SessionConfig cfg;
        bool const acceptor = role == session_role::acceptor;
        cfg.sender_comp_id = acceptor ? "ISLD" : "TW";
        cfg.target_comp_id = acceptor ? "TW" : "ISLD";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = role;
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    template <class Awaitable>
    fixpp::core::expected_t<void> run_sync(Awaitable aw, char const* site) {
        auto fut = asio::co_spawn(ioc, std::move(aw), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    // The FIX.4.2 Logon body from the peer's side, framed with a real checksum.
    static std::vector<std::byte> peer_logon(session_role our_role, std::string_view length,
                                             std::string_view encrypt_method) {
        bool const acceptor = our_role == session_role::acceptor;
        std::string const body = logon_body_with_count(
            acceptor ? "TW" : "ISLD", acceptor ? "ISLD" : "TW", length, encrypt_method);
        std::string full =
            "8=FIX.4.2\x01"
            "9=" +
            std::to_string(body.size()) + "\x01" + body;
        unsigned int cs = 0;
        for (unsigned char c : full) {
            cs += c;
        }
        char csbuf[4];
        std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
        full += "10=" + std::string{csbuf} + "\x01";
        std::vector<std::byte> out(full.size());
        std::memcpy(out.data(), full.data(), full.size());
        return out;
    }

    // Opens a session in `role`, feeds the peer Logon, and returns the state after it.
    fsm_state state_after_peer_logon(session_role role, std::string_view length,
                                     std::string_view encrypt_method) {
        Session sess(engine, make_cfg(role));
        EXPECT_TRUE(run_sync(sess.open(), "LogonArmMalformedCount::open").has_value());
        fsm_state const expected_before =
            role == session_role::acceptor ? fsm_state::NotConnected : fsm_state::LogonSent;
        EXPECT_EQ(sess.state(), expected_before) << "the arm under test was not reached";
        auto const frame = peer_logon(role, length, encrypt_method);
        (void)run_sync(sess.on_inbound_frame(std::span<const std::byte>{frame}),
                       "LogonArmMalformedCount::feed");
        return sess.state();
    }
};

TEST_F(LogonArmMalformedCount, AcceptorTwinWellFormedCountZeroEncryptMethodReachesActive) {
    EXPECT_EQ(state_after_peer_logon(session_role::acceptor, "1", "0"), fsm_state::Active);
}

TEST_F(LogonArmMalformedCount, AcceptorTwinWellFormedCountNonZeroEncryptMethodIsNotEstablished) {
    auto const s = state_after_peer_logon(session_role::acceptor, "1", "2");
    EXPECT_NE(s, fsm_state::Active);
    EXPECT_NE(s, fsm_state::LogonReceived);
}

TEST_F(LogonArmMalformedCount, AcceptorMalformedCountDoesNotHideEncryptMethod) {
    auto const s = state_after_peer_logon(session_role::acceptor, "2", "2");
    EXPECT_NE(s, fsm_state::Active)
        << "the acceptor established on a Logon with 98=2 after a malformed 95 count";
    EXPECT_NE(s, fsm_state::LogonReceived);
}

TEST_F(LogonArmMalformedCount, InitiatorTwinWellFormedCountZeroEncryptMethodReachesActive) {
    EXPECT_EQ(state_after_peer_logon(session_role::initiator, "1", "0"), fsm_state::Active);
}

TEST_F(LogonArmMalformedCount, InitiatorTwinWellFormedCountNonZeroEncryptMethodIsNotEstablished) {
    EXPECT_NE(state_after_peer_logon(session_role::initiator, "1", "2"), fsm_state::Active);
}

TEST_F(LogonArmMalformedCount, InitiatorMalformedCountDoesNotHideEncryptMethod) {
    EXPECT_NE(state_after_peer_logon(session_role::initiator, "2", "2"), fsm_state::Active)
        << "the initiator established on a Logon ack with 98=2 after a malformed 95 count";
}

TEST_F(LogonArmMalformedCount, AcceptorCountRunningPastTheFrameIsNotEstablished) {
    auto const s = state_after_peer_logon(session_role::acceptor, "999", "2");
    EXPECT_NE(s, fsm_state::Active)
        << "the acceptor established on a Logon with 98=2 after an overrunning 95 count";
    EXPECT_NE(s, fsm_state::LogonReceived);
}

TEST_F(LogonArmMalformedCount, InitiatorCountRunningPastTheFrameIsNotEstablished) {
    EXPECT_NE(state_after_peer_logon(session_role::initiator, "999", "2"), fsm_state::Active)
        << "the initiator established on a Logon ack with 98=2 after an overrunning 95 count";
}

// 98=0 is the shape a conforming peer's Logon has, so only the count can refuse it.
TEST_F(LogonArmMalformedCount, AcceptorMalformedCountWithZeroEncryptMethodIsNotEstablished) {
    auto const s = state_after_peer_logon(session_role::acceptor, "2", "0");
    EXPECT_NE(s, fsm_state::Active)
        << "the acceptor established on a Logon carrying a malformed 95 count";
    EXPECT_NE(s, fsm_state::LogonReceived);
}

TEST_F(LogonArmMalformedCount, InitiatorMalformedCountWithZeroEncryptMethodIsNotEstablished) {
    EXPECT_NE(state_after_peer_logon(session_role::initiator, "2", "0"), fsm_state::Active)
        << "the initiator established on a Logon ack carrying a malformed 95 count";
}

// ── Zero global allocations (constitution §VIII.5) ───────────────────────────
//
// These scanners run on the inbound dispatch and persist paths. Each is called once
// to warm up, then again between the mallocnesia markers, where any global
// new/malloc makes alloc_guard_end() exit(1) under the
// `session_length_data_scanner_mallocnesia` ctest entry. Results are captured to
// locals and asserted after the window, because a failing assertion allocates. The
// hooks come from a real table_view, so non-standard tags are looked up in the
// dictionary inside the window; both frames carry a counted value holding SOH.
TEST(LengthDataScannersNoHeap, ScannersDoNotAllocate) {
    auto const dict = fixpp::test_support::make_minimal_dictionary();
    auto const tv = dict->as_table_view();
    auto const hooks = fixpp::wire::dict_hooks::for_table_view(tv);

    auto const header_frame = make_frame(
        "35=D\x01"
        "34=5\x01"
        "49=S\x01"
        "56=T\x01" +
        counted(354, 355,
                "x\x01"
                "34=99") +
        "58=ok\x01");
    auto const logon = make_frame(
        "35=A\x01"
        "34=1\x01"
        "49=TW\x01"
        "52=20240101-00:00:00.000\x01"
        "56=ISLD\x01"
        "98=0\x01"
        "108=30\x01"
        "554=real\x01" +
        counted(95, 96,
                "x\x01"
                "554=forged"));
    std::vector<std::byte> mask_buf(logon.size());

    auto const run = [&] {
        auto const h = detail::scan_frame_header(std::span<const std::byte>{header_frame}, hooks);
        auto const ids = detail::scan_first_frame_ids(std::span<const std::byte>{logon});
        auto const r =
            interpret_logon(std::span<const std::byte>{logon}, "TW", "ISLD", "FIX.4.4", hooks);
        bool const has_554 = frame_has_genuine_tag554(std::span<const std::byte>{logon}, hooks);
        std::copy(logon.begin(), logon.end(), mask_buf.begin());
        bool const masked = mask_tag554_same_length_inplace(std::span<std::byte>{mask_buf}, hooks);
        return h.msg_seq_num == "5" && ids.sender_comp_id == "TW" && r.has_value() &&
               r->password == std::string_view{"real"} && has_554 && masked;
    };
    ASSERT_TRUE(run()) << "warm-up: the scanners must agree before the window is measured";

    if (alloc_guard_start) alloc_guard_start();
    bool const ok = run();
    if (alloc_guard_end) alloc_guard_end();

    EXPECT_TRUE(ok);
}

}  // namespace fixpp::session::test
