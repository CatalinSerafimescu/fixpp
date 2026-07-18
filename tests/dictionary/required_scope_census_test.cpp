// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/required_scope_census_test.cpp
//
// 079-required-presence-scope T015/T016/T017 (fixpp#201) — the non-circular
// required-set census: Contract 1 (message-level) + Contract 1a (per-group).
//
// STANDALONE binary (own executable + `add_test(NAME required_scope_census)`),
// per `[const §VII.8]`: an exact-set completeness gate is isolation-sensitive
// (`[[feedback_completeness_gate_exact_set_not_subset]]`). Selected via
// `-R required_scope_census` (Article VII §8 carve-out).
//
// Anchors:
//   tasks:     specs/079-required-presence-scope/tasks.md T015/T016/T017
//   contracts: specs/079-required-presence-scope/contracts/census-and-agreement.md
//              Contract 1 + Contract 1a
//   spec:      specs/079-required-presence-scope/spec.md FR-009/FR-009a/
//              SC-003/SC-003a/US4
//   data-model: specs/079-required-presence-scope/data-model.md §Census entities
//
// ============================================================================
// NON-CIRCULARITY BANNER (Contract 1 / Contract 1a): everything in the
// "independent oracle" section below (`qfix_walk`/`orch_walk`/
// `build_quickfix_oracle`/`build_orchestra_oracle`) is a from-scratch pugixml
// walk of the VENDORED XML. It MUST NOT call `fixpp::dict::XmlLoader`,
// `fixpp::dict::OrchestraLoader`, or `fixpp::codegen::build_ir()`. This ban
// applies ONLY to the oracle; the "actual side" legs below legitimately
// construct a `Dictionary` via the real loaders (`table_view` leg) and call
// `fixpp::codegen::build_ir()` directly (IR leg — mirrors the
// `codegen_067_emit_builders_unit_test` precedent of linking `ir.cpp` into a
// non-codegen test binary; that is the SHIPPED side under test, not the
// walker).
// ============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ir.hpp"  // fixpp::codegen::build_ir / MessageIR / collect_top_fields — ACTUAL side only

namespace {

using fixpp::dict::Dictionary;
using fixpp::dict::field_presence;
using fixpp::dict::table_view;

// StandardHeader/StandardTrailer fields (Contract 1): structurally-always-
// required, NEVER dropped even when a message references the header/trailer
// componentRef with default (optional) presence. On Orchestra, exactly one
// message (`PayManagementReportAck`) references `StandardHeader` (component
// id 1024) WITHOUT `presence="required"` (defaults to optional), and four
// messages reference `StandardTrailer` (id 1025) the same way — verified by
// direct inspection of dictionaries/orchestra/OrchestraFIXLatest.xml. Without
// this carve-out the full-component-AND oracle below would (correctly, per
// its own stronger rule) drop BeginString/BodyLength/... from those messages'
// expected sets while the shipped loader (which never gates on componentRef
// presence at all) keeps them — a spurious mismatch, not a real defect.
constexpr std::array<std::uint16_t, 8> kHeaderTrailerTags{8, 9, 34, 35, 49, 52, 56, 10};

bool is_header_trailer_tag(std::uint16_t tag) {
    return std::find(kHeaderTrailerTags.begin(), kHeaderTrailerTags.end(), tag) !=
           kHeaderTrailerTags.end();
}

// One (msg_type, outer-to-inner ancestor no_tag path, this group's own
// no_tag) coordinate — mirrors table_view.hpp's group_ctx_key convention
// (path EXCLUDES no_tag itself).
struct GroupContextKey {
    std::string msg_type;
    std::vector<std::uint16_t> path;
    std::uint16_t no_tag = 0;

    friend bool operator<(GroupContextKey const& a, GroupContextKey const& b) {
        return std::tie(a.msg_type, a.path, a.no_tag) < std::tie(b.msg_type, b.path, b.no_tag);
    }
};

// The independent oracle's full census result for one dictionary.
struct DictOracle {
    // Contract 1: msg_type -> message-level required tag set.
    std::map<std::string, std::set<std::uint16_t>> message_required;
    // Contract 1a: every REAL per-context group (non-empty direct members,
    // mirroring the loader's "if (members.empty()) continue" skip) -> its
    // direct member tags (used only to enumerate which contexts exist).
    std::map<GroupContextKey, std::set<std::uint16_t>> group_members;
    // Contract 1a: the same contexts' DIRECT required='Y' member tags.
    std::map<GroupContextKey, std::set<std::uint16_t>> group_required;
};

// ─────────────────────────── independent oracle (QuickFIX-XML) ────────────

// NOLINTNEXTLINE(misc-no-recursion) — recursive XML walk by design, mirrors
// (but shares no code with) LoaderState::expand_field_list.
void qfix_walk(pugi::xml_node parent, std::unordered_map<std::string, std::uint16_t> const& tag_by_name,
               std::unordered_map<std::string, pugi::xml_node> const& components_by_name,
               std::string const& msg_type, std::vector<std::uint16_t>& group_path, bool component_and,
               std::set<std::uint16_t>& msg_required, DictOracle& oracle) {
    for (auto const& child : parent.children()) {
        std::string_view const name{child.name()};
        if (name == "field") {
            auto const fname = std::string{child.attribute("name").as_string("")};
            auto const it = tag_by_name.find(fname);
            if (it == tag_by_name.end()) {
                throw std::runtime_error("required_scope_census oracle: <field name=\"" + fname +
                                         "\"> not declared in <fields> block");
            }
            auto const tag = it->second;
            bool const own_req = std::string_view{child.attribute("required").as_string("N")} == "Y";
            bool const msg_final = own_req && component_and && group_path.empty();
            if (msg_final || is_header_trailer_tag(tag)) {
                msg_required.insert(tag);
            }
            if (!group_path.empty()) {
                GroupContextKey key{msg_type, {group_path.begin(), group_path.end() - 1},
                                    group_path.back()};
                oracle.group_members[key].insert(tag);
                if (own_req) {
                    oracle.group_required[key].insert(tag);
                }
            }
        } else if (name == "group") {
            auto const gname = std::string{child.attribute("name").as_string("")};
            auto const it = tag_by_name.find(gname);
            if (it == tag_by_name.end()) {
                throw std::runtime_error("required_scope_census oracle: <group name=\"" + gname +
                                         "\"> has no matching <field> declaration");
            }
            auto const no_tag = it->second;
            bool const own_req = std::string_view{child.attribute("required").as_string("N")} == "Y";
            bool const msg_final = own_req && component_and && group_path.empty();
            if (msg_final || is_header_trailer_tag(no_tag)) {
                msg_required.insert(no_tag);
            }
            if (!group_path.empty()) {
                GroupContextKey key{msg_type, {group_path.begin(), group_path.end() - 1},
                                    group_path.back()};
                oracle.group_members[key].insert(no_tag);
                if (own_req) {
                    oracle.group_required[key].insert(no_tag);
                }
            }
            group_path.push_back(no_tag);
            qfix_walk(child, tag_by_name, components_by_name, msg_type, group_path, component_and,
                      msg_required, oracle);
            group_path.pop_back();
        } else if (name == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const cit = components_by_name.find(cname);
            if (cit == components_by_name.end()) {
                throw std::runtime_error("required_scope_census oracle: <component name=\"" + cname +
                                         "\"> not defined in <components> block");
            }
            bool const comp_req = std::string_view{child.attribute("required").as_string("N")} == "Y";
            qfix_walk(cit->second, tag_by_name, components_by_name, msg_type, group_path,
                      component_and && comp_req, msg_required, oracle);
        }
        // Ignore unknown child elements (forwards-compat; mirrors the loader).
    }
}

DictOracle build_quickfix_oracle(std::filesystem::path const& xml_path) {
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        throw std::runtime_error("required_scope_census oracle: failed to load " + xml_path.string());
    }
    auto const root = doc.child("fix");

    std::unordered_map<std::string, std::uint16_t> tag_by_name;
    for (auto const& f : root.child("fields").children("field")) {
        auto const name = std::string{f.attribute("name").as_string("")};
        auto const tag = static_cast<std::uint16_t>(f.attribute("number").as_uint());
        tag_by_name.emplace(name, tag);
    }
    std::unordered_map<std::string, pugi::xml_node> components_by_name;
    for (auto const& c : root.child("components").children("component")) {
        components_by_name.emplace(std::string{c.attribute("name").as_string("")}, c);
    }
    auto const header = root.child("header");
    auto const trailer = root.child("trailer");

    DictOracle oracle;
    for (auto const& m : root.child("messages").children("message")) {
        auto const msg_type = std::string{m.attribute("msgtype").as_string("")};
        auto& msg_required = oracle.message_required[msg_type];
        std::vector<std::uint16_t> group_path;
        if (header) {
            qfix_walk(header, tag_by_name, components_by_name, msg_type, group_path, true,
                      msg_required, oracle);
        }
        qfix_walk(m, tag_by_name, components_by_name, msg_type, group_path, true, msg_required,
                  oracle);
        if (trailer) {
            qfix_walk(trailer, tag_by_name, components_by_name, msg_type, group_path, true,
                      msg_required, oracle);
        }
    }
    return oracle;
}

// ─────────────────────────── independent oracle (Orchestra) ───────────────

// NOLINTNEXTLINE(misc-no-recursion)
void orch_walk(pugi::xml_node parent, std::unordered_map<std::uint32_t, pugi::xml_node> const& components_by_id,
               std::unordered_map<std::uint32_t, pugi::xml_node> const& groups_by_id,
               std::string const& msg_type, std::vector<std::uint16_t>& group_path, bool component_and,
               std::set<std::uint16_t>& msg_required, DictOracle& oracle) {
    for (auto const& child : parent.children()) {
        std::string_view const name{child.name()};
        if (name == "fixr:fieldRef") {
            auto const tag = static_cast<std::uint16_t>(child.attribute("id").as_uint());
            bool const own_req =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            bool const msg_final = own_req && component_and && group_path.empty();
            if (msg_final || is_header_trailer_tag(tag)) {
                msg_required.insert(tag);
            }
            if (!group_path.empty()) {
                GroupContextKey key{msg_type, {group_path.begin(), group_path.end() - 1},
                                    group_path.back()};
                oracle.group_members[key].insert(tag);
                if (own_req) {
                    oracle.group_required[key].insert(tag);
                }
            }
        } else if (name == "fixr:groupRef") {
            auto const gid = child.attribute("id").as_uint();
            auto const git = groups_by_id.find(gid);
            if (git == groups_by_id.end()) {
                throw std::runtime_error("required_scope_census oracle: <fixr:groupRef id=\"" +
                                         std::to_string(gid) + "\"> not defined in <fixr:groups>");
            }
            auto const numin = git->second.child("fixr:numInGroup");
            if (!numin) {
                throw std::runtime_error("required_scope_census oracle: <fixr:group id=\"" +
                                         std::to_string(gid) + "\"> missing <fixr:numInGroup>");
            }
            auto const no_tag = static_cast<std::uint16_t>(numin.attribute("id").as_uint());
            bool const own_req =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            bool const msg_final = own_req && component_and && group_path.empty();
            if (msg_final || is_header_trailer_tag(no_tag)) {
                msg_required.insert(no_tag);
            }
            if (!group_path.empty()) {
                GroupContextKey key{msg_type, {group_path.begin(), group_path.end() - 1},
                                    group_path.back()};
                oracle.group_members[key].insert(no_tag);
                if (own_req) {
                    oracle.group_required[key].insert(no_tag);
                }
            }
            group_path.push_back(no_tag);
            orch_walk(git->second, components_by_id, groups_by_id, msg_type, group_path,
                      component_and, msg_required, oracle);
            group_path.pop_back();
        } else if (name == "fixr:componentRef") {
            auto const cid = child.attribute("id").as_uint();
            auto const cit = components_by_id.find(cid);
            if (cit == components_by_id.end()) {
                throw std::runtime_error("required_scope_census oracle: <fixr:componentRef id=\"" +
                                         std::to_string(cid) + "\"> not defined in <fixr:components>");
            }
            bool const comp_req =
                std::string_view{child.attribute("presence").as_string("")} == "required";
            orch_walk(cit->second, components_by_id, groups_by_id, msg_type, group_path,
                      component_and && comp_req, msg_required, oracle);
        }
        // Ignore fixr:annotation / other unknown child elements.
    }
}

DictOracle build_orchestra_oracle(std::filesystem::path const& xml_path) {
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        throw std::runtime_error("required_scope_census oracle: failed to load " + xml_path.string());
    }
    auto const root = doc.child("fixr:repository");

    std::unordered_map<std::uint32_t, pugi::xml_node> components_by_id;
    for (auto const& c : root.child("fixr:components").children("fixr:component")) {
        components_by_id.emplace(c.attribute("id").as_uint(), c);
    }
    std::unordered_map<std::uint32_t, pugi::xml_node> groups_by_id;
    for (auto const& g : root.child("fixr:groups").children("fixr:group")) {
        groups_by_id.emplace(g.attribute("id").as_uint(), g);
    }

    DictOracle oracle;
    for (auto const& m : root.child("fixr:messages").children("fixr:message")) {
        auto const msg_type = std::string{m.attribute("msgType").as_string("")};
        if (msg_type.empty()) {
            continue;
        }
        auto& msg_required = oracle.message_required[msg_type];
        std::vector<std::uint16_t> group_path;
        auto const structure = m.child("fixr:structure");
        orch_walk(structure, components_by_id, groups_by_id, msg_type, group_path, true, msg_required,
                  oracle);
    }
    return oracle;
}

// ═══════════════════════════ end of independent oracle ════════════════════

// ─────────────────────────── shared test fixtures ──────────────────────────

struct DictCase {
    std::string label;
    std::string filename;
    bool is_orchestra = false;
    bool has_ir = false;  // codegen build_ir() target (v42/v44/v50sp2/vt11/vlatest)
};

std::vector<DictCase> const kAllDicts{
    {.label = "FIX40.xml", .filename = "FIX40.xml", .is_orchestra = false, .has_ir = false},
    {.label = "FIX41.xml", .filename = "FIX41.xml", .is_orchestra = false, .has_ir = false},
    {.label = "FIX42.xml", .filename = "FIX42.xml", .is_orchestra = false, .has_ir = true},
    {.label = "FIX43.xml", .filename = "FIX43.xml", .is_orchestra = false, .has_ir = false},
    {.label = "FIX44.xml", .filename = "FIX44.xml", .is_orchestra = false, .has_ir = true},
    {.label = "FIX50.xml", .filename = "FIX50.xml", .is_orchestra = false, .has_ir = false},
    {.label = "FIX50SP1.xml", .filename = "FIX50SP1.xml", .is_orchestra = false, .has_ir = false},
    {.label = "FIX50SP2.xml", .filename = "FIX50SP2.xml", .is_orchestra = false, .has_ir = true},
    {.label = "FIXT11.xml", .filename = "FIXT11.xml", .is_orchestra = false, .has_ir = true},
    {.label = "OrchestraFIXLatest.xml",
     .filename = "OrchestraFIXLatest.xml",
     .is_orchestra = true,
     .has_ir = true},
};

std::filesystem::path dict_path(DictCase const& dc) {
    if (dc.is_orchestra) {
        return std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / dc.filename;
    }
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / dc.filename;
}

DictOracle build_oracle(DictCase const& dc) {
    return dc.is_orchestra ? build_orchestra_oracle(dict_path(dc)) : build_quickfix_oracle(dict_path(dc));
}

Dictionary load_actual(DictCase const& dc, std::pmr::memory_resource* mr) {
    if (dc.is_orchestra) {
        return fixpp::dict::OrchestraLoader{}.load(dict_path(dc), mr);
    }
    return fixpp::dict::XmlLoader{}.load(dict_path(dc), mr);
}

std::set<std::uint16_t> to_set(std::span<std::uint16_t const> s) {
    return std::set<std::uint16_t>{s.begin(), s.end()};
}

std::string describe_diff(std::set<std::uint16_t> const& expected, std::set<std::uint16_t> const& actual) {
    std::vector<std::uint16_t> missing;
    std::vector<std::uint16_t> extra;
    std::ranges::set_difference(expected, actual, std::back_inserter(missing));
    std::ranges::set_difference(actual, expected, std::back_inserter(extra));
    std::ostringstream oss;
    if (!missing.empty()) {
        oss << "missing-from-actual{";
        for (auto t : missing) {
            oss << t << ",";
        }
        oss << "} ";
    }
    if (!extra.empty()) {
        oss << "extra-in-actual{";
        for (auto t : extra) {
            oss << t << ",";
        }
        oss << "}";
    }
    return oss.str();
}

constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;

}  // namespace

// ============================================================================
// T015 — Contract 1: message-level required-set census across all 10 dicts.
// Two legs: `table_view::required_fields()` (Step-2's literal probe surface)
// and, for the 5 codegen-target versions, the IR's top-level required list
// (`collect_top_fields` filtered to `rule == Required`; safety-net leg —
// `fixpp::codegen::build_ir()` throws for the 5 non-codegen-target dicts —
// FIX40/FIX41/FIX43/FIX50/FIX50SP1 — since `kCodegenVersions` in ir.cpp only
// maps v42/v44/v50sp2/vt11/vlatest; those 5 get the table_view leg only).
//
// ⚠️ KNOWN-RED / ESCALATED (2026-07-18, /implement Phase 6): this test is
// genuinely RED on 4 real (non-synthetic) sites, NOT a walker defect —
// confirmed against the QuickFIX-cpp 1.16.0 reference engine
// (`reference-engines/quickfix-cpp/src/C++/DataDictionary.cpp:498-530`,
// `addXMLComponentFields`): a `<field>`/`<group>` child of a `<component>`
// is only added to the MESSAGE-level required set when its own
// `required='Y'` AND the enclosing `componentRef`'s own `required` is 'Y'
// (`componentRequired` parameter, lines 509-512) — QuickFIX DOES apply
// component-AND, including to a group's own count-field. fixpp's
// `expand_field_list` (both loaders) NEVER gates on a componentRef's own
// `required` attribute at all (ignores it entirely, for both fields and
// group count-fields) — a pre-existing over-require defect, DISTINCT from
// the group-NESTING leak 079's `in_group` fix already closed. The 4 sites:
// FIX50.xml AR / NoSides(552); FIX50SP1.xml AR / NoSides(552); FIX50SP1.xml
// AB / tag 555; FIX50SP1.xml AC / tag 555 — each a `<group required='Y'>`
// declared directly inside an OPTIONAL `<component required='N'>`
// (`TrdCapRptAckSideGrp` et al.). This CONTRADICTS the Phase-0 research.md
// "0 optional-component-required sites" measurement that T016(b)'s
// synthetic-injection RED-proof is predicated on being a *safety-net*
// (load-bearing precisely BECAUSE the real corpus was believed clean) — the
// real corpus is not clean. Per orchestrator guidance (advisor call,
// 2026-07-18): do NOT carve these sites out of the oracle or the assertion,
// do NOT fix the loader here (T020 / out of T015-T017 scope), and do NOT
// weaken the oracle's component-AND rule to force green — that would
// enshrine a confirmed-real defect behind a green test
// ([[feedback_coverage_push_enshrines_bugs]]). This RED is the escalation;
// see the phase report for the full finding and the orchestrator decision
// this test is blocked on (fix the loader / open a tracked issue / amend
// Phase-0's claim in research.md).
//
// ⚠️ RESOLVED (2026-07-19, T020 fix, user-approved scope expansion): the
// loader WAS fixed — `src/dictionary/xml_loader.cpp::expand_field_list` now
// threads a `component_required` running-AND parameter (default "N" per
// QuickFIX `DataDictionary.cpp:401-403` parity) that gates the
// `required_out` pushes, mirroring `componentRequired` above. This test is
// now GREEN on all 4 sites (and stays a durable regression pin — see
// research.md R3's superseded-banner + spec.md FR-001/FR-005). The
// "KNOWN-RED / blocked" banner above is retained as the historical record of
// the finding, not as the test's current status.
// ============================================================================
TEST(RequiredScopeCensus, MessageLevelMatchesTableViewAndIrAcrossAllTenDicts) {
    std::cout << "\n=== 079 T015: message-level required-set census (all 10 dicts) ===\n";

    std::size_t total_messages = 0;
    for (auto const& dc : kAllDicts) {
        auto storage = std::make_unique<std::byte[]>(kArenaBytes);
        std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

        auto const oracle = build_oracle(dc);
        auto const dict = load_actual(dc, &mr);
        auto const tv = dict.as_table_view();

        ASSERT_FALSE(dict.messages().empty()) << dc.label << ": failed to load or has no messages";

        std::optional<fixpp::codegen::VersionIR> ir;
        std::map<std::string, std::set<std::uint16_t>> ir_required;
        if (dc.has_ir) {
            auto storage2 = std::make_unique<std::byte[]>(kArenaBytes);
            std::pmr::monotonic_buffer_resource mr2{storage2.get(), kArenaBytes};
            ir = fixpp::codegen::build_ir(dict_path(dc), &mr2);
            for (auto const& m : ir->messages) {
                std::set<std::uint16_t> req;
                for (auto const* f : fixpp::codegen::collect_top_fields(m)) {
                    if (f->ref.rule == field_presence::Required) {
                        req.insert(f->ref.tag);
                    }
                }
                ir_required.emplace(m.msg_type, std::move(req));
            }
        }

        std::size_t checked = 0;
        for (auto const& msg : dict.messages()) {
            ++checked;
            auto const msg_type = std::string{msg.msg_type};
            auto const oit = oracle.message_required.find(msg_type);
            ASSERT_NE(oit, oracle.message_required.end())
                << dc.label << ": oracle has no entry for " << msg_type;
            auto const& expected = oit->second;

            auto const actual = to_set(tv.required_fields(msg_type));
            EXPECT_EQ(expected, actual) << dc.label << " " << msg_type
                                        << " (table_view): " << describe_diff(expected, actual);

            if (dc.has_ir) {
                auto const iit = ir_required.find(msg_type);
                ASSERT_NE(iit, ir_required.end())
                    << dc.label << ": IR has no entry for " << msg_type;
                EXPECT_EQ(expected, iit->second)
                    << dc.label << " " << msg_type
                    << " (IR): " << describe_diff(expected, iit->second);
            }
        }
        std::cout << "  " << dc.label << ": " << checked << " message(s) checked"
                  << (dc.has_ir ? " (table_view + IR)" : " (table_view only, no codegen IR)") << "\n";
        total_messages += checked;
    }
    std::cout << "  total messages censused across all 10 dicts: " << total_messages << "\n";
    EXPECT_GT(total_messages, 0u);
}

// Contract 1a's text (contracts/census-and-agreement.md) names only "except
// FIX42" for the L-066-1 carve-out. The authoritative cross-feature anchor,
// `spec/behaviors-and-limitations.md` L-066-1, is WIDER: "FIX 4.0/4.1/4.2
// sessions become strict-but-GROUP-BLIND... These three dictionaries type
// their group-count fields with the legacy XML INT (not NUMINGROUP)".
// Verified directly against the vendored XML: `grep -c "type='NUMINGROUP'"`
// is 0 for FIX40.xml AND FIX41.xml (not just FIX42.xml) — so
// `dictionary.cpp`'s NumInGroup-gated context/bare population loops register
// ZERO groups for all three, not one. Generalizing the carve-out to all
// three is faithful to L-066-1, not a scope-widening decision made here;
// flagged to the orchestrator as a Contract 1a text under-scoping (see the
// phase report) — Contract 1a should read "except FIX40/FIX41/FIX42".
bool is_group_blind_l0661_dict(std::string const& filename) {
    return filename == "FIX40.xml" || filename == "FIX41.xml" || filename == "FIX42.xml";
}

// ============================================================================
// T017 — Contract 1a leg 1 (PRIMARY): context store
// `group_required_members(msg_type, parent_path, no_tag)` == the walker's
// per-context required set, exact set-equality both directions, for every
// real group context in the 7 non-group-blind dicts. FIX40/FIX41/FIX42
// carve-out (L-066-1, widened from Contract 1a's text — see
// `is_group_blind_l0661_dict` above / #196): asserted context-store-EMPTY
// instead — see the dedicated carve-out test below.
// ============================================================================
TEST(RequiredScopeCensus, PerGroupContextStoreMatchesWalkerExceptL0661GroupBlindDicts) {
    std::cout << "\n=== 079 T017 leg 1: per-group CONTEXT required-member census "
                 "(7 dicts, FIX40/FIX41/FIX42 excluded per L-066-1) ===\n";

    std::size_t total_contexts = 0;
    std::size_t max_required_count = 0;
    std::string max_context_label;

    for (auto const& dc : kAllDicts) {
        if (is_group_blind_l0661_dict(dc.filename)) {
            continue;  // dedicated carve-out test below
        }
        auto storage = std::make_unique<std::byte[]>(kArenaBytes);
        std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

        auto const oracle = build_oracle(dc);
        auto const dict = load_actual(dc, &mr);
        auto const tv = dict.as_table_view();

        std::size_t contexts_here = 0;
        for (auto const& [key, members] : oracle.group_members) {
            (void)members;
            auto const rit = oracle.group_required.find(key);
            std::set<std::uint16_t> const expected =
                (rit != oracle.group_required.end()) ? rit->second : std::set<std::uint16_t>{};

            auto const actual =
                to_set(tv.group_required_members(key.msg_type, std::span{key.path}, key.no_tag));
            EXPECT_EQ(expected, actual)
                << dc.label << " msg=" << key.msg_type << " no_tag=" << key.no_tag
                << " (context store): " << describe_diff(expected, actual);

            if (expected.size() > max_required_count) {
                max_required_count = expected.size();
                std::ostringstream oss;
                oss << dc.label << " msg=" << key.msg_type << " no_tag=" << key.no_tag;
                max_context_label = oss.str();
            }
            ++contexts_here;
        }
        std::cout << "  " << dc.label << ": " << contexts_here << " real group context(s)\n";
        total_contexts += contexts_here;
    }

    std::cout << "  total group contexts censused: " << total_contexts << "\n";
    std::cout << "  RC5: maximum per-group DIRECT required-member count observed = "
              << max_required_count << " (" << max_context_label << ")\n";
    EXPECT_GT(total_contexts, 0u);

    // RC5 (Contract 1a): pins the shipped small-count assumption so the
    // dynamic-width per-instance check (T004) cannot silently regress to a
    // bounded skip without this test flagging the drift. Measured empirically
    // by this same census (FIX43.xml msg=N no_tag=73); if a future dictionary
    // refresh changes it, update this pin deliberately (not silently) — the
    // point is to KNOW.
    EXPECT_EQ(max_required_count, 6u)
        << "shipped maximum per-group required-member count changed from the measured baseline (6) — "
           "re-examine whether the dynamic-width check (validator.hpp consume_group) still covers it "
           "before updating this pin";
}

// ============================================================================
// T017 — L-066-1 carve-out (Contract 1a leg 1, widened from FIX42-only per
// the anchor text — see `is_group_blind_l0661_dict` above / #196): FIX
// 4.0/4.1/4.2's group-count fields are XML type='INT' (never 'NUMINGROUP'),
// so `dictionary.cpp`'s context-store population loop — gated on
// `field_data_type::NumInGroup` — registers ZERO context entries for any of
// the three, even though the independent walker (structural `<group>` scan,
// type-independent) sees their real groups. Assert the context store is
// EMPTY for every real group context the walker finds in each of the three —
// a pin that flips intentionally when #196 lands (relaxes NumInGroup
// detection to structural).
// ============================================================================
TEST(RequiredScopeCensus, PerGroupContextStoreIsEmptyForL0661GroupBlindDicts) {
    std::cout << "\n=== 079 T017 L-066-1 carve-out: context store must be EMPTY for "
                 "FIX40/FIX41/FIX42 ===\n";

    for (auto const& dc : kAllDicts) {
        if (!is_group_blind_l0661_dict(dc.filename)) {
            continue;
        }
        auto storage = std::make_unique<std::byte[]>(kArenaBytes);
        std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

        auto const oracle = build_oracle(dc);
        auto const dict = load_actual(dc, &mr);
        auto const tv = dict.as_table_view();

        ASSERT_GT(oracle.group_members.size(), 0u)
            << dc.label << ": independent oracle found zero real groups — the carve-out "
               "precondition ('this dict has real structural groups the type-gated store cannot "
               "see') is unmet; either the walker regressed or this dict no longer declares groups";

        std::size_t checked = 0;
        for (auto const& [key, members] : oracle.group_members) {
            (void)members;
            auto const actual =
                tv.group_required_members(key.msg_type, std::span{key.path}, key.no_tag);
            EXPECT_TRUE(actual.empty())
                << dc.label << " msg=" << key.msg_type << " no_tag=" << key.no_tag
                << ": context store unexpectedly non-empty — if this fires, issue #196 has landed "
                   "(NumInGroup detection relaxed to structural) and this carve-out test should be "
                   "removed/updated, not silently left red";
            ++checked;
        }
        std::cout << "  " << dc.label << ": " << checked
                  << " real group context(s) found by the walker, all confirmed "
                     "context-store-EMPTY\n";
    }
}

// ============================================================================
// T017 — Contract 1a leg 2 (fallback): bare store
// `group_required_members(no_tag)` == the global first-seen variant for
// `no_tag`. The bare store is NOT required to equal every per-context oracle
// (a reused no_tag can have divergent per-context required sets — e.g. FIX44
// tag 295 NoQuoteEntries: `{}` vs `{299}`), so this asserts the WEAKER
// fallback contract: the shipped bare value is a MEMBER of the set of
// distinct per-context required-member variants the walker observed for that
// no_tag in this dict (catches a corrupted/phantom bare value; does not pin
// which specific variant "first-seen" picks — the contract's own carve-out).
// FIX40/FIX41/FIX42 excluded (bare store is ALSO NumInGroup-type-gated, so
// it is empty for all three too — verified by the same carve-out test above
// via table_view's group_bit pre-filter, which gates both stores
// identically; see `is_group_blind_l0661_dict`).
// ============================================================================
TEST(RequiredScopeCensus, BareStoreIsAValidPerContextVariantExceptL0661GroupBlindDicts) {
    std::cout << "\n=== 079 T017 leg 2: bare store == a valid per-context variant "
                 "(7 dicts, FIX40/FIX41/FIX42 excluded per L-066-1) ===\n";

    std::size_t total_no_tags = 0;
    for (auto const& dc : kAllDicts) {
        if (is_group_blind_l0661_dict(dc.filename)) {
            continue;
        }
        auto storage = std::make_unique<std::byte[]>(kArenaBytes);
        std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

        auto const oracle = build_oracle(dc);
        auto const dict = load_actual(dc, &mr);
        auto const tv = dict.as_table_view();

        // Group the oracle's per-context required sets by no_tag -> distinct variants.
        std::map<std::uint16_t, std::set<std::set<std::uint16_t>>> variants_by_no_tag;
        for (auto const& [key, members] : oracle.group_members) {
            (void)members;
            auto const rit = oracle.group_required.find(key);
            std::set<std::uint16_t> const req =
                (rit != oracle.group_required.end()) ? rit->second : std::set<std::uint16_t>{};
            variants_by_no_tag[key.no_tag].insert(req);
        }

        std::size_t checked_here = 0;
        for (auto const& [no_tag, variants] : variants_by_no_tag) {
            auto const bare_actual = to_set(tv.group_required_members(no_tag));
            EXPECT_TRUE(variants.contains(bare_actual))
                << dc.label << " no_tag=" << no_tag
                << ": bare group_required_members() value is not among the " << variants.size()
                << " distinct per-context variant(s) the walker observed — shipped bare value looks "
                   "corrupted/phantom, not merely a different real variant";
            ++checked_here;
        }
        std::cout << "  " << dc.label << ": " << checked_here << " distinct no_tag(s) checked\n";
        total_no_tags += checked_here;
    }
    std::cout << "  total no_tags censused: " << total_no_tags << "\n";
    EXPECT_GT(total_no_tags, 0u);
}
