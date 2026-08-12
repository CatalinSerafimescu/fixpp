// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/fix42_group_parse_bench.cpp
//
// 082-structural-group-detection T047 — FR-022 (b), the inbound PARSE path.
// `[const §VIII.3]`: the benchmark ships in the SAME PR as the change; a
// narrative assertion that the ±5% budget was met is not acceptable in its
// place.
//
// ── What changed on this path, and therefore what is measured ───────────────
// `table_view::is_group_tag()` is the parse path's group pre-filter
// (`table_view.hpp:737-741`): it words-indexes `group_bits_` and returns false
// when `w >= group_bits_.size()`. On FIX 4.0/4.1/4.2 that vector was **EMPTY**
// before 082, because those dictionaries type every `<group>` count field as
// legacy XML `INT` and detection keyed on the datatype — so every tag took the
// `w >= size()` short-circuit and the parser did **no group work at all**.
// After 082 the vector holds 7 words for FIX 4.2 (max registered `no_tag` 428
// → `(428 >> 6) + 1`), so the same call now loads a word and tests a bit, and a
// set bit descends into real group-context resolution (D-12).
//
// ── The three cases, chosen to separate the regimes ────────────────────────
//   NoGroup     — 35=0 Heartbeat on FIX 4.2. Carries no group tag, so every
//                 entry still resolves to "not a group" — but it now does so by
//                 LOADING A WORD AND TESTING A BIT instead of failing a size
//                 check. This is the regression risk that matters most: it is
//                 the common path for every FIX 4.0/4.1/4.2 session, group-free
//                 or not, and it must stay flat.
//   TopLevel    — 35=W MarketDataSnapshotFullRefresh, `NoMDEntries(268)`,
//                 delimiter `MDEntryType(269)`. ⚠️ This row has **no
//                 meaningful pre-082 counterpart**: before 082 the frame parsed
//                 as FLAT fields with 268 treated as a plain scalar, so a
//                 "before" number would be measuring a different operation, not
//                 a faster one. Recorded as new rather than baselined as a
//                 regression — same disposition 083's `IncrementalX` row took.
//   Nested      — 35=i MassQuote, `NoQuoteSets(296)` → `NoQuoteEntries(295)`.
//                 The two-level shape US4's exemplar writes. Also has no
//                 pre-082 counterpart.
//
// A REAL shipped dictionary is used (`dictionaries/FIX42.xml` via
// `dict::XmlLoader` → `Dictionary::as_table_view()`), never a hand-built
// `table_view`: a hand-built one populates neither `group_bits_` nor
// `group_ctx_`, so every lookup would short-circuit and the bench would measure
// exactly the pre-082 path it exists to replace.
//
// ⚠️ ANTI-VACUITY. Each group-bearing case ASSERTS registration and a non-empty
// group walk before the timed loop. A bench whose group silently failed to
// register would time a flat parse and report a perfectly plausible number —
// indistinguishable from success, and precisely the failure this feature is
// about. `SkipWithError` is used so the row goes visibly absent rather than
// quietly wrong.
//
// Baseline: bench/baselines/wire/fix42_group_parse_bench.json.

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

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Framer;
using fixpp::wire::Parser;
using fixpp::wire::pmr_carry_buffer;

constexpr std::string_view kSendingTime = "20260516-09:30:00.000";

constexpr std::size_t kDictArena = 4u * 1024u * 1024u;
constexpr std::size_t kParseArena = 32768;
constexpr std::size_t kCarryArena = 512;

[[nodiscard]] std::filesystem::path dict_path(std::string_view filename) {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
}

// as_table_view() legally outlives the Dictionary it was built from
// (dictionary.hpp) — same idiom as validate_group_bench.cpp / validator_bench.cpp.
[[nodiscard]] fixpp::dict::table_view load_fix42_table_view(std::pmr::memory_resource* mr) {
    auto dict = fixpp::dict::XmlLoader{}.load(dict_path("FIX42.xml"), mr);
    return dict.as_table_view();
}

[[nodiscard]] std::vector<std::byte> build_frame(std::string_view body) {
    std::string pre = std::string("8=FIX.4.2\x01") + "9=" + std::to_string(body.size()) + "\x01";
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

// (a) 35=0 Heartbeat — no group tag anywhere. The common-path guard.
[[nodiscard]] std::string heartbeat_body() { return header('0'); }

// (b) 35=W — NoMDEntries(268), delimiter MDEntryType(269). Two entries.
//     Required top-level fields per FIX42.xml: Symbol(55), and within the
//     group MDEntryType(269) / MDEntryPx(270).
[[nodiscard]] std::string snapshot_body() {
    std::string s = header('W');
    s += "55=AAPL\x01";
    s += "268=2\x01";
    // NOTE: each \x01 is closed by a quote. `"\x01270=..."` would parse the
    // whole run as ONE hex escape (\x01270) and overflow — C++ hex escapes are
    // greedy, unlike octal.
    s += "269=0\x01" "270=10.5\x01" "271=100\x01";
    s += "269=1\x01" "270=10.6\x01" "271=200\x01";
    return s;
}

// (c) 35=i MassQuote — NoQuoteSets(296) → NoQuoteEntries(295). Mirrors the
//     T044/T045 exemplar body exactly, including UnderlyingSymbol(311), which
//     FIX 4.2 marks required inside NoQuoteSets and FIX 4.4 does not.
[[nodiscard]] std::string mass_quote_body() {
    std::string s = header('i');
    s += "117=QID-100\x01";
    s += "296=1\x01";
    s += "302=QS1\x01" "311=AAPL\x01" "304=1\x01";
    s += "295=1\x01";
    s += "299=QE1\x01" "132=10.5\x01" "133=10.75\x01";
    return s;
}

// Runs one case. `expect_group` names the count tag that MUST be registered and
// MUST yield a non-empty walk, or 0 for the group-free row.
void run_parse_bench(benchmark::State& state, std::string const& body, std::string_view label,
                     std::uint16_t expect_group) {
    auto dict_buf = std::make_unique<std::byte[]>(kDictArena);
    std::pmr::monotonic_buffer_resource dict_mr{dict_buf.get(), kDictArena};
    auto const tv = load_fix42_table_view(&dict_mr);

    // ── Anti-vacuity gate 1: the group must be REGISTERED. ──
    // Without this, an unregistered group yields a flat parse that times fine
    // and means nothing — the exact silent-success this feature removes.
    if (expect_group != 0 && tv.group_first_field(expect_group) == 0) {
        state.SkipWithError((std::string(label) + ": tag " + std::to_string(expect_group) +
                             " is NOT a registered group on FIX42 — this bench would time a FLAT "
                             "parse and report a plausible number. Refusing to measure.")
                                .c_str());
        return;
    }
    // Symmetric check for the group-free row: is_group_tag() must be live
    // (group_bits_ non-empty) or the 'common path' claim is untested.
    if (expect_group == 0 && tv.group_first_field(268) == 0) {
        state.SkipWithError((std::string(label) +
                             ": FIX42 registered no groups at all, so group_bits_ is empty and this "
                             "row measures the PRE-082 short-circuit, not the post-082 bit test.")
                                .c_str());
        return;
    }

    auto const frame_bytes = build_frame(body);
    std::array<std::byte, kCarryArena> carry_buf{};
    std::pmr::monotonic_buffer_resource carry_mr{carry_buf.data(), carry_buf.size(),
                                                 std::pmr::null_memory_resource()};
    pmr_carry_buffer carry{carry_buf.size(), &carry_mr};
    std::array<fixpp::wire::frame_view, 4> feed_out{};
    Framer framer{};
    auto const fed = framer.feed(std::span<const std::byte>{frame_bytes}, carry,
                                std::span<fixpp::wire::frame_view>{feed_out});
    if (!fed || fed->empty()) {
        state.SkipWithError((std::string(label) + " fixture failed to frame").c_str());
        return;
    }

    std::array<std::byte, kParseArena> parse_buf{};

    // ── Anti-vacuity gate 2: warm-up parse must SUCCEED and, for a
    // group-bearing row, must actually produce group slices. ──
    {
        std::pmr::monotonic_buffer_resource warm_mr{parse_buf.data(), parse_buf.size(),
                                                    std::pmr::null_memory_resource()};
        Parser<access_mode::Index> warm{tv};
        auto mv = warm.parse((*fed)[0], &warm_mr);
        if (!mv) {
            state.SkipWithError((std::string(label) + " fixture failed to parse").c_str());
            return;
        }
        if (expect_group != 0) {
            auto const slices = mv->offsets().group_slices(expect_group);
            if (slices.empty()) {
                state.SkipWithError(
                    (std::string(label) + ": group " + std::to_string(expect_group) +
                     " produced ZERO slices — the parse did not descend into the group, so the "
                     "timed loop would measure a flat walk. Refusing to measure.")
                        .c_str());
                return;
            }
            state.counters["group_entries"] = static_cast<double>(slices.size());
        }
    }

    Parser<access_mode::Index> parser{tv};
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource parse_mr{parse_buf.data(), parse_buf.size(),
                                                     std::pmr::null_memory_resource()};
        auto r = parser.parse((*fed)[0], &parse_mr);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace

// ── BM_Fix42Parse_NoGroup ───────────────────────────────────────────────────
// The common path, and the only row on this profile with a real pre-082
// counterpart. It must stay flat: if it moved, every FIX 4.0/4.1/4.2 session
// pays for group registration whether or not its traffic carries groups.
static void BM_Fix42Parse_NoGroup(benchmark::State& state) {
    run_parse_bench(state, heartbeat_body(), "NoGroup", 0);
}
BENCHMARK(BM_Fix42Parse_NoGroup);

// ── BM_Fix42Parse_TopLevelGroup ─────────────────────────────────────────────
// 35=W / NoMDEntries(268). NO pre-082 counterpart — 268 parsed as a plain
// scalar before this feature, so there is no "before" to regress against.
static void BM_Fix42Parse_TopLevelGroup(benchmark::State& state) {
    run_parse_bench(state, snapshot_body(), "TopLevelW", 268);
}
BENCHMARK(BM_Fix42Parse_TopLevelGroup);

// ── BM_Fix42Parse_NestedGroup ───────────────────────────────────────────────
// 35=i / NoQuoteSets(296) → NoQuoteEntries(295). The US4 exemplar shape; also
// no pre-082 counterpart.
static void BM_Fix42Parse_NestedGroup(benchmark::State& state) {
    run_parse_bench(state, mass_quote_body(), "NestedMassQuote", 296);
}
BENCHMARK(BM_Fix42Parse_NestedGroup);

BENCHMARK_MAIN();
