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
// ScanFrameHeaderDifferential.* (tasks.md T013): the differential corpus of contract
// C-3 I-4 (research.md R-2 §1). The scan's fault record is compared with
// OffsetTable::build, the full parse's encoding oracle, on seeds and on every
// planted mutation of them. One TEST per mutation family, so a disagreement
// planted in one family's check (T014) is attributed to that family's cells.
//
// Anchors: data-model.md E-0/E-1; research.md R-1, R-2; contracts/unparseable-frame-disposition.md
//          C-3 I-3, I-4.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/framer.hpp>  // frame_view_slice_access
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>  // dict_hooks::for_table_view
#include <fixpp/wire/tag_scan.hpp>
#include <functional>
#include <memory_resource>
#include <optional>
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

// A dictionary declaring a Length+Data pair outside the standard table
// (CustomLen, then CustomData), so its hooks differ from dict_hooks::none().
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

// ═════════════════════════════════════════════════════════════════════════════
// Differential corpus (T013; contract C-3 I-4; research R-2 §1).
//
// A frame is a list of fields joined by SOH. A mutation replaces one field; the
// case's expectation is derived from the fields as planted -- the planted field's
// index and byte offset, and the fields before it -- never from either reader.
// The oracle is OffsetTable::build and its entries(), never find(34): the overlay
// can leave an occurrence unindexed while the build still succeeds.
//
// Tag mutations run at every field position. Count mutations run at every Length
// field of a pair; they fault only under hooks that declare the pair
// (dict_hooks::data_tag_for_length), and are clean on both sides otherwise.
// ═════════════════════════════════════════════════════════════════════════════

namespace {

using Fields = std::vector<std::string>;

struct Seed {
    char const* name;
    Fields fields;
};

// The admin seeds are the MsgTypes `is_admin_type` in Session::replay_outbound_range_
// (src/session/session.cpp) classifies as admin; re-derive with
// `grep -n "const auto is_admin_type" -A3 src/session/session.cpp`. NewOrderSingle
// is the application type; the last two carry a standard pair (SecureDataLen /
// SecureData, whose value holds a SOH) and the dictionary-only pair.
std::vector<Seed> const& seeds() {
    static std::vector<Seed> const all = {
        {.name = "Heartbeat",
         .fields = {"8=FIX.4.4", "9=0", "35=0", "34=2", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "112=TR", "10=000"}},
        {.name = "TestRequest",
         .fields = {"8=FIX.4.4", "9=0", "35=1", "34=3", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "112=TR", "10=000"}},
        {.name = "ResendRequest",
         .fields = {"8=FIX.4.4", "9=0", "35=2", "34=4", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "7=1", "16=0", "10=000"}},
        {.name = "Reject",
         .fields = {"8=FIX.4.4", "9=0", "35=3", "34=5", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "45=2", "373=0", "58=why", "10=000"}},
        {.name = "SequenceReset",
         .fields = {"8=FIX.4.4", "9=0", "35=4", "34=6", "49=SND", "43=Y",
                    "52=20240101-00:00:00.000", "122=20240101-00:00:00.000", "56=TGT", "123=Y",
                    "36=9", "10=000"}},
        {.name = "Logout",
         .fields = {"8=FIX.4.4", "9=0", "35=5", "34=7", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "58=bye", "10=000"}},
        {.name = "Logon",
         .fields = {"8=FIX.4.4", "9=0", "35=A", "34=1", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "98=0", "108=30", "141=Y", "383=4096", "464=N", "789=1", "10=000"}},
        {.name = "NewOrderSingle",
         .fields = {"8=FIX.4.4", "9=0", "35=D", "34=8", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "11=ORD", "55=IBM", "54=1", "10=000"}},
        {.name = "StandardPair",
         .fields = {"8=FIX.4.4", "9=0", "35=D", "34=9", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "90=5", "91=ab\001cd", "11=ORD", "10=000"}},
        {.name = "DictionaryOnlyPair",
         .fields = {"8=FIX.4.4", "9=0", "35=T", "34=10", "49=SND", "52=20240101-00:00:00.000",
                    "56=TGT", "5001=4", "5002=wxyz", "11=ORD", "10=000"}},
    };
    return all;
}

std::string join(Fields const& fields, bool trailing_soh = true) {
    std::string out;
    for (std::size_t k = 0; k < fields.size(); ++k) {
        out += fields[k];
        if (k + 1 < fields.size() || trailing_soh) {
            out += kSoh;
        }
    }
    return out;
}

std::uint32_t offset_of_index(Fields const& fields, std::size_t index) {
    std::size_t off = 0;
    for (std::size_t k = 0; k < index; ++k) {
        off += fields[k].size() + 1;
    }
    return static_cast<std::uint32_t>(off);
}

// A well-formed field's tag digits and value.
std::string tag_part(std::string const& field) { return field.substr(0, field.find('=')); }
std::string value_part(std::string const& field) { return field.substr(field.find('=') + 1); }
std::uint16_t tag_of(std::string const& field) {
    return static_cast<std::uint16_t>(std::stoul(tag_part(field)));
}

// The FrameHeader member each scanned tag fills, or nullopt for a tag it ignores.
std::optional<std::string_view> member_for(FrameHeader const& h, std::uint16_t tag) {
    switch (tag) {
        case 7:
            return h.begin_seqno;
        case 8:
            return h.begin_string;
        case 16:
            return h.end_seqno;
        case 34:
            return h.msg_seq_num;
        case 35:
            return h.msg_type;
        case 36:
            return h.new_seqno;
        case 43:
            return h.poss_dup_flag;
        case 49:
            return h.sender_comp_id;
        case 52:
            return h.sending_time;
        case 56:
            return h.target_comp_id;
        case 112:
            return h.test_req_id;
        case 122:
            return h.orig_sending_time;
        case 123:
            return h.gap_fill_flag;
        case 141:
            return h.reset_seqnum_flag;
        case 383:
            return h.max_message_size;
        case 464:
            return h.test_message_indicator;
        case 789:
            return h.next_expected_msg_seq_num;
        default:
            return std::nullopt;
    }
}

// Every tag the scan records, so "after the fault" can be checked for tags the
// seed never carried as well.
constexpr std::uint16_t kScannedTags[] = {7,  8,   16,  34,  35,  36,  43,  49, 52,
                                          56, 112, 122, 123, 141, 383, 464, 789};

// The oracle's side of one frame.
struct Oracle {
    // Declaration order is construction order: `table` borrows `arena` and `frame`.
    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::frame_view frame;
    fixpp::wire::OffsetTable table;

    Oracle(std::vector<std::byte> const& bytes, dict_hooks const& hooks)
        : frame(fixpp::wire::frame_view_slice_access::make(bytes.data(), bytes.size(), {})),
          table(frame, &arena, hooks) {}
};

std::string_view entry_value(std::vector<std::byte> const& bytes,
                             fixpp::wire::OffsetTable::entry const& e) {
    return {reinterpret_cast<char const*>(bytes.data()) + e.offset, e.length};
}

// A fault-free frame: both sides clean, and the fault references and the
// last-wins members agree with entries().
void expect_clean_and_agree(std::string const& wire, dict_hooks const& hooks) {
    Scanned const s = scan(wire, hooks);
    Oracle const o(s.bytes, hooks);
    EXPECT_EQ(s.h.fault, field_fault::none) << "the scan faulted at offset " << s.h.fault_offset;
    auto const status = o.table.build_status();
    EXPECT_TRUE(status.has_value()) << "OffsetTable::build failed with error "
                                    << (status ? 0 : static_cast<int>(status.error()));
    if (s.h.fault != field_fault::none || !status.has_value()) {
        return;
    }
    auto const entries = o.table.entries();
    std::string_view first_34;
    std::string_view last_34;
    std::string_view last_35;
    bool seen_34 = false;
    for (auto const& e : entries) {
        if (e.tag == 34) {
            if (!seen_34) {
                first_34 = entry_value(s.bytes, e);
                seen_34 = true;
            }
            last_34 = entry_value(s.bytes, e);
        }
        if (e.tag == 35) {
            last_35 = entry_value(s.bytes, e);
        }
    }
    bool const third_is_35 = entries.size() > 2 && entries[2].tag == 35;
    EXPECT_EQ(s.h.fault_ref_seq_num, first_34) << "fault_ref_seq_num vs the first 34 in entries()";
    EXPECT_EQ(s.h.msg_type_is_third, third_is_35)
        << "msg_type_is_third vs \"the third entries() element has tag 35\"";
    EXPECT_EQ(s.h.fault_ref_msg_type,
              third_is_35 ? entry_value(s.bytes, entries[2]) : std::string_view{})
        << "fault_ref_msg_type vs the third entries() element";
    EXPECT_EQ(s.h.msg_seq_num, last_34) << "C-3 I-3: msg_seq_num stays last-wins";
    EXPECT_EQ(s.h.msg_type, last_35) << "C-3 I-3: msg_type stays last-wins";
}

// Before any mutation: the seed must be clean on both sides under these hooks.
// Reports whether the check added a failure to the running test.
bool seed_is_clean(Seed const& seed, dict_hooks const& hooks) {
    SCOPED_TRACE(std::string{"seed "} + seed.name + " (unmutated)");
    auto const* result = ::testing::UnitTest::GetInstance()->current_test_info()->result();
    int const parts_before = result->total_part_count();
    expect_clean_and_agree(join(seed.fields), hooks);
    return result->total_part_count() == parts_before;
}

struct Planted {
    Fields fields;             // the mutated frame's fields
    std::size_t index;         // the faulting field
    field_fault kind;          // what the scan must record
    std::uint16_t length_tag;  // for length_data_mismatch, else 0
    fixpp::core::error error;  // what OffsetTable::build must fail with
    bool trailing_soh = true;  // false: the buffer ends inside the last field
};

// A planted fault: both sides fail, at the planted field, of the planted kind, and
// the scan's members are exactly those of the fields before it.
void expect_planted_fault(Planted const& p, dict_hooks const& hooks) {
    std::string const wire = join(p.fields, p.trailing_soh);
    Scanned const s = scan(wire, hooks);
    Oracle const o(s.bytes, hooks);

    EXPECT_EQ(s.h.fault, p.kind);
    EXPECT_EQ(s.h.fault_offset, offset_of_index(p.fields, p.index));
    EXPECT_EQ(s.h.fault_length_tag, p.length_tag);
    auto const status = o.table.build_status();
    ASSERT_FALSE(status.has_value()) << "OffsetTable::build accepted the planted fault";
    EXPECT_EQ(status.error(), p.error);

    for (std::uint16_t const tag : kScannedTags) {
        std::string_view expected;  // last-wins among the fields before the fault
        for (std::size_t k = 0; k < p.index; ++k) {
            if (tag_of(p.fields[k]) == tag) {
                expected = std::string_view{p.fields[k]}.substr(p.fields[k].find('=') + 1);
            }
        }
        EXPECT_EQ(member_for(s.h, tag).value_or("?"), expected) << "member for tag " << tag;
    }
    std::string_view first_34;
    for (std::size_t k = 0; k < p.index; ++k) {
        if (tag_of(p.fields[k]) == 34) {
            first_34 = std::string_view{p.fields[k]}.substr(p.fields[k].find('=') + 1);
            break;
        }
    }
    bool const third_is_35 = p.index > 2 && tag_of(p.fields[2]) == 35;
    EXPECT_EQ(s.h.fault_ref_seq_num, first_34);
    EXPECT_EQ(s.h.msg_type_is_third, third_is_35);
    EXPECT_EQ(s.h.fault_ref_msg_type,
              third_is_35 ? std::string_view{p.fields[2]}.substr(p.fields[2].find('=') + 1)
                          : std::string_view{});
}

// Runs `make(seed, k)` for every field k of every seed, under both hook sets, after
// checking the seed is clean under those hooks.
void for_each_tag_site(std::function<Planted(Fields const&, std::size_t)> const& make) {
    for_each_hooks([&](dict_hooks const& hooks) {
        for (Seed const& seed : seeds()) {
            if (!seed_is_clean(seed, hooks)) {
                ADD_FAILURE() << "seed " << seed.name << " is not clean; its mutations are skipped";
                continue;
            }
            for (std::size_t k = 0; k < seed.fields.size(); ++k) {
                SCOPED_TRACE(std::string{"seed "} + seed.name + ", field " + std::to_string(k) +
                             " (" + seed.fields[k] + ")");
                expect_planted_fault(make(seed.fields, k), hooks);
            }
        }
    });
}

// Runs a count mutation at every Length field of every seed. `count_for(data_value,
// fields, len_index)` returns the planted count. The planted fault is expected only
// when these hooks pair the Length with the field after it.
void for_each_count_site(
    std::function<std::uint32_t(Fields const&, std::size_t)> const& count_for) {
    for_each_hooks([&](dict_hooks const& hooks) {
        std::size_t sites = 0;
        for (Seed const& seed : seeds()) {
            if (!seed_is_clean(seed, hooks)) {
                ADD_FAILURE() << "seed " << seed.name << " is not clean; its mutations are skipped";
                continue;
            }
            for (std::size_t k = 0; k + 1 < seed.fields.size(); ++k) {
                std::uint16_t const len_tag = tag_of(seed.fields[k]);
                std::uint16_t const data_tag = tag_of(seed.fields[k + 1]);
                bool const declared = dict_hooks::for_table_view(pair_dict_table_view())
                                          .data_tag_for_length(len_tag) == data_tag;
                if (!declared) {
                    continue;  // not a Length+Data pair in any hook set this corpus uses
                }
                ++sites;
                SCOPED_TRACE(std::string{"seed "} + seed.name + ", Length field " +
                             std::to_string(k) + " (" + seed.fields[k] + ")");
                Fields mutated = seed.fields;
                mutated[k] = tag_part(seed.fields[k]) + "=" + std::to_string(count_for(mutated, k));
                if (hooks.data_tag_for_length(len_tag) == data_tag) {
                    expect_planted_fault({.fields = mutated,
                                          .index = k + 1,
                                          .kind = field_fault::length_data_mismatch,
                                          .length_tag = len_tag,
                                          .error = fixpp::core::error::wire_invalid_field_format},
                                         hooks);
                } else {
                    SCOPED_TRACE("these hooks do not pair it: the count is an ordinary value");
                    expect_clean_and_agree(join(mutated), hooks);
                }
            }
        }
        EXPECT_GT(sites, 0U) << "no Length field was mutated";
    });
}

// The byte length of the Data value after Length field `k`.
std::uint32_t data_length(Fields const& fields, std::size_t k) {
    return static_cast<std::uint32_t>(value_part(fields[k + 1]).size());
}

// A count that makes the Data value after Length field `k` end exactly at the end
// of the buffer. The count's own digits move where the value starts, so iterate
// to a fixed point.
std::uint32_t count_to_frame_end(Fields fields, std::size_t k) {
    std::uint32_t count = 0;
    for (int round = 0; round < 8; ++round) {
        fields[k] = tag_part(fields[k]) + "=" + std::to_string(count);
        std::string const wire = join(fields);
        std::size_t const vstart =
            offset_of_index(fields, k + 1) + tag_part(fields[k + 1]).size() + 1;
        auto const want = static_cast<std::uint32_t>(wire.size() - vstart);
        if (want == count) {
            return count;
        }
        count = want;
    }
    ADD_FAILURE() << "no fixed point for the frame-end count";
    return count;
}

}  // namespace

// ── The seeds and the accepted controls: fault-free, both sides agree ────────

TEST(ScanFrameHeaderDifferential, Seeds_CleanAndAgreeWithEntries) {
    for_each_hooks([](dict_hooks const& hooks) {
        for (Seed const& seed : seeds()) {
            SCOPED_TRACE(std::string{"seed "} + seed.name);
            expect_clean_and_agree(join(seed.fields), hooks);
        }
    });
}

TEST(ScanFrameHeaderDifferential, AcceptedControls_CleanOnBothSides) {
    ASSERT_EQ(dict_hooks::none().data_tag_for_length(95), 96U)
        << "precondition: RawDataLength/RawData is a standard pair under dict_hooks::none()";
    std::vector<std::pair<char const*, Fields>> const controls = {
        {"leading-zero tag", {"8=FIX.4.4", "9=0", "35=D", "034=2", "049=SND", "011=ORD", "10=000"}},
        {"empty ordinary value", {"8=FIX.4.4", "9=0", "35=D", "34=2", "58=", "10=000"}},
        {"non-numeric Length value, empty Data",
         {"8=FIX.4.4", "9=0", "35=D", "34=2", "90=abc", "91=", "10=000"}},
        {"non-numeric Length value, no Data",
         {"8=FIX.4.4", "9=0", "35=D", "34=2", "90=abc", "58=x", "10=000"}},
        {"Length last before the trailer", {"8=FIX.4.4", "9=0", "35=D", "34=2", "90=5", "10=000"}},
        {"unrelated tag after a Length",
         {"8=FIX.4.4", "9=0", "35=D", "34=2", "90=5", "58=x", "91=ab", "10=000"}},
        {"standard pair RawDataLength/RawData",
         {"8=FIX.4.4", "9=0", "35=D", "34=2", "95=3", "96=a\001b", "10=000"}},
        {"duplicate 34 and 35",
         {"8=FIX.4.4", "9=0", "35=D", "34=2", "49=SND", "34=9", "35=8", "10=000"}},
        {"field 3 is not 35", {"8=FIX.4.4", "9=0", "49=SND", "35=D", "34=2", "10=000"}},
    };
    for_each_hooks([&](dict_hooks const& hooks) {
        for (auto const& [name, fields] : controls) {
            SCOPED_TRACE(std::string{"control: "} + name);
            expect_clean_and_agree(join(fields), hooks);
        }
    });
}

// ── Tag mutation families, at every field position ───────────────────────────

TEST(ScanFrameHeaderDifferential, NonDigitTag_EveryPosition) {
    for_each_tag_site([](Fields const& seed, std::size_t k) {
        Fields f = seed;
        std::string const tag = tag_part(seed[k]);
        f[k] = tag.substr(0, 1) + "x" + tag.substr(1) + "=" + value_part(seed[k]);
        return Planted{.fields = f,
                       .index = k,
                       .kind = field_fault::malformed_tag,
                       .length_tag = 0,
                       .error = fixpp::core::error::wire_invalid_field_format};
    });
}

TEST(ScanFrameHeaderDifferential, TagAbove0xFFFF_EveryPosition) {
    for_each_tag_site([](Fields const& seed, std::size_t k) {
        Fields f = seed;
        f[k] = "65536=" + value_part(seed[k]);
        return Planted{.fields = f,
                       .index = k,
                       .kind = field_fault::malformed_tag,
                       .length_tag = 0,
                       .error = fixpp::core::error::wire_tag_out_of_range};
    });
}

TEST(ScanFrameHeaderDifferential, EmptyTag_EveryPosition) {
    for_each_tag_site([](Fields const& seed, std::size_t k) {
        Fields f = seed;
        f[k] = "=" + value_part(seed[k]);
        return Planted{.fields = f,
                       .index = k,
                       .kind = field_fault::malformed_tag,
                       .length_tag = 0,
                       .error = fixpp::core::error::wire_invalid_field_format};
    });
}

// The field keeps only its tag digits, so SOH comes before any '='.
TEST(ScanFrameHeaderDifferential, NoEqualsBeforeSoh_EveryPosition) {
    for_each_tag_site([](Fields const& seed, std::size_t k) {
        Fields f = seed;
        f[k] = tag_part(seed[k]);
        return Planted{.fields = f,
                       .index = k,
                       .kind = field_fault::malformed_tag,
                       .length_tag = 0,
                       .error = fixpp::core::error::wire_invalid_field_format};
    });
}

// The last field keeps only its tag digits and the buffer ends there, so the end
// comes before any '='.
TEST(ScanFrameHeaderDifferential, NoEqualsBeforeEndOfBuffer_LastField) {
    for_each_hooks([](dict_hooks const& hooks) {
        for (Seed const& seed : seeds()) {
            if (!seed_is_clean(seed, hooks)) {
                ADD_FAILURE() << "seed " << seed.name << " is not clean; its mutations are skipped";
                continue;
            }
            SCOPED_TRACE(std::string{"seed "} + seed.name);
            Fields f = seed.fields;
            std::size_t const last = f.size() - 1;
            f[last] = tag_part(seed.fields[last]);
            expect_planted_fault({.fields = f,
                                  .index = last,
                                  .kind = field_fault::malformed_tag,
                                  .length_tag = 0,
                                  .error = fixpp::core::error::wire_invalid_field_format,
                                  .trailing_soh = false},
                                 hooks);
        }
    });
}

// ── Count mutation families, at every Length field ───────────────────────────

// One byte short: the byte the count lands on is the value's last byte, not SOH.
TEST(ScanFrameHeaderDifferential, ShortCount_EveryLengthField) {
    for_each_count_site([](Fields const& f, std::size_t k) { return data_length(f, k) - 1U; });
}

// One byte long: the byte the count lands on is the next field's first byte.
TEST(ScanFrameHeaderDifferential, LongCount_EveryLengthField) {
    for_each_count_site([](Fields const& f, std::size_t k) { return data_length(f, k) + 1U; });
}

TEST(ScanFrameHeaderDifferential, CountPastTheFrame_EveryLengthField) {
    for_each_count_site([](Fields const&, std::size_t) { return std::uint32_t{999999}; });
}

TEST(ScanFrameHeaderDifferential, CountEndingExactlyAtTheFrameEnd_EveryLengthField) {
    for_each_count_site([](Fields const& f, std::size_t k) { return count_to_frame_end(f, k); });
}

}  // namespace fixpp::session::test
