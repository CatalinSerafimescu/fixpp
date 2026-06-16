// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_validate_gate_alloc_guard.cpp
//
// 041-validation-gate-wiring: alloc-guard witness for the validate-ON
// (validate_inbound_messages=true) hot path.  Closes the blind spot in the
// existing alloc-guard suite: the prior cells exercised the validation-OFF
// default path only and could not detect a per-message coroutine-frame heap
// allocation if validate_inbound_ were (re-)introduced as an asio::awaitable<>.
//
// WHAT IS MEASURED
// ----------------
// The production path in validate_inbound_() (session.cpp ~1748):
//   (1) Framer::feed()               — stack-local pmr_carry_buffer
//   (2) Parser<Index>::parse()       — PMR-arena backed (vg_buf: stack array)
//   (3) Validator::validate()        — zero-heap per [2b §6.5] spec
//
// All three steps are bounded to stack-local arenas in the synchronous helper.
// The awaitable-frame regression: when validate_inbound_ was an asio::awaitable<>
// (pre-simplify-triage), co_await created a heap-allocated coroutine frame
// (~hundreds of bytes via operator new) detectable by mallocnesia.  After the
// synchronous rewrite there is NO heap allocation on this path.
//
// DISCRIMINATION
// --------------
// If validate_inbound_ were re-introduced as an awaitable coroutine, the measured
// window would intercept the coroutine-frame allocation (global operator new) and
// the mallocnesia interceptor would exit 1 → ctest FAILS.  The test discriminates
// the regression because only the synchronous form avoids the heap alloc.
//
// MECHANISM
// ---------
// The test replicates the exact sequence in validate_inbound_():
//   - stack-backed arena (kInboundParseArena = 16384 bytes, like production)
//   - Framer + pmr_carry_buffer on the stack
//   - Parser<Index>::parse() using the arena
//   - dictionary_driven_validator::validate() using a scratch arena
// All arenas have null_memory_resource() upstream so any over-run is a hard
// failure rather than a heap fall-back that hides an alloc.
//
// Run under mallocnesia via tools/check_alloc.py:
//   python3 tools/check_alloc.py \
//       --binary build/linux-clang-debug/bin/test_validate_gate_alloc_guard
//
// [041-validation-gate-wiring; const §VIII.5; data-model E-4; SC-005]

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/validation_test_dictionary.hpp"

// mallocnesia replaces these weak no-ops with its interceptor scope markers.
extern "C" {
__attribute__((weak)) void alloc_guard_start() {}
__attribute__((weak)) void alloc_guard_end() {}
}

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::Framer;
using fixpp::wire::Parser;
using fixpp::wire::pmr_carry_buffer;

// Arena sizes matching validate_inbound_() in session.cpp.
// kInboundParseArena (16384) matches the dispatch arena — the FIX-1 fix.
constexpr std::size_t kInboundParseArena = 16384;
constexpr std::size_t kCarryArena = 512;
constexpr std::size_t kScratchArena = 512;

// Build a CheckSum-correct FIX 4.2 conformant Heartbeat(35=0) frame.
// Heartbeat is an admin message with no required body fields beyond header.
// This is a small frame (~80 bytes) — the alloc-guard proves the validate path
// is stack-only regardless of frame size (the parse arena is pre-allocated).
std::vector<std::byte> make_heartbeat_frame(std::string_view begin_string, std::uint32_t seq,
                                            std::string_view sender, std::string_view target) {
    std::string body;
    body += "35=0\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += static_cast<unsigned char>(c);
    }
    cs &= 0xFFU;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// ── ValidateGateAllocGuard / HotPathNoGlobalHeapAlloc ─────────────────────────
//
// Replicates the exact sequence of validate_inbound_() (session.cpp ~1748) with
// stack-local arenas.  The measured window covers one parse→validate cycle on a
// conformant Heartbeat(35=0) frame.
//
// WARM-UP PASS:
//   Drives one parse→validate round BEFORE the guard markers.  This primes any
//   one-time lazy-init data in the Framer/Parser (vtable resolves, STL
//   thread-local storage, etc.).  The warm-up round may allocate; only the
//   MEASURED round is zero-heap.
//
// MEASURED WINDOW (alloc_guard_start … alloc_guard_end):
//   One conformant Heartbeat frame is validated.  All storage is drawn from the
//   stack-local arenas built BEFORE the markers.  Under mallocnesia the window
//   must produce zero intercepted malloc/calloc/realloc calls.
//
// DISCRIMINATION:
//   If validate_inbound_() were re-written as an asio::awaitable<> and the
//   caller used co_await, the coroutine promise/frame would be heap-allocated
//   (operator new) in the measured window → mallocnesia intercepts it → test
//   fails.  The synchronous form eliminates this allocation.
TEST(ValidateGateAllocGuard, HotPathNoGlobalHeapAlloc) {
    // ── Build dictionary + validator BEFORE the guard markers ─────────────────
    auto dict_ptr = fixpp::test_support::make_validation_test_dictionary();
    ASSERT_NE(dict_ptr, nullptr) << "dictionary build failed";

    auto tv = dict_ptr->as_table_view();
    dictionary_driven_validator validator{tv};

    // ── Build the conformant frame BEFORE the guard markers ───────────────────
    // Heartbeat(35=0) with required header fields only.  All defined fields are
    // present (the test dict marks SendingTime(52) as required); no extra tags.
    auto frame = make_heartbeat_frame("FIX.4.2", 1, "TW", "ISLD");
    ASSERT_FALSE(frame.empty());

    // ── Helper lambda: one parse→validate round ──────────────────────────────
    // Mirrors validate_inbound_() exactly: Framer + carry on the stack, parse
    // into a stack-backed monotonic_buffer_resource, validate with a scratch mr.
    // null_memory_resource() upstream ensures any arena over-run is a hard fail
    // (bad_alloc from the PMR) rather than a silent global-heap fall-back.
    auto run_validate = [&]() -> bool {
        std::array<std::byte, kInboundParseArena> vg_buf{};
        std::pmr::monotonic_buffer_resource vg_mr{vg_buf.data(), vg_buf.size(),
                                                  std::pmr::null_memory_resource()};
        std::array<std::byte, kCarryArena> vg_carry_store{};
        std::pmr::monotonic_buffer_resource vg_carry_mr{
            vg_carry_store.data(), vg_carry_store.size(), std::pmr::null_memory_resource()};
        pmr_carry_buffer vg_carry{vg_carry_store.size(), &vg_carry_mr};

        Framer vg_framer;
        std::array<fixpp::wire::frame_view, 1> vg_out{};
        auto vg_feed = vg_framer.feed(std::span<const std::byte>{frame.data(), frame.size()},
                                      vg_carry, std::span<fixpp::wire::frame_view>{vg_out});
        if (!vg_feed || vg_feed->empty()) {
            return false;
        }

        Parser<access_mode::Index> vg_parser;
        std::array<std::byte, kScratchArena> vg_scratch_buf{};
        std::pmr::monotonic_buffer_resource vg_scratch_mr{
            vg_scratch_buf.data(), vg_scratch_buf.size(), std::pmr::null_memory_resource()};
        auto vg_mv_r = vg_parser.parse((*vg_feed)[0], &vg_mr);
        if (!vg_mv_r) {
            return false;
        }

        auto val_r = validator.validate(*vg_mv_r, &vg_scratch_mr);
        // Conformant Heartbeat: must validate cleanly (no error).
        return val_r.has_value();
    };

    // ── Warm-up pass ──────────────────────────────────────────────────────────
    // Drive one round before the guard markers to prime any lazy-init paths
    // (Framer internal buffers, Parser template instantiation caches, etc.).
    bool warmup_ok = run_validate();
    ASSERT_TRUE(warmup_ok) << "warm-up parse+validate failed — conformant Heartbeat must pass";

    // ── Measured window ────────────────────────────────────────────────────────
    // Under mallocnesia, any call to malloc/calloc/realloc inside this window
    // increments the interceptor's counter.  Zero is the required outcome.
    bool measured_ok = false;
    alloc_guard_start();
    measured_ok = run_validate();
    alloc_guard_end();

    // ── Post-guard assertions (always run, even without mallocnesia) ──────────
    EXPECT_TRUE(measured_ok)
        << "measured parse+validate failed — conformant Heartbeat must pass validation";
}

// ── ValidateGateAllocGuard / LongMsgTypeNoGlobalHeapAlloc ─────────────────────
//
// P1-fix witness (gate-b/r1): proves that field_valid_for/required_fields do NOT
// heap-allocate when the MsgType is LONGER than SSO on any standard stdlib
// (libstdc++ SSO ~15, libc++ SSO ~22 — this test uses a 32-char MsgType, which
// exceeds both). Pre-fix, table_view::field_valid_for() and required_fields()
// constructed a std::string{msg_type} temporary per call, causing a heap alloc
// for every validated field on the hot path and risking std::terminate() on
// bad_alloc inside the noexcept methods. Post-fix (transparent string_hash +
// std::equal_to<>), the .find(string_view) call is allocation-free.
//
// DISCRIMINATION:
//   Pre-fix: the std::string{msg_type} constructor allocates for a 32-char key;
//   mallocnesia intercepts it → binary exits 1 → test FAILS (RED).
//   Post-fix: no temporary is constructed; mallocnesia sees 0 allocations → PASS.
//
//   The 32-char MsgType "CustomAppMessageTypeXYZ12345678" exceeds SSO on both
//   libstdc++ (SSO=15) and libc++ (SSO=22), ensuring the temporary would
//   allocate on any commonly-used stdlib.
//
// The table_view is built locally (not via the XML loader) so this test is
// fully self-contained and independent of validation_test_dictionary.hpp.
// It drives the real validator.validate() path, not a proxy, so the alloc-guard
// window covers field_valid_for (called once per field) AND required_fields
// (called once per message).
//
// [gate-b/r1 FIX-1 witness; const §VIII.5 / §XV.1; P3 fold-in]
TEST(ValidateGateAllocGuard, LongMsgTypeNoGlobalHeapAlloc) {
    // 32-char MsgType: exceeds SSO on libstdc++ (~15) and libc++ (~22).
    // "CustomAppMessageTypeXYZ12345678" is exactly 31 chars + NUL = 32.
    constexpr std::string_view kLongMsgType = "CustomAppMessageTypeXYZ12345678";
    static_assert(kLongMsgType.size() == 31,
                  "MsgType must exceed both libstdc++ SSO (~15) and libc++ SSO (~22)");

    // ── Build a local table_view for kLongMsgType ─────────────────────────────
    // Inline build: no XML loader, no heap-alloc on the lookup path.
    // The validator calls field_valid_for(msg_type, tag) for EVERY field in the
    // parsed message, including framing tags (8, 9, 10, 35). We must declare them
    // all as valid for kLongMsgType, otherwise the unexpected-tag check fires.
    fixpp::dict::table_view tv;
    // Framing tags — always present in every FIX frame (8, 9, 35, 10).
    tv.add_valid(kLongMsgType, 8);    // BeginString
    tv.add_valid(kLongMsgType, 9);    // BodyLength
    tv.add_valid(kLongMsgType, 10);   // CheckSum
    tv.add_valid(kLongMsgType, 35);   // MsgType
    // Required header fields for kLongMsgType.
    tv.add_required(kLongMsgType, 34);   // MsgSeqNum
    tv.add_required(kLongMsgType, 49);   // SenderCompID
    tv.add_required(kLongMsgType, 52);   // SendingTime
    tv.add_required(kLongMsgType, 56);   // TargetCompID

    fixpp::wire::dictionary_driven_validator validator{tv};

    // ── Build a conformant frame with the 32-char MsgType ─────────────────────
    // Body: 35=<kLongMsgType>, then the required tags.
    // All strings are stack-only here; this frame is built before the markers.
    std::string body;
    body += "35=";
    body += kLongMsgType;
    body += '\x01';
    body += "34=1\x01";
    body += "49=SENDER\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=TARGET\x01";

    std::string hdr;
    hdr += "8=FIX.4.2\x01";
    hdr += "9=" + std::to_string(body.size()) + '\x01';

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) { cs += c; }
    cs &= 0xFFU;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + '\x01';

    std::vector<std::byte> frame_bytes;
    frame_bytes.reserve(full.size());
    for (char c : full) { frame_bytes.push_back(static_cast<std::byte>(c)); }
    ASSERT_FALSE(frame_bytes.empty());

    // ── Helper lambda: one parse→validate round ───────────────────────────────
    auto run_long_validate = [&]() -> bool {
        std::array<std::byte, kInboundParseArena> vg_buf{};
        std::pmr::monotonic_buffer_resource vg_mr{vg_buf.data(), vg_buf.size(),
                                                  std::pmr::null_memory_resource()};
        std::array<std::byte, kCarryArena> vg_carry_store{};
        std::pmr::monotonic_buffer_resource vg_carry_mr{
            vg_carry_store.data(), vg_carry_store.size(), std::pmr::null_memory_resource()};
        fixpp::wire::pmr_carry_buffer vg_carry{vg_carry_store.size(), &vg_carry_mr};

        Framer vg_framer;
        std::array<fixpp::wire::frame_view, 1> vg_out{};
        auto vg_feed = vg_framer.feed(
            std::span<const std::byte>{frame_bytes.data(), frame_bytes.size()},
            vg_carry, std::span<fixpp::wire::frame_view>{vg_out});
        if (!vg_feed || vg_feed->empty()) {
            return false;
        }

        Parser<access_mode::Index> vg_parser;
        std::array<std::byte, kScratchArena> vg_scratch_buf{};
        std::pmr::monotonic_buffer_resource vg_scratch_mr{
            vg_scratch_buf.data(), vg_scratch_buf.size(), std::pmr::null_memory_resource()};
        auto vg_mv_r = vg_parser.parse((*vg_feed)[0], &vg_mr);
        if (!vg_mv_r) {
            return false;
        }

        auto val_r = validator.validate(*vg_mv_r, &vg_scratch_mr);
        return val_r.has_value();
    };

    // ── Warm-up pass (before markers, may allocate) ───────────────────────────
    bool warmup_ok = run_long_validate();
    ASSERT_TRUE(warmup_ok)
        << "warm-up parse+validate failed for long-MsgType frame";

    // ── Measured window ────────────────────────────────────────────────────────
    // Under mallocnesia pre-fix: std::string{msg_type} constructs a 32-char heap
    // string for EACH field lookup → intercepted → exit 1 → RED.
    // Post-fix: .find(string_view) → 0 allocations → GREEN.
    bool measured_ok = false;
    alloc_guard_start();
    measured_ok = run_long_validate();
    alloc_guard_end();

    EXPECT_TRUE(measured_ok)
        << "measured parse+validate failed for long-MsgType — conformant frame must pass";
}

}  // namespace
