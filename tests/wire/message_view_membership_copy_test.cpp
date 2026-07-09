// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/message_view_membership_copy_test.cpp
//
// 066-dict-backed-inbound-parse T003 — MessageView::membership_copy():
// mechanism (b), the ONE internal accessor shared by clone
// (fixpp_msg_clone) and reify (owning_message_handle) to propagate a
// dict-backed view's membership into an OWNED, independently-lifetimed
// fixpp::dict::table_view.
//
// Anchors:
//   data-model.md "Session inbound table_view" / "Reify owning handle owned
//     table_view" (mechanism (b), accessor precondition).
//   contracts/inbound-parse.md C4 (clone/reify read identically to source;
//     the copy is self-contained and safely outlives the source
//     session/Dictionary — table_view.hpp:185-192, :204,221).
//
// Proves:
//  (a) the copy answers membership identically to the live source.
//  (b) the copy OUTLIVES the source `Dictionary` AND the source
//      `table_view` — both are explicitly destroyed mid-test (not merely
//      left to fall out of scope at test end), and the copy still answers
//      correctly afterward. This is the load-bearing lifetime claim behind
//      mechanism (b); meaningful only under ASan (a broken shallow-copy
//      mutation manifests as heap-use-after-free / double-free there) — run
//      this TU under the `linux-clang-asan` preset.
//  (c) a dict-free source (opaque_dict_ == nullptr) yields an empty copy —
//      the documented degenerate case.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::dict::Dictionary;
using fixpp::dict::table_view;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// A group-registering dict: message "U" carries group NoThings(100) ->
// [ThingA(200) delimiter, ThingB(150)]. Same shape as
// validator_production_table_view_test.cpp's load_group_dict (delimiter is
// NOT the lowest-tag member — mirrors real dicts, e.g. FIX44 NoPartyIDs).
Dictionary load_group_dict(std::pmr::memory_resource* mr) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='8'  name='BeginString' type='STRING'/>)"
        R"(<field number='9'  name='BodyLength'  type='LENGTH'/>)"
        R"(<field number='10' name='CheckSum'    type='STRING'/>)"
        R"(<field number='35' name='MsgType'     type='STRING'/>)"
        R"(<field number='60' name='TransactTime' type='STRING'/>)"
        R"(<field number='100' name='NoThings'   type='NUMINGROUP'/>)"
        R"(<field number='150' name='ThingB'     type='INT'/>)"
        R"(<field number='200' name='ThingA'     type='STRING'/>)"
        R"(</fields>)"
        R"(<messages>)"
        R"(<message name='Things' msgtype='U' msgcat='app'>)"
        R"(  <field name='BeginString'   required='N'/>)"
        R"(  <field name='BodyLength'    required='N'/>)"
        R"(  <field name='CheckSum'      required='N'/>)"
        R"(  <field name='MsgType'       required='N'/>)"
        R"(  <field name='TransactTime'  required='N'/>)"
        R"(  <group name='NoThings' required='N'>)"
        R"(    <field name='ThingA' required='N'/>)"  // 200 = delimiter
        R"(    <field name='ThingB' required='N'/>)"  // 150
        R"(  </group>)"
        R"(</message>)"
        R"(</messages>)"
        R"(</fix>)";
    return fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
}

std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

constexpr std::string_view kFrameBody =
    "35=U\x01"
    "100=1\x01"
    "200=x\x01"
    "150=7\x01"
    "60=trailing\x01";

}  // namespace

// ── (a) membership identity: the copy answers membership identically ────────
TEST(MessageViewMembershipCopy, CopyAnswersMembershipIdenticallyToSource) {
    std::array<std::byte, 2u * 1024u * 1024u> dict_buf{};
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.data(), dict_buf.size()};
    auto dict = std::make_unique<Dictionary>(load_group_dict(&dict_mr));
    auto tv = std::make_unique<table_view>(dict->as_table_view());

    auto frame = make_frame(kFrameBody);
    auto fv = fixpp::wire::test::make_frame_view(frame);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Index> parser{*tv};
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    table_view owned = mv->membership_copy();

    // Membership identity: same answers as the live source, for both a
    // member field AND a non-member field.
    EXPECT_TRUE(tv->field_valid_for("U", 200));
    EXPECT_EQ(tv->field_valid_for("U", 200), owned.field_valid_for("U", 200));
    EXPECT_EQ(tv->field_valid_for("U", 9999), owned.field_valid_for("U", 9999));
    EXPECT_EQ(tv->group_first_field(100), owned.group_first_field(100));

    auto src_members = tv->group_member_tags(100);
    auto dst_members = owned.group_member_tags(100);
    ASSERT_EQ(src_members.size(), dst_members.size());
    EXPECT_TRUE(std::equal(src_members.begin(), src_members.end(), dst_members.begin()));
}

// ── (b) outlives the source Dictionary AND the source table_view ───────────
TEST(MessageViewMembershipCopy, CopyOutlivesSourceDictionaryAndTableView) {
    std::array<std::byte, 2u * 1024u * 1024u> dict_buf{};
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.data(), dict_buf.size()};
    auto dict = std::make_unique<Dictionary>(load_group_dict(&dict_mr));
    auto tv = std::make_unique<table_view>(dict->as_table_view());

    auto frame = make_frame(kFrameBody);
    auto fv = fixpp::wire::test::make_frame_view(frame);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Index> parser{*tv};
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    table_view owned = mv->membership_copy();

    // Real destruction, mid-test — not merely end-of-scope at test exit.
    dict.reset();
    tv.reset();

    // The copy is self-contained: still answers correctly after BOTH the
    // source Dictionary and the source table_view are gone. Under ASan, a
    // shallow-copy (aliasing) accessor would read/free already-freed heap
    // storage here.
    EXPECT_TRUE(owned.field_valid_for("U", 200));
    EXPECT_TRUE(owned.field_valid_for("U", 150));
    EXPECT_FALSE(owned.field_valid_for("U", 9999));
    EXPECT_EQ(owned.group_first_field(100), std::uint16_t{200});
    auto members = owned.group_member_tags(100);
    EXPECT_EQ(members.size(), 2u);
}

// ── (c) dict-free source -> empty copy (documented degenerate case) ────────
TEST(MessageViewMembershipCopy, DictFreeSourceYieldsEmptyCopy) {
    auto frame = make_frame(kFrameBody);
    auto fv = fixpp::wire::test::make_frame_view(frame);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Index> parser{};  // default ctor: opaque_dict_ == nullptr
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    table_view owned = mv->membership_copy();
    EXPECT_FALSE(owned.field_valid_for("U", 200));
    EXPECT_EQ(owned.group_first_field(100), std::uint16_t{0});
    EXPECT_TRUE(owned.group_member_tags(100).empty());
}
