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
#include <map>
#include <memory>
#include <memory_resource>
#include <pugixml.hpp>
#include <span>
#include <sstream>
#include <string>
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
// fixr:fields and fixr:messages are required, orchestra_loader.cpp).
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
// `collect_fields`'s `unionDataType=` handling) must NOT mask the unknown base type.
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
    // (verified: dictionaries/FIX44.xml's root `<fix>` element), so this hits
    // parse_root_and_version's root check deterministically (not a downstream
    // unknown-datatype/dangling- ref path).
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
// so XmlLoader::parse_document's missing-<fix>-child check
// (`LoaderState::parse_document`'s missing-`<fix>` check) rejects it. Assert THROWS (any documented
// XmlLoader exception type).
TEST(OrchestraFailClosed, VendoredOrchestraFileFedToXmlLoaderThrows) {
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::XmlLoader loader;
    // `LoaderState::parse_document`'s `doc.child("fix")`-missing check fires (the
    // Orchestra root is <fixr:repository>, so `doc.child("fix")` finds
    // nothing) — the base xml_parse_error, NOT orchestra_parse_error (that
    // type is OrchestraLoader-only).
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

// (u) Gate B FQ-1 — a duplicate <fixr:field id="1"> declaration must throw
// (fail-closed parity with XmlLoader's duplicate <field number> reject,
// `LoaderState::parse_global_fields`'s duplicate check). Otherwise-valid skeleton; the throw is
// attributable only to the duplicate id.
TEST(OrchestraFailClosed, DuplicateFieldIdThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
    <fixr:field id="1" name="AccountDup" type="String"/>
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
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (v) Gate B FQ-1 — a duplicate <fixr:component id="1000"> declaration must
// throw (fail-closed parity with XmlLoader's duplicate <component name>
// reject, `LoaderState::collect_components`'s duplicate check; also removes the phantom-top-level-
// ComponentRef corruption an unconditional push_back would otherwise cause).
TEST(OrchestraFailClosed, DuplicateComponentIdThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
  </fixr:fields>
  <fixr:components>
    <fixr:component id="1000" name="CompA">
      <fixr:fieldRef id="1"/>
    </fixr:component>
    <fixr:component id="1000" name="CompB">
      <fixr:fieldRef id="1"/>
    </fixr:component>
  </fixr:components>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure><fixr:componentRef id="1000"/></fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_THROW((void)loader.load_from_string(kXml, &mr), fixpp::dict::orchestra_parse_error);
}

// (w) Gate B FQ-1 — a duplicate <fixr:group id="10"> declaration must throw.
// No direct QuickFIX-XML sibling (groups are inline there), but the same
// FR-009 fail-closed-on-malformed-structural-id obligation applies.
TEST(OrchestraFailClosed, DuplicateGroupIdThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="100" name="NoA" type="NumInGroup"/>
    <fixr:field id="200" name="Shared" type="String"/>
  </fixr:fields>
  <fixr:groups>
    <fixr:group id="10" name="GroupA">
      <fixr:numInGroup id="100"/>
      <fixr:fieldRef id="200"/>
    </fixr:group>
    <fixr:group id="10" name="GroupADup">
      <fixr:numInGroup id="100"/>
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

// (x) Gate B FQ-1 follow-on — a duplicate <fixr:codeSet name="..."> declaration
// must throw (same duplicate-structural-id class as (u)/(v)/(w); no direct
// XmlLoader sibling — codesets are QuickFIX-XML's inline <field><value/></field>
// enums — but the same FR-009 fail-closed obligation applies).
TEST(OrchestraFailClosed, DuplicateCodeSetNameThrows) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:codeSets>
    <fixr:codeSet name="AdvSideCodeSet" type="char">
      <fixr:code id="1" name="Buy" value="B"/>
    </fixr:codeSet>
    <fixr:codeSet name="AdvSideCodeSet" type="char">
      <fixr:code id="1" name="Sell" value="S"/>
    </fixr:codeSet>
  </fixr:codeSets>
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
        {.file = "FIX40.xml", .message_count = 27U},
        {.file = "FIX41.xml", .message_count = 28U},
        {.file = "FIX42.xml", .message_count = 46U},
        {.file = "FIX43.xml", .message_count = 68U},
        {.file = "FIX44.xml", .message_count = 93U},
        {.file = "FIX50.xml", .message_count = 93U},
        {.file = "FIX50SP1.xml", .message_count = 105U},
        {.file = "FIX50SP2.xml", .message_count = 156U},
        {.file = "FIXT11.xml", .message_count = 8U},
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

// fixpp#427 — Length+Data pairs come from each data field's `lengthId=`. The
// expected set is DERIVED from the vendored XML, never hand-listed, and the two
// sides are compared over every declared field id: a pair's Length tag is a
// declared field, so that population is exhaustive.
TEST(OrchestraLengthPairs, DictionaryPairsEqualTheXmlLengthIds) {
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_file(orchestra_file().c_str()));
    std::map<std::uint16_t, std::uint16_t> from_xml;
    std::vector<std::uint16_t> declared;
    for (auto const& f : doc.child("fixr:repository").child("fixr:fields").children("fixr:field")) {
        auto const id = static_cast<std::uint16_t>(f.attribute("id").as_uint());
        declared.push_back(id);
        if (auto const len = f.attribute("lengthId")) {
            from_xml.emplace(static_cast<std::uint16_t>(len.as_uint()), id);
        }
    }
    ASSERT_FALSE(from_xml.empty());

    std::pmr::monotonic_buffer_resource mr;
    auto const dict = fixpp::dict::OrchestraLoader{}.load(orchestra_file(), &mr);
    std::map<std::uint16_t, std::uint16_t> from_dict;
    for (auto const tag : declared) {
        if (auto const data = dict.length_pair_data_tag(tag); data != 0) {
            from_dict.emplace(tag, data);
        }
    }
    EXPECT_EQ(from_dict, from_xml);
}

// Upstream EP303 declares EncodedMDEntryStatusText(3109) as `data` with no
// `lengthId=`, although EncodedMDEntryStatusTextLen(3108) is a Length field. The
// loader does not guess a partner. This is the complement of the census above: a
// data field without `lengthId=` is only this named upstream gap. If upstream
// adds the attribute, this fails and the exception should be deleted.
TEST(OrchestraLengthPairs, DataFieldsWithoutLengthIdAreOnlyTheKnownUpstreamGap) {
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_file(orchestra_file().c_str()));
    std::vector<std::uint16_t> unpaired;
    for (auto const& f : doc.child("fixr:repository").child("fixr:fields").children("fixr:field")) {
        std::string_view const type = f.attribute("type").as_string("");
        if ((type == "data" || type == "XMLData") && !f.attribute("lengthId")) {
            unpaired.push_back(static_cast<std::uint16_t>(f.attribute("id").as_uint()));
        }
    }
    EXPECT_EQ(unpaired, std::vector<std::uint16_t>{3109});

    std::pmr::monotonic_buffer_resource mr;
    auto const dict = fixpp::dict::OrchestraLoader{}.load(orchestra_file(), &mr);
    EXPECT_EQ(dict.length_pair_data_tag(3108), 0U);
}

namespace {

// A minimal repository whose Heartbeat references every field in `fields`.
std::string repository_with_fields(std::string_view fields, std::string_view refs) {
    return std::string{R"xml(<fixr:repository version="FIX.Latest_EP303"><fixr:fields>)xml"} +
           std::string{fields} +
           R"xml(</fixr:fields><fixr:messages><fixr:message id="1" name="Heartbeat" msgType="0"><fixr:structure>)xml" +
           std::string{refs} +
           R"xml(</fixr:structure></fixr:message></fixr:messages></fixr:repository>)xml";
}

}  // namespace

// A `lengthId=` may point FORWARD, as Signature(89) -> SignatureLength(93) does.
TEST(OrchestraLengthPairs, ForwardLengthIdResolves) {
    auto const xml = repository_with_fields(
        R"xml(<fixr:field id="89" name="Signature" type="data" lengthId="93"/>
              <fixr:field id="93" name="SignatureLength" type="Length"/>)xml",
        R"xml(<fixr:fieldRef id="93"/><fixr:fieldRef id="89"/>)xml");
    std::pmr::monotonic_buffer_resource mr;
    auto const dict = fixpp::dict::OrchestraLoader{}.load_from_string(xml, &mr);
    EXPECT_EQ(dict.length_pair_data_tag(93), 89U);
    EXPECT_EQ(dict.length_pair_data_tag(89), 0U);
}

TEST(OrchestraFailClosed, LengthIdNamingAnUndeclaredFieldThrows) {
    auto const xml = repository_with_fields(
        R"xml(<fixr:field id="96" name="RawData" type="data" lengthId="95"/>)xml",
        R"xml(<fixr:fieldRef id="96"/>)xml");
    std::pmr::monotonic_buffer_resource mr;
    EXPECT_THROW((void)fixpp::dict::OrchestraLoader{}.load_from_string(xml, &mr),
                 fixpp::dict::orchestra_parse_error);
}

TEST(OrchestraFailClosed, LengthIdNamingANonLengthFieldThrows) {
    auto const xml = repository_with_fields(
        R"xml(<fixr:field id="95" name="RawDataLength" type="int"/>
              <fixr:field id="96" name="RawData" type="data" lengthId="95"/>)xml",
        R"xml(<fixr:fieldRef id="95"/><fixr:fieldRef id="96"/>)xml");
    std::pmr::monotonic_buffer_resource mr;
    EXPECT_THROW((void)fixpp::dict::OrchestraLoader{}.load_from_string(xml, &mr),
                 fixpp::dict::orchestra_parse_error);
}

TEST(OrchestraFailClosed, LengthIdOnANonDataFieldThrows) {
    auto const xml = repository_with_fields(
        R"xml(<fixr:field id="95" name="RawDataLength" type="Length"/>
              <fixr:field id="58" name="Text" type="String" lengthId="95"/>)xml",
        R"xml(<fixr:fieldRef id="95"/><fixr:fieldRef id="58"/>)xml");
    std::pmr::monotonic_buffer_resource mr;
    EXPECT_THROW((void)fixpp::dict::OrchestraLoader{}.load_from_string(xml, &mr),
                 fixpp::dict::orchestra_parse_error);
}

TEST(OrchestraFailClosed, TwoDataFieldsSharingOneLengthThrow) {
    auto const xml = repository_with_fields(
        R"xml(<fixr:field id="95" name="RawDataLength" type="Length"/>
              <fixr:field id="96" name="RawData" type="data" lengthId="95"/>
              <fixr:field id="97" name="OtherData" type="data" lengthId="95"/>)xml",
        R"xml(<fixr:fieldRef id="95"/><fixr:fieldRef id="96"/>)xml");
    std::pmr::monotonic_buffer_resource mr;
    EXPECT_THROW((void)fixpp::dict::OrchestraLoader{}.load_from_string(xml, &mr),
                 fixpp::dict::orchestra_parse_error);
}

TEST(OrchestraFailClosed, MalformedLengthIdThrows) {
    auto const xml = repository_with_fields(
        R"xml(<fixr:field id="95" name="RawDataLength" type="Length"/>
              <fixr:field id="96" name="RawData" type="data" lengthId="9x"/>)xml",
        R"xml(<fixr:fieldRef id="95"/><fixr:fieldRef id="96"/>)xml");
    std::pmr::monotonic_buffer_resource mr;
    EXPECT_THROW((void)fixpp::dict::OrchestraLoader{}.load_from_string(xml, &mr),
                 fixpp::dict::orchestra_parse_error);
}

// ─────────────────────────────────────────────────────────────────────────
// fixpp#426 (Gate B r9 R-3) — zero can never be half of a Length+Data pair,
// refused at FORMATION rather than only at `table_view::set_length_pair_data_tag`.
// The setter guard is downstream of the loader, so without this a zero-headed
// pair would still reach `Dictionary::length_pair_data_tag` and `field_ref`.
//
// ⚠️ These assert the MESSAGE, not just the exception type. Every neighbouring
// OrchestraFailClosed case throws the same type, so a type-only assertion would
// go green while a DIFFERENT check did the work — the guard here sits ahead of
// both the datatype check and the declared-Length check, so it is the one that
// must fire.
//
// ⚠️ The two cases below assert DIFFERENT mechanisms, and the difference follows
// from where each value comes from. `data_tag` is a key of `fields_by_tag_`,
// which `collect_fields` bars from being zero (fixpp#457); `length_tag` is a
// `lengthId=` reference, parsed with the shared `parse_orchestra_id` — which
// must keep admitting zero for the structural-id namespace — and resolved after
// the declaration check. So one case still exercises the pair guard and the
// other exercises the declaration refusal that precedes it. Re-derive by
// reading the two call sites, not by trusting this note.
namespace {

// Returns the orchestra_parse_error message, or "" if the load did not throw.
std::string orchestra_load_error_message(std::string_view xml_text) {
    std::pmr::monotonic_buffer_resource mr;
    try {
        (void)fixpp::dict::OrchestraLoader{}.load_from_string(xml_text, &mr);
    } catch (fixpp::dict::orchestra_parse_error const& e) {
        return std::string{e.what()};
    }
    return {};
}

}  // namespace

TEST(OrchestraFailClosed, ZeroLengthIdCannotBeHalfOfAPair) {
    // A valid DATA field whose `lengthId=` names 0. Field 0 is no longer
    // declarable (fixpp#457), so the fixture cannot declare it — but `lengthId=`
    // is not a declaration, and `resolve_length_pairs` orders the zero guard
    // AHEAD of both the datatype check and the declared-Length check, so the
    // zero guard is still the one that must fire. That ordering is exactly what
    // the message assertion below discriminates: the dangling-reference check
    // sitting behind it would also reject this document, with a different
    // message, and a type-only assertion could not tell the two apart.
    auto const xml = repository_with_fields(
        R"xml(<fixr:field id="96" name="RawData" type="data" lengthId="0"/>)xml",
        R"xml(<fixr:fieldRef id="96"/>)xml");
    auto const msg = orchestra_load_error_message(xml);
    ASSERT_FALSE(msg.empty()) << "a zero Length half must fail the load closed";
    EXPECT_NE(msg.find("field number 0"), std::string::npos)
        << "the load failed for the WRONG reason — the zero-pair guard did not fire. Message: "
        << msg;
}

// The mirror image: a DATA field numbered 0, with a valid Length partner. The
// declaration refusal precedes the pair guard, so this fixture must fail with
// the declaration's message and NOT the pair guard's. Asserting both directions
// is what keeps the two cases from collapsing into one: relax the declaration
// rule and this goes RED, handing the guard's `data_tag` half back its caller.
TEST(OrchestraFailClosed, ZeroDataFieldIsRefusedAtDeclarationBeforeThePairGuard) {
    auto const xml = repository_with_fields(
        R"xml(<fixr:field id="95" name="RawDataLength" type="Length"/>
              <fixr:field id="0" name="ZeroData" type="data" lengthId="95"/>)xml",
        R"xml(<fixr:fieldRef id="95"/><fixr:fieldRef id="0"/>)xml");
    auto const msg = orchestra_load_error_message(xml);
    ASSERT_FALSE(msg.empty()) << "a zero-numbered DATA field must fail the load closed";
    EXPECT_NE(msg.find("<fixr:field> id must be 1..65535"), std::string::npos)
        << "expected the fixpp#457 DECLARATION refusal. Message: " << msg;
    EXPECT_EQ(msg.find("field number 0"), std::string::npos)
        << "the pair-formation guard fired, which means the declaration rule did not — the "
           "zero reached `resolve_length_pairs`. Message: "
        << msg;
}

// ---------------------------------------------------------------------------
// fixpp#457 — a <fixr:field id="0"> is refused at DECLARATION.
//
// Tightening the declaration is sufficient for the whole loader: no reference
// to a field (`<fixr:fieldRef>`, `<fixr:numInGroup>`, `lengthId=`) can resolve
// to 0, because 0 can no longer be declared — though which guard reports it
// differs by reference kind; `lengthId=` is caught earlier, by the retained
// zero-pair guard in `resolve_length_pairs` (witness
// `ZeroLengthIdCannotBeHalfOfAPair`, above).
// ---------------------------------------------------------------------------
TEST(OrchestraFailClosed, ZeroFieldIdThrows) {
    auto const msg = orchestra_load_error_message(
        repository_with_fields(R"xml(<fixr:field id="0" name="ZeroTag" type="String"/>)xml", ""));
    ASSERT_FALSE(msg.empty()) << "a zero <fixr:field id> must fail the load closed";
    // The message, not just the type: every neighbouring OrchestraFailClosed
    // case throws this same type, so a type-only arm cannot tell the
    // declaration refusal from any other malformation in the fixture.
    EXPECT_NE(msg.find("<fixr:field> id must be 1..65535"), std::string::npos)
        << "refused for the WRONG reason. Message: " << msg;
}

// The false-positive arm. `<fixr:component id>` and `<fixr:group id>` are a
// repository-LOCAL surrogate key — an XML document id, not a FIX tag — and they
// share `parse_orchestra_id` with the field-tag sites. Zero is a legal value
// there, so a rejection placed inside the shared parser (or inside
// `try_parse_uint16`) would retroactively outlaw a valid Orchestra document.
// This arm is what distinguishes the two namespaces; without it, the cheapest
// wrong fix passes every other test in this file.
TEST(OrchestraFailClosed, ZeroStructuralXmlIdsAreStillAccepted) {
    constexpr std::string_view kXml = R"xml(
<fixr:repository version="FIX.Latest_EP303">
  <fixr:fields>
    <fixr:field id="1" name="Account" type="String"/>
    <fixr:field id="100" name="NoLegs" type="NumInGroup"/>
    <fixr:field id="200" name="LegSymbol" type="String"/>
  </fixr:fields>
  <fixr:components>
    <fixr:component id="0" name="ZeroIdComponent">
      <fixr:fieldRef id="1"/>
    </fixr:component>
  </fixr:components>
  <fixr:groups>
    <fixr:group id="0" name="ZeroIdGroup">
      <fixr:numInGroup id="100"/>
      <fixr:fieldRef id="200"/>
    </fixr:group>
  </fixr:groups>
  <fixr:messages>
    <fixr:message id="1" name="Heartbeat" msgType="0">
      <fixr:structure>
        <fixr:componentRef id="0"/>
        <fixr:groupRef id="0"/>
      </fixr:structure>
    </fixr:message>
  </fixr:messages>
</fixr:repository>
)xml";
    std::pmr::monotonic_buffer_resource mr;
    fixpp::dict::OrchestraLoader loader;
    EXPECT_NO_THROW({
        auto dict = loader.load_from_string(kXml, &mr);
        // Non-vacuity: the zero-id component and group must actually have been
        // expanded, not merely tolerated and dropped.
        auto const fields = dict.message_fields("0");
        bool saw_component_field = false;
        bool saw_group_count = false;
        for (auto const& fr : fields) {
            saw_component_field |= fr.tag == 1;
            saw_group_count |= fr.tag == 100;
        }
        EXPECT_TRUE(saw_component_field) << "<fixr:component id='0'> must still expand";
        EXPECT_TRUE(saw_group_count) << "<fixr:group id='0'> must still expand";
    });
}
