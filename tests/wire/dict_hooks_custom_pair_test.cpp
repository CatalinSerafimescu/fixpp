// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/dict_hooks_custom_pair_test.cpp — fixpp#426 (design §3, D-2).
//
// Witnesses for `wire::dict_hooks` carrying a dictionary's own Length+Data
// pair through every scanner it reaches: Index, Iter, both
// `OffsetTable::nested_group_slices` overloads, the `wire::get(span, tag,
// hooks, gen)` free helper (the shape a generated group-entry reader calls
// through `entry_context::hooks`), and `MessageView::membership_copy()`.
//
// The dictionary declares TWO custom pairs — LENGTH 5001/DATA 5002 used only
// at the message top level, and a DISTINCT LENGTH 5011/DATA 5012 used only
// inside a NESTED group (NoOuter(7001) > NoCustom(6001)) — rather than
// reusing one tag pair at both structural depths. That is a structural
// requirement, not a simplification: `message_fields()` dedupes a message's
// FieldRef list by tag, first-seen-wins (xml_loader.cpp, the stable-sort+
// unique step below `collect_messages`), so a tag declared both at message
// root and inside a group collapses to the ROOT FieldRef — the nested
// occurrence's `group_no_tag` is silently discarded, `NoCustom` never
// registers it as a member, and `consume_group_extent`'s outer walk (which
// checks membership of every entry against the group it is inside,
// offset_table.cpp) breaks the instance one field early, truncating the
// outer group's slice before the pair is reached. This is unrelated to
// `dict_hooks` — it reproduces with any ordinary reused-tag field — so two
// distinct tag pairs are used here to isolate the dict_hooks behaviour under
// test from that pre-existing per-message dedup rule.
//
// Each Data value carries an embedded SOH followed by a forged `58=` (Text)
// field; every witness below asserts the forged field is never observed as
// its own entry and the exact Data bytes come back untouched.
//
// Precedence witnesses (design §3's lookup rule — standard first, a
// dictionary pair applies only when NEITHER of its tags is a standard pair
// tag on either side) use two further hand-built dictionaries: one that
// tries to re-pair the standard RawDataLength(95) with a custom Data tag, and
// one that tries to pair a custom Length tag with the standard RawData(96).
//
// Mutation procedure: make `OffsetTable::build` and `field_iterator::advance`
// call `detail::standard_data_tag_for_length` instead of
// `hooks_.data_tag_for_length`. Every custom-pair witness below must then fail;
// the two precedence tests must not.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/wire_test_hooks.hpp"  // fixpp#426 r11 T-1: the nested-cache introspection seam

namespace {

using fixpp::dict::table_view;
using fixpp::dict::table_view_builder;
using fixpp::wire::access_mode;
using fixpp::wire::dict_hooks;
using fixpp::wire::Parser;

// ── Shared frame builder (mirrors group_slice_trailing_soh_test.cpp / T008)
// — a fixed "10=000" trailer; frame_view_factory locates fields structurally
// and does not verify the checksum VALUE.
std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

constexpr std::string_view kCustomPairXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    // Two LENGTH/DATA adjacencies (XmlLoader::detect_length_pairs, fixpp#426
    // design §2) using DISTINCT tag pairs — see the file banner above for why
    // one pair cannot be reused at both structural depths.
    R"(<field number='5001' name='CustomLen' type='LENGTH'/>)"
    R"(<field number='5002' name='CustomData' type='DATA'/>)"
    R"(<field number='5011' name='CustomLen2' type='LENGTH'/>)"
    R"(<field number='5012' name='CustomData2' type='DATA'/>)"
    R"(<field number='7001' name='NoOuter' type='NUMINGROUP'/>)"
    R"(<field number='7002' name='OuterID' type='STRING'/>)"
    R"(<field number='6001' name='NoCustom' type='NUMINGROUP'/>)"
    R"(<field number='6002' name='CustomEntryID' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='TestMsg' msgtype='T' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    // Top-level use of the first pair.
    R"(<field name='CustomLen' required='N'/>)"
    R"(<field name='CustomData' required='N'/>)"
    // Nested use: NoOuter > NoCustom, the SECOND pair inside the inner group.
    R"(<group name='NoOuter' required='N'>)"
    R"(<field name='OuterID' required='N'/>)"
    R"(<group name='NoCustom' required='N'>)"
    R"(<field name='CustomEntryID' required='N'/>)"
    R"(<field name='CustomLen2' required='N'/>)"
    R"(<field name='CustomData2' required='N'/>)"
    R"(</group></group>)"
    R"(</message>)"
    R"(</messages></fix>)";

table_view load_custom_pair_dict(std::pmr::memory_resource* mr) {
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kCustomPairXml, mr);
    return dict.as_table_view();
}

// Top-level "5002=" value: 'x' SOH "58=F" — adjacent string-literal
// concatenation so the `\x01` escape does NOT swallow the following "58" as
// hex digits (a bare `"x\x0158=F"` would be misparsed by the compiler).
constexpr std::string_view kTopValue =
    "x"
    "\x01"
    "58=F";
// Nested-in-group "5012=" value — a distinct forged tag/value so the two
// occurrences are independently identifiable.
constexpr std::string_view kNestedValue =
    "y"
    "\x01"
    "58=G";
static_assert(kTopValue.size() == 6);
static_assert(kNestedValue.size() == 6);

std::vector<std::byte> make_custom_pair_frame() {
    std::string body = "35=T\x01";
    body += "5001=6\x01";
    body += "5002=";
    body += kTopValue;
    body += "\x01";
    body += "7001=1\x01";
    body += "7002=O1\x01";
    body += "6001=1\x01";
    body += "6002=E1\x01";
    body += "5011=6\x01";
    body += "5012=";
    body += kNestedValue;
    body += "\x01";
    return make_raw_frame(body);
}

}  // namespace

TEST(DictHooksCustomPair, CustomPairSplitsThroughEveryDictAwarePath) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto tv = load_custom_pair_dict(&dict_mr);

    // Preconditions: both dictionary pairs actually registered. Without
    // these, a silent `detect_length_pairs` miss would make the whole
    // witness pass by vacuity — no pair registered means the scanner splits
    // at the embedded SOH, `find(58)` finds the forged field, and that
    // assertion fires below for the WRONG reason.
    ASSERT_EQ(tv.length_pair_data_tag(5001), 5002U)
        << "precondition: the top-level pair must be registered";
    ASSERT_EQ(tv.length_pair_data_tag(5011), 5012U)
        << "precondition: the nested-group pair must be registered";

    // Precondition: the structural fix this file's banner describes actually
    // holds — 5012 is a registered member of NoCustom(6001) under NoOuter's
    // own child context. This is exactly the check that failed before this
    // file used two distinct tag pairs (a same-tag reuse across structural
    // depths collapses in `message_fields()`'s first-seen-wins dedup).
    {
        auto const hooks_probe = fixpp::wire::dict_hooks::for_table_view(tv);
        fixpp::wire::group_context const under_outer =
            fixpp::wire::group_context{.msg_type = "T"}.pushed(7001);
        ASSERT_TRUE(
            hooks_probe.group_member_fn()(hooks_probe.opaque_dict(), under_outer, 6001, 5011))
            << "precondition: 5011 must be a registered member of NoCustom under [7001]";
        ASSERT_TRUE(
            hooks_probe.group_member_fn()(hooks_probe.opaque_dict(), under_outer, 6001, 5012))
            << "precondition: 5012 must be a registered member of NoCustom under [7001]";
    }

    auto buf = make_custom_pair_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "make_frame_view failed";

    // ── Index path ──────────────────────────────────────────────────────
    Parser<access_mode::Index> parser{tv};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "parser.parse failed";

    // The forged 58 field must never appear as its own entry — proving the
    // Data value's embedded SOH did not split it out.
    EXPECT_FALSE(mv->offsets().find(58).has_value())
        << "Index: a forged 58 field must not appear as a separate entry";

    // find() returns the FIRST occurrence — the top-level 5002.
    auto top_5002 = mv->offsets().find(5002);
    ASSERT_TRUE(top_5002.has_value());
    std::string_view const top_bytes{
        reinterpret_cast<char const*>(mv->bytes().data() + top_5002->offset), top_5002->length};
    EXPECT_EQ(top_bytes, kTopValue) << "Index: top-level 5002 must come back byte-exact";

    // Parsing continued correctly past the counted value: the group count
    // field is found (proves the scanner did not desync on the value).
    EXPECT_TRUE(mv->offsets().find(7001).has_value())
        << "Index: parsing must continue correctly past the counted 5002 value";

    // ── The Index view's own field_iterator ─────────────────────────────
    // MessageView<Index>::begin()/end() pass this view's own hooks_
    // (parser.hpp), so the SAME dict-aware split applies to the field_iterator
    // walk. ⚠️ This is NOT `Parser<Iter>::parse_iter()` — that surface is
    // covered by DictBackedIterParser below, which is where Gate B r8 P-1 found
    // the bundle being dropped.
    bool iter_saw_58 = false;
    bool iter_saw_top_5002 = false;
    bool iter_saw_nested_5012 = false;
    for (auto it = mv->begin(); !(it == mv->end()); ++it) {
        auto const& f = *it;
        if (f.tag == 58) {
            iter_saw_58 = true;
        }
        if (f.tag == 5002) {
            std::string_view const v{reinterpret_cast<char const*>(f.value.data()), f.value.size()};
            EXPECT_EQ(v, kTopValue) << "Iter: top-level 5002 must be yielded byte-exact";
            iter_saw_top_5002 = true;
        }
        if (f.tag == 5012) {
            std::string_view const v{reinterpret_cast<char const*>(f.value.data()), f.value.size()};
            EXPECT_EQ(v, kNestedValue) << "Iter: nested-in-group 5012 must be yielded byte-exact";
            iter_saw_nested_5012 = true;
        }
    }
    EXPECT_FALSE(iter_saw_58) << "Iter: a forged 58 field must not appear as a separate entry";
    EXPECT_TRUE(iter_saw_top_5002) << "Iter: top-level 5002 must be yielded";
    EXPECT_TRUE(iter_saw_nested_5012) << "Iter: nested-in-group 5012 must be yielded";

    // ── nested_group_slices: BOTH overloads ────────────────────────────
    auto outer = mv->offsets().group_slices(7001);
    ASSERT_EQ(outer.size(), 1U);
    auto const& outer0 = outer[0];

    fixpp::wire::group_context const outer_ctx{.msg_type = "T"};

    // 6-arg overload: explicit caller-supplied hooks.
    auto explicit_hooks_result =
        mv->offsets()
            .nested_group_slices(outer0.data, outer0.len, /*nested_no_tag=*/6001, mv->hooks(),
                                 fv->token(), outer_ctx)
            .slices;
    ASSERT_EQ(explicit_hooks_result.size(), 1U)
        << "nested_group_slices (6-arg, explicit hooks): NoCustom must resolve to one instance";

    // 4-arg convenience overload: forwards the ROOT table's own hooks_.
    auto convenience_result = mv->offsets()
                                  .nested_group_slices(outer0.data, outer0.len,
                                                       /*nested_no_tag=*/6001, outer_ctx)
                                  .slices;
    ASSERT_EQ(convenience_result.size(), 1U)
        << "nested_group_slices (4-arg convenience): NoCustom must resolve to one instance";

    struct overload_case {
        std::span<fixpp::wire::group_slice const> const* result;
        char const* name;
    };
    for (auto const& c : {overload_case{&explicit_hooks_result, "6-arg explicit hooks"},
                          overload_case{&convenience_result, "4-arg convenience"}}) {
        auto const& inner0 = (*c.result)[0];
        // ── wire::get with an entry_context's hooks ────────────────────
        // Mirrors what a generated group-entry reader calls through
        // `ctx_.hooks` (emit_messages.cpp / parser.hpp).
        fixpp::wire::entry_context entry_ctx{};
        entry_ctx.span = std::span<const std::byte>{inner0.data, inner0.len};
        entry_ctx.hooks = mv->hooks();
        entry_ctx.gen = fv->token();
        auto nested_5012 = fixpp::wire::get(entry_ctx.span, 5012, entry_ctx.hooks, entry_ctx.gen);
        ASSERT_TRUE(nested_5012.has_value()) << "overload: " << c.name;
        EXPECT_EQ(nested_5012->as_string(), kNestedValue)
            << "nested_group_slices sub-table (" << c.name << "): 5012 must come back byte-exact";
        auto nested_58 = fixpp::wire::get(entry_ctx.span, 58, entry_ctx.hooks, entry_ctx.gen);
        EXPECT_FALSE(nested_58.has_value())
            << "nested_group_slices sub-table (" << c.name
            << "): a forged 58 field must not appear as its own entry";
    }

    // ── membership_copy() + re-parse ────────────────────────────────────
    auto copy_tv = mv->membership_copy();
    Parser<access_mode::Index> copy_parser{copy_tv};
    std::pmr::monotonic_buffer_resource arena2;
    auto mv2 = copy_parser.parse(*fv, &arena2);
    ASSERT_TRUE(mv2.has_value()) << "re-parse over the membership_copy() must succeed";
    EXPECT_FALSE(mv2->offsets().find(58).has_value())
        << "membership_copy() re-parse: a forged 58 field must not appear as a separate entry";
    auto top_5002_again = mv2->offsets().find(5002);
    ASSERT_TRUE(top_5002_again.has_value());
    std::string_view const top_bytes_again{
        reinterpret_cast<char const*>(mv2->bytes().data() + top_5002_again->offset),
        top_5002_again->length};
    EXPECT_EQ(top_bytes_again, kTopValue)
        << "membership_copy() re-parse: top-level 5002 must still come back byte-exact";
}

// ── Validator walk ──────────────────────────────────────────────────────
//
// design §3: `Validator::validate` walks with the hooks of ITS OWN dictionary,
// not the view's. The view here is parsed dict-free on purpose, so a walk that
// reused the view's hooks (`msg.begin()`) would split 5002 at the embedded SOH
// and report the forged 58 as a tag TestMsg does not declare.
TEST(DictHooksCustomPair, ValidatorWalksWithItsOwnDictionaryPairs) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto tv = load_custom_pair_dict(&dict_mr);
    ASSERT_EQ(tv.length_pair_data_tag(5001), 5002U);

    std::string body =
        "35=T\x01"
        "5001=6\x01"
        "5002=";
    body += kTopValue;
    body += "\x01";
    auto buf = make_raw_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Index> dict_free_parser{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = dict_free_parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());
    ASSERT_FALSE(mv->is_dict_backed()) << "precondition: the view must not carry the dictionary";

    fixpp::wire::dictionary_driven_validator const validator{tv};
    std::pmr::monotonic_buffer_resource scratch;
    std::uint16_t ref_tag = 0;
    auto const rc = validator.validate(*mv, &scratch, &ref_tag);
    EXPECT_TRUE(rc.has_value()) << "validate failed (error " << static_cast<int>(rc.error())
                                << ", ref tag " << ref_tag
                                << "): the walk split the custom Data value at its embedded SOH";
}

// ── reify's owning handle ───────────────────────────────────────────────
//
// design §3: the handle re-frames its OWN byte copy with a Parser over its
// OWN `membership_copy()`, so the hooks point at that copy. Reached through the
// detail seam because `reify()` dispatches on generated message types and a
// custom MsgType has no codegen owner.
TEST(DictHooksCustomPair, ReifiedHandleReframesWithTheCopiedPairs) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto tv = load_custom_pair_dict(&dict_mr);
    ASSERT_EQ(tv.length_pair_data_tag(5001), 5002U);

    std::string body =
        "35=T\x01"
        "5001=6\x01"
        "5002=";
    body += kTopValue;
    body += "\x01";
    std::pmr::monotonic_buffer_resource owning_mr;
    std::optional<fixpp::dict::owning_message_handle> handle;
    {
        // The source frame, dictionary view and parse arena all die before the
        // handle is read, so only the handle's own copies can answer. The handle
        // re-frames through a real Framer, which checks CheckSum, so this frame
        // carries a correct one (make_raw_frame's fixed 10=000 would be rejected
        // and the handle would fall back to an empty view).
        std::string const head =
            "8=FIX.4.4\x01"
            "9=" +
            std::to_string(body.size()) + "\x01" + body;
        unsigned sum = 0;
        for (char const c : head) {
            sum += static_cast<unsigned char>(c);
        }
        std::string const trailer = std::to_string(sum % 256U);
        std::string const full =
            head + "10=" + std::string(3 - trailer.size(), '0') + trailer + "\x01";
        std::vector<std::byte> buf(full.size());
        std::memcpy(buf.data(), full.data(), full.size());
        auto fv = fixpp::wire::test::make_frame_view(buf);
        ASSERT_TRUE(fv.has_value());
        Parser<access_mode::Index> parser{tv};
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parser.parse(*fv, &arena);
        ASSERT_TRUE(mv.has_value());
        auto made = fixpp::dict::detail::owning_message_handle_from_frame(
            fixpp::dict::resolved_message_version{}, *mv, &owning_mr);
        ASSERT_TRUE(made.has_value());
        handle.emplace(std::move(*made));
    }

    auto const& view = handle->view();
    ASSERT_TRUE(view.is_dict_backed()) << "precondition: the handle must re-frame dict-backed";
    EXPECT_FALSE(view.offsets().find(58).has_value())
        << "reify: a forged 58 field must not appear as a separate entry";
    auto const data = view.offsets().find(5002);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ((std::string_view{reinterpret_cast<char const*>(view.bytes().data() + data->offset),
                                data->length}),
              kTopValue);
}

// ── Precedence: the standard table wins on EITHER side ─────────────────
//
// design §3's lookup rule: a dictionary pair applies only when NEITHER of
// its tags is a standard pair tag, on either side.
TEST(DictHooksCustomPair, StandardLengthTagIgnoresConflictingDictionaryPair) {
    // RawDataLength(95) is standard-paired with RawData(96). This hand-built
    // dictionary re-pairs it with a custom Data tag 5002; the standard pair
    // must still govern.
    table_view_builder tvb;
    tvb.set_length_pair_data_tag(95, 5002);
    table_view const tv = std::move(tvb).build();
    ASSERT_EQ(tv.length_pair_data_tag(95), 5002U)
        << "precondition: the dictionary really did register the conflicting pair";

    auto hooks = dict_hooks::for_table_view(tv);
    EXPECT_EQ(hooks.data_tag_for_length(95), 96U)
        << "the standard pair 95->96 must win over a dictionary pair naming a standard Length tag";
}

TEST(DictHooksCustomPair, StandardDataTagIsNeverPairedByADictionary) {
    // A custom Length tag 5001 is paired by this dictionary with the
    // STANDARD Data tag 96 (RawData). Because 96 already names a side of a
    // standard pair, the dictionary pair must not be honoured at all — not
    // even under the custom Length tag.
    table_view_builder tvb;
    tvb.set_length_pair_data_tag(5001, 96);
    table_view const tv = std::move(tvb).build();
    ASSERT_EQ(tv.length_pair_data_tag(5001), 96U)
        << "precondition: the dictionary really did register the conflicting pair";

    auto hooks = dict_hooks::for_table_view(tv);
    EXPECT_EQ(hooks.data_tag_for_length(5001), 0U)
        << "a dictionary pair whose Data tag is a standard pair tag must not be honoured";
}

// ── Re-pairing keeps both directions inverse (Gate B r1 G-4) ────────────────
TEST(DictHooksCustomPair, RepairingATagKeepsBothDirectionsInverse) {
    table_view_builder length_movesb;
    length_movesb.set_length_pair_data_tag(5001, 5002);
    length_movesb.set_length_pair_data_tag(5001, 5003);
    table_view const length_moves = std::move(length_movesb).build();
    EXPECT_EQ(length_moves.length_pair_data_tag(5001), 5003U);
    EXPECT_EQ(length_moves.data_pair_length_tag(5003), 5001U);
    EXPECT_EQ(length_moves.data_pair_length_tag(5002), 0U) << "the old Data tag keeps no Length";
    EXPECT_EQ(dict_hooks::for_table_view(length_moves).length_tag_for_data(5002), 0U);

    table_view_builder data_movesb;
    data_movesb.set_length_pair_data_tag(5001, 5002);
    data_movesb.set_length_pair_data_tag(5011, 5002);
    table_view const data_moves = std::move(data_movesb).build();
    EXPECT_EQ(data_moves.data_pair_length_tag(5002), 5011U);
    EXPECT_EQ(data_moves.length_pair_data_tag(5011), 5002U);
    EXPECT_EQ(data_moves.length_pair_data_tag(5001), 0U) << "the old Length tag keeps no Data";
    EXPECT_EQ(dict_hooks::for_table_view(data_moves).data_tag_for_length(5001), 0U);
}

// ── The Data->Length inverse (fixpp#428, design §3) ─────────────────────────
TEST(DictHooksCustomPair, LengthTagForDataIsTheInverseWithTheSamePrecedence) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto tv = load_custom_pair_dict(&dict_mr);
    auto const hooks = dict_hooks::for_table_view(tv);
    EXPECT_EQ(hooks.length_tag_for_data(5002), 5001U) << "a dictionary pair, inverted";
    EXPECT_EQ(hooks.length_tag_for_data(355), 354U) << "a standard pair the dictionary omits";
    EXPECT_EQ(hooks.length_tag_for_data(89), 93U) << "a standard inverted pair";
    EXPECT_EQ(hooks.length_tag_for_data(5001), 0U) << "a Length tag is not a Data tag";
    EXPECT_EQ(dict_hooks::none().length_tag_for_data(5002), 0U) << "no dictionary, no custom pair";

    table_view_builder conflictingb;
    conflictingb.set_length_pair_data_tag(5001, 96);
    table_view const conflicting = std::move(conflictingb).build();
    EXPECT_EQ(dict_hooks::for_table_view(conflicting).length_tag_for_data(96), 95U)
        << "the standard RawDataLength(95) keeps RawData(96)";
}

// ── The fast paths change no answer ──────────────────────────────────────────
//
// dict_hooks answers a standard pair tag from a constexpr bitset, and
// `for_table_view` gives it no pair callback at all unless the dictionary declares a
// pair with both tags outside the standard table. Every 16-bit tag, in both
// directions, must still get the answer of the rule written with lookups only.
namespace {

std::uint16_t lookup_only_data_tag_for_length(table_view const& tv, std::uint16_t tag) {
    namespace detail = fixpp::wire::detail;
    if (std::uint16_t const standard = detail::standard_data_tag_for_length(tag); standard != 0) {
        return standard;
    }
    if (detail::standard_length_tag_for_data(tag) != 0) {
        return 0;
    }
    std::uint16_t const data = tv.length_pair_data_tag(tag);
    return (data != 0 && !detail::is_standard_pair_tag(data)) ? data : 0;
}

std::uint16_t lookup_only_length_tag_for_data(table_view const& tv, std::uint16_t tag) {
    namespace detail = fixpp::wire::detail;
    if (std::uint16_t const standard = detail::standard_length_tag_for_data(tag); standard != 0) {
        return standard;
    }
    if (detail::standard_data_tag_for_length(tag) != 0) {
        return 0;
    }
    std::uint16_t const length = tv.data_pair_length_tag(tag);
    return (length != 0 && !detail::is_standard_pair_tag(length)) ? length : 0;
}

// Counts the tags whose answer differs, so one bad tag cannot hide the rest.
void expect_fast_paths_change_no_answer(table_view const& tv) {
    auto const hooks = dict_hooks::for_table_view(tv);
    std::size_t standard_bit_wrong = 0;
    std::size_t length_side_wrong = 0;
    std::size_t data_side_wrong = 0;
    for (std::uint32_t t = 0; t <= 0xFFFFU; ++t) {
        auto const tag = static_cast<std::uint16_t>(t);
        standard_bit_wrong += fixpp::wire::detail::standard_pair_tag_bit(tag) !=
                              fixpp::wire::detail::is_standard_pair_tag(tag);
        length_side_wrong +=
            hooks.data_tag_for_length(tag) != lookup_only_data_tag_for_length(tv, tag);
        data_side_wrong +=
            hooks.length_tag_for_data(tag) != lookup_only_length_tag_for_data(tv, tag);
    }
    EXPECT_EQ(standard_bit_wrong, 0U) << "the standard bits must name exactly the standard tags";
    EXPECT_EQ(length_side_wrong, 0U) << "data_tag_for_length";
    EXPECT_EQ(data_side_wrong, 0U) << "length_tag_for_data";
}

}  // namespace

TEST(DictHooksCustomPair, FastPathsChangeNoAnswerForAnyTag) {
    table_view_builder tvb;
    tvb.set_length_pair_data_tag(5001, 5002);  // a custom pair
    tvb.set_length_pair_data_tag(95, 7002);    // re-pairs a standard Length
    tvb.set_length_pair_data_tag(7101, 96);    // pairs a standard Data tag
    tvb.set_length_pair_data_tag(7201, 7202);
    tvb.set_length_pair_data_tag(7201, 7203);  // the Length moves: 7202's bit stays set
    tvb.set_length_pair_data_tag(7301, 7302);
    tvb.set_length_pair_data_tag(7311, 7302);    // the Data moves: 7301's bit stays set
    tvb.set_length_pair_data_tag(65535, 65534);  // the top of the tag range
    table_view const tv = std::move(tvb).build();
    auto const hooks = dict_hooks::for_table_view(tv);
    // The walk compares answers, so it must see pairs that answer non-zero.
    ASSERT_EQ(hooks.data_tag_for_length(5001), 5002U);
    ASSERT_EQ(hooks.length_tag_for_data(7203), 7201U);
    ASSERT_EQ(hooks.length_tag_for_data(7302), 7311U);
    ASSERT_EQ(hooks.data_tag_for_length(65535), 65534U);
    ASSERT_TRUE(tv.has_nonstandard_pair());
    expect_fast_paths_change_no_answer(tv);

    expect_fast_paths_change_no_answer(table_view{});  // no pairs at all

    // The predicate must be EXACT, not merely safe: a dictionary whose every pair
    // names a standard tag gets no callback, and the rule answers those pairs from
    // the standard table anyway. If the flag were set here, the answers would not
    // change — only the per-field cost — so this arm pins the flag itself as well.
    table_view_builder standard_onlyb;
    standard_onlyb.set_length_pair_data_tag(95, 7002);   // a standard Length
    standard_onlyb.set_length_pair_data_tag(7101, 96);   // a standard Data
    standard_onlyb.set_length_pair_data_tag(354, 7202);  // EncodedTextLen
    table_view const standard_only = std::move(standard_onlyb).build();
    EXPECT_FALSE(standard_only.has_nonstandard_pair());
    expect_fast_paths_change_no_answer(standard_only);
}

// ── The dict-backed streaming parser (Gate B r8 P-1) ────────────────────────
//
// `Parser<Iter>` captures a dict_hooks bundle in its constructor, and
// `parse_iter()` used to return `MessageView<Iter>{frame}` — dropping it. A
// dictionary's own Length+Data pairs therefore never reached the streaming
// scanner: the standard table split those frames, the dictionary's pairs did not,
// and the test above could not see it because it walks an Index view's iterator.
//
// Mutation procedure: make `parse_iter()` return `{frame}` again; this test fails
// on the forged 58 and on the truncated 5002 value, while everything above stays
// green — which is exactly how the defect survived.
TEST(DictBackedIterParser, CustomPairSplitsThroughParseIter) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto tv = load_custom_pair_dict(&dict_mr);
    ASSERT_EQ(tv.length_pair_data_tag(5001), 5002U) << "precondition: the pair is registered";

    auto const frame = make_custom_pair_frame();
    auto const fv = fixpp::wire::test::make_frame_view(frame);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Iter> parser{tv};
    auto mv = parser.parse_iter(*fv);
    ASSERT_TRUE(mv.has_value()) << "parse_iter failed";

    bool saw_forged_58 = false;
    bool saw_top_5002 = false;
    for (auto it = mv->begin(); !(it == mv->end()); ++it) {
        auto const& f = *it;
        if (f.tag == 58) {
            saw_forged_58 = true;
        }
        if (f.tag == 5002) {
            std::string_view const v{reinterpret_cast<char const*>(f.value.data()), f.value.size()};
            EXPECT_EQ(v, kTopValue) << "parse_iter: 5002 must come back byte-exact";
            saw_top_5002 = true;
        }
    }
    EXPECT_FALSE(saw_forged_58)
        << "parse_iter: the SOH inside the Data value must not forge a 58 field — "
           "the parser's own dictionary must reach the streaming scanner";
    EXPECT_TRUE(saw_top_5002) << "parse_iter: the counted 5002 value must be yielded";

    // The dict-free Iter view is the control: no hooks, so the standard table alone
    // governs and the forged field DOES appear. Without this arm the test above could
    // pass on a frame that never needed a dictionary.
    fixpp::wire::MessageView<access_mode::Iter> bare{*fv};
    bool bare_saw_58 = false;
    for (auto it = bare.begin(); !(it == bare.end()); ++it) {
        if ((*it).tag == 58) {
            bare_saw_58 = true;
        }
    }
    EXPECT_TRUE(bare_saw_58)
        << "control: without the dictionary the embedded SOH must split out a 58 field";
}

// ── Zero is not half of a pair (Gate B r8 P-2) ──────────────────────────────
//
// Zero is what BOTH accessors answer for "no pair", so a stored 0 -> data would read
// back as a forward pair whose inverse says absent — two directions that can never
// agree. Since fixpp#457 the loaders refuse a zero-numbered field outright, so a
// dictionary can no longer offer one — this stays the boundary for a table
// populated through `table_view_builder`, and any future non-loader builder.
//
// The first four expectations read state BETWEEN two mutator calls, which is why
// `table_view_builder` forwards those three accessors (fixpp#456 design §5d item
// 4). Do NOT split this into two build cycles: the case pins that a rejected zero
// leaves no inverse behind IN THE SAME TABLE, and two tables cannot assert that.
TEST(DictHooksCustomPair, ZeroIsNeverHalfOfAPair) {
    table_view_builder b;
    b.set_length_pair_data_tag(0, 5002);
    EXPECT_EQ(b.length_pair_data_tag(0), 0U) << "a zero Length tag must not be stored";
    EXPECT_EQ(b.data_pair_length_tag(5002), 0U) << "and must leave no inverse behind";

    b.set_length_pair_data_tag(5001, 0);
    EXPECT_EQ(b.length_pair_data_tag(5001), 0U) << "a zero Data tag stays a no-op";
    EXPECT_FALSE(b.has_nonstandard_pair()) << "neither call may arm the pair flag";

    table_view const tv = std::move(b).build();
    auto const hooks = dict_hooks::for_table_view(tv);
    EXPECT_EQ(hooks.data_tag_for_length(0), 0U);
    EXPECT_EQ(hooks.length_tag_for_data(5002), 0U);
}

// ── DELETED at fixpp#456, and the deletion is a DISCLOSURE ──────────────────
//
// `DictHooksCustomPair.ABundleIsASnapshotOfTheDictionaryItWasBuiltFrom` pinned
// that `for_table_view` latches `has_nonstandard_pair()` at build time, by
// mutating a view AFTER a bundle had been taken from it. That premise is
// unconstructible now that the view is sealed, so the staleness half of the
// behaviour is no longer observable through the public API. What records the
// loss: `B-456-2` in spec/behaviors-and-limitations.md, and the compile-time
// seal witness in tests/dictionary/table_view_seal_compile_test.cpp, which
// asserts the stronger property that replaced it. The case's POSITIVE half — a
// bundle built after the pair was registered honours it — survives above in
// `FastPathsChangeNoAnswerForAnyTag`, on the same tags.
//
// `DictHooksCustomPair.CopyAssignmentCarriesTheFlagWithThePairs` went with it:
// its subject was `table_view::operator=`, which fixpp#456 deletes. It was also
// the proof that assignment had to go (design §3.2) — copy-assignment moved the
// exact `has_nonstandard_pair_` bit the seal exists to freeze.

// ─────────────────────────────────────────────────────────────────────────
// Gate B r9 R-1 — `nested_group_slices` must split by the CALLER's bundle on
// WARM cache hits too, not only on a cold build.
//
// `nested_cache_row` used to be keyed on `(slice_data, nested_no_tag)` alone,
// so the first caller's dictionary permanently decided a slice's sub-table:
// a later caller handing in a different bundle got the earlier split back,
// silently. That is the mismatched-pairing defect this PR exists to remove
// (brain/components/wire.md, "the DELIMITER oracle (#384)") reintroduced by a
// cache key, and it contradicts design §3 — "BOTH overloads take dict_hooks
// from their caller".
//
// ⚠️ The dictionaries here are built BY HAND rather than loaded from XML. The
// loader detects a pair by LENGTH/DATA adjacency in both the <fields> block
// and the message body, so an XML "dictionary without the pair" would need two
// coordinated edits and could silently stop differing for the wrong reason.
// Hand-built views also pin `has_nonstandard_pair()`, which is what decides
// whether `for_table_view` installs the pair callback at all.
namespace {

// The value carried by the counted 5012 field. The forged tag inside it is 7003,
// a member of the OUTER group (7001) but NOT of the nested one (6001). That
// asymmetry is load-bearing in both directions:
//   - NOT a 6001 member => without the pair it ENDS the nested entry early, so
//     the nested extent differs between the two dictionaries (the discriminator);
//   - IS a 7001 member  => it does NOT end the OUTER entry, so `outer[0]` is the
//     same byte range whichever dictionary built the root table.
// ⚠️ An earlier revision embedded `58=G`, a member of NEITHER group. The outer
// slice was then truncated at the forged field by the dictionary WITHOUT the
// pair, and handing that short slice to the dictionary WITH the pair made its
// counted read overrun and return ZERO nested slices — the witness failed for a
// fixture reason that had nothing to do with the cache key under test.
constexpr std::string_view kHooksKeyValue =
    "y"
    "\x01"
    "7003=Z";
static_assert(kHooksKeyValue.size() == 8);

// Identical structure in both arms; they differ ONLY in whether 5011/5012 is a
// registered Length+Data pair.
table_view make_nested_pair_dict(bool with_pair) {
    table_view_builder b;
    b.add_valid("T", 35)
        .add_valid("T", 7001)
        .add_valid("T", 7002)
        .add_valid("T", 6001)
        .add_valid("T", 6002)
        .add_valid("T", 5011)
        .add_valid("T", 5012)
        .add_valid("T", 7003)
        .set_group_first(7001, 7002)
        .add_group_member(7001, 6001)
        .add_group_member(7001, 6002)
        .add_group_member(7001, 5011)
        .add_group_member(7001, 5012)
        // 7003 is a member of the OUTER group ONLY — deliberately never added to
        // 6001. See kHooksKeyValue above for why both halves of that matter.
        .add_group_member(7001, 7003)
        .set_group_first(6001, 6002)
        .add_group_member(6001, 5011)
        .add_group_member(6001, 5012);
    if (with_pair) {
        b.set_length_pair_data_tag(5011, 5012);
    }
    return std::move(b).build();
}

// 7001 > 6001, whose single entry carries the counted 5012 value with a forged
// `7003=Z` inside it. WITHOUT the pair the scanner stops the value at the
// embedded SOH and 7003 — not a member of 6001 — ends the NESTED entry early, so
// the nested slice is SHORTER; the OUTER entry is unaffected because 7003 IS a
// 7001 member. WITH the pair the 8 bytes are consumed whole.
std::vector<std::byte> make_nested_custom_pair_frame() {
    std::string body = "35=T\x01";
    body += "7001=1\x01";
    body += "7002=O1\x01";
    body += "6001=1\x01";
    body += "6002=E1\x01";
    body += "5011=8\x01";
    body += "5012=";
    body += kHooksKeyValue;
    body += "\x01";
    return make_raw_frame(body);
}

std::size_t nested_len_on_fresh_table(table_view const& tv, std::vector<std::byte> const& buf,
                                      fixpp::wire::frame_view const& fv,
                                      std::uint16_t nested_no_tag) {
    (void)buf;
    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::OffsetTable root{fv, &arena, dict_hooks::for_table_view(tv)};
    auto const outer = root.group_slices(7001);
    EXPECT_EQ(outer.size(), 1U);
    if (outer.empty()) {
        return 0;
    }
    auto const r = root.nested_group_slices(
        outer[0].data, outer[0].len, nested_no_tag, dict_hooks::for_table_view(tv), fv.token(),
        fixpp::wire::group_context{.msg_type = "T"}.pushed(7001));
    EXPECT_EQ(r.slices.size(), 1U);
    return r.slices.empty() ? 0 : r.slices[0].len;
}

}  // namespace

TEST(NestedGroupSlicesHooksKey, WarmCacheHonoursTheCallersDictionaryNotTheFirstCallers) {
    auto const tv_no_pair = make_nested_pair_dict(/*with_pair=*/false);
    auto const tv_with_pair = make_nested_pair_dict(/*with_pair=*/true);

    // Preconditions — the flag is what gates the pair callback, so pin both.
    ASSERT_FALSE(tv_no_pair.has_nonstandard_pair())
        << "precondition: arm A must register no non-standard pair";
    ASSERT_TRUE(tv_with_pair.has_nonstandard_pair())
        << "precondition: arm B must register 5011/5012 as a non-standard pair";
    ASSERT_EQ(tv_with_pair.length_pair_data_tag(5011), 5012U);
    ASSERT_EQ(tv_no_pair.length_pair_data_tag(5011), 0U);

    auto buf = make_nested_custom_pair_frame();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    // ── NON-VACUITY: the two dictionaries really do split this slice
    // differently, each measured on its OWN cold table. Without this, every
    // assertion below could pass with the hooks key doing nothing.
    std::size_t const cold_no_pair = nested_len_on_fresh_table(tv_no_pair, buf, *fv, 6001);
    std::size_t const cold_with_pair = nested_len_on_fresh_table(tv_with_pair, buf, *fv, 6001);
    ASSERT_NE(cold_no_pair, cold_with_pair)
        << "fixture invariant: the counted 5012 value must make the two dictionaries "
           "produce different nested extents, else this witness proves nothing";
    ASSERT_GT(cold_with_pair, cold_no_pair)
        << "fixture invariant: consuming the counted value must EXTEND the entry";

    // ── ARM 1 (exact-key warm hit): one table, dictionary A first, then B at
    // the SAME nested_no_tag. B must get B's split, not A's cached one.
    {
        std::pmr::monotonic_buffer_resource arena;
        fixpp::wire::OffsetTable root{*fv, &arena, dict_hooks::for_table_view(tv_no_pair)};
        auto const outer = root.group_slices(7001);
        ASSERT_EQ(outer.size(), 1U);
        auto const ctx = fixpp::wire::group_context{.msg_type = "T"}.pushed(7001);

        auto const first =
            root.nested_group_slices(outer[0].data, outer[0].len, 6001,
                                     dict_hooks::for_table_view(tv_no_pair), fv->token(), ctx);
        ASSERT_EQ(first.slices.size(), 1U);
        EXPECT_EQ(first.slices[0].len, cold_no_pair) << "arm A must match its own cold build";

        auto const second =
            root.nested_group_slices(outer[0].data, outer[0].len, 6001,
                                     dict_hooks::for_table_view(tv_with_pair), fv->token(), ctx);
        ASSERT_EQ(second.slices.size(), 1U);
        EXPECT_EQ(second.slices[0].len, cold_with_pair)
            << "WARM exact-key hit served the FIRST caller's dictionary: the cache row is not "
               "keyed on the bundle (Gate B r9 R-1)";

        // ── Gate B r11 T-1: the TEST-ONLY introspection seam must key on the
        // bundle too. After the two calls above the cache holds TWO rows for
        // the same (slice, 6001) — one per dictionary — which is exactly the
        // state a seam comparing only `(slice_data, nested_no_tag)` cannot
        // represent: it returns whichever row comes first, for BOTH queries.
        // An instrument blind to the distinction its subject exists to make is
        // worth no more than no instrument at all.
        auto const* sub_a = fixpp::wire::nested_cache_access_for_testing::resolve(
            root, outer[0].data, dict_hooks::for_table_view(tv_no_pair), 6001);
        auto const* sub_b = fixpp::wire::nested_cache_access_for_testing::resolve(
            root, outer[0].data, dict_hooks::for_table_view(tv_with_pair), 6001);
        ASSERT_NE(sub_a, nullptr) << "arm A's row must be resolvable by its own bundle";
        ASSERT_NE(sub_b, nullptr) << "arm B's row must be resolvable by its own bundle";
        EXPECT_NE(sub_a, sub_b)
            << "the introspection seam returned ONE sub-table for two different bundles over the "
               "same slice and no_tag — it is not comparing hooks_key (Gate B r11 T-1)";
    }

    // ── ARM 2 (donation branch): same slice, DIFFERENT nested_no_tag. The
    // `!found_slice` branch donates a row's sub-table across no_tags; it must
    // not donate one built with another dictionary.
    {
        std::pmr::monotonic_buffer_resource arena;
        fixpp::wire::OffsetTable root{*fv, &arena, dict_hooks::for_table_view(tv_no_pair)};
        auto const outer = root.group_slices(7001);
        ASSERT_EQ(outer.size(), 1U);
        auto const ctx = fixpp::wire::group_context{.msg_type = "T"}.pushed(7001);

        // Warm the cache for a DIFFERENT no_tag under dictionary A.
        (void)root.nested_group_slices(outer[0].data, outer[0].len, /*nested_no_tag=*/7002,
                                       dict_hooks::for_table_view(tv_no_pair), fv->token(), ctx);

        auto const under_b =
            root.nested_group_slices(outer[0].data, outer[0].len, 6001,
                                     dict_hooks::for_table_view(tv_with_pair), fv->token(), ctx);
        ASSERT_EQ(under_b.slices.size(), 1U);
        EXPECT_EQ(under_b.slices[0].len, cold_with_pair)
            << "the same-slice donation branch handed over a sub-table built with ANOTHER "
               "dictionary (Gate B r9 R-1)";
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Gate B r9 R-3 / fixpp#457 — a zero tag cannot reach the pair maps from a
// dictionary, because it cannot get past the field declaration.
//
// `table_view::set_length_pair_data_tag` refusing a zero half keeps the WIRE
// pair maps clean, but it sits downstream of the loaders: a dictionary could
// still FORM a zero-headed pair, and `Dictionary::length_pair_data_tag`,
// `field_ref` and `message_fields()` would report it to any caller that never
// goes through a table_view. fixpp#457 closed that further upstream still.
//
// ⚠️ ONE fixture, deliberately — the LENGTH/DATA distinction that used to make
// two cases here is no longer observable. `parse_document` runs
// `parse_global_fields` before `detect_length_pairs` (xml_loader.cpp), so the
// declaration refusal fires before any adjacency is examined; a "zero DATA
// half" fixture would take the identical path and assert the identical thing.
// Two cases that cannot diverge are one case wearing a loop.
TEST(DictHooksCustomPair, ZeroIsRefusedAtDeclarationBeforeAPairCanForm) {
    // A LENGTH field numbered 0 adjacent to a DATA field — the shape the
    // adjacency detector would have paired as (0, 5002), had it run.
    constexpr std::string_view kZeroLengthXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='0' name='ZeroLen' type='LENGTH'/>)"
        R"(<field number='5002' name='CustomData' type='DATA'/>)"
        R"(</fields><messages/></fix>)";

    std::pmr::monotonic_buffer_resource mr;
    try {
        (void)fixpp::dict::XmlLoader{}.load_from_string(kZeroLengthXml, &mr);
        FAIL() << "the loader must refuse a zero-numbered field";
    } catch (fixpp::dict::xml_parse_error const& e) {
        // The message, not just the type: this loader raises `xml_parse_error`
        // for unrelated malformations too, so a type-only arm would stay green
        // if the fixture began failing for anything but the rule under test.
        EXPECT_NE(std::string{e.what()}.find(R"(<field number="0">)"), std::string::npos)
            << "refused for the WRONG reason — the declaration rule did not fire. what()="
            << e.what();
    }
}
