// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/length_data_session_scanner_test.cpp — fixpp#426
//
// The dictionary-free session scanners must treat a counted Data value as one
// field. Each case puts `<SOH><tag>=<forged>` inside a standard Data value whose
// Length makes it well-formed, and asserts the scanner does not see the forged
// field. On the unfixed tree every scanner here splits the value at the SOH and
// keeps the LAST value per tag, so each case is RED.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/logon_credentials.hpp>
#include <fixpp/wire/parser.hpp>  // dict_hooks::for_table_view
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/scan_first_frame_ids.hpp"
#include "session/scan_frame_header.hpp"
#include "support/alloc_guard_markers.hpp"
#include "support/minimal_dictionary.hpp"

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

TEST(LengthDataSessionScanner, InterpretLogonStopsAtAMalformedCount) {
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
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->password.has_value()) << "a field after a malformed count must not be read";
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
