// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/validate_group_bench.cpp
//
// 083-group-delimiter-resolution T063 — FR-022 path 1 (inbound validate).
// `[const §VIII.3]`: the benchmark ships in the SAME PR as the change; a
// narrative assertion that the ±5% budget was met is not acceptable in its
// place.
//
// ── What changed on this path, and therefore what is measured ───────────────
// Delimiter resolution is called per offset-table entry from the validator's
// group descent (`include/fixpp/wire/validator.hpp`). Before 083 that call was
// `table_view::group_first_field(no_tag)` — one `unordered_map<uint16_t,...>`
// probe. After 083 it is `group_first_field(msg_type, parent_path, no_tag)`,
// which is:
//   1. the SAME exact `group_bits_` pre-filter first (a clear bit proves BOTH
//      stores miss, so group-free traffic short-circuits before any hash);
//   2. on a set bit, a context probe hashing (string_view, path span, no_tag);
//   3. on a context MISS, the original bare probe as a fallback.
// So the cost model has three distinct regimes, and a bench that only ran
// group-bearing traffic would miss the one that dominates real sessions.
//
// ── The three cases, chosen to separate those regimes ──────────────────────
//   NoGroup      — Heartbeat. Every entry hits regime 1 and short-circuits.
//                  This is the regression risk that matters most: it is the
//                  common path, and it must not have acquired a string hash.
//   SnapshotW    — 35=W MarketDataSnapshotFullRefresh. `NoMDEntries(268)` in
//                  `MDFullGrp` — the FIRST-SEEN context, so the pre-083 global
//                  answer and the post-083 context answer are the SAME value
//                  (269). Isolates the added lookup COST from any change in
//                  behaviour.
//   IncrementalX — 35=X MarketDataIncrementalRefresh. The same `268` in
//                  `MDIncGrp`, delimiter `MDUpdateAction(279)`. This is a
//                  DIVERGENT context: pre-083 it resolved 269 and the frame
//                  false-rejected; post-083 it resolves 279 and validates.
//                  So this row has no meaningful pre-083 counterpart to
//                  compare against — it is the case that did not work before.
//                  Recorded rather than silently baselined as a regression.
//
// A REAL shipped dictionary is used (dictionaries/FIX44.xml via
// dict::XmlLoader -> Dictionary::as_table_view()), not a hand-built
// table_view: a hand-built one never populates `group_ctx_`, so every lookup
// would fall straight through to the bare store and the bench would measure
// exactly the path 083 replaced.
//
// Baseline: bench/baselines/wire/validate_group_bench.json.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory_resource>
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

constexpr std::string_view kSendingTime = "20260516-09:30:00.000";

constexpr std::size_t kParseArena = 16384;
constexpr std::size_t kCarryArena = 512;
constexpr std::size_t kScratchArena = 1024;

[[nodiscard]] std::filesystem::path dict_path(std::string_view filename) {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
}

// as_table_view() legally outlives the Dictionary it was built from
// (dictionary.hpp) — same idiom as validator_bench.cpp.
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

[[nodiscard]] std::string header(char msg_type) {
    std::string s = "35=?\x01";
    s[3] = msg_type;
    s += "49=SENDER\x01";
    s += "56=TARGET\x01";
    s += "34=1\x01";
    s += "52=";
    s += kSendingTime;
    s += "\x01";
    return s;
}

// (a) Heartbeat — no group anywhere; every entry takes the group_bits_
//     short-circuit. The common-path regression guard.
[[nodiscard]] std::string heartbeat_body() { return header('0'); }

// (b) 35=W — NoMDEntries(268) under MDFullGrp, delimiter MDEntryType(269).
//     FIX44 requires the Instrument component (Symbol(55)) on this message.
[[nodiscard]] std::string snapshot_body() {
    std::string s = header('W');
    s += "55=IBM\x01";     // Instrument/Symbol (required component)
    s += "268=2\x01";      // NoMDEntries
    s += "269=0\x01";      // instance 1 — MDEntryType=Bid (the delimiter here)
    s += "269=1\x01";      // instance 2 — MDEntryType=Offer
    return s;
}

// (c) 35=X — the SAME NoMDEntries(268) under MDIncGrp, delimiter
//     MDUpdateAction(279). Divergent context: pre-083 this frame REJECTED.
[[nodiscard]] std::string incremental_body() {
    std::string s = header('X');
    s += "268=2\x01";      // NoMDEntries
    s += "279=0\x01";      // instance 1 — MDUpdateAction=New (the delimiter here)
    s += "269=0\x01";      // MDEntryType (optional member)
    s += "279=1\x01";      // instance 2 — MDUpdateAction=Change
    s += "269=1\x01";
    return s;
}

void run_validate_bench(benchmark::State& state, std::string const& body, char const* label) {
    std::vector<std::byte> dict_buf(8UZ * 1024UZ * 1024UZ);
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.data(), dict_buf.size()};
    auto const tv = load_fix44_table_view(&dict_mr);
    dictionary_driven_validator const validator{tv};

    auto const frame_bytes = build_frame(body);

    std::array<std::byte, kCarryArena> carry_buf{};
    std::pmr::monotonic_buffer_resource carry_mr{carry_buf.data(), carry_buf.size(),
                                                 std::pmr::null_memory_resource()};
    pmr_carry_buffer carry{carry_buf.size(), &carry_mr};
    Framer framer;
    std::array<fixpp::wire::frame_view, 1> feed_out{};
    auto fed = framer.feed(std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()},
                           carry, std::span<fixpp::wire::frame_view>{feed_out});
    if (!fed || fed->empty()) {
        state.SkipWithError((std::string(label) + " fixture failed to frame").c_str());
        return;
    }

    std::array<std::byte, kParseArena> parse_buf{};
    std::pmr::monotonic_buffer_resource parse_mr{parse_buf.data(), parse_buf.size(),
                                                 std::pmr::null_memory_resource()};
    // Dict-BACKED parse — the shipped inbound configuration since 066. A
    // dict-free parse would build an offset table with no membership oracle,
    // which is not the path FR-022 budgets.
    Parser<access_mode::Index> parser{tv};
    auto mv_r = parser.parse((*fed)[0], &parse_mr);
    if (!mv_r) {
        state.SkipWithError((std::string(label) + " fixture failed to parse").c_str());
        return;
    }
    auto const& mv = *mv_r;

    std::array<std::byte, kScratchArena> scratch_buf{};

    // Warm-up + conformance check BEFORE the timed loop: validate() short-
    // circuits on the first violation, so a non-conformant fixture would time
    // a partial walk that never reaches the group descent this bench exists
    // to measure.
    {
        std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                       std::pmr::null_memory_resource()};
        auto const r = validator.validate(mv, &scratch_mr, nullptr);
        if (!r.has_value()) {
            state.SkipWithError(
                (std::string(label) +
                 " fixture is not conformant — validate() rejected it during warm-up, so the "
                 "timed loop would measure a partial walk, not the group descent")
                    .c_str());
            return;
        }
    }

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

// ── BM_ValidateGroup_NoGroup ────────────────────────────────────────────────
// The common path. Every entry short-circuits on the group_bits_ pre-filter,
// so this row must be flat across 083 — if it moved, the pre-filter is not
// doing its job and every group-free session pays for the context store.
static void BM_ValidateGroup_NoGroup(benchmark::State& state) {
    run_validate_bench(state, heartbeat_body(), "NoGroup");
}
BENCHMARK(BM_ValidateGroup_NoGroup);

// ── BM_ValidateGroup_FirstSeenContext ───────────────────────────────────────
// 35=W: the context store answers the SAME value the bare store did, so this
// row isolates the added lookup cost from any behaviour change.
static void BM_ValidateGroup_FirstSeenContext(benchmark::State& state) {
    run_validate_bench(state, snapshot_body(), "SnapshotW");
}
BENCHMARK(BM_ValidateGroup_FirstSeenContext);

// ── BM_ValidateGroup_DivergentContext ───────────────────────────────────────
// 35=X: NoMDEntries(268) resolves MDUpdateAction(279) here, not the global
// 269. NO pre-083 counterpart — this frame was REJECTED before 083, so the
// row is recorded as new rather than compared against a baseline it cannot
// have had.
static void BM_ValidateGroup_DivergentContext(benchmark::State& state) {
    run_validate_bench(state, incremental_body(), "IncrementalX");
}
BENCHMARK(BM_ValidateGroup_DivergentContext);

BENCHMARK_MAIN();
