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
// NON-CIRCULARITY BANNER (Contract 1 / Contract 1a): the independent oracle
// (`qfix_walk`/`orch_walk`/`build_quickfix_oracle`/`build_orchestra_oracle`)
// used to live inline here; it was EXTRACTED to the sibling header
// `required_scope_oracle.hpp` at 079 T018/T019 time so
// `required_scope_parity_test.cpp` (Contract 2) can reuse the SAME walker
// rather than forking it (single-oracle guarantee). See that header's own
// banner for the non-circularity ban (must not call `XmlLoader`/
// `OrchestraLoader`/`build_ir()`). The "actual side" legs below legitimately
// construct a `Dictionary` via the real loaders (`table_view` leg) and call
// `fixpp::codegen::build_ir()` directly (IR leg — mirrors the
// `codegen_067_emit_builders_unit_test` precedent of linking `ir.cpp` into a
// non-codegen test binary; that is the SHIPPED side under test, not the
// walker).
// ============================================================================

#include <gtest/gtest.h>

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
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ir.hpp"  // fixpp::codegen::build_ir / MessageIR / collect_top_fields — ACTUAL side only
#include "required_scope_oracle.hpp"  // the shared independent oracle (079 T018/T019 extraction)

namespace {

using fixpp::dict::Dictionary;
using fixpp::dict::field_presence;
using fixpp::dict::table_view;
using namespace fixpp_test::required_scope_oracle;  // NOLINT(google-build-using-namespace)

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

// 082-structural-group-detection T015/T016/T018/T040/T042: the bare store's
// registered group-tag set, measured the SAME way T004's Phase-1 baseline
// was captured (implementation-notes.md § T004: "sweeping
// table_view::group_first_field(t) != 0 over all tags for each dictionary").
// A sweep over the whole uint16_t space rather than a union of declared
// field tags — table_view exposes no direct "list every registered no_tag"
// accessor, and this mirrors the existing measurement methodology exactly
// rather than inventing a second one.
std::set<std::uint16_t> bare_registered_group_tags(table_view const& tv) {
    std::set<std::uint16_t> tags;
    for (std::uint32_t t = 1; t <= 0xFFFFU; ++t) {
        auto const tag = static_cast<std::uint16_t>(t);
        if (tv.group_first_field(tag) != 0) {
            tags.insert(tag);
        }
    }
    return tags;
}

// 082 T017 bare-store leg: bounded, read-only, document-order pugixml scan --
// NOT "a third walker" in the 079 banner's sense (it shares no
// required/component-AND semantics with qfix_walk; only document POSITION
// matters here). Mirrors reused_tag_census_test.cpp's own DelimiterScan
// precedent (a separate bounded scan alongside the shared oracle walker).
//
// Determines which `<group name="...">` declaration site the loader's OWN
// global first-seen dedup guard (xml_loader.cpp:609,
// "if (!group_index_by_no_tag_.contains(no_tag))") resolves to: a pre-order,
// document-order depth-first search through `<field>`/`<group>`/`<component>`
// children, recursing into named `<component>` refs -- mirroring
// `expand_field_list`'s own traversal order (xml_loader.cpp:525+) -- and
// returning the FIRST `<group name=group_name>` node encountered.
std::optional<pugi::xml_node> dfs_find_group(
    pugi::xml_node const& node, std::string_view group_name,
    std::unordered_map<std::string, pugi::xml_node> const& components_by_name) {
    for (auto const& child : node.children()) {
        std::string_view const name{child.name()};
        if (name == "group") {
            if (std::string_view{child.attribute("name").as_string("")} == group_name) {
                return child;
            }
            if (auto found = dfs_find_group(child, group_name, components_by_name)) {
                return found;
            }
        } else if (name == "component") {
            auto const cname = std::string{child.attribute("name").as_string("")};
            auto const cit = components_by_name.find(cname);
            if (cit != components_by_name.end()) {
                if (auto found = dfs_find_group(cit->second, group_name, components_by_name)) {
                    return found;
                }
            }
        }
        // "field" and anything else: no group can be nested under it.
    }
    return std::nullopt;
}

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

// ============================================================================
// 082-structural-group-detection Phase 3/5 RED pins (tasks.md T015/T016/T017/
// T018/T040/T042 — written BEFORE T023 per the RED-first ordering rule).
// contracts/group-detection.md C1/C2/K1/K3/K4. See the ⚠ DESCOPE BANNER at
// the top of tasks.md for the FIX50SP2 502-vs-505 delta (issue #208), which
// T018 below pins explicitly.
//
// EXPECTED RED until T023 lands: `Dictionary::as_table_view()`'s bare and
// context population loops (dictionary.cpp:397-420, :439-470) gate on
// `fr.type == field_data_type::NumInGroup` BEFORE ever consulting the
// structural `Dictionary::group_first_field()` predicate. FIX40/41/42 type
// EVERY group-count tag `INT` (never `NUMINGROUP`), and FIX43's tag 576 is
// the same INT-vs-NUMINGROUP typo, so none of their group-declaring tags
// ever reach the loop body — the bare/context stores register zero (resp.
// 33, missing 576) for them today. T023 replaces the gate with
// `group_first_field(fr.tag) != 0` directly, at which point these pins go
// GREEN by construction.
// ============================================================================

// T015 [US1]: FIX42's 18 bare-store registered group tags, exact-set both
// directions vs the oracle (FR-005 / K1).
TEST(RequiredScopeCensus, Fix42BareStoreRegistersAllEighteenGroupTags) {
    std::cout << "\n=== 082 T015: FIX42 bare-store registered group-tag exact-set ===\n";

    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX42.xml";
    auto const oracle = build_quickfix_oracle(path);
    auto const dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto const tv = dict.as_table_view();

    ASSERT_EQ(oracle.group_tags.size(), 18u)
        << "FIX42 oracle group-tag count drifted from the pinned 18 -- re-derive, don't silently "
           "update this pin";

    auto const actual = bare_registered_group_tags(tv);
    EXPECT_EQ(oracle.group_tags, actual)
        << "FIX42 bare-store registered set vs oracle: " << describe_diff(oracle.group_tags, actual);
}

// T016 [US1]: FIX40 (4 tags) and FIX41 (7 tags) bare-store registered group
// tags, exact-set both directions vs the oracle — these two dictionaries
// have no codegen golden to regenerate, so this direct pin is their ONLY
// witness (FR-005 / K1).
TEST(RequiredScopeCensus, Fix40AndFix41BareStoreRegisterAllGroupTags) {
    std::cout << "\n=== 082 T016: FIX40/FIX41 bare-store registered group-tag exact-set ===\n";

    struct Case {
        char const* filename;
        std::size_t expected_count;
    };
    std::vector<Case> const kCases{{"FIX40.xml", 4}, {"FIX41.xml", 7}};

    for (auto const& c : kCases) {
        auto storage = std::make_unique<std::byte[]>(kArenaBytes);
        std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / c.filename;
        auto const oracle = build_quickfix_oracle(path);
        auto const dict = fixpp::dict::XmlLoader{}.load(path, &mr);
        auto const tv = dict.as_table_view();

        ASSERT_EQ(oracle.group_tags.size(), c.expected_count)
            << c.filename << ": oracle group-tag count drifted from the pinned value -- re-derive";

        auto const actual = bare_registered_group_tags(tv);
        EXPECT_EQ(oracle.group_tags, actual)
            << c.filename << " bare-store exact-set: " << describe_diff(oracle.group_tags, actual);
    }
}

// T017 [US1]: per-context member-set equality for FIX42's divergent-
// signature tag NoRelatedSym(146) across its 6 occurrences — 4 distinct
// direct-member lists (K4 / FR-004 / I-4a). Deliberately NOT tag 33
// (LinesOfText) — its two occurrences carry identical members, so a
// collapse-to-projection bug would be unobservable there (K4's own text).
// This checks the FULL per-context member SET via the context-scoped
// accessor `table_view::group_member_tags(msg_type, parent_path, no_tag)` —
// not a tag-set projection across contexts.
TEST(RequiredScopeCensus, Fix42Tag146PerContextMemberSetsMatchOracle) {
    std::cout << "\n=== 082 T017: FIX42 tag 146 (NoRelatedSym) per-context member sets ===\n";

    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX42.xml";
    auto const oracle = build_quickfix_oracle(path);
    auto const dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto const tv = dict.as_table_view();

    std::size_t contexts_checked = 0;
    std::set<std::set<std::uint16_t>> distinct_variants;
    for (auto const& [key, members] : oracle.group_members) {
        if (key.no_tag != 146) {
            continue;
        }
        auto const actual =
            to_set(tv.group_member_tags(key.msg_type, std::span{key.path}, key.no_tag));
        EXPECT_EQ(members, actual) << "FIX42 msg=" << key.msg_type
                                    << " no_tag=146 (context store): " << describe_diff(members, actual);
        distinct_variants.insert(members);
        ++contexts_checked;
    }

    ASSERT_EQ(contexts_checked, 6u)
        << "oracle found a different number of FIX42 tag-146 occurrences than the pinned 6 -- "
           "re-derive, don't silently update this pin";
    ASSERT_EQ(distinct_variants.size(), 4u)
        << "oracle found a different number of distinct tag-146 member-set variants than the "
           "pinned 4 -- re-derive, don't silently update this pin";

    std::vector<std::size_t> sizes;
    for (auto const& v : distinct_variants) {
        sizes.push_back(v.size());
    }
    std::ranges::sort(sizes);
    EXPECT_EQ(sizes, (std::vector<std::size_t>{19u, 20u, 22u, 31u}))
        << "distinct tag-146 variant sizes drifted from the pinned {19,20,22,31} "
           "({News,Email}=19, MarketDataRequest=20, "
           "{SecurityDefinitionRequest,SecurityDefinition}=22, QuoteRequest=31)";

    // ---- LEG 2 (082 T017): the BARE store holds the loader's FIRST-SEEN set ----
    //
    // BOTH legs are required and they assert DIFFERENT things. The two stores are
    // keyed differently — the context store by (msg_type, parent path, no_tag),
    // the bare store by no_tag alone — so "the stores agree" could only ever mean
    // a tag-set PROJECTION, which passes while every per-context member set is
    // wrong. Leg 1 above pins the context store per context; this leg pins the
    // bare store to the ONE variant the loader records (first-seen wins,
    // `xml_loader.cpp:609`). Without leg 2 a half-restructure that populates the
    // context store correctly and leaves the bare store wrong (or vice versa)
    // passes T017 — exactly what FR-004 exists to prevent. T015 does not close
    // this gap: it pins the bare store's registered *tag set*, not 146's *member
    // set*.
    //
    // The expected value is DERIVED via `dfs_find_group`, NOT transcribed:
    // walk `<messages>/<message>` in document order (xml_loader.cpp:747) and,
    // within each message, depth-first through field/group/component children
    // — the same traversal `expand_field_list` follows (xml_loader.cpp:525+) —
    // to find the FIRST `<group name="NoRelatedSym">` declaration site
    // anywhere in the document. A doc reorder therefore cannot silently
    // invalidate this pin; the scan re-derives the answer instead of
    // comparing against a stale literal.
    pugi::xml_document raw_doc;
    ASSERT_TRUE(raw_doc.load_file(path.c_str())) << "raw-XML scan: failed to load " << path;
    auto const raw_root = raw_doc.child("fix");
    std::unordered_map<std::string, pugi::xml_node> components_by_name;
    for (auto const& c : raw_root.child("components").children("component")) {
        components_by_name.emplace(std::string{c.attribute("name").as_string("")}, c);
    }

    std::string first_seen_msg_type;
    pugi::xml_node first_seen_node;
    // Header/trailer are expanded before EVERY message body by the real
    // loader (xml_loader.cpp:927-931), so a header/trailer-declared group
    // would win first-seen ahead of any message body — NoRelatedSym is not
    // header/trailer-declared in FIX42 (kHeaderTrailerTags has no group
    // entries), so scanning <messages> directly is faithful for this tag; a
    // header/trailer-declared no_tag would need this scan widened.
    for (auto const& m : raw_root.child("messages").children("message")) {
        if (auto found = dfs_find_group(m, "NoRelatedSym", components_by_name)) {
            first_seen_msg_type = std::string{m.attribute("msgtype").as_string("")};
            first_seen_node = *found;
            break;
        }
    }
    ASSERT_FALSE(first_seen_msg_type.empty())
        << "raw-XML document-order scan found no NoRelatedSym(146) declaration -- fixture/scan "
           "regression";
    ASSERT_EQ(std::string_view{first_seen_node.parent().name()}, std::string_view{"message"})
        << "first-seen NoRelatedSym(146) site is NOT a direct message-top-level child (path != "
           "[]) -- the GroupContextKey{msg_type, {}, 146} lookup below no longer holds; widen "
           "this scan to walk the found node's ancestor chain and resolve a non-empty path";

    GroupContextKey const first_seen_key{first_seen_msg_type, {}, 146};
    auto const oit = oracle.group_members.find(first_seen_key);
    ASSERT_NE(oit, oracle.group_members.end())
        << "oracle has no group_members entry for the scan-derived first-seen key (msg_type="
        << first_seen_msg_type << ") -- scan/oracle disagreement, investigate before trusting "
           "this pin";
    auto const& first_seen_variant = oit->second;

    // Sanity pin over the derivation (not a substitute for it): News (msgtype
    // 'B', line 269) is declared before Email (msgtype 'C', line 309) in
    // FIX42.xml, so News's 19-member NoRelatedSym is first-seen.
    EXPECT_EQ(first_seen_msg_type, "B")
        << "scan-derived first-seen msg_type for tag 146 drifted from the pinned 'B' (News) -- "
           "re-verify the dictionary's message order didn't change";
    ASSERT_EQ(first_seen_variant.size(), 19u)
        << "FIX42 News(B) tag-146 member count drifted from the derived 19";

    auto const bare_actual = to_set(tv.group_member_tags(146));
    EXPECT_EQ(first_seen_variant, bare_actual)
        << "FIX42 tag 146 (BARE store, first-seen wins, msg_type=" << first_seen_msg_type
        << "): " << describe_diff(first_seen_variant, bare_actual);
}

// T018 [US1]: the six unchanged dictionaries' bare-store registered group
// set, exact-set both directions vs the registered-after column (C2 / K1 /
// FR-014 / SC-002) — 082's C3 non-regression leg. Unlike T015/T016 this pin
// is NOT expected to flip RED->GREEN across T023 for five of the six rows
// (their group-count tags are already NUMINGROUP-typed, so the datatype gate
// was never the obstacle); it stands as a witness that the predicate swap
// moves none of them.
//
// FIX50SP2 is special-cased per the tasks.md DESCOPE BANNER (issue #208):
// the shipped loader's one-level-deep <component> member scan
// (xml_loader.cpp:610-641) never resolves 1499/1669/1919's only-nested-group
// members, so those three tags never register — a PRE-EXISTING defect
// unrelated to and unmoved by T023 (`group_first_field` returns 0 for all
// three both before and after the predicate swap;
// implementation-notes.md § BLOCKER B-1). Pinned at 502 = oracle.group_tags
// minus those 3 tags; this row flips to a plain oracle.group_tags comparison
// (505) once #208 lands.
TEST(RequiredScopeCensus, SixUnchangedDictionariesBareStoreExactSet) {
    std::cout << "\n=== 082 T018: six unchanged dictionaries' bare-store exact-set ===\n";

    struct Case {
        char const* label;
        char const* filename;
        bool is_orchestra;
        std::size_t expected_count;
    };
    std::vector<Case> const kCases{
        {"FIX44.xml", "FIX44.xml", false, 59},
        {"FIX50.xml", "FIX50.xml", false, 67},
        {"FIX50SP1.xml", "FIX50SP1.xml", false, 97},
        {"FIX50SP2.xml", "FIX50SP2.xml", false, 502},  // #208 -- see banner above; NOT 505
        {"FIXT11.xml", "FIXT11.xml", false, 1},
        {"OrchestraFIXLatest.xml", "OrchestraFIXLatest.xml", true, 524},
    };

    for (auto const& c : kCases) {
        auto storage = std::make_unique<std::byte[]>(kArenaBytes);
        std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

        auto const path = c.is_orchestra
                             ? std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / c.filename
                             : std::filesystem::path{FIXPP_DICT_DATA_DIR} / c.filename;
        auto const oracle = c.is_orchestra ? build_orchestra_oracle(path) : build_quickfix_oracle(path);
        auto const dict = c.is_orchestra ? fixpp::dict::OrchestraLoader{}.load(path, &mr)
                                          : fixpp::dict::XmlLoader{}.load(path, &mr);
        auto const tv = dict.as_table_view();

        auto expected = oracle.group_tags;
        if (std::string_view{c.filename} == "FIX50SP2.xml") {
            // #208: never registers -- one-level <component> scan defect, not this feature's predicate.
            expected.erase(1499);
            expected.erase(1669);
            expected.erase(1919);
        }
        ASSERT_EQ(expected.size(), c.expected_count)
            << c.label << ": derived expected-set size drifted from the pinned count -- re-derive";

        auto const actual = bare_registered_group_tags(tv);
        EXPECT_EQ(expected, actual)
            << c.label << " bare-store exact-set: " << describe_diff(expected, actual);
    }
}

// T040 [US3]: FIX43 tag 576 (NoClearingInstructions) registers as a
// repeating group with member ClearingInstruction(577) in the bare store.
// 576 is INT-typed (a pre-existing dialect typo -- FIX44 types it correctly
// as NUMINGROUP), so registering it is only possible once T023 replaces the
// `fr.type == NumInGroup` filter with the structural `group_first_field()`
// predicate -- FR-001's behavioral witness, not a token grep (FR-011 / K3).
TEST(RequiredScopeCensus, Fix43Tag576RegistersAsGroupWithClearingInstructionMember) {
    std::cout << "\n=== 082 T040: FIX43 tag 576 (NoClearingInstructions) registration ===\n";

    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX43.xml";
    auto const dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto const tv = dict.as_table_view();

    EXPECT_NE(tv.group_first_field(576), 0)
        << "FIX43 tag 576 (NoClearingInstructions) not registered as a group in the bare store -- "
           "INT-typed group-count fields are filtered out before T023's predicate swap";
    auto const members = to_set(tv.group_member_tags(576));
    EXPECT_EQ(members, (std::set<std::uint16_t>{577}))
        << "FIX43 tag 576 member set: " << describe_diff(std::set<std::uint16_t>{577}, members);
}

// T042 [US3]: FIX43's bare-store registered set differs from the T004
// pre-change baseline (33 tags; implementation-notes.md § T004) by EXACTLY
// +1 tag (576) -- not merely "more than before" -- and no OTHER tag moves
// (FR-013 / SC-003 / K3).
TEST(RequiredScopeCensus, Fix43RegisteredSetDeltaIsExactlyPlusOneTag576) {
    std::cout << "\n=== 082 T042: FIX43 registered-set delta vs T004 baseline ===\n";

    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX43.xml";
    auto const oracle = build_quickfix_oracle(path);
    auto const dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto const tv = dict.as_table_view();

    ASSERT_EQ(oracle.group_tags.size(), 34u)
        << "FIX43 oracle struct-set drifted from the pinned 34 -- re-derive, don't silently update";

    // T004's pre-change baseline (33 tags), derived here as oracle.group_tags
    // minus 576 -- not re-transcribed, so it stays tied to the oracle's own
    // 34-tag output rather than a second hand-copied literal.
    auto baseline_before = oracle.group_tags;
    baseline_before.erase(576);
    ASSERT_EQ(baseline_before.size(), 33u)
        << "derived T004 baseline drifted from the pinned 33 -- re-derive";

    auto const actual = bare_registered_group_tags(tv);

    // No OTHER tag moves: every one of the 33 baseline tags must still be
    // registered.
    for (auto const tag : baseline_before) {
        EXPECT_TRUE(actual.contains(tag))
            << "FIX43 baseline tag " << tag << " unexpectedly un-registered by the predicate swap";
    }
    // The delta is EXACTLY {576} -- both that it registers (RED until T023)
    // and that nothing beyond it changes.
    EXPECT_EQ(oracle.group_tags, actual)
        << "FIX43 bare-store set vs full 34-tag registered-after set: "
        << describe_diff(oracle.group_tags, actual);
    EXPECT_EQ(actual.size(), baseline_before.size() + 1)
        << "FIX43 registered-set delta is not exactly +1 tag -- actual size " << actual.size()
        << " vs baseline " << baseline_before.size();
}
