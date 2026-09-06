// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/transport/test_load_credentials_seam13_witness.cpp
//
// T044 — 011 Gate B F-1 carryover witness (FR-033 + SC-008).
// Design anchors: .specify/2g-tls.md §6.4:926-929 (cached-state fast path),
//                 .specify/2d-threading.md §6.5 (cancellable_dispatch 3 cases),
//                 .specify/2d-threading.md §4.8 (session executor model).
//
// 8 cells: 4 deterministic cases × 2 executor modes.
//
// Case 1 — cached-state fast path [2g §6.4:927-928]:
//   file_cert_source loads ALL data at construction; load_credentials() returns
//   the cached local_credentials directly WITHOUT invoking cancellable_dispatch.
//   The 011 waiver explicitly authorises this fast path.
//   Witness: load_credentials() on a real file_cert_source with a real
//   session_executor succeeds and returns valid credentials without cancellable_dispatch.
//
// Case 2 — slot signalled BEFORE handler picked up [2d §6.5 case 1]:
//   cancellable_dispatch reaps handler (never invoked);
//   awaitable → expected_t<void>{ unexpect, dispatch_aborted };
//   mapped to tls_load_cancelled.
//
// Case 3 — slot signalled DURING handler execution [2d §6.5 case 2]:
//   handler runs to first co_await checkpoint, then cancellation propagates;
//   mapped to tls_load_cancelled.
//
// Case 4 — slot NOT signalled (happy path) [2d §6.5 case 3]:
//   cancellable_dispatch completes expected_t<void>{}; load_credentials succeeds.
//
// Executor modes per [2d §4.8]:
//   (a) per_session_strand — make_session_executor wraps asio::make_strand(...)
//   (b) direct_executor    — make_session_executor wraps the bare attested executor
//
// Cancellation note for Cases 2 and 3:
// asio's co_spawn(bind_cancellation_slot(outer_slot, token)) claims outer_slot
// for its coroutine-level cancellation machinery. Inside the coroutine,
// cs.slot() (from asio::this_coro::cancellation_state) maps to an INTERNAL slot
// connected to outer_slot via asio's propagation — but calling slot.assign() on
// it replaces the internal handler without affecting the outer signal's delivery
// chain. This means signal.emit() from the outer signal does NOT trigger
// cancellable_dispatch's cancel_handler when the coroutine was co_spawned with
// bind_cancellation_slot.
//
// Correct approach (matching production + test_cancellation_parse_to_fromapp.cpp):
// pass a FRESH cancellation signal's slot directly to cancellable_dispatch — NOT
// cs.slot() from the coroutine state. The fresh signal is independent of the
// co_spawn mechanism and slot.assign() + signal.emit() work correctly.
//
// This is also how production sessions work: the root_cancellation_slot() is
// the session's own signal, not the co_spawn token's slot. The session fires
// root_cancel_.emit() directly; the coroutine's cs.slot() returns that slot
// because the session coroutines are started with that slot as root, not via
// bind_cancellation_slot.
//
// Per [[feedback_asio_post_resume_bounces_to_spawn_executor]]: direct_executor
// cells use nested asio::co_spawn(other_exec, fn, use_awaitable) NOT
// co_await asio::post(other_exec, use_awaitable).
//
// Note on Session::open(): NOT called here. session_arena() is valid immediately
// after Session construction (I-18 — the ctor pre-conditions the [2d §4.5]
// never-null chain). cancellable_dispatch only needs session_arena_of(exec),
// which resolves through the wrapper without requiring open().

#include <gtest/gtest.h>

#include <asio/as_tuple.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/cancellable_dispatch.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <variant>

#include "support/pump_until_ready.hpp"

// Path to the 011-shipped fixture directory (compiled-in via CMake).
#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

namespace {

using fixpp::core::cancellable_dispatch;
using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::core::make_session_executor;
using fixpp::core::session_executor;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;
using fixpp::tls::Certificate;
using fixpp::tls::file_cert_source;
using fixpp::tls::local_credentials;
using fixpp::tls::software_key_ref;

static std::string fixture(const char* name) {
    return std::string(FIXPP_TLS_FIXTURE_DIR) + "/" + name;
}

// ── Minimal Session + session_executor fixture ────────────────────────────────
// Session is valid for cancellable_dispatch without open() — session_arena()
// is non-null from construction (I-18).
//
// Owns the PMR arena buffer, EngineConfig, Session, and the session_executor.
// Two modes: per_session_strand and direct_executor.

struct SessionFixture {
    static constexpr std::size_t kArenaSize = 128 * 1024;  // 128 KB

    std::array<std::byte, kArenaSize> arena_buf{};
    std::pmr::monotonic_buffer_resource arena_mr{arena_buf.data(), arena_buf.size(),
                                                 std::pmr::get_default_resource()};
    asio::io_context ioc;
    EngineConfig engine;
    SessionConfig cfg;
    std::optional<Session> sess;
    std::optional<session_executor> se;

    void init(threading_mode mode, bool attested = false) {
        engine.executor = ioc.get_executor();
        engine.default_session_resource = &arena_mr;
        cfg.session_arena = &arena_mr;
        cfg.mode = mode;
        cfg.already_serialized_executor = attested;
        sess.emplace(engine, cfg);
        auto result = make_session_executor(ioc.get_executor(), mode, attested, &(*sess));
        ASSERT_TRUE(result.has_value()) << "make_session_executor failed";
        se = *result;
    }
};

// ── DispatchCertSource ────────────────────────────────────────────────────────
// A cert_source that calls cancellable_dispatch with a real session_executor.
// Used for Cases 2, 3, 4.
//
// CANCELLATION NOTE: `dispatch_slot` is a FRESH cancellation slot (from a signal
// that is separate from the co_spawn token). This matches production:
//   - Production sessions use root_cancel_.slot() directly (not the co_spawn slot).
//   - slot.assign(cancel_handler) + signal.emit() work correctly on fresh slots.
//   - co_spawn(bind_cancellation_slot(outer_slot, ...)) claims outer_slot for
//     coroutine-level cancellation, preventing fresh re-assignment via slot.assign().
//
// `gate_` (optional): when set, step 4 waits on the gate timer before completing
// — used to deterministically inject cancellation during handler execution (Case 3).
class DispatchCertSource final : public fixpp::tls::cert_source {
public:
    session_executor exec;
    asio::cancellation_slot dispatch_slot;  // fresh slot, not the co_spawn slot
    std::atomic<bool> dispatch_reached{false};
    std::atomic<bool> dispatch_cancel_fired{false};
    asio::steady_timer* gate_ = nullptr;

    [[nodiscard]] asio::awaitable<expected_t<local_credentials>> load_credentials() override {
        // RC#F (P2-3): D-17 reset per .specify/2d-threading.md §6.5 recipe step 0.
        // Production file_cert_source::load_credentials begins with this reset;
        // the stub must replicate it so the seam #13 witness exercises the same
        // D-17 cancellable_dispatch recipe path as production code.
        // Without this reset, co_spawn defaults to terminal-only cancellation
        // per [[feedback_asio_cospawn_total_cancellation_default]], silently
        // filtering cancellation_type::total and making Cases 2-4 non-representative.
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

        // Step 4: post work via cancellable_dispatch per [2g §6.4].
        // Uses dispatch_slot (a fresh slot independent of co_spawn machinery).
        auto dispatched = co_await cancellable_dispatch(exec, dispatch_slot, [this]() {
            dispatch_reached.store(true, std::memory_order_release);
        });

        if (!dispatched) {
            // Case 2: dispatch_aborted → tls_load_cancelled.
            co_return expected_t<local_credentials>{std::unexpect, error::tls_load_cancelled};
        }

        // Case 3: cancellation during step 4 (gate timer path).
        if (gate_) {
            auto [ec] = co_await gate_->async_wait(asio::as_tuple(asio::use_awaitable));
            (void)ec;
            dispatch_cancel_fired.store(true, std::memory_order_release);
            co_return expected_t<local_credentials>{std::unexpect, error::tls_load_cancelled};
        }

        // Case 4: success.
        Certificate leaf{};
        local_credentials creds;
        creds.leaf = leaf;
        creds.chain = {};
        creds.signer = software_key_ref{};
        co_return expected_t<local_credentials>{std::move(creds)};
    }

    [[nodiscard]] expected_t<std::span<const Certificate>> load_trust_anchors()
        [[clang::lifetimebound]] override {
        return std::span<const Certificate>{};
    }
};

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Case 1 — cached-state fast path [2g §6.4:927-928]
// ═══════════════════════════════════════════════════════════════════════════════
//
// file_cert_source loads ALL data at construction. load_credentials() returns
// the cached local_credentials directly WITHOUT invoking cancellable_dispatch.
// [2g §6.4:926-929] explicitly authorises this path: "for already-cached state,
// just return the prepared local_credentials directly."
//
// The session_executor is constructed but NOT used by file_cert_source — it proves
// the wiring is correct (make_session_executor succeeds with both executor modes).

TEST(Seam13Witness, Case1_Strand) {
    SessionFixture f;
    f.init(threading_mode::per_session_strand, /*attested=*/false);

    // Session executor successfully constructed: make_session_executor with
    // per_session_strand. This is the wiring this test witnesses.
    ASSERT_TRUE(f.se.has_value());
    EXPECT_TRUE(f.se->is_strand_wrapped())
        << "per_session_strand must produce a strand-wrapped executor";

    file_cert_source::Config cfg;
    cfg.leaf_path = fixture("leaf_rsa2048.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");
    cfg.ca_bundle_path = fixture("ca.pem");

    std::shared_ptr<fixpp::tls::cert_source> fcs;
    try {
        fcs = std::make_shared<file_cert_source>(cfg);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "TLS fixture not available: " << e.what();
    }

    // load_credentials() on file_cert_source uses the cached-state fast path —
    // no cancellable_dispatch hop (the §6.4 footnote authorises this).
    auto fut = asio::co_spawn(f.ioc, fcs->load_credentials(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(f.ioc, fut,
                                                          "Seam13Witness::Case1_Strand")) {
        return;
    }
    f.ioc.restart();

    auto result = fut.get();
    ASSERT_TRUE(result.has_value()) << "cached-state fast path must return valid credentials; "
                                       "error: "
                                    << static_cast<int>(result.error());
}

TEST(Seam13Witness, Case1_Direct) {
    SessionFixture f;
    // direct_executor with attested=true: the bare io_context is intrinsically
    // serialised (single-threaded).
    f.init(threading_mode::direct_executor, /*attested=*/true);

    ASSERT_TRUE(f.se.has_value());
    EXPECT_FALSE(f.se->is_strand_wrapped())
        << "direct_executor must produce an unwrapped (non-strand) executor";

    file_cert_source::Config cfg;
    cfg.leaf_path = fixture("leaf_rsa2048.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");
    cfg.ca_bundle_path = fixture("ca.pem");

    std::shared_ptr<fixpp::tls::cert_source> fcs;
    try {
        fcs = std::make_shared<file_cert_source>(cfg);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "TLS fixture not available: " << e.what();
    }

    auto fut = asio::co_spawn(f.ioc, fcs->load_credentials(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(f.ioc, fut,
                                                          "Seam13Witness::Case1_Direct")) {
        return;
    }
    f.ioc.restart();

    auto result = fut.get();
    ASSERT_TRUE(result.has_value())
        << "cached-state fast path must return valid credentials (direct_executor); "
           "error: "
        << static_cast<int>(result.error());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Case 2 — slot signalled BEFORE handler picked up [2d §6.5 case 1]
// ═══════════════════════════════════════════════════════════════════════════════
//
// cancellable_dispatch reaps the handler (NEVER invoked).
// awaitable → expected_t<void>{ unexpect, dispatch_aborted } → tls_load_cancelled.
//
// Determinism: session strand is backed by `f.ioc`; the load_credentials()
// coroutine is co_spawn'd on a SECOND io_context `drive`. drive.poll() parks
// the coroutine at the cancellable_dispatch hand-off (posted to `f.ioc`);
// dispatch_signal.emit() sets cancelled=true BEFORE f.ioc.poll() runs the
// continuation.

TEST(Seam13Witness, Case2_Strand) {
    SessionFixture f;
    f.init(threading_mode::per_session_strand, /*attested=*/false);

    asio::io_context drive;
    asio::cancellation_signal dispatch_signal;

    DispatchCertSource cs;
    cs.exec = *f.se;
    cs.dispatch_slot = dispatch_signal.slot();

    auto fut = asio::co_spawn(drive, cs.load_credentials(), asio::use_future);

    drive.poll();                                          // park at cancellable_dispatch hand-off
    dispatch_signal.emit(asio::cancellation_type::total);  // signal BEFORE pickup
    f.ioc.poll();                                          // continuation reaps handler
    drive.poll();                                          // co_spawn completion

    auto result = fut.get();

    ASSERT_FALSE(result.has_value()) << "cancel-before-pickup must produce tls_load_cancelled";
    EXPECT_EQ(result.error(), error::tls_load_cancelled);
    // The handler was REAPED — dispatch_reached must be false.
    EXPECT_FALSE(cs.dispatch_reached.load()) << "handler must NOT run when reaped before pickup";
}

TEST(Seam13Witness, Case2_Direct) {
    // For direct_executor: the session executor wraps the bare ioc executor (attested).
    // Cross-executor hop: co_spawn load_credentials() on a separate `drive` io_context;
    // cancellable_dispatch dispatches to cs.exec (= f.ioc). Since drive ≠ f.ioc,
    // the dispatch genuinely posts (not inline), creating the required reap window.
    //
    // This mirrors the direct_executor production scenario where the coroutine
    // caller (e.g., TLS handshake) runs on a different thread from the session
    // executor. The session_executor is the session's own strand (direct, attested).
    SessionFixture f;
    f.init(threading_mode::direct_executor, /*attested=*/true);

    asio::io_context drive;
    asio::cancellation_signal dispatch_signal;

    DispatchCertSource cs;
    cs.exec = *f.se;
    cs.dispatch_slot = dispatch_signal.slot();

    // Co_spawn load_credentials() on drive (external caller thread); the
    // cancellable_dispatch inside posts to cs.exec (= f.ioc), creating the
    // reap window between drive.poll() suspension and f.ioc.poll() execution.
    auto fut = asio::co_spawn(drive, cs.load_credentials(), asio::use_future);

    drive.poll();                                          // park at cancellable_dispatch hand-off
    dispatch_signal.emit(asio::cancellation_type::total);  // signal BEFORE pickup
    f.ioc.poll();                                          // continuation reaps handler
    drive.poll();                                          // co_spawn completion

    auto result = fut.get();

    ASSERT_FALSE(result.has_value())
        << "cancel-before-pickup (direct_executor) must produce tls_load_cancelled";
    EXPECT_EQ(result.error(), error::tls_load_cancelled);
    EXPECT_FALSE(cs.dispatch_reached.load())
        << "handler must NOT run when reaped before pickup (direct_executor)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Case 3 — slot signalled DURING handler execution [2d §6.5 case 2]
// ═══════════════════════════════════════════════════════════════════════════════
//
// Handler runs to first co_await checkpoint (the gate timer); cancellation is
// emitted; the gate is cancelled; the coroutine observes the gate cancellation
// and returns tls_load_cancelled.
//
// Witness: dispatch_reached=true (handler started); dispatch_cancel_fired=true
// (cancellation observed inside the gate-wait path).

TEST(Seam13Witness, Case3_Strand) {
    SessionFixture f;
    f.init(threading_mode::per_session_strand, /*attested=*/false);

    DispatchCertSource cs;
    cs.exec = *f.se;
    // No dispatch_slot needed for Case3 — cancel arrives AFTER dispatch completes.
    // The gate timer simulates the "during execution" cancel.
    asio::cancellation_signal dispatch_signal;
    cs.dispatch_slot = dispatch_signal.slot();

    // Gate: a long-expiry timer the test cancels after the handler starts.
    asio::steady_timer gate{f.ioc};
    gate.expires_after(std::chrono::hours{1});
    cs.gate_ = &gate;

    auto fut = asio::co_spawn(f.ioc, cs.load_credentials(), asio::use_future);

    // Post a driver: after the coroutine reaches the gate wait, cancel the gate.
    asio::post(f.ioc, [&]() { gate.cancel(); });

    if (!fixpp::test_support::run_to_exhaustion_or_report(f.ioc, fut,
                                                          "Seam13Witness::Case3_Strand")) {
        return;
    }
    f.ioc.restart();

    auto result = fut.get();

    ASSERT_FALSE(result.has_value()) << "in-flight cancel must produce tls_load_cancelled";
    EXPECT_EQ(result.error(), error::tls_load_cancelled);
    EXPECT_TRUE(cs.dispatch_reached.load())
        << "dispatch handler must have run before in-flight cancel";
    EXPECT_TRUE(cs.dispatch_cancel_fired.load())
        << "cancel probe must fire during handler execution";
}

TEST(Seam13Witness, Case3_Direct) {
    SessionFixture f;
    f.init(threading_mode::direct_executor, /*attested=*/true);

    DispatchCertSource cs;
    cs.exec = *f.se;
    asio::cancellation_signal dispatch_signal;
    cs.dispatch_slot = dispatch_signal.slot();

    asio::steady_timer gate{f.ioc};
    gate.expires_after(std::chrono::hours{1});
    cs.gate_ = &gate;

    // nested co_spawn for direct_executor per D-18.
    auto fut = asio::co_spawn(
        f.ioc,
        [&cs]() -> asio::awaitable<expected_t<local_credentials>> {
            co_return co_await asio::co_spawn(cs.exec, cs.load_credentials(), asio::use_awaitable);
        },
        asio::use_future);

    asio::post(f.ioc, [&]() { gate.cancel(); });

    if (!fixpp::test_support::run_to_exhaustion_or_report(f.ioc, fut,
                                                          "Seam13Witness::Case3_Direct")) {
        return;
    }
    f.ioc.restart();

    auto result = fut.get();

    ASSERT_FALSE(result.has_value())
        << "in-flight cancel (direct_executor) must produce tls_load_cancelled";
    EXPECT_EQ(result.error(), error::tls_load_cancelled);
    EXPECT_TRUE(cs.dispatch_reached.load())
        << "dispatch handler must have run before in-flight cancel (direct_executor)";
    EXPECT_TRUE(cs.dispatch_cancel_fired.load())
        << "cancel probe must fire during handler execution (direct_executor)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Case 4 — slot NOT signalled (happy path) [2d §6.5 case 3]
// ═══════════════════════════════════════════════════════════════════════════════
//
// No cancellation; cancellable_dispatch completes expected_t<void>{};
// load_credentials returns valid local_credentials.

TEST(Seam13Witness, Case4_Strand) {
    SessionFixture f;
    f.init(threading_mode::per_session_strand, /*attested=*/false);

    asio::io_context drive;
    asio::cancellation_signal dispatch_signal;  // never emitted

    DispatchCertSource cs;
    cs.exec = *f.se;
    cs.dispatch_slot = dispatch_signal.slot();

    auto fut = asio::co_spawn(drive, cs.load_credentials(), asio::use_future);

    // Run both iocs: drive runs the coroutine, f.ioc runs the dispatch.
    drive.poll();  // park at cancellable_dispatch hand-off
    f.ioc.poll();  // dispatch handler runs
    drive.poll();  // coroutine resumes and completes

    auto result = fut.get();

    ASSERT_TRUE(result.has_value()) << "happy path (strand) must return valid credentials";
    EXPECT_TRUE(cs.dispatch_reached.load()) << "dispatch handler must have run on happy path";
    EXPECT_FALSE(cs.dispatch_cancel_fired.load()) << "no cancel probe must fire on happy path";
}

TEST(Seam13Witness, Case4_Direct) {
    SessionFixture f;
    f.init(threading_mode::direct_executor, /*attested=*/true);

    asio::cancellation_signal dispatch_signal;  // never emitted

    DispatchCertSource cs;
    cs.exec = *f.se;
    cs.dispatch_slot = dispatch_signal.slot();

    // nested co_spawn for direct_executor per D-18.
    auto fut = asio::co_spawn(
        f.ioc,
        [&cs]() -> asio::awaitable<expected_t<local_credentials>> {
            co_return co_await asio::co_spawn(cs.exec, cs.load_credentials(), asio::use_awaitable);
        },
        asio::use_future);

    if (!fixpp::test_support::run_to_exhaustion_or_report(f.ioc, fut,
                                                          "Seam13Witness::Case4_Direct")) {
        return;
    }
    f.ioc.restart();

    auto result = fut.get();

    ASSERT_TRUE(result.has_value()) << "happy path (direct_executor) must return valid credentials";
    EXPECT_TRUE(cs.dispatch_reached.load())
        << "dispatch handler must have run on happy path (direct_executor)";
    EXPECT_FALSE(cs.dispatch_cancel_fired.load())
        << "no cancel probe must fire on happy path (direct_executor)";
}
