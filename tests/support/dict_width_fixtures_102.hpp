#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/dict_width_fixtures_102.hpp — TEST-ONLY dictionary text for
// 102-dict-width-multichar-type (tasks T008; research D4 "component identity", "the alias-pair
// rule", rows (a)–(f), "Overlay crossing").
//
// Each generator returns a document's text and the values the cells expect, computed here from
// the generator's own layout, in `std::uint32_t` or wider, and never read back from a
// `Dictionary`. Standard-library dependencies only, so any test binary can include it
// (`capi_pure_tests` among them). The overlay caps and the expansion budget live in
// `src/dictionary/overlay_merge.hpp`; a generator that depends on one takes it as a parameter,
// and the caller passes the constant.
//
// The wrap point. A 16-bit holder cannot hold `kWrap`; a value `v` narrowed to 16 bits reads as
// `v - kWrap` when `kWrap <= v < 2 * kWrap`. An index at `kWrap - 1` has the one-based value
// `kWrap`, the first that wraps to 0.
//
// The alias-pair rule (research D4). For each surrogate-keyed container a row reaches, the
// fixture declares a high id H above 65535 and its alias H - kWrap, both referenced, with
// distinct members. Each alias is not 0, not 1024 or 1025 (the header and trailer section ids),
// and not shared with an unrelated definition of the same kind, so a pair can fail only through
// a 16-bit narrowing. `precondition_failure()` re-reads the text for each declared pair.
//
// Lifetime. Each `*_fixture()` builds its text once per test binary (a function-local static)
// and returns a reference to it. Loading is the caller's; a cell that loads its own copy says
// so.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fixpp::test102::width {

inline constexpr std::uint32_t kWrap = 65536;

// ── Element builders (QuickFIX XML) ──────────────────────────────────────────────────────────

inline std::string x_field_def(std::uint32_t number, std::string_view name,
                               std::string_view type) {
    return "<field number='" + std::to_string(number) + "' name='" + std::string{name} +
           "' type='" + std::string{type} + "'/>\n";
}
inline std::string x_fref(std::string_view name) {
    return "<field name='" + std::string{name} + "' required='N'/>";
}
inline std::string x_cref(std::string_view name) {
    return "<component name='" + std::string{name} + "' required='N'/>";
}
inline std::string x_group(std::string_view name, std::string_view body) {
    return "<group name='" + std::string{name} + "' required='N'>" + std::string{body} +
           "</group>";
}
inline std::string x_comp(std::string_view name, std::string_view body) {
    if (body.empty()) {
        return "<component name='" + std::string{name} + "'/>\n";
    }
    return "<component name='" + std::string{name} + "'>" + std::string{body} + "</component>\n";
}
inline constexpr std::string_view kXmlOpen = "<fix type='FIX' major='4' minor='4' servicepack='0'>\n";

// ── Element builders (Orchestra) ─────────────────────────────────────────────────────────────

inline constexpr std::string_view kOrchOpen =
    "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' "
    "name='FIX' version='FIX.Latest_EP303'>\n";
inline constexpr std::string_view kOrchClose = "</fixr:repository>\n";

inline std::string o_field_def(std::uint32_t id, std::string_view name, std::string_view type) {
    return "<fixr:field id='" + std::to_string(id) + "' name='" + std::string{name} +
           "' type='" + std::string{type} + "'/>\n";
}
inline std::string o_fref(std::uint32_t id, std::string_view presence = {}) {
    return "<fixr:fieldRef id='" + std::to_string(id) + "'" +
           (presence.empty() ? std::string{} : " presence='" + std::string{presence} + "'") + "/>";
}
inline std::string o_cref(std::uint32_t id, std::string_view presence = {}) {
    return "<fixr:componentRef id='" + std::to_string(id) + "'" +
           (presence.empty() ? std::string{} : " presence='" + std::string{presence} + "'") + "/>";
}
inline std::string o_gref(std::uint32_t id, std::string_view presence = {}) {
    return "<fixr:groupRef id='" + std::to_string(id) + "'" +
           (presence.empty() ? std::string{} : " presence='" + std::string{presence} + "'") + "/>";
}
inline std::string o_comp(std::uint32_t id, std::string_view name, std::string_view body) {
    if (body.empty()) {
        return "<fixr:component id='" + std::to_string(id) + "' name='" + std::string{name} +
               "'/>\n";
    }
    return "<fixr:component id='" + std::to_string(id) + "' name='" + std::string{name} + "'>" +
           std::string{body} + "</fixr:component>\n";
}
inline std::string o_group(std::uint32_t id, std::string_view name, std::uint32_t num_in_group,
                           std::string_view body) {
    return "<fixr:group id='" + std::to_string(id) + "' name='" + std::string{name} +
           "'><fixr:numInGroup id='" + std::to_string(num_in_group) + "'/>" + std::string{body} +
           "</fixr:group>\n";
}
inline std::string o_msg(std::uint32_t id, std::string_view name, std::string_view msg_type,
                         std::string_view body) {
    return "<fixr:message id='" + std::to_string(id) + "' name='" + std::string{name} +
           "' msgType='" + std::string{msg_type} +
           "' category='SingleGeneralOrderHandling'><fixr:structure>" + std::string{body} +
           "</fixr:structure></fixr:message>\n";
}

// ── Text checks the preconditions use ────────────────────────────────────────────────────────

inline std::size_t count_of(std::string_view text, std::string_view needle) {
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string_view::npos;
         at = text.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

// The text declares a definition `<element id='id'` and references it with `<ref id='id'`.
inline bool declared_and_referenced(std::string_view text, std::string_view element,
                                    std::string_view ref, std::uint32_t id) {
    std::string const sid = std::to_string(id);
    return count_of(text, "<" + std::string{element} + " id='" + sid + "'") == 1 &&
           count_of(text, "<" + std::string{ref} + " id='" + sid + "'") >= 1;
}

// One expected FieldRef of a component or a message: its tag and its `component_index`.
struct member {
    std::uint16_t tag = 0;
    std::uint32_t component_index = 0;
    friend bool operator==(member const&, member const&) = default;
};

// One expected component, in declaration order. `id` is the zero-based declaration index;
// `parent` is 0 (top level) or the parent's id + 1.
struct expected_component {
    std::string name;
    std::uint32_t id = 0;
    std::uint32_t parent = 0;
    std::vector<member> members;  // component_fields order
};

// An alias pair, for a precondition: the element kind, the reference kind and the two ids.
struct alias_pair {
    std::string_view element;
    std::string_view ref;
    std::uint32_t high = 0;
};

inline std::string pairs_failure(std::string_view text, std::vector<alias_pair> const& pairs) {
    for (auto const& p : pairs) {
        std::uint32_t const alias = p.high - kWrap;
        if (p.high < kWrap || alias == 0 || alias == 1024 || alias == 1025) {
            return "alias pair " + std::to_string(p.high) + " breaks the alias rule";
        }
        if (!declared_and_referenced(text, p.element, p.ref, p.high) ||
            !declared_and_referenced(text, p.element, p.ref, alias)) {
            return "alias pair " + std::to_string(p.high) + "/" + std::to_string(alias) +
                   " is not declared once and referenced";
        }
    }
    return {};
}

// ── (a) A QuickFIX XML base of more than kWrap components ────────────────────────────────────
//
// Layout by declaration index (W = kWrap):
//   W-1  ParentAt65535  {TP1, component Kid1}   a parent at index 65,535
//   W    Kid1           {TK1}                    its child
//   W+1  Chigh          {FH}                     referenced from message D
//   W+2  ParentHigh     {TP2, component Kid2}   a parent past the wrap, referenced from D
//   W+3  Kid2           {TK2}
//   W+4  HeaderX        {TX, group NoX {MX}}     referenced from <header>
//   4    XAlias         {TA, group NoA {MA}}     the component at HeaderX's index - W
//   every other index   an empty filler F<index>
// A filler sits at Chigh's index - W, so a wrapped lookup of Chigh reads no members.

struct xml_identity_fixture {
    std::string text;
    std::vector<expected_component> components;  // declaration order
    std::string msg_type = "D";
    std::vector<member> message_members;  // members of D that come from a component
    std::uint32_t x_index = 0;
    std::uint16_t t_x = 0, n_x = 0, m_x = 0;
    std::uint16_t t_alias = 0, n_alias = 0, m_alias = 0;

    [[nodiscard]] std::string precondition_failure() const {
        // Definitions are the lines that start with `<component name=`; references are inline.
        std::size_t const defs = count_of(text, "\n<component name='");
        if (defs != components.size() || defs <= kWrap) {
            return "expected more than kWrap component definitions, got " + std::to_string(defs);
        }
        if (x_index < kWrap || components[x_index - kWrap].name != "XAlias") {
            return "HeaderX is not past the wrap, or XAlias is not at its index - kWrap";
        }
        return {};
    }
};

inline xml_identity_fixture make_xml_identity_fixture() {
    constexpr std::uint32_t W = kWrap;
    constexpr std::uint32_t N = W + 8;
    constexpr std::uint16_t kFH = 9001, kTP1 = 9002, kTK1 = 9003, kTP2 = 9004, kTK2 = 9005,
                            kTX = 9006, kNX = 9007, kMX = 9008, kTA = 9009, kNA = 9010,
                            kMA = 9011;
    xml_identity_fixture f;
    f.x_index = W + 4;
    f.t_x = kTX;
    f.n_x = kNX;
    f.m_x = kMX;
    f.t_alias = kTA;
    f.n_alias = kNA;
    f.m_alias = kMA;

    std::vector<std::string> names(N);
    std::vector<std::string> bodies(N);
    f.components.resize(N);
    for (std::uint32_t i = 0; i < N; ++i) {
        names[i] = "F" + std::to_string(i);
    }
    auto const set = [&](std::uint32_t i, std::string name, std::string body) {
        names[i] = std::move(name);
        bodies[i] = std::move(body);
    };
    set(W - 1, "ParentAt65535", x_fref("TP1") + x_cref("Kid1"));
    set(W, "Kid1", x_fref("TK1"));
    set(W + 1, "Chigh", x_fref("FH"));
    set(W + 2, "ParentHigh", x_fref("TP2") + x_cref("Kid2"));
    set(W + 3, "Kid2", x_fref("TK2"));
    set(W + 4, "HeaderX", x_fref("TX") + x_group("NoX", x_fref("MX")));
    set(4, "XAlias", x_fref("TA") + x_group("NoA", x_fref("MA")));

    for (std::uint32_t i = 0; i < N; ++i) {
        f.components[i].name = names[i];
        f.components[i].id = i;
    }
    auto const own = [](std::uint32_t i) { return i + 1; };
    f.components[W - 1].members = {{kTP1, own(W - 1)}, {kTK1, own(W)}};
    f.components[W].members = {{kTK1, own(W)}};
    f.components[W].parent = own(W - 1);
    f.components[W + 1].members = {{kFH, own(W + 1)}};
    f.components[W + 2].members = {{kTP2, own(W + 2)}, {kTK2, own(W + 3)}};
    f.components[W + 3].members = {{kTK2, own(W + 3)}};
    f.components[W + 3].parent = own(W + 2);
    f.components[W + 4].members = {{kTX, own(W + 4)}, {kNX, own(W + 4)}, {kMX, own(W + 4)}};
    f.components[4].members = {{kTA, own(4)}, {kNA, own(4)}, {kMA, own(4)}};

    f.message_members = {{kFH, own(W + 1)}, {kTP2, own(W + 2)}, {kTK2, own(W + 3)},
                         {kTX, own(W + 4)}, {kNX, own(W + 4)}};

    std::string& x = f.text;
    x.reserve(std::size_t{N} * 32U);
    x += kXmlOpen;
    x += "<header>" + x_fref("BeginString") + x_fref("BodyLength") + x_fref("MsgType") +
         x_cref("HeaderX") + "</header>\n";
    x += "<trailer>" + x_fref("CheckSum") + "</trailer>\n";
    x += "<messages><message name='NewOrderSingle' msgtype='D' msgcat='app'>" + x_cref("Chigh") +
         x_cref("ParentHigh") + "</message></messages>\n";
    x += "<components>\n";
    for (std::uint32_t i = 0; i < N; ++i) {
        x += x_comp(names[i], bodies[i]);
    }
    x += "</components>\n<fields>\n";
    x += x_field_def(8, "BeginString", "STRING") + x_field_def(9, "BodyLength", "LENGTH") +
         x_field_def(10, "CheckSum", "STRING") + x_field_def(35, "MsgType", "STRING");
    x += x_field_def(kFH, "FH", "STRING") + x_field_def(kTP1, "TP1", "STRING") +
         x_field_def(kTK1, "TK1", "STRING") + x_field_def(kTP2, "TP2", "STRING") +
         x_field_def(kTK2, "TK2", "STRING") + x_field_def(kTX, "TX", "STRING") +
         x_field_def(kNX, "NoX", "NUMINGROUP") + x_field_def(kMX, "MX", "STRING") +
         x_field_def(kTA, "TA", "STRING") + x_field_def(kNA, "NoA", "NUMINGROUP") +
         x_field_def(kMA, "MA", "STRING");
    x += "</fields>\n</fix>\n";
    return f;
}

inline xml_identity_fixture const& xml_identity_fixture_text() {
    static xml_identity_fixture const f = make_xml_identity_fixture();
    return f;
}

// ── (b) Its Orchestra twin ───────────────────────────────────────────────────────────────────
//
// Component ids are `index + kIdBase` except the header (id 1024), so every id from index
// `kWrap - kIdBase` on passes 65535. Layout by declaration index (W = kWrap):
//   0    Ka    id 2000   {TKa}                       alias child
//   1    Pa    id 2001   {TPa, componentRef Ka}      alias parent
//   2    Clow  id 2002   {FL}                        referenced from D
//   3    Xa    id 2003   {TXa, groupRef Gxa}         referenced from D
//   W    Kh    id 67536  {TKh}                       Ka's high twin
//   W+1  Ph    id 67537  {TPh, componentRef Kh}      Pa's high twin, referenced from D
//   W+2  Chigh id 67538  {FH}                        referenced from D
//   W+3  X     id 67539  {TX, groupRef Gx}           referenced from the header
//   W+4  StandardHeader id 1024 {8, 9, 35, componentRef X}, referenced from D
//   every other index    an empty filler C<index>
// Groups: Gx id 70000 {NX; MX}, Gxa id 4464 {NXa; MXa}. Alias pairs: (Kh, Ka), (Ph, Pa),
// (Chigh, Clow), (X, Xa) for `component_index_by_xml_id_`; (Gx, Gxa) for `group_by_xml_id_`.

struct orch_identity_fixture {
    std::string text;
    std::vector<expected_component> components;
    std::string msg_type = "D";
    std::vector<member> message_members;
    std::uint32_t header_index = 0;
    std::uint32_t x_index = 0;
    std::uint16_t t_x = 0, n_x = 0, m_x = 0;
    std::vector<alias_pair> pairs;

    [[nodiscard]] std::string precondition_failure() const {
        std::size_t const defs = count_of(text, "\n<fixr:component id='");
        if (defs != components.size() || defs <= kWrap) {
            return "expected more than kWrap component definitions, got " + std::to_string(defs);
        }
        if (header_index < kWrap || x_index < kWrap) {
            return "the header or X is not past the wrap";
        }
        return pairs_failure(text, pairs);
    }
};

inline orch_identity_fixture make_orch_identity_fixture() {
    constexpr std::uint32_t W = kWrap;
    constexpr std::uint32_t N = W + 8;
    constexpr std::uint32_t kIdBase = 2000;
    constexpr std::uint16_t kTKa = 9101, kTPa = 9102, kFL = 9103, kTXa = 9104, kNXa = 9105,
                            kMXa = 9106, kTKh = 9111, kTPh = 9112, kFH = 9113, kTX = 9114,
                            kNX = 9115, kMX = 9116;
    constexpr std::uint32_t kGx = 70000;
    constexpr std::uint32_t kGxa = kGx - W;
    orch_identity_fixture f;
    f.header_index = W + 4;
    f.x_index = W + 3;
    f.t_x = kTX;
    f.n_x = kNX;
    f.m_x = kMX;

    std::vector<std::uint32_t> ids(N);
    std::vector<std::string> names(N);
    std::vector<std::string> bodies(N);
    for (std::uint32_t i = 0; i < N; ++i) {
        ids[i] = i + kIdBase;
        names[i] = "C" + std::to_string(i);
    }
    ids[W + 4] = 1024;
    names[0] = "Ka";
    bodies[0] = o_fref(kTKa);
    names[1] = "Pa";
    bodies[1] = o_fref(kTPa) + o_cref(ids[0]);
    names[2] = "Clow";
    bodies[2] = o_fref(kFL);
    names[3] = "Xa";
    bodies[3] = o_fref(kTXa) + o_gref(kGxa);
    names[W] = "Kh";
    bodies[W] = o_fref(kTKh);
    names[W + 1] = "Ph";
    bodies[W + 1] = o_fref(kTPh) + o_cref(ids[W]);
    names[W + 2] = "Chigh";
    bodies[W + 2] = o_fref(kFH);
    names[W + 3] = "X";
    bodies[W + 3] = o_fref(kTX) + o_gref(kGx);
    names[W + 4] = "StandardHeader";
    bodies[W + 4] =
        o_fref(8, "required") + o_fref(9, "required") + o_fref(35, "required") + o_cref(ids[W + 3]);

    f.components.resize(N);
    for (std::uint32_t i = 0; i < N; ++i) {
        f.components[i].name = names[i];
        f.components[i].id = i;
    }
    auto const own = [](std::uint32_t i) { return i + 1; };
    f.components[0].members = {{kTKa, own(0)}};
    f.components[0].parent = own(1);
    f.components[1].members = {{kTPa, own(1)}, {kTKa, own(0)}};
    f.components[2].members = {{kFL, own(2)}};
    f.components[3].members = {{kTXa, own(3)}, {kNXa, own(3)}, {kMXa, own(3)}};
    f.components[W].members = {{kTKh, own(W)}};
    f.components[W].parent = own(W + 1);
    f.components[W + 1].members = {{kTPh, own(W + 1)}, {kTKh, own(W)}};
    f.components[W + 2].members = {{kFH, own(W + 2)}};
    f.components[W + 3].members = {{kTX, own(W + 3)}, {kNX, own(W + 3)}, {kMX, own(W + 3)}};
    f.components[W + 3].parent = own(W + 4);
    f.components[W + 4].members = {{8, own(W + 4)},       {9, own(W + 4)},
                                   {35, own(W + 4)},      {kTX, own(W + 3)},
                                   {kNX, own(W + 3)},     {kMX, own(W + 3)}};

    f.message_members = {{kTKa, own(0)},     {kTPa, own(1)},     {kFL, own(2)},
                         {kTXa, own(3)},     {kNXa, own(3)},     {kTKh, own(W)},
                         {kTPh, own(W + 1)}, {kFH, own(W + 2)},  {kTX, own(W + 3)},
                         {kNX, own(W + 3)}};
    f.pairs = {{"fixr:component", "fixr:componentRef", ids[W]},
               {"fixr:component", "fixr:componentRef", ids[W + 1]},
               {"fixr:component", "fixr:componentRef", ids[W + 2]},
               {"fixr:component", "fixr:componentRef", ids[W + 3]},
               {"fixr:group", "fixr:groupRef", kGx}};

    std::string& x = f.text;
    x.reserve(std::size_t{N} * 48U);
    x += kOrchOpen;
    x += "<fixr:fields>\n";
    x += o_field_def(8, "BeginString", "String") + o_field_def(9, "BodyLength", "Length") +
         o_field_def(10, "CheckSum", "String") + o_field_def(35, "MsgType", "String");
    struct field_row {
        std::uint16_t tag;
        char const* name;
        char const* type;
    };
    for (field_row const& r : {field_row{kTKa, "TKa", "String"}, field_row{kTPa, "TPa", "String"},
                               field_row{kFL, "FL", "String"}, field_row{kTXa, "TXa", "String"},
                               field_row{kNXa, "NoXa", "NumInGroup"},
                               field_row{kMXa, "MXa", "String"}, field_row{kTKh, "TKh", "String"},
                               field_row{kTPh, "TPh", "String"}, field_row{kFH, "FH", "String"},
                               field_row{kTX, "TX", "String"}, field_row{kNX, "NoX", "NumInGroup"},
                               field_row{kMX, "MX", "String"}}) {
        x += o_field_def(r.tag, r.name, r.type);
    }
    x += "</fixr:fields>\n<fixr:components>\n";
    for (std::uint32_t i = 0; i < N; ++i) {
        x += o_comp(ids[i], names[i], bodies[i]);
    }
    x += "</fixr:components>\n<fixr:groups>\n";
    x += o_group(kGx, "Gx", kNX, o_fref(kMX)) + o_group(kGxa, "Gxa", kNXa, o_fref(kMXa));
    x += "</fixr:groups>\n<fixr:messages>\n";
    x += o_msg(1, "NewOrderSingle", "D",
               o_cref(1024, "required") + o_cref(ids[W + 1]) + o_cref(ids[1]) +
                   o_cref(ids[W + 2]) + o_cref(ids[2]) + o_cref(ids[3]));
    x += "</fixr:messages>\n";
    x += kOrchClose;
    return f;
}

inline orch_identity_fixture const& orch_identity_fixture_text() {
    static orch_identity_fixture const f = make_orch_identity_fixture();
    return f;
}

// ── (c) The Orchestra header fixture of rows (b), (b2) and (c) ───────────────────────────────
//
// The header component (id 1024, named StandardHeader) is reached by no message structure. It
// references HdrHigh (id 70001: T, groupRef HgHigh {N; M}) and HdrAlias (id 4465: T',
// groupRef HgAlias {N'; M'}). Groups HgHigh id 70002, HgAlias id 4466. Component id
// 1024 + kWrap holds T_ord and component id 1025 + kWrap holds groupRef Gt {N_t; M_t}; each is
// referenced from message F only. Message E references IrHigh (id 70010: 9211, groupRef JgHigh
// id 70011 {9212; 9213, 9217}) and IrAlias (id 4474: 9214, groupRef JgAlias id 4475 {9215;
// 9216}): the code generator's IR row.

struct orch_header_fixture {
    std::string text;
    std::uint16_t t = 9201, n = 9202, m = 9203;
    std::uint16_t t2 = 9204, n2 = 9205, m2 = 9206;
    std::uint16_t t_ord = 9207;
    std::uint16_t n_t = 9208, m_t = 9209;
    // The IR row.
    std::string ir_msg_type = "E";
    std::uint16_t ir_t_high = 9211, ir_n_high = 9212, ir_m_high = 9213, ir_m_high2 = 9217;
    std::uint16_t ir_t_alias = 9214, ir_n_alias = 9215, ir_m_alias = 9216;
    std::vector<alias_pair> pairs;

    [[nodiscard]] std::string precondition_failure() const {
        if (count_of(text, "<fixr:componentRef id='1024'") != 0) {
            return "a structure references the StandardHeader component";
        }
        if (count_of(text, "<fixr:component id='1024' name='StandardHeader'") != 1) {
            return "the header component is not id 1024 named StandardHeader";
        }
        for (std::uint32_t const id : {1024U + kWrap, 1025U + kWrap}) {
            if (!declared_and_referenced(text, "fixr:component", "fixr:componentRef", id)) {
                return "component " + std::to_string(id) + " is not declared and referenced";
            }
        }
        return pairs_failure(text, pairs);
    }
};

inline orch_header_fixture make_orch_header_fixture() {
    orch_header_fixture f;
    constexpr std::uint32_t kHc = 70001, kHg = 70002, kHca = kHc - kWrap, kHga = kHg - kWrap;
    constexpr std::uint32_t kGt = 70003;
    constexpr std::uint32_t kIc = 70010, kJg = 70011, kIca = kIc - kWrap, kJga = kJg - kWrap;
    f.pairs = {{"fixr:component", "fixr:componentRef", kHc},
               {"fixr:group", "fixr:groupRef", kHg},
               {"fixr:component", "fixr:componentRef", kIc},
               {"fixr:group", "fixr:groupRef", kJg}};
    std::string& x = f.text;
    x += kOrchOpen;
    x += "<fixr:fields>\n";
    x += o_field_def(8, "BeginString", "String") + o_field_def(9, "BodyLength", "Length") +
         o_field_def(10, "CheckSum", "String") + o_field_def(35, "MsgType", "String");
    x += o_field_def(f.t, "HdrT", "String") + o_field_def(f.n, "NoHdrN", "NumInGroup") +
         o_field_def(f.m, "HdrM", "String") + o_field_def(f.t2, "HdrT2", "String") +
         o_field_def(f.n2, "NoHdrN2", "NumInGroup") + o_field_def(f.m2, "HdrM2", "String") +
         o_field_def(f.t_ord, "OrdT", "String") + o_field_def(f.n_t, "NoOrdN", "NumInGroup") +
         o_field_def(f.m_t, "OrdM", "String") + o_field_def(f.ir_t_high, "IrT", "String") +
         o_field_def(f.ir_n_high, "NoIrN", "NumInGroup") +
         o_field_def(f.ir_m_high, "IrM", "String") + o_field_def(f.ir_m_high2, "IrM2", "String") +
         o_field_def(f.ir_t_alias, "IrTa", "String") +
         o_field_def(f.ir_n_alias, "NoIrNa", "NumInGroup") +
         o_field_def(f.ir_m_alias, "IrMa", "String");
    x += "</fixr:fields>\n<fixr:components>\n";
    x += o_comp(1024, "StandardHeader",
                o_fref(8, "required") + o_fref(9, "required") + o_fref(35, "required") +
                    o_cref(kHc) + o_cref(kHca));
    x += o_comp(kHc, "HdrHigh", o_fref(f.t) + o_gref(kHg));
    x += o_comp(kHca, "HdrAlias", o_fref(f.t2) + o_gref(kHga));
    x += o_comp(1024U + kWrap, "OrdinaryAt1024", o_fref(f.t_ord));
    x += o_comp(1025U + kWrap, "OrdinaryAt1025", o_gref(kGt));
    x += o_comp(kIc, "IrHigh", o_fref(f.ir_t_high) + o_gref(kJg));
    x += o_comp(kIca, "IrAlias", o_fref(f.ir_t_alias) + o_gref(kJga));
    x += "</fixr:components>\n<fixr:groups>\n";
    x += o_group(kHg, "HgHigh", f.n, o_fref(f.m)) + o_group(kHga, "HgAlias", f.n2, o_fref(f.m2)) +
         o_group(kGt, "Gt", f.n_t, o_fref(f.m_t)) +
         o_group(kJg, "JgHigh", f.ir_n_high, o_fref(f.ir_m_high) + o_fref(f.ir_m_high2)) +
         o_group(kJga, "JgAlias", f.ir_n_alias, o_fref(f.ir_m_alias));
    x += "</fixr:groups>\n<fixr:messages>\n";
    x += o_msg(1, "IrMessage", "E", o_fref(35, "required") + o_cref(kIc) + o_cref(kIca));
    x += o_msg(2, "OrdMessage", "F",
               o_fref(35, "required") + o_cref(1024U + kWrap) + o_cref(1025U + kWrap));
    x += "</fixr:messages>\n";
    x += kOrchClose;
    return f;
}

inline orch_header_fixture const& orch_header_fixture_text() {
    static orch_header_fixture const f = make_orch_header_fixture();
    return f;
}

// ── (d) A base just under kWrap components, and an at-cap overlay that crosses it ────────────
//
// `max_new` is the overlay's new-component cap (`kMaxNewComponents`). The base declares
// `kWrap - max_new / 2` empty components; the overlay adds `max_new` components New<k>, each
// holding one new field 9500 + k. The splice appends new definitions after the base's, so New<k>
// has id `base_count + k`, and from `k = max_new / 2` on that id is past the wrap.

struct crossing_fixture {
    std::string base;
    std::string overlay;
    std::uint32_t base_count = 0;
    std::vector<expected_component> added;  // New<k>, in overlay order

    [[nodiscard]] std::string precondition_failure(std::string_view element) const {
        std::size_t const defs = count_of(base, "\n<" + std::string{element} + " ");
        if (defs != base_count || base_count >= kWrap ||
            base_count + added.size() <= std::size_t{kWrap}) {
            return "the base is not just under kWrap or the overlay does not cross it (" +
                   std::to_string(defs) + " base definitions)";
        }
        return {};
    }
};

inline std::vector<expected_component> crossing_added(std::uint32_t base_count,
                                                      std::size_t max_new) {
    std::vector<expected_component> out;
    for (std::size_t k = 0; k < max_new; ++k) {
        expected_component c;
        c.name = "New" + std::to_string(k);
        c.id = base_count + static_cast<std::uint32_t>(k);
        c.members = {{static_cast<std::uint16_t>(9500U + k), c.id + 1}};
        out.push_back(std::move(c));
    }
    return out;
}

inline crossing_fixture make_xml_crossing_fixture(std::size_t max_new) {
    crossing_fixture f;
    f.base_count = kWrap - static_cast<std::uint32_t>(max_new / 2);
    f.added = crossing_added(f.base_count, max_new);
    f.base += kXmlOpen;
    f.base += "<header>" + x_fref("BeginString") + x_fref("BodyLength") + x_fref("MsgType") +
              "</header>\n<trailer>" + x_fref("CheckSum") + "</trailer>\n";
    f.base += "<messages><message name='NewOrderSingle' msgtype='D' msgcat='app'>" +
              x_fref("Text") + "</message></messages>\n<components>\n";
    for (std::uint32_t i = 0; i < f.base_count; ++i) {
        f.base += x_comp("F" + std::to_string(i), {});
    }
    f.base += "</components>\n<fields>\n" + x_field_def(8, "BeginString", "STRING") +
              x_field_def(9, "BodyLength", "LENGTH") + x_field_def(10, "CheckSum", "STRING") +
              x_field_def(35, "MsgType", "STRING") + x_field_def(58, "Text", "STRING") +
              "</fields>\n</fix>\n";
    std::string comps;
    std::string fields;
    for (auto const& c : f.added) {
        comps += x_comp(c.name, x_fref("NewF" + std::to_string(c.members[0].tag)));
        fields += x_field_def(c.members[0].tag, "NewF" + std::to_string(c.members[0].tag), "STRING");
    }
    f.overlay = std::string{kXmlOpen} + "<components>\n" + comps + "</components>\n<fields>\n" +
                fields + "</fields>\n</fix>\n";
    return f;
}

// Base ids skip 1024 and 1025, so no base component is a section; new ids start at 70000.
inline crossing_fixture make_orch_crossing_fixture(std::size_t max_new) {
    crossing_fixture f;
    f.base_count = kWrap - static_cast<std::uint32_t>(max_new / 2);
    f.added = crossing_added(f.base_count, max_new);
    f.base += kOrchOpen;
    f.base += "<fixr:fields>\n" + o_field_def(8, "BeginString", "String") +
              o_field_def(9, "BodyLength", "Length") + o_field_def(10, "CheckSum", "String") +
              o_field_def(35, "MsgType", "String") + "</fixr:fields>\n<fixr:components>\n";
    for (std::uint32_t i = 0; i < f.base_count; ++i) {
        std::uint32_t const id = i < 1024 ? i : i + 2;
        f.base += o_comp(id, "C" + std::to_string(i), {});
    }
    f.base += "</fixr:components>\n<fixr:messages>\n" +
              o_msg(1, "NewOrderSingle", "D", o_fref(35, "required")) + "</fixr:messages>\n";
    f.base += kOrchClose;
    std::string comps;
    std::string fields;
    for (std::size_t k = 0; k < f.added.size(); ++k) {
        auto const& c = f.added[k];
        comps += o_comp(70000U + static_cast<std::uint32_t>(k), c.name, o_fref(c.members[0].tag));
        fields += o_field_def(c.members[0].tag, "NewF" + std::to_string(c.members[0].tag), "String");
    }
    f.overlay = std::string{kOrchOpen} + "<fixr:fields>\n" + fields +
                "</fixr:fields>\n<fixr:components>\n" + comps + "</fixr:components>\n" +
                std::string{kOrchClose};
    return f;
}

// ── (e) 100's oracle: a component whose id is 0x10000 + a group's id ─────────────────────────
//
// Group G (id `kG`, numInGroup 9601, member 9602) is referenced from component C, and C from
// message M. C's id is `0x10000 + kG` (`colliding = true`) or `0x10000 + kG + 1` (the control
// twin). The document is in the shape of tests/support/divergent_dicts_100.hpp's Orchestra
// documents.

struct oracle_collision_fixture {
    std::string text;
    std::uint32_t group_id = 0;
    std::uint32_t component_id = 0;
    std::uint16_t no_tag = 9601;
    std::vector<std::uint16_t> members{9602};
    std::string msg_type = "M";

    [[nodiscard]] std::string precondition_failure(bool colliding) const {
        if (colliding != (component_id == 0x10000U + group_id)) {
            return "the component id does not stand in the stated relation to the group id";
        }
        if (count_of(text, "<fixr:field id='") != 4 + 2) {
            return "the document does not declare exactly its six fields";
        }
        if (!declared_and_referenced(text, "fixr:group", "fixr:groupRef", group_id) ||
            !declared_and_referenced(text, "fixr:component", "fixr:componentRef", component_id)) {
            return "G or C is not declared and referenced";
        }
        if (count_of(text, "<fixr:numInGroup id='" + std::to_string(no_tag) + "'") != 1 ||
            count_of(text, "<fixr:fieldRef id='" + std::to_string(members[0]) + "'") != 1) {
            return "G's NumInGroup or member is not as stated";
        }
        return {};
    }
};

inline oracle_collision_fixture make_oracle_collision_fixture(bool colliding) {
    oracle_collision_fixture f;
    f.group_id = 5;
    f.component_id = 0x10000U + f.group_id + (colliding ? 0U : 1U);
    f.text = std::string{kOrchOpen} + "<fixr:fields>\n" + o_field_def(8, "BeginString", "String") +
             o_field_def(9, "BodyLength", "Length") + o_field_def(10, "CheckSum", "String") +
             o_field_def(35, "MsgType", "String") +
             o_field_def(f.no_tag, "NoG", "NumInGroup") +
             o_field_def(f.members[0], "GM", "String") + "</fixr:fields>\n<fixr:components>\n" +
             o_comp(f.component_id, "C", o_gref(f.group_id)) +
             "</fixr:components>\n<fixr:groups>\n" +
             o_group(f.group_id, "G", f.no_tag, o_fref(f.members[0])) +
             "</fixr:groups>\n<fixr:messages>\n" +
             o_msg(1, "M", f.msg_type, o_fref(35, "required") + o_cref(f.component_id)) +
             "</fixr:messages>\n" + std::string{kOrchClose};
    return f;
}

// ── (f) An Orchestra overlay that adds a field to a base component past 65535 ────────────────
//
// Base: OvHigh (id 70020 {9701}) and its alias OvAlias (id 4484 {9702}), both referenced from
// message D. The overlay declares field 9703 and adds it to OvHigh.

struct component_overlay_fixture {
    std::string base;
    std::string overlay;
    std::string high_name = "OvHigh", alias_name = "OvAlias";
    std::uint32_t high_id = 70020;
    std::uint16_t high_member = 9701, alias_member = 9702, added = 9703;
    std::vector<alias_pair> pairs;

    [[nodiscard]] std::string precondition_failure() const { return pairs_failure(base, pairs); }
};

inline component_overlay_fixture make_component_overlay_fixture() {
    component_overlay_fixture f;
    std::uint32_t const alias = f.high_id - kWrap;
    f.pairs = {{"fixr:component", "fixr:componentRef", f.high_id}};
    f.base = std::string{kOrchOpen} + "<fixr:fields>\n" + o_field_def(8, "BeginString", "String") +
             o_field_def(9, "BodyLength", "Length") + o_field_def(10, "CheckSum", "String") +
             o_field_def(35, "MsgType", "String") +
             o_field_def(f.high_member, "OvHighF", "String") +
             o_field_def(f.alias_member, "OvAliasF", "String") +
             "</fixr:fields>\n<fixr:components>\n" +
             o_comp(1024, "StandardHeader",
                    o_fref(8, "required") + o_fref(9, "required") + o_fref(35, "required")) +
             o_comp(f.high_id, f.high_name, o_fref(f.high_member)) +
             o_comp(alias, f.alias_name, o_fref(f.alias_member)) +
             "</fixr:components>\n<fixr:messages>\n" +
             o_msg(1, "NewOrderSingle", "D",
                   o_cref(1024, "required") + o_cref(f.high_id) + o_cref(alias)) +
             "</fixr:messages>\n" + std::string{kOrchClose};
    f.overlay = std::string{kOrchOpen} + "<fixr:fields>\n" +
                o_field_def(f.added, "OvAdded", "String") +
                "</fixr:fields>\n<fixr:components>\n" +
                o_comp(f.high_id, f.high_name, o_fref(f.added)) + "</fixr:components>\n" +
                std::string{kOrchClose};
    return f;
}

inline component_overlay_fixture const& component_overlay_fixture_text() {
    static component_overlay_fixture const f = make_component_overlay_fixture();
    return f;
}

// ── (g) Contract §6 (b′), cap half: restatements of base components past 65535 ──────────────
//
// The base declares `max_new + 1` components R<k> (id 80000 + k, {9801}); the overlay restates
// each with its id and name unchanged and no children.

struct restatement_fixture {
    std::string base;
    std::string overlay;
    std::size_t restated = 0;

    [[nodiscard]] std::string precondition_failure(std::size_t max_new) const {
        std::size_t const n = count_of(overlay, "<fixr:component id='");
        if (n != restated || n != max_new + 1) {
            return "the overlay restates " + std::to_string(n) + " components, not the cap + 1";
        }
        for (std::size_t k = 0; k < n; ++k) {
            if (count_of(base, "<fixr:component id='" + std::to_string(80000 + k) + "'") != 1) {
                return "a restated component is not declared once in the base";
            }
        }
        return {};
    }
};

inline restatement_fixture make_restatement_fixture(std::size_t max_new) {
    restatement_fixture f;
    f.restated = max_new + 1;
    std::string comps;
    std::string restate;
    for (std::size_t k = 0; k < f.restated; ++k) {
        auto const id = static_cast<std::uint32_t>(80000 + k);
        std::string const name = "R" + std::to_string(k);
        comps += o_comp(id, name, o_fref(9801));
        restate += o_comp(id, name, {});
    }
    f.base = std::string{kOrchOpen} + "<fixr:fields>\n" + o_field_def(8, "BeginString", "String") +
             o_field_def(9, "BodyLength", "Length") + o_field_def(10, "CheckSum", "String") +
             o_field_def(35, "MsgType", "String") + o_field_def(9801, "RF", "String") +
             "</fixr:fields>\n<fixr:components>\n" + comps +
             "</fixr:components>\n<fixr:messages>\n" +
             o_msg(1, "NewOrderSingle", "D", o_fref(35, "required")) + "</fixr:messages>\n" +
             std::string{kOrchClose};
    f.overlay = std::string{kOrchOpen} + "<fixr:components>\n" + restate + "</fixr:components>\n" +
                std::string{kOrchClose};
    return f;
}

// ── (h) Contract §6 (b′), budget half: a doubling chain whose component ids pass 65535 ──────
//
// The shape of tests/dictionary/overlay_refusal_test.cpp's `orch_doubling_overlay`, over a small
// base whose Instrument (id 1003, {55, 48}) is C_0: C_n = {componentRef C_{n-1}; groupRef G_n},
// G_n = {numInGroup; fieldRef F_n; componentRef C_{n-1}}, n = 1 .. levels, with
// `levels = min(max_new_components, max_new_groups) - 1`. Every new component id (100000 + n),
// and so every componentRef to one, passes 65535; the group ids (4000 + n) do not.
//
// `own_steps(n)` is C_n's own expansion in the build's unit (one step per child node visited):
// C_0 has two children; C_n visits two children, C_{n-1}'s expansion, and G_n's three children
// plus C_{n-1}'s expansion again. It saturates at the largest `std::uint64_t`.

struct doubling_fixture {
    std::string base;
    std::string overlay;
    std::size_t levels = 0;

    [[nodiscard]] std::uint64_t own_steps(std::size_t n) const {
        constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t t = 2;
        for (std::size_t i = 1; i <= n; ++i) {
            t = t > (kMax - 5) / 2 ? kMax : 5 + (2 * t);
        }
        return t;
    }
    [[nodiscard]] std::string precondition_failure(std::uint64_t budget) const {
        if (own_steps(levels) <= budget) {
            return "the chain's last component expands to no more than the budget";
        }
        return {};
    }
};

inline doubling_fixture make_doubling_fixture(std::size_t max_new_components,
                                              std::size_t max_new_groups) {
    doubling_fixture f;
    f.levels = (max_new_components < max_new_groups ? max_new_components : max_new_groups) - 1;
    f.base = std::string{kOrchOpen} + "<fixr:fields>\n" + o_field_def(8, "BeginString", "String") +
             o_field_def(9, "BodyLength", "Length") + o_field_def(10, "CheckSum", "String") +
             o_field_def(35, "MsgType", "String") + o_field_def(48, "SecurityID", "String") +
             o_field_def(55, "Symbol", "String") + "</fixr:fields>\n<fixr:components>\n" +
             o_comp(1024, "StandardHeader",
                    o_fref(8, "required") + o_fref(9, "required") + o_fref(35, "required")) +
             o_comp(1003, "Instrument", o_fref(55, "required") + o_fref(48)) +
             "</fixr:components>\n<fixr:messages>\n" +
             o_msg(1, "NewOrderSingle", "D", o_cref(1024, "required") + o_cref(1003, "required")) +
             "</fixr:messages>\n" + std::string{kOrchClose};
    std::string comps;
    std::string groups;
    std::string fields;
    for (std::size_t n = 1; n <= f.levels; ++n) {
        auto const cid = static_cast<std::uint32_t>(100000 + n);
        std::uint32_t const prev = n == 1 ? 1003U : cid - 1U;
        auto const gid = static_cast<std::uint32_t>(4000 + n);
        auto const no = static_cast<std::uint32_t>(20000 + n);
        auto const fld = static_cast<std::uint32_t>(21000 + n);
        comps += o_comp(cid, "VenueC" + std::to_string(n), o_cref(prev) + o_gref(gid));
        groups += o_group(gid, "VenueG" + std::to_string(n), no, o_fref(fld) + o_cref(prev));
        fields += o_field_def(no, "NoVenueG" + std::to_string(n), "NumInGroup") +
                  o_field_def(fld, "VenueF" + std::to_string(n), "String");
    }
    f.overlay = std::string{kOrchOpen} + "<fixr:fields>\n" + fields +
                "</fixr:fields>\n<fixr:components>\n" + comps +
                "</fixr:components>\n<fixr:groups>\n" + groups + "</fixr:groups>\n" +
                std::string{kOrchClose};
    return f;
}

}  // namespace fixpp::test102::width
