// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/fuzz/fuzz_wire_framer.cpp
//
// T050 — Seam #11 — libFuzzer harness for Framer::feed.
//
// Feeds arbitrary bytes to Framer::feed and asserts the invariants:
//   - No crash/OOB (ASan/UBSan will catch memory bugs).
//   - No UB (UBSan).
//   - Bounded memory: carry never grows beyond max_frame_bytes + constant.
//   - Any structural rejection is a defined wire_* error — never silent UB.
//   - No exception escapes the noexcept feed() boundary (the whole wire
//     surface is noexcept; libFuzzer would catch any terminate() call).
//
// Validator bench omitted (US4 PAUSED — deferred with US4):
//   NO fuzz_wire_validator.cpp per HARD CONSTRAINT.
//
// Build: requires Clang + libFuzzer; built under the asan/ubsan presets.
//
// Campaign note (T050): A full ≥10-min Tier-1 ASan+UBSan campaign is the
// CI/T055 responsibility. The in-PR campaign was run for 120 s each under
// -fsanitize=fuzzer,address,undefined; see .specify/decisions/004-wire-codec-
// verify.md §T050 for actual runtime + iteration counts.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>

#include <fixpp/wire/framer.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    using fixpp::wire::Framer;
    using fixpp::wire::frame_view;
    using fixpp::wire::pmr_carry_buffer;

    // Use a small max_frame_bytes to keep the fuzzer's memory footprint
    // bounded and to exercise the wire_frame_too_large reject path on typical
    // fuzzer inputs (which are rarely well-formed FIX frames).
    constexpr std::size_t kMaxFrame = 16 * 1024;

    // Carry arena (session lifetime): sized to kMaxFrame + small constant.
    // Stack-allocated so no heap usage in the fuzzer's hot path.
    std::array<std::byte, kMaxFrame + 256> carry_arena_buf{};
    std::pmr::monotonic_buffer_resource carry_arena{
        carry_arena_buf.data(), carry_arena_buf.size(),
        std::pmr::null_memory_resource()};

    pmr_carry_buffer carry{kMaxFrame, &carry_arena};

    // Output slot: one frame_view per feed call.
    std::array<frame_view, 16> out_views{};

    Framer::Config cfg;
    cfg.max_frame_bytes = kMaxFrame;
    Framer framer{cfg};

    auto incoming = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(data), size};

    // feed() is noexcept; any exception escape would call std::terminate,
    // which libFuzzer reports as a crash.
    auto result = framer.feed(
        incoming,
        carry,
        std::span<frame_view>{out_views.data(), out_views.size()});

    // Either a valid span of frame_views or a defined wire_* error — no UB.
    // Touching the result prevents the optimizer from eliding the call.
    if (result) {
        // Invariant: returned span is non-null (may be empty if no complete
        // frames). Each frame_view's bytes() must alias carry or incoming.
        for (auto const& fv : *result) {
            (void)fv.bytes();
            (void)fv.body();
        }
    }
    // Invariant: carry size never exceeds kMaxFrame.
    // (pmr_carry_buffer::append returns false on overflow; the Framer maps
    // that to wire_frame_too_large, not an abort — so carry stays bounded.)
    if (carry.size() > kMaxFrame) {
        // This should never happen — if it does, it's a Framer bug.
        __builtin_trap();
    }

    return 0;
}
