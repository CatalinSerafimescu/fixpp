// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/length_data_capi_support.hpp — fixpp#428 test dictionary + session seam.
//
// A FIX 4.2 dictionary whose NewOrderSingle ("D") carries the Length+Data shapes
// the C-ABI Data setters and the commit-time pair check must handle:
//   - EncodedTextLen(354)/EncodedText(355) at top level (a standard pair);
//   - NoAllocs(78) whose instances hold EncodedAllocTextLen(360)/EncodedAllocText(361);
//   - a custom pair 5001/5002 the standard table does not name;
//   - a custom group 5000 whose FIRST field (its delimiter) is the Data field 5004;
//   - Text(58), a STRING field.
// RawDataLength(95)/RawData(96) are declared as fields but not used by "D", so a
// Data setter on 96 reaches the MsgType-grammar check.
//
// The XmlLoader pairs a LENGTH field with the DATA field declared immediately
// after it in <fields>, so each pair below is declared adjacently there.

#pragma once

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>

#include "capi_internal.hpp"
#include "capi_loopback_support.hpp"
#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"
#include "fix/c_api/version.h"
#include "fixpp/dict/dictionary.hpp"
#include "fixpp/dict/xml_loader.hpp"

namespace fixpp::capi_test {

inline constexpr std::string_view kLengthDataFix42Xml = R"xml(
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
      <field name="ClOrdID" required="Y"/>
      <field name="Symbol" required="Y"/>
      <field name="Text" required="N"/>
      <field name="EncodedTextLen" required="N"/>
      <field name="EncodedText" required="N"/>
      <field name="CustomDataLen" required="N"/>
      <field name="CustomData" required="N"/>
      <group name="NoAllocs" required="N">
        <field name="AllocAccount" required="N"/>
        <field name="EncodedAllocTextLen" required="N"/>
        <field name="EncodedAllocText" required="N"/>
      </group>
      <group name="NoCustomBlobs" required="N">
        <field name="CustomBlob" required="N"/>
        <field name="CustomBlobLen" required="N"/>
      </group>
    </message>
  </messages>
  <fields>
    <field number="8" name="BeginString" type="STRING"/>
    <field number="9" name="BodyLength" type="LENGTH"/>
    <field number="10" name="CheckSum" type="STRING"/>
    <field number="11" name="ClOrdID" type="STRING"/>
    <field number="34" name="MsgSeqNum" type="SEQNUM"/>
    <field number="35" name="MsgType" type="STRING"/>
    <field number="49" name="SenderCompID" type="STRING"/>
    <field number="52" name="SendingTime" type="UTCTIMESTAMP"/>
    <field number="55" name="Symbol" type="STRING"/>
    <field number="56" name="TargetCompID" type="STRING"/>
    <field number="58" name="Text" type="STRING"/>
    <field number="78" name="NoAllocs" type="NUMINGROUP"/>
    <field number="79" name="AllocAccount" type="STRING"/>
    <field number="95" name="RawDataLength" type="LENGTH"/>
    <field number="96" name="RawData" type="DATA"/>
    <field number="112" name="TestReqID" type="STRING"/>
    <field number="354" name="EncodedTextLen" type="LENGTH"/>
    <field number="355" name="EncodedText" type="DATA"/>
    <field number="360" name="EncodedAllocTextLen" type="LENGTH"/>
    <field number="361" name="EncodedAllocText" type="DATA"/>
    <field number="5000" name="NoCustomBlobs" type="NUMINGROUP"/>
    <field number="5001" name="CustomDataLen" type="LENGTH"/>
    <field number="5002" name="CustomData" type="DATA"/>
    <field number="5003" name="CustomBlobLen" type="LENGTH"/>
    <field number="5004" name="CustomBlob" type="DATA"/>
  </fields>
</fix>
)xml";

// A plaintext session config over kLengthDataFix42Xml (L-050-1 dictionary seam).
// The endpoint is set separately via set_loopback_endpoint (L-050-5).
inline fixpp_session_config_t* make_length_data_session_cfg(const char* sender, const char* target,
                                                            fixpp_session_role role) {
    constexpr std::size_t kBufSize = 128U * 1024U;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    auto* raw_dict = new fixpp::dict::Dictionary{
        fixpp::dict::XmlLoader{}.load_from_string(kLengthDataFix42Xml, mr)};
    auto* raw_buf = buf.release();
    auto dict_ptr = std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](fixpp::dict::Dictionary const* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};
    auto* fd = new fixpp_dict{dict_ptr};

    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, role), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(sc, role == FIXPP_ROLE_INITIATOR),
              FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, reinterpret_cast<fixpp_dict_t*>(fd)),
              FIXPP_ERR_OK);
    delete fd;  // the setter copied the shared_ptr
    return sc;
}

inline std::string_view as_sv(const uint8_t* p, size_t n) {
    return {reinterpret_cast<const char*>(p), n};
}

inline const uint8_t* as_u8(std::string_view s) {
    return reinterpret_cast<const uint8_t*>(s.data());
}

}  // namespace fixpp::capi_test
