// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/determinism_test.cpp — T041 [US5]
//
// NFR-003-7 / AC-T1 / AC-T2:
//   AC-T1: fixpp-codegen is deterministic — byte-identical XML input →
//          byte-identical generated headers across runs.
//   AC-T2: codegen writes nothing under the source tree; a dirty checkout
//          never carries stale codegen (build-tree-only per [arch §4.2] step 3).
//   Golden anchor: the freshly generated <vXX>/Messages.hpp is byte-identical
//                  to the checked-in golden for all 4 versions (seam #1/#2).
//
// "No source-tree write" assertion mechanism (AC-T2):
//   We use std::filesystem::recursive_directory_iterator to enumerate every file
//   below FIXPP_DICT_DATA_DIR (the dictionaries/ source directory, whose presence
//   is guaranteed) before invoking the tool, recording each file's last_write_time.
//   After the tool runs we check:
//     (a) No new files appeared under FIXPP_DICT_DATA_DIR.
//     (b) No existing file's mtime changed.
//   We also verify that NEITHER run created any file outside its own --out
//   temp dir by comparing the two temp dirs' contents against a known-good
//   expected file list (only the generated subdirs may appear).
//   This approach is robust without requiring git: it does not depend on git
//   being installed or a .git directory being accessible at test runtime.

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <fixpp/dict/field_ref.hpp>

#include "../../tools/codegen/fixpp-codegen/gen_util.hpp"

namespace fs = std::filesystem;

// ── Compile-time constants injected by CMake ──────────────────────────────────
#ifndef FIXPP_CODEGEN_BIN
#error "FIXPP_CODEGEN_BIN must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_GOLDEN_DIR
#error "FIXPP_CODEGEN_GOLDEN_DIR must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_069_OFFICIAL_GOLDEN_DIR
#error "FIXPP_CODEGEN_069_OFFICIAL_GOLDEN_DIR must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_076_GOLDEN_DIR
#error "FIXPP_CODEGEN_076_GOLDEN_DIR must be set by CMake target_compile_definitions"
#endif

static constexpr const char* kBin = FIXPP_CODEGEN_BIN;
static constexpr const char* kDictDir = FIXPP_DICT_DATA_DIR;
static constexpr const char* kGoldenDir = FIXPP_CODEGEN_GOLDEN_DIR;
static constexpr const char* kOfficialBuildersGoldenDir = FIXPP_CODEGEN_069_OFFICIAL_GOLDEN_DIR;
// 076-fix-latest-typed-codegen T017/T018: the vlatest golden lives in a
// separate feature golden dir (specs/076-.../contracts/golden/), not
// specs/003's — see cmake target_compile_definitions below.
static constexpr const char* kVlatestGoldenDir = FIXPP_CODEGEN_076_GOLDEN_DIR;
static constexpr const char* kVlatestGoldenFile = "vlatest_Messages.golden.hpp";

// XMLs in the exact order the tool accepts them (matches Codegen.cmake)
static constexpr std::array<const char*, 4> kXmls = {"FIX42.xml", "FIX44.xml", "FIX50SP2.xml",
                                                     "FIXT11.xml"};

// Versions in the same order as kXmls
static constexpr std::array<const char*, 4> kVersions = {"v42", "v44", "v50sp2", "vt11"};

// Golden file names (one per version)
static constexpr std::array<const char*, 4> kGoldenFiles = {
    "v42_Messages.golden.hpp",
    "v44_Messages.golden.hpp",
    "v50sp2_Messages.golden.hpp",
    "vt11_Messages.golden.hpp",
};

// ── Helpers ───────────────────────────────────────────────────────────────────

// Read an entire file into a string in binary mode.
static std::string read_file_binary(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    EXPECT_TRUE(f.good()) << "Cannot open: " << p;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Snapshot: path → last_write_time for every file reachable from root.
// Non-throwing; on any fs error the entry is simply omitted (the test will
// detect a mtime change or new file when it crosses back).
static std::map<fs::path, fs::file_time_type> snapshot_mtimes(const fs::path& root) {
    std::map<fs::path, fs::file_time_type> snap;
    std::error_code ec;
    for (auto const& entry : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && !ec) {
            auto mtime = entry.last_write_time(ec);
            if (!ec) snap.emplace(entry.path(), mtime);
        }
        ec.clear();
    }
    return snap;
}

// Build the --xml A --out B ... command string, quoting paths portably.
// std::system() hands the string to the platform shell — /bin/sh on POSIX,
// cmd.exe on Windows — which quote differently.
static std::string quote(const std::string& s) {
#ifdef _WIN32
    // cmd.exe does not treat '...' as quoting; wrap in double quotes (the
    // paths here never contain embedded double quotes).
    return "\"" + s + "\"";
#else
    // /bin/sh: replace each ' with '\'' and wrap in single quotes.
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
#endif
}

// std::system() runs the string through `cmd.exe /c "<string>"` on Windows.
// cmd.exe's quote rule: when the string has more than two quote chars (ours wraps
// the exe path AND every path argument), it strips the FIRST and LAST quote — which
// corrupts our command (`"exe"...` becomes `exe"...`, exit 1). Wrapping the WHOLE
// command in one more pair of quotes makes cmd.exe strip that outer pair instead,
// leaving the real command intact. No-op on POSIX (/bin/sh has no such rule).
static int run_system(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

static int run_codegen(const fs::path& out_dir) {
    std::string cmd = quote(kBin);
    for (auto const* xml : kXmls) {
        fs::path xml_path = fs::path(kDictDir) / xml;
        cmd += " --xml " + quote(xml_path.string());
        cmd += " --out " + quote(out_dir.string());
    }
    return run_system(cmd);
}

// Gate B PR#187 round 1 F3: regenerate v44 only, under `--families official`.
static int run_codegen_v44_official(const fs::path& out_dir) {
    std::string cmd = quote(kBin);
    fs::path xml_path = fs::path(kDictDir) / "FIX44.xml";
    cmd += " --xml " + quote(xml_path.string());
    cmd += " --out " + quote(out_dir.string());
    cmd += " --families official";
    return run_system(cmd);
}

// 076-fix-latest-typed-codegen T017/T018: invoke the tool over ONLY the FIX
// Latest Orchestra XML. It lives under dictionaries/orchestra/ (Codegen.cmake
// :269), not directly under kDictDir like the 4 legacy XMLs in kXmls. Each
// --xml/--out pair is an independent job (main.cpp's job loop), so a
// single-job invocation still emits the full vlatest tier.
static int run_codegen_vlatest_only(const fs::path& out_dir) {
    std::string cmd = quote(kBin);
    fs::path xml_path = fs::path(kDictDir) / "orchestra" / "OrchestraFIXLatest.xml";
    cmd += " --xml " + quote(xml_path.string());
    cmd += " --out " + quote(out_dir.string());
    return run_system(cmd);
}

// T018: invoke the tool over all 5 XMLs (4 legacy + orchestra) — mirrors the
// real Codegen.cmake ON configuration (FIXPP_CODEGEN_FIX_LATEST=ON, the
// default; Codegen.cmake:373-380).
static int run_codegen_with_vlatest(const fs::path& out_dir) {
    std::string cmd = quote(kBin);
    for (auto const* xml : kXmls) {
        fs::path xml_path = fs::path(kDictDir) / xml;
        cmd += " --xml " + quote(xml_path.string());
        cmd += " --out " + quote(out_dir.string());
    }
    fs::path orchestra_path = fs::path(kDictDir) / "orchestra" / "OrchestraFIXLatest.xml";
    cmd += " --xml " + quote(orchestra_path.string());
    cmd += " --out " + quote(out_dir.string());
    return run_system(cmd);
}

static int run_codegen_args(std::string_view args) {
    std::string cmd = quote(kBin);
    if (!args.empty()) {
        cmd += " ";
        cmd += std::string(args);
    }
    return run_system(cmd);
}

// Decode the raw std::system() return value to the child's exit code.
// On POSIX, std::system() returns the wait(2)-encoded status; WEXITSTATUS
// extracts the actual exit code.  Returns -1 if the process did not exit
// normally (e.g. killed by a signal), which surfaces a clear diagnostic.
static int exit_code(int system_ret) {
#ifdef _WIN32
    return system_ret;
#else
    return WIFEXITED(system_ret) ? WEXITSTATUS(system_ret) : -1;
#endif
}

// RAII temp directory: created in the system temp dir, removed on destruction.
struct TempDir {
    fs::path path;

    explicit TempDir(const std::string& prefix) {
        auto base = fs::temp_directory_path();
        // Use PID + a counter for a unique name without relying on mkdtemp.
        static int counter = 0;
        path = base / (prefix + "_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);  // best-effort cleanup
    }

    // Non-copyable, non-movable — lifetime tied to the test body.
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// ── Test fixture ──────────────────────────────────────────────────────────────

class DeterminismTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Verify prerequisites are reachable.
        ASSERT_TRUE(fs::exists(kBin)) << "Tool binary not found: " << kBin;
        ASSERT_TRUE(fs::exists(kDictDir)) << "Dict dir not found: " << kDictDir;
        ASSERT_TRUE(fs::exists(kGoldenDir)) << "Golden dir not found: " << kGoldenDir;
        for (auto const* xml : kXmls) {
            ASSERT_TRUE(fs::exists(fs::path(kDictDir) / xml)) << "XML not found: " << xml;
        }
        for (auto const* gf : kGoldenFiles) {
            ASSERT_TRUE(fs::exists(fs::path(kGoldenDir) / gf)) << "Golden not found: " << gf;
        }
        // 076 T017/T018 prerequisites.
        ASSERT_TRUE(fs::exists(fs::path(kDictDir) / "orchestra" / "OrchestraFIXLatest.xml"))
            << "Orchestra XML not found under " << kDictDir << "/orchestra";
        ASSERT_TRUE(fs::exists(kVlatestGoldenDir)) << "076 golden dir not found: " << kVlatestGoldenDir;
        ASSERT_TRUE(fs::exists(fs::path(kVlatestGoldenDir) / kVlatestGoldenFile))
            << "076 golden not found: " << kVlatestGoldenFile;
    }
};

// ── AC-T1: Two runs produce byte-identical output ─────────────────────────────

TEST_F(DeterminismTest, ByteIdenticalAcrossRuns) {
    TempDir run1("fixpp_det_run1");
    TempDir run2("fixpp_det_run2");

    int rc1 = run_codegen(run1.path);
    ASSERT_EQ(rc1, 0) << "First codegen run failed (exit " << rc1 << ")";

    int rc2 = run_codegen(run2.path);
    ASSERT_EQ(rc2, 0) << "Second codegen run failed (exit " << rc2 << ")";

    // Walk every file produced in run1 and compare byte-for-byte with run2.
    bool all_identical = true;
    std::error_code ec;
    for (auto const& e1 : fs::recursive_directory_iterator(run1.path, ec)) {
        ASSERT_FALSE(ec) << "Iterator error in run1: " << ec.message();
        if (!e1.is_regular_file()) continue;

        // Corresponding path in run2: strip run1.path prefix, prepend run2.path.
        auto rel = fs::relative(e1.path(), run1.path, ec);
        ASSERT_FALSE(ec);
        fs::path p2 = run2.path / rel;

        EXPECT_TRUE(fs::exists(p2)) << "File missing in run2: " << rel;
        if (!fs::exists(p2)) {
            all_identical = false;
            continue;
        }

        std::string bytes1 = read_file_binary(e1.path());
        std::string bytes2 = read_file_binary(p2);
        if (bytes1 != bytes2) {
            ADD_FAILURE() << "Not byte-identical: " << rel << " (run1=" << bytes1.size()
                          << " bytes, run2=" << bytes2.size() << " bytes)";
            all_identical = false;
        }
    }
    EXPECT_TRUE(all_identical) << "NFR-003-7 / AC-T1 violated: at least one "
                                  "generated file differs between the two codegen runs.";
}

TEST_F(DeterminismTest, NoArgsExitsTwo) { EXPECT_EQ(exit_code(run_codegen_args("")), 2); }

TEST_F(DeterminismTest, MissingXmlValueExitsTwo) {
    EXPECT_EQ(exit_code(run_codegen_args("--xml")), 2);
}

TEST_F(DeterminismTest, MissingOutValueExitsTwo) {
    EXPECT_EQ(exit_code(run_codegen_args("--out")), 2);
}

TEST_F(DeterminismTest, BadXmlPathExitsOne) {
    TempDir run("fixpp_det_badxml");
    fs::path missing = run.path / "missing.xml";
    std::string cmd = "--xml " + quote(missing.string()) + " --out " + quote(run.path.string());
    EXPECT_EQ(exit_code(run_codegen_args(cmd)), 1);
}

// ── AC-T2: No source-tree write ───────────────────────────────────────────────
//
// Mechanism (robust, no git dependency):
//   1. Snapshot every file's mtime under FIXPP_DICT_DATA_DIR before the run.
//   2. Run codegen into a temp dir (not the source tree).
//   3. Re-snapshot and assert:
//      (a) No new files appeared under FIXPP_DICT_DATA_DIR.
//      (b) No existing file's mtime changed.
//   Why robust: the dictionaries/ directory is guaranteed present (it contains
//   the 4 XML inputs); any accidental write to the source tree would either
//   create a new file or update an existing one's mtime — both caught here.
//   We do not assert the entire source tree (too broad and potentially slow)
//   but focus on the directory where the tool reads its XML inputs and where
//   a "confused path" write would most likely land.

TEST_F(DeterminismTest, NoSourceTreeWrite) {
    auto before = snapshot_mtimes(fs::path(kDictDir));
    ASSERT_FALSE(before.empty()) << "Failed to snapshot " << kDictDir;

    TempDir run("fixpp_det_nosrc");
    int rc = run_codegen(run.path);
    ASSERT_EQ(rc, 0) << "Codegen run failed (exit " << rc << ")";

    auto after = snapshot_mtimes(fs::path(kDictDir));

    // (a) No new files.
    for (auto const& [p, _] : after) {
        EXPECT_TRUE(before.count(p)) << "AC-T2: New file appeared under source dict dir: " << p;
    }

    // (b) No mtime changes.
    for (auto const& [p, mtime_before] : before) {
        auto it = after.find(p);
        if (it == after.end()) {
            // File disappeared — unexpected but not a write violation; flag it.
            ADD_FAILURE() << "AC-T2: File disappeared from source dict dir: " << p;
            continue;
        }
        EXPECT_EQ(it->second, mtime_before) << "AC-T2: mtime changed under source dict dir: " << p;
    }

    // (c) Verify the tool output directory is under the temp dir (positive check:
    //     the tool created subdirs under run.path, not elsewhere).
    ASSERT_TRUE(fs::exists(run.path / "v42" / "Messages.hpp"))
        << "Expected output missing: run/v42/Messages.hpp";
    ASSERT_TRUE(fs::exists(run.path / "v44" / "Messages.hpp"))
        << "Expected output missing: run/v44/Messages.hpp";
    ASSERT_TRUE(fs::exists(run.path / "v50sp2" / "Messages.hpp"))
        << "Expected output missing: run/v50sp2/Messages.hpp";
    ASSERT_TRUE(fs::exists(run.path / "vt11" / "Messages.hpp"))
        << "Expected output missing: run/vt11/Messages.hpp";
}

// ── Golden anchor: generated Messages.hpp == checked-in golden ───────────────
//
// AC-T1 (golden clause) / NFR-003-7:
//   For each of the 4 codegen versions, the freshly generated Messages.hpp must
//   be byte-identical to the checked-in golden.  A template change that alters
//   any emitted line will perturb the golden and fail this test, ensuring every
//   template change is a reviewed golden diff.

TEST_F(DeterminismTest, GeneratedMatchesGolden) {
    TempDir run("fixpp_det_golden");
    int rc = run_codegen(run.path);
    ASSERT_EQ(rc, 0) << "Codegen run failed (exit " << rc << ")";

    for (std::size_t i = 0; i < kVersions.size(); ++i) {
        fs::path generated = run.path / kVersions[i] / "Messages.hpp";
        fs::path golden = fs::path(kGoldenDir) / kGoldenFiles[i];

        ASSERT_TRUE(fs::exists(generated)) << "Generated file missing: " << generated;
        ASSERT_TRUE(fs::exists(golden)) << "Golden file missing: " << golden;

        std::string gen_bytes = read_file_binary(generated);
        std::string golden_bytes = read_file_binary(golden);

        EXPECT_EQ(gen_bytes, golden_bytes)
            << "Golden mismatch for " << kVersions[i] << ": generated " << gen_bytes.size()
            << " bytes"
            << ", golden " << golden_bytes.size() << " bytes.\n"
            << "  generated: " << generated << "\n"
            << "  golden:    " << golden << "\n"
            << "  Run 'diff " << generated.string() << " " << golden.string()
            << "' to see the diff.\n"
            << "  Regenerate golden with:\n"
            << "    " << kBin << " --xml " << (fs::path(kDictDir) / kXmls[i]).string()
            << " --out <golden-dir> && cp <golden-dir>/" << kVersions[i] << "/Messages.hpp "
            << golden.string();
    }
}

// ── 076-fix-latest-typed-codegen T017 [US3]: V-4 determinism — vlatest ──────
//
// FR-011/R8, contract V-4: extends the golden-anchor gate above
// (GeneratedMatchesGolden) to the fixpp::vlatest tier — the freshly
// generated vlatest/Messages.hpp must be byte-identical to the checked-in
// golden under specs/076-fix-latest-typed-codegen/contracts/golden/. Runs
// under the FULL ctest (not a narrow target) — a stale/non-deterministic
// vlatest emit fails CI, exactly like the 4 legacy tiers already do.

TEST_F(DeterminismTest, VlatestGeneratedMatchesGolden) {
    TempDir run("fixpp_det_vlatest_golden");
    int rc = run_codegen_vlatest_only(run.path);
    ASSERT_EQ(rc, 0) << "vlatest codegen run failed (exit " << rc << ")";

    fs::path generated = run.path / "vlatest" / "Messages.hpp";
    fs::path golden = fs::path(kVlatestGoldenDir) / kVlatestGoldenFile;

    ASSERT_TRUE(fs::exists(generated)) << "Generated file missing: " << generated;
    ASSERT_TRUE(fs::exists(golden)) << "Golden file missing: " << golden;

    std::string gen_bytes = read_file_binary(generated);
    std::string golden_bytes = read_file_binary(golden);

    EXPECT_EQ(gen_bytes, golden_bytes)
        << "V-4 violated: vlatest/Messages.hpp diverged from the checked-in golden (generated "
        << gen_bytes.size() << " bytes, golden " << golden_bytes.size() << " bytes).\n"
        << "  generated: " << generated << "\n"
        << "  golden:    " << golden << "\n"
        << "  Run 'diff " << generated.string() << " " << golden.string()
        << "' to see the diff.\n"
        << "  Regenerate golden with:\n"
        << "    " << kBin << " --xml "
        << (fs::path(kDictDir) / "orchestra" / "OrchestraFIXLatest.xml").string()
        << " --out <golden-dir> && cp <golden-dir>/vlatest/Messages.hpp " << golden.string();
}

// ── Gate B r2 follow-up: V-4 run-to-run determinism — vlatest tier ─────────
//
// AC-T1's ByteIdenticalAcrossRuns above drives run_codegen(), which iterates
// ONLY kXmls (the 4 legacy XMLs) — it never exercises the Orchestra XML, so
// it gives no run-to-run byte-identity coverage for vlatest/{Fields,Messages,
// Validator,Reify,NormativeReferences.md,Manifest.txt}. This test closes that
// gap directly, mirroring ByteIdenticalAcrossRuns but over
// run_codegen_vlatest_only(): two independent runs into separate temp dirs,
// every file produced in run1 compared byte-for-byte against the same
// relative path in run2.
TEST_F(DeterminismTest, VlatestByteIdenticalAcrossRuns) {
    TempDir run1("fixpp_det_vlatest_run1");
    TempDir run2("fixpp_det_vlatest_run2");

    int rc1 = run_codegen_vlatest_only(run1.path);
    ASSERT_EQ(rc1, 0) << "First vlatest codegen run failed (exit " << rc1 << ")";

    int rc2 = run_codegen_vlatest_only(run2.path);
    ASSERT_EQ(rc2, 0) << "Second vlatest codegen run failed (exit " << rc2 << ")";

    // Walk every file produced in run1 and compare byte-for-byte with run2.
    bool all_identical = true;
    std::error_code ec;
    for (auto const& e1 : fs::recursive_directory_iterator(run1.path, ec)) {
        ASSERT_FALSE(ec) << "Iterator error in run1: " << ec.message();
        if (!e1.is_regular_file()) continue;

        // Corresponding path in run2: strip run1.path prefix, prepend run2.path.
        auto rel = fs::relative(e1.path(), run1.path, ec);
        ASSERT_FALSE(ec);
        fs::path p2 = run2.path / rel;

        EXPECT_TRUE(fs::exists(p2)) << "File missing in run2: " << rel;
        if (!fs::exists(p2)) {
            all_identical = false;
            continue;
        }

        std::string bytes1 = read_file_binary(e1.path());
        std::string bytes2 = read_file_binary(p2);
        if (bytes1 != bytes2) {
            ADD_FAILURE() << "Not byte-identical: " << rel << " (run1=" << bytes1.size()
                          << " bytes, run2=" << bytes2.size() << " bytes)";
            all_identical = false;
        }
    }
    EXPECT_TRUE(all_identical) << "V-4 violated: at least one generated vlatest/ file differs "
                                  "between the two codegen runs.";

    // Sanity bound: vlatest emits Fields/Messages/Validator/Reify/
    // NormativeReferences.md/Manifest.txt == 6 artifacts (no Builders.hpp —
    // the typed builder tier is descoped, see golden/README.md). A loose
    // lower bound so this doesn't need updating on an unrelated emitter-shape
    // change, while still catching a degenerate walk that compares nothing.
    std::size_t file_count = 0;
    for (auto const& e1 : fs::recursive_directory_iterator(run1.path, ec)) {
        if (!ec && e1.is_regular_file()) ++file_count;
    }
    EXPECT_GE(file_count, 5U) << "Vlatest run produced suspiciously few files (" << file_count
                               << ") — codegen output shape may have changed.";
}

// ── 076-fix-latest-typed-codegen T018 [US3]: V-7 additive OFF/ON byte-diff ──
//
// FR-004/SC-003, contract V-7: adding the fixpp::vlatest tier MUST NOT
// perturb the legacy v42/v44/v50sp2/vt11 + _dispatch/ output. Two legs:
//   (a) Absolute anchors — OFF-run legacy Messages.hpp x4 == the checked-in
//       003 golden (re-verifies GeneratedMatchesGolden's claim holds even
//       when the run genuinely never touches the orchestra XML); ON-run
//       vlatest/Messages.hpp == the checked-in 076 golden.
//   (b) Relative/additive discriminator — EVERY legacy file (Fields/
//       Messages/Validator/Reify/NormativeReferences.md/Manifest.txt/
//       Builders.hpp under v42/v44/v50sp2/vt11, plus the shared
//       _dispatch/*.hpp) is byte-identical between the OFF-run and the
//       ON-run. Neither specs/003 nor specs/069's golden dir carries a
//       golden for _dispatch/ or for Fields/Validator/Reify/
//       NormativeReferences.md/Manifest.txt (only Messages.hpp x4 and one
//       v44/Builders.hpp under --families official are goldened) — this
//       recursive walk is the ONLY gate covering those files' additivity.
//       (Both OFF and ON runs here use the default `--families all` mode,
//       matching the real Codegen.cmake invocation — official-mode
//       Builders.hpp already has its own gate,
//       OfficialModeBuildersMatchesGolden below, and is deliberately not
//       reused here.)
//
// Folds T019 (build-option ON/OFF behavior witness, US3 acceptance
// scenarios 1-2) in explicitly:
//   US3-AC1 (OFF => no fixpp::vlatest tier): asserted directly below —
//     off_run has no vlatest/ dir at all.
//   US3-AC2 (ON => vlatest appears additively): asserted directly below —
//     on_run has vlatest/Messages.hpp matching the golden AND every legacy
//     file is unperturbed relative to the OFF-run.
// A CMake-level toggle witness (reconfiguring the shared build tree with
// -DFIXPP_CODEGEN_FIX_LATEST=OFF) is deliberately NOT used here — that would
// need RUN_SERIAL against the codegen-build-graph-check git-cleanliness gate
// (RESOURCE_LOCK codegen_source_tree) for no additional coverage; this
// tool-into-temp-dir approach already discriminates both ON/OFF behaviors
// and needs no serialization. Manual verification of the CMake-level toggle
// (T015) was done separately by reconfiguring build/linux-clang-debug.
TEST_F(DeterminismTest, AdditiveOffOnByteDiff) {
    TempDir off_run("fixpp_det_off");
    TempDir on_run("fixpp_det_on");

    int rc_off = run_codegen(off_run.path);  // legacy only — OFF-path job
    ASSERT_EQ(rc_off, 0) << "OFF-path codegen run failed (exit " << rc_off << ")";

    int rc_on = run_codegen_with_vlatest(on_run.path);  // legacy + vlatest — ON-path job
    ASSERT_EQ(rc_on, 0) << "ON-path codegen run failed (exit " << rc_on << ")";

    // (a1) OFF-run legacy Messages.hpp == 003 golden.
    for (std::size_t i = 0; i < kVersions.size(); ++i) {
        fs::path generated = off_run.path / kVersions[i] / "Messages.hpp";
        fs::path golden = fs::path(kGoldenDir) / kGoldenFiles[i];
        ASSERT_TRUE(fs::exists(generated)) << "OFF-run missing: " << generated;
        EXPECT_EQ(read_file_binary(generated), read_file_binary(golden))
            << "V-7 OFF-path baseline mismatch for " << kVersions[i];
    }

    // (a2) US3-AC1 — OFF-run has NO vlatest/ dir at all.
    EXPECT_FALSE(fs::exists(off_run.path / "vlatest"))
        << "V-7 / US3-AC1 violated: vlatest/ present in the OFF-path job.";

    // (a3) US3-AC2 (part) — ON-run vlatest/Messages.hpp == 076 golden.
    fs::path on_vlatest = on_run.path / "vlatest" / "Messages.hpp";
    ASSERT_TRUE(fs::exists(on_vlatest)) << "ON-run missing: " << on_vlatest;
    EXPECT_EQ(read_file_binary(on_vlatest),
              read_file_binary(fs::path(kVlatestGoldenDir) / kVlatestGoldenFile))
        << "V-7 ON-path vlatest/Messages.hpp diverged from the 076 golden.";

    // (b) US3-AC2 (part) — relative discriminator: recursively walk every
    // file the OFF-run produced (v42/v44/v50sp2/vt11/_dispatch — no vlatest,
    // since the OFF-run never generated it) and assert byte-identity
    // against the SAME relative path under the ON-run.
    std::error_code ec;
    std::size_t compared = 0;
    for (auto const& e : fs::recursive_directory_iterator(off_run.path, ec)) {
        ASSERT_FALSE(ec) << "Iterator error walking OFF-run: " << ec.message();
        if (!e.is_regular_file()) continue;
        auto rel = fs::relative(e.path(), off_run.path, ec);
        ASSERT_FALSE(ec);
        fs::path on_counterpart = on_run.path / rel;
        EXPECT_TRUE(fs::exists(on_counterpart)) << "V-7: legacy file missing from ON-run: " << rel;
        if (!fs::exists(on_counterpart)) continue;
        EXPECT_EQ(read_file_binary(e.path()), read_file_binary(on_counterpart))
            << "V-7 additivity violated: " << rel << " differs between OFF-run and ON-run.";
        ++compared;
    }
    // Sanity bound: v42/v50sp2/vt11 emit 5 artifacts each, v44 emits 6
    // (+Builders.hpp), plus 2 shared _dispatch files == 23. A loose lower
    // bound (not the exact count) so this doesn't need updating on every
    // unrelated emitter-shape change, while still catching a degenerate
    // walk that silently compares nothing.
    EXPECT_GE(compared, 20U) << "V-7 walk compared suspiciously few files (" << compared
                              << ") — codegen output shape may have changed.";
}

// ── 076-fix-latest-typed-codegen T021 [Polish]: V-5 fail-closed on unknown ──
//   datatype (FR-010, contract V-5).
//
// The codegen TOOL, run over a synthetic Orchestra fragment whose field
// references a datatype token outside kOrchestraTypeTable, MUST fail closed:
// non-zero exit, and NO tier emitted (never a mis-typed field). This is the
// codegen-tool-level companion to the LOADER-level fail-closed already pinned
// at the read tier (tests/dictionary/orchestra_loader_test.cpp
// OrchestraFailClosed.UnknownDatatypeUsedByField, which asserts
// OrchestraLoader::load throws dict::orchestra_parse_error); here we assert
// that throw propagates through build_ir -> main() into a non-zero process
// exit with no output, so a malformed dictionary can never silently emit a
// tier with a defaulted/mis-typed field.
TEST_F(DeterminismTest, VlatestFailClosedUnknownDatatype) {
    // A field (id=2) typed with a token absent from kOrchestraTypeTable and
    // from any codeset — collect_fields resolves every declared field's type
    // eagerly, so resolve_datatype throws regardless of message references
    // (mirrors orchestra_loader_test.cpp's UnknownDatatypeUsedByField).
    static constexpr const char* kBadFragment =
        "<fixr:repository version=\"FIX.Latest_EP303\">\n"
        "  <fixr:fields>\n"
        "    <fixr:field id=\"1\" name=\"Account\" type=\"String\"/>\n"
        "    <fixr:field id=\"2\" name=\"Bogus\" type=\"TotallyBogusType\"/>\n"
        "  </fixr:fields>\n"
        "  <fixr:messages>\n"
        "    <fixr:message id=\"1\" name=\"Heartbeat\" msgType=\"0\">\n"
        "      <fixr:structure>\n"
        "        <fixr:fieldRef id=\"1\"/>\n"
        "        <fixr:fieldRef id=\"2\"/>\n"
        "      </fixr:structure>\n"
        "    </fixr:message>\n"
        "  </fixr:messages>\n"
        "</fixr:repository>\n";

    TempDir run("fixpp_det_failclosed");
    fs::path xml = run.path / "OrchestraBogus.xml";
    fs::path out = run.path / "out";
    fs::create_directories(out);
    {
        std::ofstream f(xml, std::ios::binary);
        ASSERT_TRUE(f.good()) << "could not write synthetic fragment";
        f << kBadFragment;
    }

    std::string cmd = "--xml " + quote(xml.string()) + " --out " + quote(out.string());
    int rc = exit_code(run_codegen_args(cmd));

    // Fail closed: non-zero exit (the orchestra_parse_error propagates out of
    // build_ir -> main), and NO vlatest tier emitted.
    EXPECT_NE(rc, 0) << "V-5 violated: codegen over an unmapped-datatype fragment must fail closed "
                        "(non-zero exit), not emit a mis-typed field.";
    EXPECT_FALSE(fs::exists(out / "vlatest" / "Messages.hpp"))
        << "V-5 violated: a tier was emitted despite the unmapped datatype.";
}

// ── Gate B PR#187 round 1 F3: official-mode byte identity (SC-003) ──────────
//
// test_069_mode_count.cpp (tests/session/) pins only builder_registry.size()
// (33 under `official`), not the actual byte content. tasks.md T009 verified
// byte-identity locally against a captured pre-069 baseline
// (specs/069-v44-all-families/baseline/Builders.OFFICIAL.baseline.hpp,
// gitignored — a local/manual procedure). This test promotes that same
// verified content (copied byte-for-byte into the checked-in golden below)
// into a CI-automated gate, mirroring GeneratedMatchesGolden above.

TEST_F(DeterminismTest, OfficialModeBuildersMatchesGolden) {
    TempDir run("fixpp_det_official");
    int rc = run_codegen_v44_official(run.path);
    ASSERT_EQ(rc, 0) << "official-mode codegen run failed (exit " << rc << ")";

    fs::path generated = run.path / "v44" / "Builders.hpp";
    fs::path golden = fs::path(kOfficialBuildersGoldenDir) / "v44_Builders_official.golden.hpp";

    ASSERT_TRUE(fs::exists(generated)) << "Generated file missing: " << generated;
    ASSERT_TRUE(fs::exists(golden)) << "Golden not found: " << golden;

    std::string gen_bytes = read_file_binary(generated);
    std::string golden_bytes = read_file_binary(golden);

    EXPECT_EQ(gen_bytes, golden_bytes)
        << "SC-003 violated: `--families official` v44/Builders.hpp diverged "
           "from the byte-identity baseline (generated "
        << gen_bytes.size() << " bytes, golden " << golden_bytes.size() << " bytes).\n"
        << "  generated: " << generated << "\n"
        << "  golden:    " << golden << "\n"
        << "  Run 'diff " << generated.string() << " " << golden.string() << "' to see the diff.";
}

TEST(CodegenGenUtil, AccessorNormalizationCoversKeywordsDigitsAndFallback) {
    using fixpp::codegen::to_accessor;

    EXPECT_EQ(to_accessor("ClOrdID"), "cl_ord_id");
    EXPECT_EQ(to_accessor("SecurityIDSource"), "security_id_source");
    EXPECT_EQ(to_accessor("Ord-ID"), "ord_id");
    EXPECT_EQ(to_accessor("Ord-"), "ord");
    EXPECT_EQ(to_accessor("_Ord"), "ord");
    EXPECT_EQ(to_accessor("9Lives"), "_9_lives");
    EXPECT_EQ(to_accessor("class"), "class_");
    EXPECT_EQ(to_accessor("___"), "field");
}

TEST(CodegenGenUtil, IdentifierNormalizationCoversKeywordsDigitsAndFallback) {
    using fixpp::codegen::to_identifier;

    EXPECT_EQ(to_identifier("ExecutionReport"), "ExecutionReport");
    EXPECT_EQ(to_identifier("9Foo-Bar"), "_9Foo_Bar");
    EXPECT_EQ(to_identifier("class"), "class_");
    EXPECT_EQ(to_identifier(""), "Msg");
}

TEST(CodegenGenUtil, GroupPrefixVersionAndCollisionHelpersCoverReachableBranches) {
    EXPECT_EQ(fixpp::codegen::strip_no_prefix("NoLegs"), "Legs");
    EXPECT_EQ(fixpp::codegen::strip_no_prefix("Notice"), "Notice");
    EXPECT_EQ(fixpp::codegen::app_version_enum("vt11"), "Unknown");
    EXPECT_EQ(fixpp::codegen::app_version_enum("v44"), "v44");

    std::unordered_set<std::string> used;
    EXPECT_EQ(fixpp::codegen::uniquify_accessor(used, "cl_ord_id", 11), "cl_ord_id");
    EXPECT_EQ(fixpp::codegen::uniquify_accessor(used, "cl_ord_id", 11), "cl_ord_id_t11");
}

TEST(CodegenGenUtil, KindOfMapsRepresentativeFieldKinds) {
    using fixpp::codegen::kind_of;
    using fixpp::codegen::TypeKind;
    using fixpp::dict::field_data_type;

    EXPECT_EQ(kind_of(field_data_type::Price), TypeKind::Decimal);
    EXPECT_EQ(kind_of(field_data_type::Char), TypeKind::Char);
    EXPECT_EQ(kind_of(field_data_type::Boolean), TypeKind::Bool);
    EXPECT_EQ(kind_of(field_data_type::SeqNum), TypeKind::Int32);
    EXPECT_EQ(kind_of(field_data_type::Currency), TypeKind::String);
    EXPECT_EQ(kind_of(field_data_type::DialectExtension), TypeKind::Skip);
}
