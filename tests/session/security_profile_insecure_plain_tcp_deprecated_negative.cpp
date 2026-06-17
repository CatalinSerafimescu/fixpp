// security_profile_insecure_plain_tcp_deprecated_negative.cpp
//
// Negative-compile witness for T017 / SC-005 (043-plaintext-tcp-transport):
// A TU that selects `SecurityProfile::kind::insecure_plain_tcp` WITHOUT any
// pragma suppression, compiled with `-Werror=deprecated-declarations`, MUST
// FAIL to compile.
//
// This file is INTENTIONALLY expected to fail compilation.
// It is registered as a CMake test with WILL_FAIL TRUE.
//
// Anchor: spec.md SC-005 / D-9 / [const §XII.5 v0.3 amendment] / [043 T017].
// Pattern: mirrors tests/tls/security_profile_deprecated_negative.cpp (one_way_ca)
//           and tests/tls/cipher_policy_banned_negative.cpp (WILL_FAIL idiom).
//
// DO NOT add a `#pragma clang diagnostic ignored "-Wdeprecated-declarations"` here.
// That would defeat the test — the whole point is that the unsuppressed selection
// triggers -Wdeprecated-declarations → -Werror → build failure.
#include <fixpp/session/security_profile.hpp>
int main() {
    // This selection MUST emit -Wdeprecated-declarations (from the [[deprecated]]
    // attribute on insecure_plain_tcp) → -Werror makes it a hard error.
    auto k = fixpp::session::SecurityProfile::kind::insecure_plain_tcp;
    (void)k;
    return 0;
}
