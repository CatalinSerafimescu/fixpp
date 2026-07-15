// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/validator_bench.cpp
//
// 075-live-wire-enum-validation Gate B r1 (Article VIII §3: "No perf change
// merged without a benchmark in the same PR") — dictionary_driven_validator::
// validate() latency harness (seam #5). US4 is landed (dictionary_driven_
// validator complete); this un-pauses the "validator_bench omitted (US4
// PAUSED)" deferral recorded in bench/REPORT.md and the sibling
// bench/wire/{parser,framer,writer}_bench.cpp files.
//
// [2b §6.6] ceiling: Validator::validate ≤ 200 ns.
//
// A REAL fixpp::dict::table_view is used — loaded from the shipped
// dictionaries/FIX44.xml via dict::XmlLoader -> Dictionary::as_table_view()
// (NOT support/mock_dict_table.hpp, which carries no enum store and would
// not exercise the per-field tag lookup + code-domain binary search + [for
// multi-value fields] space-tokenization that 075 added to validate()'s hot
// path — exactly the work this bench exists to measure).
//
// Each case pre-frames + pre-parses the fixture ONCE outside the timed loop
// (mirrors parser_bench.cpp / table_view_footprint_bench.cpp); only
// dictionary_driven_validator::validate() is timed. A warm-up call BEFORE
// the loop asserts has_value() via state.SkipWithError() — validate() short-
// circuits on the first violation (Step 0 header-order, then per-field
// unexpected-tag / enum / type, then the required-fields scan), so a non-
// conformant fixture would time a partial walk to the first `return`, not
// the full per-field enum path this bench is meant to measure.
//
// Baseline seed: bench/baselines/wire/validator_bench.json.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::Framer;
using fixpp::wire::Parser;
using fixpp::wire::pmr_carry_buffer;

// FIX44 UTCTIMESTAMP collapses to field_type::String (Gate A P3-b pin,
// table_view_test.cpp), so any well-formed-looking timestamp text is
// structurally accepted; content is not enum- or type-constrained.
constexpr std::string_view kSendingTime = "20260516-09:30:00.000";

constexpr std::size_t kParseArena = 16384;
constexpr std::size_t kCarryArena = 512;
constexpr std::size_t kScratchArena = 512;

[[nodiscard]] std::filesystem::path dict_path(std::string_view filename) {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
}

// Loads the shipped FIX44.xml and returns its table_view. Per
// dictionary.hpp:193-205, as_table_view() legally outlives the Dictionary it
// was built from — the enum-domain table OWNS copies of the code bytes — so
// the temporary Dictionary going out of scope here is safe.
[[nodiscard]] fixpp::dict::table_view load_fix44_table_view(std::pmr::memory_resource* mr) {
    auto dict = fixpp::dict::XmlLoader{}.load(dict_path("FIX44.xml"), mr);
    return dict.as_table_view();
}

[[nodiscard]] std::vector<std::byte> build_frame(std::string_view body) {
    std::string pre = std::string("8=FIX.4.4\x01") + "9=" + std::to_string(body.size()) + "\x01";
    pre.append(body);
    unsigned sum = 0;
    for (unsigned char c : pre) { sum += c; }
    sum %= 256U;
    char chk[8]{};
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum);
    pre.append(chk);
    std::vector<std::byte> out(pre.size());
    std::memcpy(out.data(), pre.data(), pre.size());
    return out;
}

// ── (a) Heartbeat(35=0) — admin, no body-required fields beyond header. ────
[[nodiscard]] std::string heartbeat_body() {
    std::string s = "35=0\x01";
    s += "49=SENDER\x01";
    s += "56=TARGET\x01";
    s += "34=1\x01";
    s += "52=";
    s += kSendingTime;
    s += "\x01";
    return s;
}

// ── (b) Logon(35=A) — admin, enum-backed EncryptMethod(98). ────────────────
// EncryptMethod(98) and HeartBtInt(108) are the FIX44 Logon required fields
// beyond the header (dictionaries/FIX44.xml <message name='Logon'>).
[[nodiscard]] std::string logon_body() {
    std::string s = "35=A\x01";
    s += "49=SENDER\x01";
    s += "56=TARGET\x01";
    s += "34=1\x01";
    s += "52=";
    s += kSendingTime;
    s += "\x01";
    s += "98=0\x01";   // EncryptMethod=NONE (in-domain enum)
    s += "108=30\x01"; // HeartBtInt (required, unconstrained INT — no enum)
    return s;
}

// ── (c) NewOrderSingle(35=D) — FIX44 app, enum-backed Side/OrdType/TIF. ────
// Mirrors tests/wire/validator_enum_domain_test.cpp's nos_prefix(): the
// Instrument/OrderQtyData components are required but declare zero required
// LEAF fields in FIX44.xml (measured via the production table_view), so
// ClOrdID(11) + OrdType(40) + TransactTime(60) + Side(54) are the full
// required-field set beyond the header.
[[nodiscard]] std::string new_order_single_body() {
    std::string s = "35=D\x01";
    s += "49=SENDER\x01";
    s += "56=TARGET\x01";
    s += "34=1\x01";
    s += "52=";
    s += kSendingTime;
    s += "\x01";
    s += "11=CLORDID1\x01";
    s += "40=1\x01"; // OrdType=MARKET (in-domain enum)
    s += "60=";
    s += kSendingTime;
    s += "\x01";
    s += "54=1\x01"; // Side=BUY (in-domain enum)
    s += "59=0\x01"; // TimeInForce=DAY (in-domain enum)
    return s;
}

// ── (d) NewOrderSingle(35=D) — multi-value enum ExecInst(18)="1 G 6". ──────
// FR-004: multi-value fields tokenize on a single space (table_view.hpp
// ~338-476); each token is independently binary-searched against the
// field's code domain. "1"=NOT_HELD, "G"=ALL_OR_NONE,
// "6"=PARTICIPATE_DO_NOT_INITIATE are all declared FIX44 ExecInst(18) codes,
// and ExecInst is a valid NewOrderSingle field (dictionaries/FIX44.xml).
[[nodiscard]] std::string new_order_single_multi_value_body() {
    std::string s = new_order_single_body();
    s += "18=1 G 6\x01";
    return s;
}

// Runs one BM_Validator_Validate_* case: builds the real FIX44 table_view,
// frames + parses `body` ONCE (outside the timed loop), then times only
// dictionary_driven_validator::validate() per iteration. `label` names the
// fixture in any SkipWithError diagnostic.
void run_validate_bench(benchmark::State& state, std::string_view body, char const* label) {
    // ── dictionary + table_view (built once, outside the loop) ────────────
    std::vector<std::byte> dict_arena_buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource dict_mr{dict_arena_buf.data(), dict_arena_buf.size()};
    dictionary_driven_validator validator{load_fix44_table_view(&dict_mr)};

    // ── frame + parse (once, outside the loop) ─────────────────────────────
    auto const frame = build_frame(body);

    std::array<std::byte, kCarryArena> carry_buf{};
    std::pmr::monotonic_buffer_resource carry_mr{carry_buf.data(), carry_buf.size(),
                                                 std::pmr::null_memory_resource()};
    pmr_carry_buffer carry{carry_buf.size(), &carry_mr};

    Framer framer;
    std::array<fixpp::wire::frame_view, 1> feed_out{};
    auto fed = framer.feed(std::span<const std::byte>{frame.data(), frame.size()}, carry,
                           std::span<fixpp::wire::frame_view>{feed_out});
    if (!fed || fed->empty()) {
        state.SkipWithError((std::string(label) + " fixture failed to frame").c_str());
        return;
    }

    std::array<std::byte, kParseArena> parse_buf{};
    std::pmr::monotonic_buffer_resource parse_mr{parse_buf.data(), parse_buf.size(),
                                                 std::pmr::null_memory_resource()};
    Parser<access_mode::Index> parser;
    auto mv_r = parser.parse((*fed)[0], &parse_mr);
    if (!mv_r) {
        state.SkipWithError((std::string(label) + " fixture failed to parse").c_str());
        return;
    }
    auto const& mv = *mv_r;

    // Scratch arena buffer allocated ONCE; the pmr::monotonic_buffer_resource
    // over it is re-constructed each iteration (cheap pointer reset) so the
    // arena never accumulates/exhausts across millions of iterations —
    // mirrors parser_bench.cpp's BM_Parser_Index_20tag idiom.
    std::array<std::byte, kScratchArena> scratch_buf{};

    // ── warm-up + correctness check (BEFORE the timed loop) ────────────────
    // validate() short-circuits on the first violation; a non-conformant
    // fixture would time a partial walk, not the full per-field enum path.
    {
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        auto const r = validator.validate(mv, &scratch_mr, nullptr);
        if (!r.has_value()) {
            state.SkipWithError(
                (std::string(label) + " fixture is not conformant — validate() rejected it "
                 "during warm-up (measures a partial walk, not the full path)")
                    .c_str());
            return;
        }
    }

    // ── measured window ─────────────────────────────────────────────────────
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        std::uint16_t ref_tag = 0;
        auto const r = validator.validate(mv, &scratch_mr, &ref_tag);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace

// ── BM_Validator_Validate_Heartbeat ──────────────────────────────────────────
// Admin Heartbeat(35=0): header fields only, no enum-backed body fields.
// [2b §6.6] ceiling ≤ 200 ns.
static void BM_Validator_Validate_Heartbeat(benchmark::State& state) {
    run_validate_bench(state, heartbeat_body(), "Heartbeat");
}
BENCHMARK(BM_Validator_Validate_Heartbeat);

// ── BM_Validator_Validate_Logon ──────────────────────────────────────────────
// Admin Logon(35=A) with enum-backed EncryptMethod(98).
// [2b §6.6] ceiling ≤ 200 ns.
static void BM_Validator_Validate_Logon(benchmark::State& state) {
    run_validate_bench(state, logon_body(), "Logon");
}
BENCHMARK(BM_Validator_Validate_Logon);

// ── BM_Validator_Validate_NewOrderSingle ─────────────────────────────────────
// FIX44 app NewOrderSingle(35=D) with enum-backed Side/OrdType/TimeInForce.
// [2b §6.6] ceiling ≤ 200 ns.
static void BM_Validator_Validate_NewOrderSingle(benchmark::State& state) {
    run_validate_bench(state, new_order_single_body(), "NewOrderSingle");
}
BENCHMARK(BM_Validator_Validate_NewOrderSingle);

// ── BM_Validator_Validate_NewOrderSingleMultiValueExecInst ───────────────────
// FIX44 app NewOrderSingle(35=D) carrying multi-value ExecInst(18)="1 G 6" —
// exercises the FR-004 space-tokenization path (3 tokens, 3 independent
// binary searches). [2b §6.6] ceiling ≤ 200 ns.
static void BM_Validator_Validate_NewOrderSingleMultiValueExecInst(benchmark::State& state) {
    run_validate_bench(state, new_order_single_multi_value_body(), "NewOrderSingleMultiValue");
}
BENCHMARK(BM_Validator_Validate_NewOrderSingleMultiValueExecInst);

BENCHMARK_MAIN();
