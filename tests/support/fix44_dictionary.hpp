// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/fix44_dictionary.hpp
//
// 066-dict-backed-inbound-parse T001 — shared test-support: load the REAL
// `dictionaries/FIX44.xml` (NOT a synthetic inline dict) into a heap-owned
// `shared_ptr<const Dictionary>` suitable for `SessionConfig::dictionary` /
// the C-ABI `fixpp_session_config_set_dictionary` seam.
//
// 066's correctness witnesses MUST drive real dispatch through a dictionary
// that actually registers groups (research.md Decision 1/2/6): the shipped
// FIX44.xml's `ExecutionReport`(35=8) declares `<component
// name='InstrmtLegExecGrp' required='N' />` (dictionaries/FIX44.xml:251),
// which expands (dictionaries/FIX44.xml:2830-2845) to
// `<group name='NoLegs' required='N'>` (field 555, NUMINGROUP,
// dictionaries/FIX44.xml:5561) containing the `InstrumentLeg` component
// (LegSymbol=600, LegSide=624, LegQty=687, ... dictionaries/FIX44.xml:2436+)
// — a real dict-registered group on a real app message, per the FIX43/
// FIX44/FIX50/FIX50SP1/FIX50SP2/FIXT.1.1 group-registering scope (L-063-1).
// A synthetic inline test dict would not exercise the real component-
// expansion registration path the shipped `as_table_view()` walks.
//
// PMR-buffer-in-shared_ptr-deleter pattern mirrors
// tests/support/minimal_dictionary.hpp; 4 MiB buffer size mirrors
// tests/dictionary/concurrent_readers_test.cpp's full-FIX44.xml load.
//
// Consuming test targets MUST define FIXPP_DICT_DATA_DIR (the dictionaries/
// path; mirror tests/session/CMakeLists.txt's test_exemplar_read block /
// tests/dictionary/concurrent_readers_test.cpp).
#pragma once

#include <array>
#include <cstddef>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory>
#include <memory_resource>
#include <string>

namespace fixpp::test_support {

// Returns a shared_ptr<const Dictionary> backed by the real FIX44.xml,
// loaded into a heap-allocated 4 MiB PMR monotonic buffer co-owned via the
// shared_ptr's deleter (mirrors make_minimal_dictionary()).
[[nodiscard]] inline std::shared_ptr<const fixpp::dict::Dictionary> make_fix44_dictionary() {
    constexpr std::size_t kBufSize = 4u * 1024u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};

    std::string path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml";
    fixpp::dict::Dictionary d = fixpp::dict::XmlLoader{}.load(path, mr);

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
