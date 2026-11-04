// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_peek_fixt_route.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-21's peek half (tasks.md T043): what
// src/session/peek_fixt_route.hpp reads from a frame's header, on the tables a Session holds as S.
// The comparands are FIXT11.xml's raw text, read here with no loader, dict::reify's dispatch on the
// same MsgTypes, and scan_frame_header on the same bytes. The dictionary half is
// tests/dictionary/test_099_fixt_header_tag_sets.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/peek_fixt_route.hpp"
#include "session/scan_frame_header.hpp"
#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

using detail::fixt_header_repeats_msg_type;
using detail::fixt_route_peek;
using detail::peek_fixt_route;
using detail::scan_frame_header;

// S's table as a Session holds it: the route set's entry for S (data-model E-6). One per process.
const dd::table_view& table_of(const DictPtr& s) {
    static std::map<const dd::Dictionary*, dd::table_view> tables;
    const auto it = tables.find(s.get());
    if (it != tables.end()) {
        return it->second;
    }
    return tables.emplace(s.get(), s->as_table_view(dd::fixt_framing)).first->second;
}

std::string soh(std::string text) {
    for (char& c : text) {
        if (c == '|') {
            c = '\x01';
        }
    }
    return text;
}

std::span<const std::byte> bytes(const std::string& frame) {
    return std::as_bytes(std::span{frame.data(), frame.size()});
}

fixt_route_peek peek_on(const DictPtr& s, const std::string& frame) {
    const dd::table_view& table = table_of(s);
    return peek_fixt_route(bytes(frame), wire::dict_hooks::for_table_view(table), table);
}

// T, A, L and a FIX 4.4 S: one rule on every shape.
std::vector<std::pair<const char*, DictPtr>> every_s() {
    return {{"T", fixt11()}, {"A", fix50sp2()}, {"L", latest()}, {"FIX44", fix44()}};
}

// A `|`-separated frame with a Length and a Data whose value may hold SOH.
std::string counted(const std::string& head, std::uint16_t length_tag, std::uint16_t data_tag,
                    const std::string& data, const std::string& tail) {
    return soh(head) + std::to_string(length_tag) + "=" + std::to_string(data.size()) + "\x01" +
           std::to_string(data_tag) + "=" + data + "\x01" + soh(tail);
}

const std::string kHead = "8=FIXT.1.1|9=0|";

TEST(W21Peek, BodylessFrame_StopsAt93) {
    const std::string frame = soh(kHead + "35=D|93=1|89=x|1128=10|");
    for (const auto& [name, s] : every_s()) {
        const fixt_route_peek p = peek_on(s, frame);
        EXPECT_TRUE(p.readable) << name;
        EXPECT_EQ(p.msg_type, "D") << name;
        EXPECT_FALSE(p.header_1128) << name << ": the peek read on past 93, to " << p.appl_ver_id;
    }
}

TEST(W21Peek, Fix44S_ReadsHeader1128) {
    ASSERT_FALSE(table_of(fix44()).is_header_tag(1128))
        << "FIX44 declares 1128: this cell no longer shows the standard set answering";
    const fixt_route_peek p = peek_on(fix44(), soh(kHead + "35=D|49=S|56=T|34=2|1128=6|11=x|"));
    EXPECT_TRUE(p.readable);
    EXPECT_TRUE(p.header_1128);
    EXPECT_EQ(p.appl_ver_id, "6");
}

// The embedded 1128 stands ahead of the real one, and the peek keeps the first it sees, so a walk
// that split the Data would answer 10.
TEST(W21Peek, StandardPairOnA_DataEmbeds1128_TheHeaders1128Reads) {
    ASSERT_FALSE(table_of(fix50sp2()).is_header_tag(90));
    const std::string data =
        "x\x01"
        "1128=10\x01"
        "y";
    const fixt_route_peek p =
        peek_on(fix50sp2(), counted(kHead + "35=D|", 90, 91, data, "1128=9|11=x|"));
    EXPECT_TRUE(p.readable);
    EXPECT_TRUE(p.header_1128);
    EXPECT_EQ(p.appl_ver_id, "9");
}

TEST(W21Peek, StandardPairOnA_DataEmbeds1128_NoHeader1128) {
    const std::string data =
        "x\x01"
        "1128=10\x01"
        "y";
    const fixt_route_peek p = peek_on(fix50sp2(), counted(kHead + "35=D|", 90, 91, data, "11=x|"));
    EXPECT_TRUE(p.readable);
    EXPECT_FALSE(p.header_1128) << p.appl_ver_id;
}

TEST(W21Peek, StandardPairOnA_DataOfNonTagBytes_Readable) {
    const std::string data =
        "\x01"
        "ab\x01"
        "cd";
    const fixt_route_peek p =
        peek_on(fix50sp2(), counted(kHead + "35=D|", 90, 91, data, "1128=9|11=x|"));
    EXPECT_TRUE(p.readable);
    EXPECT_EQ(p.appl_ver_id, "9");
}

// S's own header pair: only S's hooks split it, so a walk with no dictionary reads the embedded
// 1128, or faults on the non-tag bytes.
const DictPtr& fixt11_with_header_pair_and_tag() {
    static const DictPtr d =
        overlay_on("FIXT11.xml", "<fix type='FIXT' major='1' minor='1' servicepack='0'><header>" +
                                     kPairMembers +
                                     "<field name='VenueHdrScalar' required='N' /></header><fields>"
                                     "<field number='5030' name='VenuePairLen' type='LENGTH' />"
                                     "<field number='5031' name='VenuePairData' type='DATA' />"
                                     "<field number='5020' name='VenueHdrScalar' type='STRING' />"
                                     "</fields></fix>");
    return d;
}

TEST(W21Peek, SsHeaderPair_DataEmbeds1128_TheHeaders1128Reads) {
    const std::string data =
        "x\x01"
        "1128=10\x01"
        "y";
    const fixt_route_peek p = peek_on(fixt11_with_header_pair_and_tag(),
                                      counted(kHead + "35=D|", 5030, 5031, data, "1128=9|"));
    EXPECT_TRUE(p.readable);
    EXPECT_EQ(p.appl_ver_id, "9");
}

TEST(W21Peek, SsHeaderPair_DataOfNonTagBytes_Readable) {
    const std::string data =
        "\x01"
        "ab\x01"
        "cd";
    const fixt_route_peek p = peek_on(fixt11_with_header_pair_and_tag(),
                                      counted(kHead + "35=D|", 5030, 5031, data, "1128=9|"));
    EXPECT_TRUE(p.readable);
    EXPECT_EQ(p.appl_ver_id, "9");
}

TEST(W21Peek, AHeaderTagOnlyS_DeclaresWalksOn_OnPlainSItStops) {
    const std::string frame = soh(kHead + "35=D|5020=v|1128=9|");
    EXPECT_EQ(peek_on(fixt11_with_header_pair_and_tag(), frame).appl_ver_id, "9");
    EXPECT_FALSE(peek_on(fixt11(), frame).header_1128);
}

TEST(W21Peek, MsgTypeIsTheThirdFields) {
    const auto adjacent = peek_on(fixt11(), soh(kHead + "35=0|35=D|"));
    EXPECT_TRUE(adjacent.readable);
    EXPECT_EQ(adjacent.msg_type, "0");
    EXPECT_TRUE(adjacent.admin);

    const auto past_header = peek_on(fixt11(), soh(kHead + "35=D|11=x|35=0|"));
    EXPECT_TRUE(past_header.readable);
    EXPECT_EQ(past_header.msg_type, "D");
    EXPECT_FALSE(past_header.admin);
}

TEST(W21Peek, TheThirdFieldIsNot35_Unreadable) {
    for (const char* text :
         {"8=FIXT.1.1|35=D|9=0|", "8=FIXT.1.1|9=0|49=S|35=D|", "8=FIXT.1.1|9=0|", ""}) {
        const std::string frame = soh(text);
        const fixt_route_peek p = peek_on(fixt11(), frame);
        EXPECT_FALSE(p.readable) << text;
        EXPECT_TRUE(p.msg_type.empty()) << text;
        EXPECT_FALSE(scan_frame_header(bytes(frame)).msg_type_is_third) << text;
    }
}

// Whether or not 35 was read: a count that runs past the frame, and one before the 35.
TEST(W21Peek, AReadFaultBeforeTheStop_Unreadable) {
    for (const std::string& frame :
         {soh(kHead + "35=D|1128=9|90=99|91=x|"), soh("8=FIXT.1.1|90=99|91=x|35=D|")}) {
        const dd::table_view& table = table_of(fixt11());
        const auto hooks = wire::dict_hooks::for_table_view(table);
        const fixt_route_peek p = peek_fixt_route(bytes(frame), hooks, table);
        EXPECT_FALSE(p.readable);
        EXPECT_FALSE(p.header_1128);
        const auto scan = scan_frame_header(bytes(frame), hooks);
        EXPECT_TRUE(scan.fault != wire::field_fault::none || !scan.msg_type_is_third);
    }
}

TEST(W21Peek, AMalformedTagBeforeTheStop_Unreadable) {
    for (const char* tail : {"3a=1|", "=1|", "49", "65536=1|"}) {
        const std::string frame = soh(kHead + "35=D|" + tail);
        const dd::table_view& table = table_of(fixt11());
        const auto hooks = wire::dict_hooks::for_table_view(table);
        EXPECT_FALSE(peek_fixt_route(bytes(frame), hooks, table).readable) << tail;
        EXPECT_EQ(scan_frame_header(bytes(frame), hooks).fault, wire::field_fault::malformed_tag)
            << tail;
    }
}

TEST(W21Peek, AFaultPastTheStop_Readable) {
    for (const char* tail : {"11=x|90=99|91=x|", "11=x|3a=1|"}) {
        const fixt_route_peek p = peek_on(fixt11(), soh(kHead + "35=D|1128=9|" + tail));
        EXPECT_TRUE(p.readable) << tail;
        EXPECT_EQ(p.msg_type, "D") << tail;
        EXPECT_EQ(p.appl_ver_id, "9") << tail;
    }
}

// The scan's third-field value is what W-24's harness compares the peek with, on any fault-free
// frame, so a stop ahead of the third field does not lose it. Nothing past that stop is read.
TEST(W21Peek, AStopAheadOfTheThirdField_TheThirdFieldStillAnswers) {
    const std::string frame = soh("11=x|2=y|35=D|1128=9|");
    const fixt_route_peek p = peek_on(fixt11(), frame);
    EXPECT_TRUE(p.readable);
    EXPECT_EQ(p.msg_type, "D");
    EXPECT_FALSE(p.header_1128);
    const auto scan = scan_frame_header(bytes(frame));
    EXPECT_EQ(scan.fault, wire::field_fault::none);
    EXPECT_EQ(scan.fault_ref_msg_type, p.msg_type);
}

TEST(W21Peek, A1128PastTheStop_IsNotTheHeaders) {
    EXPECT_FALSE(peek_on(fixt11(), soh(kHead + "35=D|11=x|1128=6|")).header_1128);
}

TEST(W21Peek, AnEmpty1128IsPresent_AnAbsentOneIsNot) {
    const fixt_route_peek empty = peek_on(fixt11(), soh(kHead + "35=D|1128=|11=x|"));
    EXPECT_TRUE(empty.header_1128);
    EXPECT_TRUE(empty.appl_ver_id.empty());
    EXPECT_FALSE(peek_on(fixt11(), soh(kHead + "35=D|11=x|")).header_1128);
}

TEST(W21Peek, TheFirst1128Reads) {
    EXPECT_EQ(peek_on(fixt11(), soh(kHead + "35=D|1128=9|1128=6|")).appl_ver_id, "9");
}

// The peek's own literals, and then dict::reify's dispatch on the same frame. reify calls the same
// predicate, so the literals are what a change to that predicate cannot move.
TEST(W21Peek, AIsAdmin_DAndNAreApplication) {
    EXPECT_TRUE(peek_on(fixt11(), soh(kHead + "35=A|")).admin);
    EXPECT_FALSE(peek_on(fixt11(), soh(kHead + "35=D|")).admin);
    EXPECT_FALSE(peek_on(fixt11(), soh(kHead + "35=n|")).admin);
}

TEST(W21Peek, AdminAgreesWithReifysDispatchOnTheSameMsgType) {
    constexpr dd::version_profile kProfile{dd::session_version::vt11, dd::application_version::v44,
                                           true, 0};
    using Kind = dd::resolved_message_version::kind;
    for (const char* mt : {"0", "1", "2", "3", "4", "5", "A", "D", "8", "n"}) {
        const std::string frame = wire("FIXT.1.1", std::string{"35="} + mt +
                                                       "\x01"
                                                       "49=S\x01"
                                                       "56=T\x01"
                                                       "34=2\x01");
        std::pmr::monotonic_buffer_resource arena;
        wire::pmr_carry_buffer carry{frame.size(), &arena};
        wire::Framer framer{};
        wire::frame_view fvs[1]{};
        auto framed = framer.feed(bytes(frame), carry, std::span<wire::frame_view>{fvs, 1});
        ASSERT_TRUE(framed && !framed->empty()) << mt;
        const wire::MessageView<wire::access_mode::Index> view{fvs[0], &arena};
        const auto handle = dd::reify(view, kProfile, &arena);
        ASSERT_TRUE(handle.has_value()) << mt << ": reify answered nothing to compare with";
        EXPECT_EQ(peek_on(fixt11(), frame).admin, handle->version().k == Kind::session_admin) << mt;
    }
}

// FIXT11.xml's raw text: a named element of `section`, by name through its `<fields>`.
std::vector<std::uint16_t> raw_fixt11_tags(const std::string& section) {
    std::ifstream in{std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIXT11.xml", std::ios::binary};
    const std::string xml{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    std::map<std::string, std::uint16_t> by_name;
    const std::regex def{"<field number='([0-9]+)' name='([A-Za-z0-9]+)'"};
    for (std::sregex_iterator it{xml.begin(), xml.end(), def}, end; it != end; ++it) {
        by_name[(*it)[2].str()] = static_cast<std::uint16_t>(std::stoul((*it)[1].str()));
    }
    const auto from = xml.find("<" + section + ">");
    const auto to = xml.find("</" + section + ">");
    EXPECT_NE(from, std::string::npos);
    EXPECT_NE(to, std::string::npos);
    const std::string block = xml.substr(from, to - from);
    std::vector<std::uint16_t> tags;
    const std::regex member{"<(?:field|group) name='([A-Za-z0-9]+)'"};
    for (std::sregex_iterator it{block.begin(), block.end(), member}, end; it != end; ++it) {
        const auto found = by_name.find((*it)[1].str());
        EXPECT_NE(found, by_name.end()) << (*it)[1].str();
        if (found != by_name.end()) {
            tags.push_back(found->second);
        }
    }
    return tags;
}

TEST(W21Peek, EveryTagOfTheRawHeader_TheWalkReadsOnToA1128) {
    const auto tags = raw_fixt11_tags("header");
    ASSERT_GT(tags.size(), 25U) << "the raw read did not reach the header";
    for (const auto& [name, s] : every_s()) {
        for (const std::uint16_t tag : tags) {
            const auto p = peek_on(s, soh(kHead + "35=D|" + std::to_string(tag) + "=1|1128=9|"));
            EXPECT_TRUE(p.readable) << name << " " << tag;
            // 1128 is itself a header tag, and the first one reads.
            EXPECT_EQ(p.appl_ver_id, tag == 1128 ? "1" : "9") << name << " " << tag;
        }
    }
}

TEST(W21Peek, EveryTagOfTheRawTrailer_TheWalkStops) {
    const auto tags = raw_fixt11_tags("trailer");
    ASSERT_EQ(tags.size(), 3U) << "the raw read did not reach the trailer";
    for (const auto& [name, s] : every_s()) {
        for (const std::uint16_t tag : tags) {
            const auto p = peek_on(s, soh(kHead + "35=D|" + std::to_string(tag) + "=1|1128=9|"));
            EXPECT_TRUE(p.readable) << name << " " << tag;
            EXPECT_FALSE(p.header_1128) << name << " " << tag;
        }
    }
}

TEST(W21Peek, Tag370_TheWalkReadsOnToA1128) {
    for (const auto& [name, s] : every_s()) {
        EXPECT_EQ(peek_on(s, soh(kHead + "35=D|370=x|1128=4|")).appl_ver_id, "4") << name;
    }
}

bool repeats(const std::string& frame) {
    const dd::table_view& table = table_of(fixt11());
    return fixt_header_repeats_msg_type(bytes(frame), wire::dict_hooks::for_table_view(table),
                                        table);
}

TEST(W21PeekColdPath, ASecond35InTheHeaderOnly) {
    EXPECT_TRUE(repeats(soh(kHead + "35=0|35=D|")));
    EXPECT_TRUE(repeats(soh(kHead + "35=D|49=S|35=0|")));
    EXPECT_FALSE(repeats(soh(kHead + "35=D|11=x|35=0|")));
    EXPECT_FALSE(repeats(soh(kHead + "35=D|49=S|")));
    const std::string data =
        "x\x01"
        "35=0\x01"
        "y";
    EXPECT_FALSE(repeats(counted(kHead + "35=D|", 90, 91, data, "11=x|")));
}

}  // namespace
}  // namespace fixpp::session::test099
