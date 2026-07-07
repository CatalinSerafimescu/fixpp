// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/defect_a_group_context_test.cpp — 063 T010 [US1]
//
// Defect-A discriminating guard (data-model.md INV-A / tasks.md T010): load
// the REAL FIX44.xml via the shipped loader, build a table_view via
// Dictionary::as_table_view(), and assert that FIX44 tag 295 (NoQuoteEntries)
// resolves — IN MASSQUOTE CONTEXT (msg_type "i", nested one level under
// NoQuoteSets(296) via the QuotSetGrp/QuotEntryGrp component chain,
// FIX44.xml:943-955/3350-3358/3219-3248) — to QuotEntryGrp's members
// {299 QuoteEntryID, 132 BidPx, 133 OfferPx}, NOT the globally-first-declared
// QuotCxlEntriesGrp variant (FIX44.xml:3180-3187, which resolves 295's
// members via Instrument/FinancingDetails/UndInstrmtGrp/InstrmtLegGrp
// components and carries NONE of QuoteEntryID/BidPx/OfferPx directly).
//
// Mutation-proven RED on the pre-fix bare-no_tag-keyed store (SC-003): before
// 063 T011-T015, Dictionary::as_table_view() populates ONLY the legacy
// global `no_tag -> members` store, first-XML-occurrence-wins — so 295
// resolves dictionary-wide to QuotCxlEntriesGrp (declared first at line
// 3180) regardless of message context, and this test's positive assertions
// fail.
//
// group_member_fn: a byte-identical local copy of the group_member_fn_t the
// Parser dict-lvalue ctor installs (parser.hpp:494-517) — mirrors the
// established pattern in tests/wire/nested_group_slices_cache_test.cpp's
// `dict_group_member` — so this test drives the SAME logic the wire parser
// uses, without pulling in a codegen dependency (tests/dictionary/ has none).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/group_view.hpp>  // fixpp::wire::group_context
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace {

using fixpp::dict::table_view;
using fixpp::wire::group_context;

// Byte-identical copy of the group_member_fn_t installed by Parser's
// dict-lvalue ctor (include/fixpp/wire/parser.hpp:494-517) — see file header.
bool group_member_fn(void const* d, group_context const& ctx, std::uint16_t no_tag,
                     std::uint16_t tag) noexcept {
    auto const* dict = static_cast<table_view const*>(d);
    auto const members = dict->group_member_tags(
        ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
    for (auto const member_tag : members) {
        if (member_tag == tag) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(DefectAGroupContext, MassQuote295ResolvesToQuotEntryGrpNotQuotCxlEntriesGrp) {
    std::array<std::byte, 1024UZ * 1024UZ> buffer{};
    std::pmr::monotonic_buffer_resource mr{buffer.data(), buffer.size()};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto tv = dict.as_table_view();

    // MassQuote context: msg_type "i", tag 295 nested one level under 296
    // (NoQuoteSets) — data-model.md's worked exemplar (parent_path == [296]).
    std::array<std::uint16_t, 1> const parent_path_storage{296};
    std::span<std::uint16_t const> const parent_path{parent_path_storage};
    group_context const mass_quote_ctx{.msg_type = "i", .parent_path = {296}, .depth = 1};

    auto const members = tv.group_member_tags("i", parent_path, 295);
    std::vector<std::uint16_t> const member_vec{members.begin(), members.end()};

    auto const contains = [&](std::uint16_t tag) {
        return std::find(member_vec.begin(), member_vec.end(), tag) != member_vec.end();
    };

    EXPECT_TRUE(contains(299)) << "295 in MassQuote context must include QuoteEntryID(299) "
                                  "(QuotEntryGrp) — got a member set of size "
                               << member_vec.size();
    EXPECT_TRUE(contains(132)) << "295 in MassQuote context must include BidPx(132)";
    EXPECT_TRUE(contains(133)) << "295 in MassQuote context must include OfferPx(133)";

    // INV-A: the production predicate (group_member_fn, mirrored above)
    // agrees.
    EXPECT_TRUE(group_member_fn(&tv, mass_quote_ctx, 295, 299))
        << "group_member_fn must accept 299 as a member of 295 in MassQuote context";
    EXPECT_TRUE(group_member_fn(&tv, mass_quote_ctx, 295, 132))
        << "group_member_fn must accept 132 as a member of 295 in MassQuote context";
    EXPECT_TRUE(group_member_fn(&tv, mass_quote_ctx, 295, 133))
        << "group_member_fn must accept 133 as a member of 295 in MassQuote context";

    // NOTE (escalated, tracked separately — NOT part of T010's INV-A
    // acceptance criteria per tasks.md, which asserts membership +
    // group_member_fn only): the context-scoped DELIMITER for a genuinely
    // COLLIDING no_tag (like 295) cannot yet be derived correctly, because
    // `Dictionary::message_fields()` returns its per-message FieldRef span
    // SORTED BY TAG (xml_loader.cpp's `append_run`, stable-sort + dedup for
    // O(log n) lookup), which discards the declaration order needed to pick
    // the true first-in-wire-order field. `tv.group_first_field("i",
    // {296}, 295)` currently resolves to the LOWEST-numbered member tag
    // (an artifact of the sorted source), not QuoteEntryID(299). This is a
    // real, separately-tracked gap (T012's "register the delimiter" clause)
    // requiring either an xml_loader.cpp change (capture per-context
    // declaration order before the sort) or an explicit deferral — see the
    // phase-implementer escalation report. MEMBERSHIP (asserted above) is
    // unaffected: it is a set derived from `FieldRef.group_no_tag`, which
    // survives the sort/dedup intact.
}
