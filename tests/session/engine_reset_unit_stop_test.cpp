// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/engine_reset_unit_stop_test.cpp
//
// 093-inbound-frame-dispositions — Q-26 (tasks.md T082; contract C-6; data-model E-13;
// spec FR-041, SC-006): Engine::stop() begins while a 141=Y reset unit's store operation
// is in flight.
//
// A plaintext Engine on a mock clock with a raw peer (plain_engine_rig.hpp), in each role.
// The peer's Logon (acceptor) or Logon-ack (initiator) carries ResetSeqNumFlag(141)=Y,
// so the arm runs its reset unit. The unit's store operation is held until stop()'s
// step 1 has run on the session's strand (the session's engine-stop flag, read through
// session_test_access), so stop's step-1 handler runs first:
//   - MemoryStore: a forward-mode HookedStore (tests/support/hooked_store.hpp), whose
//     reset_to is the inner MemoryStore's one-step reset_to, then the hold;
//   - FileStore: its file-I/O executor is an io_context the cell stops running once the
//     unit is in flight, so the unit's reset_to waits inside the store, with its writer
//     lock taken, until the flag is set;
//   - a default-body HookedStore: reset_to is the store's own reset() and
//     next_seqnum(), and the hold is in the first of them.
// Each runs with and without a teardown reset (reset_on_disconnect).
//
// Asserted after stop() completes:
//   - the durable counters meet contract C-6's table: the unit's targets (next-in 2,
//     the Logon being consumed; next-out 1, the session sending no 141=Y) without a
//     teardown reset, (1, 1) with one;
//   - per role, nothing the arm does after the unit happened: no admin frame reached
//     toAdmin after the unit began, no session_event_sequence_numbers_reset is in the
//     event ring, onLogon never fired and no Active is in the state ring.
//
// Registered standalone (an Engine, timers and coroutines, not a pure bucket member).
#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/session/application.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "plain_engine_rig.hpp"
#include "support/hooked_store.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"

namespace fixpp::session::test {
namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
using fixpp::test_support::HookedStoreFactory;
using fixpp::test_support::reset_to_mode;
using fixpp::test_support::StoreLog;

// Counts what the application sees.
class StopEffectsApp final : public Application {
public:
    int to_admin = 0;
    int on_logon = 0;

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const SessionId& /*sid*/) override {
        ++to_admin;
    }
    void onLogon(const SessionId& /*sid*/) override { ++on_logon; }
};

enum class StoreKind : std::uint8_t { memory, file, default_body };

struct Q26Param {
    StoreKind store;
    bool teardown;
    session_role role;
};

std::string param_name(::testing::TestParamInfo<Q26Param> const& info) {
    auto const& p = info.param;
    std::string const store = p.store == StoreKind::memory ? "MemoryStore"
                              : p.store == StoreKind::file ? "FileStore"
                                                           : "DefaultBody";
    return std::string{p.role == session_role::acceptor ? "Acceptor" : "Initiator"} + "_" +
           store + (p.teardown ? "_TeardownReset" : "_NoTeardownReset");
}

class Q26 : public ::testing::TestWithParam<Q26Param> {};

TEST_P(Q26, EngineStopDuringTheUnitMeetsTheTableAndTheArmActsOnNothingMore) {
    auto const& p = GetParam();
    auto const app = std::make_shared<StopEffectsApp>();
    plain_rig::Rig rig{app};
    asio::io_context fio;  // the FileStore's file-I/O executor
    auto const dir = fixpp::test_support::unique_temp_dir("q26");

    // The cell's session, once published; the flags are read through it.
    std::shared_ptr<Session> sess;
    auto session = [&]() -> Session* {
        if (!sess) sess = rig.session();
        return sess.get();
    };
    auto stop_step1_ran = [&] {
        auto* s = session();
        return s != nullptr && session_test_access::engine_stop_requested(*s);
    };

    auto c = rig.cfg(p.role);
    c.reset_on_disconnect = p.teardown;
    std::shared_ptr<StoreLog> log;
    bool hooked_op_began = false;
    FileStore::Config file_cfg;
    if (p.store == StoreKind::file) {
        file_cfg.directory = dir;
        file_cfg.sender_comp_id = "ISLD";
        file_cfg.target_comp_id = "TW";
        file_cfg.max_frame_bytes = 4096;
        file_cfg.file_io_executor = fio.get_executor();
        c.store_factory = std::make_shared<FileStoreFactory>(file_cfg);
    } else {
        auto factory = std::make_shared<HookedStoreFactory>();
        factory->mode =
            p.store == StoreKind::memory ? reset_to_mode::forward : reset_to_mode::default_body;
        factory->hooks.on_reset = [&hooked_op_began] { hooked_op_began = true; };
        factory->hooks.release_when = stop_step1_ran;
        log = factory->log;
        c.store_factory = std::move(factory);
    }
    auto unit_in_flight = [&] {
        if (p.store != StoreKind::file) return hooked_op_began;
        auto* s = session();
        return s != nullptr && session_test_access::reset_unit_in_flight(*s);
    };

    ASSERT_TRUE(rig.start(c));
    // Runs the engine, and the FileStore's executor while `run_fio` holds, until `ready`.
    auto run_until = [&](auto ready, auto run_fio) {
        auto const deadline = std::chrono::steady_clock::now() + fixpp::test_support::kPumpBudget;
        while (!ready()) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            if (run_fio()) {
                fio.restart();
                (void)fio.poll();
            }
            rig.ioc.run_for(fixpp::test_support::kPumpSlice);
            rig.ioc.restart();
        }
        return true;
    };
    auto fio_until_unit = [&] { return !unit_in_flight(); };

    std::string const logon_141 = "98=0\x01" "108=30\x01" "141=Y\x01";
    if (p.role == session_role::acceptor) {
        ASSERT_TRUE(rig.connect_peer());
    } else {
        ASSERT_TRUE(run_until(
            [&] {
                return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
                       rig.state() == fsm_state::LogonSent;
            },
            [] { return true; }))
            << "the initiator's Logon";
    }
    rig.peer.send(rig.msg("A", 1, logon_141));
    ASSERT_TRUE(run_until(unit_in_flight, fio_until_unit)) << "the unit's store operation";
    ASSERT_NE(session(), nullptr);
    int const to_admin_at_unit = app->to_admin;

    auto stop_fut = asio::co_spawn(rig.ioc, rig.engine->stop(), asio::use_future);
    bool const stopped = run_until(
        [&] { return stop_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
        [&] { return p.store != StoreKind::file || stop_step1_ran(); });
    ASSERT_TRUE(stopped) << "Engine::stop() did not complete";
    stop_fut.get();

    // Nothing the arm does after the unit.
    EXPECT_EQ(app->to_admin, to_admin_at_unit) << "an admin frame reached toAdmin after the unit";
    EXPECT_EQ(app->on_logon, 0) << "onLogon fired";
    auto const ring = sess->fsm_visit_history();
    EXPECT_EQ(std::count(ring.begin(), ring.end(), fsm_state::Active), 0)
        << "the session reached Active";
    EXPECT_FALSE(std::ranges::any_of(sess->recent_events(), [](auto const& ev) {
        return std::holds_alternative<session_event_sequence_numbers_reset>(ev);
    })) << "the arm emitted its reset event";

    // The table.
    std::optional<std::pair<seqnum_t, seqnum_t>> durable;
    if (p.store == StoreKind::file) {
        sess.reset();
        FileStoreFactory factory{file_cfg};
        auto minted = factory.make("ISLD", "TW", nullptr, 1024 * 1024 * 1024, fio.get_executor());
        ASSERT_TRUE(minted.has_value()) << "restart over the store directory";
        auto read = asio::co_spawn(
            rig.ioc,
            [&]() -> asio::awaitable<std::pair<seqnum_t, seqnum_t>> {
                auto in = co_await (*minted)->next_seqnum(direction_t::inbound, false);
                auto out = co_await (*minted)->next_seqnum(direction_t::outbound, false);
                co_return std::pair{in.value_or(0), out.value_or(0)};
            },
            asio::use_future);
        ASSERT_TRUE(run_until(
            [&] { return read.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
            [] { return true; }));
        durable = read.get();
    } else {
        ASSERT_TRUE(log && log->inner);
        auto read = asio::co_spawn(
            rig.ioc,
            [&]() -> asio::awaitable<std::pair<seqnum_t, seqnum_t>> {
                auto in = co_await log->inner->next_seqnum(direction_t::inbound, false);
                auto out = co_await log->inner->next_seqnum(direction_t::outbound, false);
                co_return std::pair{in.value_or(0), out.value_or(0)};
            },
            asio::use_future);
        ASSERT_TRUE(run_until(
            [&] { return read.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
            [] { return true; }));
        durable = read.get();
        EXPECT_FALSE(log->hold_timed_out)
            << "the unit's store operation was released by its bound, not by stop()'s step 1";
    }
    auto const want = p.teardown ? std::pair{seqnum_min, seqnum_min}
                                 : std::pair{seqnum_t{seqnum_min + 1}, seqnum_min};
    EXPECT_EQ(durable, std::optional{want})
        << "durable (next-in, next-out); want the unit's targets without a teardown reset, "
           "(1, 1) with one";

    (void)fixpp::test_support::try_remove_temp_dir(dir);
}

std::vector<Q26Param> q26_params() {
    std::vector<Q26Param> out;
    for (auto const role : {session_role::acceptor, session_role::initiator}) {
        for (auto const store : {StoreKind::memory, StoreKind::file, StoreKind::default_body}) {
            for (bool const teardown : {false, true}) {
                out.push_back({store, teardown, role});
            }
        }
    }
    return out;
}

INSTANTIATE_TEST_SUITE_P(EngineResetUnitStop, Q26, ::testing::ValuesIn(q26_params()), param_name);

}  // namespace
}  // namespace fixpp::session::test
