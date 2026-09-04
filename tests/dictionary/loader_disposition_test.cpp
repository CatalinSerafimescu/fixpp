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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Gate B r1 F1 (fixpp#216 P1-1): the direct `find_incomplete_group_context()`
// unit test and the FR-023 rejection-witness test seam, same pattern as
// tests/capi/capi_group_delimiter_ctx_test.cpp's W-11a include.
#include "dictionary_internal.hpp"

namespace {

using fixpp::dict::group_delimiter_collision_error;
using fixpp::dict::orchestra_parse_error;
using fixpp::dict::unresolved_group_policy;
using fixpp::dict::xml_parse_error;

// Gate B r1 F1: RAII guard for the FR-023 test seam
// (`detail::set_force_incomplete_group_context_for_testing`). Declared BEFORE
// any `try` in each user so a thrown exception still resets it — the flag is
// a process-global shared by every case in this 20-file bucket binary
// (dictionary_pure_tests).
struct ForceIncompleteGroupContextGuard {
    ForceIncompleteGroupContextGuard() {
        fixpp::dict::detail::set_force_incomplete_group_context_for_testing(true);
    }
    ~ForceIncompleteGroupContextGuard() {
        fixpp::dict::detail::set_force_incomplete_group_context_for_testing(false);
    }
};

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

// ── Gate B r1 F1 (fixpp#216): a WELL-FORMED Orchestra twin of
// kScalarReuseXml's `V1` message — group `NoGood(600)` has a real member
// `FieldA(610)`, so it loads and registers a context under the default policy
// with no seam involved. Used with `ForceIncompleteGroupContextGuard` to
// exercise the FR-023 throw itself (as `orchestra_parse_error`) on a
// dictionary that would otherwise load cleanly.
constexpr std::string_view kOrchestraValidGroupXml =
    R"(<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' )"
    R"(name='FIX' version='FIX.Latest_EP303'>)"
    R"(<fixr:datatypes>)"
    R"(<fixr:datatype name='String'/><fixr:datatype name='int'/>)"
    R"(<fixr:datatype name='NumInGroup'/>)"
    R"(</fixr:datatypes>)"
    R"(<fixr:fields>)"
    R"(<fixr:field id='35' name='MsgType' type='String'/>)"
    R"(<fixr:field id='600' name='NoGood' type='NumInGroup'/>)"
    R"(<fixr:field id='610' name='FieldA' type='String'/>)"
    R"(</fixr:fields>)"
    R"(<fixr:groups>)"
    R"(<fixr:group id='6000' name='GoodGrp'>)"
    R"(<fixr:numInGroup id='600'/>)"
    R"(<fixr:fieldRef id='610'/>)"
    R"(</fixr:group>)"
    R"(</fixr:groups>)"
    R"(<fixr:messages>)"
    R"(<fixr:message id='1' name='V1Msg' msgType='V1'>)"
    R"(<fixr:structure>)"
    R"(<fixr:fieldRef id='35'/>)"
    R"(<fixr:groupRef id='6000'/>)"
    R"(</fixr:structure>)"
    R"(</fixr:message>)"
    R"(</fixr:messages>)"
    R"(</fixr:repository>)";

// ── Gate B r1 F1 (fixpp#261 PR review) — the FIX 4.0/4.1/4.2 legacy shape
// (L-063-1): the group count tag NoGood(600) is declared type='INT', not
// NUMINGROUP. Otherwise identical to kOrchestraValidGroupXml/
// kUnresolvableGroupXml's well-formed sibling — a real group with a real
// member, FieldA(610). Used with ForceIncompleteGroupContextGuard to prove
// the FR-023 completeness sweep now examines this population, which the
// datatype-gated predicate used to skip entirely.
constexpr std::string_view kIntTypedValidGroupXml =
    R"(<fix type='FIX' major='4' minor='2' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='600' name='NoGood' type='INT'/>)"
    R"(<field number='610' name='FieldA' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='V1Msg' msgtype='V1' msgcat='app'>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<group name='NoGood' required='N'>)"
    R"(<field name='FieldA' required='N'/>)"
    R"(</group>)"
    R"(</message>)"
    R"(</messages></fix>)";

// ── Gate B r1 F1 (fixpp#261 PR review) — the Orchestra twin of
// kIntTypedValidGroupXml: NoGood(600) is declared type='int', not
// 'NumInGroup'.
constexpr std::string_view kOrchestraIntTypedValidGroupXml =
    R"(<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' )"
    R"(name='FIX' version='FIX.Latest_EP303'>)"
    R"(<fixr:datatypes>)"
    R"(<fixr:datatype name='String'/><fixr:datatype name='int'/>)"
    R"(</fixr:datatypes>)"
    R"(<fixr:fields>)"
    R"(<fixr:field id='35' name='MsgType' type='String'/>)"
    R"(<fixr:field id='600' name='NoGood' type='int'/>)"
    R"(<fixr:field id='610' name='FieldA' type='String'/>)"
    R"(</fixr:fields>)"
    R"(<fixr:groups>)"
    R"(<fixr:group id='6000' name='GoodGrp'>)"
    R"(<fixr:numInGroup id='600'/>)"
    R"(<fixr:fieldRef id='610'/>)"
    R"(</fixr:group>)"
    R"(</fixr:groups>)"
    R"(<fixr:messages>)"
    R"(<fixr:message id='1' name='V1Msg' msgType='V1'>)"
    R"(<fixr:structure>)"
    R"(<fixr:fieldRef id='35'/>)"
    R"(<fixr:groupRef id='6000'/>)"
    R"(</fixr:structure>)"
    R"(</fixr:message>)"
    R"(</fixr:messages>)"
    R"(</fixr:repository>)";

// ── Gate B r1 F1 (fixpp#261 PR review) — the Int-typed twin of
// kScalarReuseXml: V1 declares NoGood(600) [type='INT'] as a real group with
// member FieldA(610); V2 reuses tag 600 as a plain scalar <field>. Pins that
// C-3.4a's `!members.empty()` exclusion still holds once the sweep's
// candidate source is the DICTIONARY-WIDE structural tag set rather than a
// per-message type test — the false-rejection widening the fix queue warns
// against would flag V2 even though it contributes no context.
constexpr std::string_view kIntTypedScalarReuseXml =
    R"(<fix type='FIX' major='4' minor='2' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='600' name='NoGood' type='INT'/>)"
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

// ── fixpp#264 fixture: a COMPLETE dictionary whose deepest group context has an
// ancestor chain one longer than `kMaxGroupContextDepth`. Past the clamp is the
// only region where the FR-023 completeness probe and the registration path can
// build different keys for the same context, and one-past is the minimum that
// reaches it.
//
// Shape: `NoA<i>` count tags nested one inside the next, each declaring its own
// scalar `D<i>` BEFORE its nested child group so document order gives every one
// of them a resolvable first member. That ordering is load-bearing: a group
// whose first emission is its child group's count tag still resolves, but a
// group with NO scalar and no child at all would trip xml_loader's FR-006
// `captured == 0` disposition and the load would be rejected for an unrelated
// reason — indistinguishable, from the outside, from the rejection this fixture
// exists to catch. `NoLeaf` sits inside the innermost `NoA<i>` with its own
// member `LeafF`, so ITS parent path — ancestors only, own `no_tag` excluded
// (C-1.3 / Entity 1) — is the full `kDeepChainLen` chain.
//
// Nothing here is malformed: every group has a member and every field is
// declared, so a rejection of this input is a false rejection of valid input.
// Derived, not hardcoded: the fixture must sit exactly one level past the
// clamp, which is the minimal chain length at which two clamp orders can
// disagree. A literal here would silently stop reproducing #264 if K grew.
inline constexpr std::uint16_t kDeepChainLen =
    static_cast<std::uint16_t>(fixpp::dict::kMaxGroupContextDepth + 1);
static_assert(kDeepChainLen > fixpp::dict::kMaxGroupContextDepth,
              "the fixture must nest PAST the clamp or it pins nothing");
inline constexpr std::uint16_t kDeepCountBase = 900;   // NoA<i> = 900 + i
inline constexpr std::uint16_t kDeepDelimBase = 1000;  // D<i>   = 1000 + i
inline constexpr std::uint16_t kDeepLeafNoTag = 9000;
inline constexpr std::uint16_t kDeepLeafDelim = 9001;

[[nodiscard]] std::string make_deep_nesting_xml() {
    std::string xml = R"(<fix type='FIX' major='4' minor='4' servicepack='0'><fields>)"
                      R"(<field number='8' name='BeginString' type='STRING'/>)"
                      R"(<field number='9' name='BodyLength' type='INT'/>)"
                      R"(<field number='10' name='CheckSum' type='STRING'/>)"
                      R"(<field number='35' name='MsgType' type='STRING'/>)";
    for (std::uint16_t i = 1; i <= kDeepChainLen; ++i) {
        xml += "<field number='" + std::to_string(kDeepCountBase + i) + "' name='NoA" +
               std::to_string(i) + "' type='NUMINGROUP'/>";
        xml += "<field number='" + std::to_string(kDeepDelimBase + i) + "' name='D" +
               std::to_string(i) + "' type='STRING'/>";
    }
    xml +=
        "<field number='" + std::to_string(kDeepLeafNoTag) + "' name='NoLeaf' type='NUMINGROUP'/>";
    xml += "<field number='" + std::to_string(kDeepLeafDelim) + "' name='LeafF' type='STRING'/>";
    xml += R"(</fields><messages><message name='DeepMsg' msgtype='M' msgcat='app'>)"
           R"(<field name='MsgType' required='N'/>)";
    for (std::uint16_t i = 1; i <= kDeepChainLen; ++i) {
        xml += "<group name='NoA" + std::to_string(i) + "' required='N'>";
        xml += "<field name='D" + std::to_string(i) + "' required='N'/>";
    }
    xml += "<group name='NoLeaf' required='N'><field name='LeafF' required='N'/></group>";
    for (std::uint16_t i = 0; i < kDeepChainLen; ++i) {
        xml += "</group>";
    }
    xml += R"(</message></messages></fix>)";
    return xml;
}

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
// Gate B r1 F1 (fixpp#216 P1-1) — a DIRECT unit test of
// `detail::find_incomplete_group_context()` on a hand-built handle with one
// `group_ctx_delim_pool_` record simply never populated. Needs no loader, no
// XML, and no test seam: it covers the detector's found-branch
// (dictionary_internal.hpp:265/:534) in complete isolation from whether the
// loaders' throw is reachable.
// ============================================================================
TEST(LoaderDisposition, FindIncompleteGroupContextDetectsMissingRecord) {
    std::array<std::byte, 4096> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    fixpp::dict::detail::dict_metadata_handle h{&mr};

    // One message: NoGood(600) is a registered NumInGroup context with one
    // member, FieldA(610) — exactly C-3.4a's "!members.empty()" registration
    // predicate — but NO delimiter record is added to the pool.
    fixpp::dict::FieldRef group_fr{};
    group_fr.tag = 600;
    group_fr.type = fixpp::dict::field_data_type::NumInGroup;
    group_fr.group_no_tag = 0;

    fixpp::dict::FieldRef member_fr{};
    member_fr.tag = 610;
    member_fr.type = fixpp::dict::field_data_type::String;
    member_fr.group_no_tag = 600;

    h.fields_.push_back(group_fr);
    h.fields_.push_back(member_fr);
    h.per_msg_field_offsets_.push_back({.start = 0, .count = 2});
    // The violation under test: zero delimiter records for this message.
    h.per_msg_group_ctx_delim_offsets_.push_back({.start = 0, .count = 0});

    std::array<std::uint16_t, 1> const structural_tags{600};
    auto const bad = fixpp::dict::detail::find_incomplete_group_context(h, structural_tags);
    ASSERT_TRUE(bad.has_value())
        << "a registered context (NoGood/600, with member FieldA/610) that has no delimiter "
           "record must be detected as the FR-023 / C-3.4 violation it is.";
    EXPECT_EQ(bad->first, 0u);
    EXPECT_EQ(bad->second, 600);
}

// ============================================================================
// Gate B r1 F1 (fixpp#261 PR review) — the `field_data_type::Int` twin of
// FindIncompleteGroupContextDetectsMissingRecord above. Proves the datatype-
// gated predicate (`fr.type == NumInGroup`) misses exactly the population 082
// added: legacy FIX 4.0/4.1/4.2 dialects whose `<group>` count fields are
// INT-typed. Identical fixture, ONLY the type differs.
// ============================================================================
TEST(LoaderDisposition, FindIncompleteGroupContextDetectsMissingRecordIntTyped) {
    std::array<std::byte, 4096> buf{};
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    fixpp::dict::detail::dict_metadata_handle h{&mr};

    // Same as the NumInGroup-typed fixture above, except the count tag's own
    // FieldRef.type is Int — matching how FIX 4.0/4.1/4.2 declare <group>
    // count fields (L-063-1).
    fixpp::dict::FieldRef group_fr{};
    group_fr.tag = 600;
    group_fr.type = fixpp::dict::field_data_type::Int;
    group_fr.group_no_tag = 0;

    fixpp::dict::FieldRef member_fr{};
    member_fr.tag = 610;
    member_fr.type = fixpp::dict::field_data_type::String;
    member_fr.group_no_tag = 600;

    h.fields_.push_back(group_fr);
    h.fields_.push_back(member_fr);
    h.per_msg_field_offsets_.push_back({.start = 0, .count = 2});
    // The violation under test: zero delimiter records for this message.
    h.per_msg_group_ctx_delim_offsets_.push_back({.start = 0, .count = 0});

    std::array<std::uint16_t, 1> const structural_tags{600};
    auto const bad = fixpp::dict::detail::find_incomplete_group_context(h, structural_tags);
    ASSERT_TRUE(bad.has_value())
        << "an Int-typed registered context (NoGood/600, with member FieldA/610) that has no "
           "delimiter record must be detected as the FR-023 / C-3.4 violation it is, exactly as "
           "the NumInGroup-typed case above — the sweep's candidate source is structural "
           "(caller-supplied group tags), not FieldRef.type. RED before the Gate B r1 F1 fix "
           "(proven: bad.has_value() == false, see the fix's commit message for the transcript).";
    EXPECT_EQ(bad->first, 0u);
    EXPECT_EQ(bad->second, 600);
}

// ============================================================================
// Gate B r1 F1 (fixpp#216 P1-1) — the FR-023 / C-3.4 REJECTION witness itself:
// constructs a dictionary reaching the invariant and asserts the load is
// rejected (contracts/group_ctx_delims.md C-3.4's Witnesses clause, verbatim).
//
// C-7.3's zero-population measurement (research.md D-7) means no ORDINARY
// dialect reaches this throw, so `ForceIncompleteGroupContextGuard` is used to
// truncate a real, otherwise-valid dictionary's delimiter run to zero records
// immediately before the check both loaders run — `find_incomplete_group_context`
// then genuinely misses and the throw fires for the real reason it exists to
// catch (proven by the mutation test below: deleting the detector call turns
// this RED).
//
// The assertion discriminates the FR-023 throw from 072's
// `group_delimiter_collision_error` (also an `xml_parse_error`, `error.hpp:67`)
// by BOTH catch type (caught separately, ADD_FAILURE if it fires instead) AND
// the FR-023 message text — never the base class alone
// (`feedback_witness_asserts_named_postcondition_not_proxy`).
// ============================================================================
TEST(LoaderDisposition, ContextWithoutDelimiterRecordRejectedAtFinalize) {
    ForceIncompleteGroupContextGuard const guard;

    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    bool threw = false;
    std::string what;
    try {
        // kScalarReuseXml's V1 message is otherwise valid: NoGood(600) has a
        // real member, FieldA(610). Messages sort bytewise ("V1" < "V2"), so
        // message index 0 — the one the seam truncates — is V1.
        auto d = fixpp::dict::XmlLoader{}.load_from_string(kScalarReuseXml, &mr);
        (void)d;
    } catch (group_delimiter_collision_error const& e) {
        ADD_FAILURE() << "the wrong guard fired (072's nested/parent delimiter collision check, "
                         "not the FR-023 completeness invariant this test targets): "
                      << e.what();
        return;
    } catch (xml_parse_error const& e) {
        threw = true;
        what = e.what();
    }
    ASSERT_TRUE(threw) << "FR-023 / C-3.4: a registered context with no delimiter record must "
                          "reject the load.";
    EXPECT_NE(what.find("no per-context delimiter record (FR-023 completeness invariant)"),
              std::string::npos)
        << "the diagnostic must name the FR-023 completeness invariant specifically; got: "
        << what;
    EXPECT_NE(what.find("600"), std::string::npos)
        << "the diagnostic must name the offending group's NumInGroup tag; got: " << what;
    EXPECT_NE(what.find("V1"), std::string::npos)
        << "the diagnostic must name the offending message; got: " << what;
}

// Gate B r1 F1 — the Orchestra twin (FR-006c: the Orchestra loader's own
// exception type, discriminated by catch type). Reuses the Orchestra fixture
// idiom already established at `OrchestraRejectionIsOrchestraParseError`
// above.
TEST(LoaderDisposition, ContextWithoutDelimiterRecordRejectedAtFinalizeOrchestra) {
    ForceIncompleteGroupContextGuard const guard;

    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    bool caught_derived = false;
    std::string what;
    try {
        auto d = fixpp::dict::OrchestraLoader{}.load_from_string(kOrchestraValidGroupXml, &mr);
        (void)d;
    } catch (orchestra_parse_error const& e) {
        caught_derived = true;
        what = e.what();
    } catch (xml_parse_error const& e) {
        ADD_FAILURE() << "FR-006c / C-6.1b: OrchestraLoader threw the BASE xml_parse_error, not "
                         "the derived orchestra_parse_error; got: "
                      << e.what();
        return;
    }
    ASSERT_TRUE(caught_derived) << "FR-023 / C-3.4 must reject in the Orchestra loader too, as "
                                   "its own orchestra_parse_error (FR-006c).";
    EXPECT_NE(what.find("no per-context delimiter record (FR-023 completeness invariant)"),
              std::string::npos)
        << "the diagnostic must name the FR-023 completeness invariant specifically; got: "
        << what;
    EXPECT_NE(what.find("600"), std::string::npos)
        << "the diagnostic must name the offending group's NumInGroup tag; got: " << what;
}

// ============================================================================
// Gate B r1 F1 (fixpp#261 PR review) — the loader-seam twin of
// ContextWithoutDelimiterRecordRejectedAtFinalize, on an INT-typed group
// (kIntTypedValidGroupXml). Before the fix, the FR-023 sweep's `fr.type ==
// NumInGroup` gate meant this exact fixture, forced incomplete by the SAME
// guard, LOADED SUCCESSFULLY — the guard truncated the delimiter run, but the
// detector never even looked at tag 600 because its FieldRef.type is Int.
// That silent gap is exactly what F-1 reports; this proves it is closed.
// ============================================================================
TEST(LoaderDisposition, ContextWithoutDelimiterRecordRejectedAtFinalizeIntTyped) {
    ForceIncompleteGroupContextGuard const guard;

    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    bool threw = false;
    std::string what;
    try {
        auto d = fixpp::dict::XmlLoader{}.load_from_string(kIntTypedValidGroupXml, &mr);
        (void)d;
    } catch (group_delimiter_collision_error const& e) {
        ADD_FAILURE() << "the wrong guard fired (072's nested/parent delimiter collision check, "
                         "not the FR-023 completeness invariant this test targets): "
                      << e.what();
        return;
    } catch (xml_parse_error const& e) {
        threw = true;
        what = e.what();
    }
    ASSERT_TRUE(threw) << "FR-023 / C-3.4: an Int-typed registered context with no delimiter "
                          "record must reject the load — the sweep's candidate source is "
                          "structural (as_table_view()'s own predicate), not FieldRef.type.";
    EXPECT_NE(what.find("no per-context delimiter record (FR-023 completeness invariant)"),
              std::string::npos)
        << "the diagnostic must name the FR-023 completeness invariant specifically; got: "
        << what;
    EXPECT_NE(what.find("600"), std::string::npos)
        << "the diagnostic must name the offending group's count tag; got: " << what;
    EXPECT_NE(what.find("V1"), std::string::npos)
        << "the diagnostic must name the offending message; got: " << what;
}

// Gate B r1 F1 — the Orchestra twin (FR-006c), on kOrchestraIntTypedValidGroupXml.
TEST(LoaderDisposition, ContextWithoutDelimiterRecordRejectedAtFinalizeIntTypedOrchestra) {
    ForceIncompleteGroupContextGuard const guard;

    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    bool caught_derived = false;
    std::string what;
    try {
        auto d =
            fixpp::dict::OrchestraLoader{}.load_from_string(kOrchestraIntTypedValidGroupXml, &mr);
        (void)d;
    } catch (orchestra_parse_error const& e) {
        caught_derived = true;
        what = e.what();
    } catch (xml_parse_error const& e) {
        ADD_FAILURE() << "FR-006c / C-6.1b: OrchestraLoader threw the BASE xml_parse_error, not "
                         "the derived orchestra_parse_error; got: "
                      << e.what();
        return;
    }
    ASSERT_TRUE(caught_derived) << "FR-023 / C-3.4 must reject an Int-typed context in the "
                                   "Orchestra loader too, as its own orchestra_parse_error "
                                   "(FR-006c).";
    EXPECT_NE(what.find("no per-context delimiter record (FR-023 completeness invariant)"),
              std::string::npos)
        << "the diagnostic must name the FR-023 completeness invariant specifically; got: "
        << what;
    EXPECT_NE(what.find("600"), std::string::npos)
        << "the diagnostic must name the offending group's count tag; got: " << what;
}

// ============================================================================
// Gate B r1 F1 (fixpp#261 PR review) — the negative counterpart to the two
// tests above: proves widening the sweep's candidate source to the
// DICTIONARY-WIDE structural tag set (rather than per-message FieldRef.type)
// did NOT reopen the false-rejection hazard the fix queue names. V1 declares
// a real Int-typed group (so tag 600 IS in the structural set); V2 reuses the
// same tag as a plain scalar and must NOT be flagged, exactly as
// ScalarReuseOfGroupTagIsNotACompletenessViolation pins for the
// NumInGroup-typed case.
// ============================================================================
TEST(LoaderDisposition, IntTypedScalarReuseOfGroupTagIsNotACompletenessViolation) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    // The load itself is the assertion under test: if the widened sweep's
    // structural tag set (built dictionary-wide) mistakenly flagged V2's
    // scalar reuse as an incomplete context, this would throw
    // xml_parse_error — discriminated explicitly rather than left to an
    // uncaught-exception failure, so the test's intent is legible from the
    // assertion itself.
    std::optional<fixpp::dict::Dictionary> dict;
    EXPECT_NO_THROW({
        dict = fixpp::dict::XmlLoader{}.load_from_string(kIntTypedScalarReuseXml, &mr);
    }) << "the structural sweep's !members.empty() exclusion must still hold per-message when the "
          "candidate source is the dictionary-wide structural tag set (V2's scalar reuse of "
          "NoGood(600) must not read as an incomplete context)";
    ASSERT_TRUE(dict.has_value());

    auto const tv = dict->as_table_view();
    std::array<std::uint16_t, 0> const root{};
    // V1's genuine Int-typed group context resolves.
    EXPECT_EQ(tv.group_first_field("V1", root, std::uint16_t{600}), 610)
        << "V1 declares NoGood(600) as a real Int-typed group; its context must resolve.";
}

// ============================================================================
// T042 / FR-023 / C-3.4 — a property pin (NOT the rejection witness — see
// `ContextWithoutDelimiterRecordRejectedAtFinalize` above for that): every
// shipped dictionary's registered contexts resolve a non-zero delimiter,
// which is what lets finalize()'s check pass on all ten without a carve-out.
//
// Why the assertion is shaped as "all ten load, and every registered context
// resolves a non-zero delimiter" rather than as a hand-built violating
// dictionary: the invariant holds BY CONSTRUCTION on ordinary input, and that
// is worth stating rather than engineering around. `captured == 0` means no
// FieldRef was emitted at the group's level, so no FieldRef carries its
// `group_no_tag`, so C-3.4a's `!members.empty()` leg excludes it from the
// registered set — there is no reachable ORDINARY input that registers a
// context and omits its record. The check is defence in depth against a
// future change breaking that correspondence, and this case pins the
// property it defends.
//
// The measured precondition is recorded in research.md D-7 (C-7.3): R\E = 0 on
// all ten, with E taken from the INDEPENDENT document-order oracle rather than
// from the loader's own table.
// ============================================================================
TEST(LoaderDisposition, AllShippedContextsHaveADelimiterRecord) {
    // The invariant is live: every shipped dictionary passes finalize()'s
    // check, and no registered context resolves the 0 that a missing record
    // would produce. A 0 here would be SILENT, not loud: set_group_first_ctx
    // stores it AND injects tag 0 as a member, after which the validator reads
    // the group as not-a-group and skips it.
    for (auto const& d : kAllTen) {
        if (d.is_orchestra) {
            // Not re-derived by this walk (which would cost a second 64 MB
            // re-parse of the same file). Covered TRANSITIVELY instead: W-6
            // (`AllTenShippedDictionariesLoadUnderFailClosedDefault`, above)
            // asserts the Orchestra dictionary loads successfully under the
            // fail-closed default, and the FR-023 finalize() check enforced in
            // BOTH loaders (`ContextWithoutDelimiterRecordRejectedAtFinalizeOrchestra`
            // proves it fires when it should) is exactly what would reject
            // that load if any registered context had no delimiter record —
            // so "loads" already implies "every context here resolves a
            // non-zero delimiter".
            continue;
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

// ============================================================================
// fixpp#264 — the FR-023 completeness probe and the registration path must
// build the SAME key for the same group context.
//
// Both walk a group's ancestor chain outward and reverse it to outermost-first.
// The K=16 clamp belongs to `make_group_ctx_delim` / `make_group_ctx_key`,
// which keep the OUTERMOST `kMaxGroupContextDepth` entries. The probe used to
// clamp DURING its walk instead, which keeps the INNERMOST 16 — the same 16
// only while the chain is no longer than K. At 17 the two keys differ by
// construction, the probe's `lower_bound` misses a record that IS there, and a
// COMPLETE dictionary is rejected at load as an internal-invariant violation.
//
// The pin is that a complete deep dictionary LOADS **and** that its deepest
// context RESOLVES. "Loads" alone would also pass with the FR-023 check
// deleted outright; the delimiter assertion is what keeps the check's removal
// visible from this test.
// ============================================================================
TEST(LoaderDisposition, DeepAncestorChainLoadsAndResolvesDelimiter) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};

    auto const xml = make_deep_nesting_xml();

    // The `try` deliberately wraps the LOAD and nothing else: its failure
    // message names a load rejection, and if it also covered the assertions
    // below, any `xml_parse_error` escaping those would be misreported as one.
    std::optional<fixpp::dict::Dictionary> dict;
    try {
        dict.emplace(fixpp::dict::XmlLoader{}.load_from_string(xml, &mr));
    } catch (xml_parse_error const& e) {
        FAIL() << "fixpp#264: a COMPLETE dictionary nesting " << kDeepChainLen
               << " groups was rejected at load. RED before the fix with the FR-023 completeness "
                  "message; any other message means the fixture, not the probe, is at fault. "
                  "what(): "
               << e.what();
    }
    ASSERT_TRUE(dict.has_value());
    auto const tv = dict->as_table_view();

    // The leaf's full ancestor chain, outermost first.
    std::vector<std::uint16_t> full_path;
    for (std::uint16_t i = 1; i <= kDeepChainLen; ++i) {
        full_path.push_back(static_cast<std::uint16_t>(kDeepCountBase + i));
    }

    // `make_group_ctx_key` stores this context under the OUTERMOST K of that
    // chain. `group_ctx_query` does NOT clamp — it hashes and compares the
    // caller's span verbatim — so the query passes the same truncated prefix
    // the stored key holds. A caller passing the untruncated chain misses; that
    // is a separate lookup-side divergence and is not what this case pins.
    std::span<std::uint16_t const> const key_path{full_path.data(),
                                                  fixpp::dict::kMaxGroupContextDepth};

    auto const first = tv.group_first_field_exact("M", key_path, kDeepLeafNoTag);
    ASSERT_TRUE(first.has_value())
        << "fixpp#264: the depth-" << full_path.size() << " context for NoLeaf(" << kDeepLeafNoTag
        << ") was registered by as_table_view() but cannot be found under the key "
           "as_table_view() itself built.";
    EXPECT_EQ(*first, kDeepLeafDelim)
        << "fixpp#264: the deepest context must resolve to its declared first member. A 0 "
           "here means the context is present but empty — which is also what deleting the "
           "FR-023 check would leave behind.";
}
