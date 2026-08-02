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
// architecture.md:509 rejects and tools/check_layers.py enforces — which is why
// this is a separate executable and not another TU of consumer_witness.
//
// Compiled as C++ rather than C so the standalone witness project needs only the
// CXX language enabled; the header is C-ABI either way.

#include <fix/c_api.h>

#include <cstdio>

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
