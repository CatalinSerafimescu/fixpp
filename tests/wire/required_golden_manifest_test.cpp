// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/required_golden_manifest_test.cpp
//
// fixpp#412 — generator-source pin for the QuickFIX required-set goldens
// (`tools/quickfix_required_golden/golden.csv` and `golden_groups.csv`).
// Mirrors EnumGoldenManifestTest.GeneratorSourceHashMatches
// (tests/wire/enum_golden_manifest_test.cpp) for the required-set generator.
//
// Each golden records `generator_source_hash` = SHA-1 over the raw bytes of
// tools/quickfix_required_golden/main.cpp. The pin exists for the reason spec
// 075 FR-019 gives for the enum generator: configuration lives in the
// generator's source rather than CLI arguments so the hash pins it. A recorded
// but unasserted hash lets the source drift from the goldens it certifies.
//
// Links NO QuickFIX and never touches reference-engines/: it only hashes a
// checked-in file and reads the checked-in manifests, so it is a pure
// tree-consistency gate. Each golden carries its own copy of the row, so each
// is asserted by its own TEST.
//
// To prove this gate can go RED: change one byte of main.cpp in the tree and
// run the binary (paths are read at runtime, no rebuild) — both TESTs must
// fail; then restore main.cpp byte-exact (it is digest-bound and `-text`).

#include <gtest/gtest.h>
#include <openssl/evp.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef FIXPP_REQUIRED_GOLDEN_CSV
#error "FIXPP_REQUIRED_GOLDEN_CSV must be defined by CMake"
#endif
#ifndef FIXPP_REQUIRED_GOLDEN_GROUPS_CSV
#error "FIXPP_REQUIRED_GOLDEN_GROUPS_CSV must be defined by CMake"
#endif
#ifndef FIXPP_REQUIRED_GOLDEN_MAIN_CPP
#error "FIXPP_REQUIRED_GOLDEN_MAIN_CPP must be defined by CMake"
#endif

namespace {

// SHA-1 via OpenSSL EVP — an independent re-implementation of the
// generator's own hashing, so this is a cross-check rather than a shared bug.
std::string sha1_hex(std::string const& bytes) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EXPECT_NE(ctx, nullptr);
    if (!ctx) {
        return {};
    }
    int ok = EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
    ok = ok && EVP_DigestUpdate(ctx, bytes.data(), bytes.size());
    ok = ok && EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);
    EXPECT_TRUE(ok);
    if (!ok) {
        return {};
    }
    static char const* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(digest_len * 2);
    for (unsigned int i = 0; i < digest_len; ++i) {
        out.push_back(kHex[(digest[i] >> 4) & 0xF]);
        out.push_back(kHex[digest[i] & 0xF]);
    }
    return out;
}

std::string read_file_bytes(std::string const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ADD_FAILURE() << "cannot open '" << path << "'";
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Returns the value of the single `# generator_source_hash=<hex>` manifest
// line. Prose comment lines that merely mention the key (without the exact
// `=` form) are not matched; zero or more than one match is a failure, so a
// duplicated row cannot silently win.
std::string manifest_generator_hash(std::string const& csv_path) {
    static std::string const kPrefix = "# generator_source_hash=";
    std::ifstream f(csv_path, std::ios::binary);
    if (!f) {
        ADD_FAILURE() << "cannot open '" << csv_path << "'";
        return {};
    }
    std::vector<std::string> values;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.compare(0, kPrefix.size(), kPrefix) == 0) {
            values.push_back(line.substr(kPrefix.size()));
        }
    }
    if (values.size() != 1) {
        ADD_FAILURE() << "expected exactly one '" << kPrefix << "' manifest line in '" << csv_path
                      << "', found " << values.size();
        return {};
    }
    return values.front();
}

TEST(RequiredGoldenManifestTest, GeneratorSourceHashMatchesGolden) {
    std::string const recomputed = sha1_hex(read_file_bytes(FIXPP_REQUIRED_GOLDEN_MAIN_CPP));
    EXPECT_EQ(recomputed, manifest_generator_hash(FIXPP_REQUIRED_GOLDEN_CSV))
        << "tools/quickfix_required_golden/main.cpp changed (config pin or corpus) without "
           "regenerating golden.csv (tree drift)";
}

TEST(RequiredGoldenManifestTest, GeneratorSourceHashMatchesGoldenGroups) {
    std::string const recomputed = sha1_hex(read_file_bytes(FIXPP_REQUIRED_GOLDEN_MAIN_CPP));
    EXPECT_EQ(recomputed, manifest_generator_hash(FIXPP_REQUIRED_GOLDEN_GROUPS_CSV))
        << "tools/quickfix_required_golden/main.cpp changed (config pin or corpus) without "
           "regenerating golden_groups.csv (tree drift)";
}

}  // namespace
