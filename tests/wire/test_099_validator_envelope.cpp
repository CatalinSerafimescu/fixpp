// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/wire/test_099_validator_envelope.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-3's unit half (tasks.md T026):
// dictionary_driven_validator::validate(…, envelope) over real tables, in data-model E-4's terms.
// S, the envelope, judges a header tag it declares, except the four tags the route answers. The
// validator's table is the routed one. A plain table with a null envelope is the virtual validate's
// walk.
//
// The Session half of the same cells is tests/session/test_099_session_overlay.cpp. Their mutants
// are M-12a to M-12e.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <fstream>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

namespace dd = fixpp::dict;
using fixpp::core::error;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::Parser;

constexpr const char* kFixtHead = "<fix type='FIXT' major='1' minor='1' servicepack='0'>";
constexpr const char* k50Head = "<fix type='FIX' major='5' minor='0' servicepack='2'>";

std::pmr::memory_resource* dict_resource() {
    // Never destroyed, so that it outlives every static dictionary in this file, whichever was
    // constructed first.
    static auto* const mr = new std::pmr::monotonic_buffer_resource;
    return mr;
}

const dd::Dictionary& load_plain(const char* file) {
    static std::vector<std::pair<std::string, std::unique_ptr<const dd::Dictionary>>> cache;
    for (const auto& [name, d] : cache) {
        if (name == file) {
            return *d;
        }
    }
    cache.emplace_back(file,
                       std::make_unique<const dd::Dictionary>(dd::load_any(
                           std::filesystem::path{FIXPP_DICT_DATA_DIR} / file, dict_resource())));
    return *cache.back().second;
}

std::unique_ptr<const dd::Dictionary> overlay_on(const char* file, const std::string& overlay) {
    std::ifstream in{std::filesystem::path{FIXPP_DICT_DATA_DIR} / file, std::ios::binary};
    const std::string base{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return std::make_unique<const dd::Dictionary>(
        dd::XmlLoader{}.load_with_overlay_from_string(base, overlay, dict_resource()));
}

// A frame's own bytes, its BodyLength and CheckSum computed.
std::vector<std::byte> wire(const std::string& body) {
    const std::string pre =
        "8=FIXT.1.1\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (const unsigned char c : pre) {
        sum += c;
    }
    std::string chk = std::to_string(sum % 256U);
    chk.insert(0, 3 - chk.size(), '0');
    const std::string full = pre + "10=" + chk + "\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

const std::string& kHdr() {
    static const std::string value =
        "34=2\x01"
        "49=A\x01"
        "52=20240101-00:00:00\x01"
        "56=B\x01";
    return value;
}
const std::string& kNosBody() {
    static const std::string value =
        "11=C\x01"
        "21=1\x01"
        "55=X\x01"
        "54=1\x01"
        "60=20240101-00:00:00\x01"
        "40=1\x01"
        "38=1\x01";
    return value;
}

// A NewOrderSingle: `before_34` sits right after 35, as a route's 1128 does.
std::string nos(const std::string& header_extra, const std::string& before_34 = "1128=9\x01") {
    return "35=D\x01" + before_34 + kHdr() + header_extra + kNosBody();
}

struct Verdict {
    bool ok = false;
    std::optional<error> err;
    std::uint16_t ref_tag = 0;
    friend bool operator==(const Verdict&, const Verdict&) = default;
};

std::ostream& operator<<(std::ostream& os, const Verdict& v) {
    os << (v.ok ? "pass" : "reject") << " ref_tag=" << v.ref_tag;
    if (v.err) {
        os << " err=" << static_cast<int>(*v.err);
    }
    return os;
}

// The frame parsed with the routed table, then validated by a validator holding that table.
Verdict run_validate(const dictionary_driven_validator& v, const std::string& body,
                     const dd::table_view* envelope) {
    const std::vector<std::byte> buf = wire(body);
    std::array<std::byte, 8192> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto fv = fixpp::wire::test::make_frame_view(buf);
    EXPECT_TRUE(fv.has_value());
    Parser<access_mode::Index> parser{v.table()};
    auto mv = parser.parse(*fv, &arena);
    EXPECT_TRUE(mv.has_value());
    Verdict out;
    const auto r = v.validate(*mv, &arena, &out.ref_tag, envelope);
    out.ok = r.has_value();
    if (!r) {
        out.err = r.error();
    }
    return out;
}

constexpr Verdict kPass{.ok = true, .err = std::nullopt, .ref_tag = 0};
constexpr Verdict rejected(error e, std::uint16_t tag) {
    return {.ok = false, .err = e, .ref_tag = tag};
}

// ── Header tags an overlay adds to S, on a table that does not declare them ──────────────────

const dd::Dictionary& fixt11_with_header_tags() {
    static const auto d = overlay_on(
        "FIXT11.xml", std::string(kFixtHead) +
                          "<header><field name='VenueHdrScalar' required='N' />"
                          "<field name='VenueHdrEnum' required='N' />"
                          "<field name='VenueHdrInt' required='N' /></header><fields>"
                          "<field number='5020' name='VenueHdrScalar' type='STRING' />"
                          "<field number='5021' name='VenueHdrEnum' type='CHAR'>"
                          "<value enum='A' description='ALPHA'/>"
                          "<value enum='B' description='BETA'/></field>"
                          "<field number='5022' name='VenueHdrInt' type='INT' /></fields></fix>");
    return *d;
}

const dictionary_driven_validator& fix50sp2_route() {
    static const dictionary_driven_validator v{
        load_plain("FIX50SP2.xml").as_table_view(dd::fixt_framing)};
    return v;
}

const dd::table_view& envelope_header_tags() {
    static const dd::table_view tv = fixt11_with_header_tags().as_table_view();
    return tv;
}

struct HeaderCell {
    const char* name{};
    const char* field{};
    Verdict with_envelope;
};

const std::vector<HeaderCell>& kHeaderCells() {
    static const std::vector<HeaderCell> value = {
        {.name = "Scalar", .field = "5020=v\x01", .with_envelope = kPass},
        {.name = "EnumValid", .field = "5021=A\x01", .with_envelope = kPass},
        {.name = "EnumInvalid",
         .field = "5021=Z\x01",
         .with_envelope = rejected(error::wire_field_value_out_of_range, 5021)},
        {.name = "IntValid", .field = "5022=7\x01", .with_envelope = kPass},
        {.name = "IntInvalid",
         .field = "5022=abc\x01",
         .with_envelope = rejected(error::wire_field_value_out_of_range, 5022)},
    };
    return value;
}

class W3UnitHeaderTag : public testing::TestWithParam<HeaderCell> {};

TEST_P(W3UnitHeaderTag, JudgedByTheEnvelope) {
    const HeaderCell& c = GetParam();
    EXPECT_EQ(run_validate(fix50sp2_route(), nos(c.field), &envelope_header_tags()),
              c.with_envelope);
}

// The comparand: with no envelope the routed table, which does not declare the tag, refuses it. So
// a pass above is the envelope's doing, not the route's. FIX50SP2 does not define these custom
// tags (field-definition grep), so the refusal is an undefined tag (101 FR-002 supersedes this cell's wire_unexpected_tag).
TEST_P(W3UnitHeaderTag, NoEnvelopeRoutedTableRefuses) {
    const HeaderCell& c = GetParam();
    const std::uint16_t tag = static_cast<std::uint16_t>(std::stoi(std::string{c.field}));
    EXPECT_EQ(run_validate(fix50sp2_route(), nos(c.field), nullptr),
              rejected(error::wire_undefined_tag, tag));
}

INSTANTIATE_TEST_SUITE_P(W3Unit, W3UnitHeaderTag, testing::ValuesIn(kHeaderCells()),
                         [](const testing::TestParamInfo<HeaderCell>& i) { return i.param.name; });

// A tag S's header does not declare is the routed table's to judge, with an envelope or without.
// FIX50SP2 does not define 5099, so the route answers an undefined tag (101 FR-002 supersedes this
// cell's wire_unexpected_tag).
TEST(W3Unit, TagSDoesNotDeclare_TheRouteAnswers) {
    EXPECT_EQ(run_validate(fix50sp2_route(), nos("5099=v\x01"), &envelope_header_tags()),
              rejected(error::wire_undefined_tag, 5099));
}

// ── Collision: S's header closes 5040 at {X}, the routed NewOrderSingle closes it at {Y} ─────

const dd::Dictionary& fixt11_with_collision_tags() {
    static const auto d =
        overlay_on("FIXT11.xml", std::string(kFixtHead) +
                                     "<header><field name='VenueCollide' required='N' />"
                                     "<field name='VenueOther' required='N' /></header><fields>"
                                     "<field number='5040' name='VenueCollide' type='STRING'>"
                                     "<value enum='X' description='EX'/></field>"
                                     "<field number='5041' name='VenueOther' type='STRING'>"
                                     "<value enum='X' description='EX'/></field></fields></fix>");
    return *d;
}

const dictionary_driven_validator& collision_route() {
    static const auto d = overlay_on(
        "FIX50SP2.xml", std::string(k50Head) +
                            "<messages><message name='NewOrderSingle' msgtype='D' msgcat='app'>"
                            "<field name='VenueCollide' required='N' /></message>"
                            "<message name='ExecutionReport' msgtype='8' msgcat='app'>"
                            "<field name='VenueOther' required='N' /></message></messages><fields>"
                            "<field number='5040' name='VenueCollide' type='STRING'>"
                            "<value enum='Y' description='WHY'/></field>"
                            "<field number='5041' name='VenueOther' type='STRING'>"
                            "<value enum='Y' description='WHY'/></field></fields></fix>");
    static const dictionary_driven_validator v{d->as_table_view(dd::fixt_framing)};
    return v;
}

const dd::table_view& envelope_collision() {
    static const dd::table_view tv = fixt11_with_collision_tags().as_table_view();
    return tv;
}

TEST(W3UnitCollision, SValuePasses) {
    ASSERT_TRUE(collision_route().table().field_valid_for("D", 5040))
        << "the routed table must declare it on this message, or nothing collides";
    EXPECT_EQ(run_validate(collision_route(), nos("5040=X\x01"), &envelope_collision()), kPass);
}

TEST(W3UnitCollision, RoutedValueRefused) {
    EXPECT_EQ(run_validate(collision_route(), nos("5040=Y\x01"), &envelope_collision()),
              rejected(error::wire_field_value_out_of_range, 5040));
}

// The comparand: with no envelope the same two values read the other way round.
TEST(W3UnitCollision, NoEnvelopeTheRouteAnswers) {
    EXPECT_EQ(run_validate(collision_route(), nos("5040=Y\x01"), nullptr), kPass);
    EXPECT_EQ(run_validate(collision_route(), nos("5040=X\x01"), nullptr),
              rejected(error::wire_field_value_out_of_range, 5040));
}

TEST(W3UnitCollision, TagOnAnotherMsgTypeOnly_Control) {
    ASSERT_FALSE(collision_route().table().field_valid_for("D", 5041));
    ASSERT_TRUE(collision_route().table().field_valid_for("8", 5041));
    EXPECT_EQ(run_validate(collision_route(), nos("5041=X\x01"), &envelope_collision()), kPass);
}

// ── The four tags the route answers, S closing each where the routed table answers otherwise ──

TEST(W3UnitFourTags, Tag1128_10_RoutesToLatest) {
    static const dictionary_driven_validator latest{
        load_plain("orchestra/OrchestraFIXLatest.xml").as_table_view(dd::fixt_framing)};
    static const dd::table_view fixt = load_plain("FIXT11.xml").as_table_view();
    ASSERT_FALSE(fixt.enum_valid(1128, std::as_bytes(std::span{"10", 2})))
        << "FIXT11.xml must close 1128 short of 10, or nothing is being tested";
    ASSERT_TRUE(fixt.is_header_tag(1128));
    EXPECT_EQ(run_validate(latest, nos("", "1128=10\x01"), &fixt), kPass);
}

// CU, PartyEntitlementsRequest: FIX50SP2 has it, FIX44 closes 35 without it, and it requires no
// field.
TEST(W3UnitFourTags, Tag35_OnFix44S_AFix50sp2OnlyMsgType) {
    static const dd::table_view fix44 = load_plain("FIX44.xml").as_table_view();
    ASSERT_FALSE(fix44.enum_valid(35, std::as_bytes(std::span{"CU", 2})))
        << "FIX44.xml must close 35 short of CU, or nothing is being tested";
    ASSERT_TRUE(fix44.is_header_tag(35));
    EXPECT_EQ(run_validate(fix50sp2_route(),
                           "35=CU\x01"
                           "1128=9\x01" +
                               kHdr(),
                           &fix44),
              kPass);
}

const dd::table_view& fixt11_closing_1129_1156() {
    static const auto d = overlay_on(
        "FIXT11.xml", std::string(kFixtHead) +
                          "<fields><field number='1129' name='CstmApplVerID' type='STRING'>"
                          "<value enum='Z' description='ZED'/></field>"
                          "<field number='1156' name='ApplExtID' type='INT'>"
                          "<value enum='7' description='SEVEN'/></field></fields></fix>");
    static const dd::table_view tv = d->as_table_view();
    return tv;
}

TEST(W3UnitFourTags, Tag1129_ByTheRoute) {
    const dd::table_view& s = fixt11_closing_1129_1156();
    ASSERT_FALSE(s.enum_valid(1129, std::as_bytes(std::span{"other", 5})));
    ASSERT_TRUE(s.is_header_tag(1129));
    EXPECT_EQ(run_validate(fix50sp2_route(), nos("1129=other\x01"), &s), kPass);
}

TEST(W3UnitFourTags, Tag1156_ByTheRoute) {
    const dd::table_view& s = fixt11_closing_1129_1156();
    ASSERT_FALSE(s.enum_valid(1156, std::as_bytes(std::span{"303", 3})));
    ASSERT_TRUE(s.is_header_tag(1156));
    EXPECT_EQ(run_validate(fix50sp2_route(), nos("1156=303\x01"), &s), kPass);
}

// ── A null envelope equals the virtual validate ──────────────────────────────────────────────

struct NullCell {
    const char* name{};
    std::string body;
};

class W3UnitNullEnvelope : public testing::TestWithParam<NullCell> {};

TEST_P(W3UnitNullEnvelope, EqualsTheVirtual) {
    const dictionary_driven_validator& v = fix50sp2_route();
    const fixpp::wire::Validator& base = v;

    const std::vector<std::byte> buf = wire(GetParam().body);
    std::array<std::byte, 8192> stack{};
    std::pmr::monotonic_buffer_resource arena{stack.data(), stack.size(),
                                              std::pmr::null_memory_resource()};
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    Parser<access_mode::Index> parser{v.table()};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    std::uint16_t ref_virtual = 0;
    std::uint16_t ref_null = 0;
    const auto through_virtual = base.validate(*mv, &arena, &ref_virtual);
    const auto through_null = v.validate(*mv, &arena, &ref_null, nullptr);
    EXPECT_EQ(through_virtual.has_value(), through_null.has_value());
    if (!through_virtual && !through_null) {
        EXPECT_EQ(through_virtual.error(), through_null.error());
    }
    EXPECT_EQ(ref_virtual, ref_null);
}

INSTANTIATE_TEST_SUITE_P(W3Unit, W3UnitNullEnvelope,
                         testing::Values(NullCell{"Pass", nos("")},
                                         NullCell{"UnexpectedTag", nos("5020=v\x01")},
                                         NullCell{"TypeRefused",
                                                  "35=D\x01"
                                                  "1128=9\x01"
                                                  "34=abc\x01"
                                                  "49=A\x01"
                                                  "52=20240101-00:00:00\x01"
                                                  "56=B\x01" +
                                                      kNosBody()},
                                         NullCell{"RequiredMissing",
                                                  "35=D\x01"
                                                  "1128=9\x01" +
                                                      kHdr()}),
                         [](const testing::TestParamInfo<NullCell>& i) { return i.param.name; });

}  // namespace
