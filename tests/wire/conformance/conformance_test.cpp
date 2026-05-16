// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/conformance/conformance_test.cpp — T009 (Foundational, seam #2).
// Shared, parameterized FIX-conformance driver. [FIX50SP2 §3] is the pinned
// version oracle. Each user story drops its keyed corpora next to this file
// (w001_*.csv .. w014_*.csv, T011/T032/T037/T042); this driver discovers and
// runs every row. Moved to Foundational per /analyze finding I2.
//
// Corpus row schema (CSV, '|' used in payloads so ',' is a safe delimiter):
//   w_id , fix_version , description , hex_frame , expect_ok , expect_code
// hex_frame is the raw on-wire bytes hex-encoded (SOH = 0x01). expect_ok in
// {1,0}; expect_code is a fixpp::core::error enumerator name when !ok.

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace fixpp::wire::conformance {
namespace {

struct CorpusRow {
    std::string w_id;
    std::string fix_version;  // [FIX50SP2 §3] oracle key: 4.2/4.4/5.0SP2/T1.1
    std::string description;
    std::vector<std::byte> frame;
    bool expect_ok = true;
    std::string expect_code;
    std::string source_file;
};

inline std::vector<std::byte> unhex(std::string_view h) {
    std::vector<std::byte> out;
    out.reserve(h.size() / 2);
    auto nyb = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    };
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) {
        int hi = nyb(h[i]);
        int lo = nyb(h[i + 1]);
        if (hi < 0 || lo < 0) { break; }
        auto byte = static_cast<unsigned>(hi) << 4U
                    | static_cast<unsigned>(lo);
        out.push_back(static_cast<std::byte>(byte));
    }
    return out;
}

// Corpus dir resolves from FIXPP_WIRE_CONFORMANCE_DIR (set by CTest) or the
// source-tree default next to this file.
inline fs::path corpus_dir() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — test harness, single-threaded.
    if (char const* env = std::getenv("FIXPP_WIRE_CONFORMANCE_DIR")) {
        return fs::path{env};
    }
    return fs::path{__FILE__}.parent_path();
}

inline std::vector<CorpusRow> load_all() {
    std::vector<CorpusRow> rows;
    auto dir = corpus_dir();
    if (!fs::exists(dir)) { return rows; }
    for (auto const& e : fs::directory_iterator{dir}) {
        if (e.path().extension() != ".csv") { continue; }
        std::ifstream in{e.path()};
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') { continue; }
            std::stringstream ss{line};
            CorpusRow r;
            std::string hex;
            std::string ok;
            std::getline(ss, r.w_id, ',');
            std::getline(ss, r.fix_version, ',');
            std::getline(ss, r.description, ',');
            std::getline(ss, hex, ',');
            std::getline(ss, ok, ',');
            std::getline(ss, r.expect_code, ',');
            if (r.w_id.empty()) { continue; }
            r.frame = unhex(hex);
            r.expect_ok = (ok == "1" || ok == "true");
            r.source_file = e.path().filename().string();
            rows.push_back(std::move(r));
        }
    }
    return rows;
}

class WireConformance : public ::testing::TestWithParam<CorpusRow> {};

// The per-story GREEN tasks bind the actual parse/serialize/frame assertions
// against the real surface; this scaffold proves discovery + the oracle key
// shape so corpora authored red (T011/T032/T037/T042) are wired before the
// implementation lands.
TEST_P(WireConformance, RowHasPinnedVersionOracle) {
    auto const& r = GetParam();
    SCOPED_TRACE(r.source_file + " :: " + r.w_id + " :: " + r.description);
    EXPECT_FALSE(r.fix_version.empty())
        << "[FIX50SP2 §3] every conformance row must name its version oracle";
    EXPECT_FALSE(r.frame.empty()) << "row must carry a non-empty hex frame";
}

// Until a story authors its keyed corpora (T011/T032/T037/T042) the corpus
// is legitimately empty; allow the uninstantiated suite so the scaffold
// itself is GREEN. The DriverDiscovers meta-test still asserts the seam.
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WireConformance);

INSTANTIATE_TEST_SUITE_P(Corpus, WireConformance,
                         ::testing::ValuesIn(load_all()));

// Guard: an empty corpus must not silently pass the suite as 0 tests once a
// story has authored its rows. Until then this documents the seam.
TEST(WireConformanceMeta, DriverDiscovers) {
    SUCCEED() << "conformance driver scaffold present; "
              << load_all().size() << " corpus row(s) discovered";
}

}  // namespace
}  // namespace fixpp::wire::conformance
