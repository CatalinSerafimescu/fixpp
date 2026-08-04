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
// FORM MATTERS — and the earlier form was NOT strong enough (Gate B r1, P2 #7).
//
// This previously took the two addresses into namespace-scope, non-`static`,
// non-`const` pointers, on the stated grounds that "neither constant-folding nor
// --gc-sections may discard it". THAT CLAIM WAS FALSE. External linkage stops
// ordinary TU-local dead-code elimination, but under `-ffunction-sections
// -fdata-sections -Wl,--gc-sections`, or whole-program LTO, an unreferenced data
// section holding those pointers can be discarded — and the executable then links
// WITHOUT pulling the dictionary/session closure. The gate would have gone hollow
// under a link-flag change nobody would have associated with it.
//
// The references are now CALLS, reached from a branch whose condition the
// compiler cannot fold (`argc`), so the relocations are unavoidable at link time
// regardless of section GC or LTO.
//
// Direct calls are safe here for the reason stated below: this TU is built and
// linked but NEVER executed — the driver runs only ${_sub_build}/consumer_witness
// — so no runtime behaviour is depended on or asserted. The guard needs 4+ argv
// entries, so ORDINARY zero-argument execution skips it. It is NOT true that
// every accidental execution skips it: an invocation with three or more arguments
// does enter the branch. Stated precisely because the earlier wording overclaimed
// (Gate B r2 P3 #6); it does not weaken the link gate either way, since the
// harness never executes this binary.

int main(int argc, char** argv) {
    // False for any invocation the harness makes; the compiler cannot know that,
    // which is exactly what makes the calls below non-elidable.
    if (argc > 3) {
        fixpp_dict_t* d = nullptr;
        (void)fixpp_dict_load_from_xml(argv[1], &d);
        fixpp_engine_t* e = nullptr;
        (void)fixpp_engine_create(nullptr, 1, 0, &e);
    }

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
