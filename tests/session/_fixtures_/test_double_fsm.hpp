// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/_fixtures_/test_double_fsm.hpp
//
// Deterministic scripted test-double FSM for 008-message-store seams.
// Research D-4 / Clarifications Q1 / FR-034.
//
// SCOPE GUARD: This drives a FIXED, deterministic (seqnum, direction,
// frame_bytes) sequence and asserts ONLY 2e-owned properties:
//   - byte equality after round-trip
//   - toApp → Writer::commit → store → async_write ordering
//   - awaitable-visitor span lifetime across co_await
//   - cancellation completion shape per method
//   - FileStore atomicity / durability under SIGKILL
//   - flush_for_session_close() invocation timing
//
// It NEVER asserts FIX FSM correctness (Logon flow, ResendRequest,
// SequenceReset-GapFill — those are 005's concern). The FIX field names
// below are SCRIPT LABELS, not real protocol output.
//
// Reused by US1 / US2 / US3 / US4 seams that need a store driver.
#pragma once

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace fixpp::store_test {

// ── Script step ─────────────────────────────────────────────────────────────

// A single scripted store operation with the expected call and direction.
struct store_step {
    fixpp::session::seqnum_t seq{};
    fixpp::session::direction_t dir{};
    std::vector<std::byte> frame_bytes;  // the raw FIX frame bytes (opaque)
};

// Build a synthetic FIX-like frame for testing.
// The bytes are: "8=FIX.4.4\x01" + "35=D\x01" + "34=<N>\x01" + padding.
// The content is OPAQUE to the store; we just need byte-identical round-trips.
[[nodiscard]] inline std::vector<std::byte> make_test_frame(fixpp::session::seqnum_t seq,
                                                            fixpp::session::direction_t dir,
                                                            std::size_t extra_bytes = 0) {
    std::string raw;
    raw += "8=FIX.4.4\x01";
    raw += "35=D\x01";
    raw += "34=";
    raw += std::to_string(static_cast<unsigned>(seq));
    raw += "\x01";
    raw += "49=";
    raw += (dir == fixpp::session::direction_t::outbound ? "SENDER" : "TARGET");
    raw += "\x01";
    raw += "10=123\x01";

    // Add extra_bytes of padding (tests that need large frames)
    for (std::size_t i = 0; i < extra_bytes; ++i) {
        raw += static_cast<char>(static_cast<std::uint8_t>(i & 0xFF));
    }

    std::vector<std::byte> result(raw.size());
    std::transform(raw.begin(), raw.end(), result.begin(),
                   [](char c) { return static_cast<std::byte>(c); });
    return result;
}

// Build a default N-step store script: seqnums 1..N for a given direction.
[[nodiscard]] inline std::vector<store_step> make_store_script(std::size_t count,
                                                               fixpp::session::direction_t dir,
                                                               std::size_t extra_bytes = 0) {
    std::vector<store_step> steps;
    steps.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto seq = static_cast<fixpp::session::seqnum_t>(i + 1);
        steps.push_back({seq, dir, make_test_frame(seq, dir, extra_bytes)});
    }
    return steps;
}

// ── Byte-collecting visitor ──────────────────────────────────────────────────

// Collects all frames seen during retrieve() into a vector of vectors.
// Thread-safe for single-coroutine use (no concurrency needed for US1 seams).
class byte_collecting_visitor final : public fixpp::session::retrieve_visitor {
public:
    struct entry {
        fixpp::session::seqnum_t seq{};
        std::vector<std::byte> bytes;
    };

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        fixpp::session::seqnum_t seq,
        std::span<const std::byte> frame [[clang::lifetimebound]]) noexcept override {
        // Deep-copy the frame bytes immediately (span lifetime bound)
        entries_.push_back({seq, std::vector<std::byte>(frame.begin(), frame.end())});
        co_return fixpp::core::expected_t<fixpp::session::visit_result>{
            fixpp::session::visit_result::cont};
    }

    [[nodiscard]] const std::vector<entry>& entries() const noexcept { return entries_; }

    void clear() noexcept { entries_.clear(); }

private:
    std::vector<entry> entries_;
};

// ── FSM driver coroutine ─────────────────────────────────────────────────────

// Drives a script of store() calls on the given store, then performs a
// retrieve() and asserts byte equality. Returns true on success.
//
// This is intentionally synchronous-like (runs on a single coroutine chain)
// to keep US1 deterministic. Writer-mutex wiring is US3's concern; US1 bodies
// are single-threaded-correct.
[[nodiscard]] inline asio::awaitable<bool> drive_store_and_retrieve(
    fixpp::session::MessageStore& store, const std::vector<store_step>& script,
    fixpp::session::direction_t dir) {
    // Phase 1: store all frames in order
    for (const auto& step : script) {
        auto result =
            co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes), step.dir);
        if (!result) {
            co_return false;  // store failed
        }
    }

    // Phase 2: retrieve all frames and check byte equality
    byte_collecting_visitor visitor;
    auto rresult = co_await store.retrieve(1,  // begin = 1
                                           0,  // end = 0 (to-end sentinel)
                                           dir, visitor);
    if (!rresult) {
        co_return false;  // retrieve failed
    }

    // Check count
    if (visitor.entries().size() != script.size()) {
        co_return false;
    }

    // Check byte equality in seqnum order
    for (std::size_t i = 0; i < script.size(); ++i) {
        const auto& expected = script[i].frame_bytes;
        const auto& got = visitor.entries()[i].bytes;
        if (expected.size() != got.size()) {
            co_return false;
        }
        for (std::size_t j = 0; j < expected.size(); ++j) {
            if (expected[j] != got[j]) {
                co_return false;
            }
        }
    }

    co_return true;
}

// Run a coroutine on a thread_pool and get the result.
template <class Coro>
auto run_on_pool(asio::thread_pool& pool, Coro coro) {
    return asio::co_spawn(pool.get_executor(), std::move(coro), asio::use_future).get();
}

}  // namespace fixpp::store_test
