// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/capi_group_delimiter_ctx_test.cpp
//
// 083-group-delimiter-resolution T049 — the C-ABI construction-path witnesses
// (FR-018 / FR-018a / FR-018b; contracts/capi_group_grammar.md C-9.1..C-9.5).
//
// One rule on both paths: a message the BUILDER accepts is never rejected by
// the engine's own inbound validation on delimiter grounds, and vice versa.
//
//   W-11  ConstructionAcceptsIffValidationAccepts
//   W-11a CommitDoesNotRebuildTableViewPerMessage
//   W-11b DictFreeHandleStillSkipsDelimiterCheck
//   W-12  DisclosedDelimiterMoveRejectsOldOrder
//   W-13  GroupBeginStillAcceptsEveryRegisteredGroup
//
// Anchors: tasks.md T049/T053; contracts/capi_group_grammar.md; research.md D-10/D-13.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"
#include "capi_loopback_support.hpp"
#include "dictionary_internal.hpp"  // 083 T049: as_table_view_call_count() seam

#include "fixpp/dict/dictionary.hpp"
#include "fixpp/dict/orchestra_loader.hpp"
#include "fixpp/dict/xml_loader.hpp"

using namespace fixpp::capi_test;
using fixpp::dict::Dictionary;
using fixpp::dict::field_data_type;
using fixpp::dict::XmlLoader;

namespace {

// ── The DIVERGENT NESTED fixture (W-11) ─────────────────────────────────────
// `NoInner(500)` appears in BOTH messages, nested one level inside
// `NoWrap(400)` — so its context path is [400], NON-EMPTY, and a key off by one
// path element would produce a different key rather than an accidentally equal
// one. Its declaration order DIFFERS between the two messages:
//
//   D : NoWrap(400){ WrapA(401), NoInner(500){ InnerX(501), InnerY(502) } }
//         -> ("D", [400], 500) delimiter = 501
//   E : NoWrap(400){ WrapA(401), NoInner(500){ InnerY(502), InnerX(501) } }
//         -> ("E", [400], 500) delimiter = 502
//
// D is lexically first, so the dictionary-GLOBAL first-seen value for 500 is
// 501. In message E the context-correct delimiter (502) therefore DIFFERS from
// the global — which is what makes a fall-through to the global observable at
// all. A non-divergent fixture would pass either way and witness nothing.
constexpr std::string_view kDivergentNestedXml = R"xml(
<fix type="FIX" major="4" minor="2" servicepack="0">
  <header>
    <field name="BeginString"  required="Y"/>
    <field name="BodyLength"   required="Y"/>
    <field name="MsgType"      required="Y"/>
    <field name="SenderCompID" required="Y"/>
    <field name="TargetCompID" required="Y"/>
    <field name="MsgSeqNum"    required="Y"/>
    <field name="SendingTime"  required="Y"/>
  </header>
  <trailer>
    <field name="CheckSum" required="Y"/>
  </trailer>
  <fields>
    <field number="8"   name="BeginString"  type="STRING"/>
    <field number="9"   name="BodyLength"   type="INT"/>
    <field number="10"  name="CheckSum"     type="STRING"/>
    <field number="34"  name="MsgSeqNum"    type="SEQNUM"/>
    <field number="35"  name="MsgType"      type="STRING"/>
    <field number="49"  name="SenderCompID" type="STRING"/>
    <field number="52"  name="SendingTime"  type="UTCTIMESTAMP"/>
    <field number="56"  name="TargetCompID" type="STRING"/>
    <field number="11"  name="ClOrdID"      type="STRING"/>
    <field number="112" name="TestReqID"    type="STRING"/>
    <field number="400" name="NoWrap"       type="NUMINGROUP"/>
    <field number="401" name="WrapA"        type="STRING"/>
    <field number="500" name="NoInner"      type="NUMINGROUP"/>
    <field number="501" name="InnerX"       type="STRING"/>
    <field number="502" name="InnerY"       type="STRING"/>
  </fields>
  <messages>
    <message name="Heartbeat" msgtype="0" msgcat="admin">
      <field name="TestReqID" required="N"/>
    </message>
    <message name="MsgD" msgtype="D" msgcat="app">
      <field name="ClOrdID" required="N"/>
      <group name="NoWrap" required="N">
        <field name="WrapA" required="N"/>
        <group name="NoInner" required="N">
          <field name="InnerX" required="N"/>
          <field name="InnerY" required="N"/>
        </group>
      </group>
    </message>
    <message name="MsgE" msgtype="E" msgcat="app">
      <field name="ClOrdID" required="N"/>
      <group name="NoWrap" required="N">
        <field name="WrapA" required="N"/>
        <group name="NoInner" required="N">
          <field name="InnerY" required="N"/>
          <field name="InnerX" required="N"/>
        </group>
      </group>
    </message>
  </messages>
</fix>
)xml";

// Builds a session config over an inline dictionary, mirroring
// message_write_test.cpp's PMR/shared_ptr pattern.
fixpp_session_config_t* make_cfg_with_dict(std::string_view xml, const char* sender,
                                           const char* target) {
    constexpr std::size_t kBufSize = 256u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    Dictionary d = XmlLoader{}.load_from_string(xml, mr);
    auto* raw_dict = new Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    auto dict_ptr = std::shared_ptr<const Dictionary>{raw_dict,
                                                      [mr, raw_buf](const Dictionary* p) {
                                                          delete p;
                                                          delete mr;
                                                          delete raw_buf;
                                                      }};
    auto* fd = new fixpp_dict{dict_ptr};
    auto* dict_handle = reinterpret_cast<fixpp_dict_t*>(fd);

    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_ACCEPTOR), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, dict_handle), FIXPP_ERR_OK);
    delete fd;
    return sc;
}

// Same, from a shipped dictionary FILE (W-12 runs on the real FIX44).
fixpp_session_config_t* make_cfg_from_file(const char* filename, const char* begin_string,
                                           const char* sender, const char* target) {
    constexpr std::size_t kArena = 64u * 1024u * 1024u;
    auto* storage = new std::byte[kArena];
    auto* mr = new std::pmr::monotonic_buffer_resource{storage, kArena};
    Dictionary d = XmlLoader{}.load(std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename, mr);
    auto* raw_dict = new Dictionary{std::move(d)};
    auto dict_ptr = std::shared_ptr<const Dictionary>{raw_dict,
                                                      [mr, storage](const Dictionary* p) {
                                                          delete p;
                                                          delete mr;
                                                          delete[] storage;
                                                      }};
    auto* fd = new fixpp_dict{dict_ptr};
    auto* dict_handle = reinterpret_cast<fixpp_dict_t*>(fd);

    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, begin_string), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_ACCEPTOR), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, dict_handle), FIXPP_ERR_OK);
    delete fd;
    return sc;
}

// Same, with NO dictionary configured (W-11b).
fixpp_session_config_t* make_cfg_dict_free(const char* sender, const char* target) {
    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_ACCEPTOR), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    return sc;
}

// Builds `35=E` carrying NoWrap(400) x1 whose nested NoInner(500) instance opens
// with `inner_first`, then commits. Returns the commit's error code.
fixpp_error_t build_and_commit_nested(fixpp_session_t* sess, uint16_t inner_first,
                                      uint16_t inner_second) {
    fixpp_msg_t* msg = nullptr;
    EXPECT_EQ(fixpp_msg_create_outbound(sess, "E", 1, &msg), FIXPP_ERR_OK);
    if (msg == nullptr) {
        return FIXPP_ERR_INVALID_HANDLE;
    }
    fixpp_group_builder_t* outer = nullptr;
    EXPECT_EQ(fixpp_msg_group_begin(msg, 400, &outer), FIXPP_ERR_OK);
    fixpp_entry_t* oe = nullptr;
    EXPECT_EQ(fixpp_group_builder_add_entry(outer, &oe), FIXPP_ERR_OK);
    // NoWrap's own delimiter is WrapA(401) in BOTH messages — held correct so
    // the only thing under test is the NESTED context's delimiter.
    EXPECT_EQ(fixpp_entry_set_string(oe, 401, "W", 1), FIXPP_ERR_OK);

    fixpp_group_builder_t* inner = nullptr;
    EXPECT_EQ(fixpp_entry_group_begin(oe, 500, &inner), FIXPP_ERR_OK);
    fixpp_entry_t* ie = nullptr;
    EXPECT_EQ(fixpp_group_builder_add_entry(inner, &ie), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_entry_set_string(ie, inner_first, "A", 1), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_entry_set_string(ie, inner_second, "B", 1), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_msg_group_end(msg, inner), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_msg_group_end(msg, outer), FIXPP_ERR_OK);

    const uint8_t* out = nullptr;
    std::size_t len = 0;
    fixpp_error_t const rc = fixpp_msg_commit(msg, &out, &len);
    fixpp_msg_destroy(msg);
    return rc;
}

struct SessionFixture {
    fixpp_engine_t* eng = nullptr;
    fixpp_session_t* sess = nullptr;
};

fixpp_error_t make_engine(fixpp_engine_t** out) {
    return fixpp_engine_create(make_engine_cfg(), 1, 0, out);
}

SessionFixture open_session(fixpp_session_config_t* sc) {
    SessionFixture f;
    EXPECT_EQ(make_engine(&f.eng), FIXPP_ERR_OK);
    set_loopback_endpoint(sc, "127.0.0.1", 0);
    EXPECT_EQ(fixpp_session_open(f.eng, sc, &f.sess), FIXPP_ERR_OK);
    return f;
}

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
// W-11 — construction accepts iff validation accepts, BOTH directions, on a
// DIVERGENT context under a NON-EMPTY ancestor path (C-9.1 / C-9.2).
// ============================================================================
TEST(CapiGroupDelimiterCtx, ConstructionAcceptsIffValidationAccepts) {
    // Fixture pin, asserted rather than assumed: the context under test really
    // IS divergent, i.e. its own delimiter differs from the dictionary-global
    // first-seen value. Without this the case would pass on a fixture where a
    // fall-through to the global is unobservable, and witness nothing.
    {
        constexpr std::size_t kBufSize = 256u * 1024u;
        auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
        std::pmr::monotonic_buffer_resource mr{buf->data(), buf->size()};
        auto const dict = XmlLoader{}.load_from_string(kDivergentNestedXml, &mr);
        auto const tv = dict.as_table_view();
        std::array<uint16_t, 1> const path400{400};
        EXPECT_EQ(dict.group_first_field(500), 501)
            << "fixture: the dictionary-GLOBAL first-seen delimiter for 500 must be D's (501).";
        EXPECT_EQ(tv.group_first_field("E", path400, uint16_t{500}), 502)
            << "fixture: E's own context ([400], 500) must resolve to 502 — otherwise this "
               "context is not divergent and the case witnesses nothing.";
    }

    // Direction 1: the CONTEXT-CORRECT opening tag (502) must COMMIT.
    {
        auto f = open_session(make_cfg_with_dict(kDivergentNestedXml, "CTXA", "CTXB"));
        EXPECT_EQ(build_and_commit_nested(f.sess, /*inner_first=*/502, /*inner_second=*/501),
                  FIXPP_ERR_OK)
            << "FR-018 / C-9.1: the builder must accept the order message E's OWN declaration "
               "defines (delimiter 502). Rejecting it would mean construction disagrees with "
               "inbound validation, which accepts exactly this order.";
        fixpp_engine_destroy(f.eng);
    }

    // Direction 2: the GLOBAL-first-seen opening tag (501) must be REJECTED.
    // This is the half that fails against a context-free resolution: pre-T052
    // the bare global says 501, so the wrong order was accepted and the right
    // one rejected — exactly inverted.
    {
        auto f = open_session(make_cfg_with_dict(kDivergentNestedXml, "CTXC", "CTXD"));
        EXPECT_EQ(build_and_commit_nested(f.sess, /*inner_first=*/501, /*inner_second=*/502),
                  FIXPP_ERR_TYPE_MISMATCH)
            << "FR-018: opening E's nested group with the DIFFERENT context's delimiter (501, "
               "D's) must be rejected at commit — inbound validation rejects it too.";
        fixpp_engine_destroy(f.eng);
    }
}

// ============================================================================
// W-11a — the table_view is built ONCE per opened session and NEVER per
// message (C-9.2a / D-13; `dict->as_table_view()` in the commit path is barred
// by [const §XV.1], a constitution violation rather than a slow path).
// ============================================================================
TEST(CapiGroupDelimiterCtx, CommitDoesNotRebuildTableViewPerMessage) {
    fixpp::dict::detail::reset_as_table_view_call_count();

    auto f = open_session(make_cfg_with_dict(kDivergentNestedXml, "CNTA", "CNTB"));
    auto const after_open = fixpp::dict::detail::as_table_view_call_count();

    // The session builds exactly one view at open. (Constructing the fixture
    // dictionary itself does not call as_table_view, so this is attributable.)
    EXPECT_EQ(after_open, 1u)
        << "C-9.2a: fixpp_session_open must build the session's table_view exactly ONCE.";

    constexpr int kMessages = 5;
    for (int i = 0; i < kMessages; ++i) {
        EXPECT_EQ(build_and_commit_nested(f.sess, 502, 501), FIXPP_ERR_OK) << "message " << i;
    }

    EXPECT_EQ(fixpp::dict::detail::as_table_view_call_count(), after_open)
        << "C-9.2a / [const §XV.1]: committing " << kMessages
        << " group-bearing messages must add ZERO as_table_view() calls. A per-message rebuild "
           "would be a constitution violation, not merely a slow path — and it is invisible to "
           "every functional assertion, which is why this counter exists.";
    fixpp_engine_destroy(f.eng);
}

// ============================================================================
// W-11b — a DICT-FREE handle still skips the delimiter check entirely (C-9.4).
// "No dictionary" and "no view" are ONE state, so the dict-free disposition is
// unchanged by this feature.
// ============================================================================
TEST(CapiGroupDelimiterCtx, DictFreeHandleStillSkipsDelimiterCheck) {
    auto f = open_session(make_cfg_dict_free("DFA", "DFB"));

    fixpp_msg_t* msg = nullptr;
    // With no dictionary the msgtype is not validated either; "0" is accepted.
    ASSERT_EQ(fixpp_msg_create_outbound(f.sess, "0", 1, &msg), FIXPP_ERR_OK);
    ASSERT_NE(msg, nullptr);

    // MEASURED, correcting this case's first draft: a dict-free handle CAN open
    // a group. `group_begin`'s is-this-a-group predicate is dict-gated, and with
    // no dictionary there is nothing to reject against — so the builder is
    // permissive here and always has been. What C-9.4 actually says is that the
    // DELIMITER check is skipped, and that is what this case must pin.
    fixpp_group_builder_t* b = nullptr;
    ASSERT_EQ(fixpp_msg_group_begin(msg, 400, &b), FIXPP_ERR_OK)
        << "C-9.4: group_begin is dict-gated; with no dictionary it does not reject.";
    fixpp_entry_t* e = nullptr;
    ASSERT_EQ(fixpp_group_builder_add_entry(b, &e), FIXPP_ERR_OK);
    // Open the instance with a tag that would be the WRONG delimiter under the
    // divergent dictionary (501 is D's, not E's). With no dictionary this must
    // still commit: there is no delimiter to check against.
    ASSERT_EQ(fixpp_entry_set_string(e, 501, "A", 1), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_group_end(msg, b), FIXPP_ERR_OK);

    const uint8_t* out = nullptr;
    std::size_t len = 0;
    EXPECT_EQ(fixpp_msg_commit(msg, &out, &len), FIXPP_ERR_OK)
        << "C-9.4: a dict-free handle must still commit — the delimiter check is SKIPPED, not "
           "failed closed. T052 added a fail-closed branch for a dictionary-PRESENT handle that "
           "cannot reach the session view; if that branch ever widened to cover the dict-free "
           "case it would break every dictionary-less C-ABI client, and this is the pin.";
    fixpp_msg_destroy(msg);
    fixpp_engine_destroy(f.eng);
}

// ============================================================================
// W-12 — a DISCLOSED delimiter move rejects the OLD construction order and
// accepts the new one, on a REAL shipped dictionary (FR-019 class (b)).
//
// FIX44 `CollateralRequest(AX)` / `NoExecs(124)`: the pre-083 engine resolved
// this context's delimiter from the dictionary-global first-seen value
// **32 (LastQty)**; AX's own declaration says **17 (ExecID)**. So an integrator
// who built this group opening with 32 was accepted before and is rejected now
// — which is exactly the behaviour change T069 discloses, and C-9.7 records
// that the old order was never schema-valid under declaration order, so SC-007
// is not violated.
// ============================================================================
TEST(CapiGroupDelimiterCtx, DisclosedDelimiterMoveRejectsOldOrder) {
    auto build_ax = [](fixpp_session_t* sess, uint16_t first_tag) {
        fixpp_msg_t* msg = nullptr;
        EXPECT_EQ(fixpp_msg_create_outbound(sess, "AX", 2, &msg), FIXPP_ERR_OK);
        if (msg == nullptr) {
            return FIXPP_ERR_INVALID_HANDLE;
        }
        fixpp_group_builder_t* b = nullptr;
        EXPECT_EQ(fixpp_msg_group_begin(msg, 124, &b), FIXPP_ERR_OK);
        fixpp_entry_t* e = nullptr;
        EXPECT_EQ(fixpp_group_builder_add_entry(b, &e), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_entry_set_string(e, first_tag, "X", 1), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_msg_group_end(msg, b), FIXPP_ERR_OK);
        const uint8_t* out = nullptr;
        std::size_t len = 0;
        fixpp_error_t const rc = fixpp_msg_commit(msg, &out, &len);
        fixpp_msg_destroy(msg);
        return rc;
    };

    // Fixture pin: this really is a disclosed MOVE — global says 32, AX says 17.
    {
        constexpr std::size_t kArena = 64u * 1024u * 1024u;
        auto storage = std::make_unique<std::byte[]>(kArena);
        std::pmr::monotonic_buffer_resource mr{storage.get(), kArena};
        auto const dict =
            XmlLoader{}.load(std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml", &mr);
        auto const tv = dict.as_table_view();
        std::array<uint16_t, 0> const root{};
        ASSERT_EQ(tv.group_first_field("AX", root, uint16_t{124}), 17)
            << "fixture: AX/NoExecs(124) must resolve to its OWN declaration order (17/ExecID).";
    }

    {
        auto f = open_session(make_cfg_from_file("FIX44.xml", "FIX.4.4", "W12A", "W12B"));
        EXPECT_EQ(build_ax(f.sess, 17), FIXPP_ERR_OK)
            << "W-12: the NEW (correct) opening tag 17/ExecID must be accepted.";
        fixpp_engine_destroy(f.eng);
    }
    {
        auto f = open_session(make_cfg_from_file("FIX44.xml", "FIX.4.4", "W12C", "W12D"));
        EXPECT_EQ(build_ax(f.sess, 32), FIXPP_ERR_TYPE_MISMATCH)
            << "W-12: the OLD opening tag 32/LastQty — accepted pre-083 because the engine used "
               "the dictionary-GLOBAL first-seen delimiter — must now be rejected. This is the "
               "disclosed behaviour change (FR-019 class (b) / T069).";
        fixpp_engine_destroy(f.eng);
    }
}

// ============================================================================
// W-13 (T053) — `group_begin`'s is-this-a-group PREDICATE still answers for
// EVERY registered group, in all ten dictionaries.
//
// This is D-10's total-regression pin. T028/T030 delete the one-level component
// scan that used to populate the global `GroupDef.first_field_tag`, and the
// same global is what `src/capi/message_write.cpp:157`, `:812` and `:923` use
// as a bare predicate. If the repopulating projection ever leaves it 0, the C
// ABI's group_begin rejects EVERY group through a GA-frozen ABI — and the whole
// hazard is that this would otherwise be found by a client after release.
//
// C-9.5: those three sites are deliberately NOT converted to context-keyed
// lookups. Making them so would reject a group whose context the caller has not
// yet established, which is every group.
// ============================================================================
TEST(CapiGroupDelimiterCtx, GroupBeginStillAcceptsEveryRegisteredGroup) {
    std::size_t total_groups = 0;
    for (auto const& d : kAllTen) {
        constexpr std::size_t kArena = 64u * 1024u * 1024u;
        auto storage = std::make_unique<std::byte[]>(kArena);
        std::pmr::monotonic_buffer_resource mr{storage.get(), kArena};
        auto const path = d.is_orchestra
                              ? std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / d.filename
                              : std::filesystem::path{FIXPP_DICT_DATA_DIR} / d.filename;
        auto const dict = d.is_orchestra ? fixpp::dict::OrchestraLoader{}.load(path, &mr)
                                         : XmlLoader{}.load(path, &mr);

        std::set<uint16_t> real_groups;
        for (auto const& msg : dict.messages()) {
            auto const fields = dict.message_fields(msg.msg_type);
            std::set<uint16_t> nig;
            std::set<uint16_t> has_members;
            for (auto const& fr : fields) {
                if (fr.type == field_data_type::NumInGroup) {
                    nig.insert(fr.tag);
                }
                if (fr.group_no_tag != 0) {
                    has_members.insert(fr.group_no_tag);
                }
            }
            for (auto const g : nig) {
                if (has_members.count(g) != 0) {
                    real_groups.insert(g);
                }
            }
        }
        for (auto const g : real_groups) {
            ++total_groups;
            EXPECT_NE(dict.group_first_field(g), 0)
                << "C-9.5 / D-10: " << d.label << " group " << g
                << " answers 0 to the bare is-this-a-group predicate, so the C ABI's "
                   "fixpp_msg_group_begin would REJECT it — through a GA-frozen ABI. The "
                   "projection that repopulates GroupDef.first_field_tag (T028/T030) must never "
                   "leave a reachable group at 0.";
        }
    }
    // Non-vacuity: an empty sweep would pass silently.
    EXPECT_GT(total_groups, 1000u)
        << "W-13 censused only " << total_groups
        << " groups across all ten dictionaries — far too few; the sweep is not reaching them.";
}
