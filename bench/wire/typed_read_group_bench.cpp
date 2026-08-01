// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/typed_read_group_bench.cpp
//
// 083-group-delimiter-resolution T064 — FR-022 path 2 (typed read).
// `[const §VIII.3]`: benchmark in the same PR as the change.
//
// ── What 083 added to this path ─────────────────────────────────────────────
//   (1) C-8.1/C-8.2 — `group_slices_status` resolves its boundary delimiter
//       through a new `group_delim_fn_` callback into the per-context store,
//       instead of reading `entries_[first].tag`. One extra indirect call and
//       one context-keyed probe per materialization.
//   (2) C-8.0c — `consume_group_extent` performs an ADDITIONAL
//       `group_member_fn_` evaluation at every instance-opening delimiter, to
//       decide whether that delimiter is itself a nested group's count tag.
//       This one is PER GROUP INSTANCE, so it is the term that scales.
//
// (C-8.5's nested-extent skip is NOT in the delivered code — descoped on
// T054's measured negative result — so there is nothing to measure for it.
// Recorded here rather than leaving a reader to wonder why the third term the
// task text names is absent.)
//
// ── Why a mode-(c) shape is MANDATORY here ──────────────────────────────────
// (2)'s probe returns false on an ordinary delimiter and the walk takes the
// cheap `++k`. A bench built only from ordinary groups therefore measures the
// branch that never descends and would report a budget met over the cheap arm
// while the expensive arm — the one 083 exists to make correct, 485 shipped
// contexts — went unmeasured. `ModeC_*` below is that shape: outer group 100's
// delimiter IS nested group 200's own count tag, so the descent fires once per
// outer instance.
//
// ── Cases ───────────────────────────────────────────────────────────────────
//   Flat_2Inst    — ordinary group, 2 instances. The descent probe runs twice
//                   and answers false both times: pure added-probe cost.
//   ModeC_2Inst   — mode (c), 2 instances. The descent probe answers TRUE and
//                   recurses. Pre-083 this returned ONE slice (the extent
//                   truncated), so its slice count is not comparable to a
//                   pre-083 run — it is more work because it is now correct.
//   ModeC_8Inst   — the same shape at 8 instances, so the per-instance term is
//                   separable from the fixed per-materialization term by
//                   comparing against ModeC_2Inst rather than by assertion.
//
// Only `group_slices_status()` is timed; framing and parsing happen once,
// outside the loop. The slice cache is keyed by no_tag and materializes once
// per table, so each iteration re-parses into a fresh table — that per-
// iteration parse is CONSTANT across all three rows and cancels when they are
// compared to each other, which is what the rows are for.
//
// Baseline: bench/baselines/wire/typed_read_group_bench.json.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Framer;
using fixpp::wire::Parser;
using fixpp::wire::pmr_carry_buffer;

constexpr std::size_t kParseArena = 32768;
constexpr std::size_t kCarryArena = 512;

// Two messages over one field set:
//   'F' — FlatMsg:  NoOuter(100) delimited by OuterField(201); 201 is a plain
//                   scalar, so the delimiter-position descent probe answers
//                   false. Ordinary shape.
//   'C' — ModeCMsg: NoOuter(100) delimited by NoInner(200), which is itself a
//                   nested group's count tag. FR-021 mode (c).
constexpr std::string_view kBenchXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
    R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
    R"(<field number='201' name='OuterField' type='STRING'/>)"
    R"(<field number='202' name='InnerField' type='STRING'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='FlatMsg' msgtype='F' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoOuter' required='N'>)"
    R"(<field name='OuterField' required='N'/>)"
    R"(</group></message>)"
    R"(<message name='ModeCMsg' msgtype='C' msgcat='app'>)"
    R"(<field name='BeginString' required='N'/>)"
    R"(<field name='BodyLength' required='N'/>)"
    R"(<field name='MsgType' required='N'/>)"
    R"(<field name='CheckSum' required='N'/>)"
    R"(<group name='NoOuter' required='N'>)"
    R"(<group name='NoInner' required='N'>)"
    R"(<field name='InnerField' required='N'/>)"
    R"(</group></group></message>)"
    R"(</messages></fix>)";

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

// 35=F | 100=N | (201=x) * N — ordinary group, N instances.
[[nodiscard]] std::string flat_body(unsigned n) {
    std::string s = "35=F\x01";
    s += "100=" + std::to_string(n) + "\x01";
    for (unsigned i = 0; i < n; ++i) {
        s += "201=x\x01";
    }
    return s;
}

// 35=C | 100=N | (200=1 202=y) * N — mode (c): each outer instance opens with
// nested group 200's count tag, which is also outer group 100's delimiter.
[[nodiscard]] std::string mode_c_body(unsigned n) {
    std::string s = "35=C\x01";
    s += "100=" + std::to_string(n) + "\x01";
    for (unsigned i = 0; i < n; ++i) {
        s += "200=1\x01";
        s += "202=y\x01";
    }
    return s;
}

void run_split_bench(benchmark::State& state, std::string const& body, unsigned expect_slices,
                     char const* label) {
    std::vector<std::byte> dict_buf(2UZ * 1024UZ * 1024UZ);
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.data(), dict_buf.size()};
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kBenchXml, &dict_mr);
    auto const tv = dict.as_table_view();

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
    auto const frame = (*fed)[0];

    std::vector<std::byte> parse_buf(kParseArena);
    Parser<access_mode::Index> parser{tv};

    // Warm-up + shape check BEFORE the timed loop. Without it, a fixture that
    // silently truncated to one slice would still "pass" the bench and report
    // the cheap branch's number under the expensive branch's name — the exact
    // false-green this case's mode-(c) requirement exists to prevent.
    {
        std::pmr::monotonic_buffer_resource parse_mr{parse_buf.data(), parse_buf.size(),
                                                     std::pmr::null_memory_resource()};
        auto mv = parser.parse(frame, &parse_mr);
        if (!mv) {
            state.SkipWithError((std::string(label) + " fixture failed to parse").c_str());
            return;
        }
        auto const r = mv->offsets().group_slices_status(100);
        if (r.alloc_failed || r.slices.size() != expect_slices) {
            state.SkipWithError((std::string(label) + " fixture split into " +
                                 std::to_string(r.slices.size()) + " slices, expected " +
                                 std::to_string(expect_slices))
                                    .c_str());
            return;
        }
    }

    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource parse_mr{parse_buf.data(), parse_buf.size(),
                                                     std::pmr::null_memory_resource()};
        auto mv = parser.parse(frame, &parse_mr);
        auto const r = mv->offsets().group_slices_status(100);
        benchmark::DoNotOptimize(r.slices.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace

// ── BM_TypedReadGroup_Flat2 ─────────────────────────────────────────────────
// Ordinary group: the C-8.0c descent probe runs once per instance and answers
// FALSE both times. Pure added-probe cost, no recursion.
static void BM_TypedReadGroup_Flat2(benchmark::State& state) {
    run_split_bench(state, flat_body(2), 2, "Flat_2Inst");
}
BENCHMARK(BM_TypedReadGroup_Flat2);

// ── BM_TypedReadGroup_ModeC2 ────────────────────────────────────────────────
// FR-021 mode (c) — the outer delimiter IS a nested group's count tag, so the
// descent probe answers TRUE and recurses once per outer instance. Pre-083
// this shape returned ONE slice (truncated extent), so its cost is not
// comparable to a pre-083 measurement: it is more work because it is now
// correct, not because it regressed.
static void BM_TypedReadGroup_ModeC2(benchmark::State& state) {
    run_split_bench(state, mode_c_body(2), 2, "ModeC_2Inst");
}
BENCHMARK(BM_TypedReadGroup_ModeC2);

// ── BM_TypedReadGroup_ModeC8 ────────────────────────────────────────────────
// The same shape at 8 instances. Comparing this row to ModeC2 separates the
// PER-INSTANCE term (the descent probe + recursion) from the fixed
// per-materialization term, so the scaling claim is derived from two measured
// points rather than asserted.
static void BM_TypedReadGroup_ModeC8(benchmark::State& state) {
    run_split_bench(state, mode_c_body(8), 8, "ModeC_8Inst");
}
BENCHMARK(BM_TypedReadGroup_ModeC8);

BENCHMARK_MAIN();
