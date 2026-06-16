// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/validation_test_dictionary.hpp
//
// 041-validation-gate-wiring T012/T013 — richer inline FIX 4.2 dictionary for
// validation-gate witness tests. Extends the minimal dictionary with:
//   - Logon (35=A) with required SendingTime(52), EncryptMethod(98), HeartBtInt(108)
//   - Heartbeat (35=0) with optional TestReqID(112)
//   - NewOrderSingle (35=D) with required ClOrdID(11), Side(54), TransactTime(60),
//     and optional OrderQty(38, INT) and Price(44, PRICE→FLOAT)
//   - A repeating group in Heartbeat is NOT needed for T012; group tests use D
//   - INT field: OrderQty(38), for type-nonconformant (reason=5) testing
//   - FLOAT field: Price(44, PRICE→FLOAT), for decimal-precision-loss (reason=6)
//
// NOTE: tag 38 in this dict is OrderQty (INT), distinct from
//       core::error::wire_required_field_missing(38) — that is the error SLOT
//       number, not a FIX tag. No clash at the dictionary level.
//
// The Logon message includes SendingTime(52) as a required header field (shared
// header), so an absent 52 in a Logon frame → wire_required_field_missing →
// reason=1. A present-but-malformed 52 passes (field_type_of(52)→String in the
// 7-value enum; no format check on String).
//
// Used by:
//   tests/session/test_validate_gate_inbound.cpp  (T012)
//   tests/session/test_validate_gate_logon_arm.cpp (T013)
#pragma once

#include <array>
#include <cstddef>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory>
#include <memory_resource>
#include <string_view>

namespace fixpp::test_support {

// FIX 4.2 XML with Logon(35=A), Heartbeat(35=0), NewOrderSingle(35=D).
// Header fields: BeginString(8), BodyLength(9), MsgType(35), SenderCompID(49),
//   TargetCompID(56), MsgSeqNum(34), SendingTime(52)[required].
// Notable typed fields for T012 validation witnesses:
//   - ClOrdID(11): STRING
//   - OrderQty(38): INT  — for type-nonconformant reason=5 witness
//   - Price(44): PRICE (→ Float in the 7-value field_type enum) — for reason=5/6
//   - Side(54): CHAR — for type-nonconformant (multi-byte) reason=5 witness
//   - TransactTime(60): UTCTIMESTAMP (→ String in 7-value enum; no format check)
//   - TestReqID(112): STRING
// The header field order must be: 8, 9, 35, 49, 56, 34, 52 (standard FIX order).
constexpr std::string_view kValidationTestFix42Xml = R"xml(
<fix major="4" minor="2">
  <header>
    <field number="8"  name="BeginString"  required="Y"/>
    <field number="9"  name="BodyLength"   required="Y"/>
    <field number="35" name="MsgType"      required="Y"/>
    <field number="49" name="SenderCompID" required="Y"/>
    <field number="56" name="TargetCompID" required="Y"/>
    <field number="34" name="MsgSeqNum"    required="Y"/>
    <field number="52" name="SendingTime"  required="Y"/>
  </header>
  <trailer>
    <field number="10" name="CheckSum" required="Y"/>
  </trailer>
  <messages>
    <message name="Heartbeat" msgtype="0" msgcat="admin">
      <field number="112" name="TestReqID" required="N"/>
    </message>
    <message name="Logon" msgtype="A" msgcat="admin">
      <field number="98"  name="EncryptMethod" required="Y"/>
      <field number="108" name="HeartBtInt"    required="Y"/>
    </message>
    <message name="NewOrderSingle" msgtype="D" msgcat="app">
      <field number="11"  name="ClOrdID"      required="Y"/>
      <field number="54"  name="Side"         required="Y"/>
      <field number="60"  name="TransactTime" required="Y"/>
      <field number="38"  name="OrderQty"     required="N"/>
      <field number="44"  name="Price"        required="N"/>
    </message>
  </messages>
  <fields>
    <field number="8"   name="BeginString"  type="STRING"/>
    <field number="9"   name="BodyLength"   type="INT"/>
    <field number="10"  name="CheckSum"     type="STRING"/>
    <field number="11"  name="ClOrdID"      type="STRING"/>
    <field number="34"  name="MsgSeqNum"    type="INT"/>
    <field number="35"  name="MsgType"      type="STRING"/>
    <field number="38"  name="OrderQty"     type="INT"/>
    <field number="44"  name="Price"        type="PRICE"/>
    <field number="49"  name="SenderCompID" type="STRING"/>
    <field number="52"  name="SendingTime"  type="UTCTIMESTAMP"/>
    <field number="54"  name="Side"         type="CHAR"/>
    <field number="56"  name="TargetCompID" type="STRING"/>
    <field number="60"  name="TransactTime" type="UTCTIMESTAMP"/>
    <field number="98"  name="EncryptMethod" type="INT"/>
    <field number="108" name="HeartBtInt"   type="INT"/>
    <field number="112" name="TestReqID"    type="STRING"/>
  </fields>
</fix>
)xml";

/// Returns a shared_ptr<const Dictionary> backed by a heap-allocated PMR
/// monotonic buffer. Includes Logon(35=A), Heartbeat(35=0), NOS(35=D) with
/// typed fields for validation-gate witnesses (T012/T013).
[[nodiscard]] inline std::shared_ptr<const fixpp::dict::Dictionary>
make_validation_test_dictionary() {
    constexpr std::size_t kBufSize = 128u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};

    fixpp::dict::Dictionary d =
        fixpp::dict::XmlLoader{}.load_from_string(kValidationTestFix42Xml, mr);

    auto* raw_dict = new fixpp::dict::Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](const fixpp::dict::Dictionary* p) {
            delete p;
            delete mr;
            delete raw_buf;
        }};
}

}  // namespace fixpp::test_support
