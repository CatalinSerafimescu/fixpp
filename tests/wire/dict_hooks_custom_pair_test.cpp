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

namespace {

using fixpp::dict::table_view;
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

    // ── Iter path ───────────────────────────────────────────────────────
    // MessageView<Index>::begin()/end() pass this view's own hooks_
    // (parser.hpp), so the SAME dict-aware split applies to the field_iterator
    // walk.
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
    table_view tv;
    tv.set_length_pair_data_tag(95, 5002);
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
    table_view tv;
    tv.set_length_pair_data_tag(5001, 96);
    ASSERT_EQ(tv.length_pair_data_tag(5001), 96U)
        << "precondition: the dictionary really did register the conflicting pair";

    auto hooks = dict_hooks::for_table_view(tv);
    EXPECT_EQ(hooks.data_tag_for_length(5001), 0U)
        << "a dictionary pair whose Data tag is a standard pair tag must not be honoured";
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

    table_view conflicting;
    conflicting.set_length_pair_data_tag(5001, 96);
    EXPECT_EQ(dict_hooks::for_table_view(conflicting).length_tag_for_data(96), 95U)
        << "the standard RawDataLength(95) keeps RawData(96)";
}
