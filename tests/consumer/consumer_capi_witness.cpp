// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/consumer_capi_witness.cpp
//
// 084 T034 — BY-NAME export-member witness (FR-003).
//
// What this proves, and only this: the installed package publishes the imported
// target under the name `fixpp::capi`, that name resolves through
// find_package(fixpp), and linking it produces a working binary. It is the only
// place in this feature where a by-name export member's INSTALLED imported name
// is exercised at all — every other witness links the umbrella or the session
// stack, so without this T022's EXPORT_NAME sweep would be proven statically
// (T026) and never by a link.
//
// It is NOT a re-test of the C ABI: that surface has its own suites. Calling one
// trivial accessor is enough to force the linker to resolve a real symbol out of
// the installed archive rather than merely accept the target.
//
// ⚠️ This target deliberately does NOT link fixpp::fixpp. Linking the C++
// umbrella and fixpp::capi into one binary is the combination Article IV §2 /
// architecture.md §7.4 rejects.
//
// NOTHING MECHANICALLY ENFORCES THAT (corrected by 086/FR-014 — this comment used
// to say tools/check_layers.py did). That script is a source #include-edge lint
// over src/** and bindings/** (:2-7, :173-176); it parses no CMake and reads no
// link interface, and an installed package cannot observe which targets a
// consumer links together anyway. Keeping this a separate executable IS the
// convention being upheld.
//
// Compiled as C++ rather than C so the standalone witness project needs only the
// CXX language enabled; the header is C-ABI either way.

#include <fix/c_api.h>

#include <cstdio>

// ── 086 T014a (FR-009) — pull the SESSION/DICTIONARY closure at LINK time ─────
//
// Without these two references this witness proves less than it appears to.
// main() below touches only fixpp_library_version and fixpp_strerror, and
// neither of those objects references anything outside fixpp_capi_objects — so
// the witness would stay green even if $<LINK_ONLY:> silently dropped a real
// transitive archive edge, which is exactly the regression 086 could introduce.
// fixpp_dict_load_from_xml and fixpp_engine_create reach the dictionary and
// session/transport/TLS closures, including both deliberate static-archive
// cycles (wire <-> dictionary, dictionary -> bridge -> dictionary).
//
// FORM MATTERS. These are namespace-scope, non-`static`, non-`const` pointers:
// external linkage means the initializer is a relocation the linker must
// satisfy, and neither constant-folding nor --gc-sections may discard it. An
// address assigned to an unused local CAN be optimised away together with its
// relocation, which would silently restore the gap (Gate A r3 carry-forward #6).
//
// No runtime obligation is taken on. This TU is built and linked but NEVER
// executed — the driver runs only ${_sub_build}/consumer_witness
// (run_consumer_witness.cmake:167,190) — so nothing here may depend on, or assert,
// runtime behaviour. Building and linking IS the assertion.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): making these
// `const` would DEFEAT them. A namespace-scope `const` object has INTERNAL
// linkage in C++, and an internal-linkage pointer nothing reads is exactly what
// the optimiser may discard together with its relocation — the failure mode this
// declaration exists to avoid. External linkage is the property doing the work,
// and `const` is mutually exclusive with it here without an `extern` that the
// check would flag anyway. Nothing mutates these; the check cannot see that the
// non-constness is a linkage consequence rather than a design choice.
fixpp_error_t (*fixpp_capi_witness_dict_entry)(const char*,
                                               fixpp_dict_t**) = &fixpp_dict_load_from_xml;
fixpp_error_t (*fixpp_capi_witness_engine_entry)(fixpp_engine_config_t*, uint16_t, uint16_t,
                                                 fixpp_engine_t**) = &fixpp_engine_create;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

int main() {
    const fixpp_version_t v = fixpp_library_version();

    // fixpp_strerror is in a different C-ABI translation unit from
    // fixpp_library_version, so referencing both forces more than one object out
    // of the installed archive.
    const char* ok = fixpp_strerror(FIXPP_ERR_OK);
    if (ok == nullptr) {
        std::fprintf(stderr, "FAIL: fixpp_strerror(FIXPP_ERR_OK) returned NULL\n");
        return 1;
    }

    std::printf("PASS: linked fixpp::capi by name from the installed package; version %u.%u.%u\n",
                static_cast<unsigned>(v.major), static_cast<unsigned>(v.minor),
                static_cast<unsigned>(v.patch));
    return 0;
}
