// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/minimal_dictionary.hpp
//
// Test-only helper: build a minimal shared_ptr<const Dictionary> using an
// inline FIX 4.2 XML string. Used by threading seam tests that call
// Session::open() and must satisfy the null-dictionary rejection check
// (T050 / FR-018 / I-13) without depending on on-disk XML data files.
//
// The returned Dictionary is a valid FIX 4.2 dict with a single Heartbeat
// message and its required header/trailer/fields. session_version::v42.
// Do NOT use for dictionary-semantic tests — only for threading/executor/
// clock/cancellation tests that need a non-null dictionary precondition.
#pragma once

#include <array>
#include <cstddef>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <string_view>

namespace fixpp::test_support {

// Minimal FIX 4.2 XML: header, trailer, one message (Heartbeat), required fields.
// Used only as a non-null dictionary sentinel for threading tests.
constexpr std::string_view kMinimalFix42Xml = R"xml(
<fix major="4" minor="2">
  <header>
    <field number="8"  name="BeginString"  required="Y"/>
    <field number="9"  name="BodyLength"   required="Y"/>
    <field number="35" name="MsgType"      required="Y"/>
    <field number="49" name="SenderCompID" required="Y"/>
    <field number="56" name="TargetCompID" required="Y"/>
    <field number="34" name="MsgSeqNum"    required="Y"/>
    <field number="52" name="SendingTime"  required="Y"/>
    <field number="10" name="CheckSum"     required="Y"/>
  </header>
  <trailer>
    <field number="10" name="CheckSum" required="Y"/>
  </trailer>
  <messages>
    <message name="Heartbeat" msgtype="0" msgcat="admin">
      <field number="112" name="TestReqID" required="N"/>
    </message>
  </messages>
  <fields>
    <field number="8"   name="BeginString"  type="STRING"/>
    <field number="9"   name="BodyLength"   type="INT"/>
    <field number="35"  name="MsgType"      type="STRING"/>
    <field number="49"  name="SenderCompID" type="STRING"/>
    <field number="56"  name="TargetCompID" type="STRING"/>
    <field number="34"  name="MsgSeqNum"    type="INT"/>
    <field number="52"  name="SendingTime"  type="UTCTIMESTAMP"/>
    <field number="10"  name="CheckSum"     type="STRING"/>
    <field number="112" name="TestReqID"    type="STRING"/>
  </fields>
</fix>
)xml";

// Returns a shared_ptr<const Dictionary> backed by a heap-allocated PMR
// monotonic buffer. The Dictionary lifetime is tied to the shared_ptr;
// the PMR buffer is co-owned via the shared_ptr's deleter. This is
// intentionally oversized (64 KiB) to accommodate the XML loader's
// internal allocations.
[[nodiscard]] inline std::shared_ptr<const fixpp::dict::Dictionary> make_minimal_dictionary() {
    // Allocate a PMR buffer on the heap; co-own it via the shared_ptr deleter.
    constexpr std::size_t kBufSize = 64u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};

    fixpp::dict::Dictionary d = fixpp::dict::XmlLoader{}.load_from_string(kMinimalFix42Xml, mr);

    // Move Dictionary into a shared_ptr; the deleter owns both the
    // monotonic_buffer_resource and the backing buffer.
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
