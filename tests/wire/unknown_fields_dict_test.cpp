// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/unknown_fields_dict_test.cpp
//
// W1 (`.specify/688-707-559-wire-headroom-token.md` §1.6 item 3, ruling R-W1-3): over a real
// dictionary, MessageView::unknown_fields() yields the fields whose tag the dictionary does not
// define (table_view::tag_defined), not the fields the MsgType does not declare. A tag the
// dictionary defines for another MsgType is the validator's wire_unexpected_tag, and on a FIX 5.0
// dictionary, whose QuickFIX XML has an empty <header/>, the standard header is defined, not
// unknown. Each undefined tag here is one the vendored XML does not define: re-check a choice with
// `grep -c "number='5000'" dictionaries/FIX44.xml`, which must print 0 (the files quote
// attributes with ', so the same grep for number='34' is its known positive).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::dict::Dictionary;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;

Dictionary load_real_dict(char const* file, std::pmr::memory_resource* mr) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / file;
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

std::vector<std::byte> make_frame(std::string_view begin_string, std::string_view body) {
    std::string pre = "8=" + std::string{begin_string} + "\x01" +
                      "9=" + std::to_string(body.size()) + "\x01" + std::string{body};
    unsigned sum = 0;
    for (unsigned char const c : pre) sum += c;
    sum %= 256U;
    std::string chk = "10=";
    chk.push_back(static_cast<char>('0' + ((sum / 100U) % 10U)));
    chk.push_back(static_cast<char>('0' + ((sum / 10U) % 10U)));
    chk.push_back(static_cast<char>('0' + (sum % 10U)));
    chk.push_back('\x01');
    pre += chk;
    std::vector<std::byte> out(pre.size());
    std::memcpy(out.data(), pre.data(), pre.size());
    return out;
}

std::vector<std::uint16_t> unknown_tags(fixpp::wire::MessageView<access_mode::Index> const& mv) {
    std::vector<std::uint16_t> tags;
    for (auto const& kv : mv.unknown_fields()) tags.push_back(kv.tag);
    return tags;
}

// A FIX 4.4 NewOrderSingle carrying MDReqID(262), which FIX44.xml defines for the market-data
// messages and NewOrderSingle does not declare, then one undefined tag. Only the undefined tag is
// unknown. The validator still rejects the frame at 262 as wire_unexpected_tag, the split the
// view leaves to it.
TEST(WireUnknownFieldsDict, TagDefinedForAnotherMsgTypeIsNotUnknownAndTheValidatorRejectsIt) {
    std::pmr::monotonic_buffer_resource dict_mr;
    Dictionary const dict = load_real_dict("FIX44.xml", &dict_mr);
    fixpp::dict::table_view const tv = dict.as_table_view();
    ASSERT_TRUE(tv.tag_defined(262)) << "the frame's misplaced tag must be one FIX44.xml defines";
    ASSERT_FALSE(tv.field_valid_for("D", 262)) << "and one NewOrderSingle does not declare";
    ASSERT_FALSE(tv.tag_defined(5000));

    auto const buf = make_frame("FIX.4.4",
                                "35=D\x01"
                                "34=1\x01"
                                "49=SENDER\x01"
                                "52=20240101-00:00:00\x01"
                                "56=TARGET\x01"
                                "262=REQ1\x01"
                                "11=CLORD1\x01"
                                "21=1\x01"
                                "55=X\x01"
                                "54=1\x01"
                                "60=20240101-00:00:00\x01"
                                "40=1\x01"
                                "5000=undefined\x01");
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{tv};
    auto const mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    std::vector<std::uint16_t> const expected{5000};
    EXPECT_EQ(unknown_tags(*mv), expected);

    fixpp::wire::dictionary_driven_validator v{dict.as_table_view()};
    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch{scratch_buf.data(), scratch_buf.size(),
                                                std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto const verdict = v.validate(*mv, &scratch, &ref_tag);
    ASSERT_FALSE(verdict.has_value()) << "the validator accepted a misplaced tag";
    EXPECT_EQ(verdict.error(), fixpp::core::error::wire_unexpected_tag);
    EXPECT_EQ(ref_tag, 262U);
}

// A FIX 5.0 SP2 NewOrderSingle with the standard header (34, 49, 52, 56) and one undefined tag.
// FIX50SP2.xml's <header/> is empty, so no MsgType declares the header tags, but the dictionary
// defines them: only the undefined tag is unknown.
TEST(WireUnknownFieldsDict, Fix50StandardHeaderIsNotUnknown) {
    std::pmr::monotonic_buffer_resource dict_mr;
    Dictionary const dict = load_real_dict("FIX50SP2.xml", &dict_mr);
    fixpp::dict::table_view const tv = dict.as_table_view();
    ASSERT_FALSE(tv.field_valid_for("D", 49)) << "FIX50SP2.xml's header is expected to be empty";
    ASSERT_TRUE(tv.tag_defined(49));
    ASSERT_FALSE(tv.tag_defined(5000));

    auto const buf = make_frame("FIXT.1.1",
                                "35=D\x01"
                                "34=1\x01"
                                "49=SENDER\x01"
                                "52=20240101-00:00:00\x01"
                                "56=TARGET\x01"
                                "11=CLORD1\x01"
                                "54=1\x01"
                                "60=20240101-00:00:00\x01"
                                "40=2\x01"
                                "5000=undefined\x01");
    auto const fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{tv};
    auto const mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    std::vector<std::uint16_t> const expected{5000};
    EXPECT_EQ(unknown_tags(*mv), expected);
}

}  // namespace
