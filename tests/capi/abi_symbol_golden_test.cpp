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
//     completeness gate. tests/core/test_017/019/020_error_completeness.cpp
//     already assert (per feature) the exact enumerator SET each feature
//     introduced, with test_020's ExactSetEquality pinning the CURRENT
//     highest-known boundary: slot 131 (app_payload_malformed) carries a
//     message, slot 132 is "unknown error" (nothing added beyond 020's
//     boundary). Since 062 introduces no error enumerator, the correct
//     062-scoped check is simply: that boundary is STILL exactly where
//     test_020 left it — i.e. 062 did not push it forward. If a future
//     feature legitimately extends the enum, it will add its OWN
//     test_0NN_error_completeness.cpp (per the established per-feature
//     pattern) and this test's slot-132 boundary assertion is expected to
//     move in lockstep — that is the intended coupling, not a maintenance
//     trap: this test is 062's evidence that IT did not move the boundary.

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

TEST(AbiSymbolGolden, CabiSymbolSetUnchanged) {
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
}

TEST(AbiSymbolGolden, ErrorEnumUnchanged) {
    using fixpp::core::error;

    // The 020 boundary (see file header): slot 131 is the last message-
    // bearing enumerator, slot 132 is "unknown error". 062 must not have
    // pushed this boundary forward.
    EXPECT_EQ(static_cast<std::uint8_t>(error::app_payload_malformed), 131u)
        << "the pre-062 error-enum boundary (app_payload_malformed) must stay at slot 131";
    EXPECT_EQ(fixpp::core::error_message(static_cast<error>(132u)), std::string_view{"unknown error"})
        << "slot 132 must remain unknown — 062 introduces no error enumerator (FR-007)";
}
