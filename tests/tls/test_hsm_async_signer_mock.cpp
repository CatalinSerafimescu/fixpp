// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/tls/test_hsm_async_signer_mock.cpp
// T027 — [2g §9 seam #8]: HSM-style async_signer_ref mock.
//
// Binding contract: signing work routes OFF the session strand via a
// cancellable_dispatch hop. The test verifies that the sign callback executes
// on a DIFFERENT thread from the one that initiated the call.
//
// Per [[feedback_asio_post_resume_bounces_to_spawn_executor]]:
//   co_await asio::post(other_exec, use_awaitable) does NOT pin the coroutine
//   RESUME to other_exec — only the completion handler runs there. To pin the
//   sign body to a different executor and sample std::this_thread::get_id()
//   correctly, we use nested asio::co_spawn(other_exec, fn, use_awaitable)
//   and put the sampling INSIDE fn. This test witnesses that pattern.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <memory_resource>
#include <thread>
#include <vector>

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::tls::async_signer_ref;
using fixpp::tls::sign_request;
using fixpp::tls::sign_response;

// ── HsmSignerRoutesOffSessionStrand ──────────────────────────────────────────
// An async_signer_ref whose sign function posts the actual work to a
// SEPARATE thread pool executor. The test verifies that the sign body
// executes on a different thread from the spawning (session) thread.
//
// Implementation note: the sign_fn is an awaitable coroutine that is
// spawned onto `hsm_pool`'s executor and samples the executing thread id
// INSIDE the coroutine body (pinned by co_spawn, not by co_await asio::post).
TEST(HsmAsyncSignerMock, SignCallbackRunsOnDifferentExecutor) {
    asio::io_context session_ioc;
    asio::thread_pool hsm_pool{1};  // separate "HSM" executor

    const std::thread::id session_thread_id = std::this_thread::get_id();
    std::thread::id sign_thread_id{};  // captured inside sign callback

    // Build the async_signer_ref with a sign_fn that:
    // (a) Spawns a coroutine onto hsm_pool (simulating off-strand dispatch).
    // (b) Samples the executing thread id from INSIDE the spawned coroutine.
    async_signer_ref signer;
    signer.sign = [&](sign_request const& req) -> asio::awaitable<expected_t<sign_response>> {
        // Use co_spawn to pin the body to hsm_pool (per anti-pattern note).
        // The sign_fn is already running as an awaitable on session_ioc;
        // to route to hsm_pool, we co_await a nested co_spawn.
        co_await asio::co_spawn(
            hsm_pool.get_executor(),
            [&sign_thread_id]() -> asio::awaitable<void> {
                // Inside hsm_pool executor — sample thread id here.
                sign_thread_id = std::this_thread::get_id();
                co_return;
            },
            asio::use_awaitable);

        // Build a trivial signature response (PMR-default allocator for test).
        sign_response resp{};
        resp.signature = std::pmr::vector<std::byte>(req.tbs.begin(), req.tbs.end());
        co_return expected_t<sign_response>{std::move(resp)};
    };

    // Drive the sign call from session_ioc.
    std::vector<std::byte> tbs_data = {std::byte{0xDE}, std::byte{0xAD}};
    sign_request req{std::span{tbs_data}, 0 /* NID placeholder */};

    auto future = asio::co_spawn(
        session_ioc,
        [&]() -> asio::awaitable<expected_t<sign_response>> {
            co_return co_await signer.sign(req);
        },
        asio::use_future);

    session_ioc.run();

    auto result = future.get();
    ASSERT_TRUE(result.has_value()) << "sign must succeed";

    // Primary contract: sign body ran on a DIFFERENT thread than session.
    EXPECT_NE(sign_thread_id, session_thread_id)
        << "HSM sign callback must execute on a different thread from the "
           "session strand (off-strand dispatch contract per [2g §6.4])";

    // Secondary: signature bytes match the tbs input (mock identity).
    ASSERT_EQ(result->signature.size(), tbs_data.size());
    for (std::size_t i = 0; i < tbs_data.size(); ++i) {
        EXPECT_EQ(result->signature[i], tbs_data[i]);
    }

    hsm_pool.join();
}

// ── HsmSignerVariantIsWellFormed ──────────────────────────────────────────────
// Sanity: async_signer_ref can be stored in the signer variant and extracted.
TEST(HsmAsyncSignerMock, SignerVariantStorageRoundTrip) {
    using fixpp::tls::Certificate;
    using fixpp::tls::local_credentials;
    using fixpp::tls::software_key_ref;

    async_signer_ref signer;
    signer.sign = [](sign_request const&) -> asio::awaitable<expected_t<sign_response>> {
        sign_response r{};
        co_return expected_t<sign_response>{std::move(r)};
    };

    // Place in variant, extract, confirm sign is non-null.
    std::variant<software_key_ref, async_signer_ref> v = std::move(signer);
    ASSERT_TRUE(std::holds_alternative<async_signer_ref>(v));
    EXPECT_TRUE(std::get<async_signer_ref>(v).sign)
        << "sign_fn must be non-null after variant round-trip";
}

}  // namespace
