// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/table_view_enum_domain_test.cpp
//
// 075-live-wire-enum-validation, T017/T018/T019: unit-level witnesses for
// `table_view::enum_valid()` (the real dictionary-driven enum-domain check)
// and its builder-time population surface (`add_enum` / `set_multi_value`).
// Exercises `table_view` directly via its chain-style builder API — no
// Dictionary / XmlLoader involved (that projection is T015, out of scope
// here).
//
// Anchors:
//   tasks: specs/075-live-wire-enum-validation/tasks.md T017/T018/T019
//   contracts: specs/075-live-wire-enum-validation/contracts/enum-domain.md C-3
//   data-model: specs/075-live-wire-enum-validation/data-model.md Entity B

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <span>
#include <string>
#include <string_view>

namespace {

using fixpp::dict::table_view;

[[nodiscard]] std::span<std::byte const> as_bytes(std::string_view sv) noexcept {
    return {reinterpret_cast<std::byte const*>(sv.data()), sv.size()};
}

}  // namespace

// ── Floor 1 [FR-003]: absent tag / empty codeset ⇒ ACCEPT ─────────────────

TEST(TableViewEnumDomainTest, AbsentTagAccepts) {
    table_view tv;  // no add_enum call for tag 54 at all
    EXPECT_TRUE(tv.enum_valid(54, as_bytes("Z")));
}

// Mirrors FIXT11's MsgType(35) arm: a tag that IS present but whose declared
// code set is empty must still accept — this is a DISTINCT branch from
// AbsentTagAccepts (a tag that was never registered at all).
TEST(TableViewEnumDomainTest, EmptyCodesetAccepts) {
    table_view tv;
    tv.set_multi_value(35, false);  // registers tag 35 with zero codes
    EXPECT_TRUE(tv.enum_valid(35, as_bytes("A")));
}

// ── Floor 2 [FR-008]: empty field VALUE bypasses the enum check ───────────
// unconditionally, regardless of a non-empty declared codeset.
// Mutation: delete the empty-value bypass guard (branch 2) ⇒ this MUST flip
// to reject, because "" is absent from every codeset.

TEST(TableViewEnumDomainTest, EmptyValueBypassesEnumCheckAndAccepts) {
    table_view tv;
    tv.add_enum(18, "1").add_enum(18, "G");
    EXPECT_TRUE(tv.enum_valid(18, std::span<std::byte const>{}));
}

// ── Single-value: byte-exact whole-token binary search ────────────────────

TEST(TableViewEnumDomainTest, InDomainSingleValueAccepts) {
    table_view tv;
    tv.add_enum(54, "1").add_enum(54, "2").add_enum(54, "A");
    EXPECT_TRUE(tv.enum_valid(54, as_bytes("1")));
    EXPECT_TRUE(tv.enum_valid(54, as_bytes("A")));
}

TEST(TableViewEnumDomainTest, OutOfDomainSingleValueRejects) {
    table_view tv;
    tv.add_enum(54, "1").add_enum(54, "2").add_enum(54, "A");
    EXPECT_FALSE(tv.enum_valid(54, as_bytes("Z")));
}

// No case folding, no prefix matching (FR-009): MatchType(574) declares
// A1..A5 but not bare 'A' — a prefix match would wrongly accept.
TEST(TableViewEnumDomainTest, NoPrefixMatchOnDeclaredExtension) {
    table_view tv;
    tv.add_enum(574, "A1").add_enum(574, "A2").add_enum(574, "A3");
    EXPECT_FALSE(tv.enum_valid(574, as_bytes("A")));
    EXPECT_TRUE(tv.enum_valid(574, as_bytes("A1")));
}

// ── Multi-value [FR-004/FR-014]: tokenize on a single space, require EVERY
// token declared. ────────────────────────────────────────────────────────

// The discriminating accept case: reddens under a whole-string-lookup
// mutation of the tokenizer (a reject-only witness set would pass under a
// broken tokenizer that never splits).
TEST(TableViewEnumDomainTest, MultiValueAllDeclaredAccepts) {
    table_view tv;
    tv.add_enum(18, "1").add_enum(18, "G").add_enum(18, "6");
    tv.set_multi_value(18);
    EXPECT_TRUE(tv.enum_valid(18, as_bytes("1 G 6")));
}

TEST(TableViewEnumDomainTest, MultiValueOneUndeclaredRejects) {
    table_view tv;
    tv.add_enum(18, "1").add_enum(18, "G").add_enum(18, "6");
    tv.set_multi_value(18);
    EXPECT_FALSE(tv.enum_valid(18, as_bytes("1 ZZ 6")));
}

// T018: a double space yields an empty token in the MIDDLE of the value,
// which is never a declared code ⇒ reject. Byte-for-byte QuickFIX
// (DataDictionary.h:265-275) — the tokenizer must NOT skip empty tokens.
TEST(TableViewEnumDomainTest, DoubleSpaceYieldsEmptyTokenAndRejects) {
    table_view tv;
    tv.add_enum(18, "1").add_enum(18, "G").add_enum(18, "6");
    tv.set_multi_value(18);
    EXPECT_FALSE(tv.enum_valid(18, as_bytes("1  G")));
}

// T018: a trailing space yields a final empty token ⇒ reject.
TEST(TableViewEnumDomainTest, TrailingSpaceYieldsEmptyTokenAndRejects) {
    table_view tv;
    tv.add_enum(18, "1").add_enum(18, "G").add_enum(18, "6");
    tv.set_multi_value(18);
    EXPECT_FALSE(tv.enum_valid(18, as_bytes("1 G ")));
}

// A single-value field whose value happens to contain a space is checked as
// ONE whole token, with NO tokenization — tokenization applies only to
// tags the dictionary types as multi-value.
TEST(TableViewEnumDomainTest, SingleValueFieldWithSpaceIsNotTokenized) {
    table_view tv;
    tv.add_enum(166, "ISO Country Code");  // multi_value left at default false
    EXPECT_TRUE(tv.enum_valid(166, as_bytes("ISO Country Code")));
    EXPECT_FALSE(tv.enum_valid(166, as_bytes("ISO")));
}

// ── T019: add_enum / set_multi_value population surface ───────────────────

TEST(TableViewEnumDomainTest, AddEnumDedupesDuplicateCodes) {
    table_view tv;
    tv.add_enum(54, "1").add_enum(54, "1").add_enum(54, "2");
    EXPECT_TRUE(tv.enum_valid(54, as_bytes("1")));
    EXPECT_TRUE(tv.enum_valid(54, as_bytes("2")));
    EXPECT_FALSE(tv.enum_valid(54, as_bytes("3")));
}

// The codes are OWNED copies: mutating the caller's original buffer after
// add_enum() must not affect a subsequent enum_valid() call.
TEST(TableViewEnumDomainTest, AddEnumOwnsACopyOfTheCodeBytes) {
    table_view tv;
    std::string mutable_code = "1";
    tv.add_enum(54, mutable_code);
    mutable_code[0] = 'Z';  // mutate the source buffer after add_enum returns
    EXPECT_TRUE(tv.enum_valid(54, as_bytes("1")));   // still declared
    EXPECT_FALSE(tv.enum_valid(54, as_bytes("Z")));  // "Z" was never declared
}

// set_multi_value is add_enum's companion for the multi-value bit
// (FR-005) — without it, the tokenizer has no unit-level witness.
TEST(TableViewEnumDomainTest, SetMultiValueDefaultsFalse) {
    table_view tv;
    tv.add_enum(18, "1").add_enum(18, "G");
    // multi_value never set ⇒ default false ⇒ "1 G" is ONE undeclared token.
    EXPECT_FALSE(tv.enum_valid(18, as_bytes("1 G")));
}
