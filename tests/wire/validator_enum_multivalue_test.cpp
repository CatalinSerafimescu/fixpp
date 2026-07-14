// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/validator_enum_multivalue_test.cpp
//
// 075-live-wire-enum-validation T027 (US2, P1, co-equal with US1).
//
// Multi-value enum fields validate PER TOKEN, not as a single opaque string.
// A naive whole-string domain check would false-reject every conformant
// ExecInst/QuoteCondition/TradeCondition/... — this is ranked risk 1.
//
// Anchors:
//   spec: specs/075-live-wire-enum-validation/spec.md FR-004/FR-009/FR-014,
//         SC-003
//   tasks: specs/075-live-wire-enum-validation/tasks.md T027

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fixpp::dict::table_view;
using fixpp::wire::dictionary_driven_validator;

std::span<const std::byte> as_bytes(std::string_view s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

// See validator_enum_domain_test.cpp for the outliving-Dictionary rationale
// (dictionary.hpp:193-205 -- as_table_view() owns copies of the code bytes).
table_view load_shipped_table_view(char const* filename) {
    std::vector<std::byte> buf(8u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
    auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    return dict.as_table_view();
}

}  // namespace

// ── ExecInst(18) on FIX44 -- ground truth (T001, measured): 40 codes incl.
//    "1", "G", "6"; no "ZZ" ──────────────────────────────────────────────

TEST(ValidatorEnumMultivalue, ExecInst18AllDeclaredTokensAccept) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    // Mutation target: replacing the tokenizer with a whole-string lookup
    // flips THIS case to reject -- "1 G 6" is not itself a declared code,
    // only its three space-separated tokens are.
    auto rc = v.validate_field(18, as_bytes("1 G 6"));
    EXPECT_TRUE(rc.has_value())
        << "ExecInst(18)=\"1 G 6\" -- three individually-declared codes -- must accept; slot "
        << (rc.has_value() ? -1 : static_cast<int>(rc.error()));
}

TEST(ValidatorEnumMultivalue, ExecInst18OneUndeclaredTokenRejects) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto rc = v.validate_field(18, as_bytes("1 ZZ 6"));
    ASSERT_FALSE(rc.has_value()) << "ExecInst(18)=\"1 ZZ 6\" -- \"ZZ\" is not a declared code";
    EXPECT_EQ(rc.error(), fixpp::core::error::wire_field_value_out_of_range)
        << "got slot " << static_cast<int>(rc.error());
}

TEST(ValidatorEnumMultivalue, ExecInst18SingleTokenAccepts) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto rc = v.validate_field(18, as_bytes("1"));
    EXPECT_TRUE(rc.has_value()) << "ExecInst(18)=\"1\" (single token) must accept";
}

// ── FR-014: degenerate whitespace -- an empty token is never a declared
//    code, so a double space or a trailing space rejects. Byte-for-byte
//    QuickFIX (DataDictionary.h:265-275). ─────────────────────────────────

TEST(ValidatorEnumMultivalue, ExecInst18DoubleSpaceRejects) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    // "1  G" splits (on a single space) into {"1", "", "G"} -- the middle
    // empty token is never a declared code.
    auto rc = v.validate_field(18, as_bytes("1  G"));
    ASSERT_FALSE(rc.has_value())
        << "ExecInst(18)=\"1  G\" (double space -> empty middle token) must reject";
    EXPECT_EQ(rc.error(), fixpp::core::error::wire_field_value_out_of_range)
        << "got slot " << static_cast<int>(rc.error());
}

TEST(ValidatorEnumMultivalue, ExecInst18TrailingSpaceRejects) {
    auto tv = load_shipped_table_view("FIX44.xml");
    dictionary_driven_validator v{std::move(tv)};

    // "1 " splits into {"1", ""} -- the trailing empty token is never a
    // declared code.
    auto rc = v.validate_field(18, as_bytes("1 "));
    ASSERT_FALSE(rc.has_value())
        << "ExecInst(18)=\"1 \" (trailing space -> empty trailing token) must reject";
    EXPECT_EQ(rc.error(), fixpp::core::error::wire_field_value_out_of_range)
        << "got slot " << static_cast<int>(rc.error());
}

// ── AC US2 #3: a SINGLE-VALUE field whose value contains a space is
//    checked as ONE code with NO tokenization. SettlLocation(166) on
//    FIX41 declares the literal code "ISO Country Code" (measured, T001) --
//    the only known shipped example of a single-value field whose declared
//    code itself contains a space, so this is a real discriminator: a
//    tokenizing implementation would split it into {"ISO","Country","Code"}
//    and reject (none of those three tokens alone is declared), while the
//    correct non-tokenizing single-value path matches the whole string. ──

TEST(ValidatorEnumMultivalue, SingleValueFieldWithSpaceInValueNotTokenized) {
    auto tv = load_shipped_table_view("FIX41.xml");
    dictionary_driven_validator v{std::move(tv)};

    auto rc = v.validate_field(166, as_bytes("ISO Country Code"));
    EXPECT_TRUE(rc.has_value())
        << "SettlLocation(166)=\"ISO Country Code\" is a single declared code (single-value "
           "field) and must NOT be tokenized on its embedded space; slot "
        << (rc.has_value() ? -1 : static_cast<int>(rc.error()));

    // Positive control: a value NOT equal to any declared code (incl. not
    // matching by accidental sub-token) must still reject.
    auto rc2 = v.validate_field(166, as_bytes("US"));
    EXPECT_FALSE(rc2.has_value())
        << "SettlLocation(166)=\"US\" is not a declared FIX41 code and must reject";
}

// ── Table-driven over the FULL multi-value census (T001, measured). Uses
//    each tag's ACTUAL declared codes (via Dictionary::enum_values()) rather
//    than a hand-maintained per-tag expectation list, so a dictionary
//    refresh cannot silently invalidate the gate: for every enum-backed
//    multi-value tag in every dictionary, two declared codes joined by a
//    single space must accept, and one declared + one undeclared token must
//    reject. ──────────────────────────────────────────────────────────────

TEST(ValidatorEnumMultivalue, FullMultiValueCensusTableDriven) {
    struct CensusEntry {
        char const* file;
        std::vector<std::uint16_t> tags;
    };
    // FIX44's 8 (measured); FIX50/SP1 add 1031 AND 1035 (both enum-backed
    // there); FIX50SP2 has 9 -- 1035 is MULTIPLESTRINGVALUE there but
    // declares ZERO <value> children (measured), so it is excluded.
    std::vector<CensusEntry> const census = {
        {"FIX44.xml", {18, 276, 277, 286, 291, 292, 529, 546}},
        {"FIX50.xml", {18, 276, 277, 286, 291, 292, 529, 546, 1031, 1035}},
        {"FIX50SP1.xml", {18, 276, 277, 286, 291, 292, 529, 546, 1031, 1035}},
        {"FIX50SP2.xml", {18, 276, 277, 286, 291, 292, 529, 546, 1031}},
    };

    for (auto const& entry : census) {
        SCOPED_TRACE(entry.file);
        std::vector<std::byte> buf(16u * 1024u * 1024u);
        std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / entry.file;
        auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);

        // Capture each tag's actual declared codes BEFORE consuming `dict`
        // into the table_view.
        std::vector<std::pair<std::uint16_t, std::vector<std::string>>> codes_by_tag;
        for (auto const tag : entry.tags) {
            auto const ev = dict.enum_values(tag);
            ASSERT_GE(ev.size(), 2u) << "tag " << tag << " in " << entry.file
                                      << " must declare >=2 codes (measured census)";
            std::vector<std::string> codes;
            codes.reserve(ev.size());
            for (auto const& e : ev) {
                codes.emplace_back(e.value);
            }
            codes_by_tag.emplace_back(tag, std::move(codes));
        }

        dictionary_driven_validator v{dict.as_table_view()};

        for (auto const& [tag, codes] : codes_by_tag) {
            SCOPED_TRACE(tag);

            // (a) a single declared token accepts.
            {
                auto rc = v.validate_field(tag, as_bytes(codes[0]));
                EXPECT_TRUE(rc.has_value())
                    << "single declared token \"" << codes[0] << "\" must accept";
            }

            // (b) two declared tokens, single-space-separated, accept --
            //     the multi-value discriminator: a whole-string lookup
            //     would find "codes[0]+' '+codes[1]" absent and reject.
            {
                std::string const two = codes[0] + " " + codes[1];
                auto rc = v.validate_field(tag, as_bytes(two));
                EXPECT_TRUE(rc.has_value())
                    << "two declared tokens \"" << two << "\" must accept (per-token check)";
            }

            // (c) one declared token + one undeclared token rejects.
            {
                std::string const mixed = codes[0] + " NOT_A_DECLARED_CODE_ZZZ";
                auto rc = v.validate_field(tag, as_bytes(mixed));
                EXPECT_FALSE(rc.has_value())
                    << "\"" << mixed << "\" must reject (one undeclared token)";
            }
        }
    }
}
