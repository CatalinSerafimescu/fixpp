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
// Validator harness: fuzz_wire_validator.cpp wired by 041-validation-gate-wiring
// (T026) — the feature-004 "US4 PAUSED" deferral is ended.
//
// Build: requires Clang + libFuzzer; built under the asan/ubsan presets.
//
// Campaign note (T050): A full ≥10-min Tier-1 ASan+UBSan campaign is the
// CI/T055 responsibility. The in-PR campaign was run for 120 s each under
// -fsanitize=fuzzer,address,undefined; see .specify/decisions/004-wire-codec-
// verify.md §T050 for actual runtime + iteration counts.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/wire/framer.hpp>
#include <memory_resource>
#include <span>

#include "support/framer_test_access.hpp"

namespace {

// 093-inbound-frame-dispositions (contract C-1; tasks.md T020): the resync arm. The
// input is fed `step` bytes per read, as the session's read pump feeds it (one out
// slot, drained after each read), into a Framer with resync on. It traps on
//   - non-termination: a drained Framer fed nothing again must do nothing (no frame,
//     no garble, no byte consumed), else a feed stopped short of what it could do;
//   - C-1's counted-work bound exceeded (framer_test_access).
// wire_frame_too_large ends the arm: the session closes there.
void resync_arm(std::span<const std::byte> input, std::size_t step) noexcept {
    using fixpp::wire::frame_view;
    using fixpp::wire::Framer;
    using fixpp::wire::framer_test_access;
    using fixpp::wire::pmr_carry_buffer;

    constexpr std::size_t kLimit = 16 * 1024;
    // R: the pump's per-read size, so the carry is L plus one read. Re-derive from
    // `read_buf` in run_read_pump (src/session/engine.cpp).
    constexpr std::size_t kReadSize = 4096;

    std::array<std::byte, kLimit + kReadSize + 256> carry_arena_buf{};
    std::pmr::monotonic_buffer_resource carry_arena{carry_arena_buf.data(), carry_arena_buf.size(),
                                                    std::pmr::null_memory_resource()};
    pmr_carry_buffer carry{kLimit + kReadSize, &carry_arena};
    std::array<frame_view, 1> out{};
    Framer framer{Framer::Config{.max_frame_bytes = kLimit, .resync_on_garble = true}};

    std::uint64_t received = 0;
    for (std::size_t pos = 0; pos < input.size(); pos += step) {
        auto const chunk = input.subspan(pos, std::min(step, input.size() - pos));
        received += chunk.size();
        auto r = framer.feed(chunk, carry, out);
        std::size_t drains = 0;
        while (r && !r->empty()) {
            if (++drains > input.size()) {
                __builtin_trap();  // more frames than bytes: a feed that never ends
            }
            r = framer.feed({}, carry, out);
        }
        if (!framer_test_access::within_work_bound(framer, received, kLimit, kReadSize)) {
            __builtin_trap();
        }
        if (!r) {
            return;
        }
        // The fixpoint. At rest the probe changes no state, so it runs on the Framer
        // itself; its work is not part of the stream's and is taken back.
        auto const counted = framer_test_access::work(framer);
        std::size_t const pending = framer.pending_bytes();
        auto again = framer.feed({}, carry, out);
        auto const g = framer.last_garbles();
        if (!again || !again->empty() || g.regions != 0U || g.discarded != 0U ||
            framer.pending_bytes() != pending) {
            __builtin_trap();
        }
        framer_test_access::restore_work(framer, counted);
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    using fixpp::wire::frame_view;
    using fixpp::wire::Framer;
    using fixpp::wire::pmr_carry_buffer;

    // Use a small max_frame_bytes to keep the fuzzer's memory footprint
    // bounded and to exercise the wire_frame_too_large reject path on typical
    // fuzzer inputs (which are rarely well-formed FIX frames).
    constexpr std::size_t kMaxFrame = 16 * 1024;

    // Carry arena (session lifetime): sized to kMaxFrame + small constant.
    // Stack-allocated so no heap usage in the fuzzer's hot path.
    std::array<std::byte, kMaxFrame + 256> carry_arena_buf{};
    std::pmr::monotonic_buffer_resource carry_arena{carry_arena_buf.data(), carry_arena_buf.size(),
                                                    std::pmr::null_memory_resource()};

    pmr_carry_buffer carry{kMaxFrame, &carry_arena};

    // Output slot: one frame_view per feed call.
    std::array<frame_view, 16> out_views{};

    Framer::Config cfg;
    cfg.max_frame_bytes = kMaxFrame;
    Framer framer{cfg};

    auto incoming = std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), size};

    // feed() is noexcept; any exception escape would call std::terminate,
    // which libFuzzer reports as a crash.
    auto result =
        framer.feed(incoming, carry, std::span<frame_view>{out_views.data(), out_views.size()});

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

    // 093: the same input through the resync arm, whole and one byte per read.
    if (size > 0U) {
        resync_arm(incoming, size);
        resync_arm(incoming, 1);
    }

    return 0;
}
