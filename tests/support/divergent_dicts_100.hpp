#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/divergent_dicts_100.hpp — TEST-ONLY dictionary text for
// 100-wire-group-read-integrity's custom-dictionary cells (tasks T011, T028; research D9).
//
//   - XML and Orchestra dictionary-text builders (`xml_dict`, `orch_dict` and their element
//     helpers), each dictionary declaring BeginString, BodyLength, CheckSum, MsgType and Text
//     besides the fields its caller lists.
//   - `kDivFields` and the divergent-definition dictionaries (A), (B) and (C) of research D9,
//     as text, per loader. Each including file states its own expected values.

#include <string>
#include <string_view>
#include <vector>

namespace fixpp::test100::div {

// A FIX 4.4 XML dictionary: `fields` (number, name, type), `header`, `trailer`, `messages`,
// `components` element bodies.
struct xf {
    int number;
    char const* name;
    char const* type;
};
inline std::string xml_dict(std::vector<xf> const& fields, std::string const& header,
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
inline std::string xfield(char const* name) {
    return std::string{"<field name='"} + name + "' required='N'/>";
}
inline std::string xgroup(char const* name, std::string const& body) {
    return std::string{"<group name='"} + name + "' required='N'>" + body + "</group>";
}
inline std::string xcomp(char const* name) {
    return std::string{"<component name='"} + name + "' required='N'/>";
}
inline std::string xmsg(char const* type, std::string const& body) {
    return std::string{"<message name='M"} + type + "' msgtype='" + type + "' msgcat='app'>" +
           body + "</message>";
}

// An Orchestra repository: `fields` (id, name, type), then the components (1024 and 1025 are
// the header and trailer), groups and messages element bodies.
inline std::string orch_dict(std::vector<xf> const& fields, std::string const& header_refs,
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
inline std::string ofield(int id) { return "<fixr:fieldRef id='" + std::to_string(id) + "'/>"; }
inline std::string ogroupref(int id) { return "<fixr:groupRef id='" + std::to_string(id) + "'/>"; }
inline std::string ocompref(int id) {
    return "<fixr:componentRef id='" + std::to_string(id) + "'/>";
}
inline std::string ogroup(int id, int num_in_group, std::string const& body) {
    return "<fixr:group id='" + std::to_string(id) + "' name='G" + std::to_string(id) +
           "'><fixr:numInGroup id='" + std::to_string(num_in_group) + "'/>" + body +
           "</fixr:group>";
}
inline std::string ocomp(int id, std::string const& body) {
    return "<fixr:component id='" + std::to_string(id) + "' name='C" + std::to_string(id) + "'>" +
           body + "</fixr:component>";
}
inline std::string omsg(char const* type, std::string const& body) {
    return std::string{"<fixr:message name='M"} + type + "' msgType='" + type +
           "'><fixr:structure>" + ocompref(1024) + body + ocompref(1025) +
           "</fixr:structure></fixr:message>";
}

inline std::vector<xf> const kDivFields{
    {601, "NoA", "NUMINGROUP"}, {602, "A1", "STRING"},  {603, "NoB", "NUMINGROUP"},
    {606, "NoZ", "NUMINGROUP"}, {607, "Z1", "STRING"},  {610, "X1", "STRING"},
    {611, "X2", "STRING"},      {9120, "Y1", "STRING"}, {9121, "Y2", "STRING"},
    {630, "NoT", "NUMINGROUP"}, {631, "T1", "STRING"},  {632, "T2", "STRING"},
    {633, "T3", "STRING"},      {634, "T4", "STRING"},  {640, "N1", "STRING"},
    {641, "N2", "STRING"},      {642, "R1", "STRING"},  {643, "R2", "STRING"}};
inline std::vector<xf> orch_types(std::vector<xf> v) {
    for (auto& f : v) {
        f.type = std::string_view{f.type} == "NUMINGROUP" ? "NumInGroup" : "String";
    }
    return v;
}

// (A): NoB(603) nested under header groups NoA {X1, X2} and NoZ {Y1, Y2}.
inline std::string div_a_xml() {
    return xml_dict(kDivFields,
                    xgroup("NoA", xfield("A1") + xgroup("NoB", xfield("X1") + xfield("X2"))) +
                        xgroup("NoZ", xfield("Z1") + xgroup("NoB", xfield("Y1") + xfield("Y2"))),
                    "", xmsg("M", xfield("Text")));
}
inline std::string div_a_orch() {
    return orch_dict(orch_types(kDivFields), ogroupref(2601) + ogroupref(2606), "", "",
                     ogroup(2601, 601, ofield(602) + ogroupref(2701)) +
                         ogroup(2606, 606, ofield(607) + ogroupref(2702)) +
                         ogroup(2701, 603, ofield(610) + ofield(611)) +
                         ogroup(2702, 603, ofield(9120) + ofield(9121)),
                     omsg("M", ofield(58)));
}

// (B): NoT(630) in a declared message's body {T1, T2} and in the trailer {T3, T4}.
inline std::string div_b_xml() {
    return xml_dict(kDivFields, "", xgroup("NoT", xfield("T3") + xfield("T4")),
                    xmsg("M", xfield("Text") + xgroup("NoT", xfield("T1") + xfield("T2"))));
}
inline std::string div_b_orch() {
    return orch_dict(
        orch_types(kDivFields), "", ogroupref(2632), "",
        ogroup(2631, 630, ofield(631) + ofield(632)) + ogroup(2632, 630, ofield(633) + ofield(634)),
        omsg("M", ofield(58) + ogroupref(2631)));
}

// (C): NoB(603) at the root of declared message M {R1, R2} and inside body group NoA {N1, N2};
// message L holds only NoA.
inline std::string div_c_xml() {
    return xml_dict(
        kDivFields, "", "",
        xmsg("L", xgroup("NoA", xfield("A1") + xgroup("NoB", xfield("N1") + xfield("N2")))) +
            xmsg("M", xgroup("NoA", xfield("A1") + xgroup("NoB", xfield("N1") + xfield("N2"))) +
                          xgroup("NoB", xfield("R1") + xfield("R2")) + xfield("Text")));
}
inline std::string div_c_orch() {
    return orch_dict(
        orch_types(kDivFields), "", "", "",
        ogroup(2601, 601, ofield(602) + ogroupref(2741)) +
            ogroup(2741, 603, ofield(640) + ofield(641)) +
            ogroup(2742, 603, ofield(642) + ofield(643)),
        omsg("L", ogroupref(2601)) + omsg("M", ogroupref(2601) + ogroupref(2742) + ofield(58)));
}

}  // namespace fixpp::test100::div
