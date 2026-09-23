// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/dictionary/reify_bench.cpp
//
// T047 — NFR-003-3 dict::reify / reify_as latency harness (seam #5/#6).
//
// NFR-003-3 ceilings:
//   dict::reify_as<Msg>:  ≤ 1 µs (20-tag msg), ≤ 10 µs (200-tag msg)
//   dict::reify (dispatch): ≤ 1.2 µs (20-tag msg)
//
// Also exercises the codegen-table lookup arm (seam #5/#6 — the codegen-driven
// dispatch switch in _dispatch/reify_dispatch_application.hpp).
//
// Baseline seeds: bench/baselines/dictionary/reify_bench.json (written on
// the first green CI run that includes this bench).
//
// ─── R6 DEFERRED NOTICE ────────────────────────────────────────────────────
// FIXPP_R6_BENCH_DEFERRED: The vendored frozen wire stub
// (include/fixpp/wire/message_view_contract.hpp) carries NO frame state.
//
// dict::reify_as<Msg> is a free function template declared in
// include/fixpp/dict/reify.hpp but its BODY is R6-deferred — there is no
// implementation until 2b lands (the body requires OffsetTable-backed frame
// bytes to actually deep-copy). This bench therefore uses
// owning_<Msg>::from_view() directly (the generated Reify.hpp defines its
// body, so it links) to measure the allocation + dispatch path overhead.
//
// from_view() IS the implementation that reify_as<Msg> will delegate to;
// the bench correctly captures the same hot path.
//
// dict::reify() also returns field-absent in R6 scope (get<35>() on the stub
// always returns dict_xml_parse_failed). For the functions below that pass a
// default-constructed MV (BM_ReifyAs_20tag, BM_ReifyAs_200tag,
// BM_Reify_Dispatch_20tag), these benchmarks therefore measure dispatch and
// error-path overhead only; real NFR-003-3 numbers require the 2b wire
// feature. NFR-003-3 assertion gates MUST NOT be derived from stub timings.
// See spec.md §11 R6 / plan.md Tier-1 preset matrix.
// BM_Reify_DictBacked_20tag below does NOT pass a default-constructed MV --
// it parses a real dict-backed frame, so this scope does not apply to it.
// ───────────────────────────────────────────────────────────────────────────

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Generated reify headers (build-tree only, AC-C4).
// from_view() is defined in the generated Reify.hpp (inline out-of-class body).
#include <fixpp/v44/Reify.hpp>
#include <fixpp/v50sp2/Reify.hpp>
#include <fixpp/vt11/Reify.hpp>

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// `_20tag` / `_200tag` in a benchmark name below designates the NFR-003-3
// workload CLASS a row is compared against (spec.md §11), not the field
// count of the frame it constructs -- read the frame each function builds,
// not its suffix, for what it actually times.
//
// Stack-local PMR monotonic buffer sized for a typical "20-tag" message.
constexpr std::size_t k20TagBufSz = 4 * 1024;
// 64 KiB for the "200-tag" scenario.
constexpr std::size_t k200TagBufSz = 64 * 1024;

}  // namespace

// ── BM_ReifyAs_20tag ─────────────────────────────────────────────────────────
// owning_NewOrderSingle::from_view(view, mr) — proxy for reify_as<NOS> —
// NFR ceiling ≤ 1 µs (R6 deferred; from_view is the reify_as impl path).
// NewOrderSingle is the canonical 20-tag FIX44 message.
static void BM_ReifyAs_20tag(benchmark::State& state) {
    MV mv;
    std::array<std::byte, k20TagBufSz> buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
        auto r = fixpp::v44::owning_NewOrderSingle::from_view(mv, &arena);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ReifyAs_20tag);

// ── BM_ReifyAs_200tag ────────────────────────────────────────────────────────
// owning_v50sp2::NewOrderSingle::from_view(view, mr) — NFR ceiling ≤ 10 µs.
// v50sp2::NewOrderSingle has a substantially larger field set (~200 tags).
// NFR-003-3 200-tag workload. (R6 deferred)
static void BM_ReifyAs_200tag(benchmark::State& state) {
    MV mv;
    std::array<std::byte, k200TagBufSz> buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
        auto r = fixpp::v50sp2::owning_NewOrderSingle::from_view(mv, &arena);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ReifyAs_200tag);

// ── BM_Reify_Dispatch_20tag ──────────────────────────────────────────────────
// dict::reify(view, version_profile, mr) — NFR ceiling ≤ 1.2 µs (R6 deferred).
// Exercises the codegen-table lookup arm (seam #5/#6): runtime dispatch reads
// MsgType from the frozen stub (returns field-absent → error propagation).
// The dispatch infrastructure (error path, profile lookup) is exercised.
static void BM_Reify_Dispatch_20tag(benchmark::State& state) {
    MV mv;
    std::array<std::byte, k20TagBufSz> buf{};

    // A v44-default profile (no FIXT session-level default).
    fixpp::dict::version_profile profile{};
    profile.default_appl = fixpp::dict::application_version::v44;

    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size()};
        auto r = fixpp::dict::reify(mv, profile, &arena);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Reify_Dispatch_20tag);

// ── BM_Reify_DictBacked_20tag ─────────────────────────────────────────────────
// gate-b/r1 (090-capi-refusals G-1 / [const §VIII.3]): dict::reify() on a
// REAL dict-backed, MsgType-bearing v44 NewOrderSingle. Unlike
// BM_Reify_Dispatch_20tag above -- a default-constructed MV with no MsgType,
// so dict::reify() returns at the missing-tag-35 step before any dispatch --
// this frame carries tag 35 and is parsed against a real FIX44 table_view, so
// generated dispatch runs and the factory's EAGER materialisation (D-4,
// fixpp#458 -- the dict-backed re-parse that used to run lazily on first
// view() access, now moved into the factory call itself) is on the timed
// path. The dictionary load, frame assembly and source parse all happen
// ONCE, outside the timed loop; only dict::reify() itself is timed.
static void BM_Reify_DictBacked_20tag(benchmark::State& state) {
    auto const dict_path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    std::array<std::byte, 4U * 1024U * 1024U> dict_buf{};
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.data(), dict_buf.size()};
    auto const dictionary = fixpp::dict::XmlLoader{}.load(dict_path, &dict_mr);
    auto const tv = dictionary.as_table_view();

    // ClOrdID(11)="ORD1", MsgType(35)="D", v44. BodyLength + CheckSum computed
    // (mirrors tests/support/reify_test_frame.hpp's make_nos_frame() --
    // reproduced locally rather than included, since a tests/support header
    // is not on a bench target's include path).
    std::string const body = std::string("35=D\x01") + "34=1\x01" + "49=S\x01" + "56=T\x01" +
                             "11=ORD1\x01" + "55=AAPL\x01";
    std::string const pre =
        std::string("8=FIX.4.4\x01") + "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string const frame_str = pre + chk.data();
    std::vector<std::byte> frame_bytes(frame_str.size());
    std::memcpy(frame_bytes.data(), frame_str.data(), frame_str.size());

    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{frame_bytes.size(), &frame_mr};
    fixpp::wire::Framer framer{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = framer.feed(std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()},
                              carry, std::span<fixpp::wire::frame_view>{fvs, 1});
    if (!framed.has_value() || framed->empty()) {
        state.SkipWithError("fixture precondition failed: Framer::feed did not produce a frame");
        return;
    }
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{tv};
    auto parsed = parser.parse(fvs[0], &frame_mr);
    if (!parsed.has_value()) {
        state.SkipWithError("fixture precondition failed: dict-backed parse of the source frame");
        return;
    }

    fixpp::dict::version_profile profile{};
    profile.default_appl = fixpp::dict::application_version::v44;

    std::array<std::byte, k20TagBufSz> reify_buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{reify_buf.data(), reify_buf.size()};
        auto r = fixpp::dict::reify(*parsed, profile, &arena);
        if (!r.has_value()) {
            state.SkipWithError("dict::reify() unexpectedly refused the valid source frame");
            break;
        }
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Reify_DictBacked_20tag);

// ── fixpp#495 rows (`.specify/495-493-486-dict-reify-copy.md` §11) ─────────────
// Both rows reify the SAME v44 NewOrderSingle, whose body carries at least 20
// fields the FIX44 table accepts for MsgType D (checked in setup). The OWNED row
// parses it on Parser's owned route, as the Session dispatch path does, so
// dict::reify shares the table: it is NFR-003-3's 1.2 µs ceiling row. The
// BORROWED row parses it through Parser{tv}, so dict::reify deep-copies the table:
// informational only (the copy's cost on this frame), not a regression witness —
// BM_Reify_DictBacked_20tag is that.
//
// Setup checks (SkipWithError): the frame parses with >= 20 entries, >= 20 of them
// valid for D; one untimed reify succeeds and, on the owned row, shares the
// owner's table (the timed loop reaches the owned path, not an early return); and
// the arena margin rule holds — one untimed reify's draw from a counting resource
// over this frame must fit HALF of the timed loop's arena. That arena has
// `null_memory_resource()` upstream, so an overflow in the timed loop is a refusal
// (caught below), never a silent heap allocation.
namespace {

constexpr std::size_t kNfr495Arena = 16U * 1024U;

// Counts the bytes drawn from it; forwards to new_delete_resource().
class drawn_bytes_resource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] std::size_t drawn() const noexcept { return drawn_; }

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        drawn_ += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& o) const noexcept override {
        return this == &o;
    }
    std::size_t drawn_ = 0;
};

// A v44 NewOrderSingle whose body carries >= 20 fields; BodyLength + CheckSum
// computed.
std::vector<std::byte> make_nos_20field_frame() {
    std::string const body =
        std::string("35=D\x01") + "34=1\x01" + "49=S\x01" + "52=20240101-00:00:00.000\x01" +
        "56=T\x01" + "1=ACCT\x01" + "11=ORD1\x01" + "15=USD\x01" + "18=G\x01" + "21=1\x01" +
        "22=4\x01" + "38=100\x01" + "40=2\x01" + "44=10.5\x01" + "48=US0378331005\x01" +
        "54=1\x01" + "55=AAPL\x01" + "58=bench\x01" + "59=0\x01" + "60=20240101-00:00:00.000\x01" +
        "100=XNAS\x01" + "110=10\x01" + "207=XNAS\x01";
    std::string const pre =
        std::string("8=FIX.4.4\x01") + "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string const full = pre + chk.data();
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

void reify_20field(benchmark::State& state, bool owned) {
    auto const dict_path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    auto dict_mr = std::make_unique<std::pmr::monotonic_buffer_resource>();
    auto const dictionary = fixpp::dict::XmlLoader{}.load(dict_path, dict_mr.get());
    std::shared_ptr<const fixpp::dict::table_view> const sp =
        std::make_shared<const fixpp::dict::table_view>(dictionary.as_table_view());

    auto const frame_bytes = make_nos_20field_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{frame_bytes.size(), &frame_mr};
    fixpp::wire::Framer framer{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = framer.feed(std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()},
                              carry, std::span<fixpp::wire::frame_view>{fvs, 1});
    if (!framed.has_value() || framed->empty()) {
        state.SkipWithError("setup: Framer::feed did not produce a frame");
        return;
    }
    std::optional<fixpp::wire::Parser<fixpp::wire::access_mode::Index>> parser;
    if (owned) {
        parser.emplace(fixpp::wire::detail::owned_route_key{}, sp);
    } else {
        parser.emplace(*sp);
    }
    auto parsed = parser->parse(fvs[0], &frame_mr);
    if (!parsed.has_value()) {
        state.SkipWithError("setup: dict-backed parse of the source frame failed");
        return;
    }
    std::size_t valid_for_d = 0;
    for (auto const& e : parsed->offsets().entries()) {
        valid_for_d += sp->field_valid_for("D", e.tag) ? 1U : 0U;
    }
    if (parsed->offsets().entries().size() < 20U || valid_for_d < 20U) {
        state.SkipWithError("setup: the frame must carry >= 20 fields valid for MsgType D");
        return;
    }

    fixpp::dict::version_profile profile{};
    profile.default_appl = fixpp::dict::application_version::v44;
    {
        drawn_bytes_resource counting;
        auto probe = fixpp::dict::reify(*parsed, profile, &counting);
        if (!probe.has_value()) {
            state.SkipWithError("setup: an untimed dict::reify() refused the source frame");
            return;
        }
        if (owned && probe->view().hooks().opaque_dict() != sp.get()) {
            state.SkipWithError("setup: the owned row did not reach the owned path");
            return;
        }
        if (counting.drawn() * 2U > kNfr495Arena) {
            state.SkipWithError("setup: arena margin rule violated (draw exceeds half the arena)");
            return;
        }
    }

    std::array<std::byte, kNfr495Arena> buf{};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size(),
                                                  std::pmr::null_memory_resource()};
        auto r = fixpp::dict::reify(*parsed, profile, &arena);
        if (!r.has_value()) {
            state.SkipWithError("dict::reify() refused in the timed loop (arena overflow?)");
            break;
        }
        benchmark::DoNotOptimize(r);
    }
}

}  // namespace

static void BM_Reify_DictBacked_Owned_20field(benchmark::State& state) {
    reify_20field(state, /*owned=*/true);
}
BENCHMARK(BM_Reify_DictBacked_Owned_20field);

static void BM_Reify_DictBacked_Borrowed_20field(benchmark::State& state) {
    reify_20field(state, /*owned=*/false);
}
BENCHMARK(BM_Reify_DictBacked_Borrowed_20field);

BENCHMARK_MAIN();
