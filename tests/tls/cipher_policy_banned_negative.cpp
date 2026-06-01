// cipher_policy_banned_negative.cpp
// Negative-compile witness for T035: adding a banned CBC token to an allow-list
// MUST trigger a compile-time error (FR-011).
//
// This file is INTENTIONALLY invalid — the static_assert below MUST fire.
// It is registered as a CMake test with WILL_FAIL TRUE.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.4 / [const §XII.3].
#include <array>
#include <fixpp/tls/cipher_policy.hpp>
#include <string_view>

namespace fixpp::tls {

// This list contains a CBC token, which is on the banned_tokens list.
// detail::contains_banned must detect it and this static_assert must fire.
static constexpr std::array<std::string_view, 1> bad_list{{
    "ECDHE-RSA-AES128-CBC-SHA256",  // CBC is banned per [const §XII.4].
}};

// WILL_FAIL: this static_assert is expected to trigger because bad_list has "CBC".
static_assert(!detail::contains_banned(bad_list, CipherPolicy::banned_tokens),
              "CBC token must be detected as banned — WILL_FAIL expected.");

}  // namespace fixpp::tls

int main() { return 0; }
