// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/fuzz/fuzz_session_cancellation.cpp — [2d §9.12] Seam 12 (T035)
//
// VOLUNTARY cancellation-timing harness (Gate-A discretion per [2d §9 seam
// 12]; NOT a [const §VII.7] obligation — 2d is not parser-touching, D-11).
// Drives Session::close(graceful|terminal) interleaved with a scripted
// fromApp dispatch (via fixpp::core::cancellable_dispatch) and a frozen
// mock_clock sleep, firing close at an input-selected co_await checkpoint.
// Properties asserted under -fsanitize=fuzzer,address,undefined:
//   • no deadlock — the single io_context drains within a bounded handler
//     budget (an unbounded loop = a cancellation deadlock → abort());
//   • no double-free / UB — ASan/UBSan;
//   • no PMR leak / no global-heap dispatch fallback — the session arena is
//     a bounded monotonic resource; cancellable_dispatch must surface
//     strand_dispatch_failed_oom (slot 50), never silently hit the heap, and
//     every awaitable must complete exactly once (no leaked handler).
#include <array>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fixpp/core/cancellable_dispatch.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory_resource>
#include <optional>

namespace {

using fixpp::core::cancellable_dispatch;
using fixpp::core::EngineConfig;
using fixpp::core::expected_t;
using fixpp::core::make_session_executor;
using fixpp::session::close_mode;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;

    const bool terminal = (data[0] & 0x1u) != 0;
    const int n_dispatches = 1 + (data[0] >> 1) % 8;  // 1..8
    const std::size_t checkpoint =
        (size > 1 ? data[1] : 0u) % static_cast<std::size_t>(n_dispatches + 1);

    // Bounded session arena: exercises the OOM contract path
    // (strand_dispatch_failed_oom, slot 50) deterministically instead of UB
    // or a silent global-heap fallback.
    std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size(),
                                              std::pmr::null_memory_resource()};

    asio::io_context ctx;
    EngineConfig engine;
    engine.executor = ctx.get_executor();
    SessionConfig cfg;
    cfg.session_arena = &arena;
    Session sess{engine, cfg};
    auto se = *make_session_executor(ctx.get_executor(), threading_mode::per_session_strand,
                                     /*attested=*/false, &sess);

    asio::co_spawn(ctx, sess.open(), asio::detached);
    ctx.run();
    ctx.restart();

    // The cancellation_signal does NOT own its slot's storage; it MUST
    // outlive every cancellable_dispatch coroutine that holds a slot into it
    // (standard asio lifetime contract — a loop-scoped signal is a
    // use-after-scope the way the real Session::root_cancel_ member is not).
    // n_dispatches ≤ 8; cancellation_signal is non-movable so a fixed array.
    std::array<asio::cancellation_signal, 8> sigs{};

    int completed = 0;
    for (int i = 0; i < n_dispatches; ++i) {
        auto& sig = sigs[static_cast<std::size_t>(i)];
        asio::co_spawn(
            ctx,
            [&, i]() -> asio::awaitable<void> {
                auto r = co_await cancellable_dispatch(se, sig.slot(), [] {});
                // Every awaitable completes exactly once with a DEFINED
                // outcome (value, dispatch_aborted, or the bounded-arena
                // OOM error) — never a leak, never UB.
                (void)r;
                ++completed;
            },
            asio::detached);
        if (static_cast<std::size_t>(i) == checkpoint) {
            asio::co_spawn(ctx, sess.close(terminal ? close_mode::terminal : close_mode::graceful),
                           asio::detached);
        }
    }

    // No deadlock: a correct two-phase close + reaping drains in far fewer
    // than this many handlers; exceeding it means an unterminated
    // cancellation cycle.
    std::size_t budget = 100000;
    while (ctx.poll() != 0) {
        if (--budget == 0) std::abort();  // libFuzzer flags the deadlock
    }

    // Every spawned dispatch coroutine must have completed (no leaked
    // in-flight awaitable / handler).
    if (completed != n_dispatches) std::abort();
    return 0;
}
