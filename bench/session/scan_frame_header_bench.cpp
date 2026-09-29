// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/scan_frame_header_bench.cpp
//
// 092-garbled-frame-reject (research R-9) — the inbound header scan
// `fixpp::session::detail::scan_frame_header` on CLEAN frames, the cost every
// inbound frame pays before any disposition. Landed in a bench-only commit before
// any production edit, so the same source builds against the merge-base and the
// candidate for the paired comparison (`[const §VIII.2]`).
//
// Frames: a Heartbeat, a NewOrderSingle, and a NewOrderSingle carrying a standard
// Length+Data pair (EncodedTextLen(354)/EncodedText(355)). Each is scanned under
// the dictionary hooks a session derives from its dictionary
// (`dict_hooks::for_table_view` of the real FIX44 dictionary) and under
// `dict_hooks::none()`.
//
// Reach: before timing, each case checks that the scan read the frame's
// MsgType and MsgSeqNum; the Length+Data case also checks that a forged
// `36=` inside the counted EncodedText value was NOT read as NewSeqNo, which
// holds only when the counted-Data path consumed the value. A failed check is a
// `SkipWithError`, never a timing.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/parser.hpp>  // dict_hooks::for_table_view
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bench target includes ${CMAKE_SOURCE_DIR} (library root); see
// bench/session/CMakeLists.txt.
#include "src/session/scan_frame_header.hpp"
#include "tests/support/fix44_dictionary.hpp"

namespace {

using fixpp::session::detail::scan_frame_header;
using fixpp::wire::dict_hooks;

std::vector<std::byte> make_frame(std::string_view msg_type, std::uint32_t seq,
                                  std::string const& extra_body) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += extra_body;

    std::string msg = "8=FIX.4.4\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[5];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

std::string nos_body() {
    return "11=ORD001\x01"
           "21=1\x01"
           "38=100\x01"
           "40=2\x01"
           "44=10.25\x01"
           "54=1\x01"
           "55=IBM\x01"
           "60=20240101-00:00:00.000\x01";
}

// The EncodedText value embeds `<SOH>36=77`: counted as Data it is one value; read
// as plain fields it would set NewSeqNo.
std::string nos_length_data_body() {
    std::string const text =
        "ab\x01"
        "36=77";
    return nos_body() + "354=" + std::to_string(text.size()) + "\x01" + "355=" + text + "\x01";
}

enum class frame_kind : int { heartbeat = 0, nos = 1, nos_length_data = 2 };

struct Case {
    std::vector<std::byte> frame;
    std::string_view msg_type;
    std::string_view seq;
};

Case make_case(frame_kind k) {
    switch (k) {
        case frame_kind::heartbeat:
            return {.frame = make_frame("0", 2, {}), .msg_type = "0", .seq = "2"};
        case frame_kind::nos:
            return {.frame = make_frame("D", 2, nos_body()), .msg_type = "D", .seq = "2"};
        case frame_kind::nos_length_data:
            return {
                .frame = make_frame("D", 2, nos_length_data_body()), .msg_type = "D", .seq = "2"};
    }
    return {};
}

// The dictionary outlives every hooks value derived from it.
std::shared_ptr<const fixpp::dict::Dictionary> const& fix44() {
    static auto const dict = fixpp::test_support::make_fix44_dictionary();
    return dict;
}

void run_scan(benchmark::State& state, dict_hooks const& hooks) {
    auto const kind = static_cast<frame_kind>(state.range(0));
    Case const c = make_case(kind);
    std::span<const std::byte> const frame{c.frame};

    auto const probe = scan_frame_header(frame, hooks);
    if (probe.msg_type != c.msg_type || probe.msg_seq_num != c.seq) {
        state.SkipWithError("scan did not read MsgType/MsgSeqNum of the clean frame");
        return;
    }
    if (kind == frame_kind::nos_length_data && !probe.new_seqno.empty()) {
        state.SkipWithError("EncodedText was not consumed as counted Data");
        return;
    }

    // `_` is the google-benchmark loop idiom: the loop runs for the iteration, not the value.
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        auto const* data = frame.data();
        benchmark::DoNotOptimize(data);
        auto h = scan_frame_header(std::span<const std::byte>{data, frame.size()}, hooks);
        benchmark::DoNotOptimize(h);
    }
}

void BM_ScanFrameHeader_DictHooks(benchmark::State& state) {
    auto const& dict = fix44();
    auto const tv = dict->as_table_view();
    run_scan(state, dict_hooks::for_table_view(tv));
}
BENCHMARK(BM_ScanFrameHeader_DictHooks)
    ->ArgName("frame")
    ->Arg(static_cast<int>(frame_kind::heartbeat))
    ->Arg(static_cast<int>(frame_kind::nos))
    ->Arg(static_cast<int>(frame_kind::nos_length_data));

void BM_ScanFrameHeader_NoHooks(benchmark::State& state) { run_scan(state, dict_hooks::none()); }
BENCHMARK(BM_ScanFrameHeader_NoHooks)
    ->ArgName("frame")
    ->Arg(static_cast<int>(frame_kind::heartbeat))
    ->Arg(static_cast<int>(frame_kind::nos))
    ->Arg(static_cast<int>(frame_kind::nos_length_data));

}  // namespace
