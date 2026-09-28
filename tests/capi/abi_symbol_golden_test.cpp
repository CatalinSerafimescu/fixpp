// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/abi_symbol_golden_test.cpp — 062 T022 (Polish, FR-007/SC-004
// no-regression guard).
//
// 062 is a wire+codegen-internal fix (group-entry typed reads) that must
// make NO C-ABI / error-enum change (FR-007). This test REUSES the existing
// frozen-artifact mechanisms rather than hand-maintaining a second copy:
//
//   - CabiSymbolSetUnchanged: the exported `fixpp_*` C-ABI symbol set is
//     already frozen at tests/abi/golden/fixpp_capi_symbols.txt and enforced
//     per-PR by .github/workflows/abi-golden.yml via
//       nm --defined-only --extern-only <libfixpp_capi.a> | ... | sort -u
//     diffed against that golden file. This is a REAL local assertion (not a
//     thin presence check): libfixpp_capi.a IS built under linux-clang-debug
//     (fixpp_capi is a normal STATIC target, no gating), so this test shells
//     out to `nm` on the actual built artifact via $<TARGET_FILE:fixpp_capi>
//     and diffs it against the SAME golden file the CI workflow uses —
//     mirroring the CI mechanism exactly rather than inventing a second
//     symbol list.
//
//   - ErrorEnumUnchanged: a THIN assertion, not a duplicate of the exact-set
//     completeness gates. tests/core/test_0NN_error_completeness.cpp assert
//     (per feature) the exact enumerator SET each feature introduced; the
//     newest of them pins the highest-known boundary: its block's last
//     enumerator carries a message and the slot after it is "unknown error".
//     Since 062 introduces no error enumerator, the correct 062-scoped check
//     is simply: that boundary is STILL exactly where the newest completeness
//     gate puts it — i.e. 062 did not push it forward. A feature that
//     legitimately extends the enum adds its OWN
//     test_0NN_error_completeness.cpp (per the established per-feature
//     pattern) and moves this test's boundary assertion in lockstep — that is
//     the intended coupling, not a maintenance trap.

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <fixpp/core/error.hpp>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifndef FIXPP_CAPI_LIB
#error "FIXPP_CAPI_LIB must be defined by CMake to $<TARGET_FILE:fixpp_capi>"
#endif
#ifndef FIXPP_CAPI_GOLDEN
#error "FIXPP_CAPI_GOLDEN must be defined by CMake to the golden symbol file path"
#endif

// The exported-symbol golden shells out to `nm` on the Unix static archive —
// POSIX-only (no nm / different archive + decoration on Windows), so the helpers
// are compiled only there; the TEST that uses them GTEST_SKIPs on Windows.
#ifndef _WIN32
namespace {

// Runs `nm --defined-only --extern-only <lib>`, extracts the LAST
// whitespace-separated field (the symbol name — same as CI's `awk
// '{print $NF}'`), keeps only `fixpp_`-prefixed names, and returns the
// SORTED-UNIQUE set — the exact transform abi-golden.yml applies.
std::set<std::string> exported_fixpp_symbols(std::string const& lib_path) {
    std::set<std::string> out;
    std::string cmd = "nm --defined-only --extern-only \"" + lib_path + "\" 2>/dev/null";
    std::unique_ptr<FILE, int (*)(FILE*)> pipe{popen(cmd.c_str(), "r"), pclose};
    if (!pipe) {
        return out;
    }
    std::array<char, 1024> line{};
    while (std::fgets(line.data(), static_cast<int>(line.size()), pipe.get()) != nullptr) {
        std::string_view sv{line.data()};
        // Strip trailing newline.
        while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) {
            sv.remove_suffix(1);
        }
        // Last whitespace-separated field.
        auto pos = sv.find_last_of(" \t");
        std::string_view name = (pos == std::string_view::npos) ? sv : sv.substr(pos + 1);
        if (name.starts_with("fixpp_")) {
            out.emplace(name);
        }
    }
    return out;
}

std::set<std::string> read_golden(std::string const& path) {
    std::set<std::string> out;
    std::ifstream f{path};
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (!line.empty()) {
            out.emplace(line);
        }
    }
    return out;
}

}  // namespace
#endif  // !_WIN32

TEST(AbiSymbolGolden, CabiSymbolSetUnchanged) {
#ifdef _WIN32
    GTEST_SKIP() << "nm/.a exported-symbol golden is Linux-specific (no nm on Windows, "
                    "different archive format + symbol decoration); the C-ABI symbol freeze "
                    "is enforced on Linux by abi-golden.yml + Tier-1. The FR-007 error-enum "
                    "guard (ErrorEnumUnchanged) runs cross-platform.";
#else
    std::set<std::string> const golden = read_golden(FIXPP_CAPI_GOLDEN);
    ASSERT_FALSE(golden.empty()) << "golden file empty/unreadable: " << FIXPP_CAPI_GOLDEN;

    std::set<std::string> const built = exported_fixpp_symbols(FIXPP_CAPI_LIB);
    ASSERT_FALSE(built.empty()) << "nm produced no fixpp_* symbols from: " << FIXPP_CAPI_LIB;

    std::vector<std::string> missing;
    std::vector<std::string> unexpected;
    for (auto const& s : golden) {
        if (!built.contains(s)) {
            missing.push_back(s);
        }
    }
    for (auto const& s : built) {
        if (!golden.contains(s)) {
            unexpected.push_back(s);
        }
    }

    for (auto const& s : missing) {
        ADD_FAILURE() << "MISSING exported symbol (present in golden, absent from build): " << s;
    }
    for (auto const& s : unexpected) {
        ADD_FAILURE() << "UNEXPECTED exported symbol (absent from golden, present in build): " << s;
    }
    EXPECT_EQ(built, golden)
        << "C-ABI exported symbol set drifted from tests/abi/golden/fixpp_capi_symbols.txt "
           "(062 must make NO C-ABI change per FR-007)";
#endif  // _WIN32
}

TEST(AbiSymbolGolden, ErrorEnumUnchanged) {
    using fixpp::core::error;

    // The boundary (see file header): the newest block's last enumerator
    // (092's wire_length_data_mismatch) is the last message-bearing one, and
    // the slot after it is "unknown error". 062 must not have pushed this
    // boundary forward.
    EXPECT_EQ(static_cast<std::uint8_t>(error::app_payload_malformed), 131U)
        << "the pre-062 error-enum boundary (app_payload_malformed) must stay at slot 131";
    EXPECT_EQ(fixpp::core::error_message(static_cast<error>(
                  static_cast<std::uint8_t>(error::wire_length_data_mismatch) + 1U)),
              std::string_view{"unknown error"})
        << "the slot after wire_length_data_mismatch must remain unknown — 062 "
           "introduces no error enumerator (FR-007)";
}
