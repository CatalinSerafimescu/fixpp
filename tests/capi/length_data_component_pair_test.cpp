// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/length_data_component_pair_test.cpp — 091 (fixpp#418), FR-019
//
// C-ABI witnesses for a dictionary whose custom LENGTH/DATA pair (5001/5002) is
// adjacent ONLY inside a <component> definition, loaded through
// fixpp_dict_load_from_xml (the entry point dict.h's BREAKING note marks). The
// two fields are not adjacent in <fields> order and are never direct <field>
// children of any message, header or trailer, so pair-ness comes from the
// component alone (contracts/codegen-builders.md C-2.5a's global
// preconditions).
//
// T008 (outbound message API): a Data value written without its Length is
// refused at commit; a well-formed pair commits; a SOH-bearing value on the
// Data tag is accepted; fixpp_msg_set_data with len == 0 is refused as a
// malformed value (its len == 0 check precedes the declaration check in
// src/capi/message_write.cpp — re-derive by reading fixpp_msg_set_data).
//
// T009 (session send): over a shipped FIX 4.4 dictionary with the same
// component injected and referenced from NewOrderSingle, a payload whose Data
// count does not end on SOH is refused as malformed.

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory_resource>
#include <string>
#include <string_view>

#include "capi_loopback_support.hpp"
#include "fix/c_api/dict.h"
#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"
#include "fix/c_api/version.h"
#include "fixpp/dict/dictionary.hpp"
#include "fixpp/dict/xml_loader.hpp"

using namespace fixpp::capi_test;

namespace {

// '|' stands for SOH, so no hex escape can swallow the digits that follow it.
std::string soh(std::string_view s) {
    std::string out{s};
    for (char& c : out) {
        if (c == '|') {
            c = '\x01';
        }
    }
    return out;
}

// A FIX 4.2 session dictionary. CustomPairLen(5001) and CustomPairData(5002) are
// separated in <fields> by Text(58) and appear together only in component
// CustomPair, which NewOrderSingle references.
constexpr std::string_view kComponentPairFix42Xml = R"xml(
<fix major="4" minor="2">
  <header>
    <field name="BeginString" required="Y"/>
    <field name="BodyLength" required="Y"/>
    <field name="MsgType" required="Y"/>
    <field name="SenderCompID" required="Y"/>
    <field name="TargetCompID" required="Y"/>
    <field name="MsgSeqNum" required="Y"/>
    <field name="SendingTime" required="Y"/>
  </header>
  <trailer>
    <field name="CheckSum" required="Y"/>
  </trailer>
  <messages>
    <message name="Heartbeat" msgtype="0" msgcat="admin">
      <field name="TestReqID" required="N"/>
    </message>
    <message name="NewOrderSingle" msgtype="D" msgcat="app">
      <field name="ClOrdID" required="N"/>
      <component name="CustomPair" required="N"/>
    </message>
  </messages>
  <components>
    <component name="CustomPair">
      <field name="CustomPairLen" required="N"/>
      <field name="CustomPairData" required="N"/>
    </component>
  </components>
  <fields>
    <field number="8" name="BeginString" type="STRING"/>
    <field number="9" name="BodyLength" type="LENGTH"/>
    <field number="10" name="CheckSum" type="STRING"/>
    <field number="11" name="ClOrdID" type="STRING"/>
    <field number="34" name="MsgSeqNum" type="SEQNUM"/>
    <field number="35" name="MsgType" type="STRING"/>
    <field number="49" name="SenderCompID" type="STRING"/>
    <field number="52" name="SendingTime" type="UTCTIMESTAMP"/>
    <field number="56" name="TargetCompID" type="STRING"/>
    <field number="112" name="TestReqID" type="STRING"/>
    <field number="5001" name="CustomPairLen" type="LENGTH"/>
    <field number="58" name="Text" type="STRING"/>
    <field number="5002" name="CustomPairData" type="DATA"/>
  </fields>
</fix>
)xml";

// Writes `xml` to a per-process, per-test temp file, loads it through
// fixpp_dict_load_from_xml, and removes the file.
fixpp_dict_t* load_dict_from_xml_file(std::string_view xml) {
    auto const* info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::filesystem::path const path = std::filesystem::temp_directory_path() /
                                       ("fixpp_091_" + std::to_string(::getpid()) + "_" +
                                        info->test_suite_name() + "_" + info->name() + ".xml");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(xml.data(), static_cast<std::streamsize>(xml.size()));
    }
    fixpp_dict_t* d = nullptr;
    EXPECT_EQ(fixpp_dict_load_from_xml(path.c_str(), &d), FIXPP_ERR_OK);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return d;
}

fixpp_session_config_t* make_cfg(fixpp_dict_t* dict, const char* sender, const char* target,
                                 const char* begin_string, fixpp_session_role role) {
    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, begin_string), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, role), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(sc, role == FIXPP_ROLE_INITIATOR),
              FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, dict), FIXPP_ERR_OK);
    return sc;
}

// An open (not established) outbound NewOrderSingle over kComponentPairFix42Xml.
struct ComponentPairMsg {
    fixpp_engine_t* eng = nullptr;
    fixpp_session_t* sess = nullptr;
    fixpp_msg_t* msg = nullptr;
    ComponentPairMsg() {
        EXPECT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                      FIXPP_C_ABI_VERSION_MINOR, &eng),
                  FIXPP_ERR_OK);
        fixpp_dict_t* d = load_dict_from_xml_file(kComponentPairFix42Xml);
        fixpp_session_config_t* sc = make_cfg(d, "CLI", "SRV", "FIX.4.2", FIXPP_ROLE_INITIATOR);
        fixpp_dict_destroy(d);  // the setter copied the shared_ptr
        set_loopback_endpoint(sc, "127.0.0.1", 0);
        EXPECT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_msg_create_outbound(sess, "D", 1, &msg), FIXPP_ERR_OK);
    }
    ~ComponentPairMsg() {
        if (msg) fixpp_msg_destroy(msg);
        if (eng) fixpp_engine_destroy(eng);
    }
    ComponentPairMsg(ComponentPairMsg const&) = delete;
    ComponentPairMsg& operator=(ComponentPairMsg const&) = delete;

    fixpp_error_t set_string(uint16_t tag, std::string_view v) {
        return fixpp_msg_set_string(msg, tag, v.data(), v.size());
    }
    fixpp_error_t commit(std::string& payload) {
        const uint8_t* p = nullptr;
        size_t n = 0;
        fixpp_error_t const rc = fixpp_msg_commit(msg, &p, &n);
        payload = rc == FIXPP_ERR_OK ? std::string{reinterpret_cast<const char*>(p), n} : "";
        return rc;
    }
};

}  // namespace

// ── T008 ────────────────────────────────────────────────────────────────────

TEST(CapiComponentPair, DataWithoutItsLengthIsRefusedAtCommit) {
    ComponentPairMsg f;
    ASSERT_NE(f.msg, nullptr);
    std::string const value = "abc";
    ASSERT_EQ(f.set_string(11, "ORD1"), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_string(5002, value), FIXPP_ERR_OK);
    std::string payload;
    EXPECT_EQ(f.commit(payload), FIXPP_ERR_WIRE_CONFORMANCE)
        << "a Data field with no Length before it must be refused; committed payload: " << payload;
}

TEST(CapiComponentPair, WellFormedPairCommits) {
    ComponentPairMsg f;
    ASSERT_NE(f.msg, nullptr);
    ASSERT_EQ(f.set_string(11, "ORD1"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 5001, 3), FIXPP_ERR_OK);
    ASSERT_EQ(f.set_string(5002, "abc"), FIXPP_ERR_OK);
    std::string payload;
    ASSERT_EQ(f.commit(payload), FIXPP_ERR_OK);
    EXPECT_NE(payload.find(soh("|5001=3|5002=abc|")), std::string::npos) << payload;
}

TEST(CapiComponentPair, SohInTheDataValueIsAccepted) {
    ComponentPairMsg f;
    ASSERT_NE(f.msg, nullptr);
    std::string const value = soh("a|b");
    ASSERT_EQ(value.size(), 3U);
    ASSERT_EQ(f.set_string(11, "ORD1"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_int(f.msg, 5001, 3), FIXPP_ERR_OK);
    EXPECT_EQ(f.set_string(5002, value), FIXPP_ERR_OK)
        << "set_string must accept SOH in the Data half of a dictionary pair";
    std::string payload;
    EXPECT_EQ(f.commit(payload), FIXPP_ERR_OK);
}

TEST(CapiComponentPair, SetDataWithZeroLengthIsRefusedAsMalformed) {
    ComponentPairMsg f;
    ASSERT_NE(f.msg, nullptr);
    uint8_t const byte = 'x';  // non-null: a null pointer is refused earlier, for another reason
    EXPECT_EQ(fixpp_msg_set_data(f.msg, 5002, &byte, 0), FIXPP_ERR_WIRE_CONFORMANCE);
}

// ── T009 ────────────────────────────────────────────────────────────────────

namespace {

// Replaces the first `anchor` in `s` with `anchor + insert` (or `insert +
// anchor` when `before`); fails the test when the anchor is absent.
bool inject(std::string& s, std::string_view anchor, std::string_view insert, bool before) {
    auto const pos = s.find(anchor);
    if (pos == std::string::npos) {
        ADD_FAILURE() << "injection anchor not found: " << anchor;
        return false;
    }
    s.insert(before ? pos : pos + anchor.size(), insert);
    return true;
}

// The shipped FIX 4.4 dictionary with component CustomPair (5001/5002)
// declared first under <components> and referenced first in NewOrderSingle.
// 5001 is declared first in <fields> and 5002 last, so they are not adjacent
// there. The shipped file is single-quoted; the anchors use its spelling.
std::string fix44_with_component_pair() {
    std::ifstream in(std::string{FIXPP_DICT_DATA_DIR} + "/FIX44.xml", std::ios::binary);
    std::string s{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    EXPECT_FALSE(s.empty()) << "could not read FIX44.xml";
    bool ok = true;
    ok &= inject(s, "<fields>", "<field number='5001' name='CustomPairLen' type='LENGTH'/>",
                 /*before=*/false);
    ok &= inject(s, "</fields>", "<field number='5002' name='CustomPairData' type='DATA'/>",
                 /*before=*/true);
    ok &= inject(s, "<components>",
                 "<component name='CustomPair'><field name='CustomPairLen' required='N'/>"
                 "<field name='CustomPairData' required='N'/></component>",
                 /*before=*/false);
    ok &= inject(s, "<message name='NewOrderSingle' msgtype='D' msgcat='app'>",
                 "<component name='CustomPair' required='N'/>", /*before=*/false);
    return ok ? s : std::string{};
}

}  // namespace

TEST(CapiComponentPairSend, DataCountNotEndingOnSohIsRefused) {
    std::string const xml = fix44_with_component_pair();
    ASSERT_FALSE(xml.empty());

    // Preconditions on the injected dictionary: NewOrderSingle declares both
    // halves, and the two are not adjacent in <fields>.
    {
        std::pmr::monotonic_buffer_resource mr;
        auto const dict = fixpp::dict::XmlLoader{}.load_from_string(xml, &mr);
        ASSERT_NE(dict.field_ref("D", 5001).rule, fixpp::dict::field_presence::NotDeclared);
        ASSERT_NE(dict.field_ref("D", 5002).rule, fixpp::dict::field_presence::NotDeclared);
        auto const len_decl = xml.find("<field number='5001'");
        auto const data_decl = xml.find("<field number='5002'");
        ASSERT_NE(len_decl, std::string::npos);
        ASSERT_NE(data_decl, std::string::npos);
        auto const after_len = xml.find("<field number=", len_decl + 1);
        ASSERT_NE(after_len, data_decl) << "5001 and 5002 must not be adjacent in <fields>";
    }

    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                  FIXPP_C_ABI_VERSION_MINOR, &B),
              FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                  FIXPP_C_ABI_VERSION_MINOR, &A),
              FIXPP_ERR_OK);

    fixpp_dict_t* d = load_dict_from_xml_file(xml);
    ASSERT_NE(d, nullptr);
    fixpp_session_config_t* acc = make_cfg(d, "ACC-CP", "INI-CP", "FIX.4.4", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);
    std::uint16_t const port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0U);

    fixpp_session_config_t* ini = make_cfg(d, "INI-CP", "ACC-CP", "FIX.4.4", FIXPP_ROLE_INITIATOR);
    fixpp_dict_destroy(d);  // both setters copied the shared_ptr
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    EXPECT_TRUE(wait_for_established(ini_h));
    EXPECT_TRUE(wait_for_established(acc_h));

    std::string const payload = soh("35=D|5001=2|5002=abc|");
    EXPECT_EQ(
        fixpp_session_send(ini_h, reinterpret_cast<const uint8_t*>(payload.data()), payload.size()),
        FIXPP_ERR_APP_PAYLOAD_MALFORMED);

    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}
