#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/overlay_side_table_docs_102.hpp — TEST-ONLY document builders of the 098
// side-table rows (tests/dictionary/overlay_refusal_test.cpp's `OverlayRefusalW3XmlSideTable` and
// `OverlayRefusalW3OrchSideTable`), moved here by 102-dict-width-multichar-type (tasks T013) so the
// C-ABI cells (tests/capi/test_102_dict_load_capi.cpp) build the same documents. Text code with
// standard-library dependencies only. Each `*_size*` function is a sizing expression a side-table
// row computed inline; the row now calls it.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp::test102::side {

inline constexpr std::string_view kOpen = "<fix type='FIX' major='4' minor='4' servicepack='0'>\n";

inline std::string overlay_doc(std::string_view body) {
    return std::string{kOpen} + std::string{body} + "</fix>\n";
}

inline std::string field_def(int number, std::string_view name, std::string_view type) {
    return "  <field number='" + std::to_string(number) + "' name='" + std::string{name} +
           "' type='" + std::string{type} + "' />\n";
}

inline std::string fref(std::string_view name) {
    return "   <field name='" + std::string{name} + "' required='N' />\n";
}

// A generated base whose side tables fit: message A1 holds `kGroups` groups NoSG_i
// of one member each (each the first-seen group of its tag), and three components
// declared in the order E0, E1, Late. VenueBig is a new component of `kBigFields`
// fields; a group NoSG_i { component VenueBig } adds 1 + kBigFields entries to its
// parent level and leaves the groups table alone, since NoSG_i is first seen in A1.

inline constexpr int kGroups = 700;
inline constexpr int kBigFields = 100;

inline std::string side_table_base() {
    std::string msg = "  <message name='SideTable' msgtype='A1' msgcat='app'>\n";
    std::string fields;
    for (int i = 0; i < kGroups; ++i) {
        msg += "   <group name='NoSG" + std::to_string(i) + "' required='N'>\n" +
               fref("SGF" + std::to_string(i)) + "   </group>\n";
        fields += field_def(10000 + i, "NoSG" + std::to_string(i), "NUMINGROUP") +
                  field_def(11000 + i, "SGF" + std::to_string(i), "STRING");
    }
    msg += "  </message>\n";
    return std::string{kOpen} +
           " <header>\n  <field name='BeginString' required='Y' />\n"
           " </header>\n <messages>\n" +
           msg +
           " </messages>\n <components>\n"
           "  <component name='E0'>\n" +
           fref("BeginString") +
           "  </component>\n"
           "  <component name='E1'>\n" +
           fref("BeginString") +
           "  </component>\n"
           "  <component name='Late'>\n" +
           fref("BeginString") + "  </component>\n </components>\n <fields>\n" +
           field_def(8, "BeginString", "STRING") + fields + " </fields>\n</fix>\n";
}

inline std::string big_component_and_fields(std::string_view extra_fields = {}) {
    std::string big = "  <component name='VenueBig'>\n";
    std::string fields = " <fields>\n";
    for (int i = 0; i < kBigFields; ++i) {
        big += fref("VenueBigF" + std::to_string(i));
        fields += field_def(20000 + i, "VenueBigF" + std::to_string(i), "STRING");
    }
    return big + "  </component>\n" + "@@FIELDS@@" + fields + std::string{extra_fields} +
           " </fields>\n";
}

// `count` groups NoSG_i { component VenueBig } starting at `first`.
inline std::string big_groups(int first, int count) {
    std::string s;
    for (int i = first; i < first + count; ++i) {
        s += "   <group name='NoSG" + std::to_string(i) +
             "' required='N'>\n"
             "    <component name='VenueBig' required='N' />\n   </group>\n";
    }
    return s;
}

// components = the overlay's <components> body (VenueBig is added), messages = its
// <messages> body, extra fields beyond VenueBig's.
inline std::string side_overlay(std::string const& components, std::string const& messages = {},
                                std::string const& extra_fields = {}) {
    std::string const bc = big_component_and_fields(extra_fields);
    std::size_t const cut = bc.find("@@FIELDS@@");
    std::string body;
    if (!messages.empty()) {
        body += " <messages>\n" + messages + " </messages>\n";
    }
    body += " <components>\n" + components + bc.substr(0, cut) + " </components>\n" +
            bc.substr(cut + std::string_view{"@@FIELDS@@"}.size());
    return overlay_doc(body);
}

// The entries one NoSG_i { component VenueBig } adds.
inline constexpr int kPerGroup = 1 + kBigFields;

inline std::string grow_e0_e1(int each) {
    return "  <component name='E0'>\n" + big_groups(0, each) +
           "  </component>\n"
           "  <component name='E1'>\n" +
           big_groups(each, each) + "  </component>\n";
}

// A large group NoVenueLow(5), lower than every base NumInGroup tag, in a new
// message that sorts after A1 (so every NoSG_i stays first seen in A1).
inline std::string low_group_overlay(int n) {
    return side_overlay({},
                        "  <message name='VenueLowMsg' msgtype='Z9' msgcat='app'>\n"
                        "   <group name='NoVenueLow' required='N'>\n" +
                            big_groups(0, n) + "   </group>\n  </message>\n",
                        field_def(5, "NoVenueLow", "NUMINGROUP"));
}

inline constexpr std::string_view kOrchOpen =
    "<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' "
    "version='FIX.Latest_EP303'>\n";

inline std::string orch_overlay(std::string_view body) {
    return std::string{kOrchOpen} + std::string{body} + "</fixr:repository>\n";
}

inline std::string ofield(int id, std::string_view name, std::string_view type,
                          std::string_view extra = {}) {
    return "  <fixr:field id='" + std::to_string(id) + "' name='" + std::string{name} + "' type='" +
           std::string{type} + "'" + std::string{extra} + " />\n";
}

// A reference element: kind is fieldRef, componentRef or groupRef.
inline std::string oref(std::string_view kind, int id, std::string_view presence = {}) {
    return "    <fixr:" + std::string{kind} + " id='" + std::to_string(id) + "'" +
           (presence.empty() ? std::string{} : " presence='" + std::string{presence} + "'") +
           " />\n";
}

inline std::string ocomp(int id, std::string_view name, std::string_view body) {
    return "  <fixr:component id='" + std::to_string(id) + "' name='" + std::string{name} + "'>\n" +
           std::string{body} + "  </fixr:component>\n";
}

inline std::string ogroup(int id, std::string_view name, int nig, std::string_view body) {
    return "  <fixr:group id='" + std::to_string(id) + "' name='" + std::string{name} +
           "'>\n    <fixr:numInGroup id='" + std::to_string(nig) + "' />\n" + std::string{body} +
           "  </fixr:group>\n";
}

inline std::string omsg(int id, std::string_view name, std::string_view msg_type,
                        std::string_view body) {
    return "  <fixr:message id='" + std::to_string(id) + "' name='" + std::string{name} +
           "' msgType='" + std::string{msg_type} + "'>\n   <fixr:structure>\n" + std::string{body} +
           "   </fixr:structure>\n  </fixr:message>\n";
}

inline std::string ofields(std::string_view body) {
    return " <fixr:fields>\n" + std::string{body} + " </fixr:fields>\n";
}
inline std::string ocomps(std::string_view body) {
    return " <fixr:components>\n" + std::string{body} + " </fixr:components>\n";
}
inline std::string ogroups(std::string_view body) {
    return " <fixr:groups>\n" + std::string{body} + " </fixr:groups>\n";
}

// A generated base whose side tables fit: message A1 refers to `kGroups` groups
// SG_i (NumInGroup tag 10000 + i) of one member each, each the first-seen group of
// its tag, and three components declared in the order E0, E1, Late. VenueBig is a
// new component of `kOrchBigFields` fields. A new group H_h has a base tag 10000 + h,
// so it is never first seen (A1 registers that tag first) and leaves the groups
// table alone; H_h = { componentRef VenueBig } adds 1 + kOrchBigFields entries to
// its parent level. A new group G_k (base tag 10000 + 100 + k) refers to the first
// `n` of the H groups and adds 1 + n × (1 + kOrchBigFields) entries.

inline constexpr int kOrchBigFields = 960;
inline constexpr int kOrchPerH = 1 + kOrchBigFields;
inline constexpr int kOrchHGroups = 23;

inline std::string orch_side_table_base() {
    std::string msg;
    std::string fields = ofield(8, "BeginString", "String");
    std::string groups;
    for (int i = 0; i < kGroups; ++i) {
        msg += oref("groupRef", 10000 + i);
        groups +=
            ogroup(10000 + i, "SG" + std::to_string(i), 10000 + i, oref("fieldRef", 11000 + i));
        fields += ofield(10000 + i, "NoSG" + std::to_string(i), "NumInGroup") +
                  ofield(11000 + i, "SGF" + std::to_string(i), "String");
    }
    return std::string{kOrchOpen} + ofields(fields) +
           ocomps(ocomp(1, "E0", oref("fieldRef", 8)) + ocomp(2, "E1", oref("fieldRef", 8)) +
                  ocomp(3, "Late", oref("fieldRef", 8))) +
           ogroups(groups) + " <fixr:messages>\n" + omsg(1, "SideTable", "A1", msg) +
           " </fixr:messages>\n</fixr:repository>\n";
}

// G groups 30000 + k, each referring to the first `n_k` H groups (20000 + h).
struct orch_side_parts {
    std::vector<int> g_sizes;  // n_k of each G group
};

inline std::string orch_side_groups(orch_side_parts const& parts) {
    std::string groups;
    for (int h = 0; h < kOrchHGroups; ++h) {
        groups +=
            ogroup(20000 + h, "VenueH" + std::to_string(h), 10000 + h, oref("componentRef", 3999));
    }
    for (std::size_t k = 0; k < parts.g_sizes.size(); ++k) {
        std::string body;
        for (int h = 0; h < parts.g_sizes[k]; ++h) {
            body += oref("groupRef", 20000 + h);
        }
        groups += ogroup(30000 + static_cast<int>(k), "VenueG" + std::to_string(k),
                         10000 + 100 + static_cast<int>(k), body);
    }
    return ogroups(groups);
}

inline std::string orch_big_and_fields(std::string_view extra_fields = {}) {
    std::string big;
    std::string fields;
    for (int i = 0; i < kOrchBigFields; ++i) {
        big += oref("fieldRef", 40000 + i);
        fields += ofield(40000 + i, "VenueBigF" + std::to_string(i), "String");
    }
    return ocomp(3999, "VenueBig", big) + "@@FIELDS@@" + fields + std::string{extra_fields};
}

// components = the overlay's fixr:components body (VenueBig is appended after it),
// messages = its fixr:messages body, extra fields beyond VenueBig's.
inline std::string orch_side_overlay(std::string const& components, orch_side_parts const& parts,
                                     std::string const& messages = {},
                                     std::string const& extra_fields = {}) {
    std::string const bc = orch_big_and_fields(extra_fields);
    std::size_t const cut = bc.find("@@FIELDS@@");
    std::string body = ofields(bc.substr(cut + std::string_view{"@@FIELDS@@"}.size())) +
                       ocomps(components + bc.substr(0, cut)) + orch_side_groups(parts);
    if (!messages.empty()) {
        body += " <fixr:messages>\n" + messages + " </fixr:messages>\n";
    }
    return orch_overlay(body);
}

// The refs to the G groups `first` .. `first + count - 1`.
inline std::string g_refs(int first, int count) {
    std::string s;
    for (int k = first; k < first + count; ++k) {
        s += oref("groupRef", 30000 + k);
    }
    return s;
}

inline constexpr int orch_g_entries(int n) { return 1 + (n * kOrchPerH); }

inline std::string orch_grow_e0_e1() {
    return ocomp(1, "E0", oref("fieldRef", 8) + g_refs(0, 2)) +
           ocomp(2, "E1", oref("fieldRef", 8) + g_refs(2, 1));
}

// A large group NoVenueLow(5), lower than every base NumInGroup tag, in a new message
// that sorts after A1 (so every SG_i and every reused tag stays first seen in A1).
inline std::string orch_low_group_overlay(std::vector<int> const& g_sizes) {
    std::string const low =
        ogroup(4999, "VenueLow", 5, g_refs(0, static_cast<int>(g_sizes.size())));
    std::string const overlay =
        orch_side_overlay({}, {g_sizes}, omsg(9999, "VenueLowMsg", "Z9", oref("groupRef", 4999)),
                          ofield(5, "NoVenueLow", "NumInGroup"));
    // NoVenueLow joins the groups container the H and G groups are in.
    std::string out = overlay;
    std::string const anchor = " </fixr:groups>\n";
    out.insert(out.find(anchor), low);
    return out;
}

// ── The sizing each side-table row computes (moved from the rows) ────────────────────────────

// QuickFIX XML, the start push: E0 and E1 each stay within a count of 65535; Late's start
// passes it.
inline int xml_start_push_each() { return ((65536 / 2) / kPerGroup) + 1; }

// QuickFIX XML, the new component's count.
inline int xml_new_count_n() { return (65536 / kPerGroup) + 1; }

// QuickFIX XML, the low-tagged group: its own count fits; later starts do not.
inline int xml_low_group_n() { return (65535 - (kGroups / 2)) / kPerGroup; }

// Orchestra, the start push and the new component's count: each G group refers to every H
// group.
inline orch_side_parts orch_start_push_parts() {
    return {{kOrchHGroups, kOrchHGroups, kOrchHGroups}};
}
inline orch_side_parts orch_new_count_parts() {
    return {{kOrchHGroups, kOrchHGroups, kOrchHGroups}};
}

// Orchestra, the low-tagged group's G sizes: its own count fits; a later base group's start
// does not.
inline std::vector<int> orch_low_group_sizes() {
    return {kOrchHGroups, kOrchHGroups, kOrchHGroups - 1};
}

}  // namespace fixpp::test102::side
