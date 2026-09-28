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
//
// Validator cells: `dictionary_driven_validator::validate` over a view whose
// field walk meets each fault kind, reached through the public API — a view
// constructed directly over a malformed tag, and a view built under hooks that
// differ from the validator's for a Length/Data mismatch — plus clean controls
// whose view uses the validator's own hooks.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/tag_scan.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/frame_view_factory.hpp"

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

// ── Validator cells (data-model E-5) ────────────────────────────────────────
//
// `validate` must not report "conformant" for a walk that met an encoding
// fault. Each reject cell asserts the returned error and `*ref_tag_out`,
// seeded with a sentinel no fixture carries, so "untouched" cannot pass on a
// write of 0.

namespace {

using fixpp::core::error;
using fixpp::dict::field_type;
using fixpp::wire::dictionary_driven_validator;

constexpr std::uint16_t kRefSentinel = 0xBEEF;

// A whole frame around `body` ('|' for SOH). The checksum value is not
// checked by the frame_view factory.
std::vector<std::byte> make_frame(std::string_view body) {
    std::string full = "8=FIX.4.4|9=" + std::to_string(body.size()) + "|";
    full += body;
    full += "10=000|";
    return bytes_of(full);
}

// A failed build leaves msg_type() empty, so the validator's field walk passes
// a field only when the dictionary names it a FIXT framing tag. Every field in
// front of the fault in the malformed-tag fixtures is one, so the walk reaches
// the fault.
table_view make_framing_dict() {
    table_view_builder b;
    b.add_fixt_framing_tag(8, field_type::String);
    b.add_fixt_framing_tag(9, field_type::Length);
    b.add_fixt_framing_tag(10, field_type::String);
    b.add_fixt_framing_tag(35, field_type::String);
    b.add_fixt_framing_tag(49, field_type::String);
    return std::move(b).build();
}

// A malformed tag fails `OffsetTable::build` whatever the hooks, so the view is
// constructed directly and its failed build asserted first.
void expect_malformed_tag_rejected(std::string_view body, error build_error) {
    std::pmr::monotonic_buffer_resource mr;
    auto buf = make_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    MessageView<access_mode::Index> const mv{*fv, &mr, dict_hooks::none()};
    ASSERT_FALSE(mv.offsets().build_status().has_value()) << "precondition: the build fails";
    EXPECT_EQ(mv.offsets().build_status().error(), build_error);

    dictionary_driven_validator const v{make_framing_dict()};
    std::uint16_t ref = kRefSentinel;
    auto const r = v.validate(mv, &mr, &ref);
    ASSERT_FALSE(r.has_value()) << "a walk that met a malformed tag reported conformant";
    EXPECT_EQ(r.error(), error::wire_invalid_tag_number);
    EXPECT_EQ(ref, kRefSentinel)
        << "a malformed tag has no tag to report: RefTagID stays untouched";
}

constexpr std::string_view kPairXml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
                                      R"(<fields>)"
                                      R"(<field number='8' name='BeginString' type='STRING'/>)"
                                      R"(<field number='9' name='BodyLength' type='INT'/>)"
                                      R"(<field number='10' name='CheckSum' type='STRING'/>)"
                                      R"(<field number='35' name='MsgType' type='STRING'/>)"
                                      R"(<field number='49' name='SenderCompID' type='STRING'/>)"
                                      R"(<field number='95' name='RawDataLength' type='LENGTH'/>)"
                                      R"(<field number='96' name='RawData' type='DATA'/>)"
                                      R"(<field number='5001' name='CustomLen' type='LENGTH'/>)"
                                      R"(<field number='5002' name='CustomData' type='DATA'/>)"
                                      R"(</fields>)"
                                      R"(<messages>)"
                                      R"(<message name='TestMsg' msgtype='T' msgcat='app'>)"
                                      R"(<field name='BeginString' required='N'/>)"
                                      R"(<field name='BodyLength' required='N'/>)"
                                      R"(<field name='MsgType' required='N'/>)"
                                      R"(<field name='CheckSum' required='N'/>)"
                                      R"(<field name='SenderCompID' required='N'/>)"
                                      R"(<field name='RawDataLength' required='N'/>)"
                                      R"(<field name='RawData' required='N'/>)"
                                      R"(<field name='CustomLen' required='N'/>)"
                                      R"(<field name='CustomData' required='N'/>)"
                                      R"(</message>)"
                                      R"(</messages></fix>)";

table_view load_pair_dict(std::pmr::memory_resource* mr) {
    return fixpp::dict::XmlLoader{}.load_from_string(kPairXml, mr).as_table_view();
}

// The view splits by `none()`, which does not know the dictionary-only pair, so
// it builds; the validator walks by its own dictionary, which counts the Data
// value and meets the mismatch.
void expect_length_data_rejected(std::string_view body) {
    std::pmr::monotonic_buffer_resource mr;
    auto tv = load_pair_dict(&mr);
    ASSERT_EQ(tv.length_pair_data_tag(5001), 5002U) << "precondition: the custom pair registered";
    auto buf = make_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    MessageView<access_mode::Index> const mv{*fv, &mr, dict_hooks::none()};
    ASSERT_TRUE(mv.offsets().build_status().has_value()) << "precondition: the build succeeds";

    dictionary_driven_validator const v{tv};
    std::uint16_t ref = kRefSentinel;
    auto const r = v.validate(mv, &mr, &ref);
    ASSERT_FALSE(r.has_value()) << "a walk that met a Length/Data mismatch reported conformant";
    EXPECT_EQ(r.error(), error::wire_length_data_mismatch);
    EXPECT_EQ(ref, 5001U) << "RefTagID must be the Length tag";
}

// Well-formed: the view uses the validator's own hooks.
void expect_clean(std::string_view body) {
    std::pmr::monotonic_buffer_resource mr;
    auto tv = load_pair_dict(&mr);
    ASSERT_EQ(tv.length_pair_data_tag(5001), 5002U) << "precondition: the custom pair registered";
    auto buf = make_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    MessageView<access_mode::Index> const mv{*fv, &mr, dict_hooks::for_table_view(tv)};
    ASSERT_TRUE(mv.offsets().build_status().has_value()) << "precondition: the build succeeds";

    dictionary_driven_validator const v{tv};
    std::uint16_t ref = kRefSentinel;
    auto const r = v.validate(mv, &mr, &ref);
    EXPECT_TRUE(r.has_value()) << "a well-formed message was rejected";
    EXPECT_EQ(ref, kRefSentinel);
}

}  // namespace

// ── Malformed tag: E-4 S1–S3 stop the walk ──────────────────────────────────

TEST(ValidatorFieldFault, MalformedTag_S1_NonDigitTagByte) {
    expect_malformed_tag_rejected("35=T|4x=1|49=S|", error::wire_invalid_field_format);
}

TEST(ValidatorFieldFault, MalformedTag_S2_TagAbove0xFFFF) {
    expect_malformed_tag_rejected("35=T|65536=1|49=S|", error::wire_tag_out_of_range);
}

TEST(ValidatorFieldFault, MalformedTag_S3_NoEqualsBeforeSoh) {
    expect_malformed_tag_rejected("35=T|49|49=S|", error::wire_invalid_field_format);
}

// T1: the empty tag is yielded as tag 0. The fault check runs before the field
// checks, so it is not reported as an unexpected tag.
TEST(ValidatorFieldFault, MalformedTag_T1_EmptyTagIsNotUnexpectedTag) {
    expect_malformed_tag_rejected("35=T|=x|49=S|", error::wire_invalid_field_format);
}

// ── Length/Data: built under none(), validated under the dictionary's pair ──

TEST(ValidatorFieldFault, LengthData_S4_CountNotFollowedBySoh) {
    expect_length_data_rejected("35=T|5001=3|5002=abcd|49=S|");
}

TEST(ValidatorFieldFault, LengthData_T2_CountPastTheEnd) {
    expect_length_data_rejected("35=T|5001=99|5002=ab|49=S|");
}

TEST(ValidatorFieldFault, LengthData_T3_CountReachingTheEndExactly) {
    // The count covers "ab|49=S|10=000|", the rest of the frame.
    expect_length_data_rejected("35=T|5001=15|5002=ab|49=S|");
}

// ── Clean controls ──────────────────────────────────────────────────────────

TEST(ValidatorFieldFault, CleanStandardPairIsConformant) { expect_clean("35=T|95=3|96=a|c|49=S|"); }

TEST(ValidatorFieldFault, CleanDictionaryOnlyPairIsConformant) {
    expect_clean("35=T|5001=3|5002=a|c|49=S|");
}

// ── A failed build under an ordinary dictionary (T062a) ─────────────────────
//
// The cells above validate a failed-build view with a dictionary that names
// every field in front of the fault a FIXT framing tag, so the field walk
// reaches the fault. These cells use an ordinary FIX.4.4 dictionary, which
// names none of them one: a failed build leaves msg_type() empty, so the walk
// would reject tag 8 as an unexpected tag before it met the fault. FR-012
// still requires the fault's own code.

namespace {

// A view constructed directly under `none()`, whose build must fail.
void expect_failed_build_rejected(std::string_view body, error expected,
                                  std::uint16_t expected_ref) {
    std::pmr::monotonic_buffer_resource mr;
    auto tv = load_pair_dict(&mr);
    ASSERT_FALSE(tv.is_fixt_framing_tag(8)) << "precondition: an ordinary dictionary";
    auto buf = make_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    MessageView<access_mode::Index> const mv{*fv, &mr, dict_hooks::none()};
    ASSERT_FALSE(mv.offsets().build_status().has_value()) << "precondition: the build fails";
    ASSERT_TRUE(mv.msg_type().empty()) << "precondition: a failed build has no MsgType";

    dictionary_driven_validator const v{tv};
    std::uint16_t ref = kRefSentinel;
    auto const r = v.validate(mv, &mr, &ref);
    ASSERT_FALSE(r.has_value()) << "a failed-build view reported conformant";
    EXPECT_EQ(r.error(), expected);
    EXPECT_EQ(ref, expected_ref);
}

}  // namespace

TEST(ValidatorFieldFault, FailedBuild_OrdinaryDict_MalformedTag) {
    expect_failed_build_rejected("35=T|4x=1|49=S|", error::wire_invalid_tag_number, kRefSentinel);
}

// `none()` pairs the standard RawDataLength(95) with RawData(96), so the build
// fails on the mismatch too.
TEST(ValidatorFieldFault, FailedBuild_OrdinaryDict_LengthDataMismatch) {
    expect_failed_build_rejected("35=T|95=3|96=abcd|49=S|", error::wire_length_data_mismatch, 95);
}

// A build that failed for a reason other than an encoding fault (here the
// offset-table cap) leaves a walk with no fault, so validate behaves as it did
// before the fault pre-scan: the empty MsgType makes tag 8 an unexpected tag.
TEST(ValidatorFieldFault, FailedBuild_OrdinaryDict_CapWithNoFaultFallsThrough) {
    std::string body = "35=T|";
    for (std::size_t i = 0; i < fixpp::wire::default_max_offset_entries; ++i) {
        body += "49=S|";
    }
    std::pmr::monotonic_buffer_resource mr;
    auto tv = load_pair_dict(&mr);
    ASSERT_FALSE(tv.is_fixt_framing_tag(8)) << "precondition: an ordinary dictionary";
    auto buf = make_frame(body);
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    MessageView<access_mode::Index> const mv{*fv, &mr, dict_hooks::none()};
    ASSERT_FALSE(mv.offsets().build_status().has_value()) << "precondition: the build fails";
    EXPECT_EQ(mv.offsets().build_status().error(), error::wire_offset_table_full);

    dictionary_driven_validator const v{tv};
    std::uint16_t ref = kRefSentinel;
    auto const r = v.validate(mv, &mr, &ref);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::wire_unexpected_tag);
    EXPECT_EQ(ref, 8U);
}
