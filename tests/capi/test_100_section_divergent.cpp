// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/test_100_section_divergent.cpp — 100-wire-group-read-integrity, tasks T011 (US1;
// FR-001, FR-003; research D2 "section arm" and "Per-occurrence records", D9 section-arm table,
// D9 divergent definitions).
//
// Custom dictionaries loaded with `XmlLoader{}.load_from_string` and the Orchestra loader's
// `load_from_string`, each frame parsed with the loaded dictionary's `as_table_view()` and read
// through the C getters as tests/capi/test_100_group_getters.cpp reads them.
//
// Section-arm cells (D9's table): each states the ending research D9's "Target" column names.
// Divergent-definition cells (shapes (A), (B), (C)): one count tag defined differently at two
// places; each read has two instances with distinct slice boundaries, and each cell asserts the
// cursor count and, per instance, the values of its first and last tag through
// `fixpp_group_get_field_string`. Each divergent cell first asserts its precondition from the
// built table view's bare accessors: the delimiter and the members the bare store holds for the
// count tag differ from the occurrence it reads (or, for a control, equal it). In every witness
// dictionary the two member sets are disjoint from each other and from the field after the
// group.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "group_getters_100_support.hpp"
#include "support/frame_view_factory.hpp"
#include "support/group_read_100.hpp"

namespace {

using fixpp::test100::frame44;
using fixpp::test100::get_group;
using fixpp::test100::get_nested;
using fixpp::test100::inbound_handle;
using fixpp::test100::rc_name;
using fixpp::test100::read_out;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

struct custom_dict {
    fixpp::dict::Dictionary dict;
    fixpp::dict::table_view tv;
};

custom_dict load_xml(std::string const& xml) {
    auto d = fixpp::dict::XmlLoader{}.load_from_string(xml, std::pmr::get_default_resource());
    auto tv = d.as_table_view();
    return {std::move(d), std::move(tv)};
}
custom_dict load_orch(std::string const& xml) {
    auto d = fixpp::dict::OrchestraLoader{}.load_from_string(xml, std::pmr::get_default_resource());
    auto tv = d.as_table_view();
    return {std::move(d), std::move(tv)};
}

// A FIX 4.4 XML dictionary: `fields` (number, name, type), `header`, `trailer`, `messages`,
// `components` element bodies.
struct xf {
    int number;
    char const* name;
    char const* type;
};
std::string xml_dict(std::vector<xf> const& fields, std::string const& header,
                     std::string const& trailer, std::string const& messages,
                     std::string const& components = {}) {
    std::string x = "<fix type='FIX' major='4' minor='4' servicepack='0'>";
    x += "<header><field name='MsgType' required='N'/>" + header + "</header>";
    x += "<trailer>" + trailer + "</trailer>";
    x += "<messages>" + messages + "</messages>";
    x += "<components>" + components + "</components><fields>";
    x += "<field number='8' name='BeginString' type='STRING'/>";
    x += "<field number='9' name='BodyLength' type='INT'/>";
    x += "<field number='10' name='CheckSum' type='STRING'/>";
    x += "<field number='35' name='MsgType' type='STRING'/>";
    x += "<field number='58' name='Text' type='STRING'/>";
    for (auto const& f : fields) {
        x += "<field number='" + std::to_string(f.number) + "' name='" + f.name + "' type='" +
             f.type + "'/>";
    }
    return x + "</fields></fix>";
}
std::string xfield(char const* name) {
    return std::string{"<field name='"} + name + "' required='N'/>";
}
std::string xgroup(char const* name, std::string const& body) {
    return std::string{"<group name='"} + name + "' required='N'>" + body + "</group>";
}
std::string xcomp(char const* name) {
    return std::string{"<component name='"} + name + "' required='N'/>";
}
std::string xmsg(char const* type, std::string const& body) {
    return std::string{"<message name='M"} + type + "' msgtype='" + type + "' msgcat='app'>" +
           body + "</message>";
}

// An Orchestra repository: `fields` (id, name, type), then the components (1024 and 1025 are
// the header and trailer), groups and messages element bodies.
std::string orch_dict(std::vector<xf> const& fields, std::string const& header_refs,
                      std::string const& trailer_refs, std::string const& components,
                      std::string const& groups, std::string const& messages) {
    std::string x =
        "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' "
        "name='FIX' version='FIX.Latest_EP303'><fixr:datatypes><fixr:datatype name='String'/>"
        "<fixr:datatype name='int'/><fixr:datatype name='NumInGroup'/><fixr:datatype "
        "name='Length'/></fixr:datatypes><fixr:fields>"
        "<fixr:field id='8' name='BeginString' type='String'/>"
        "<fixr:field id='9' name='BodyLength' type='Length'/>"
        "<fixr:field id='10' name='CheckSum' type='String'/>"
        "<fixr:field id='35' name='MsgType' type='String'/>"
        "<fixr:field id='58' name='Text' type='String'/>";
    for (auto const& f : fields) {
        x += "<fixr:field id='" + std::to_string(f.number) + "' name='" + f.name + "' type='" +
             f.type + "'/>";
    }
    x += "</fixr:fields><fixr:components>";
    x += "<fixr:component id='1024' name='StandardHeader'><fixr:fieldRef id='35'/>" + header_refs +
         "</fixr:component>";
    x += "<fixr:component id='1025' name='StandardTrailer'>" + trailer_refs + "</fixr:component>";
    x += components + "</fixr:components><fixr:groups>" + groups + "</fixr:groups><fixr:messages>" +
         messages + "</fixr:messages></fixr:repository>";
    return x;
}
std::string ofield(int id) { return "<fixr:fieldRef id='" + std::to_string(id) + "'/>"; }
std::string ogroupref(int id) { return "<fixr:groupRef id='" + std::to_string(id) + "'/>"; }
std::string ocompref(int id) { return "<fixr:componentRef id='" + std::to_string(id) + "'/>"; }
std::string ogroup(int id, int num_in_group, std::string const& body) {
    return "<fixr:group id='" + std::to_string(id) + "' name='G" + std::to_string(id) +
           "'><fixr:numInGroup id='" + std::to_string(num_in_group) + "'/>" + body +
           "</fixr:group>";
}
std::string ocomp(int id, std::string const& body) {
    return "<fixr:component id='" + std::to_string(id) + "' name='C" + std::to_string(id) + "'>" +
           body + "</fixr:component>";
}
std::string omsg(char const* type, std::string const& body) {
    return std::string{"<fixr:message name='M"} + type + "' msgType='" + type +
           "'><fixr:structure>" + ocompref(1024) + body + ocompref(1025) +
           "</fixr:structure></fixr:message>";
}

// A frame parsed with `tv`.
struct parsed {
    std::vector<std::byte> buf;
    std::optional<fixpp::wire::frame_view> fv;
    std::pmr::monotonic_buffer_resource arena;
    std::optional<MessageView<access_mode::Index>> mv;
    parsed(fixpp::dict::table_view const& tv, std::string const& body) : buf(frame44(body)) {
        auto f = fixpp::wire::test::make_frame_view(buf);
        if (!f) {
            return;
        }
        fv.emplace(*f);
        Parser<access_mode::Index> parser{tv};
        auto r = parser.parse(*fv, &arena);
        if (r) {
            mv.emplace(std::move(*r));
        }
    }
};

std::string field_value(fixpp_group_t const* g, std::size_t i, std::uint16_t tag) {
    char const* v = nullptr;
    std::size_t len = 0;
    if (fixpp_group_get_field_string(g, i, tag, &v, &len) != FIXPP_ERR_OK || v == nullptr) {
        return "<absent>";
    }
    return {v, len};
}

// The expected shape of a read: `n` instances, instance i's first tag `first` holding "d<i>"
// and last tag `last` holding "l<i>"; or a code with no cursor.
struct want {
    int rc = FIXPP_ERR_OK;
    std::size_t n = 0;
    std::uint16_t first = 0;
    std::uint16_t last = 0;
};

void expect_read(read_out const& r, want const& w, std::string const& what) {
    if (w.rc != FIXPP_ERR_OK || w.n == 0) {
        EXPECT_EQ(r.rc, w.rc) << what << ": expected " << rc_name(w.rc) << "; actual "
                              << rc_name(r.rc) << " (count " << r.count << ")";
        EXPECT_EQ(r.count, 0U) << what << ": count";
        return;
    }
    EXPECT_EQ(r.rc, FIXPP_ERR_OK) << what << ": expected a cursor of " << w.n << "; actual "
                                  << rc_name(r.rc);
    EXPECT_EQ(r.count, w.n) << what << ": instance count";
    if (r.rc != FIXPP_ERR_OK || r.count != w.n || r.g == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < w.n; ++i) {
        EXPECT_EQ(field_value(r.g, i, w.first), "d" + std::to_string(i))
            << what << ": instance " << i << "'s first tag " << w.first;
        EXPECT_EQ(field_value(r.g, i, w.last), "l" + std::to_string(i))
            << what << ": instance " << i << "'s last tag " << w.last;
    }
}

// The flat read of `tag`, asserted against `w` while the view is alive.
void flat_expect(custom_dict const& d, std::string const& body, std::uint16_t tag, want const& w,
                 std::string const& what) {
    parsed const p{d.tv, body};
    ASSERT_TRUE(p.mv.has_value()) << what << ": setup: the frame did not parse";
    inbound_handle const h{*p.mv};
    expect_read(get_group(h.ptr(), tag), w, what);
}

// The nested read of `nested_tag` in instance 0 of the top-level `outer_tag`, asserted against
// `w` while the view is alive.
void nested_expect(custom_dict const& d, std::string const& body, std::uint16_t outer_tag,
                   std::uint16_t nested_tag, want const& w, std::string const& what) {
    parsed const p{d.tv, body};
    ASSERT_TRUE(p.mv.has_value()) << what << ": setup: the frame did not parse";
    inbound_handle const h{*p.mv};
    auto const outer = get_group(h.ptr(), outer_tag);
    ASSERT_EQ(outer.rc, FIXPP_ERR_OK) << what << ": setup: the outer read " << rc_name(outer.rc);
    ASSERT_GE(outer.count, 1U) << what << ": setup: the outer instance";
    expect_read(get_nested(outer.g, 0, nested_tag), w, what);
}

// Two instances of a group whose first tag is `first` and last tag `last`: "<first>=d<i>",
// "<last>=l<i>".
std::string two_instances(std::uint16_t no_tag, std::uint16_t first, std::uint16_t last) {
    std::string s = std::to_string(no_tag) + "=2\x01";
    for (int i = 0; i < 2; ++i) {
        s += std::to_string(first) + "=d" + std::to_string(i) + "\x01";
        s += std::to_string(last) + "=l" + std::to_string(i) + "\x01";
    }
    return s;
}

std::string const kText = "58=t\x01";
std::string head(char const* msg_type) { return std::string{"35="} + msg_type + "\x01"; }

// Bare-store precondition: whether the delimiter and the member set the bare store holds for
// `no_tag` equal `delim` and `members`.
bool bare_equals(fixpp::dict::table_view const& tv, std::uint16_t no_tag, std::uint16_t delim,
                 std::vector<std::uint16_t> members) {
    auto const span = tv.group_member_tags(no_tag);
    std::vector<std::uint16_t> bare{span.begin(), span.end()};
    std::ranges::sort(bare);
    std::ranges::sort(members);
    return tv.group_first_field(no_tag) == delim && bare == members;
}
bool bare_differs_in_both(fixpp::dict::table_view const& tv, std::uint16_t no_tag,
                          std::uint16_t delim, std::vector<std::uint16_t> members) {
    auto const span = tv.group_member_tags(no_tag);
    std::vector<std::uint16_t> bare{span.begin(), span.end()};
    std::ranges::sort(bare);
    std::ranges::sort(members);
    return tv.group_first_field(no_tag) != delim && bare != members;
}

// ════════════════════════════════════════════════════════════════════════════════
// Section arm, XML (research D9's table)
// ════════════════════════════════════════════════════════════════════════════════

// (i-a), (i-b), (i-c): kScalarReuseXml's shape with the scalar NoGood(600) moved into the
// header; V1 declares NoGood as a body group with FieldA(610).
custom_dict const& dict_i() {
    static custom_dict const d = load_xml(
        xml_dict({{600, "NoGood", "NUMINGROUP"}, {610, "FieldA", "STRING"}}, xfield("NoGood"), "",
                 xmsg("V1", xfield("Text") + xgroup("NoGood", xfield("FieldA"))) +
                     xmsg("V2", xfield("Text"))));
    return d;
}
TEST(SectionArm100, Ia_HeaderScalarReadInV2_TypeMismatch) {
    flat_expect(dict_i(),
                head("V2") +
                    "600=2\x01"
                    "610=a\x01"
                    "610=b\x01" +
                    kText,
                600, {.rc = FIXPP_ERR_TYPE_MISMATCH}, "(i-a) in V2 / FR-003");
}
TEST(SectionArm100, Ia_HeaderScalarReadInUndeclaredType_TypeMismatch) {
    flat_expect(dict_i(),
                head("ZZ") +
                    "600=2\x01"
                    "610=a\x01"
                    "610=b\x01" +
                    kText,
                600, {.rc = FIXPP_ERR_TYPE_MISMATCH}, "(i-a) in an undeclared type / FR-003");
}
TEST(SectionArm100, Ib_Control_CountZero_TypeMismatch) {
    flat_expect(dict_i(),
                head("V2") +
                    "600=0\x01"
                    "610=a\x01" +
                    kText,
                600, {.rc = FIXPP_ERR_TYPE_MISMATCH}, "(i-b) (control)");
}
TEST(SectionArm100, Ic_Control_NonMemberFollows_TypeMismatch) {
    flat_expect(dict_i(), head("V2") + "600=2\x01" + kText, 600, {.rc = FIXPP_ERR_TYPE_MISMATCH},
                "(i-c) (control)");
}

// (ii), (ii-0): a trailer group NoT(9120){9121, 9122}.
custom_dict const& dict_ii() {
    static custom_dict const d = load_xml(
        xml_dict({{9120, "NoT", "NUMINGROUP"}, {9121, "T1", "STRING"}, {9122, "T2", "STRING"}}, "",
                 xgroup("NoT", xfield("T1") + xfield("T2")), xmsg("M", xfield("Text"))));
    return d;
}
TEST(SectionArm100, Ii_Control_TrailerGroupInUndeclaredType_Cursor) {
    flat_expect(dict_ii(), head("ZZ") + kText + two_instances(9120, 9121, 9122), 9120,
                {.n = 2, .first = 9121, .last = 9122}, "(ii) (control)");
}
TEST(SectionArm100, Ii_Control_TrailerGroupInItsDeclaredMessage_Cursor) {
    flat_expect(dict_ii(), head("M") + kText + two_instances(9120, 9121, 9122), 9120,
                {.n = 2, .first = 9121, .last = 9122}, "(ii) declared message (control)");
}
TEST(SectionArm100, Ii0_TrailerGroupCountZeroInUndeclaredType_OkZero) {
    flat_expect(dict_ii(), head("ZZ") + kText + "9120=0\x01", 9120, {.rc = FIXPP_ERR_OK},
                "(ii-0) / FR-002");
}

// (iii): a group NoB(603) nested in a header group NoA(601), read with the nested getter.
custom_dict const& dict_iii() {
    static custom_dict const d =
        load_xml(xml_dict({{601, "NoA", "NUMINGROUP"},
                           {602, "A1", "STRING"},
                           {603, "NoB", "NUMINGROUP"},
                           {604, "B1", "STRING"},
                           {605, "B2", "STRING"}},
                          xgroup("NoA", xfield("A1") + xgroup("NoB", xfield("B1") + xfield("B2"))),
                          "", xmsg("M", xfield("Text"))));
    return d;
}
TEST(SectionArm100, Iii_Control_NestedInHeaderGroupUndeclaredType_Cursor) {
    nested_expect(dict_iii(),
                  head("ZZ") +
                      "601=1\x01"
                      "602=a\x01" +
                      two_instances(603, 604, 605) + kText,
                  601, 603, {.n = 2, .first = 604, .last = 605}, "(iii) (control)");
}

// (iv): component C holds NoB(603){604}; C is referenced first inside body group NoA(601) and
// then at the message root.
custom_dict const& dict_iv() {
    static custom_dict const d = load_xml(xml_dict(
        {{601, "NoA", "NUMINGROUP"},
         {602, "A1", "STRING"},
         {603, "NoB", "NUMINGROUP"},
         {604, "B1", "STRING"},
         {605, "B2", "STRING"}},
        "", "", xmsg("M", xgroup("NoA", xfield("A1") + xcomp("C")) + xcomp("C") + xfield("Text")),
        "<component name='C'>" + xgroup("NoB", xfield("B1") + xfield("B2")) + "</component>"));
    return d;
}
TEST(SectionArm100, Iv_Control_ComponentGroupAtRootAfterNestedUse_Cursor) {
    auto const ref = dict_iv().dict.field_ref("M", 603);
    ASSERT_EQ(ref.group_no_tag, 601U)
        << "(iv) precondition: the surviving NoB FieldRef is the one inside NoA";
    flat_expect(dict_iv(), head("M") + two_instances(603, 604, 605) + kText, 603,
                {.n = 2, .first = 604, .last = 605}, "(iv) (control)");
}

// (v): C referenced inside two header groups NoA(601) and NoZ(606).
custom_dict const& dict_v() {
    static custom_dict const d = load_xml(xml_dict(
        {{601, "NoA", "NUMINGROUP"},
         {602, "A1", "STRING"},
         {603, "NoB", "NUMINGROUP"},
         {604, "B1", "STRING"},
         {605, "B2", "STRING"},
         {606, "NoZ", "NUMINGROUP"},
         {607, "Z1", "STRING"}},
        xgroup("NoA", xfield("A1") + xcomp("C")) + xgroup("NoZ", xfield("Z1") + xcomp("C")), "",
        xmsg("M", xfield("Text")),
        "<component name='C'>" + xgroup("NoB", xfield("B1") + xfield("B2")) + "</component>"));
    return d;
}
TEST(SectionArm100, V_Control_UnderNoA_Cursor) {
    nested_expect(dict_v(),
                  head("ZZ") +
                      "601=1\x01"
                      "602=a\x01" +
                      two_instances(603, 604, 605) + kText,
                  601, 603, {.n = 2, .first = 604, .last = 605}, "(v) under NoA (control)");
}
TEST(SectionArm100, V_Control_UnderNoZ_Cursor) {
    nested_expect(dict_v(),
                  head("ZZ") +
                      "606=1\x01"
                      "607=b\x01" +
                      two_instances(603, 604, 605) + kText,
                  606, 603, {.n = 2, .first = 604, .last = 605}, "(v) under NoZ (control)");
}

// ════════════════════════════════════════════════════════════════════════════════
// Section arm, Orchestra
// ════════════════════════════════════════════════════════════════════════════════

std::vector<xf> const kOrchFields{{601, "NoA", "NumInGroup"}, {602, "A1", "String"},
                                  {603, "NoB", "NumInGroup"}, {604, "B1", "String"},
                                  {605, "B2", "String"},      {606, "NoZ", "NumInGroup"},
                                  {607, "Z1", "String"},      {9120, "NoT", "NumInGroup"},
                                  {9121, "T1", "String"},     {9122, "T2", "String"}};

custom_dict const& orch_ii() {
    static custom_dict const d = load_orch(
        orch_dict(kOrchFields, "", ogroupref(2620), "",
                  ogroup(2620, 9120, ofield(9121) + ofield(9122)), omsg("M", ofield(58))));
    return d;
}
TEST(SectionArm100, IiO_Control_TrailerGroupInUndeclaredType_Cursor) {
    flat_expect(orch_ii(), head("ZZ") + kText + two_instances(9120, 9121, 9122), 9120,
                {.n = 2, .first = 9121, .last = 9122}, "(ii-O) (control)");
}
TEST(SectionArm100, Ii0O_TrailerGroupCountZeroInUndeclaredType_OkZero) {
    flat_expect(orch_ii(), head("ZZ") + kText + "9120=0\x01", 9120, {.rc = FIXPP_ERR_OK},
                "(ii-0-O) / FR-002");
}

custom_dict const& orch_iii() {
    static custom_dict const d =
        load_orch(orch_dict(kOrchFields, ogroupref(2601), "", "",
                            ogroup(2601, 601, ofield(602) + ogroupref(2603)) +
                                ogroup(2603, 603, ofield(604) + ofield(605)),
                            omsg("M", ofield(58))));
    return d;
}
TEST(SectionArm100, IiiO_Control_NestedInHeaderGroupUndeclaredType_Cursor) {
    nested_expect(orch_iii(),
                  head("ZZ") +
                      "601=1\x01"
                      "602=a\x01" +
                      two_instances(603, 604, 605) + kText,
                  601, 603, {.n = 2, .first = 604, .last = 605}, "(iii-O) (control)");
}

custom_dict const& orch_iv() {
    static custom_dict const d =
        load_orch(orch_dict(kOrchFields, "", "", ocomp(2000, ogroupref(2603)),
                            ogroup(2601, 601, ofield(602) + ocompref(2000)) +
                                ogroup(2603, 603, ofield(604) + ofield(605)),
                            omsg("M", ogroupref(2601) + ocompref(2000) + ofield(58))));
    return d;
}
TEST(SectionArm100, IvO_Control_ComponentGroupAtRootAfterNestedUse_Cursor) {
    auto const ref = orch_iv().dict.field_ref("M", 603);
    ASSERT_EQ(ref.group_no_tag, 601U)
        << "(iv-O) precondition: the surviving NoB FieldRef is the one inside NoA";
    flat_expect(orch_iv(), head("M") + two_instances(603, 604, 605) + kText, 603,
                {.n = 2, .first = 604, .last = 605}, "(iv-O) (control)");
}

custom_dict const& orch_v() {
    static custom_dict const d = load_orch(
        orch_dict(kOrchFields, ogroupref(2601) + ogroupref(2606), "", ocomp(2000, ogroupref(2603)),
                  ogroup(2601, 601, ofield(602) + ocompref(2000)) +
                      ogroup(2606, 606, ofield(607) + ocompref(2000)) +
                      ogroup(2603, 603, ofield(604) + ofield(605)),
                  omsg("M", ofield(58))));
    return d;
}
TEST(SectionArm100, VO_Control_UnderNoA_Cursor) {
    nested_expect(orch_v(),
                  head("ZZ") +
                      "601=1\x01"
                      "602=a\x01" +
                      two_instances(603, 604, 605) + kText,
                  601, 603, {.n = 2, .first = 604, .last = 605}, "(v-O) under NoA (control)");
}
TEST(SectionArm100, VO_Control_UnderNoZ_Cursor) {
    nested_expect(orch_v(),
                  head("ZZ") +
                      "606=1\x01"
                      "607=b\x01" +
                      two_instances(603, 604, 605) + kText,
                  606, 603, {.n = 2, .first = 604, .last = 605}, "(v-O) under NoZ (control)");
}

// ════════════════════════════════════════════════════════════════════════════════
// Divergent definitions (research D9; D2 "Per-occurrence records")
// ════════════════════════════════════════════════════════════════════════════════

std::vector<xf> const kDivFields{
    {601, "NoA", "NUMINGROUP"}, {602, "A1", "STRING"},  {603, "NoB", "NUMINGROUP"},
    {606, "NoZ", "NUMINGROUP"}, {607, "Z1", "STRING"},  {610, "X1", "STRING"},
    {611, "X2", "STRING"},      {9120, "Y1", "STRING"}, {9121, "Y2", "STRING"},
    {630, "NoT", "NUMINGROUP"}, {631, "T1", "STRING"},  {632, "T2", "STRING"},
    {633, "T3", "STRING"},      {634, "T4", "STRING"},  {640, "N1", "STRING"},
    {641, "N2", "STRING"},      {642, "R1", "STRING"},  {643, "R2", "STRING"}};
std::vector<xf> orch_types(std::vector<xf> v) {
    for (auto& f : v) {
        f.type = std::string_view{f.type} == "NUMINGROUP" ? "NumInGroup" : "String";
    }
    return v;
}

// (A): NoB nested under header groups NoA {X1, X2} and NoZ {Y1, Y2}.
custom_dict const& div_a_xml() {
    static custom_dict const d = load_xml(
        xml_dict(kDivFields,
                 xgroup("NoA", xfield("A1") + xgroup("NoB", xfield("X1") + xfield("X2"))) +
                     xgroup("NoZ", xfield("Z1") + xgroup("NoB", xfield("Y1") + xfield("Y2"))),
                 "", xmsg("M", xfield("Text"))));
    return d;
}
custom_dict const& div_a_orch() {
    static custom_dict const d =
        load_orch(orch_dict(orch_types(kDivFields), ogroupref(2601) + ogroupref(2606), "", "",
                            ogroup(2601, 601, ofield(602) + ogroupref(2701)) +
                                ogroup(2606, 606, ofield(607) + ogroupref(2702)) +
                                ogroup(2701, 603, ofield(610) + ofield(611)) +
                                ogroup(2702, 603, ofield(9120) + ofield(9121)),
                            omsg("M", ofield(58))));
    return d;
}
std::string div_a_frame_under(std::uint16_t parent, std::uint16_t parent_member,
                              std::uint16_t first, std::uint16_t last) {
    return head("ZZ") + std::to_string(parent) + "=1\x01" + std::to_string(parent_member) +
           "=a\x01" + two_instances(603, first, last) + kText;
}
// `control` false: the parent whose occurrence the bare store does not hold; true: the parent
// whose occurrence it holds (the control).
void run_div_a(custom_dict const& d, char const* loader, bool control) {
    std::string const L = loader;
    bool const noa_is_bare = bare_equals(d.tv, 603, 610, {610, 611});
    bool const noz_is_bare = bare_equals(d.tv, 603, 9120, {9120, 9121});
    ASSERT_TRUE(noa_is_bare != noz_is_bare)
        << L << " (A) precondition: the bare store holds exactly one of the two definitions";
    bool const read_noa = (noa_is_bare == control);
    std::uint16_t const parent = read_noa ? 601 : 606;
    std::uint16_t const parent_member = read_noa ? 602 : 607;
    std::uint16_t const first = read_noa ? 610 : 9120;
    auto const last = static_cast<std::uint16_t>(first + 1);
    if (control) {
        ASSERT_TRUE(bare_equals(d.tv, 603, first, {first, last}))
            << L << " (A) control precondition: the bare store holds the read occurrence";
    } else {
        ASSERT_TRUE(bare_differs_in_both(d.tv, 603, first, {first, last}))
            << L
            << " (A) precondition: the bare delimiter and members differ from the read "
               "occurrence";
    }
    nested_expect(d, div_a_frame_under(parent, parent_member, first, last), parent, 603,
                  {.n = 2, .first = first, .last = last},
                  L + (control ? " (A) control: under the parent the bare store holds"
                               : " (A) under the parent the bare store does not hold"));
}
TEST(Divergent100, A_Xml_NestedUnderTheNonBareParentAtItsOwnDefinition) {
    run_div_a(div_a_xml(), "XML", false);
}
TEST(Divergent100, A_Xml_Control_NestedUnderTheBareParent) { run_div_a(div_a_xml(), "XML", true); }
TEST(Divergent100, A_Orchestra_NestedUnderTheNonBareParentAtItsOwnDefinition) {
    run_div_a(div_a_orch(), "Orchestra", false);
}
TEST(Divergent100, A_Orchestra_Control_NestedUnderTheBareParent) {
    run_div_a(div_a_orch(), "Orchestra", true);
}

// (B): NoT(630) in a declared message's body {T1, T2} and in the trailer {T3, T4}; the trailer
// NoT read with the flat getter in an undeclared type.
custom_dict const& div_b_xml() {
    static custom_dict const d =
        load_xml(xml_dict(kDivFields, "", xgroup("NoT", xfield("T3") + xfield("T4")),
                          xmsg("M", xfield("Text") + xgroup("NoT", xfield("T1") + xfield("T2")))));
    return d;
}
custom_dict const& div_b_orch() {
    static custom_dict const d = load_orch(orch_dict(
        orch_types(kDivFields), "", ogroupref(2632), "",
        ogroup(2631, 630, ofield(631) + ofield(632)) + ogroup(2632, 630, ofield(633) + ofield(634)),
        omsg("M", ofield(58) + ogroupref(2631))));
    return d;
}
void run_div_b(custom_dict const& d, char const* loader) {
    std::string const L = loader;
    ASSERT_TRUE(bare_differs_in_both(d.tv, 630, 633, {633, 634}))
        << L << " (B) precondition: the bare store does not hold the trailer definition";
    flat_expect(d, head("ZZ") + kText + two_instances(630, 633, 634), 630,
                {.n = 2, .first = 633, .last = 634}, L + " (B) trailer NoT in an undeclared type");
}
TEST(Divergent100, B_Xml_TrailerGroupAtTheTrailerDefinition) { run_div_b(div_b_xml(), "XML"); }
TEST(Divergent100, B_Orchestra_TrailerGroupAtTheTrailerDefinition) {
    run_div_b(div_b_orch(), "Orchestra");
}

// (C): NoB(603) at the root of declared message M {642, 643} and inside body group NoA {640, 641};
// the surviving NoB FieldRef is the nested one, so M has no group_ctx_ key at the root. Message
// L, which holds only NoA, makes the nested definition the one the loader reaches first, so the
// bare store holds it; the precondition asserts that rather than relying on the order.
custom_dict const& div_c_xml() {
    static custom_dict const d = load_xml(xml_dict(
        kDivFields, "", "",
        xmsg("L", xgroup("NoA", xfield("A1") + xgroup("NoB", xfield("N1") + xfield("N2")))) +
            xmsg("M", xgroup("NoA", xfield("A1") + xgroup("NoB", xfield("N1") + xfield("N2"))) +
                          xgroup("NoB", xfield("R1") + xfield("R2")) + xfield("Text"))));
    return d;
}
custom_dict const& div_c_orch() {
    static custom_dict const d = load_orch(orch_dict(
        orch_types(kDivFields), "", "", "",
        ogroup(2601, 601, ofield(602) + ogroupref(2741)) +
            ogroup(2741, 603, ofield(640) + ofield(641)) +
            ogroup(2742, 603, ofield(642) + ofield(643)),
        omsg("L", ogroupref(2601)) + omsg("M", ogroupref(2601) + ogroupref(2742) + ofield(58))));
    return d;
}
void run_div_c(custom_dict const& d, char const* loader) {
    std::string const L = loader;
    ASSERT_FALSE(d.tv.group_first_field_exact("M", {}, 603).has_value())
        << L << " (C) precondition: M has no group_ctx_ key for NoB at the root";
    ASSERT_TRUE(bare_differs_in_both(d.tv, 603, 642, {642, 643}))
        << L << " (C) precondition: the bare store does not hold the root definition";
    flat_expect(d, head("M") + two_instances(603, 642, 643) + kText, 603,
                {.n = 2, .first = 642, .last = 643}, L + " (C) root NoB at the root definition");
}
void run_div_c_control(custom_dict const& d, char const* loader) {
    std::string const L = loader;
    nested_expect(d,
                  head("M") +
                      "601=1\x01"
                      "602=a\x01" +
                      two_instances(603, 640, 641) + kText,
                  601, 603, {.n = 2, .first = 640, .last = 641},
                  L + " (C) control: NoB under NoA (a group_ctx_ hit)");
}
TEST(Divergent100, C_Xml_RootGroupAtTheRootDefinition) { run_div_c(div_c_xml(), "XML"); }
TEST(Divergent100, C_Xml_Control_NestedUnderNoA) { run_div_c_control(div_c_xml(), "XML"); }
TEST(Divergent100, C_Orchestra_RootGroupAtTheRootDefinition) {
    run_div_c(div_c_orch(), "Orchestra");
}
TEST(Divergent100, C_Orchestra_Control_NestedUnderNoA) {
    run_div_c_control(div_c_orch(), "Orchestra");
}

}  // namespace
