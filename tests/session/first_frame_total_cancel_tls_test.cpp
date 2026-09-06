// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/first_frame_total_cancel_tls_test.cpp — T6 (SC-018 / FR-018),
// 088-firstframe-budget-timer-lifetime, Phase 4 (US2), tasks.md T023-T025.
//
// The ONLY cell pinning FR-018: `Engine::stop()`'s cancellation_type::total
// must abort an in-flight first-frame read on a REAL TLS transport. Without
// FR-018, stop() hangs FOREVER on the default TLS accept path.
//
// Why a mock cell cannot substitute (research.md D-6.10): mock_transport's
// read composes an asio::steady_timer wait, which honours `total` natively —
// so a mock-driven cell is green EXACTLY where a real asio::ssl::stream
// hangs. asio_tls_transport::async_read_some's SSL composed op is built with
// a one-argument reset_cancellation_state(enable_total_cancellation()) at
// src/transport/asio_tls_transport.cpp:1134 — the terminal-only IN filter is
// honoured by the SSL composed op's internal state, but there is no OUT
// filter mapping `total` back to `terminal`, so an emitted `total` is
// silently dropped and the read never aborts. FR-018 (task T029, NOT landed
// by this file) replaces the one-argument reset with a two-argument OUT-
// mapping form. This file's RED basis is the CURRENT (un-mapped) tree — see
// research.md D-6.13(b) and tasks.md T025: no revert is needed, because
// asio_tls_transport.cpp:1134 is already at the one-argument form.
//
// Two legs (research.md D-6.10a — a single joined leg cannot assert an exact
// error value, because BOTH the read arm and the deadline arm inside
// read_first_frame_bounded's internal timer are live and whichever the
// scheduler retires first decides the observable outcome):
//
//   Leg A — co_await read_first_frame_bounded(...) (the joined helper as
//           delivered). Binding: watchdog did not fire; call returned
//           promptly; outcome is CANCELLATION-ATTRIBUTABLE (one of
//           transport_read_cancelled / transport_handshake_timeout, and NOT
//           success / transport_already_closed / transport_read_eof).
//   Leg B — co_await transport.async_read_some(...) DIRECTLY, no join. Only
//           one operation is live, so the EXACT transport_read_cancelled is
//           assertable with no ordering premise at all.
//
// Positive initiation barrier (research.md D-6.13a — load-bearing, NOT
// decorative): an unset completion flag proves only "not finished", and is
// satisfied by a coroutine that never ran. Each leg's wrapper coroutine sets
// `entered_read` AFTER its cancellation-state reset and IMMEDIATELY BEFORE
// co_awaiting the subject; the test then poll()s until entered_read is true
// AND a following poll() returns with the coroutine still not completed —
// i.e. genuinely suspended inside the read, not merely queued. Stated
// limitation (research.md D-6.13a): this proves suspension at the awaited
// expression, not that the SSL composed op reached the socket layer;
// asio_tls_transport::read_in_flight_ is private with no accessor and this
// feature deliberately does not add one (an accessor for a witness is
// exactly the surface D-9 prices).
//
// Watchdog lifecycle (research.md D-6.13c): guards on !ec (an
// operation_aborted completion is the normal path — does nothing); is
// CANCELLED on the cell's normal completion; and the test drives the context
// until any fired watchdog handler has itself run, so no handler outlives
// this TU's locals — the same discipline FR-005 imposes on production.
//
// Anchors: spec.md FR-018/SC-018; research.md D-6.10/D-6.10a/D-6.12/D-6.13;
//          tasks.md T023-T025.

#include <gtest/gtest.h>

#include <array>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <chrono>
#include <cstddef>
#include <exception>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "session/read_first_frame_bounded.hpp"
#include "transport/loopback_tls_fixture.hpp"

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::detail::read_first_frame_bounded;
using fixpp::transport::TlsTransport;
using fixpp::transport::Transport;
using fixpp::transport::test::LoopbackTlsFixture;
using namespace std::chrono_literals;

std::string describe(expected_t<std::size_t> const& r) {
    if (!r.has_value())
        return std::string("error=") + std::string(fixpp::core::to_string(r.error()));
    return "value=" + std::to_string(*r);
}

// A cancellation-attributable outcome per D-6.10a's leg-A binding: one of the
// two errors a genuinely-cancelled read/deadline can surface, and explicitly
// none of the outcomes a socket-close (the watchdog's fallback) would produce.
bool is_cancellation_attributable(expected_t<std::size_t> const& r) {
    if (r.has_value()) return false;
    return r.error() == error::transport_read_cancelled ||
           r.error() == error::transport_handshake_timeout;
}

// ── Shared setup: real loopback mTLS pair, client goes silent post-handshake ─
//
// Client connects + completes the handshake, then sends nothing and does not
// close — the exact production state of a first-frame read against a stalled
// peer (research.md D-6.10 construction step 1). `client` is kept alive by
// the caller for the cell's duration (destroyed at end of TEST scope, well
// after the leg completes).
struct EstablishedPair {
    std::unique_ptr<Transport> client;
    std::unique_ptr<Transport> server;
};

void establish_pair(asio::io_context& ioc, LoopbackTlsFixture& fixture, EstablishedPair& pair) {
    pair.client = fixture.make_client(ioc.get_executor());
    bool client_ok = false;
    bool server_ok = false;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            auto cr = co_await pair.client->async_connect(fixture.server_endpoint());
            if (!cr.has_value()) co_return;
            auto* tls_client = dynamic_cast<TlsTransport*>(pair.client.get());
            if (!tls_client) co_return;
            auto hr = co_await tls_client->async_handshake(fixture.ssl_cfg());
            client_ok = hr.has_value();
            // Handshake done — go silent. No further reads/writes/close.
        },
        asio::detached);

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            auto ar = co_await fixture.listener().async_accept();
            if (!ar.has_value()) co_return;
            pair.server = std::move(*ar);
            auto* tls_server = dynamic_cast<TlsTransport*>(pair.server.get());
            if (!tls_server) co_return;
            auto hr = co_await tls_server->async_handshake(fixture.ssl_cfg());
            server_ok = hr.has_value();
        },
        asio::detached);

    ioc.run_for(5s);
    ioc.restart();

    ASSERT_TRUE(client_ok) << "setup: client connect+handshake did not complete";
    ASSERT_TRUE(server_ok) << "setup: server accept+handshake did not complete";
    ASSERT_TRUE(pair.server != nullptr) << "setup: server transport not established";
}

// Positive initiation barrier (D-6.13a): poll() until `entered_read` is set
// AND a following poll() leaves `result` still unset — suspended inside the
// read, not merely not-yet-run.
void confirm_suspended_in_read(asio::io_context& ioc, bool const& entered_read,
                               std::optional<expected_t<std::size_t>> const& result) {
    for (int i = 0; i < 10'000 && !entered_read; ++i) ioc.poll();
    ASSERT_TRUE(entered_read) << "positive barrier: wrapper coroutine never reached the "
                              << "co_await of the subject read — vacuous cell (D-6.13a).";
    ioc.poll();  // drain anything already ready; a genuine I/O wait leaves nothing to drain.
    ASSERT_FALSE(result.has_value()) << "positive barrier: the read completed before cancellation "
                                     << "was emitted (peer sent something, or completed "
                                     << "synchronously) — the barrier cannot certify suspension "
                                     << "inside the read.";
}

}  // namespace

// ── Leg A (joined helper) ─────────────────────────────────────────────────────
TEST(FirstFrameTotalCancelTls, LegA_JoinedHelper_CancellationAttributable) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    EstablishedPair pair;
    ASSERT_NO_FATAL_FAILURE(establish_pair(ioc, fixture, pair));

    // T6's outer wrapper — the frame production actually has (engine.cpp
    // run_accept_loop resets total-cancel one frame up); required per
    // research.md D-6.12(a)/C2, or the test's own co_spawn swallows `total`
    // before it ever reaches the subject and the watchdog fires on CORRECT
    // code.
    asio::cancellation_signal signal;
    bool entered_read = false;
    std::optional<expected_t<std::size_t>> result;
    std::exception_ptr thrown;
    std::vector<std::byte> buf;
    constexpr std::size_t kMaxBytes = 4096;
    // The internal helper deadline must not preempt the watchdog. #377 makes
    // that STRUCTURAL rather than a margin: the clock below is a frozen
    // mock_clock (never advanced), so the deadline cannot fire at all and
    // kDeadline's value is inert. It is kept only because the signature takes a
    // relative duration; do not read it as a live bound.
    constexpr auto kDeadline = 5000ms;
    fixpp::core::mock_clock clock{{}, {}, ioc.get_executor()};

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            entered_read = true;
            result = co_await read_first_frame_bounded(*pair.server, buf, clock, kDeadline, kMaxBytes);
        },
        asio::bind_cancellation_slot(signal.slot(), [&](std::exception_ptr ep) { thrown = ep; }));

    // Watchdog — converts the un-mapped build's hang into a bounded, captured
    // assertion failure (A2) instead of a ctest TIMEOUT kill.
    bool watchdog_fired = false;
    asio::steady_timer watchdog{ioc.get_executor()};
    watchdog.expires_after(1000ms);
    watchdog.async_wait([&](std::error_code ec) {
        if (!ec) {
            watchdog_fired = true;
            (void)pair.server->close();  // unblocks the hung read via socket close.
        }
    });

    ASSERT_NO_FATAL_FAILURE(confirm_suspended_in_read(ioc, entered_read, result));

    auto const t_emit = std::chrono::steady_clock::now();
    signal.emit(asio::cancellation_type::total);

    while (!result.has_value()) {
        ASSERT_GT(ioc.run_one(), 0u) << "io_context ran out of work before the read completed — "
                                     << "a broken cell (mis-wired watchdog/timers), not a RED "
                                     << "proof (D-6.13b).";
    }
    auto const elapsed = std::chrono::steady_clock::now() - t_emit;

    // Watchdog lifecycle (D-6.13c): cancel on normal completion (a no-op if
    // it already fired), then drain until its handler (aborted, or already
    // run) has retired — no handler may outlive this function's locals.
    watchdog.cancel();
    ioc.poll();

    ASSERT_FALSE(thrown) << "LegA: the wrapper coroutine threw.";

    // A2 — THE RED assertion. Under the un-mapped build, total dies inside
    // the SSL composed op's terminal-only state; the read never aborts; only
    // the watchdog's close() eventually unblocks it.
    EXPECT_FALSE(watchdog_fired)
        << "T6 leg A (SC-018/FR-018): watchdog fired — Engine::stop()'s cancellation_type::total "
        << "did not abort the in-flight TLS read within 1000ms. asio_tls_transport.cpp:1134 is "
        << "still the one-argument reset_cancellation_state(enable_total_cancellation()); the "
        << "SSL composed op's internal state is terminal-only, so an emitted `total` is silently "
        << "dropped and the read hangs until this test's watchdog force-closes the socket. "
        << "Fixed by T029 (a two-argument OUT-mapping reset). Elapsed emit-to-completion: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "ms.";
    if (watchdog_fired) return;  // A3-A/A4 are meaningless once the watchdog force-closed the read.

    // A3-A — cancellation-attributable, not the exact value (ordering
    // premise disclaimed per D-6.10a).
    EXPECT_TRUE(is_cancellation_attributable(*result))
        << "T6 leg A (SC-018): expected a cancellation-attributable outcome (transport_read_"
        << "cancelled or transport_handshake_timeout), got " << describe(*result);

    // A4 — promptness, normative (D-6.10's 10x watchdog / 50x deadline margins).
    EXPECT_LT(elapsed, 100ms)
        << "T6 leg A (SC-018/FR-015 on TLS): expected completion within "
        << "100ms of the total emit (got "
        << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        << "ms) — cancellation must be PROMPT, not merely eventual.";
}

// ── Leg B (raw async_read_some, no join) ──────────────────────────────────────
TEST(FirstFrameTotalCancelTls, LegB_DirectRead_ExactCancelled) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    EstablishedPair pair;
    ASSERT_NO_FATAL_FAILURE(establish_pair(ioc, fixture, pair));

    asio::cancellation_signal signal;
    bool entered_read = false;
    std::optional<expected_t<std::size_t>> result;
    std::exception_ptr thrown;
    std::array<std::byte, 256> read_buf{};

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            entered_read = true;
            result = co_await pair.server->async_read_some(std::span<std::byte>{read_buf});
        },
        asio::bind_cancellation_slot(signal.slot(), [&](std::exception_ptr ep) { thrown = ep; }));

    bool watchdog_fired = false;
    asio::steady_timer watchdog{ioc.get_executor()};
    watchdog.expires_after(1000ms);
    watchdog.async_wait([&](std::error_code ec) {
        if (!ec) {
            watchdog_fired = true;
            (void)pair.server->close();
        }
    });

    ASSERT_NO_FATAL_FAILURE(confirm_suspended_in_read(ioc, entered_read, result));

    auto const t_emit = std::chrono::steady_clock::now();
    signal.emit(asio::cancellation_type::total);

    while (!result.has_value()) {
        ASSERT_GT(ioc.run_one(), 0u) << "io_context ran out of work before the read completed — "
                                     << "a broken cell (mis-wired watchdog/timers), not a RED "
                                     << "proof (D-6.13b).";
    }
    auto const elapsed = std::chrono::steady_clock::now() - t_emit;

    watchdog.cancel();
    ioc.poll();

    ASSERT_FALSE(thrown) << "LegB: the wrapper coroutine threw.";

    // A2 — the RED assertion, same mechanism as leg A.
    EXPECT_FALSE(watchdog_fired)
        << "T6 leg B (SC-018/FR-018): watchdog fired — a single, un-joined "
        << "transport.async_read_some(...) was not aborted by cancellation_type::total within "
        << "1000ms. See leg A's message for the mechanism (asio_tls_transport.cpp:1134).";
    if (watchdog_fired) return;

    // A3-B — no join, no order[0] premise: the EXACT value is assertable.
    ASSERT_FALSE(result->has_value())
        << "T6 leg B (SC-018): expected a failure (transport_read_cancelled), got a successful "
        << "read of " << **result << " bytes.";
    EXPECT_EQ(result->error(), error::transport_read_cancelled)
        << "T6 leg B (SC-018): expected EXACTLY transport_read_cancelled (no join, no ordering "
        << "premise), got " << describe(*result);

    EXPECT_LT(elapsed, 100ms)
        << "T6 leg B (SC-018/FR-015 on TLS): expected completion within "
        << "100ms of the total emit (got "
        << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "ms).";
}
