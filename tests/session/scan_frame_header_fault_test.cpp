// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/scan_frame_header_fault_test.cpp
//
// 092-garbled-frame-reject — the header scan's first-fault record
// (specs/092-garbled-frame-reject/data-model.md E-0, E-1; research.md R-1).
//
// ScanFrameHeaderFault.* (tasks.md T009): one cell per fault kind and site of E-1.
//   - malformed_tag: a non-digit tag byte; a tag above 0xFFFF; no '=' before SOH; no
//     '=' before the end of the buffer; an empty tag ('=' first);
//   - length_data_mismatch: a count past the frame; a count ending exactly at the
//     frame end; a counted value not followed by SOH;
//   - the header identification: msg_type_is_third, the first 34 (fault_ref_seq_num)
//     and field 3's 35 (fault_ref_msg_type);
//   - contract C-3 I-3: a fault-free frame keeps last-wins for msg_seq_num/msg_type.
//
// Every expectation is derived from the bytes the cell plants, never from the scan:
// a fault offset is the position of the planted field in the buffer, and the fields
// before and after it are named in the cell. Every cell runs under a dictionary's
// hooks (one declaring a dictionary-only Length+Data pair) and under
// dict_hooks::none().
//
// Anchors: data-model.md E-0/E-1; research.md R-1; contracts/unparseable-frame-disposition.md
//          C-3 I-3.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/parser.hpp>  // dict_hooks::for_table_view
#include <fixpp/wire/tag_scan.hpp>
#include <functional>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/scan_frame_header.hpp"

namespace fixpp::session::test {
namespace {

using fixpp::session::detail::FrameHeader;
using fixpp::session::detail::scan_frame_header;
using fixpp::wire::dict_hooks;
using fixpp::wire::field_fault;

// A dictionary declaring one Length+Data pair outside the standard table
// (5001 -> 5002), so the dictionary hooks differ from dict_hooks::none().
constexpr std::string_view kPairDictXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
                                          R"(<fields>)"
                                          R"(<field number='8' name='BeginString' type='STRING'/>)"
                                          R"(<field number='9' name='BodyLength' type='INT'/>)"
                                          R"(<field number='10' name='CheckSum' type='STRING'/>)"
                                          R"(<field number='35' name='MsgType' type='STRING'/>)"
                                          R"(<field number='5001' name='CustomLen' type='LENGTH'/>)"
                                          R"(<field number='5002' name='CustomData' type='DATA'/>)"
                                          R"(</fields>)"
                                          R"(<messages>)"
                                          R"(<message name='TestMsg' msgtype='T' msgcat='app'>)"
                                          R"(<field name='CustomLen' required='N'/>)"
                                          R"(<field name='CustomData' required='N'/>)"
                                          R"(</message>)"
                                          R"(</messages></fix>)";

fixpp::dict::table_view const& pair_dict_table_view() {
    static std::pmr::monotonic_buffer_resource mr;
    static fixpp::dict::Dictionary const dict =
        fixpp::dict::XmlLoader{}.load_from_string(kPairDictXml, &mr);
    static fixpp::dict::table_view const tv = dict.as_table_view();
    return tv;
}

// Runs `body` under the dictionary hooks and under dict_hooks::none(), each traced.
void for_each_hooks(std::function<void(dict_hooks const&)> const& body) {
    dict_hooks const dict = dict_hooks::for_table_view(pair_dict_table_view());
    ASSERT_EQ(dict.data_tag_for_length(5001), 5002U)
        << "precondition: the dictionary hooks must declare the 5001 -> 5002 pair";
    {
        SCOPED_TRACE("hooks: dictionary");
        body(dict);
    }
    {
        SCOPED_TRACE("hooks: dict_hooks::none()");
        body(dict_hooks::none());
    }
}

constexpr char kSoh = '\x01';

// 8=FIX.4.4 | 9=<len> | body | 10=000 (the scan does not check the checksum).
std::string framed(std::string const& body) {
    return "8=FIX.4.4\x01"
           "9=" +
           std::to_string(body.size()) + "\x01" + body + "10=000\x01";
}

std::vector<std::byte> bytes_of(std::string const& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char const c : s) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

// The scanned bytes and their header. The header's views alias `bytes`, whose heap
// buffer a move of this struct keeps in place.
struct Scanned {
    std::vector<std::byte> bytes;
    FrameHeader h;
};

Scanned scan(std::string const& wire, dict_hooks const& hooks) {
    Scanned s{.bytes = bytes_of(wire), .h = {}};
    s.h = scan_frame_header(std::span<const std::byte>{s.bytes}, hooks);
    return s;
}

// The offset of the field that begins `field` (which starts right after a SOH).
std::uint32_t offset_of_field(std::string const& wire, std::string const& field) {
    auto const pos = wire.find(kSoh + field);
    EXPECT_NE(pos, std::string::npos) << "planted field not found: " << field;
    return static_cast<std::uint32_t>(pos + 1);
}

// The body shared by the fault-site cells: the fields before the planted fault
// (35, 34, 49) and after it (56, 52, 112). Every value is non-empty, so a
// populated member can be told from an empty one.
std::string body_around(std::string const& planted) {
    return std::string{"35=D\x01"} + "34=7\x01" + "49=SND\x01" + planted + "56=TGT\x01" +
           "52=20240101-00:00:00.000\x01" + "112=TR\x01";
}

// The fields before the planted fault are populated; the fields after it are empty.
void expect_stopped_between(FrameHeader const& h) {
    EXPECT_EQ(h.begin_string, "FIX.4.4");
    EXPECT_EQ(h.msg_type, "D");
    EXPECT_EQ(h.msg_seq_num, "7");
    EXPECT_EQ(h.sender_comp_id, "SND");
    EXPECT_TRUE(h.msg_type_is_third);
    EXPECT_EQ(h.fault_ref_msg_type, "D");
    EXPECT_EQ(h.fault_ref_seq_num, "7");
    EXPECT_TRUE(h.target_comp_id.empty()) << "56 follows the fault: " << h.target_comp_id;
    EXPECT_TRUE(h.sending_time.empty()) << "52 follows the fault: " << h.sending_time;
    EXPECT_TRUE(h.test_req_id.empty()) << "112 follows the fault: " << h.test_req_id;
}

void expect_malformed_tag_at(std::string const& planted_field_text) {
    for_each_hooks([&](dict_hooks const& hooks) {
        std::string const wire = framed(body_around(planted_field_text));
        auto const s = scan(wire, hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::malformed_tag);
        EXPECT_EQ(h.fault_length_tag, 0U);
        EXPECT_EQ(h.fault_offset, offset_of_field(wire, planted_field_text));
        expect_stopped_between(h);
    });
}

}  // namespace

// ── malformed_tag ────────────────────────────────────────────────────────────

TEST(ScanFrameHeaderFault, MalformedTag_NonDigitTagByte) {
    expect_malformed_tag_at("5x6=FORGED\x01");
}

TEST(ScanFrameHeaderFault, MalformedTag_TagAbove0xFFFF) {
    expect_malformed_tag_at("65536=FORGED\x01");
}

TEST(ScanFrameHeaderFault, MalformedTag_NoEqualsBeforeSoh) { expect_malformed_tag_at("999\x01"); }

TEST(ScanFrameHeaderFault, MalformedTag_EmptyTag) { expect_malformed_tag_at("=FORGED\x01"); }

// The buffer ends inside a field that has no '=': nothing after it exists, so the
// before-fault half is what the cell checks. The scan does not read BodyLength's value.
TEST(ScanFrameHeaderFault, MalformedTag_NoEqualsBeforeEndOfBuffer) {
    for_each_hooks([](dict_hooks const& hooks) {
        std::string const wire = std::string{"8=FIX.4.4\x01"} + "9=0\x01" + "35=D\x01" +
                                 "34=7\x01" + "49=SND\x01" + "999";
        auto const s = scan(wire, hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::malformed_tag);
        EXPECT_EQ(h.fault_length_tag, 0U);
        EXPECT_EQ(h.fault_offset, offset_of_field(wire, "999"));
        EXPECT_EQ(h.msg_type, "D");
        EXPECT_EQ(h.msg_seq_num, "7");
        EXPECT_EQ(h.sender_comp_id, "SND");
    });
}

// ── length_data_mismatch (SecureDataLen(90) / SecureData(91), a standard pair) ──

TEST(ScanFrameHeaderFault, LengthDataMismatch_CountPastTheFrame) {
    for_each_hooks([](dict_hooks const& hooks) {
        std::string const wire =
            framed(body_around("90=999\x01"
                               "91=x\x01"));
        auto const s = scan(wire, hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::length_data_mismatch);
        EXPECT_EQ(h.fault_length_tag, 90U);
        EXPECT_EQ(h.fault_offset, offset_of_field(wire, "91=x"));
        expect_stopped_between(h);
    });
}

TEST(ScanFrameHeaderFault, LengthDataMismatch_CountNotFollowedBySoh) {
    for_each_hooks([](dict_hooks const& hooks) {
        std::string const wire =
            framed(body_around("90=2\x01"
                               "91=xyz\x01"));
        auto const s = scan(wire, hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::length_data_mismatch);
        EXPECT_EQ(h.fault_length_tag, 90U);
        EXPECT_EQ(h.fault_offset, offset_of_field(wire, "91=xyz"));
        expect_stopped_between(h);
    });
}

// The counted value reaches exactly the end of the buffer, so no SOH terminates it.
// The scan does not read BodyLength's value.
TEST(ScanFrameHeaderFault, LengthDataMismatch_CountEndingExactlyAtTheFrameEnd) {
    for_each_hooks([](dict_hooks const& hooks) {
        std::string const wire = std::string{"8=FIX.4.4\x01"} + "9=0\x01" + "35=D\x01" +
                                 "34=7\x01" + "49=SND\x01" + "90=3\x01" + "91=abc";
        auto const s = scan(wire, hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::length_data_mismatch);
        EXPECT_EQ(h.fault_length_tag, 90U);
        EXPECT_EQ(h.fault_offset, offset_of_field(wire, "91=abc"));
        EXPECT_EQ(h.msg_type, "D");
        EXPECT_EQ(h.msg_seq_num, "7");
        EXPECT_EQ(h.sender_comp_id, "SND");
    });
}

// ── msg_type_is_third ────────────────────────────────────────────────────────

TEST(ScanFrameHeaderFault, MsgTypeIsThird_TrueWhenField3Is35) {
    for_each_hooks([](dict_hooks const& hooks) {
        auto const s = scan(framed("35=D\x01"
                                   "34=7\x01"
                                   "49=SND\x01"),
                            hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::none);
        EXPECT_TRUE(h.msg_type_is_third);
        EXPECT_EQ(h.fault_ref_msg_type, "D");
    });
}

TEST(ScanFrameHeaderFault, MsgTypeIsThird_FalseWhenField3IsNot35) {
    for_each_hooks([](dict_hooks const& hooks) {
        auto const s = scan(framed("49=SND\x01"
                                   "35=D\x01"
                                   "34=7\x01"),
                            hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::none);
        EXPECT_FALSE(h.msg_type_is_third);
        EXPECT_TRUE(h.fault_ref_msg_type.empty()) << h.fault_ref_msg_type;
        EXPECT_EQ(h.msg_type, "D") << "the last-wins msg_type is unchanged";
    });
}

TEST(ScanFrameHeaderFault, MsgTypeIsThird_FalseWhenField3IsTheFaultingField) {
    for_each_hooks([](dict_hooks const& hooks) {
        std::string const wire = framed(
            "X35=D\x01"
            "35=0\x01"
            "34=7\x01");
        auto const s = scan(wire, hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::malformed_tag);
        EXPECT_EQ(h.fault_offset, offset_of_field(wire, "X35=D"));
        EXPECT_FALSE(h.msg_type_is_third);
        EXPECT_TRUE(h.fault_ref_msg_type.empty()) << h.fault_ref_msg_type;
        EXPECT_TRUE(h.msg_type.empty()) << "35 follows the fault: " << h.msg_type;
        EXPECT_TRUE(h.msg_seq_num.empty()) << "34 follows the fault: " << h.msg_seq_num;
    });
}

TEST(ScanFrameHeaderFault, MsgTypeIsThird_FalseWhenTheScanFaultsBeforeField3) {
    for_each_hooks([](dict_hooks const& hooks) {
        std::string const wire =
            std::string{"8=FIX.4.4\x01"} + "9x=12\x01" + "35=D\x01" + "34=7\x01" + "10=000\x01";
        auto const s = scan(wire, hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::malformed_tag);
        EXPECT_EQ(h.fault_offset, offset_of_field(wire, "9x=12"));
        EXPECT_FALSE(h.msg_type_is_third);
        EXPECT_TRUE(h.fault_ref_msg_type.empty()) << h.fault_ref_msg_type;
        EXPECT_TRUE(h.fault_ref_seq_num.empty()) << h.fault_ref_seq_num;
        EXPECT_EQ(h.begin_string, "FIX.4.4");
    });
}

// ── fault_ref_seq_num / fault_ref_msg_type ────────────────────────────────────

// The first well-formed 34 is the Reject's 45, never a later one; msg_seq_num stays
// last-wins among the fields read before the fault.
TEST(ScanFrameHeaderFault, FaultRefSeqNum_IsTheFirst34) {
    for_each_hooks([](dict_hooks const& hooks) {
        auto const s = scan(framed("34=99\x01"
                                   "35=D\x01"
                                   "34=2\x01"
                                   "9x9=1\x01"),
                            hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::malformed_tag);
        EXPECT_EQ(h.fault_ref_seq_num, "99");
        EXPECT_EQ(h.msg_seq_num, "2");
    });
}

// A first 34 with an empty value is still the first 34: a later 34 does not replace it.
TEST(ScanFrameHeaderFault, FaultRefSeqNum_AnEmptyFirst34IsStillTheFirst) {
    for_each_hooks([](dict_hooks const& hooks) {
        auto const s = scan(framed("35=D\x01"
                                   "34=\x01"
                                   "34=5\x01"),
                            hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::none);
        EXPECT_EQ(h.fault_ref_seq_num, "");
        EXPECT_EQ(h.msg_seq_num, "5");
    });
}

// Field 3's 35 is the Reject's 372, never a later 35.
TEST(ScanFrameHeaderFault, FaultRefMsgType_IsField3NotALater35) {
    for_each_hooks([](dict_hooks const& hooks) {
        auto const s = scan(framed("35=D\x01"
                                   "34=2\x01"
                                   "35=4\x01"
                                   "9x9=1\x01"),
                            hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::malformed_tag);
        EXPECT_TRUE(h.msg_type_is_third);
        EXPECT_EQ(h.fault_ref_msg_type, "D");
        EXPECT_EQ(h.msg_type, "4");
    });
}

// ── C-3 I-3: a fault-free frame keeps last-wins ──────────────────────────────

TEST(ScanFrameHeaderFault, CleanFrame_Duplicate34_KeepsLastWins) {
    for_each_hooks([](dict_hooks const& hooks) {
        auto const s = scan(framed("35=0\x01"
                                   "34=1\x01"
                                   "49=SND\x01"
                                   "34=5\x01"),
                            hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::none);
        EXPECT_EQ(h.msg_seq_num, "5");
        EXPECT_EQ(h.fault_ref_seq_num, "1");
        EXPECT_EQ(h.sender_comp_id, "SND");
    });
}

TEST(ScanFrameHeaderFault, CleanFrame_Duplicate35_KeepsLastWins) {
    for_each_hooks([](dict_hooks const& hooks) {
        auto const s = scan(framed("35=0\x01"
                                   "34=1\x01"
                                   "35=D\x01"),
                            hooks);
        auto const& h = s.h;
        EXPECT_EQ(h.fault, field_fault::none);
        EXPECT_EQ(h.msg_type, "D");
        EXPECT_TRUE(h.msg_type_is_third);
        EXPECT_EQ(h.fault_ref_msg_type, "0");
    });
}

}  // namespace fixpp::session::test
