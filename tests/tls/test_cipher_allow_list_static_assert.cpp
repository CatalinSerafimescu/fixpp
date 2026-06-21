// tests/tls/test_cipher_allow_list_static_assert.cpp
// T035 — seam #10: CipherPolicy allow-list compile-time static_assert witnesses.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.4 + contracts/cipher_policy.hpp.
// Spec anchors: FR-011 (compile-time allow-list), FR-012 (is_allowed predicate).
//
// POSITIVE: published allow-list arrays compile clean; is_allowed returns
// the right truth values.
// NEGATIVE-COMPILE: tested via CMake try_compile in CMakeLists.txt — a separate
// .cpp that adds a banned suite triggers static_assert at build time.

#include <gtest/gtest.h>

#include <fixpp/tls/cipher_policy.hpp>

using fixpp::tls::CipherPolicy;

// ── Positive: is_allowed on the explicit allow-list entries ──────────────────

TEST(CipherPolicyStaticAssert, TLS13SuitesAllowed) {
    // Every TLS 1.3 suite must be allowed.
    EXPECT_TRUE(CipherPolicy::is_allowed("TLS_AES_128_GCM_SHA256"));
    EXPECT_TRUE(CipherPolicy::is_allowed("TLS_AES_256_GCM_SHA384"));
    EXPECT_TRUE(CipherPolicy::is_allowed("TLS_CHACHA20_POLY1305_SHA256"));
}

TEST(CipherPolicyStaticAssert, TLS12SuitesAllowed) {
    // Every TLS 1.2 suite must be allowed.
    EXPECT_TRUE(CipherPolicy::is_allowed("ECDHE-RSA-AES128-GCM-SHA256"));
    EXPECT_TRUE(CipherPolicy::is_allowed("ECDHE-RSA-AES256-GCM-SHA384"));
    EXPECT_TRUE(CipherPolicy::is_allowed("ECDHE-ECDSA-AES128-GCM-SHA256"));
    EXPECT_TRUE(CipherPolicy::is_allowed("ECDHE-ECDSA-AES256-GCM-SHA384"));
    EXPECT_TRUE(CipherPolicy::is_allowed("ECDHE-RSA-CHACHA20-POLY1305"));
    EXPECT_TRUE(CipherPolicy::is_allowed("ECDHE-ECDSA-CHACHA20-POLY1305"));
}

// ── Negative: banned tokens return false ────────────────────────────────────

TEST(CipherPolicyStaticAssert, CBCBanned) {
    // The key negative from the contract oracle: CBC-containing suite.
    EXPECT_FALSE(CipherPolicy::is_allowed("ECDHE-RSA-AES128-CBC-SHA256"));
}

TEST(CipherPolicyStaticAssert, BannedSubstringMatching) {
    // Substring-match: RC4, MD5, 3DES, etc.
    EXPECT_FALSE(CipherPolicy::is_allowed("RC4-MD5"));
    EXPECT_FALSE(CipherPolicy::is_allowed("ECDHE-RSA-AES128-CBC-SHA1"));
    EXPECT_FALSE(CipherPolicy::is_allowed("NULL-MD5"));
    EXPECT_FALSE(CipherPolicy::is_allowed("DES-CBC3-SHA"));
    EXPECT_FALSE(CipherPolicy::is_allowed("TLS_RSA_WITH_AES_128_GCM_SHA256"));
    EXPECT_FALSE(CipherPolicy::is_allowed("EXPORT_RSA"));
    EXPECT_FALSE(CipherPolicy::is_allowed("TLS_AES_128_CCM_SHA256"));
    EXPECT_FALSE(CipherPolicy::is_allowed("DH_anon-AES128-SHA"));
}

TEST(CipherPolicyStaticAssert, UnknownSuiteNotAllowed) {
    // Not on either allow-list — rejected even if no banned token.
    EXPECT_FALSE(CipherPolicy::is_allowed(""));
    EXPECT_FALSE(CipherPolicy::is_allowed("ECDHE-RSA-AES256-SHA"));
    EXPECT_FALSE(CipherPolicy::is_allowed("DHE-RSA-AES128-GCM-SHA256"));
}

// ── Allow-list array counts (contract assertions 1 + 2) ──────────────────────

TEST(CipherPolicyStaticAssert, ArraySizes) {
    static_assert(CipherPolicy::tls13_suites.size() == 3,
                  "tls13_suites must have exactly 3 entries per [2g §4.4].");
    static_assert(CipherPolicy::tls12_suites.size() == 6,
                  "tls12_suites must have exactly 6 entries per [2g §4.4].");
    static_assert(CipherPolicy::kx_groups.size() == 3,
                  "kx_groups must have exactly 3 entries per [2g §4.4].");
    static_assert(CipherPolicy::sig_algs.size() == 4,
                  "sig_algs must have exactly 4 entries per [2g §4.4].");
    static_assert(CipherPolicy::banned_tokens.size() == 12,
                  "banned_tokens must have exactly 12 entries per [2g §4.4].");
    SUCCEED();  // static_asserts above are the real gate.
}

// ── is_allowed is constexpr + noexcept (FR-012) ──────────────────────────────

TEST(CipherPolicyStaticAssert, IsAllowedConstexprNoexcept) {
    // Compile-time evaluation must work (constexpr).
    static_assert(CipherPolicy::is_allowed("TLS_AES_256_GCM_SHA384"),
                  "is_allowed must be usable in a constexpr context.");
    static_assert(!CipherPolicy::is_allowed("RC4-MD5"),
                  "banned cipher must be rejected constexpr.");

    // noexcept — verified at the call site.
    // Pass std::string_view explicitly (pointer+length form) to avoid the non-noexcept
    // std::string_view(const char*) implicit conversion under libc++ (strlen not noexcept).
    static_assert(noexcept(CipherPolicy::is_allowed(std::string_view{"test", 4})),
                  "is_allowed must be noexcept per FR-012.");
    SUCCEED();
}
