// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/core/test_session_executor_accessor_survives_erasure.cpp
// [2d §9.21] Seam 21 — round-3 root cause #1 / I-11.
//
// session_executor is a value-typed class that IS an ASIO executor; the
// typed Session* travels with it and is recovered via the MEMBER FUNCTION
// session_ptr() — NOT asio::any_io_executor::query (the rejected v0.3 shape).
// This seam pins:
//   • compile-time: asio::execution::is_executor_v<session_executor>;
//   • survival of the typed Session* through copy, require/prefer (the
//     decoration co_spawn applies), any_io_executor erasure + target<>()
//     recovery, asio::make_strand wrapping, and bind_executor association;
//   • NEGATIVE: an any_io_executor erasing a NON-session executor exposes
//     NO Session* — recovery is ONLY the typed accessor, never a property
//     query (documented known-bad v0.3 design).
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/bind_executor.hpp>
#include <asio/associated_executor.hpp>
#include <asio/execution.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

namespace {

using fixpp::core::EngineConfig;
using fixpp::core::make_session_executor;
using fixpp::core::session_executor;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;

static_assert(asio::execution::is_executor_v<session_executor>,
              "seam 21: session_executor MUST satisfy the ASIO executor "
              "concept (round-3 RC#1)");

TEST(SeamSessionExecutorAccessorSurvivesErasure, TypedSessionPtrSurvivesDecoration) {
    asio::thread_pool pool{2};
    EngineConfig engine;
    engine.executor = pool.get_executor();
    SessionConfig cfg;
    Session sess{engine, cfg};

    auto made = make_session_executor(pool.get_executor(),
                                      threading_mode::per_session_strand,
                                      /*attested=*/false, &sess);
    ASSERT_TRUE(made.has_value());
    const session_executor se = *made;
    ASSERT_EQ(se.session_ptr(), &sess);

    // copy
    session_executor copy = se;
    EXPECT_EQ(copy.session_ptr(), &sess);

    // require / prefer RE-WRAP preserving the typed Session* (the exact
    // decoration co_spawn applies, e.g. outstanding_work.tracked). Scoped so
    // the work-tracker released by prefer(outstanding_work.tracked) does NOT
    // keep the pool alive across the final pool.join() below.
    {
        auto pref = se.prefer(asio::execution::outstanding_work.tracked);
        EXPECT_EQ(pref.session_ptr(), &sess);
        auto req = se.require(asio::execution::blocking.never);
        EXPECT_EQ(req.session_ptr(), &sess);
    }

    // any_io_executor erasure → recovered via the TYPED target<>(), the
    // round-3 RC#1 mechanism (NOT a query).
    asio::any_io_executor erased{se};
    const auto* recovered = erased.target<session_executor>();
    ASSERT_NE(recovered, nullptr);
    EXPECT_EQ(recovered->session_ptr(), &sess);

    // survives asio::make_strand wrapping (inner executor is session_executor)
    auto strand = asio::make_strand(se);
    EXPECT_EQ(strand.get_inner_executor().session_ptr(), &sess);

    // survives bind_executor association
    auto bound = asio::bind_executor(se, [] {});
    EXPECT_EQ(asio::get_associated_executor(bound).session_ptr(), &sess);

    pool.join();
}

// NEGATIVE: an any_io_executor erasing a plain (non-session) executor must
// NOT yield a session_executor — there is no Session* property channel; the
// only recovery path is the typed target<session_executor>() accessor.
TEST(SeamSessionExecutorAccessorSurvivesErasure, NonSessionExecutorExposesNoSessionPtr) {
    asio::thread_pool pool{1};
    asio::any_io_executor plain{pool.get_executor()};
    EXPECT_EQ(plain.target<session_executor>(), nullptr);   // no Session* leak
    pool.join();
}

}  // namespace
