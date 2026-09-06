// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_load_credentials_cancellation.cpp
// T028 — [2g §9 seam #13]: FR-021 ASIO cancellation seam.
//
// Binding contracts (per contracts/cert_source.hpp §6.4 recipe):
//   (1) Pre-I/O reap (step 3): cs.cancelled() != none → tls_load_cancelled
//       BEFORE step 4 runs. Driven deterministically by emitting on the
//       signal BEFORE the child coroutine starts, with the signal slot bound
//       to the child's own cancellation_state via the nested co_spawn token.
//   (2) Dispatch step (step 4): cancellation arriving during the dispatch
//       hop → tls_load_cancelled. Driven deterministically via a steady_timer
//       gate the test cancels after emitting the signal.
//   (3) No-cancel path returns valid credentials.
//   (4) Cancellation produces expected_t::unexpected, never a thrown exception.
//
// Per [[feedback_asio_cospawn_total_cancellation_default]]: co_spawn defaults
// to terminal-only. reset_cancellation_state(enable_total_cancellation()) at
// recipe step 1 enables total-cancellation delivery.

#include <gtest/gtest.h>

#include <asio/as_tuple.hpp>
#include <asio/bind_cancellation_slot.hpp>
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
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <string>
#include <variant>
#include <vector>

#include "support/pump_until_ready.hpp"

namespace {

// Path to the fixture directory (compiled-in via CMake definition
// FIXPP_TLS_FIXTURE_DIR — unioned onto every tests/tls target).
#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

std::string fixture(const char* name) { return std::string(FIXPP_TLS_FIXTURE_DIR) + "/" + name; }

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::tls::cert_source;
using fixpp::tls::Certificate;
using fixpp::tls::local_credentials;
using fixpp::tls::sign_response;
using fixpp::tls::software_key_ref;

// ── CancellableMockCertSource ─────────────────────────────────────────────────
// Implements the §6.4 recipe exactly. The optional `gate_` lets the test pin
// step 4 on a timer the test cancels after emitting the cancellation signal —
// removing the otherwise-racey "post emit + post yield" interleave.
class CancellableMockCertSource final : public cert_source {
public:
    std::atomic<bool> step3_reap_fired{false};
    std::atomic<bool> step4_reached{false};
    std::atomic<bool> step4_cancel_fired{false};

    asio::steady_timer* gate_ = nullptr;  // optional; if set, step 4 waits on it.

    [[nodiscard]] asio::awaitable<expected_t<local_credentials>> load_credentials() override {
        // #349: read the INHERITED state, reap, THEN reset -- mirroring the
        // production ordering in file_cert_source.cpp. The reset used to stand
        // above the read here too, which is exactly what made the reap
        // unfireable and produced this file's since-deleted claim that step 3
        // was "externally untestable".
        auto cs = co_await asio::this_coro::cancellation_state;

        // Step 3: pre-I/O reap (load-bearing).
        if (cs.cancelled() != asio::cancellation_type::none) {
            step3_reap_fired.store(true, std::memory_order_release);
            co_return expected_t<local_credentials>{std::unexpect, error::tls_load_cancelled};
        }

        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

        // Step 4: dispatch / build credentials.
        step4_reached.store(true, std::memory_order_release);

        if (gate_) {
            // Wait until the test cancels the gate. Swallow operation_aborted
            // via as_tuple so the coroutine resumes cleanly.
            co_await gate_->async_wait(asio::as_tuple(asio::use_awaitable));

            cs = co_await asio::this_coro::cancellation_state;
            if (cs.cancelled() != asio::cancellation_type::none) {
                step4_cancel_fired.store(true, std::memory_order_release);
                co_return expected_t<local_credentials>{std::unexpect, error::tls_load_cancelled};
            }
        }

        // Success.
        Certificate c{};
        local_credentials creds;
        creds.leaf = c;
        creds.chain = {};
        creds.signer = software_key_ref{};
        co_return expected_t<local_credentials>{std::move(creds)};
    }

    [[nodiscard]] expected_t<std::span<const Certificate>> load_trust_anchors()
        [[clang::lifetimebound]] override {
        return std::span<const Certificate>{};
    }
};

// Drive cs.load_credentials() directly via co_spawn so the bound cancellation
// slot becomes the child coroutine's OWN cancellation_state — NOT the outer
// future's. This is what lets a pre-emitted signal land on step 3.
static expected_t<local_credentials> spawn_and_run(asio::io_context& ioc,
                                                   CancellableMockCertSource& cs,
                                                   asio::cancellation_signal& signal) {
    auto fut = asio::co_spawn(ioc, cs.load_credentials(),
                              asio::bind_cancellation_slot(signal.slot(), asio::use_future));
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "spawn_and_run")) {
        return expected_t<local_credentials>{std::unexpect,
                                             fixpp::test_support::kWindowMissSentinel};
    }
    ioc.restart();
    return fut.get();
}

}  // namespace

// Drive load_credentials() AS A CHILD AWAIT of a parent coroutine that already
// carries an emitted `total` on its own slot.
//
// THIS IS THE SHAPE §6.4 ACTUALLY BINDS: its window is a CHILD AWAIT, which
// inherits the parent's cancellation_state. A co_spawn ROOT (see spawn_and_run
// below) cannot reach it — the entry state is terminal-only and an
// emit-before-spawn has no listener yet — so a harness built that way concludes
// the reap is unreachable, which is what this file used to record.
static expected_t<local_credentials> await_as_child_with_pending_cancel(
    asio::io_context& ioc, cert_source& src, asio::cancellation_signal& signal) {
    auto fut = asio::co_spawn(
        ioc,
        [&src, &signal]() -> asio::awaitable<expected_t<local_credentials>> {
            // Parent admits total, so the state the child inherits can carry it.
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            // ⚠️ WITHOUT THIS LINE THE CHILD BODY NEVER RUNS, and step 3 cannot
            // reap anything however it is ordered. asio's awaitable
            // await_transform (awaitable_frame_base, guarded by
            // `throw_if_cancelled_`, which DEFAULTS TO TRUE) throws
            // operation_aborted when a cancellation is already pending — so a
            // default caller gets a thrown error rather than the
            // tls_load_cancelled §6.4 names, and never enters the child.
            // Re-derive by deleting this line: the cells below then fail with a
            // thrown "co_await: Operation aborted" instead of an error value.
            //
            // §6.4's reap therefore has an UNSTATED PRECONDITION on the caller,
            // and NO production caller meets it (#351). These cells pin the
            // mechanism under the condition where the reap is what answers; they
            // are NOT a witness that any shipped caller gets tls_load_cancelled.
            co_await asio::this_coro::throw_if_cancelled(false);
            // Emit on the parent's OWN slot while the parent is running: the
            // handler is installed, nothing is suspended, so this records the
            // emission in the state the child is about to inherit.
            signal.emit(asio::cancellation_type::total);
            co_return co_await src.load_credentials();
        },
        asio::bind_cancellation_slot(signal.slot(), asio::use_future));
    ioc.run();
    ioc.restart();
    return fut.get();
}

// ── Step 3: pre-I/O reap — MEASURED, not asserted by code shape ──────────────
// ⚠️ RED-ARM CONTRACT: this cell fails on a tree whose load_credentials resets
// the cancellation state before reading it. Verified by reverting the recipe
// order in the mock: the child then observes `none`, step 3 stays silent, and
// the call returns credentials instead of tls_load_cancelled. Keep the
// assertion on the RETURNED ERROR (not on a flag alone) so the cell means the
// same thing against both orderings.
TEST(LoadCredentialsCancellation, Step3ReapsCancellationInheritedFromParent) {
    CancellableMockCertSource cs;
    asio::io_context ioc;
    asio::cancellation_signal signal;

    auto result = await_as_child_with_pending_cancel(ioc, cs, signal);

    ASSERT_FALSE(result.has_value())
        << "a total emitted before the call must be reaped at step 3 per §6.4";
    EXPECT_EQ(result.error(), error::tls_load_cancelled);
    EXPECT_TRUE(cs.step3_reap_fired.load()) << "step 3 is the step that must reap it";
    EXPECT_FALSE(cs.step4_reached.load()) << "§6.4 requires the reap to precede any I/O work";
}

// ── The PRODUCTION path, not just the recipe ────────────────────────────────
// The mock above witnesses the recipe shape. This cell witnesses
// fixpp::tls::file_cert_source itself, which is the object §6.4 names and the
// one #349 reported. Without it the mock only proves the test file agrees with
// itself.
TEST(LoadCredentialsCancellation, FileCertSourceStep3ReapsInheritedCancellation) {
    fixpp::tls::file_cert_source::Config cfg;
    cfg.leaf_path = fixture("leaf_rsa2048.pem");
    cfg.chain_path = fixture("chain_depth_8.pem");
    cfg.private_key_path = fixture("leaf_rsa2048.key");
    cfg.ca_bundle_path = fixture("ca.pem");

    auto made = fixpp::tls::file_cert_source::make_file_cert_source(cfg, nullptr);
    ASSERT_TRUE(made.has_value()) << "fixture credentials must load; without them this cell "
                                     "would pass for the wrong reason";
    ASSERT_NE(*made, nullptr);

    asio::io_context ioc;
    asio::cancellation_signal signal;
    auto result = await_as_child_with_pending_cancel(ioc, **made, signal);

    ASSERT_FALSE(result.has_value())
        << "file_cert_source must reap a cancellation emitted before the call (§6.4)";
    EXPECT_EQ(result.error(), error::tls_load_cancelled);
}

// ── No-cancel path: step 4 reached, step 3 silent, credentials returned ──────
TEST(LoadCredentialsCancellation, NoCancelReachesStep4ReturnsCredentials) {
    CancellableMockCertSource cs;
    asio::io_context ioc;
    asio::cancellation_signal signal;

    auto result = spawn_and_run(ioc, cs, signal);

    ASSERT_TRUE(result.has_value()) << "no-cancel path must return credentials";
    EXPECT_TRUE(cs.step4_reached.load()) << "step 4 must be reached when no cancellation";
    EXPECT_FALSE(cs.step3_reap_fired.load()) << "step 3 must NOT fire when no cancellation";
}

// ── Step 4: in-flight cancellation fires via gate-cancel after signal.emit ───
TEST(LoadCredentialsCancellation, Step4InFlightCancelFiresDeterministic) {
    CancellableMockCertSource cs;
    asio::io_context ioc;
    asio::cancellation_signal signal;

    // Gate the mock's step 4 on a long-expiry timer. The test will emit the
    // cancellation signal then cancel the timer — together this guarantees
    // the cancellation arrives DURING step 4 (not before, not after).
    asio::steady_timer gate{ioc};
    gate.expires_after(std::chrono::hours{1});
    cs.gate_ = &gate;

    auto fut = asio::co_spawn(ioc, cs.load_credentials(),
                              asio::bind_cancellation_slot(signal.slot(), asio::use_future));

    // Post a driver onto ioc that emits the signal and cancels the gate AFTER
    // the coroutine has reached step 4's gate-wait. We sequence by posting the
    // emit-and-cancel onto ioc; asio runs the coroutine up to its first
    // suspension before the post fires.
    asio::post(ioc, [&]() {
        signal.emit(asio::cancellation_type::total);
        gate.cancel();
    });

    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fut, "LoadCredentialsCancellation::Step4InFlightCancelFiresDeterministic")) {
        return;
    }
    auto result = fut.get();

    ASSERT_FALSE(result.has_value()) << "in-flight cancellation must produce tls_load_cancelled";
    EXPECT_EQ(result.error(), error::tls_load_cancelled);
    EXPECT_TRUE(cs.step4_reached.load()) << "step 4 must have been reached before in-flight cancel";
    EXPECT_TRUE(cs.step4_cancel_fired.load()) << "step 4 cancel probe must fire deterministically";
    EXPECT_FALSE(cs.step3_reap_fired.load())
        << "step 3 must NOT fire when cancellation arrives at step 4";
}

// ── Cancellation surfaces as expected_t::unexpected, never as a thrown ex ────
// Drives the step-4 deterministic cancellation path (gate timer) and confirms
// neither arbitrary exceptions escape nor non-cancelled results sneak through.
TEST(LoadCredentialsCancellation, CancelledResultIsExpectedNotException) {
    CancellableMockCertSource cs;
    asio::io_context ioc;
    asio::cancellation_signal signal;

    asio::steady_timer gate{ioc};
    gate.expires_after(std::chrono::hours{1});
    cs.gate_ = &gate;

    auto fut = asio::co_spawn(ioc, cs.load_credentials(),
                              asio::bind_cancellation_slot(signal.slot(), asio::use_future));

    asio::post(ioc, [&]() {
        signal.emit(asio::cancellation_type::total);
        gate.cancel();
    });

    bool threw_non_system = false;
    expected_t<local_credentials> result{std::unexpect, error::tls_load_cancelled};
    try {
        if (!fixpp::test_support::run_to_exhaustion_or_report(
                ioc, fut, "LoadCredentialsCancellation::CancelledResultIsExpectedNotException")) {
            return;
        }
        result = fut.get();
    } catch (const std::system_error&) {
        // asio::operation_aborted as a system_error is acceptable.
    } catch (...) {
        threw_non_system = true;
    }

    EXPECT_FALSE(threw_non_system) << "cancellation must not throw arbitrary exceptions";
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::tls_load_cancelled);
}

// ── Direct: file_cert_source factory + constructor no-throw boundary ─────────
TEST(LoadCredentialsCancellation, FileCertSourceLoadCredentialsNoThrow) {
    // The constructor MAY throw std::runtime_error for a missing file per
    // [arch §5.3] construction-time carve-out; anything else is a contract
    // violation.
    bool threw_non_runtime = false;
    try {
        fixpp::tls::file_cert_source::Config cfg;
        cfg.leaf_path = "/nonexistent/path.pem";
        fixpp::tls::file_cert_source fcs_instance{cfg};
    } catch (const std::runtime_error&) {
        // expected
    } catch (...) {
        threw_non_runtime = true;
    }
    EXPECT_FALSE(threw_non_runtime)
        << "constructor should only throw std::runtime_error for missing file";
}
