// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_support_dictionary_seam_test.cpp — 089 T031.
//
// Witnesses hp_support.hpp's production-dictionary opt-in seam
// (production_dictionary_and_digest()) against FR-001 / R-5a / data-model.md
// §1a `dictionary_digest`:
//   (a) the production seam yields a dictionary where a FIX44-only fact
//       holds, and does not hold for the sentinel make_minimal_dictionary();
//   (b) the returned digest equals an independently computed SHA-256 of the
//       file (a second OpenSSL API surface — EVP — not the seam's own
//       low-level SHA256() call, so this is a genuine second computation);
//   (c) make_session_config()'s DEFAULT path is untouched — it still returns
//       the FIX 4.2 single-Heartbeat sentinel, byte-for-byte, for every
//       existing call site;
//   (d) the env override (`FIXPP_FIX44_DICT_XML`, the SAME knob
//       run_interop_cell.py sets) is the file actually loaded — not the
//       compile-def fallback — proven by pointing it at a byte-modified copy
//       and checking the digest tracks the copy, not the original.
#include <gtest/gtest.h>

#include <openssl/evp.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "happy/hp_support.hpp"
#include "support/minimal_dictionary.hpp"

namespace {

using fixpp::interop::hp::ProductionDictionary;

// `setenv`/`unsetenv` are POSIX and DO NOT EXIST in the MSVC CRT -- MSVC
// reports `error C3861: 'setenv': identifier not found`, which is a hard build
// break on all three windows-msvc-* legs, not a degraded test. The portable
// pair is `_putenv_s(name, value)`, where passing an EMPTY value REMOVES the
// variable (that is the documented deletion spelling; there is no `_unsetenv`).
// Both surfaces write the CRT environment `std::getenv` reads, so the seam
// under test sees the change either way. The same split already exists in
// `tests/session/test_file_store_crash_survival.cpp`, which uses the wide
// `::_wputenv_s` for the same reason -- this is the house idiom, not a new one.
inline void test_set_env(char const* name, char const* value)
{
#ifdef _WIN32
    ::_putenv_s(name, value);
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded test setup.
    ::setenv(name, value, 1);
#endif
}

inline void test_unset_env(char const* name)
{
#ifdef _WIN32
    ::_putenv_s(name, "");  // empty value == delete, per the CRT contract
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ::unsetenv(name);
#endif
}

// RAII guard: sets `name` to `value` for the guard's lifetime, restoring
// whatever the environment held before (unset, if it was unset) on
// destruction — including on an early ASSERT_* return from the test body.
class ScopedEnvVar {
public:
    ScopedEnvVar(char const* name, std::string const& value) : name_(name) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded test setup.
        if (char const* prev = std::getenv(name); prev != nullptr) {
            had_prev_ = true;
            prev_value_ = prev;
        }
        test_set_env(name_, value.c_str());
    }
    ~ScopedEnvVar() {
        if (had_prev_) {
            test_set_env(name_, prev_value_.c_str());
        } else {
            test_unset_env(name_);
        }
    }
    ScopedEnvVar(ScopedEnvVar const&) = delete;
    ScopedEnvVar& operator=(ScopedEnvVar const&) = delete;

private:
    char const* name_;
    bool had_prev_ = false;
    std::string prev_value_;
};

// A FIX44-only fact: NewOrderSingle (msgtype "D") declares ClOrdID(11) as a
// field. kMinimalFix42Xml declares no "D" message at all, so
// field_valid_for("D", 11) is false there by construction (undeclared
// msg_type ⇒ FieldRef::rule == NotDeclared, dictionary.cpp
// Dictionary::field_valid_for).
constexpr char kFactMsgType[] = "D";
constexpr std::uint16_t kFactTag = 11;  // ClOrdID

// Independent SHA-256 over the raw file bytes, via the EVP digest API — a
// different OpenSSL surface than the seam's own SHA256() call, so this is a
// genuine second computation, not a copy of the same call site.
std::string independent_sha256_hex(std::string const& path) {
    std::ifstream f(path, std::ios::binary);
    EXPECT_TRUE(f.is_open()) << "cannot open " << path;
    std::ostringstream oss;
    oss << f.rdbuf();
    std::string const bytes = oss.str();

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, bytes.data(), bytes.size());
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex(digest_len * 2, '\0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        hex[2 * i] = kHex[digest[i] >> 4U];
        hex[(2 * i) + 1] = kHex[digest[i] & 0xFU];
    }
    return hex;
}

}  // namespace

// (a) The production seam's dictionary carries the FIX44-only fact; the
// sentinel does not.
TEST(HpSupportDictionarySeam, ProductionDictionaryHasFix44OnlyFact) {
    ProductionDictionary prod = fixpp::interop::hp::production_dictionary_and_digest();
    ASSERT_NE(prod.dictionary, nullptr);
    EXPECT_TRUE(prod.dictionary->field_valid_for(kFactMsgType, kFactTag))
        << "production FIX44.xml should declare ClOrdID(11) on NewOrderSingle(D)";

    auto sentinel = fixpp::test_support::make_minimal_dictionary();
    ASSERT_NE(sentinel, nullptr);
    EXPECT_FALSE(sentinel->field_valid_for(kFactMsgType, kFactTag))
        << "the FIX 4.2 single-Heartbeat sentinel must NOT declare NewOrderSingle at all";
}

// (b) The digest equals an independently computed SHA-256 of the file.
TEST(HpSupportDictionarySeam, DigestMatchesIndependentSha256) {
    ProductionDictionary prod = fixpp::interop::hp::production_dictionary_and_digest();
    std::string const path = fixpp::interop::hp::production_fix44_dict_path();
    std::string const expected = independent_sha256_hex(path);

    ASSERT_EQ(prod.dictionary_digest.size(), 64u);
    EXPECT_EQ(prod.dictionary_digest, expected);
}

// (c) make_session_config()'s default path is unmodified: it still returns
// the FIX 4.2 single-Heartbeat sentinel for every existing call site.
TEST(HpSupportDictionarySeam, MakeSessionConfigDefaultStillReturnsSentinel) {
    fixpp::session::SessionConfig c = fixpp::interop::hp::make_session_config(
        fixpp::interop::Role::fixpp_initiator, "FIX.4.4", nullptr, asio::any_io_executor{},
        fixpp::transport::Endpoint{"127.0.0.1", 0});

    ASSERT_NE(c.dictionary, nullptr);
    // The sentinel's only declared message is Heartbeat("0") — it must NOT
    // declare NewOrderSingle("D") at all (the same fact test (a) checks).
    EXPECT_FALSE(c.dictionary->field_valid_for(kFactMsgType, kFactTag));
    // Positive control: the sentinel DOES declare Heartbeat's TestReqID(112),
    // so the negative check above is discriminating, not merely a dead
    // dictionary.
    EXPECT_TRUE(c.dictionary->field_valid_for("0", 112));
}

// (d) The env override is the file actually loaded — proven by pointing
// FIXPP_FIX44_DICT_XML at a BYTE-MODIFIED copy of the real file and checking
// the digest tracks the copy, not the original (which is what a stale
// FIXPP_DICT_DATA_DIR-only seam would silently return instead).
TEST(HpSupportDictionarySeam, EnvOverridePointsAtTheFileActuallyLoaded) {
    std::string const orig_path = fixpp::interop::hp::production_fix44_dict_path();
    std::string const orig_digest = independent_sha256_hex(orig_path);

    std::string const tmp_path = ::testing::TempDir() + "FIX44-plus-one-byte-089-T031.xml";
    {
        std::ifstream in(orig_path, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "cannot open " << orig_path;
        std::ostringstream oss;
        oss << in.rdbuf();
        std::string xml = oss.str();
        xml.push_back('\n');  // one byte appended — a distinct file
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open()) << "cannot write " << tmp_path;
        out << xml;
    }
    std::string const modified_digest = independent_sha256_hex(tmp_path);
    ASSERT_NE(modified_digest, orig_digest);  // sanity: the append actually changed the hash

    ScopedEnvVar guard("FIXPP_FIX44_DICT_XML", tmp_path);
    ProductionDictionary prod = fixpp::interop::hp::production_dictionary_and_digest();

    EXPECT_EQ(prod.dictionary_digest, modified_digest);
    EXPECT_NE(prod.dictionary_digest, orig_digest);

    std::remove(tmp_path.c_str());
}
