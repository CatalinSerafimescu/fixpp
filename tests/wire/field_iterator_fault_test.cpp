// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/field_iterator_fault_test.cpp — 092-garbled-frame-reject, FR-012.
//
// Anchors: specs/092-garbled-frame-reject/data-model.md E-4 (the iterator's
// fault record) and E-5 (the validator's fault handling); research.md R-7.
//
// Iterator cells: one per E-4 row (S0–S4, T1–T3) over a bare buffer, each run
// under three hook sets — a dictionary with only the standard pairs, a
// dictionary that also declares a custom pair, and `dict_hooks::none()`. Each
// asserts fault(), fault_length_tag() and the whole yielded sequence. Every
// expected sequence is spelled out by hand: E-4 requires the yield to stay
// what it was before 092, so it must not be derived from the iterator under
// test. To re-derive a row's yield, walk advance() by hand over the buffer.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/tag_scan.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fixpp::dict::table_view;
using fixpp::dict::table_view_builder;
using fixpp::wire::access_mode;
using fixpp::wire::dict_hooks;
using fixpp::wire::field_fault;
using fixpp::wire::MessageView;

using iter_t = MessageView<access_mode::Index>::field_iterator;

// '|' stands for SOH so the fixtures stay readable.
std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char const c : s) {
        out.push_back(static_cast<std::byte>(c == '|' ? '\x01' : c));
    }
    return out;
}

// One expected yield: the field, and the fault the iterator reports while it
// sits on that field.
struct yielded {
    std::uint16_t tag;
    std::string value;  // '|' for SOH, as in the fixtures
    field_fault fault_at_yield;
};

struct walk_result {
    std::vector<std::pair<std::uint16_t, std::string>> sequence;  // what was yielded
    std::vector<field_fault> faults_at_yield;
    field_fault fault = field_fault::none;
    std::uint16_t fault_length_tag = 0;
};

walk_result walk(std::span<std::byte const> buf, dict_hooks hooks) {
    walk_result r;
    iter_t it{buf, 0, hooks};
    iter_t const end{buf, buf.size(), hooks};
    for (; !(it == end); ++it) {
        std::string v;
        for (std::byte const b : (*it).value) {
            auto const c = static_cast<char>(b);
            v.push_back(c == '\x01' ? '|' : c);
        }
        r.sequence.emplace_back((*it).tag, std::move(v));
        r.faults_at_yield.push_back(it.fault());
    }
    r.fault = it.fault();
    r.fault_length_tag = it.fault_length_tag();
    return r;
}

// The yielded sequence and the fault record are compared separately, so a
// failure says which of the two moved.
void expect_walk(walk_result const& r, std::vector<yielded> const& expected, field_fault fault,
                 std::uint16_t length_tag) {
    std::vector<std::pair<std::uint16_t, std::string>> sequence;
    std::vector<field_fault> faults;
    for (auto const& y : expected) {
        sequence.emplace_back(y.tag, y.value);
        faults.push_back(y.fault_at_yield);
    }
    EXPECT_EQ(r.sequence, sequence) << "the yielded sequence moved";
    EXPECT_EQ(r.faults_at_yield, faults);
    EXPECT_EQ(r.fault, fault);
    EXPECT_EQ(r.fault_length_tag, length_tag);
}

// The three hook sets. `standard_dict` declares no custom pair, so it pairs
// exactly what `none()` pairs; `custom_dict` also pairs Length 5001 with
// Data 5002, which no standard pair names.
struct hook_sets {
    table_view standard_dict;
    table_view custom_dict;

    hook_sets() : standard_dict{make_standard()}, custom_dict{make_custom()} {}

    static table_view make_standard() {
        table_view_builder b;
        return std::move(b).build();
    }
    static table_view make_custom() {
        table_view_builder b;
        b.set_length_pair_data_tag(5001, 5002);
        return std::move(b).build();
    }

    [[nodiscard]] dict_hooks standard() const noexcept {
        return dict_hooks::for_table_view(standard_dict);
    }
    [[nodiscard]] dict_hooks custom() const noexcept {
        return dict_hooks::for_table_view(custom_dict);
    }
};

constexpr auto kNone = field_fault::none;
constexpr auto kTag = field_fault::malformed_tag;
constexpr auto kLd = field_fault::length_data_mismatch;

// Runs `buf` under all three hook sets and expects the same outcome from each.
void expect_all_hooks(std::string_view fixture, std::vector<yielded> const& expected,
                      field_fault fault, std::uint16_t length_tag) {
    hook_sets const hs;
    auto const buf = bytes_of(fixture);
    std::pair<char const*, dict_hooks> const sets[] = {{"standard", hs.standard()},
                                                       {"dictionary-only", hs.custom()},
                                                       {"none", dict_hooks::none()}};
    for (auto const& [name, hooks] : sets) {
        SCOPED_TRACE(std::string{"hooks: "} + name + ", fixture: " + std::string{fixture});
        expect_walk(walk(buf, hooks), expected, fault, length_tag);
    }
}

// Runs a custom-pair `buf`: the dictionary-only hook set splits it by the
// custom pair, the other two split it at every SOH.
void expect_custom_pair(std::string_view fixture, std::vector<yielded> const& paired,
                        field_fault paired_fault, std::uint16_t paired_length_tag,
                        std::vector<yielded> const& unpaired) {
    hook_sets const hs;
    auto const buf = bytes_of(fixture);
    {
        SCOPED_TRACE(std::string{"hooks: dictionary-only, fixture: "} + std::string{fixture});
        expect_walk(walk(buf, hs.custom()), paired, paired_fault, paired_length_tag);
    }
    std::pair<char const*, dict_hooks> const sets[] = {{"standard", hs.standard()},
                                                       {"none", dict_hooks::none()}};
    for (auto const& [name, hooks] : sets) {
        SCOPED_TRACE(std::string{"hooks: "} + name + ", fixture: " + std::string{fixture});
        expect_walk(walk(buf, hooks), unpaired, kNone, 0);
    }
}

}  // namespace

// ── S0: the end of the buffer ───────────────────────────────────────────────

TEST(FieldIteratorFault, S0_EmptyBufferIsNoFault) { expect_all_hooks("", {}, kNone, 0); }

TEST(FieldIteratorFault, S0_CleanWalkEndsWithNoFault) {
    expect_all_hooks("35=A|95=3|96=a|c|49=S|",
                     {{35, "A", kNone}, {95, "3", kNone}, {96, "a|c", kNone}, {49, "S", kNone}},
                     kNone, 0);
}

// ── S1–S3: a malformed tag stops the walk ───────────────────────────────────

TEST(FieldIteratorFault, S1_NonDigitTagByteStops) {
    expect_all_hooks("35=A|4x=1|49=S|", {{35, "A", kNone}}, kTag, 0);
}

TEST(FieldIteratorFault, S2_TagAbove0xFFFFStops) {
    expect_all_hooks("35=A|65536=1|49=S|", {{35, "A", kNone}}, kTag, 0);
}

TEST(FieldIteratorFault, S3_NoEqualsBeforeSohStops) {
    expect_all_hooks("35=A|49|56=T|", {{35, "A", kNone}}, kTag, 0);
}

TEST(FieldIteratorFault, S3_NoEqualsBeforeEndStops) {
    expect_all_hooks("35=A|49", {{35, "A", kNone}}, kTag, 0);
}

// ── S4: a counted value not followed by SOH stops the walk ──────────────────

TEST(FieldIteratorFault, S4_CountedValueNotFollowedBySohStops) {
    expect_all_hooks("35=A|95=3|96=abcd|49=S|", {{35, "A", kNone}, {95, "3", kNone}}, kLd, 95);
}

TEST(FieldIteratorFault, S4_CustomPair) {
    expect_custom_pair(
        "35=A|5001=3|5002=abcd|49=S|", {{35, "A", kNone}, {5001, "3", kNone}}, kLd, 5001,
        {{35, "A", kNone}, {5001, "3", kNone}, {5002, "abcd", kNone}, {49, "S", kNone}});
}

// ── T1: an empty tag is yielded as tag 0, and reported ──────────────────────

TEST(FieldIteratorFault, T1_EmptyTagYieldsTagZeroAndReports) {
    expect_all_hooks("35=A|=x|49=S|", {{35, "A", kNone}, {0, "x", kTag}, {49, "S", kTag}}, kTag, 0);
}

// ── T2: a count past the end is clamped, and reported ───────────────────────

TEST(FieldIteratorFault, T2_CountPastEndClampsAndReports) {
    expect_all_hooks("35=A|95=99|96=ab|49=S|",
                     {{35, "A", kNone}, {95, "99", kNone}, {96, "ab|49=S|", kLd}}, kLd, 95);
}

TEST(FieldIteratorFault, T2_CustomPair) {
    expect_custom_pair(
        "35=A|5001=99|5002=ab|49=S|",
        {{35, "A", kNone}, {5001, "99", kNone}, {5002, "ab|49=S|", kLd}}, kLd, 5001,
        {{35, "A", kNone}, {5001, "99", kNone}, {5002, "ab", kNone}, {49, "S", kNone}});
}

// ── T3: a count that reaches the end exactly is yielded, and reported ───────

TEST(FieldIteratorFault, T3_CountReachingEndExactlyReports) {
    expect_all_hooks("35=A|95=7|96=ab|49=S",
                     {{35, "A", kNone}, {95, "7", kNone}, {96, "ab|49=S", kLd}}, kLd, 95);
}

TEST(FieldIteratorFault, T3_CustomPair) {
    expect_custom_pair(
        "35=A|5001=7|5002=ab|49=S", {{35, "A", kNone}, {5001, "7", kNone}, {5002, "ab|49=S", kLd}},
        kLd, 5001, {{35, "A", kNone}, {5001, "7", kNone}, {5002, "ab", kNone}, {49, "S", kNone}});
}

// ── Sticky: the first fault wins over a later one of the other kind ─────────

TEST(FieldIteratorFault, FirstFaultIsKeptAcrossALaterFault) {
    // T1 first, then an S4 stop: the malformed tag stays the fault, and no
    // Length tag is recorded for the later mismatch.
    expect_all_hooks("=x|95=3|96=abcd|", {{0, "x", kTag}, {95, "3", kTag}}, kTag, 0);
}
