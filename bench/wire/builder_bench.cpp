// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/builder_bench.cpp
//
// Outbound builder latency: the generated v44 build_NewOrderSingle (linked
// from the precompiled fixpp::builders::v44 lib) and fixpp::wire::body_builder
// driven directly. Each case builds a full body and commits it into a
// caller-owned buffer per iteration; all inputs are prepared once, outside
// the timed loop.
//
// Feature 091 (fixpp #418) SC-005 comparand: run this binary before and after
// the change and compare BM_Build_NOS_NoGroup (the representative message
// with no Data field). The generated builder comes from codegen output, so
// the after-run needs a regenerated tree: build the fixpp-codegen target,
// remove build/<preset>/_codegen, then rebuild this target.
//
// Every case first builds once outside the timed loop and checks a body
// substring that only the intended path produces; on a mismatch it calls
// SkipWithError, so a case that would time an early error exit reports an
// error instead of a number.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/v44/messages/NewOrderSingle.hpp>
#include <fixpp/wire/body_builder.hpp>
#include <memory_resource>
#include <span>
#include <string_view>

namespace {

using fixpp::decimal_t;
using fixpp::v44::NewOrderSingleArgs;
using fixpp::v44::groups::G_453Args;

// Decimals are parsed once into this arena, which outlives every benchmark.
std::pmr::memory_resource* decimal_arena() {
    static std::array<std::byte, 4096> buf{};
    static std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size(),
                                                  std::pmr::null_memory_resource()};
    return &mr;
}

decimal_t parse_decimal(std::string_view sv) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, decimal_arena());
    return r.value_or(decimal_t{});
}

const decimal_t& qty() {
    static const decimal_t d = parse_decimal("100");
    return d;
}

const decimal_t& px() {
    static const decimal_t d = parse_decimal("150.25");
    return d;
}

NewOrderSingleArgs base_args() {
    return NewOrderSingleArgs{
        .account = "ACC001",
        .cl_ord_id = "ORD12345678",
        .order_qty = qty(),
        .ord_type = '2',
        .price = px(),
        .side = '1',
        .symbol = "AAPL",
        .text = "BenchOrder",
        .time_in_force = '0',
        .transact_time = "20260516-09:30:00.000",
    };
}

std::string_view as_sv(std::span<const std::byte> b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

constexpr std::array<G_453Args, 3> kParties{{
    {.party_id = "EXECBROKER1", .party_id_source = 'D', .party_role = 1},
    {.party_id = "CLEARFIRM22", .party_id_source = 'D', .party_role = 4},
    {.party_id = "TRADER333", .party_id_source = 'D', .party_role = 11},
}};

constexpr std::string_view kAsciiEncodedText = "ASCII encoded text payload 0123";

// Build once outside the timed loop; require success and the path markers.
// With `exact`, the whole body must equal `must_have` byte for byte.
bool precheck(benchmark::State& state, const NewOrderSingleArgs& args, std::span<std::byte> out,
              std::string_view must_have, std::string_view must_not_have, bool exact) {
    auto r = fixpp::v44::build_NewOrderSingle(out, args);
    if (!r) {
        state.SkipWithError("build_NewOrderSingle failed in precheck");
        return false;
    }
    const auto body = as_sv(*r);
    if (exact && body != must_have) {
        state.SkipWithError("precheck: body differs from the expected bytes");
        return false;
    }
    if (body.find(must_have) == std::string_view::npos) {
        state.SkipWithError("precheck: expected path marker absent from body");
        return false;
    }
    if (!must_not_have.empty() && body.find(must_not_have) != std::string_view::npos) {
        state.SkipWithError("precheck: unexpected marker present in body");
        return false;
    }
    return true;
}

void run_nos(benchmark::State& state, const NewOrderSingleArgs& args, std::string_view must_have,
             std::string_view must_not_have, bool exact = false) {
    std::array<std::byte, 4096> out{};
    if (!precheck(state, args, out, must_have, must_not_have, exact)) return;
    for (auto _ : state) {
        auto r = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
        benchmark::DoNotOptimize(r);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace

// Representative NewOrderSingle: scalar fields only, no group, no Data field.
// The body is pinned exactly, so a before/after comparison cannot silently
// time two different messages.
constexpr std::string_view kNoGroupBody =
    "35=D\x01"
    "1=ACC001\x01"
    "11=ORD12345678\x01"
    "38=100\x01"
    "40=2\x01"
    "44=150.25\x01"
    "54=1\x01"
    "55=AAPL\x01"
    "58=BenchOrder\x01"
    "59=0\x01"
    "60=20260516-09:30:00.000\x01";

static void BM_Build_NOS_NoGroup(benchmark::State& state) {
    const auto args = base_args();
    run_nos(state, args, kNoGroupBody, "", /*exact=*/true);
}
BENCHMARK(BM_Build_NOS_NoGroup);

// Same scalars plus the Parties (NoPartyIDs 453) repeating group.
static void BM_Build_NOS_WithGroup(benchmark::State& state) {
    auto args = base_args();
    args.party_i_ds = std::span<const G_453Args>{kParties};
    run_nos(state, args,
            "\x01"
            "453=3\x01"
            "448=EXECBROKER1\x01",
            "");
}
BENCHMARK(BM_Build_NOS_WithGroup);

// Same scalars plus the coupled EncodedTextLen(354) + EncodedText(355) pair carrying an ASCII
// value.
static void BM_Build_NOS_AsciiEncodedText(benchmark::State& state) {
    auto args = base_args();
    args.encoded_text = kAsciiEncodedText;
    run_nos(state, args,
            "\x01"
            "354=31\x01"
            "355=ASCII encoded text payload 0123\x01",
            "");
}
BENCHMARK(BM_Build_NOS_AsciiEncodedText);

// body_builder driven directly: flat field() calls, then commit().
static void BM_BodyBuilder_Raw_10Fields(benchmark::State& state) {
    std::array<std::byte, 4096> out{};
    const auto& q = qty();
    const auto& p = px();
    auto build = [&]() noexcept -> fixpp::core::expected_t<std::span<std::byte>> {
        fixpp::wire::body_builder bb{"D"};
        if (auto r = bb.field(1, std::string_view{"ACC001"}); !r) return std::unexpected(r.error());
        if (auto r = bb.field(11, std::string_view{"ORD12345678"}); !r)
            return std::unexpected(r.error());
        if (auto r = bb.field(38, q); !r) return std::unexpected(r.error());
        if (auto r = bb.field(40, '2'); !r) return std::unexpected(r.error());
        if (auto r = bb.field(44, p); !r) return std::unexpected(r.error());
        if (auto r = bb.field(54, '1'); !r) return std::unexpected(r.error());
        if (auto r = bb.field(55, std::string_view{"AAPL"}); !r) return std::unexpected(r.error());
        if (auto r = bb.field(58, std::string_view{"BenchOrder"}); !r)
            return std::unexpected(r.error());
        if (auto r = bb.field(59, '0'); !r) return std::unexpected(r.error());
        if (auto r = bb.field(60, std::string_view{"20260516-09:30:00.000"}); !r)
            return std::unexpected(r.error());
        return bb.commit(std::span<std::byte>{out});
    };
    {
        auto r = build();
        if (!r || as_sv(*r).find("\x01"
                                 "60=20260516-09:30:00.000\x01") == std::string_view::npos) {
            state.SkipWithError("body_builder precheck failed");
            return;
        }
    }
    for (auto _ : state) {
        auto r = build();
        benchmark::DoNotOptimize(r);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BodyBuilder_Raw_10Fields);
