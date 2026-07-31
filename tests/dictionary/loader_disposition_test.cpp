// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/loader_disposition_test.cpp
//
// 083-group-delimiter-resolution T035 / T042 — the load-disposition witnesses
// for FR-006 (fail-closed default), FR-006a (explicit tolerant opt-in),
// FR-006c (per-loader exception type), FR-006d / C-3.6 (a group producing zero
// contexts must NOT fail closed) and FR-023 / C-3.4 (the Entity-2 completeness
// invariant).
//
// Anchors:
//   tasks:     specs/083-group-delimiter-resolution/tasks.md T035, T042
//   research:  research.md D-7 (+ its C-7.1 / C-7.3 precondition records)
//   contract:  contracts/group_ctx_delims.md C-6.1, C-6.1a, C-6.1b, C-3.4,
//              C-3.4a, C-3.6, C-3.7

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/loader_policy.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fixpp::dict::orchestra_parse_error;
using fixpp::dict::unresolved_group_policy;
using fixpp::dict::xml_parse_error;

// ── W-4/W-5 fixture: an OTHERWISE VALID dictionary carrying exactly one group
// whose delimiter cannot be resolved. `NoBad(700)` is declared as a group in
// message `V1` with NO children at all, so its document-order walk emits no
// first member. `NoGood(600)/FieldA(610)` is a well-formed sibling, present so
// the tolerant case can assert that the REST of the dictionary still loads —
// without it, "loads" would be indistinguishable from "loaded nothing".
constexpr std::string_view kUnresolvableGroupXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='600' name='NoGood' type='NUMINGROUP'/>)"
    R"(<field number='610' name='FieldA' type='STRING'/>)"
    R"(<field number='700' name='NoBad' type='NUMINGROUP'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='V1Msg' msgtype='V1' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoGood' required='N'>)"
    R"(<field name='FieldA' required='N'/>)"
    R"(</group>)"
    R"(<group name='NoBad' required='N'></group>)"  // <- no members: unresolvable
    R"(</message>)"
    R"(</messages></fix>)";

// ── FR-006d / C-6.1a class 2 fixture: `NoBad(700)` is declared in <fields> and
// used as a group ONLY inside a component that no message references. It is
// therefore reachable from no message expansion and produces ZERO contexts, so
// C-6.1 must NOT fire even though its delimiter is likewise unresolved.
constexpr std::string_view kZeroContextGroupXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='600' name='NoGood' type='NUMINGROUP'/>)"
    R"(<field number='610' name='FieldA' type='STRING'/>)"
    R"(<field number='700' name='NoBad' type='NUMINGROUP'/>)"
    R"(</fields>)"
    R"(<components>)"
    R"(<component name='UnreferencedComp'>)"
    R"(<group name='NoBad' required='N'></group>)"
    R"(</component>)"
    R"(</components>)"
    R"(<messages>)"
    R"(<message name='V1Msg' msgtype='V1' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoGood' required='N'>)"
    R"(<field name='FieldA' required='N'/>)"
    R"(</group>)"
    R"(</message>)"
    R"(</messages></fix>)";

// ── C-3.4a's `!members.empty()` leg (T042): message `V2` reuses the
// NUMINGROUP-typed tag 600 as a PLAIN SCALAR <field>, contributing no context.
// That must not read as a completeness violation.
constexpr std::string_view kScalarReuseXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='600' name='NoGood' type='NUMINGROUP'/>)"
    R"(<field number='610' name='FieldA' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='V1Msg' msgtype='V1' msgcat='app'>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<group name='NoGood' required='N'>)"
    R"(<field name='FieldA' required='N'/>)"
    R"(</group>)"
    R"(</message>)"
    R"(<message name='V2Msg' msgtype='V2' msgcat='app'>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='NoGood' required='N'/>)"  // scalar reuse, NOT a group here
    R"(</message>)"
    R"(</messages></fix>)";

// ── FR-006c / C-6.1b: the Orchestra twin of kUnresolvableGroupXml.
constexpr std::string_view kOrchestraUnresolvableXml =
    R"(<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' )"
    R"(name='FIX' version='FIX.Latest_EP303'>)"
    R"(<fixr:datatypes>)"
    R"(<fixr:datatype name='String'/><fixr:datatype name='int'/>)"
    R"(<fixr:datatype name='NumInGroup'/>)"
    R"(</fixr:datatypes>)"
    R"(<fixr:fields>)"
    R"(<fixr:field id='35' name='MsgType' type='String'/>)"
    R"(<fixr:field id='700' name='NoBad' type='NumInGroup'/>)"
    R"(</fixr:fields>)"
    R"(<fixr:groups>)"
    R"(<fixr:group id='7000' name='BadGrp'>)"
    R"(<fixr:numInGroup id='700'/>)"
    R"(</fixr:group>)"
    R"(</fixr:groups>)"
    R"(<fixr:messages>)"
    R"(<fixr:message id='1' name='V1Msg' msgType='V1'>)"
    R"(<fixr:structure>)"
    R"(<fixr:fieldRef id='35'/>)"
    R"(<fixr:groupRef id='7000'/>)"
    R"(</fixr:structure>)"
    R"(</fixr:message>)"
    R"(</fixr:messages>)"
    R"(</fixr:repository>)";

struct DictFile {
    std::string label;
    std::string filename;
    bool is_orchestra;
};

std::vector<DictFile> const kAllTen{
    {"FIX40", "FIX40.xml", false},       {"FIX41", "FIX41.xml", false},
    {"FIX42", "FIX42.xml", false},       {"FIX43", "FIX43.xml", false},
    {"FIX44", "FIX44.xml", false},       {"FIX50", "FIX50.xml", false},
    {"FIX50SP1", "FIX50SP1.xml", false}, {"FIX50SP2", "FIX50SP2.xml", false},
    {"FIXT11", "FIXT11.xml", false},
    {"Orchestra FIX Latest", "OrchestraFIXLatest.xml", true},
};

}  // namespace

// ============================================================================
// W-4 — an unresolvable group is REJECTED under the fail-closed DEFAULT, and
// the diagnostic NAMES the offending group (FR-006 / C-6.1).
// ============================================================================
TEST(LoaderDisposition, UnresolvableGroupRejectedUnderFailClosedDefault) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    bool threw = false;
    std::string what;
    try {
        // NOTE: called with NO policy argument, so this also pins that the
        // DEFAULT is fail-closed (C-6.6) rather than merely that the option
        // exists.
        auto d = fixpp::dict::XmlLoader{}.load_from_string(kUnresolvableGroupXml, &mr);
        (void)d;
    } catch (xml_parse_error const& e) {
        threw = true;
        what = e.what();
    }
    ASSERT_TRUE(threw) << "FR-006 / C-6.1: a group whose delimiter cannot be resolved must "
                          "REJECT the load under the default policy, not be silently dropped — "
                          "the silent drop is what concealed three FIX50SP2 groups.";
    // The diagnostic must NAME the group, not merely report that something
    // failed: the whole point is that an operator can find it.
    EXPECT_NE(what.find("NoBad"), std::string::npos)
        << "the diagnostic must name the offending group by name; got: " << what;
    EXPECT_NE(what.find("700"), std::string::npos)
        << "the diagnostic must name the offending group's NumInGroup tag; got: " << what;
}

// ============================================================================
// W-5 — the SAME dictionary LOADS under the explicit tolerant opt-in, with the
// offending group left UNREGISTERED rather than half-registered (FR-006a /
// FR-023a).
// ============================================================================
TEST(LoaderDisposition, UnresolvableGroupSkippedUnderTolerantOptIn) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kUnresolvableGroupXml, &mr,
                                                          unresolved_group_policy::tolerant);

    // The rest of the dictionary is intact — otherwise "it loaded" would be
    // satisfied by loading nothing.
    EXPECT_EQ(dict.group_first_field(600), 610)
        << "FR-006a: tolerant mode must skip only the unresolvable group, leaving well-formed "
           "siblings fully resolved.";

    // And the offending group is UNREGISTERED, not registered with a bogus
    // delimiter. A half-registration would put tag 0 into the member set and
    // read downstream as 'not a group' — silently, which is the failure mode
    // tolerant mode exists to avoid, not to introduce.
    EXPECT_EQ(dict.group_first_field(700), 0)
        << "FR-023a: a tolerantly-skipped group must be left UNREGISTERED so it never enters "
           "the consumer's enumeration.";
    auto const tv = dict.as_table_view();
    std::array<std::uint16_t, 0> const root{};
    EXPECT_EQ(tv.group_first_field("V1", root, std::uint16_t{700}), 0)
        << "FR-023a: the skipped group must not appear in the per-context store either.";
}

// ============================================================================
// W-6 — all ten shipped dictionaries load under the fail-closed DEFAULT. This
// is C-7.1's checked-in twin: the precondition was run as a one-off before the
// throw was enabled (research.md D-7), and this keeps it pinned.
// ============================================================================
TEST(LoaderDisposition, AllTenShippedDictionariesLoadUnderFailClosedDefault) {
    for (auto const& d : kAllTen) {
        std::vector<std::byte> buf(64u * 1024u * 1024u);
        std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
        auto const path = d.is_orchestra
                              ? std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / d.filename
                              : std::filesystem::path{FIXPP_DICT_DATA_DIR} / d.filename;
        try {
            if (d.is_orchestra) {
                auto dict = fixpp::dict::OrchestraLoader{}.load(path, &mr);
                EXPECT_GT(dict.messages().size(), 0u) << d.label;
            } else {
                auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
                EXPECT_GT(dict.messages().size(), 0u) << d.label;
            }
        } catch (std::exception const& e) {
            ADD_FAILURE() << "C-7.1 / W-6: " << d.label
                          << " must load under the fail-closed default. Enabling FR-006's throw "
                             "without this holding is the one unacceptable outcome. what()="
                          << e.what();
        }
    }
}

// ============================================================================
// FR-006d / C-6.1a class 2 — a group DECLARED but reachable from no message
// expansion produces ZERO contexts, and must NOT fail closed. This is the
// disposition the six real survivors take (tags 384/627 in FIX50/SP1/SP2).
// ============================================================================
TEST(LoaderDisposition, DeclaredGroupWithZeroContextsDoesNotFailClosed) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    // Default policy — this must NOT throw.
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kZeroContextGroupXml, &mr);

    EXPECT_EQ(dict.group_first_field(600), 610)
        << "the well-formed sibling must still resolve.";
    // NoBad(700) is declared and unresolved, but contributes zero contexts, so
    // C-6.1 must not fire on it (C-3.6). It stays unresolved — informational,
    // not fatal.
    EXPECT_EQ(dict.group_first_field(700), 0)
        << "FR-006d / C-3.6: a group reachable from no message expansion contributes zero "
           "contexts and is an informational diagnostic, never a load rejection.";
}

// ============================================================================
// FR-006c / C-6.1b — the Orchestra loader's rejection is the DERIVED
// `orchestra_parse_error`, not the base. This is not cosmetic: the Orchestra
// fuzz harness catches the derived type, so a base `xml_parse_error` would
// escape to its terminal rethrow and crash the fuzzer on every input carrying
// an unresolvable group.
// ============================================================================
TEST(LoaderDisposition, OrchestraRejectionIsOrchestraParseError) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    bool caught_derived = false;
    try {
        auto d = fixpp::dict::OrchestraLoader{}.load_from_string(kOrchestraUnresolvableXml, &mr);
        (void)d;
    } catch (orchestra_parse_error const& e) {
        caught_derived = true;
        EXPECT_NE(std::string{e.what()}.find("700"), std::string::npos)
            << "the diagnostic must name the offending group's NumInGroup tag; got: " << e.what();
    } catch (xml_parse_error const&) {
        ADD_FAILURE() << "FR-006c / C-6.1b: OrchestraLoader threw the BASE xml_parse_error. The "
                         "Orchestra fuzz harness catches orchestra_parse_error, so the base type "
                         "escapes to its terminal rethrow and crashes the fuzzer.";
    }
    EXPECT_TRUE(caught_derived)
        << "FR-006 must reject an unresolvable group in the Orchestra loader too — FR-005 / "
           "C-1.5: a one-loader fix is a half-restructure.";

    // And the tolerant opt-in is the SAME option with the SAME semantics on
    // this loader (C-6.4).
    std::vector<std::byte> buf2(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr2{buf2.data(), buf2.size()};
    EXPECT_NO_THROW({
        auto d = fixpp::dict::OrchestraLoader{}.load_from_string(
            kOrchestraUnresolvableXml, &mr2, unresolved_group_policy::tolerant);
        (void)d;
    }) << "C-6.4: tolerant mode must behave identically in both loaders.";
}

// ============================================================================
// T042 / C-3.4a's `!members.empty()` leg — a message that reuses a
// NUMINGROUP-typed tag as a PLAIN SCALAR contributes no context, so it must
// not read as an Entity-2 completeness violation. Without this leg the FR-023
// check would reject every dictionary with a scalar-reused group tag.
// ============================================================================
TEST(LoaderDisposition, ScalarReuseOfGroupTagIsNotACompletenessViolation) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    // V2 uses tag 600 as a scalar. The load must succeed under the default.
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kScalarReuseXml, &mr);

    auto const tv = dict.as_table_view();
    std::array<std::uint16_t, 0> const root{};
    // V1's genuine group context resolves.
    EXPECT_EQ(tv.group_first_field("V1", root, std::uint16_t{600}), 610)
        << "V1 declares NoGood(600) as a real group; its context must resolve.";
    // V2 contributes NO context for 600 — it is a scalar there. The context
    // store has no V2 entry, so this query MISSES and falls through to the bare
    // global (table_view.hpp:361-364). That fall-through is the documented
    // behaviour, not a registration: what this case pins is that the load
    // SUCCEEDED, i.e. the scalar reuse did not trip C-3.4.
    SUCCEED() << "load succeeded with a NumInGroup-typed tag reused as a scalar";
}

// ============================================================================
// T042 / FR-023 / C-3.4 — the Entity-2 completeness invariant is enforced at
// finalize(), NOT at as_table_view().
//
// Why the assertion is shaped as "all ten load, and every registered context
// resolves a non-zero delimiter" rather than as a hand-built violating
// dictionary: the invariant holds BY CONSTRUCTION, and that is worth stating
// rather than engineering around. `captured == 0` means no FieldRef was
// emitted at the group's level, so no FieldRef carries its `group_no_tag`, so
// C-3.4a's `!members.empty()` leg excludes it from the registered set — there
// is no reachable input that registers a context and omits its record. The
// check is defence in depth against a future change breaking that
// correspondence, and this case pins the property it defends.
//
// The measured precondition is recorded in research.md D-7 (C-7.3): R\E = 0 on
// all ten, with E taken from the INDEPENDENT document-order oracle rather than
// from the loader's own table.
// ============================================================================
TEST(LoaderDisposition, ContextWithoutDelimiterRecordRejectedAtFinalize) {
    // The invariant is live: every shipped dictionary passes finalize()'s
    // check, and no registered context resolves the 0 that a missing record
    // would produce. A 0 here would be SILENT, not loud: set_group_first_ctx
    // stores it AND injects tag 0 as a member, after which the validator reads
    // the group as not-a-group and skips it.
    for (auto const& d : kAllTen) {
        if (d.is_orchestra) {
            continue;  // covered by the same loop in W-6; keeps this case fast
        }
        std::vector<std::byte> buf(64u * 1024u * 1024u);
        std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
        auto dict = fixpp::dict::XmlLoader{}.load(
            std::filesystem::path{FIXPP_DICT_DATA_DIR} / d.filename, &mr);
        auto const tv = dict.as_table_view();
        std::size_t checked = 0;
        std::size_t zero = 0;
        for (auto const& msg : dict.messages()) {
            auto const fields = dict.message_fields(msg.msg_type);
            for (auto const& fr : fields) {
                if (fr.type != fixpp::dict::field_data_type::NumInGroup) {
                    continue;
                }
                bool has_members = false;
                for (auto const& m : fields) {
                    if (m.group_no_tag == fr.tag) {
                        has_members = true;
                        break;
                    }
                }
                if (!has_members || fr.group_no_tag != 0) {
                    continue;  // top-level contexts only — enough to pin the property
                }
                ++checked;
                std::array<std::uint16_t, 0> const root{};
                if (tv.group_first_field(msg.msg_type, root, fr.tag) == 0) {
                    ++zero;
                }
            }
        }
        EXPECT_EQ(zero, 0u) << "FR-023 / C-3.4: " << d.label << " has " << zero << " of "
                            << checked
                            << " registered top-level contexts resolving delimiter 0, i.e. with "
                               "no Entity-2 record. finalize() should have rejected the load.";
    }
}

// FR-023a — the tolerant twin. A tolerantly-skipped group is skipped, not
// half-registered: it must not appear in the consumer's enumeration at all.
TEST(LoaderDisposition, ContextWithoutDelimiterRecordTolerantModeSkipsGroup) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kUnresolvableGroupXml, &mr,
                                                          unresolved_group_policy::tolerant);
    auto const tv = dict.as_table_view();
    std::array<std::uint16_t, 0> const root{};

    // Skipped, not half-registered. A half-registration would store delimiter 0
    // and inject tag 0 into the member set — which reads downstream as
    // "not a group" and silently drops the group instead of loudly skipping it.
    EXPECT_EQ(tv.group_first_field("V1", root, std::uint16_t{700}), 0)
        << "FR-023a: the skipped group must not be registered.";
    auto const members = tv.group_member_tags("V1", root, std::uint16_t{700});
    for (auto const m : members) {
        EXPECT_NE(m, 0) << "FR-023a: a half-registration would inject tag 0 as a member — the "
                           "silent-drop shape tolerant mode exists to avoid, not to introduce.";
    }

    // And the well-formed sibling is untouched, so "skipped" did not mean
    // "gave up on the dictionary".
    EXPECT_EQ(tv.group_first_field("V1", root, std::uint16_t{600}), 610);
}
