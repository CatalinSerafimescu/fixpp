// tests/dictionary/orchestra_loader_test.cpp
// 074-orchestra-native-reader — native Orchestra reader test bucket
// (dictionary_orchestra_tests; label "orchestra"; select via `ctest -L orchestra`).
//
// US1 (T011/T012): load the vendored OrchestraFIXLatest.xml (EP303) → a
// Dictionary with 181 messages, distinct session_version::vlatest identity,
// headline msg types present, and codeset values+descriptions preserved.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <span>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

std::filesystem::path orchestra_file() {
    return std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "OrchestraFIXLatest.xml";
}

std::filesystem::path fix50sp2_file() {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX50SP2.xml";
}

std::string read_file(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Builds a registry with BOTH a FIX50SP2 and a FIX Latest dict — the FR-010
// case. Hoisted out of the ASSERT_DEATH macro because the braced-init commas
// would otherwise be parsed as extra macro arguments.
void build_both_dict_registry() {
    std::pmr::monotonic_buffer_resource mr;
    auto latest = std::make_shared<const fixpp::dict::Dictionary>(
        fixpp::dict::OrchestraLoader{}.load(orchestra_file(), &mr));
    auto sp2 = std::make_shared<const fixpp::dict::Dictionary>(
        fixpp::dict::XmlLoader{}.load(fix50sp2_file(), &mr));
    std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> const dicts{latest, sp2};
    fixpp::dict::version_registry reg{dicts};
    (void)reg;
}

// True if `mt` appears in the loaded message set.
bool has_msg_type(fixpp::dict::Dictionary const& d, std::string_view mt) {
    for (auto const& m : d.messages()) {
        if (m.msg_type == mt) {
            return true;
        }
    }
    return false;
}

}  // namespace

// T011 — SC-001/FR-003: exactly 181 messages, distinct vlatest identity.
TEST(OrchestraLoad, LoadsEP303) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto dict = loader.load(orchestra_file(), &mr);

    EXPECT_EQ(dict.messages().size(), 181U);
    EXPECT_EQ(dict.which_session_version(), fixpp::dict::session_version::vlatest);
}

// T012a — headline FIX Latest msg types are present in the loaded set.
TEST(OrchestraLoad, Headlines) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto dict = loader.load(orchestra_file(), &mr);

    EXPECT_TRUE(has_msg_type(dict, "D"));   // NewOrderSingle
    EXPECT_TRUE(has_msg_type(dict, "8"));   // ExecutionReport
    EXPECT_TRUE(has_msg_type(dict, "AE"));  // TradeCaptureReport
    EXPECT_TRUE(has_msg_type(dict, "R"));   // QuoteRequest
    EXPECT_TRUE(has_msg_type(dict, "AB"));  // NewOrderMultileg
}

// T012b — FR-002: a known codeset field's enumerated values AND their
// descriptions survive into the internal dictionary. AdvSide (tag 4) is backed
// by AdvSideCodeSet: B=Buy, S=Sell, T=Trade, X=Cross.
TEST(OrchestraCodesets, PreservesValuesAndDescriptions) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto dict = loader.load(orchestra_file(), &mr);

    auto const codes = dict.enum_values(4);
    ASSERT_FALSE(codes.empty()) << "AdvSide(4) codeset values must be preserved";

    // Both the value bytes AND the description text must survive.
    auto find_desc = [&](std::string_view value) -> std::string_view {
        for (auto const& c : codes) {
            if (c.value == value) {
                return c.description;
            }
        }
        return std::string_view{};
    };
    EXPECT_EQ(find_desc("B"), "Buy");
    EXPECT_EQ(find_desc("S"), "Sell");
    EXPECT_EQ(find_desc("T"), "Trade");
    EXPECT_EQ(find_desc("X"), "Cross");
    EXPECT_EQ(codes.size(), 4U);
}

// T015 — SC-003/FR-004: end-to-end full-parent-path group resolution over the
// real depth-7 chain rooted at MassQuoteAck (msgType "b"):
//   296 (QuotSetAckGrp) -> 295 (QuotEntryAckGrp) -> 555 (InstrmtLegGrp, via
//   InstrumentLeg component 1005) -> 40241 (LegStreamGrp) -> 41686
//   (LegStreamCommoditySettlPeriodGrp, via LegStreamCommodity component 4237)
//   -> 41680 (LegStreamCommoditySettlDayGrp) -> 41683
//   (LegStreamCommoditySettlTimeGrp).
//
// Ground truth read directly from dictionaries/orchestra/OrchestraFIXLatest.xml
// (group ids 2048/2042/2019/4031/4242/4240/4241). The deepest 4 levels
// (40241/41686/41680/41683) have exactly ONE `<fixr:numInGroup>` declaration
// each in the whole file (grep-verified), so their delimiters are asserted
// exactly. The upper levels (296/295/555) are REUSED tags (2-10 distinct
// `<fixr:group>` definitions share the same numInGroup id across the
// dictionary) — the bare/legacy first-seen-wins delimiter fallback
// (table_view.hpp's documented L-063-3 residual) is not assumed for those, so
// only member-set containment (not delimiter identity) is asserted for them.
TEST(OrchestraGroups, DeepAndReused) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto dict = loader.load(orchestra_file(), &mr);
    auto const tv = dict.as_table_view();

    constexpr std::string_view kMassQuoteAck = "b";

    // ── Leaf level: 41683 (LegStreamCommoditySettlTimeGrp) ──────────────────
    // Full parent path (outermost first, excluding 41683 itself).
    std::uint16_t const path_41683[] = {296, 295, 555, 40241, 41686, 41680};
    EXPECT_EQ(tv.group_first_field(kMassQuoteAck, path_41683, 41683), 41684U);
    {
        auto const members = tv.group_member_tags(kMassQuoteAck, path_41683, 41683);
        std::vector<std::uint16_t> sorted{members.begin(), members.end()};
        std::ranges::sort(sorted);
        EXPECT_EQ(sorted, (std::vector<std::uint16_t>{41684, 41685, 41935}));
    }

    // ── One level up: 41680 (LegStreamCommoditySettlDayGrp) ─────────────────
    std::uint16_t const path_41680[] = {296, 295, 555, 40241, 41686};
    EXPECT_EQ(tv.group_first_field(kMassQuoteAck, path_41680, 41680), 41681U);
    {
        auto const members = tv.group_member_tags(kMassQuoteAck, path_41680, 41680);
        std::vector<std::uint16_t> sorted{members.begin(), members.end()};
        std::ranges::sort(sorted);
        // 41683 is the nested group's own count field, propagated into this run.
        EXPECT_EQ(sorted, (std::vector<std::uint16_t>{41681, 41682, 41683}));
    }

    // ── 41686 (LegStreamCommoditySettlPeriodGrp) ─────────────────────────────
    std::uint16_t const path_41686[] = {296, 295, 555, 40241};
    EXPECT_EQ(tv.group_first_field(kMassQuoteAck, path_41686, 41686), 41687U);
    {
        auto const members = tv.group_member_tags(kMassQuoteAck, path_41686, 41686);
        std::unordered_set<std::uint16_t> const member_set{members.begin(), members.end()};
        EXPECT_TRUE(member_set.contains(41687)) << "declared delimiter";
        EXPECT_TRUE(member_set.contains(41680)) << "nested LegStreamCommoditySettlDayGrp count tag";
    }

    // ── 40241 (LegStreamGrp) ──────────────────────────────────────────────
    std::uint16_t const path_40241[] = {296, 295, 555};
    EXPECT_EQ(tv.group_first_field(kMassQuoteAck, path_40241, 40241), 40242U);
    {
        auto const members = tv.group_member_tags(kMassQuoteAck, path_40241, 40241);
        std::unordered_set<std::uint16_t> const member_set{members.begin(), members.end()};
        EXPECT_TRUE(member_set.contains(40242)) << "declared delimiter";
        EXPECT_TRUE(member_set.contains(41686))
            << "LegStreamCommoditySettlPeriodGrp count tag, reached via componentRef 4237 "
               "(LegStreamCommodity) inlined transitively";
    }

    // ── SC-003/FR-004: reused tag 555 (NoLegs) disambiguated by full parent
    // path. 555 is declared by ~10 distinct <fixr:group> elements across the
    // dictionary. Under msgType "b" at path [296,295] it resolves to
    // InstrmtLegGrp (group id 2019, via componentRef 1005 InstrumentLeg);
    // under msgType "AB" (NewOrderMultileg) at the top-level path [] it
    // resolves to the DISTINCT LegOrdGrp (group id 2025). LegOrderQty(685) is
    // declared only inside LegOrdGrp's own body (not InstrumentLeg/1005 or
    // LegFinancingDetails/2251, the components InstrmtLegGrp pulls in), so its
    // presence/absence discriminates the two contexts directly — proving the
    // resolution used the full path, not a bare no_tag lookup.
    std::uint16_t const path_555_massquoteack[] = {296, 295};
    auto const members_b = tv.group_member_tags(kMassQuoteAck, path_555_massquoteack, 555);
    ASSERT_FALSE(members_b.empty());
    EXPECT_FALSE(std::ranges::find(members_b, std::uint16_t{685}) != members_b.end())
        << "LegOrderQty(685) must NOT appear under InstrmtLegGrp's context";

    auto const members_ab = tv.group_member_tags("AB", std::span<std::uint16_t const>{}, 555);
    ASSERT_FALSE(members_ab.empty());
    EXPECT_TRUE(std::ranges::find(members_ab, std::uint16_t{685}) != members_ab.end())
        << "LegOrderQty(685) must appear under LegOrdGrp's (msgType AB, top-level) context";
}

// T018 — SC-005/FR-005: FIX Latest has a real DISTINCT session identity
// (session_version::vlatest), not a FIX.5.0SP2 relabel; its wire application
// version maps to v50sp2 (observed through the registry). Distinct from all
// nine legacy session_versions and from vt11.
TEST(OrchestraVersionIdentity, DistinctVLatestMapsToV50SP2) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto dict = loader.load(orchestra_file(), &mr);

    // Distinct identity — not any legacy version, not vt11, not v50sp2.
    auto const sv = dict.which_session_version();
    EXPECT_EQ(sv, fixpp::dict::session_version::vlatest);
    for (auto legacy : {fixpp::dict::session_version::v40, fixpp::dict::session_version::v41,
                        fixpp::dict::session_version::v42, fixpp::dict::session_version::v43,
                        fixpp::dict::session_version::v44, fixpp::dict::session_version::v50,
                        fixpp::dict::session_version::v50sp1, fixpp::dict::session_version::v50sp2,
                        fixpp::dict::session_version::vt11}) {
        EXPECT_NE(sv, legacy);
    }

    // vlatest → application_version::v50sp2 (observed: a registry holding ONLY
    // the FIX Latest dict answers a v50sp2 lookup with it — the standards-correct
    // wire app-version arm, no distinct ApplVerID).
    auto shared = std::make_shared<const fixpp::dict::Dictionary>(std::move(dict));
    fixpp::dict::version_registry reg{{shared}};
    auto got = reg.get(fixpp::dict::application_version::v50sp2);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, shared.get());
}

// T019 — FR-010: a version_registry carrying BOTH a real FIX50SP2 dict and a
// FIX Latest dict (both resolving to the shared application_version::v50sp2
// slot) MUST fail loud, not silently last-writer-wins. Single-dict configs
// (each alone) succeed unaffected. Death-test lives in this grouped bucket
// (fork-safe); the regex matches our own portable fatal message (NOT the
// libstdc++-only "terminate called" banner — feedback_death_test_terminate_
// banner_not_portable).
TEST(OrchestraFR010Guard, BothDictsFailLoud) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ASSERT_DEATH(build_both_dict_registry(), "FR-010");
}

TEST(OrchestraFR010Guard, SingleDictConfigsSucceed) {
    // FIX Latest alone.
    {
        std::pmr::monotonic_buffer_resource mr;
        auto latest = std::make_shared<const fixpp::dict::Dictionary>(
            fixpp::dict::OrchestraLoader{}.load(orchestra_file(), &mr));
        fixpp::dict::version_registry reg{{latest}};
        auto got = reg.get(fixpp::dict::application_version::v50sp2);
        ASSERT_TRUE(got.has_value());
        EXPECT_EQ(*got, latest.get());
    }
    // FIX50SP2 alone.
    {
        std::pmr::monotonic_buffer_resource mr;
        auto sp2 = std::make_shared<const fixpp::dict::Dictionary>(
            fixpp::dict::XmlLoader{}.load(fix50sp2_file(), &mr));
        fixpp::dict::version_registry reg{{sp2}};
        auto got = reg.get(fixpp::dict::application_version::v50sp2);
        ASSERT_TRUE(got.has_value());
        EXPECT_EQ(*got, sp2.get());
    }
}

// T022 — fail-closed matrix per contracts/orchestra_loader.md. Synthetic
// Orchestra XML strings via `load_from_string`, each a discriminating case
// against a distinct rejection/acceptance path in OrchestraLoaderState.
namespace {

// Minimal valid Orchestra skeleton: one field (String, tag 1), one message
// (Heartbeat, msgType "0") referencing it. No fixr:datatypes / fixr:codeSets /
// fixr:components / fixr:groups blocks — all optional per collect_*() (only
// fixr:fields and fixr:messages are required, orchestra_loader.cpp:305-390).
constexpr std::string_view kMinimalValidRepository = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
  </fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure>
        <fixr:fieldRef id="1"/>
      </fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";

}  // namespace

// (a) SC-002 discriminating: a field's type= is a datatype token outside
// kOrchestraTypeTable AND outside the (empty) codeset names — collect_fields
// resolves every declared field's type eagerly, so this throws regardless of
// whether any message references tag 2.
TEST(OrchestraFailClosed, UnknownDatatypeUsedByField) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
    <fixr:field id="2" name="Bogus" type="TotallyBogusType"/>
  </fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure>
        <fixr:fieldRef id="1"/>
        <fixr:fieldRef id="2"/>
      </fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (b) An unused unknown <fixr:datatype> DECLARATION (inside <fixr:datatypes>,
// not a field's type=) does not fail the load: the loader has no
// collect_datatypes() step at all (orchestra_loader.cpp's parse_document only
// walks fixr:codeSets/fixr:fields/fixr:components/fixr:groups/fixr:messages);
// resolve_datatype is only ever invoked against a FIELD's `type=` attribute.
TEST(OrchestraFailClosed, UnusedUnknownDatatypeDeclarationDoesNotThrow) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:datatypes>
    <fixr:datatype name="Bogus"/>
  </fixr:datatypes>
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
  </fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure>
        <fixr:fieldRef id="1"/>
      </fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto dict = loader.load_from_string(kXml, &mr);
    EXPECT_EQ(dict.messages().size(), 1U);
}

// (c) unionDataType= present, but the PRIMARY type= is itself an unknown
// datatype token (not a codeset name, not in kOrchestraTypeTable) — the
// drop-second-arm rule (unionDataType is never even read, per
// orchestra_loader.cpp:314-317) must NOT mask the unknown base type.
TEST(OrchestraFailClosed, UnionDataTypeDoesNotMaskUnknownPrimaryType) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
    <fixr:field id="2" name="BogusUnion" type="TotallyBogusType" unionDataType="Qty"/>
  </fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure>
        <fixr:fieldRef id="1"/>
        <fixr:fieldRef id="2"/>
      </fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (d) non-fixr:repository root.
TEST(OrchestraFailClosed, NonRepositoryRoot) {
    constexpr std::string_view kXml = R"xml(<foo><bar/></foo>)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (e) A real QuickFIX FIX44.xml fed to OrchestraLoader — wrong grammar (root
// is <fix major=...>, not <fixr:repository>) — must throw, never mis-parse.
TEST(OrchestraFailClosed, QuickFixXmlFedToOrchestraLoaderThrows) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto const fix44 = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    // FIX44.xml's root is <fix major="4" minor="4" ...>, not <fixr:repository>
    // (verified: dictionaries/FIX44.xml:1), so this hits parse_root_and_version's
    // root check deterministically (not a downstream unknown-datatype/dangling-
    // ref path).
    EXPECT_THROW((void)loader.load(fix44, &mr), fixpp::dict::orchestra_parse_error);
}

// (f) Truncated / malformed XML — pugixml parse failure mapped to
// orchestra_parse_error.
TEST(OrchestraFailClosed, TruncatedXmlThrows) {
    constexpr std::string_view kXml = "<fixr:repository><fixr:fiel";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (g) A dangling <fixr:componentRef id="999"> inside a message's structure,
// where no <fixr:components> block declares id 999. 999 is a valid uint16 so it
// passes the id-parse guard and reaches the not-defined check (the point here).
TEST(OrchestraFailClosed, DanglingComponentRefThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
  </fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure>
        <fixr:fieldRef id="1"/>
        <fixr:componentRef id="999"/>
      </fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (h) [reverse asymmetry regression pin] the vendored Orchestra file fed to
// XmlLoader (QuickFIX-XML reader) — its root is <fixr:repository>, not <fix>,
// so XmlLoader::parse_version's non-"fix"-root check (xml_loader.cpp:284-285)
// rejects it. Assert THROWS (any documented XmlLoader exception type).
TEST(OrchestraFailClosed, VendoredOrchestraFileFedToXmlLoaderThrows) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::XmlLoader loader;
    // xml_loader.cpp:284-285's `root.name() != "fix"` check fires (the
    // Orchestra root is <fixr:repository>) — the base xml_parse_error, NOT
    // orchestra_parse_error (that type is OrchestraLoader-only).
    EXPECT_THROW((void)loader.load(orchestra_file(), &mr), fixpp::dict::xml_parse_error);
}

// Sanity: the minimal valid skeleton used as a base for the negative cases
// above actually loads clean on its own (proves the negative cases fail for
// the INTENDED reason, not an unrelated malformedness in the skeleton).
TEST(OrchestraFailClosed, MinimalValidSkeletonLoadsClean) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto dict = loader.load_from_string(kMinimalValidRepository, &mr);
    EXPECT_EQ(dict.messages().size(), 1U);
    EXPECT_EQ(dict.which_session_version(), fixpp::dict::session_version::vlatest);
}

// (i) A well-formed fixr:repository whose version= is NOT FIX Latest EP303 —
// the version resolver rejects it with unknown_version_error (FR-005 negative;
// distinct from orchestra_parse_error, discriminated by catch type).
TEST(OrchestraFailClosed, WrongVersionThrowsUnknownVersion) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.4.4">
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
  </fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure><fixr:fieldRef id="1"/></fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::unknown_version_error);
}

// (j) Missing the required <fixr:fields> block.
TEST(OrchestraFailClosed, MissingFieldsBlockThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0"><fixr:structure/></fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (k) Missing the required <fixr:messages> block.
TEST(OrchestraFailClosed, MissingMessagesBlockThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields><fixr:field id="1" name="Account" type="String"/></fixr:fields>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (l) A <fixr:message> missing its msgType attribute.
TEST(OrchestraFailClosed, MessageMissingMsgTypeThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields><fixr:field id="1" name="Account" type="String"/></fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat"><fixr:structure><fixr:fieldRef id="1"/></fixr:structure></fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (m) A <fixr:fieldRef> referencing a tag not declared in <fixr:fields>.
TEST(OrchestraFailClosed, UndeclaredFieldRefThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields><fixr:field id="1" name="Account" type="String"/></fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure><fixr:fieldRef id="1"/><fixr:fieldRef id="4242"/></fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (n) A dangling <fixr:groupRef id="999"> (valid uint16, undeclared) — reaches
// the group-not-defined check.
TEST(OrchestraFailClosed, DanglingGroupRefThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields><fixr:field id="1" name="Account" type="String"/></fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure><fixr:fieldRef id="1"/><fixr:groupRef id="999"/></fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (o) A <fixr:codeSet> missing its name attribute.
TEST(OrchestraFailClosed, CodeSetMissingNameThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:codeSets>
    <fixr:codeSet id="4" type="char"><fixr:code id="1" name="Buy" value="B"/></fixr:codeSet>
  </fixr:codeSets>
  <fixr:fields><fixr:field id="1" name="Account" type="String"/></fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0"><fixr:structure><fixr:fieldRef id="1"/></fixr:structure></fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (p) load(path) on a nonexistent file — the ifstream-open-fail arm.
TEST(OrchestraFailClosed, LoadNonexistentPathThrows) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    auto const bad = std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "does_not_exist.xml";
    EXPECT_THROW((void)loader.load(bad, &mr), fixpp::dict::orchestra_parse_error);
}

// (t) A structural ref with a non-numeric id — parse_orchestra_id fail-closed
// (distinct from the not-defined check: the id never parses to a uint16).
TEST(OrchestraFailClosed, MalformedStructuralIdThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields><fixr:field id="1" name="Account" type="String"/></fixr:fields>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure><fixr:fieldRef id="1"/><fixr:componentRef id="abc"/></fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (q) T017 — nested-group delimiter collision: a nested group whose delimiter
// (first member field) equals its enclosing group's delimiter fails loud with
// group_delimiter_collision_error (the 072 load-time check, applied to the
// Orchestra reader). group A (numInGroup 100) and nested group B (numInGroup
// 300) BOTH have field 200 as their first member → B.delimiter == A.delimiter.
TEST(OrchestraFailClosed, NestedDelimiterCollisionThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="100" name="NoA" type="NumInGroup"/>
    <fixr:field id="200" name="Shared" type="String"/>
    <fixr:field id="300" name="NoB" type="NumInGroup"/>
  </fixr:fields>
  <fixr:groups>
    <fixr:group id="10" name="GroupA">
      <fixr:numInGroup id="100"/>
      <fixr:fieldRef id="200"/>
      <fixr:groupRef id="20"/>
    </fixr:group>
    <fixr:group id="20" name="GroupB">
      <fixr:numInGroup id="300"/>
      <fixr:fieldRef id="200"/>
    </fixr:group>
  </fixr:groups>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure><fixr:groupRef id="10"/></fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr),
                 fixpp::dict::group_delimiter_collision_error);
}

// (r) A <fixr:group> (referenced by a groupRef) that is missing its required
// <fixr:numInGroup> child.
TEST(OrchestraFailClosed, GroupMissingNumInGroupThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields><fixr:field id="200" name="Shared" type="String"/></fixr:fields>
  <fixr:groups>
    <fixr:group id="10" name="GroupA"><fixr:fieldRef id="200"/></fixr:group>
  </fixr:groups>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure><fixr:groupRef id="10"/></fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (s) A <fixr:numInGroup> whose id is not a declared <fixr:field>.
TEST(OrchestraFailClosed, NumInGroupIdNotDeclaredThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields><fixr:field id="200" name="Shared" type="String"/></fixr:fields>
  <fixr:groups>
    <fixr:group id="10" name="GroupA">
      <fixr:numInGroup id="999"/>
      <fixr:fieldRef id="200"/>
    </fixr:group>
  </fixr:groups>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure><fixr:groupRef id="10"/></fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// T021 — US4/FR-007/SC-007: provenance + Apache-2.0 attribution artifacts are
// present and correct. The cryptographic sha1 pin is enforced at configure time
// (tests/dictionary/CMakeLists.txt file(SHA1) gate); this test covers the
// UPSTREAM/LICENSE/NOTICE artifacts + the exact byte size (tamper-evident).
TEST(OrchestraProvenance, ArtifactsPresentAndPinned) {
    namespace fs = std::filesystem;
    fs::path const dir{FIXPP_ORCHESTRA_DATA_DIR};

    // Vendored source present + exact byte count (7,525,180 — the OFFICIAL file).
    auto const xml = dir / "OrchestraFIXLatest.xml";
    ASSERT_TRUE(fs::exists(xml));
    EXPECT_EQ(fs::file_size(xml), 7525180U);

    // UPSTREAM.txt records repo + pinned commit + sha1 + EP303 + license.
    auto const upstream = read_file(dir / "UPSTREAM.txt");
    EXPECT_NE(upstream.find("FIXTradingCommunity/orchestrations"), std::string::npos);
    EXPECT_NE(upstream.find("236d4a4054f0818f1931601713f7a6a68b275df7"), std::string::npos);
    EXPECT_NE(upstream.find("26f60db1c1f52d169d3b6825ac68800abf487fde"), std::string::npos);
    EXPECT_NE(upstream.find("EP303"), std::string::npos);
    EXPECT_NE(upstream.find("Apache-2.0"), std::string::npos);

    // Apache-2.0 LICENSE text present.
    auto const license = read_file(dir / "LICENSE");
    EXPECT_NE(license.find("Apache License"), std::string::npos);
    EXPECT_NE(license.find("Version 2.0"), std::string::npos);

    // NOTICE (Apache-2.0 §4) with the FIX Protocol Ltd attribution.
    auto const notice = read_file(dir / "NOTICE");
    EXPECT_FALSE(notice.empty());
    EXPECT_NE(notice.find("FIX Protocol Ltd"), std::string::npos);
    EXPECT_NE(notice.find("Apache License"), std::string::npos);
}

// T024 — legacy no-regression pin: 074 (OrchestraLoader) is purely additive —
// XmlLoader and the nine QuickFIX dicts it serves are untouched. Pins the
// exact message count per dict (recorded by running this loader once) so any
// future accidental XmlLoader change is caught here, not just in
// xml_loader_test.cpp.
TEST(OrchestraLegacyNoRegression, NineQuickFixDictsUnchanged) {
    namespace fs = std::filesystem;
    fs::path const dir{FIXPP_DICT_DATA_DIR};

    struct Pinned {
        char const* file;
        std::size_t message_count;
    };
    // Counts recorded from a real load of each vendored dict (074 T024).
    constexpr Pinned kPins[] = {
        {"FIX40.xml", 27U},     {"FIX41.xml", 28U},     {"FIX42.xml", 46U},
        {"FIX43.xml", 68U},     {"FIX44.xml", 93U},     {"FIX50.xml", 93U},
        {"FIX50SP1.xml", 105U}, {"FIX50SP2.xml", 156U}, {"FIXT11.xml", 8U},
    };

    for (auto const& pin : kPins) {
        std::pmr::monotonic_buffer_resource mr;
        fixpp::dict::XmlLoader loader;
        fixpp::dict::Dictionary dict = loader.load(dir / pin.file, &mr);
        EXPECT_EQ(dict.messages().size(), pin.message_count) << pin.file;
    }

    // Group tables still build: FIX44's NoHops(627) group (header-level,
    // present since FIX.4.0) resolves with a non-zero delimiter.
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::XmlLoader loader;
    auto const fix44 = loader.load(dir / "FIX44.xml", &mr);
    EXPECT_NE(fix44.group_first_field(627), 0U);
    ASSERT_TRUE(fix44.group(627).has_value());
}
