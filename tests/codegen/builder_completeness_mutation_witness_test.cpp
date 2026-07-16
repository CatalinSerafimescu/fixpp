// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/builder_completeness_mutation_witness_test.cpp
//
// 077-builder-args-dedup T025 [US4] -- proves the T023 builder-completeness
// census can go RED (C3b, SC-006, quickstart V6;
// specs/077-builder-args-dedup/tasks.md T025). Standalone, §8-EXEMPT
// (isolation-sensitive: spawns a subprocess and writes to a scratch dir --
// mirrors codegen_determinism_test.cpp's TempDir/run_system pattern; NOT
// folded into a whole-binary target).
//
// MECHANISM (per feedback_sanitizer_canary_must_be_proven_red -- a canary
// never observed red proves nothing): this test invokes the T024 mutation
// seam's fixpp-codegen-drop-witness binary (built with
// -DFIXPP_CODEGEN_DROP_BUILDER_MSGTYPE="D") in a SUBPROCESS against
// FIX44.xml only, regenerating v44/Builders.hpp into an isolated temp dir
// with msgtype "D" (NewOrderSingle) missing from both the builder_registry
// array and its build_<Msg>/validate_<Msg> definitions. It then runs the
// SAME runtime comparison the real census
// (test_077_v44_builder_completeness.cpp ::RegistryExactSetEqualsRawXmlWalk
// and ::BuildFnSignaturesMatchRawXmlWalk) performs -- via the SAME
// builder_completeness_common.hpp functions, parameterized on the dropped
// header's path -- and asserts the comparison DETECTS the drop (goes RED)
// naming exactly "D" as the missing msg_type. The witness test itself
// PASSES iff the census mechanism correctly flags the drop; it FAILS if the
// mechanism is broken (e.g. silently tolerates the missing message), so
// green here IS the "proven red" proof for C3b.
//
// SCOPE NOTE (advisor-confirmed, see implementation report): the address-of
// compile-time leg (C2, test_077_v44_builder_completeness.cpp's kEntries
// array) is NOT re-probed here -- dropping a message from Builders.hpp
// would make an address-of TU built against it FAIL TO COMPILE, not fail an
// assertion, so there is no "rerun and observe an assertion go red" for
// that leg; its "red" is the language-guaranteed compile error, a
// documented property, not something a runtime harness can demonstrate.
// This witness instead exercises the runtime TEXT-COMPARISON legs (registry
// array parse + independent build_<Msg>( signature scan), which is exactly
// what C3b's "the census goes RED with the expected missing msg_type"
// requires.
//
// Anchors: specs/077-builder-args-dedup/tasks.md T024/T025;
//          contracts/builder-completeness.md C3b;
//          tests/codegen/determinism_test.cpp (subprocess/TempDir pattern).

#include "builder_completeness_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef FIXPP_CODEGEN_DROP_WITNESS_BIN
#error "FIXPP_CODEGEN_DROP_WITNESS_BIN must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif

namespace fs = std::filesystem;

namespace {

// Mirrors determinism_test.cpp's quote()/run_system() -- portable
// std::system() argv quoting for the subprocess invocation.
std::string quote(std::string const& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
#endif
}

int run_system(std::string const& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

int exit_code(int system_ret) {
#ifdef _WIN32
    return system_ret;
#else
    return WIFEXITED(system_ret) ? WEXITSTATUS(system_ret) : -1;
#endif
}

struct TempDir {
    fs::path path;
    explicit TempDir(std::string const& prefix) {
        auto const base = fs::temp_directory_path();
        static int counter = 0;
#ifdef _WIN32
        auto const pid = static_cast<long>(_getpid());
#else
        auto const pid = static_cast<long>(getpid());
#endif
        path = base / (prefix + "_" + std::to_string(pid) + "_" + std::to_string(counter++));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(TempDir const&) = delete;
    TempDir& operator=(TempDir const&) = delete;
};

}  // namespace

// C3b -- the committed mutation seam (T024) makes the completeness census
// (T023) go RED, naming exactly "D" as the missing msg_type.
TEST(BuilderCompletenessMutationWitness, DropOneMessageMakesCensusDetectItRed) {
    ASSERT_TRUE(fs::exists(FIXPP_CODEGEN_DROP_WITNESS_BIN))
        << "fixpp-codegen-drop-witness not built -- CMake dependency wiring is broken";

    TempDir out("fixpp_bc_mutation_witness");
    std::string cmd = quote(FIXPP_CODEGEN_DROP_WITNESS_BIN);
    cmd += " --xml " + quote(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml");
    cmd += " --out " + quote(out.path.string());
    int const rc = exit_code(run_system(cmd));
    ASSERT_EQ(rc, 0) << "fixpp-codegen-drop-witness subprocess failed, cmd=" << cmd;

    std::string const dropped_builders_hpp = (out.path / "v44" / "Builders.hpp").string();
    ASSERT_TRUE(fs::exists(dropped_builders_hpp)) << "dropped v44/Builders.hpp was not emitted";

    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const expected =
        legacy_expected_msgtypes(std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml", {"BE", "BF", "BW", "BX", "BY"});
    ASSERT_EQ(expected.count("D"), 1U) << "sanity: \"D\" (NewOrderSingle) must be in the raw-XML in-scope set";

    // Leg 1: registry-array parse (mirrors
    // BuilderCompleteness077V44.RegistryExactSetEqualsRawXmlWalk).
    std::set<std::string> const dropped_registry = parse_registry_msgtypes(dropped_builders_hpp);
    EXPECT_EQ(dropped_registry.count("D"), 0U) << "the drop seam did not remove \"D\" from builder_registry";
    EXPECT_NE(dropped_registry, expected) << "registry-vs-raw-XML-walk comparison did NOT go RED for the drop";

    std::vector<std::string> missing;
    std::set_difference(expected.begin(), expected.end(), dropped_registry.begin(), dropped_registry.end(),
                         std::back_inserter(missing));
    ASSERT_EQ(missing.size(), 1U) << "expected exactly one message missing, got " << missing.size();
    EXPECT_EQ(missing.front(), "D");
    std::cerr << "[builder_completeness_mutation_witness] RED CONFIRMED: registry-array leg detected missing "
                 "msg_type=\""
              << missing.front() << "\" (expected.size()=" << expected.size()
              << ", dropped_registry.size()=" << dropped_registry.size() << ")\n";

    // Leg 2: independent build_<Msg>( signature scan (mirrors
    // BuilderCompleteness077V44.BuildFnSignaturesMatchRawXmlWalk) -- also
    // catches the drop, via a differently-shaped parse than leg 1.
    std::set<std::string> const dropped_build_idents = parse_build_fn_identifiers(dropped_builders_hpp);
    EXPECT_EQ(dropped_build_idents.count("NewOrderSingle"), 0U)
        << "the drop seam did not remove build_NewOrderSingle( from the header";
    EXPECT_EQ(dropped_build_idents.size(), expected.size() - 1)
        << "build_<Msg>( signature count should be exactly one less than the raw-XML walk count";
    std::cerr << "[builder_completeness_mutation_witness] RED CONFIRMED: build_<Msg>( signature-scan leg detected "
                 "missing build_NewOrderSingle( (expected.size()="
              << expected.size() << ", dropped_build_idents.size()=" << dropped_build_idents.size() << ")\n";
}
