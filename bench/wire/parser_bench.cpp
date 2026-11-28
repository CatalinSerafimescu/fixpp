// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/parser_bench.cpp
//
// T048 — [2b §6.6] Parser<Iter/Index>::parse latency harness (seam #5).
//
// [2b §6.6] ceilings:
//   Parser<Iter>::parse_iter   20-tag message            ≤  80 ns
//   Parser<Index>::parse       20-tag message            ≤ 400 ns
//   Parser<Index>::parse       200-tag Instrument-heavy  ≤   4 µs
//
// Baseline seed: bench/baselines/wire/parser_bench.json (written on the first
// green CI run that includes this bench).
//
// hffix comparison: hffix is NOT wired into the build.
// D-14 says "measured-not-blocker"; see bench/REPORT.md.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

// Concrete dict::table_view definition (seam #1 — test double).
// The real 2c table_view is forward-declared only in parser.hpp; the bench
// is not production code, so the test double is appropriate here.
#include "support/mock_dict_table.hpp"
// Test-only frame_view_factory (friend-of-frame_view).
#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::frame_view;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

[[nodiscard]] std::vector<std::byte> build_frame(std::string_view body) {
    std::string pre = std::string("8=FIX.4.4\x01") + "9=" + std::to_string(body.size()) + "\x01";
    pre.append(body);
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    sum %= 256U;
    char chk[8]{};
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum);
    pre.append(chk);
    std::vector<std::byte> out(pre.size());
    std::memcpy(out.data(), pre.data(), pre.size());
    return out;
}

// ~20-tag NewOrderSingle-ish body.
const std::string k20TagBody =
    std::string("35=D\x01") + "34=1\x01" + "49=SENDER01\x01" + "56=TARGET01\x01" +
    "52=20260516-09:30:00.000\x01" + "11=ORD12345678\x01" + "55=AAPL\x01" + "54=1\x01" +
    "38=100\x01" + "40=2\x01" + "44=150.25\x01" + "59=0\x01" + "60=20260516-09:30:00.000\x01" +
    "1=ACC001\x01" + "21=1\x01" + "110=0\x01" + "111=0\x01" + "15=USD\x01" + "58=BenchOrder\x01" +
    "207=XNAS\x01";

// ~200-tag Instrument-heavy body: repeat many fields to exercise the hash
// overlay more heavily. Real 200-tag messages have many repeating-group
// entries; this approximates the allocation + hash insert cost.
static std::string build_200tag_body() {
    std::string body = std::string("35=W\x01") + "34=1\x01" + "49=SENDER01\x01" +
                       "56=TARGET01\x01" + "52=20260516-09:30:00.000\x01" + "55=AAPL\x01";
    // NoMDEntries (268) = 48; then 48 × 4 fields ≈ 198 total fields.
    body += "268=48\x01";
    for (int i = 0; i < 48; ++i) {
        body += "269=0\x01";         // MDEntryType
        body += "270=150.25\x01";    // MDEntryPx
        body += "271=100\x01";       // MDEntrySize
        body += "272=20260516\x01";  // MDEntryDate
    }
    return body;
}

const std::vector<std::byte> k20TagFrame = build_frame(k20TagBody);
const std::string k200TagBody = build_200tag_body();
const std::vector<std::byte> k200TagFrame = build_frame(k200TagBody);

// Mint frame_views via the test factory (friend of frame_view).
[[nodiscard]] frame_view make_fv(std::span<const std::byte> buf) {
    auto r = fixpp::wire::test::make_frame_view(buf);
    return r.value_or(frame_view{});
}

const frame_view k20TagFv = make_fv(k20TagFrame);
const frame_view k200TagFv = make_fv(k200TagFrame);

}  // namespace

// ── BM_Parser_Iter_20tag ──────────────────────────────────────────────────────
// Parser<Iter>::parse_iter — 20-tag message.
// [2b §6.6] ceiling ≤ 80 ns.
static void BM_Parser_Iter_20tag(benchmark::State& state) {
    fixpp::dict::table_view tv{};
    Parser<access_mode::Iter> p{tv};
    for (auto _ : state) {
        auto r = p.parse_iter(k20TagFv);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Parser_Iter_20tag);

// ── BM_Parser_Index_20tag ─────────────────────────────────────────────────────
// Parser<Index>::parse — 20-tag message.
// [2b §6.6] ceiling ≤ 400 ns.
static void BM_Parser_Index_20tag(benchmark::State& state) {
    fixpp::dict::table_view tv{};
    Parser<access_mode::Index> p{tv};
    std::array<std::byte, 32 * 1024> arena_buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                                  std::pmr::null_memory_resource()};
        auto r = p.parse(k20TagFv, &arena);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Parser_Index_20tag);

// ── BM_Parser_Index_200tag ────────────────────────────────────────────────────
// Parser<Index>::parse — 200-tag Instrument-heavy message.
// [2b §6.6] ceiling ≤ 4 µs.
static void BM_Parser_Index_200tag(benchmark::State& state) {
    fixpp::dict::table_view tv{};
    Parser<access_mode::Index> p{tv};
    std::array<std::byte, 256 * 1024> arena_buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                                  std::pmr::null_memory_resource()};
        auto r = p.parse(k200TagFv, &arena);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Parser_Index_200tag);

// ── W1: unknown_fields() (`.specify/688-707-559-wire-headroom-token.md` §1.4) ──────────────
// The shapes where the filtered view of D-688-1 can cost more than the list it replaces, plus
// the single pass beside them. They run over a table loaded from dictionaries/FIX44.xml, so a
// tag is known in both the merge base's classification (declared for NewOrderSingle) and the
// W1 one (defined by the dictionary). They use only unknown_fields(), begin(), end(), empty()
// and a range-for binding auto const&, so this file builds on the merge base too and the same
// source can run there (the transplanted A-B-A-B run). Each case first checks that the frame
// yields the unknown count it is named for, and stops with an error otherwise, so the two
// trees time the same shape.
namespace {

[[nodiscard]] fixpp::dict::table_view const& fix44_table() {
    static std::pmr::monotonic_buffer_resource dict_mr;
    static fixpp::dict::Dictionary const dict = fixpp::dict::XmlLoader{}.load(
        std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml", &dict_mr);
    static fixpp::dict::table_view const tv = dict.as_table_view();
    return tv;
}

constexpr std::size_t kTrailingUnknowns = 3;
const std::vector<std::byte> kKnownThenUnknownFrame =
    build_frame(k20TagBody + "5000=U0\x01" + "5001=U1\x01" + "5002=U2\x01");
const frame_view kKnownThenUnknownFv = make_fv(kKnownThenUnknownFrame);

[[nodiscard]] std::size_t count_unknown(MessageView<access_mode::Index> const& mv) {
    std::size_t n = 0;
    for (auto const& kv : mv.unknown_fields()) {
        std::uint16_t tag = kv.tag;
        benchmark::DoNotOptimize(tag);
        ++n;
    }
    return n;
}

}  // namespace

// One pass per fresh parse: the parse, then one full iteration of unknown_fields().
static void BM_UnknownFields_SinglePassAfterParse(benchmark::State& state) {
    Parser<access_mode::Index> p{fix44_table()};
    std::array<std::byte, 32 * 1024> arena_buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                                  std::pmr::null_memory_resource()};
        auto r = p.parse(kKnownThenUnknownFv, &arena);
        if (!r || count_unknown(*r) != kTrailingUnknowns) {
            state.SkipWithError("the frame does not yield its trailing unknown fields");
            break;
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_UnknownFields_SinglePassAfterParse);

// Repeated unknown_fields() calls and full iterations on one parsed message.
static void BM_UnknownFields_RepeatedCalls(benchmark::State& state) {
    Parser<access_mode::Index> p{fix44_table()};
    std::array<std::byte, 32 * 1024> arena_buf{};
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    auto r = p.parse(kKnownThenUnknownFv, &arena);
    if (!r || count_unknown(*r) != kTrailingUnknowns) {
        state.SkipWithError("the frame does not yield its trailing unknown fields");
        return;
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(count_unknown(*r));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_UnknownFields_RepeatedCalls);

// Repeated full iterations of one view, built once.
static void BM_UnknownFields_RepeatedIterationOfOneView(benchmark::State& state) {
    Parser<access_mode::Index> p{fix44_table()};
    std::array<std::byte, 32 * 1024> arena_buf{};
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    auto r = p.parse(kKnownThenUnknownFv, &arena);
    if (!r || count_unknown(*r) != kTrailingUnknowns) {
        state.SkipWithError("the frame does not yield its trailing unknown fields");
        return;
    }
    auto const uf = r->unknown_fields();
    for (auto _ : state) {
        std::size_t n = 0;
        for (auto const& kv : uf) {
            std::uint16_t tag = kv.tag;
            benchmark::DoNotOptimize(tag);
            ++n;
        }
        benchmark::DoNotOptimize(n);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_UnknownFields_RepeatedIterationOfOneView);

// empty() on a frame with no unknown field, called repeatedly on one parsed message.
static void BM_UnknownFields_EmptyOnFrameWithNone(benchmark::State& state) {
    Parser<access_mode::Index> p{fix44_table()};
    std::array<std::byte, 32 * 1024> arena_buf{};
    std::pmr::monotonic_buffer_resource arena{arena_buf.data(), arena_buf.size(),
                                              std::pmr::null_memory_resource()};
    auto r = p.parse(k20TagFv, &arena);
    if (!r || count_unknown(*r) != 0U) {
        state.SkipWithError("the 20-tag frame is expected to have no unknown field");
        return;
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(r->unknown_fields().empty());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_UnknownFields_EmptyOnFrameWithNone);

BENCHMARK_MAIN();
