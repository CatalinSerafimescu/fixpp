// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/packaging/real_client/shim/support/minimal_dictionary.hpp — 084 Gate B
// round 1 F1.
//
// Witness-local stand-in for tests/support/minimal_dictionary.hpp.
//
// ── WHY A SHIM ───────────────────────────────────────────────────────────────
// The driver's quoted include "support/minimal_dictionary.hpp" used to be
// satisfied by COPYING the real tests/support/minimal_dictionary.hpp into the
// scratch project (see run_real_client_witness.cmake history). That smuggled a
// private test-support header into the consumer project, violating spec.md:78
// (US1 acceptance scenario 7 — "does not reach into ... its test-support
// headers") and spec.md:90 (FR-018a MUST — the packaged variant replaces the
// dictionary helper with a runtime load through the PUBLIC API of a dictionary
// SHIPPED in the package).
//
// This shim is the same technique already used four lines above it in
// tasks.md for HdrHistogram_c (see ../hdr/hdr_histogram.h): the driver stays
// byte-for-byte unmodified (same quoted include, same call, same namespace,
// same return type), and the witness-local implementation satisfies it without
// reaching into the source tree.
//
// ── WHAT IT LOADS, AND WHY FIX 4.2 (not 4.4) ─────────────────────────────────
// tests/support/minimal_dictionary.hpp builds a synthetic inline FIX 4.2 XML
// string via XmlLoader::load_from_string. This shim instead loads the REAL,
// SHIPPED dictionaries/FIX42.xml through the PUBLIC XmlLoader::load(path, mr)
// API, from the staged install prefix's share/fixpp/dictionaries/ directory
// (CMakeLists.txt:774-777 installs the whole dictionaries/ directory). FIX 4.2
// is kept — not substituted for FIX 4.4 — because the driver's SessionConfig
// for BOTH sides of the loopback pair is version-agnostic w.r.t. this helper
// (it only needs A valid, non-null dictionary satisfying the null-dictionary
// rejection check), and substituting versions is no part of what F1 asks for.
//
// FIXPP_DICT_DATA_DIR is a compile definition set by
// tests/packaging/real_client/CMakeLists.txt from the ${_stage} prefix that
// run_real_client_witness.cmake already stages via `cmake --install`
// (mirrors the FIXPP_DICT_DATA_DIR convention used tree-wide, e.g.
// tests/support/fix44_dictionary.hpp / tests/dictionary/CMakeLists.txt).
//
// ── PMR ARENA LIFETIME ───────────────────────────────────────────────────────
// Mirrors tests/support/fix44_dictionary.hpp exactly: the monotonic buffer and
// the Dictionary are both heap-allocated and co-owned by the returned
// shared_ptr's custom deleter, so the arena outlives every use of the
// dictionary for as long as the shared_ptr (and its copies held by
// SessionConfig) are alive. No caller-managed lifetime is required. 4 MiB
// mirrors fix44_dictionary.hpp's sizing for the larger FIX44.xml; FIX42.xml
// (126 KiB source) fits with headroom to spare.
#pragma once

#include <array>
#include <cstddef>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory>
#include <memory_resource>
#include <string>

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be defined by the build (see real_client/CMakeLists.txt)"
#endif

namespace fixpp::test_support {

// Returns a shared_ptr<const Dictionary> backed by the SHIPPED FIX42.xml,
// loaded through the public API from the staged install prefix, into a
// heap-allocated 4 MiB PMR monotonic buffer co-owned via the shared_ptr's
// deleter (mirrors tests/support/fix44_dictionary.hpp).
[[nodiscard]] inline std::shared_ptr<const fixpp::dict::Dictionary> make_minimal_dictionary() {
    constexpr std::size_t kBufSize = 4u * 1024u * 1024u;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};

    std::string path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX42.xml";
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
