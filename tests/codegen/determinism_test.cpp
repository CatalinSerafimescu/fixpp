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

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
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
#ifndef FIXPP_CODEGEN_076_GOLDEN_DIR
#error "FIXPP_CODEGEN_076_GOLDEN_DIR must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_078_GOLDEN_DIR
#error "FIXPP_CODEGEN_078_GOLDEN_DIR must be set by CMake target_compile_definitions"
#endif

static constexpr const char* kBin = FIXPP_CODEGEN_BIN;
static constexpr const char* kDictDir = FIXPP_DICT_DATA_DIR;
static constexpr const char* kGoldenDir = FIXPP_CODEGEN_GOLDEN_DIR;
// 076-fix-latest-typed-codegen T017/T018: the vlatest golden lives in a
// separate feature golden dir (specs/076-.../contracts/golden/), not
// specs/003's — see cmake target_compile_definitions below.
static constexpr const char* kVlatestGoldenDir = FIXPP_CODEGEN_076_GOLDEN_DIR;
static constexpr const char* kVlatestGoldenFile = "vlatest_Messages.golden.hpp";
// 078-precompiled-builder-libs T007/T008 (R6/FR-010): the per-version builder-
// tier golden SET (groups.hpp/all.hpp/validators/traits.hpp/messages/<Msg>.*)
// lives under this feature's own contracts/golden/<ns>/ dir, replacing the
// 077 single-file monolith goldens AND 069's `official`-mode byte golden
// (Gate-A round-2 decision: the official 33 are a byte-identical subset of
// the default-mode 83, already pinned here — `official` mode instead gets a
// lighter STRUCTURAL witness, OfficialModeBuildersStructuralShape below).
static constexpr const char* kBuilderGoldenDir = FIXPP_CODEGEN_078_GOLDEN_DIR;

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

// Bounded byte-equality check for potentially-large generated artifacts.
// A plain EXPECT_EQ on two multi-MB strings is a footgun: on mismatch gtest
// builds a line-by-line unified diff over the FULL contents, which for a
// ~75 MB / ~750k-line builder header balloons past 11 GB RAM and can OOM-kill
// the host (observed 2026-07-16: a single crash-induced 1-byte-corrupted
// v50sp2 builder golden drove this path to a 24 GB WSL2 kernel panic). This
// reports the first differing byte offset + a short hex window and FAILS
// CLEANLY at any file size — never materializing a whole-file diff.
static void expect_bytes_equal(const std::string& actual, const std::string& expected,
                               const std::string& context) {
    if (actual == expected) return;  // fast path — no allocation, no diff
    const std::size_t n = std::min(actual.size(), expected.size());
    std::size_t off = 0;
    while (off < n && actual[off] == expected[off]) ++off;
    auto window = [](const std::string& s, std::size_t at) {
        std::ostringstream o;
        const std::size_t start = at > 8 ? at - 8 : 0;
        const std::size_t end = std::min(s.size(), at + 9);
        o << std::hex << std::setfill('0');
        for (std::size_t i = start; i < end; ++i)
            o << (i == at ? "[" : " ") << std::setw(2)
              << static_cast<unsigned>(static_cast<unsigned char>(s[i])) << (i == at ? "]" : "");
        return o.str();
    };
    ADD_FAILURE() << context << "\n  byte content differs: actual " << actual.size()
                  << " B, expected " << expected.size() << " B; first difference at byte offset "
                  << off << (off == n ? " (one is a prefix of the other)" : "")
                  << "\n  actual  [" << off << "]:" << window(actual, off)
                  << "\n  expected[" << off << "]:" << window(expected, off);
}

// ── 078-precompiled-builder-libs T008 (R6/FR-010): builder-tier file SET ─────
//
// The split emitter interleaves builder-tier artifacts (groups.hpp, all.hpp,
// validators/traits.hpp, groups/<Plan>.hpp, messages/<Msg>.*) with read-tier
// artifacts (Fields.hpp, Messages.hpp, Validator.hpp, Reify.hpp,
// NormativeReferences.md, Manifest.txt) under the same <ns>/ output dir.
// These helpers isolate the builder-tier SUBSET so the SET+COUNT+content
// invariant below is scoped correctly (the golden SET under
// contracts/golden/<ns>/ holds only this subset, not the read tier — that
// stays goldened by the 003/076 dirs).

// Collect every builder-tier file under a version root, as paths relative to
// that root (POSIX-style so they compare portably across the two roots being
// diffed): "groups.hpp", "all.hpp", "validators/traits.hpp",
// "groups/<relpath>" for everything under the SC-001 per-plan group headers
// dir, and "messages/<relpath>" for everything under messages/.
static std::set<std::string> collect_builder_tier_set(const fs::path& ver_root) {
    std::set<std::string> out;
    for (auto const* leaf : {"groups.hpp", "all.hpp"}) {
        if (fs::exists(ver_root / leaf)) out.insert(leaf);
    }
    if (fs::exists(ver_root / "validators" / "traits.hpp")) out.insert("validators/traits.hpp");
    std::error_code ec;
    for (auto const* sub : {"groups", "messages"}) {
        for (auto const& e : fs::recursive_directory_iterator(ver_root / sub, ec)) {
            if (ec) break;
            if (!e.is_regular_file(ec) || ec) continue;
            auto rel = fs::relative(e.path(), ver_root, ec);
            if (!ec) out.insert(rel.generic_string());
        }
        ec.clear();
    }
    return out;
}

// Assert two builder-tier file sets are IDENTICAL in NAME-SET + COUNT (a
// dropped/renamed message is a distinct failure a content-only diff would
// miss — R6) and that every common file's content is byte-identical.
// `context` labels failures.
static void expect_builder_sets_equal(const fs::path& a_root, const fs::path& b_root,
                                      const std::string& context) {
    auto a_set = collect_builder_tier_set(a_root);
    auto b_set = collect_builder_tier_set(b_root);

    std::vector<std::string> only_in_a, only_in_b;
    std::set_difference(a_set.begin(), a_set.end(), b_set.begin(), b_set.end(),
                        std::back_inserter(only_in_a));
    std::set_difference(b_set.begin(), b_set.end(), a_set.begin(), a_set.end(),
                        std::back_inserter(only_in_b));
    for (auto const& rel : only_in_a)
        ADD_FAILURE() << context << ": file present under " << a_root << " but missing under "
                      << b_root << ": " << rel;
    for (auto const& rel : only_in_b)
        ADD_FAILURE() << context << ": file present under " << b_root << " but missing under "
                      << a_root << ": " << rel;
    EXPECT_EQ(a_set.size(), b_set.size())
        << context << ": builder-tier file COUNT differs (" << a_root << "=" << a_set.size()
        << ", " << b_root << "=" << b_set.size() << ")";

    // Degenerate-walk guard: both sides empty (e.g. a wrong/missing messages/
    // dir on both roots) would satisfy set-equality vacuously — fail loud
    // instead of silently passing on a comparison of nothing.
    EXPECT_GT(a_set.size(), 10U) << context << ": suspiciously few builder-tier files ("
                                 << a_set.size() << ") under " << a_root
                                 << " — codegen output shape may have changed.";

    for (auto const& rel : a_set) {
        if (!b_set.count(rel)) continue;  // already reported above
        expect_bytes_equal(read_file_binary(a_root / rel), read_file_binary(b_root / rel),
                           context + ": " + rel + " not byte-identical");
    }
}

// Parse the declared size N out of a generated all.hpp's
// `inline constexpr ::std::array<builder_registry_entry, N> builder_registry`
// declaration (data-model.md Entity 5). Used only by the official-mode
// structural witness below, which needs the array's cardinality, not its
// msg_type contents (contrast tests/codegen/builder_completeness_common.hpp's
// parse_registry_msgtypes, which extracts the full SET for the census — a
// minimal local parse here avoids pulling in that header's pugixml
// dependency for a single integer). Reports via ADD_FAILURE rather than
// throwing so a malformed all.hpp fails the calling EXPECT_EQ cleanly.
static std::size_t parse_registry_array_size(const fs::path& all_hpp_path) {
    std::string const text = read_file_binary(all_hpp_path);
    static const std::string kNeedle = "builder_registry_entry, ";
    auto pos = text.find(kNeedle);
    if (pos == std::string::npos) {
        ADD_FAILURE() << "no builder_registry array declaration found in " << all_hpp_path;
        return 0;
    }
    pos += kNeedle.size();
    std::size_t end = pos;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
    if (end == pos) {
        ADD_FAILURE() << "malformed builder_registry array size in " << all_hpp_path;
        return 0;
    }
    return static_cast<std::size_t>(std::stoul(text.substr(pos, end - pos)));
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
        // 078-precompiled-builder-libs T007/T008 prerequisites: the golden
        // SET's per-version messages/ subdir existing is the cheapest signal
        // that the set was actually checked in (not just an empty <ns>/ dir).
        ASSERT_TRUE(fs::exists(kBuilderGoldenDir))
            << "078 golden dir not found: " << kBuilderGoldenDir;
        // 082 T036: `v42` joins the builder-bearing versions (issue #196) — its
        // tier is emitted now that detection is structural.
        for (auto const* ver : {"v42", "v44", "v50sp2", "vlatest"}) {
            ASSERT_TRUE(fs::exists(fs::path(kBuilderGoldenDir) / ver / "messages"))
                << "078 golden set missing for " << ver << " under " << kBuilderGoldenDir;
        }
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

    // 078-precompiled-builder-libs T008 (R6/FR-010): the generic walk above
    // passes vacuously if a builder-bearing version's file SET silently
    // shrank/grew between runs (a content-only diff misses a dropped/renamed
    // message — the split multiplies files). Assert explicitly, per version,
    // that the builder-tier SET (groups.hpp/all.hpp/validators/traits.hpp/
    // messages/**) is identical in NAME-SET, COUNT, and CONTENT between the
    // two runs -- v44 and v50sp2 are builder-bearing under the plain
    // legacy-XML run_codegen() job set (v42/vt11 emit none, T017/T018).
    for (auto const* ver : {"v44", "v50sp2"}) {
        expect_builder_sets_equal(run1.path / ver, run2.path / ver,
                                  std::string(ver) + " builder-tier set not identical across runs");
    }
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

        expect_bytes_equal(gen_bytes, golden_bytes,
                           "Golden mismatch for " + std::string(kVersions[i]) +
                               " Messages.hpp\n  generated: " + generated.string() +
                               "\n  golden:    " + golden.string() + "\n  Regenerate: " + kBin +
                               " --xml " + (fs::path(kDictDir) / kXmls[i]).string() +
                               " --out <dir> && cp <dir>/" + kVersions[i] + "/Messages.hpp " +
                               golden.string());
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

    expect_bytes_equal(gen_bytes, golden_bytes,
                       "V-4 violated: vlatest/Messages.hpp diverged from the checked-in golden\n"
                       "  generated: " +
                           generated.string() + "\n  golden:    " + golden.string() +
                           "\n  Regenerate: " + kBin + " --xml " +
                           (fs::path(kDictDir) / "orchestra" / "OrchestraFIXLatest.xml").string() +
                           " --out <dir> && cp <dir>/vlatest/Messages.hpp " + golden.string());
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

    // 078-precompiled-builder-libs T008 (R6/FR-010): as of 078 the builder
    // tier is a per-message file SET (groups.hpp/all.hpp/validators/
    // traits.hpp/messages/**), not a single Builders.hpp -- the generic walk
    // above already covers run-to-run byte-identity file-by-file, but a
    // vacuous pass (the whole SET silently missing) wouldn't fail it. Assert
    // explicitly that the SET is identical in NAME-SET, COUNT, and CONTENT.
    expect_builder_sets_equal(run1.path / "vlatest", run2.path / "vlatest",
                              "vlatest builder-tier set not identical across runs");

    // Sanity bound: vlatest emits Fields/Messages/Validator/Reify/
    // NormativeReferences.md/Manifest.txt/groups.hpp/all.hpp + the
    // messages/**/validators/ builder-tier set (was one monolithic
    // Builders.hpp pre-078 -- the split multiplies files, T003-T005). A
    // loose lower bound so this doesn't need updating on an unrelated
    // emitter-shape change, while still catching a degenerate walk that
    // compares nothing.
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
//       Messages/Validator/Reify/NormativeReferences.md/Manifest.txt/ the
//       builder-tier output (`all.hpp` + split files, post-078) under
//       v42/v44/v50sp2/vt11, plus the shared
//       _dispatch/*.hpp) is byte-identical between the OFF-run and the
//       ON-run. Neither specs/003 nor specs/069's golden dir carries a
//       golden for _dispatch/ or for Fields/Validator/Reify/
//       NormativeReferences.md/Manifest.txt (only Messages.hpp x4 are
//       goldened) — this recursive walk is the ONLY gate covering those
//       files' additivity.
//       (Both OFF and ON runs here use the default `--families all` mode,
//       matching the real Codegen.cmake invocation — official-mode already
//       has its own structural gate, OfficialModeBuildersStructuralShape
//       below, and is deliberately not reused here.)
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
        expect_bytes_equal(read_file_binary(generated), read_file_binary(golden),
                           "V-7 OFF-path baseline mismatch for " + std::string(kVersions[i]));
    }

    // (a2) US3-AC1 — OFF-run has NO vlatest/ dir at all.
    EXPECT_FALSE(fs::exists(off_run.path / "vlatest"))
        << "V-7 / US3-AC1 violated: vlatest/ present in the OFF-path job.";

    // (a3) US3-AC2 (part) — ON-run vlatest/Messages.hpp == 076 golden.
    fs::path on_vlatest = on_run.path / "vlatest" / "Messages.hpp";
    ASSERT_TRUE(fs::exists(on_vlatest)) << "ON-run missing: " << on_vlatest;
    expect_bytes_equal(read_file_binary(on_vlatest),
                       read_file_binary(fs::path(kVlatestGoldenDir) / kVlatestGoldenFile),
                       "V-7 ON-path vlatest/Messages.hpp diverged from the 076 golden.");

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
        expect_bytes_equal(read_file_binary(e.path()), read_file_binary(on_counterpart),
                           "V-7 additivity violated: " + rel.string() +
                               " differs between OFF-run and ON-run.");
        ++compared;
    }
    // Sanity bound: v42/v50sp2/vt11 emit 5 artifacts each, v44 emits 5
    // read-tier artifacts plus its builder-tier output (`all.hpp` + split
    // files, post-078 -- a single +Builders.hpp pre-078), plus 2 shared
    // _dispatch files -- comfortably over 20. A loose lower bound (not the
    // exact count) so this doesn't need updating on every unrelated
    // emitter-shape change, while still catching a degenerate walk that
    // silently compares nothing.
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

// ── 077-builder-args-dedup T019 [US2]: OFF-path stale-file check ───────────
//   (quickstart.md V3, FR-012).
//
// Mirrors AdditiveOffOnByteDiff's OFF-path job (run_codegen() -- the legacy
// XMLs only, exactly what a FIXPP_CODEGEN_FIX_LATEST=OFF configure passes to
// the tool; Codegen.cmake never adds the orchestra --xml job when OFF) but
// asserts the BUILDER-specific claim: no stale vlatest/all.hpp (the
// builder-tier output, post-078) survives an OFF-path run, while the other
// builder-bearing versions (v44/v50sp2, wired by 077 T008-T017) are
// unaffected by the option being OFF.
//
// Deliberately does NOT reconfigure the shared build tree (RUN_SERIAL vs the
// codegen-build-graph-check git-cleanliness gate would be required for no
// additional coverage, same rationale as AdditiveOffOnByteDiff above) --
// this isolated tool-into-TempDir invocation already discriminates the
// OFF-path behavior.
TEST_F(DeterminismTest, BuildersOffPathNoStaleVlatestOthersUnaffected) {
    TempDir off_run("fixpp_det_builders_off");
    int rc = run_codegen(off_run.path);  // legacy XMLs only == OFF-path job set
    ASSERT_EQ(rc, 0) << "OFF-path codegen run failed (exit " << rc << ")";

    // FR-012: no vlatest/ dir at all when the option is OFF -- the
    // conditional OFF-clean (cmake/Codegen.cmake:347-349) mirrored at the
    // tool-invocation level: an OFF configure simply never adds the
    // orchestra --xml job, so no vlatest/all.hpp (or any other vlatest/*
    // builder-tier artifact) is ever written to begin with. `all.hpp` is the
    // 078 builder-tier aggregator (FR-008, replaces Builders.hpp) -- its
    // presence is the split-era discriminator for "this version's builder
    // tier emitted".
    EXPECT_FALSE(fs::exists(off_run.path / "vlatest"))
        << "FR-012 violated: OFF-path job produced a vlatest/ dir -- a stale "
           "vlatest/all.hpp could live here.";
    EXPECT_FALSE(fs::exists(off_run.path / "vlatest" / "all.hpp"));

    // Other builder-bearing versions are unaffected by the option being OFF
    // (FIXPP_CODEGEN_FIX_LATEST gates vlatest only, per T014/G4a).
    EXPECT_TRUE(fs::exists(off_run.path / "v44" / "all.hpp"))
        << "v44/all.hpp missing from OFF-path job -- FR-012's gating must be vlatest-only.";
    EXPECT_TRUE(fs::exists(off_run.path / "v50sp2" / "all.hpp"))
        << "v50sp2/all.hpp missing from OFF-path job -- FR-012's gating must be vlatest-only.";

    // 082-structural-group-detection T035 [US2] — issue #196: INVERTED. This read
    // "v42 is DESCOPED independent of the FIX_LATEST option (T017/issue #196) --
    // confirm the OFF-path job doesn't accidentally emit it either". The descope is
    // retired (detection is structural; T035 deleted main.cpp's ns predicate), so
    // v42 now belongs with the v44/v50sp2 assertions immediately above: its builder
    // tier must be present on the OFF path too, because FIXPP_CODEGEN_FIX_LATEST
    // gates **vlatest only** (FR-012 / T014 / G4a).
    //
    // ⚠️ This was a THIRD v42 builder-descope assertion. FR-016b names only two
    // (`test_077_builder_no_emit.cpp`, `test_077_v42_vt11_completeness_and_c4.cpp`)
    // and this one lives in neither — it was found only because it went RED after
    // T035. Same shape as the #208 carve-out this feature already hit: a list of
    // sites built by naming the obvious files misses the one filed elsewhere.
    EXPECT_TRUE(fs::exists(off_run.path / "v42" / "all.hpp"))
        << "v42/all.hpp missing from OFF-path job -- FR-012's gating must be vlatest-only, and "
           "v42's builder tier is in scope as of 082.";
}

#ifdef FIXPP_CODEGEN_MAIN_VLATEST_ALL_HPP
// This whole suite drives the codegen TOOL directly into isolated TempDirs
// (never reconfigures/touches the shared main build tree's _codegen/include
// output). Confirm that holds: the main tree's vlatest/all.hpp (the
// builder-tier output, post-078 -- present iff THIS build was configured
// with FIXPP_CODEGEN_FIX_LATEST=ON -- the macro itself is only defined by
// CMake in that case) still exists after
// BuildersOffPathNoStaleVlatestOthersUnaffected above ran.
TEST_F(DeterminismTest, MainTreeVlatestBuildersUnaffectedByOffPathWitness) {
    EXPECT_TRUE(fs::exists(FIXPP_CODEGEN_MAIN_VLATEST_ALL_HPP))
        << "Main build tree's vlatest/all.hpp missing after the "
           "OFF-path tool-invocation witness -- that witness must run "
           "against an isolated TempDir only, never the shared tree.";
}
#endif

// ── 078-precompiled-builder-libs T008 (Gate-A round-2 decision): official-mode
// STRUCTURAL witness, replacing the pre-078 byte-identity gate (Gate B PR#187
// round 1 F3, SC-003) ─────────────────────────────────────────────────────
//
// Retirement rationale: `--families official` selects a strict 33-of-83
// subset of the default `all`-mode messages, and the split's per-message
// `.builder.cpp`/`.validator.cpp` bytes for those 33 are already pinned
// byte-for-byte by V44AllModeBuildersMatchesGolden's golden-SET diff below —
// a second byte-golden for the same bytes under a narrower selection would
// add no coverage the R6 contract asks for, so no `v44-official/` golden set
// is checked in.
//
// What IS new post-078 and NOT covered by the default-mode golden: the
// per-MODE emitted file-SET *shape* (33 vs 83 messages) and the
// official-mode `builder_registry` array's cardinality — a future emitter
// regression that silently emitted the wrong subset under `official` (e.g.
// dropped/duplicated a message during the `is_official` filter) would pass
// the default-mode golden diff untouched. This test pins both directly
// against a fresh, ISOLATED `--families official` tool invocation —
// independent of whatever family mode the shared build tree happens to be
// configured with (unlike test_069_mode_count.cpp's compiled-array pin,
// which only sees the tree's *configured* mode).
TEST_F(DeterminismTest, OfficialModeBuildersStructuralShape) {
    TempDir run("fixpp_det_official");
    int rc = run_codegen_v44_official(run.path);
    ASSERT_EQ(rc, 0) << "official-mode codegen run failed (exit " << rc << ")";

    // (i) file-SET shape: 33 messages x 5 files/message + groups.hpp +
    // all.hpp + validators/traits.hpp + the SC-001 per-plan groups/<Plan>.hpp
    // headers reachable from those 33 messages' closure (54, a deterministic
    // function of the official message subset + the group-plan dedup graph)
    // == 222.
    constexpr std::size_t kExpectedOfficialMsgCount = 33;
    constexpr std::size_t kExpectedOfficialGroupPlanCount = 54;
    constexpr std::size_t kExpectedOfficialFileCount =
        kExpectedOfficialMsgCount * 5 + kExpectedOfficialGroupPlanCount + 3;
    auto const built_set = collect_builder_tier_set(run.path / "v44");
    EXPECT_EQ(built_set.size(), kExpectedOfficialFileCount)
        << "SC-003 violated: `--families official` v44 builder-tier file-set shape changed "
           "(expected "
        << kExpectedOfficialMsgCount << " messages x 5 files + " << kExpectedOfficialGroupPlanCount
        << " group-plan headers + 3 shared = "
        << kExpectedOfficialFileCount << ", got " << built_set.size() << ")";

    // (ii) builder_registry cardinality — the `all.hpp` aggregate array's
    // declared size (`::std::array<builder_registry_entry, N>`).
    EXPECT_EQ(parse_registry_array_size(run.path / "v44" / "all.hpp"), kExpectedOfficialMsgCount)
        << "SC-003 violated: `--families official` v44/all.hpp builder_registry array size != "
        << kExpectedOfficialMsgCount;
}

// ── 078-precompiled-builder-libs T007/T008: checked-in-golden-SET-diff gate
// for the three builder-bearing versions (R6/FR-010, replaces 077 T027's
// single-file monolith diff). Boundary vs T008's run-to-run *determinism*
// cells above (ByteIdenticalAcrossRuns / VlatestByteIdenticalAcrossRuns): this
// is the missing *generated-set-vs-checked-in-golden-set* leg (assertion 3 of
// R6), mirroring GeneratedMatchesGolden / VlatestGeneratedMatchesGolden. The
// v44 `official`-mode structural witness is a distinct, narrower mode gated
// separately by OfficialModeBuildersStructuralShape above.

// 082-structural-group-detection T036 [US2] — issue #196: v42's `all`-mode
// builder-tier golden set. Without this cell the 226 files checked in under
// golden/v42/ would be a DEAD golden — present in the repo and compared by
// nothing, which reads as coverage while gating nothing.
//
// Derived, not transcribed (T031 / FR-016b): 226 files, 28 `groups/<Plan>.hpp`
// plan headers over 17 tags, registry 39, reproducible from `emit_builders`' own
// interning rule via `contracts/builder_plan_census.py`. The ordinal map
// {73:3, 78:2, 146:4, 268:2, 295:3, 296:2, 420:2} accounts for exactly the
// 28 - 17 = 11 extra plans, which is B-077-1's structural-key guarantee made
// arithmetic: a second structural variant of a tag becomes a NEW ordinaled plan
// rather than silently sharing the first one (T039).
TEST_F(DeterminismTest, V42AllModeBuildersMatchesGolden) {
    TempDir run("fixpp_det_v42_all_builders");
    int rc = run_codegen(run.path);  // default families mode == All (39 v42 msgs)
    ASSERT_EQ(rc, 0) << "codegen run failed (exit " << rc << ")";

    expect_builder_sets_equal(run.path / "v42", fs::path(kBuilderGoldenDir) / "v42",
                              "R6/FR-010 violated: v42 `all`-mode builder-tier set diverged from "
                              "the checked-in golden set");
}

TEST_F(DeterminismTest, V44AllModeBuildersMatchesGolden) {
    TempDir run("fixpp_det_v44_all_builders");
    int rc = run_codegen(run.path);  // default families mode == All (83 msgs)
    ASSERT_EQ(rc, 0) << "codegen run failed (exit " << rc << ")";

    expect_builder_sets_equal(run.path / "v44", fs::path(kBuilderGoldenDir) / "v44",
                              "R6/FR-010 violated: v44 `all`-mode builder-tier set diverged from "
                              "the checked-in golden set");
}

TEST_F(DeterminismTest, V50sp2BuildersMatchesGolden) {
    TempDir run("fixpp_det_v50sp2_builders");
    int rc = run_codegen(run.path);
    ASSERT_EQ(rc, 0) << "codegen run failed (exit " << rc << ")";

    expect_builder_sets_equal(run.path / "v50sp2", fs::path(kBuilderGoldenDir) / "v50sp2",
                              "R6/FR-010 violated: v50sp2 builder-tier set diverged from the "
                              "checked-in golden set");
}

TEST_F(DeterminismTest, VlatestBuildersMatchesGolden) {
    TempDir run("fixpp_det_vlatest_builders_golden");
    int rc = run_codegen_vlatest_only(run.path);
    ASSERT_EQ(rc, 0) << "vlatest codegen run failed (exit " << rc << ")";

    expect_builder_sets_equal(run.path / "vlatest", fs::path(kBuilderGoldenDir) / "vlatest",
                              "R6/FR-010 violated: vlatest builder-tier set diverged from the "
                              "checked-in golden set");
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
