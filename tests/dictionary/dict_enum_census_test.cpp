// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/dict_enum_census_test.cpp
//
// 075-live-wire-enum-validation T021 [US1] — SC-010/FR-016 + SC-011/FR-022
// dictionary census gates.
//
// STANDALONE binary (own executable + add_test), per `[const §VII.8]`: an
// exact-set completeness gate is isolation-sensitive
// (`[[feedback_completeness_gate_exact_set_not_subset]]`).
//
// GOVERNING RULE: every assertion below is computed from the SHIPPED
// `dictionaries/*.xml` at test time (via `XmlLoader`/`OrchestraLoader` and,
// where the loader itself sanitizes the fact under test — dedup, fail-closed
// missing-`enum` — a direct pugixml scan of the raw file). None of these
// numbers is a hand-maintained expectation list; a dictionary refresh that
// violates any invariant here fails the BUILD.
//
// Anchors:
//   tasks:      specs/075-live-wire-enum-validation/tasks.md T021
//   spec:       specs/075-live-wire-enum-validation/spec.md SC-010/SC-011
//               (:289-301), FR-016/FR-022
//   T016 pin:   specs/075-live-wire-enum-validation/tasks.md T016
//   research:   specs/075-live-wire-enum-validation/research.md R-11
//   production: src/dictionary/dictionary.cpp:459-511 (as_table_view() —
//               the store-driven enum-domain build + T016's multi_value
//               default, mirrored below field-for-field so the census asks
//               the SAME "message-unreachable" question production answers)
//
// SCOPE NOTE (stated explicitly, not silently narrowed — mirrors T028's
// carve-out discipline): SC-011's "zero duplicate codes / zero `<value>`
// missing `enum` / zero missing `description`" checks are scoped to the
// NINE XmlLoader (QuickFIX-XML) dictionaries, because `<value enum=
// description=>` is a QuickFIX-XML-specific element/attribute shape that
// does not exist in Orchestra's `fixr:codeSet`/`fixr:code value= name=>`
// schema. The SPACE-CODE exact-set check is the one leg SC-011 explicitly
// measures across all TEN (spec.md:292-299 — "including the Orchestra one,
// 5708 fixr:code values, zero with a space"), so it alone spans both
// schemas. The T016 store-only-multi-value pin is likewise scoped to the
// nine — T016's own task text names "35 such tags across FIX50/SP1/SP2, the
// FIXT split", never Orchestra.

#include "reused_tag_census.hpp"  // fixpp_test_support::kRuntimeDicts (the nine)

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

using fixpp::dict::Dictionary;
using fixpp::dict::field_presence;
using fixpp_test_support::kRuntimeDicts;

// Generous heap-backed arena — census tests are not alloc-gated (FIX50SP2.xml
// is 1.4 MiB and the largest metadata handle in the tree; see
// reused_tag_census_test.cpp's identical rationale).
constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;

// ---------------------------------------------------------------------------
// Raw pugixml census of ONE QuickFIX-XML `<fields>` section: for every
// `<field number=... type=...>`, its raw (NOT deduped) `<value>` children,
// in declaration order — exactly what the loader itself sees before its own
// dedupe/fail-closed logic runs (xml_loader.cpp T013). Reading this
// independently of `Dictionary::enum_values()` is deliberate: the loader
// already sanitizes duplicates transparently, so asking the loaded
// Dictionary "were there duplicates?" is unanswerable — only the raw file
// can say.
// ---------------------------------------------------------------------------
struct RawValue {
    std::string code;      // the `enum=` attribute value (wire literal)
    bool has_enum_attr = false;
    bool has_desc_attr = false;
};
struct RawField {
    std::uint16_t tag = 0;
    std::string name;
    std::string type;
    std::vector<RawValue> values;  // declaration order, NOT deduped
};

std::vector<RawField> scan_quickfix_fields(std::filesystem::path const& xml_path) {
    std::vector<RawField> out;
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        return out;  // reported empty; the primary Dictionary::load below
                      // already exercises the same file and would fail loudly.
    }
    auto const fields_node = doc.child("fix").child("fields");
    for (auto const& f : fields_node.children("field")) {
        RawField rf;
        rf.tag = static_cast<std::uint16_t>(f.attribute("number").as_uint());
        rf.name = f.attribute("name").value();
        rf.type = f.attribute("type").value();
        for (auto const& v : f.children("value")) {
            RawValue rv;
            auto const enum_attr = v.attribute("enum");
            rv.has_enum_attr = !enum_attr.empty();
            rv.code = enum_attr.value();
            rv.has_desc_attr = !v.attribute("description").empty();
            rf.values.push_back(std::move(rv));
        }
        out.push_back(std::move(rf));
    }
    return out;
}

// One space-bearing declared code, with enough provenance to name it in the
// exact-set assertion (dictionary label + field/codeSet name + tag/id).
struct SpaceCode {
    std::string dict_label;
    std::string field_name;
    std::string code;

    friend bool operator<(SpaceCode const& a, SpaceCode const& b) {
        return std::tie(a.dict_label, a.field_name, a.code) <
               std::tie(b.dict_label, b.field_name, b.code);
    }
    friend bool operator==(SpaceCode const& a, SpaceCode const& b) {
        return a.dict_label == b.dict_label && a.field_name == b.field_name && a.code == b.code;
    }
};

std::ostream& operator<<(std::ostream& os, SpaceCode const& sc) {
    return os << sc.dict_label << ":" << sc.field_name << ":\"" << sc.code << "\"";
}

// Raw scan of Orchestra's `fixr:codeSets/fixr:codeSet/fixr:code value=...`
// tree for space-bearing `value` attributes (SC-011's tenth-dictionary leg).
void scan_orchestra_space_codes(std::filesystem::path const& xml_path, std::string const& label,
                                 std::set<SpaceCode>& out) {
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    ASSERT_TRUE(result) << "failed to load " << xml_path;
    auto const code_sets = doc.child("fixr:repository").child("fixr:codeSets");
    for (auto const& cs : code_sets.children("fixr:codeSet")) {
        std::string const cs_name = cs.attribute("name").value();
        for (auto const& code : cs.children("fixr:code")) {
            std::string_view const value = code.attribute("value").value();
            if (value.find(' ') != std::string_view::npos) {
                out.insert(SpaceCode{label, cs_name, std::string{value}});
            }
        }
    }
}

}  // namespace

// ============================================================================
// SC-010 / FR-016 — no message type is bricked by its own MsgType code.
//
// For every shipped dictionary (all ten — the accept-floor this pins is a
// property of `table_view::enum_valid()`'s Floor 1, which both loaders feed
// identically via the same store-driven `as_table_view()`), every declared
// `<message msgtype="X">` must be present in the MsgType(35) code set, OR
// that code set must be EMPTY.
//
// Rationale: a dictionary declaring a message type while omitting it from
// tag 35's codeset would have that message type REJECTED OUTRIGHT by
// `enum_valid(35, msgtype)` — a self-inflicted total loss for every message
// of that type.
//
// FIXT11 passes via the EMPTY-SET arm: its `MsgType` field declares ZERO
// `<value>` children. This is stated explicitly, not special-cased — it is
// a LOAD-BEARING dependency on FR-003's accept-floor (Floor 1: absent tag /
// empty codeset => accept), not a coincidence. Without that floor, FIXT11
// would reject every single message it carries.
//
// Mutation proof performed manually (not shipped): temporarily forcing this
// assertion to require an always-non-empty codeset on FIXT11 flips it RED
// (see report). Reverted before commit.
// ============================================================================
TEST(DictEnumCensus, SC010_EveryMessageTypeCoveredByMsgTypeCodesetOrCodesetEmpty) {
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    std::cout << "\n=== 075 T021 SC-010: MsgType(35) codeset coverage census (all ten) ===\n";

    bool saw_fixt11_empty_arm = false;

    auto const check = [&](Dictionary const& dict, std::string const& label) {
        auto const msgtype_codes = dict.enum_values(std::uint16_t{35});
        bool const codeset_empty = msgtype_codes.empty();

        std::set<std::string_view> declared_codes;
        for (auto const& ev : msgtype_codes) {
            declared_codes.insert(ev.value);
        }

        std::size_t checked = 0;
        std::size_t missing = 0;
        std::vector<std::string_view> missing_types;
        for (auto const& msg : dict.messages()) {
            ++checked;
            if (!codeset_empty && !declared_codes.contains(msg.msg_type)) {
                ++missing;
                missing_types.push_back(msg.msg_type);
            }
        }

        std::cout << "  " << label << ": " << checked << " declared message type(s), MsgType(35) "
                  << (codeset_empty ? "codeset EMPTY (Floor-1 accept-floor arm)"
                                    : "codeset has " + std::to_string(msgtype_codes.size()) + " code(s)")
                  << "\n";

        EXPECT_EQ(missing, 0u)
            << label << ": " << missing << " declared message type(s) NOT present in the MsgType(35) "
            << "codeset while that codeset is non-empty — those message types would be REJECTED "
            << "OUTRIGHT by enum_valid(35, msgtype). First offender: "
            << (missing_types.empty() ? std::string{} : std::string{missing_types.front()});

        if (label == "FIXT11.xml") {
            saw_fixt11_empty_arm = codeset_empty;
        }
    };

    for (auto const& fname : kRuntimeDicts) {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
        auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
        ASSERT_FALSE(dict.messages().empty()) << fname << " failed to load or has no messages";
        check(dict, std::string{fname});
    }

    {
        auto const path = std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "OrchestraFIXLatest.xml";
        auto dict = fixpp::dict::OrchestraLoader{}.load(path, &mr);
        ASSERT_FALSE(dict.messages().empty()) << "Orchestra failed to load or has no messages";
        check(dict, std::string{"OrchestraFIXLatest.xml"});
    }

    // FIXT11 MUST take the empty-set arm — this is the load-bearing case the
    // whole test exists to pin. If a future FIXT11 revision started
    // declaring MsgType codes, this assertion would need re-examination
    // (not silent re-baselining): the empty-set arm is why FIXT11 works at
    // all today.
    EXPECT_TRUE(saw_fixt11_empty_arm)
        << "FIXT11's MsgType(35) codeset is expected to be EMPTY (the accept-floor arm) — if this "
           "assertion fails, FIXT11 now declares MsgType codes and must be re-examined against the "
           "non-empty arm instead, not silently accepted.";
}

// ============================================================================
// SC-011 / FR-022 leg 1 — zero duplicate codes / zero missing `enum` / zero
// missing `description`, across the NINE XmlLoader (QuickFIX-XML)
// dictionaries (scope note at file header).
//
// research.md R-5 measured all three as ZERO on 2026-07-14. This test
// RE-DERIVES that fact from the shipped XML at test time (raw pugixml scan,
// deliberately bypassing the loader's own dedupe/fail-closed sanitization —
// see scan_quickfix_fields()'s header comment) rather than trusting the
// prior measurement, so a future dictionary refresh that regresses any of
// the three fails the BUILD.
// ============================================================================
TEST(DictEnumCensus, SC011_ZeroDuplicateZeroMissingEnumZeroMissingDescription) {
    std::cout << "\n=== 075 T021 SC-011 leg 1: duplicate/missing-attr census (nine QuickFIX dicts) ===\n";

    std::size_t total_duplicate_codes = 0;
    std::size_t total_missing_enum = 0;
    std::size_t total_missing_description = 0;

    for (auto const& fname : kRuntimeDicts) {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
        auto const raw_fields = scan_quickfix_fields(path);
        ASSERT_FALSE(raw_fields.empty()) << fname << ": raw <fields> scan found nothing";

        std::size_t dup_here = 0;
        std::size_t missing_enum_here = 0;
        std::size_t missing_desc_here = 0;

        for (auto const& rf : raw_fields) {
            std::set<std::string> seen;
            for (auto const& rv : rf.values) {
                if (!rv.has_enum_attr) {
                    ++missing_enum_here;
                }
                if (!rv.has_desc_attr) {
                    ++missing_desc_here;
                }
                if (rv.has_enum_attr && !seen.insert(rv.code).second) {
                    ++dup_here;
                }
            }
        }

        std::cout << "  " << fname << ": " << raw_fields.size() << " field(s), " << dup_here
                  << " duplicate code(s), " << missing_enum_here << " missing-enum, " << missing_desc_here
                  << " missing-description\n";

        total_duplicate_codes += dup_here;
        total_missing_enum += missing_enum_here;
        total_missing_description += missing_desc_here;
    }

    EXPECT_EQ(total_duplicate_codes, 0u)
        << "duplicate <value enum=...> codes found within a single field across the nine shipped "
           "QuickFIX dictionaries — QuickFIX tolerates this (union semantics, T013 rule 1), but a "
           "regression in OUR vendored data should be caught, not silently deduped away.";
    EXPECT_EQ(total_missing_enum, 0u)
        << "a <value> element missing its enum attribute in a shipped QuickFIX dictionary would "
           "make the LOADER ITSELF throw (T013 rule 2, fail-closed) — this leg exists as a direct, "
           "loader-independent confirmation.";
    EXPECT_EQ(total_missing_description, 0u)
        << "a <value> element missing its description attribute across the nine shipped QuickFIX "
           "dictionaries — legal per T013 rule 3, but a regression in OUR vendored data should be "
           "caught, not silently accepted as diagnostics-only noise.";
}

// ============================================================================
// SC-011 / FR-022 leg 2 — the EXACT-SET of declared codes containing a
// SPACE, across ALL TEN shipped dictionaries (spec.md:292-299; the one leg
// SC-011 explicitly measures across both schemas — Orchestra included).
//
// This is an EXACT-SET predicate, not subset/presence
// (`[[feedback_completeness_gate_exact_set_not_subset]]`): GREEN today
// because the tree happens to have exactly two such codes (both
// `SettlLocation(166)`'s `"ISO Country Code"` placeholder, on FIX41 and
// FIX42 — research.md R-11 / spec.md DV-4). ANY addition, removal, or
// edited literal — on ANY of the ten dictionaries, including a brand-new
// Orchestra codeSet — fails the build. An earlier formulation of this gate
// ("no declared code contains a space") was a gate DEFINED to fail (Gate A
// round 2, C2-1) — this exact-exception-set form is green today and goes
// RED on any drift.
// ============================================================================
TEST(DictEnumCensus, SC011_SpaceBearingCodesAreExactlyTheTwoSettlLocationPlaceholders) {
    std::set<SpaceCode> found;

    for (auto const& fname : kRuntimeDicts) {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
        auto const raw_fields = scan_quickfix_fields(path);
        ASSERT_FALSE(raw_fields.empty()) << fname << ": raw <fields> scan found nothing";
        for (auto const& rf : raw_fields) {
            for (auto const& rv : rf.values) {
                if (rv.code.find(' ') != std::string::npos) {
                    found.insert(SpaceCode{std::string{fname}, rf.name, rv.code});
                }
            }
        }
    }

    {
        auto const path = std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "OrchestraFIXLatest.xml";
        scan_orchestra_space_codes(path, "OrchestraFIXLatest.xml", found);
    }

    std::set<SpaceCode> const expected{
        SpaceCode{"FIX41.xml", "SettlLocation", "ISO Country Code"},
        SpaceCode{"FIX42.xml", "SettlLocation", "ISO Country Code"},
    };

    std::cout << "\n=== 075 T021 SC-011 leg 2: space-bearing declared codes across all TEN dictionaries "
                 "===\n";
    for (auto const& sc : found) {
        std::cout << "  " << sc << "\n";
    }

    EXPECT_EQ(found, expected)
        << "the set of declared codes containing a space, across all ten shipped dictionaries, must "
           "be EXACTLY the two SettlLocation(166) 'ISO Country Code' placeholders on FIX41/FIX42 "
           "(spec.md DV-4 / FR-022) — any addition, removal, or edited literal converts a placeholder "
           "codeset into a silent reject-everything trap and must be re-argued, not silently absorbed.";
}

// ============================================================================
// SC-011 / FR-022 leg 3 — the T016 pin: every enum-backed tag present in the
// STORE (`Dictionary::enum_values(tag)` non-empty) but ABSENT from message
// expansion (unreachable via any `Dictionary::messages()` /
// `Dictionary::message_fields()` walk) carries a NON-multi-value declared
// type today.
//
// Mirrors `Dictionary::as_table_view()` (src/dictionary/dictionary.cpp:
// 459-511) field-for-field: "message expansion" below is exactly the same
// walk production uses to decide `set_multi_value()`, so this census asks
// the SAME reachability question T016's default answers.
//
// T016's default (`multi_value = false` for a store-only tag) is an
// ASSUMPTION, measured-safe today (35 store-only tags, all STRING/BOOLEAN/
// INT/CHAR, per T016's own task text). This gate makes a future violating
// dictionary go RED instead of silently false-rejecting a conformant
// multi-value field through the tokenizer-bypassed single-token path.
//
// Scoped to the NINE XmlLoader dictionaries (T016's own task text: "35 such
// tags across FIX50/SP1/SP2, the FIXT split" — never names Orchestra).
// ============================================================================
TEST(DictEnumCensus, SC011_StoreOnlyEnumBackedTagsAreNeverMultiValueTyped) {
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    std::cout << "\n=== 075 T021 SC-011 leg 3 (T016 pin): store-only enum-backed tag types (nine "
                 "QuickFIX dicts) ===\n";

    std::size_t total_store_only = 0;

    for (auto const& fname : kRuntimeDicts) {
        auto const dict_path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
        auto dict = fixpp::dict::XmlLoader{}.load(dict_path, &mr);
        ASSERT_FALSE(dict.messages().empty()) << fname << " failed to load or has no messages";

        auto const raw_fields = scan_quickfix_fields(dict_path);
        ASSERT_FALSE(raw_fields.empty()) << fname << ": raw <fields> scan found nothing";

        // "message expansion" — mirrors dictionary.cpp:467-480 exactly.
        std::set<std::uint16_t> reached_tags;
        for (auto const& msg : dict.messages()) {
            for (auto const& fr : dict.message_fields(msg.msg_type)) {
                if (fr.rule != field_presence::NotDeclared) {
                    reached_tags.insert(fr.tag);
                }
            }
        }

        std::size_t store_only_here = 0;
        for (auto const& rf : raw_fields) {
            if (rf.values.empty()) {
                continue;  // not enum-backed at all
            }
            if (dict.enum_values(rf.tag).empty()) {
                continue;  // defensive: shouldn't happen given raw_fields has values
            }
            if (reached_tags.contains(rf.tag)) {
                continue;  // reached by message expansion — T016's default never applies
            }
            ++store_only_here;

            bool const is_multi = (rf.type == "MULTIPLECHARVALUE" || rf.type == "MULTIPLEVALUESTRING" ||
                                    rf.type == "MULTIPLESTRINGVALUE");
            EXPECT_FALSE(is_multi)
                << fname << ": store-only enum-backed tag " << rf.tag << " (" << rf.name
                << ") is declared " << rf.type
                << " — a MULTIPLE* type. T016's `multi_value = false` default for store-only tags is "
                   "an ASSUMPTION that this tag now VIOLATES: it would be checked as a single "
                   "whole-string token instead of being tokenized, silently false-rejecting every "
                   "conformant multi-token value. table_view::set_multi_value() must be extended to "
                   "cover this tag before the assumption is safe again.";
        }

        if (store_only_here > 0) {
            std::cout << "  " << fname << ": " << store_only_here
                      << " store-only enum-backed tag(s) (unreachable via message expansion)\n";
        }
        total_store_only += store_only_here;
    }

    std::cout << "  total store-only enum-backed tags across the nine: " << total_store_only << "\n";
    EXPECT_GT(total_store_only, 0u)
        << "expected at least one store-only enum-backed tag (e.g. MsgType(35), EncryptMethod(98)) — "
           "if this is zero, either the dictionary set changed or the reachability walk above no "
           "longer mirrors production; either way this test's own precondition needs re-examination.";
}

// ============================================================================
// T044 — exact-set blast-radius pin for the QuickFIX-parity CHAR->String
// relaxation (src/dictionary/xml_loader.cpp resolve_field_type(),
// legacy_char_is_string). Asserts, AGAINST THE SHIPPED XML (not a
// hand-maintained list), that the set of dictionaries declaring
// BeginString(8) OR CheckSum(10) as type='CHAR' is EXACTLY {FIX40.xml,
// FIX41.xml} — the two pre-FIX.4.2 dictionaries QuickFIX's
// `m_beginString < "FIX.4.2"` rule targets (DataDictionary.cpp:589-592).
//
// This is an EXACT-SET predicate
// (`[[feedback_completeness_gate_exact_set_not_subset]]`), not a
// subset/presence check: a future dictionary refresh that adds a THIRD
// dictionary typing tag 8/10 as CHAR (which would need the same relaxation
// wired for it) or that removes CHAR from FIX40/FIX41's tag 8/10 (making the
// relaxation dead code) must fail the BUILD, not pass silently. Pins the
// exact blast radius the T044 fix is scoped to.
// ============================================================================
TEST(DictEnumCensus, T044_CharTypedBeginStringOrCheckSumIsExactlyFix40AndFix41) {
    std::cout << "\n=== 075 T044: BeginString(8)/CheckSum(10) type='CHAR' census (nine QuickFIX "
                 "dicts) ===\n";

    std::set<std::string> found;
    for (auto const& fname : kRuntimeDicts) {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
        auto const raw_fields = scan_quickfix_fields(path);
        ASSERT_FALSE(raw_fields.empty()) << fname << ": raw <fields> scan found nothing";

        for (auto const& rf : raw_fields) {
            if ((rf.tag == 8 || rf.tag == 10) && rf.type == "CHAR") {
                found.insert(std::string{fname});
                std::cout << "  " << fname << ": tag " << rf.tag << " (" << rf.name
                          << ") declared type='CHAR'\n";
            }
        }
    }

    std::set<std::string> const expected{"FIX40.xml", "FIX41.xml"};
    EXPECT_EQ(found, expected)
        << "the set of shipped dictionaries declaring BeginString(8) or CheckSum(10) as "
           "type='CHAR' must be EXACTLY {FIX40.xml, FIX41.xml} — xml_loader.cpp's "
           "legacy_char_is_string relaxation (T044, QuickFIX DataDictionary.cpp:589-592 parity) "
           "is scoped to precisely this set; a drift here means either a new dictionary needs "
           "the same relaxation wired for it, or FIX40/FIX41 no longer need it (the relaxation "
           "would be dead code) — either way, re-examine, don't silently absorb.";
}
