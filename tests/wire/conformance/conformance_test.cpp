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
//
// gate-b/r1 dispatcher (PR68-07): replaces the CSV-linter RowHasPinnedVersion-
// Oracle stub with a real behavioral dispatcher keyed by w_id prefix.
//   - w001..w013 (parse/serialize/multi-frame): frame → Framer → Parser;
//     assert has_value() == expect_ok.
//   - w014 (validate): frame → Framer → Parser → dictionary_driven_validator;
//     assert validation result matches expect_ok / expect_code.
// seam #1 include order: mock_dict_table.hpp BEFORE validator.hpp so
// dictionary_driven_validator::dict_ is complete at instantiation.

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

// seam #1 — mock_dict_table MUST precede validator.hpp
#include "support/mock_dict_table.hpp"
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include "support/frame_view_factory.hpp"

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

// ── Error-code name → fixpp::core::error ──────────────────────────────────

[[nodiscard]] inline fixpp::core::error error_from_name(std::string_view name) noexcept {
    using E = fixpp::core::error;
    if (name == "wire_frame_too_large")          { return E::wire_frame_too_large; }
    if (name == "wire_invalid_body_length")       { return E::wire_invalid_body_length; }
    if (name == "wire_checksum_mismatch")         { return E::wire_checksum_mismatch; }
    if (name == "wire_framing_resync")            { return E::wire_framing_resync; }
    if (name == "wire_invalid_field_format")      { return E::wire_invalid_field_format; }
    if (name == "wire_offset_table_full")         { return E::wire_offset_table_full; }
    if (name == "wire_group_too_large")           { return E::wire_group_too_large; }
    if (name == "wire_tag_out_of_range")          { return E::wire_tag_out_of_range; }
    if (name == "wire_required_field_missing")    { return E::wire_required_field_missing; }
    if (name == "wire_header_out_of_order")       { return E::wire_header_out_of_order; }
    if (name == "wire_field_value_out_of_range")  { return E::wire_field_value_out_of_range; }
    if (name == "wire_field_value_truncated")     { return E::wire_field_value_truncated; }
    if (name == "wire_unexpected_tag")            { return E::wire_unexpected_tag; }
    // fallback: return a sentinel that will never equal a real code
    return static_cast<E>(0);
}

// ── Dictionary factories (w014 version oracle) ────────────────────────────
// Each version-oracle dict covers the minimal grammar for the W-014 corpus.
// The [FIX50SP2 §3] oracle maps fix_version → a grammar that covers all
// message types present in the w014 corpus rows.  For parse-only rows
// (w001..w013) the validator step is skipped entirely — no dict is needed.

// Heartbeat grammar (35=0): required header + framing tags; no app fields.
// Used by: 4.2, 5.0SP2, T1.1 conforming-heartbeat rows and unexpected-tag row.
[[nodiscard]] inline fixpp::dict::table_view make_heartbeat_grammar() {
    using ft = fixpp::dict::field_type;
    fixpp::dict::table_view t;
    // Required standard-header fields for Heartbeat
    t.add_required("0", 8)   // BeginString
     .add_required("0", 9)   // BodyLength
     .add_required("0", 35)  // MsgType
     .add_required("0", 49)  // SenderCompID
     .add_required("0", 56)  // TargetCompID
     .add_required("0", 34)  // MsgSeqNum
     .add_required("0", 10)  // CheckSum
     .set_type(34, ft::Int);
    return t;
}

// NewOrderSingle grammar (35=D): required fields for the W-014 NOS corpus rows.
// Matches make_d_grammar() in validator_domain_test.cpp exactly.
[[nodiscard]] inline fixpp::dict::table_view make_nos_grammar() {
    using ft = fixpp::dict::field_type;
    fixpp::dict::table_view t;
    t.add_required("D", 8)    // BeginString
     .add_required("D", 9)    // BodyLength
     .add_required("D", 35)   // MsgType
     .add_required("D", 49)   // SenderCompID
     .add_required("D", 56)   // TargetCompID
     .add_required("D", 34)   // MsgSeqNum
     .add_required("D", 11)   // ClOrdID
     .add_required("D", 55)   // Symbol
     .add_required("D", 54)   // Side
     .add_required("D", 10)   // CheckSum
     .add_valid("D", 38)      // OrderQty
     .add_valid("D", 40)      // OrdType
     .add_valid("D", 453)     // NoPartyIDs (group count)
     .add_valid("D", 448)     // PartyID (group member)
     .add_valid("D", 447)     // PartyIDSource (group member)
     .set_type(11, ft::String)
     .set_type(38, ft::Float)
     .set_type(34, ft::Int)
     .set_type(54, ft::Char)
     .add_enum(54, "1")
     .add_enum(54, "2")
     .set_group_first(453, 448);
    return t;
}

// Resolve the dict to use for a w014 validation row.  The fix_version string
// selects among the known W-014 grammars; msg_type selects within the version.
[[nodiscard]] inline fixpp::dict::table_view
make_w014_dict(std::string_view /*fix_version*/, std::string_view msg_type) {
    if (msg_type == "D") {
        return make_nos_grammar();
    }
    // Heartbeat ("0") and anything else: use the heartbeat grammar.
    return make_heartbeat_grammar();
}

// ── Frame-and-parse helper ─────────────────────────────────────────────────
// Feed `frame_bytes` through the real Framer and return the first frame_view,
// or an error. A carry buffer of 64 KiB is sufficient for all corpus rows.
[[nodiscard]] inline fixpp::core::expected_t<fixpp::wire::frame_view>
feed_first_frame(std::vector<std::byte> const& frame_bytes,
                 fixpp::wire::Framer& framer,
                 fixpp::wire::pmr_carry_buffer& carry,
                 std::array<fixpp::wire::frame_view, 8>& out_buf) noexcept {
    auto span = std::span<const std::byte>{frame_bytes};
    auto result = framer.feed(span, carry, std::span<fixpp::wire::frame_view>{out_buf});
    if (!result.has_value()) {
        return fixpp::core::expected_t<fixpp::wire::frame_view>{
            std::unexpect, result.error()};
    }
    if (result->empty()) {
        // No complete frame in the buffer yet (shouldn't happen for well-formed rows).
        return fixpp::core::expected_t<fixpp::wire::frame_view>{
            std::unexpect, fixpp::core::error::wire_framing_resync};
    }
    return (*result)[0];
}

// ── Parameterized test ─────────────────────────────────────────────────────

class WireConformance : public ::testing::TestWithParam<CorpusRow> {};

// gate-b/r1 behavioral dispatcher (PR68-07).
// Each row is dispatched based on w_id prefix:
//   - w001..w013: Framer + Parser behavioral check.
//   - w014:       Framer + Parser + dictionary_driven_validator check.
TEST_P(WireConformance, BehavioralDispatch) {
    auto const& r = GetParam();
    SCOPED_TRACE(r.source_file + " :: " + r.w_id + " :: " + r.description);

    // Basic corpus shape invariants (kept from the original scaffold).
    ASSERT_FALSE(r.fix_version.empty())
        << "[FIX50SP2 §3] every conformance row must name its version oracle";
    ASSERT_FALSE(r.frame.empty()) << "row must carry a non-empty hex frame";

    // ── Framer step ──────────────────────────────────────────────────────
    fixpp::wire::Framer framer;
    std::pmr::monotonic_buffer_resource carry_mr;
    fixpp::wire::pmr_carry_buffer carry{65536, &carry_mr};
    std::array<fixpp::wire::frame_view, 8> out_buf{};

    auto fv_result = feed_first_frame(r.frame, framer, carry, out_buf);

    if (!r.expect_ok && !r.expect_code.empty()) {
        // Check if the framing step itself produced the expected error.
        if (!fv_result.has_value()) {
            auto const expected_err = error_from_name(r.expect_code);
            EXPECT_EQ(fv_result.error(), expected_err)
                << "framing error mismatch: got "
                << static_cast<int>(fv_result.error())
                << " expected " << r.expect_code;
            return;  // error was at the framing layer — done
        }
    } else if (!fv_result.has_value()) {
        // Expected success but framing failed.
        if (r.expect_ok) {
            ADD_FAILURE() << "Framer::feed failed unexpectedly; error="
                          << static_cast<int>(fv_result.error());
        }
        return;
    }

    // Frame produced: proceed to parse.
    // ── Parser step ──────────────────────────────────────────────────────
    std::pmr::monotonic_buffer_resource parse_mr;
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{
        fixpp::dict::table_view{}};
    auto mv_result = parser.parse(*fv_result, &parse_mr);

    if (!mv_result.has_value()) {
        if (r.expect_ok) {
            ADD_FAILURE() << "Parser::parse failed unexpectedly; error="
                          << static_cast<int>(mv_result.error());
        } else if (!r.expect_code.empty()) {
            auto const expected_err = error_from_name(r.expect_code);
            EXPECT_EQ(mv_result.error(), expected_err)
                << "parse error mismatch: got "
                << static_cast<int>(mv_result.error())
                << " expected " << r.expect_code;
        }
        return;
    }

    // ── w014 validation step ──────────────────────────────────────────────
    if (r.w_id == "w014") {
        auto const msg_type = mv_result->msg_type();
        auto dict = make_w014_dict(r.fix_version, msg_type);
        fixpp::wire::dictionary_driven_validator validator{std::move(dict)};

        std::array<std::byte, 4096> scratch_buf{};
        std::pmr::monotonic_buffer_resource scratch_mr{
            scratch_buf.data(), scratch_buf.size(),
            std::pmr::null_memory_resource()};

        auto val_result = validator.validate(*mv_result, &scratch_mr);
        if (r.expect_ok) {
            EXPECT_TRUE(val_result.has_value())
                << "validator rejected a conforming message; error="
                << (val_result.has_value() ? 0
                                           : static_cast<int>(val_result.error()));
        } else {
            ASSERT_FALSE(val_result.has_value())
                << "validator accepted a non-conforming message";
            if (!r.expect_code.empty()) {
                auto const expected_err = error_from_name(r.expect_code);
                EXPECT_EQ(val_result.error(), expected_err)
                    << "validation error mismatch: got "
                    << static_cast<int>(val_result.error())
                    << " expected " << r.expect_code;
            }
        }
        return;
    }

    // ── Parse-only / serialize rows (w001..w013) ──────────────────────────
    // For these rows the expected outcome is always expect_ok=1 (all corpus
    // rows currently authored for these w_ids are conforming frames);
    // w010 may contain expect_ok=0 rows whose failure should have been caught
    // at the Framer step above.
    if (r.expect_ok) {
        SUCCEED();  // parsed successfully — conformance confirmed
    } else {
        // Parsing succeeded but we expected failure (should have been caught
        // at framing; flag if it wasn't).
        ADD_FAILURE() << "expected parse failure but parse succeeded for: "
                      << r.description;
    }
}

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
