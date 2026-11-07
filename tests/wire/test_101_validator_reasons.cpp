// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/wire/test_101_validator_reasons.cpp
//
// 101-inbound-reject-fidelity, tasks.md T008 (W101-6, W101-7, W101-14): the validator's result
// for an undefined MsgType and an undefined tag, read as the Session reads it, through
// `wire_error_to_session_reject_reason` and `ref_tag_out` (FR-001, FR-002; research D1, D2).
// The tables are hand-built through `table_view_builder`, so a tag is "defined" exactly when a
// population operation named it (research D1's census, re-derived at tasks.md T003). The tags
// are custom (6001 up), and on a hand-built table only its operations count.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/reject_reason_map.hpp>
#include <fixpp/wire/validator.hpp>
#include <functional>
#include <memory_resource>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::dict::field_type;
using fixpp::dict::table_view;
using fixpp::dict::table_view_builder;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::Parser;

// A frame of `fields` (each `tag=value`, SOH-terminated) after 8 and 9, CheckSum computed.
std::vector<std::byte> make_frame(std::string_view fields) {
    std::string pre =
        "8=FIX.4.4\x01"
        "9=" +
        std::to_string(fields.size()) + "\x01" + std::string(fields);
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char cs[8];
    std::snprintf(cs, sizeof cs, "10=%03u\x01", sum % 256U);
    pre += cs;
    std::vector<std::byte> out(pre.size());
    std::memcpy(out.data(), pre.data(), pre.size());
    return out;
}

// What the Session's gate reads: the mapped SessionRejectReason (-1 when the frame validates),
// and the RefTagID the validator wrote (0 when it wrote none).
struct Verdict {
    int reason = -1;
    std::uint16_t ref = 0;
    bool parsed = false;
    std::optional<fixpp::core::error> error;  // the validator's own result, when it failed
};

Verdict judge(table_view tv, std::string_view fields) {
    const std::vector<std::byte> buf = make_frame(fields);
    std::array<std::byte, 8192> arena_buf{};
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    Verdict v;
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv) {
        ADD_FAILURE() << "make_frame_view failed";
        return v;
    }
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena);
    if (!mv) {
        ADD_FAILURE() << "parse failed";
        return v;
    }
    v.parsed = true;
    const dictionary_driven_validator val{std::move(tv)};
    std::array<std::byte, 4096> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch{scratch_buf.data(), scratch_buf.size(),
                                                std::pmr::null_memory_resource()};
    const auto r = val.validate(*mv, &scratch, &v.ref);
    v.reason = r ? -1 : fixpp::wire::wire_error_to_session_reject_reason(r.error());
    if (!r) {
        v.error = r.error();
    }
    return v;
}

// A table that defines MsgType "X" with the framing fields as its valid tags.
table_view_builder x_table() {
    table_view_builder b;
    for (std::uint16_t t : {8, 9, 10, 35}) {
        b.add_valid("X", t);
    }
    return b;
}

// ── W101-6: the MsgType check's position (after Step 0, before Step 1) ──────────────────

TEST(Validator101Reasons, W101_6_UndefinedMsgType_Reason11_NoRefTag) {
    const Verdict v = judge(x_table().build(), "35=U9\x01");
    ASSERT_TRUE(v.parsed);
    EXPECT_EQ(v.reason, 11);
    EXPECT_EQ(v.ref, 0) << "an undefined MsgType names no field";
    EXPECT_EQ(v.error, fixpp::core::error::wire_invalid_msg_type);
}

// The header-order check runs before the MsgType check, so its fault is the answer.
TEST(Validator101Reasons, W101_6_UndefinedMsgTypeAndHeaderOrder_Reason14) {
    const Verdict v = judge(x_table().build(),
                            "34=1\x01"
                            "35=U9\x01");
    ASSERT_TRUE(v.parsed);
    EXPECT_EQ(v.reason, 14);
    EXPECT_EQ(v.ref, 0);
}

// The MsgType check comes before the field walk: an undefined tag after it is not reached.
TEST(Validator101Reasons, W101_6_UndefinedMsgTypeAndUndefinedTag_Reason11) {
    const Verdict v = judge(x_table().build(),
                            "35=U9\x01"
                            "6099=x\x01");
    ASSERT_TRUE(v.parsed);
    EXPECT_EQ(v.reason, 11);
    EXPECT_EQ(v.ref, 0);
    EXPECT_EQ(v.error, fixpp::core::error::wire_invalid_msg_type);
}

// ── W101-7: which operations define a MsgType on a hand-built table (research D2) ───────

TEST(Validator101Reasons, W101_7_RequiredOnly_DefinesTheMsgType) {
    table_view_builder b;
    for (std::uint16_t t : {8, 9, 10, 35, 6001}) {
        b.add_required("X", t);
    }
    const Verdict v = judge(std::move(b).build(),
                            "35=X\x01"
                            "6001=v\x01");
    ASSERT_TRUE(v.parsed);
    EXPECT_EQ(v.reason, -1) << "validates";
}

TEST(Validator101Reasons, W101_7_ContextOnly_DoesNotDefineTheMsgType) {
    table_view_builder b;
    b.add_group_member_ctx("Y", {}, 6100, 6002);
    const Verdict v = judge(std::move(b).build(), "35=Y\x01");
    ASSERT_TRUE(v.parsed);
    EXPECT_EQ(v.reason, 11);
    EXPECT_EQ(v.ref, 0);
    EXPECT_EQ(v.error, fixpp::core::error::wire_invalid_msg_type);
}

// ── W101-14: US2 scenario 5, one cell per population-operation class (research D1) ──────
//
// The tag under test (6050) gets one role from one operation, and no message "X" lists it:
// 373=2 with 371 = the tag, because the table was told something about it. A tag no operation
// names answers 373=0. The framing-role class (`add_fixt_framing_tag`) has no cell here: Step 1(a)
// passes a tag `table_view::is_fixt_framing_tag` answers true for, on any message, so the
// validator cannot show whether that operation defined it; tasks.md T029's store cells read it.

constexpr std::uint16_t kTag = 6050;
constexpr std::uint16_t kOther = 6051;
constexpr std::uint16_t kNoTag = 6100;

struct OpCell {
    const char* op;
    std::function<void(table_view_builder&)> apply;
    std::string frame_tail;  // the field(s) carrying kTag
};

void PrintTo(const OpCell& c, std::ostream* os) { *os << c.op; }

std::vector<OpCell> op_cells() {
    const std::array<std::uint16_t, 0> root{};
    const std::string plain = std::to_string(kTag) + "=v\x01";
    return {
        {"a_add_valid", [](table_view_builder& b) { b.add_valid("Z", kTag); }, plain},
        {"a_add_valid_tag", [](table_view_builder& b) { b.add_valid_tag("Z", kTag); }, plain},
        {"b_add_required", [](table_view_builder& b) { b.add_required("Z", kTag); }, plain},
        {"b_add_required_tag", [](table_view_builder& b) { b.add_required_tag("Z", kTag); }, plain},
        {"c_set_field_type",
         [](table_view_builder& b) { b.set_field_type(kTag, field_type::String); }, plain},
        {"c_set_type", [](table_view_builder& b) { b.set_type(kTag, field_type::String); }, plain},
        {"d_add_enum", [](table_view_builder& b) { b.add_enum(kTag, "v"); }, plain},
        {"e_set_multi_value", [](table_view_builder& b) { b.set_multi_value(kTag); }, plain},
        {"f_add_group_member", [](table_view_builder& b) { b.add_group_member(kNoTag, kTag); },
         plain},
        {"f_add_group_required_member",
         [](table_view_builder& b) { b.add_group_required_member(kNoTag, kTag); }, plain},
        {"f_set_group_first", [](table_view_builder& b) { b.set_group_first(kNoTag, kTag); },
         plain},
        {"g_add_group_member_ctx",
         [root](table_view_builder& b) { b.add_group_member_ctx("Z", root, kNoTag, kTag); }, plain},
        {"g_add_group_required_member_ctx",
         [root](table_view_builder& b) {
             b.add_group_required_member_ctx("Z", root, kNoTag, kTag);
         },
         plain},
        {"g_set_group_first_ctx",
         [root](table_view_builder& b) { b.set_group_first_ctx("Z", root, kNoTag, kTag); }, plain},
        {"h_add_header_tag", [](table_view_builder& b) { b.add_header_tag(kTag); }, plain},
        // j: the Length tag of a pair the table declares; k: its Data tag. Each frame carries
        // only the tag under test, so the walk reads it as a plain field.
        {"j_set_length_pair_data_tag_length",
         [](table_view_builder& b) { b.set_length_pair_data_tag(kTag, kOther); },
         std::to_string(kTag) + "=1\x01"},
        {"k_set_length_pair_data_tag_data",
         [](table_view_builder& b) { b.set_length_pair_data_tag(kOther, kTag); }, plain},
        // l: the dictionary-built fill's forwarder (tasks.md T030), which names no role.
        {"l_add_defined_tag", [](table_view_builder& b) { b.add_defined_tag(kTag); }, plain},
    };
}

class Validator101OpClass : public testing::TestWithParam<OpCell> {};

TEST_P(Validator101OpClass, W101_14_DefinesItsTag_Reason2) {
    const OpCell& c = GetParam();
    table_view_builder b = x_table();
    c.apply(b);
    const Verdict v = judge(std::move(b).build(), "35=X\x01" + c.frame_tail);
    ASSERT_TRUE(v.parsed);
    EXPECT_EQ(v.reason, 2);
    EXPECT_EQ(v.ref, kTag);
}

INSTANTIATE_TEST_SUITE_P(Validator101Reasons, Validator101OpClass, testing::ValuesIn(op_cells()),
                         [](const testing::TestParamInfo<OpCell>& info) {
                             return std::string(info.param.op);
                         });

TEST(Validator101Reasons, W101_14_TagNoOpNames_Reason0) {
    const Verdict v = judge(x_table().build(), "35=X\x01" + std::to_string(kTag) + "=v\x01");
    ASSERT_TRUE(v.parsed);
    EXPECT_EQ(v.reason, 0);
    EXPECT_EQ(v.ref, kTag);
}

}  // namespace
