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
//     next_seqnum(), and the hold is in the first of them;
//   - plan OD-25's two contended stores, whose reset_to waits on a lock another holder
//     has when stop() begins: a contended-mode HookedStore, whose lock the cell holds
//     until the flag is set, and (initiator only) a FileStore whose writer lock a store()
//     the cell issued holds, its file I/O parked until the flag is set. The acceptor arm
//     reads the store before its unit, so a FileStore competitor issued before the Logon
//     would hold that read instead.
// Each runs with and without a teardown reset (reset_on_disconnect).
//
// Which rows test that no cancellation reaches the unit's store operation: only the
// contended rows without a teardown reset. Elsewhere the store operation waits on no
// lock another holder has, so stop()'s emission finds nothing it can cancel, and a
// teardown reset ends at (1, 1) whatever the unit did. Those rows test the table and the
// engine-stop flag.
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
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/sync/async_mutex.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>

#include "plain_engine_rig.hpp"
#include "support/hooked_store.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"
#include "support/validation_test_dictionary.hpp"

namespace fixpp::session::test {
namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
using fixpp::test_support::HookedStoreFactory;
using fixpp::test_support::kHoldBound;
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

enum class StoreKind : std::uint8_t { memory, file, default_body, contended, file_held };

struct Q26Param {
    StoreKind store;
    bool teardown;
    session_role role;
};

std::string param_name(::testing::TestParamInfo<Q26Param> const& info) {
    auto const& p = info.param;
    std::string const store = p.store == StoreKind::memory         ? "MemoryStore"
                              : p.store == StoreKind::file         ? "FileStore"
                              : p.store == StoreKind::default_body ? "DefaultBody"
                              : p.store == StoreKind::contended    ? "ContendedLock"
                                                                   : "FileStoreWriterLockHeld";
    return std::string{p.role == session_role::acceptor ? "Acceptor" : "Initiator"} + "_" + store +
           (p.teardown ? "_TeardownReset" : "_NoTeardownReset");
}

class Q26 : public ::testing::TestWithParam<Q26Param> {};

TEST_P(Q26, EngineStopDuringTheUnitMeetsTheTableAndTheArmActsOnNothingMore) {
    auto const& p = GetParam();
    bool const file_backed = p.store == StoreKind::file || p.store == StoreKind::file_held;
    // The contended row's lock; declared before the rig, so it outlives the store.
    fixpp::sync::async_mutex reset_lock;
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
    if (file_backed) {
        file_cfg.directory = dir;
        file_cfg.sender_comp_id = "ISLD";
        file_cfg.target_comp_id = "TW";
        file_cfg.max_frame_bytes = 4096;
        file_cfg.file_io_executor = fio.get_executor();
        c.store_factory = std::make_shared<FileStoreFactory>(file_cfg);
    } else {
        auto factory = std::make_shared<HookedStoreFactory>();
        factory->mode = p.store == StoreKind::memory         ? reset_to_mode::forward
                        : p.store == StoreKind::default_body ? reset_to_mode::default_body
                                                             : reset_to_mode::contended;
        factory->hooks.on_reset = [&hooked_op_began] { hooked_op_began = true; };
        factory->hooks.release_when = stop_step1_ran;
        factory->hooks.reset_to_lock = &reset_lock;
        log = factory->log;
        c.store_factory = std::move(factory);
    }
    auto unit_in_flight = [&] {
        if (!file_backed) return hooked_op_began;
        auto* s = session();
        return s != nullptr && session_test_access::reset_unit_in_flight(*s);
    };

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
    auto fio_until_unit = [&] { return p.store != StoreKind::file_held && !unit_in_flight(); };

    // The FileStore row's competing store(), issued below.
    bool competitor_began = false;
    bool competitor_done = false;
    // A setup step missed after start() created the engine: fails the cell, then stops the
    // engine on this cell's stop path (the FileStore's executor runs once step 1 has), so
    // ~Engine's stopped() precondition holds and the binary's later cells still run. A
    // competing store() is let finish before its store is destroyed.
    auto fail_and_stop = [&](std::string_view what) {
        ADD_FAILURE() << what;
        auto fut = asio::co_spawn(rig.ioc, rig.engine->stop(), asio::use_future);
        if (!run_until(
                [&] { return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
                [&] { return !file_backed || stop_step1_ran(); })) {
            fixpp::test_support::cancel_and_drain_or_report(rig.ioc, *rig.clock,
                                                            "Q26::fail_and_stop");
            return;
        }
        fut.get();
        if (!run_until([&] { return !competitor_began || competitor_done; }, [] { return true; })) {
            ADD_FAILURE() << "the competing store()";
        }
    };

    if (!rig.start(c)) {
        fail_and_stop("the rig did not start");
        return;
    }

    // The contended row: the cell holds the lock until stop()'s step 1 has run.
    bool lock_held = false;
    bool lock_released = false;
    bool holder_timed_out = false;
    if (p.store == StoreKind::contended) {
        asio::co_spawn(
            rig.ioc,
            [&]() -> asio::awaitable<void> {
                auto guard = co_await reset_lock.async_lock();
                if (!guard) co_return;
                lock_held = true;
                auto ex = co_await asio::this_coro::executor;
                auto const deadline = std::chrono::steady_clock::now() + kHoldBound;
                while (!stop_step1_ran()) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        holder_timed_out = true;
                        break;
                    }
                    co_await asio::post(ex, asio::use_awaitable);
                }
                lock_released = true;
            },
            asio::detached);
        if (!run_until([&] { return lock_held; }, [] { return false; })) {
            fail_and_stop("the cell's lock");
            return;
        }
    }

    std::string const logon_141 =
        "98=0\x01"
        "108=30\x01"
        "141=Y\x01";
    if (p.role == session_role::acceptor) {
        if (!rig.connect_peer()) {
            fail_and_stop("rig.connect_peer()");
            return;
        }
    } else if (!run_until(
                   [&] {
                       return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
                              rig.state() == fsm_state::LogonSent;
                   },
                   [] { return true; })) {
        fail_and_stop("the initiator's Logon");
        return;
    }
    // The FileStore row: a store() the cell issues on the session's strand takes the
    // writer lock and parks in its file I/O, which does not run until the flag is set.
    if (p.store == StoreKind::file_held) {
        auto* s = session();
        if (s == nullptr) {
            fail_and_stop("session() is null");
            return;
        }
        auto* store = session_test_access::store(*s);
        if (store == nullptr) {
            fail_and_stop("the session's store is null");
            return;
        }
        std::string const competing = rig.msg("0", 2);
        asio::co_spawn(
            s->executor().underlying(),
            [&, store, competing]() -> asio::awaitable<void> {
                competitor_began = true;
                auto const bytes = plain_rig::to_bytes(competing);
                (void)co_await store->store(2, std::span<const std::byte>{bytes},
                                            direction_t::outbound);
                competitor_done = true;
            },
            asio::detached);
        rig.settle();
        if (!competitor_began) {
            fail_and_stop("competitor_began");
            return;
        }
        if (competitor_done) {
            fail_and_stop("the competing store() did not park in its file I/O");
            return;
        }
    }
    rig.peer.send(rig.msg("A", 1, logon_141));
    if (!run_until(unit_in_flight, fio_until_unit)) {
        fail_and_stop("the unit's store operation");
        return;
    }
    if (session() == nullptr) {
        fail_and_stop("session() is null");
        return;
    }
    int const to_admin_at_unit = app->to_admin;

    auto stop_fut = asio::co_spawn(rig.ioc, rig.engine->stop(), asio::use_future);
    bool const stopped = run_until(
        [&] { return stop_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
        [&] { return !file_backed || stop_step1_ran(); });
    if (!stopped) {
        fixpp::test_support::cancel_and_drain_or_report(rig.ioc, *rig.clock, "Q26::stop");
        ADD_FAILURE() << "Engine::stop() did not complete";
        return;
    }
    stop_fut.get();
    if (p.store == StoreKind::file_held) {
        // The competing store() must finish before its store is destroyed.
        ASSERT_TRUE(run_until([&] { return competitor_done; }, [] { return true; }))
            << "the competing store()";
    }

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
    EXPECT_FALSE(holder_timed_out) << "the cell's lock was released by its bound";
    if (p.store == StoreKind::contended) EXPECT_TRUE(lock_released);
    std::optional<std::pair<seqnum_t, seqnum_t>> durable;
    if (file_backed) {
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
        for (auto const store : {StoreKind::memory, StoreKind::file, StoreKind::default_body,
                                 StoreKind::contended, StoreKind::file_held}) {
            if (store == StoreKind::file_held && role == session_role::acceptor) continue;
            for (bool const teardown : {false, true}) {
                out.push_back({.store = store, .teardown = teardown, .role = role});
            }
        }
    }
    return out;
}

INSTANTIATE_TEST_SUITE_P(EngineResetUnitStop, Q26, ::testing::ValuesIn(q26_params()), param_name);

// ── 093 plan OD-25: the 789 path and the reply Logon ─────────────────────────────
//
// A Logon arm tests logon_arm_superseded after every suspension before its next effect,
// including the suspensions inside honor_peer_next_expected_ and replay_outbound_range_
// and the store await inside the reply Logon's store_then_emit (contract C-6's
// erratum). Each cell below holds one such suspension in a HookedStore, and posts
// close(graceful) or runs Engine::stop() while it is held; the operation is released
// once the close has begun, or once stop()'s step 1 has run.
//
// close(graceful) is the close used: in LogonReceived its phase 1 writes its own Logout
// and waits for the peer's, so the socket is still open when the held operation resumes,
// and a frame written after the close began reaches the peer's capture. Each close cell
// has a control with no close, in which the same frame or event does appear.

// Records toAdmin's MsgTypes, and those passed after `close_began` was set.
class Od25App final : public Application {
public:
    std::vector<std::string> to_admin_all;
    std::vector<std::string> to_admin_after_close;
    bool close_began = false;

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const SessionId& /*sid*/) override {
        to_admin_all.emplace_back(msg.msg_type());
        if (close_began) to_admin_after_close.emplace_back(msg.msg_type());
    }
};

// An application frame from the session ("ISLD") at `seq`, as the store would hold it.
std::string stored_app_frame(plain_rig::Rig const& rig, seqnum_t seq, std::string_view extra) {
    return plain_rig::message(rig.begin_string, "D", seq, "ISLD", "TW", rig.sending_time(), extra);
}

// A MemoryStore with `frames` stored outbound, in order from 1. MemoryStore's store()
// takes only its next number and advances the counter past it.
std::shared_ptr<MemoryStore> seeded_store(std::vector<std::pair<seqnum_t, std::string>> frames) {
    auto inner =
        std::make_shared<MemoryStore>(MemoryStore::Config{.policy = capacity_policy::unbounded});
    asio::io_context seed;
    asio::co_spawn(
        seed,
        [&]() -> asio::awaitable<void> {
            for (auto const& [seq, text] : frames) {
                auto const bytes = plain_rig::to_bytes(text);
                (void)co_await inner->store(seq, std::span<const std::byte>{bytes},
                                            direction_t::outbound);
            }
        },
        asio::detached);
    seed.run();
    return inner;
}

struct Od25Case {
    std::string logon_extra;  // fields after 98 and 108 on the peer's Logon
    std::vector<std::pair<seqnum_t, std::string>> frames;  // stored outbound before open
    // Hold the n-th outbound store() (1-based; 0 = none), or the first outbound
    // next_seqnum(_, false) after the reply Logon's store.
    int hold_store = 0;
    bool hold_replay_read = false;
    bool close = true;  // false: the control, no hold and no close
};

struct Od25Outcome {
    bool settled = false;
    bool close_returned = false;
    bool hold_timed_out = false;
    std::string wire;
    std::vector<fsm_state> ring;
    std::size_t ring_at_close = 0;
    bool gap_filled_event = false;
    std::vector<std::string> to_admin_all;
    std::vector<std::string> to_admin_after_close;
};

// The acceptor: the peer logs on with `c.logon_extra`, 789 enabled; the session is
// settled once the close returned (or, in the control, once it is Active or
// Disconnected), then a short window lets anything queued behind it run.
Od25Outcome run_od25_acceptor(Od25Case const& c) {
    Od25Outcome out;
    auto const app = std::make_shared<Od25App>();
    plain_rig::Rig rig{app};
    auto cfg = rig.cfg(session_role::acceptor);
    cfg.enable_next_expected_msg_seq_num = true;

    std::shared_ptr<Session> held;
    bool close_returned = false;
    std::size_t ring_at_close = 0;
    auto post_close = [&] {
        held = rig.session();
        if (!held) return;
        asio::any_io_executor ex = held->executor().underlying();
        asio::post(ex, [&, ex] {
            asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    app->close_began = true;
                    ring_at_close = held->fsm_visit_history().size();
                    (void)co_await held->close(close_mode::graceful);
                    close_returned = true;
                },
                asio::detached);
        });
    };

    auto factory = std::make_shared<HookedStoreFactory>();
    if (!c.frames.empty()) factory->log->inner = seeded_store(c.frames);
    int stores = 0;
    bool read_held = false;
    if (c.close) {
        factory->hooks.on_outbound_store = [&] {
            ++stores;
            if (stores != c.hold_store) return false;
            post_close();
            return true;
        };
        factory->hooks.on_outbound_read = [&] {
            if (!c.hold_replay_read || read_held || stores < 1) return false;
            read_held = true;
            post_close();
            return true;
        };
        factory->hooks.release_when = [&] { return app->close_began; };
    }
    auto const log = factory->log;
    cfg.store_factory = std::move(factory);

    if (!rig.start(cfg) || !rig.connect_peer()) return out;
    rig.peer.send(rig.msg("A", 1,
                          "98=0\x01"
                          "108=30\x01" +
                              c.logon_extra));
    out.settled = rig.run_until([&] {
        if (c.close) return close_returned;
        auto const st = rig.state();
        return st == fsm_state::Active || st == fsm_state::Disconnected;
    });
    rig.settle();

    out.close_returned = close_returned;
    out.hold_timed_out = log->hold_timed_out;
    out.wire = rig.peer.received;
    out.ring_at_close = ring_at_close;
    if (auto s = rig.session()) {
        auto const ring = s->fsm_visit_history();
        out.ring.assign(ring.begin(), ring.end());
        out.gap_filled_event = std::ranges::any_of(s->recent_events(), [](auto const& ev) {
            return std::holds_alternative<session_event_resend_slot_gap_filled>(ev);
        });
    }
    out.to_admin_all = app->to_admin_all;
    out.to_admin_after_close = app->to_admin_after_close;
    held.reset();
    rig.stop();
    return out;
}

// A setup step missed after the rig created its engine: fails the cell, then stops the
// engine on the rig's own stop path, so ~Engine's stopped() precondition holds and the
// binary's later cells still run.
void fail_and_stop(plain_rig::Rig& rig, std::string_view what) {
    ADD_FAILURE() << what;
    rig.stop();
}

std::string ring_text(std::vector<fsm_state> const& ring) {
    std::string t;
    for (auto const st : ring) t += std::to_string(static_cast<int>(st)) + " ";
    return t;
}

// Frames on the wire of `type` whose `tag` is `value` ("" = any).
std::size_t wire_count(std::string const& wire, std::string_view type, std::string_view tag = {},
                       std::string_view value = {}) {
    std::size_t n = 0;
    for (auto const& f : plain_rig::frames_of_type(wire, type)) {
        if (tag.empty() || plain_rig::field_value(f, tag).starts_with(value)) ++n;
    }
    return n;
}

// The states a close(graceful) begun in LogonReceived writes: phase 1's LogoutSent, then
// Disconnected. A Disconnected the arm writes first makes phase 1 skip, so the arm's write
// shows as the missing LogoutSent.
// A bad_alloc while building it before main aborts the binary, which fails the run.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization,cert-err58-cpp)
std::vector<fsm_state> const kCloseWrites{fsm_state::LogoutSent, fsm_state::Disconnected};

// What every close cell asserts: the close ran and returned, its hold was released by
// the close, and no admin frame other than close()'s own Logout reached toAdmin after
// the close began.
void expect_close_cell(Od25Outcome const& o) {
    ASSERT_TRUE(o.settled) << "ring=" << ring_text(o.ring);
    EXPECT_TRUE(o.close_returned);
    EXPECT_FALSE(o.hold_timed_out) << "the held operation was released by its bound";
    for (auto const& t : o.to_admin_after_close) {
        EXPECT_EQ(t, "5") << "an admin frame of type " << t << " reached toAdmin after close()";
    }
}

// The seeded outbound store for the replay cells: next-out 4, Heartbeats at 1 and 3 and
// the application frame `slot2` at 2. The peer's 789=2 asks for [2, 4) and the reply
// Logon is 4. Every slot below 4 is stored, in order, as seeded_store() requires.
Od25Case replay_case(plain_rig::Rig const& rig, std::string slot2_extra, bool close) {
    Od25Case c;
    c.logon_extra = "789=2\x01";
    auto heartbeat = [&rig](seqnum_t seq) {
        return plain_rig::message(rig.begin_string, "0", seq, "ISLD", "TW", rig.sending_time());
    };
    c.frames = {{1, heartbeat(1)}, {2, stored_app_frame(rig, 2, slot2_extra)}, {3, heartbeat(3)}};
    c.hold_replay_read = true;
    c.close = close;
    return c;
}

// A replayed application frame (PossDupFlag(43)=Y) does not reach the wire after close()
// began. The check before the replay's write.
TEST(Od25, CloseDuringA789ReplaysReadWritesNoReplayFrame) {
    plain_rig::Rig frames_rig;
    auto const control = run_od25_acceptor(replay_case(frames_rig, "11=ORD1\x01", false));
    ASSERT_TRUE(control.settled);
    EXPECT_EQ(wire_count(control.wire, "D", "43", "Y"), 1U)
        << "control: the replay frame is on the wire";

    auto const o = run_od25_acceptor(replay_case(frames_rig, "11=ORD1\x01", true));
    expect_close_cell(o);
    EXPECT_EQ(wire_count(o.wire, "D"), 0U) << "a replay frame was written after close() began";
    EXPECT_EQ(wire_count(o.wire, "4"), 0U) << "a GapFill was written after close() began";
}

// A slot whose replay frame cannot be built (#424: many SendingTime(52) fields) records
// no session_event_resend_slot_gap_filled after close() began. The check before the
// event.
TEST(Od25, CloseDuringA789ReplaysReadRecordsNoGapFilledSlot) {
    plain_rig::Rig frames_rig;
    std::string many_52;
    for (int i = 0; i < 400; ++i) many_52 += "52=\x01";
    auto const control = run_od25_acceptor(replay_case(frames_rig, many_52, false));
    ASSERT_TRUE(control.settled);
    EXPECT_TRUE(control.gap_filled_event) << "control: the slot is recorded gap-filled";

    auto const o = run_od25_acceptor(replay_case(frames_rig, many_52, true));
    expect_close_cell(o);
    EXPECT_FALSE(o.gap_filled_event) << "a gap-filled slot was recorded after close() began";
}

// A slot too large to capture fails the replay (RC#B). After close() began, the 789
// path writes no Disconnected for it: close() owns that write. The check before the
// failed replay's Disconnected write.
TEST(Od25, CloseDuringA789ReplaysReadWritesNoStateForAFailedReplay) {
    plain_rig::Rig frames_rig;
    std::string const big = "58=" + std::string(5000, 'x') + "\x01";
    auto const control = run_od25_acceptor(replay_case(frames_rig, big, false));
    ASSERT_TRUE(control.settled);
    EXPECT_EQ(control.ring.empty() ? std::optional<fsm_state>{} : control.ring.back(),
              std::optional{fsm_state::Disconnected})
        << "control: the failed replay disconnects; ring=" << ring_text(control.ring);

    auto const o = run_od25_acceptor(replay_case(frames_rig, big, true));
    expect_close_cell(o);
    ASSERT_LE(o.ring_at_close, o.ring.size());
    std::vector<fsm_state> const after_close(
        o.ring.begin() + static_cast<std::ptrdiff_t>(o.ring_at_close), o.ring.end());
    EXPECT_EQ(after_close, kCloseWrites)
        << "states written after close() began; ring=" << ring_text(o.ring);
}

// The 789 Logout branches (X==0, X>N): the Logout's store is held. After close() began,
// that Logout is not written, and the branch writes no Disconnected. The checks in
// store_then_emit and after it.
void run_logout_branch_cell(std::string const& logon_789, std::string_view text) {
    Od25Case c{.logon_extra = logon_789, .hold_store = 2, .close = false};
    auto const control = run_od25_acceptor(c);
    ASSERT_TRUE(control.settled);
    EXPECT_EQ(wire_count(control.wire, "5", "58", text), 1U)
        << "control: the 789 Logout is on the wire";

    c.close = true;
    auto const o = run_od25_acceptor(c);
    expect_close_cell(o);
    EXPECT_EQ(wire_count(o.wire, "5", "58", text), 0U)
        << "the 789 Logout was written after close() began";
    ASSERT_LE(o.ring_at_close, o.ring.size());
    std::vector<fsm_state> const after_close(
        o.ring.begin() + static_cast<std::ptrdiff_t>(o.ring_at_close), o.ring.end());
    EXPECT_EQ(after_close, kCloseWrites)
        << "states written after close() began; ring=" << ring_text(o.ring);
}

TEST(Od25, CloseDuringTheInvalid789LogoutsStoreWritesNoLogoutAndNoState) {
    run_logout_branch_cell("789=0\x01", "NextExpectedMsgSeqNum invalid");
}

TEST(Od25, CloseDuringTheTooHigh789LogoutsStoreWritesNoLogoutAndNoState) {
    run_logout_branch_cell("789=99\x01", "NextExpectedMsgSeqNum too high");
}

// The reply Logon's store is held (contract C-6's erratum; plan OD-25). After close()
// began, the reply is not written. The check in store_then_emit.
TEST(Od25, CloseDuringTheReplyLogonsStoreWritesNoReply) {
    Od25Case c{.hold_store = 1, .close = false};
    auto const control = run_od25_acceptor(c);
    ASSERT_TRUE(control.settled);
    EXPECT_EQ(wire_count(control.wire, "A"), 1U) << "control: the reply Logon is on the wire";

    c.close = true;
    auto const o = run_od25_acceptor(c);
    expect_close_cell(o);
    EXPECT_EQ(wire_count(o.wire, "A"), 0U) << "the reply Logon was written after close() began";
}

// Engine::stop() during the initiator's 789 replay (it sent 141=Y, bilateral_strict; the
// peer acks with 141=Y and 789=1, so the unit's targets are (2, 2) and the replay is
// [1, 1], an absent slot after the reset). The replay's first store read is held until
// stop()'s step 1 has run. No GapFill reaches toAdmin or the wire. The check before the
// GapFill's toAdmin.
TEST(Od25, EngineStopDuringA789ReplaysReadFiresNoGapFill) {
    auto const app = std::make_shared<Od25App>();
    plain_rig::Rig rig{app};
    auto cfg = rig.cfg(session_role::initiator);
    cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_strict;
    cfg.enable_next_expected_msg_seq_num = true;

    std::shared_ptr<Session> sess;
    auto stop_step1_ran = [&] {
        if (!sess) sess = rig.session();
        return sess != nullptr && session_test_access::engine_stop_requested(*sess);
    };
    auto factory = std::make_shared<HookedStoreFactory>();
    auto const log = factory->log;
    bool read_held = false;
    std::size_t to_admin_at_hold = 0;
    factory->hooks.on_outbound_read = [&] {
        bool const unit_done = std::ranges::any_of(
            log->writes, [](auto const& w) { return w.op.starts_with("reset_to"); });
        if (read_held || !unit_done) return false;
        read_held = true;
        to_admin_at_hold = app->to_admin_all.size();
        return true;
    };
    factory->hooks.release_when = stop_step1_ran;
    cfg.store_factory = std::move(factory);

    // Each setup miss below goes through fail_and_stop, not a fatal ASSERT.
    if (!rig.start(cfg)) {
        fail_and_stop(rig, "the rig did not start");
        return;
    }
    if (!rig.run_until([&] {
            return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
                   rig.state() == fsm_state::LogonSent;
        })) {
        fail_and_stop(rig, "the initiator's Logon");
        return;
    }
    rig.peer.send(rig.msg("A", 1,
                          "98=0\x01"
                          "108=30\x01"
                          "141=Y\x01"
                          "789=1\x01"));
    if (!rig.run_until([&] { return read_held; })) {
        fail_and_stop(rig, "the replay's read");
        return;
    }
    std::size_t const wire_at_hold = rig.peer.received.size();

    auto stop_fut = asio::co_spawn(rig.ioc, rig.engine->stop(), asio::use_future);
    bool const stopped = rig.run_until(
        [&] { return stop_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; });
    if (!stopped) {
        fixpp::test_support::cancel_and_drain_or_report(rig.ioc, *rig.clock,
                                                        "Od25::EngineStop stop");
        ADD_FAILURE() << "Engine::stop() did not complete";
        return;
    }
    stop_fut.get();
    ASSERT_NE(sess, nullptr);

    EXPECT_FALSE(log->hold_timed_out) << "the read was released by its bound, not by stop()";
    EXPECT_EQ(app->to_admin_all.size(), to_admin_at_hold)
        << "an admin frame reached toAdmin after stop()'s step 1";
    EXPECT_EQ(wire_count(rig.peer.received.substr(wire_at_hold), "4"), 0U)
        << "a GapFill was written after stop()'s step 1";
    auto const ring = sess->fsm_visit_history();
    EXPECT_EQ(std::count(ring.begin(), ring.end(), fsm_state::Active), 0)
        << "the session reached Active";
}

// ── 093 plan OD-26: the other suspension-then-effect windows in the Logon arms ─────
//
// Each wire site passes the arm into store_then_emit, and each Disconnected written
// after a store suspension goes through one member that tests the predicate first. A
// cell holds the store operation at its site, posts close(graceful) there, and the
// operation is released once the close has begun. Two observables:
//   - the site's frame on the peer's capture: close(graceful) writes nothing to the
//     socket before its flush returns, and its flush is held (below), so a frame written
//     after the close began is on the capture;
//   - the FSM state when close()'s flush returns: the HookedStore's flush yields until
//     the arm has had its turns (Hooks::flush_until), then records the state. A
//     Disconnected written by the arm shows there; close() writes its own after the
//     flush.
// Each cell has a control with no hold and no close, in which the frame does appear.

struct Od26Case {
    session_role role = session_role::acceptor;
    // Adjusts the session config, and builds the peer's Logon (or Logon-ack).
    std::function<void(SessionConfig&, plain_rig::Rig&)> configure;
    std::function<std::string(plain_rig::Rig&)> peer_logon;
    // The held operation: the n-th outbound store() (1-based; an initiator's own Logon
    // is the first), the first outbound next_seqnum(_, false), or the first inbound
    // next_seqnum(_, true). fail: it returns store_io_failure once released.
    int hold_store = 0;
    bool hold_first_outbound_read = false;
    bool hold_inbound_persist = false;
    bool fail = false;
    bool close = true;  // false: the control, no hold and no close
};

struct Od26Outcome {
    bool settled = false;
    bool close_returned = false;
    bool hold_timed_out = false;
    bool flush_hold_timed_out = false;
    std::string wire;
    std::optional<fsm_state> state_at_flush_end;
    std::optional<fsm_state> state_at_settle;
};

Od26Outcome run_od26(Od26Case const& c) {
    Od26Outcome out;
    auto const app = std::make_shared<Od25App>();
    plain_rig::Rig rig{app};
    auto cfg = rig.cfg(c.role);
    if (c.configure) c.configure(cfg, rig);

    std::shared_ptr<Session> held;
    bool close_returned = false;
    auto post_close = [&] {
        held = rig.session();
        if (!held) return;
        asio::any_io_executor ex = held->executor().underlying();
        asio::post(ex, [&, ex] {
            asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    app->close_began = true;
                    (void)co_await held->close(close_mode::graceful);
                    close_returned = true;
                },
                asio::detached);
        });
    };

    auto factory = std::make_shared<HookedStoreFactory>();
    int stores = 0;
    bool read_held = false;
    int flush_polls = 0;
    if (c.close) {
        factory->hooks.on_outbound_store = [&] {
            ++stores;
            if (stores != c.hold_store) return false;
            post_close();
            return true;
        };
        factory->hooks.on_outbound_read = [&] {
            if (!c.hold_first_outbound_read || read_held) return false;
            read_held = true;
            post_close();
            return true;
        };
        if (c.hold_inbound_persist) factory->hooks.on_inbound_persist = post_close;
        factory->hooks.release_when = [&] { return app->close_began; };
        factory->hooks.fail_held = c.fail;
        factory->hooks.flush_until = [&] {
            if (held) out.state_at_flush_end = held->state();
            return ++flush_polls >= 32;
        };
    }
    auto const log = factory->log;
    cfg.store_factory = std::move(factory);

    if (!rig.start(cfg)) return out;
    if (c.role == session_role::acceptor) {
        if (!rig.connect_peer()) return out;
    } else if (!rig.run_until([&] {
                   return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
                          rig.state() == fsm_state::LogonSent;
               })) {
        return out;
    }
    rig.peer.send(c.peer_logon(rig));
    out.settled = rig.run_until([&] {
        if (c.close) return close_returned;
        // The control: the session settled, or a Reject or Logout reached the peer (a
        // validation Reject leaves the session where it was).
        auto const st = rig.state();
        return st == fsm_state::Active || st == fsm_state::Disconnected ||
               !plain_rig::frames_of_type(rig.peer.received, "3").empty() ||
               !plain_rig::frames_of_type(rig.peer.received, "5").empty();
    });
    rig.settle();

    out.close_returned = close_returned;
    out.hold_timed_out = log->hold_timed_out;
    out.flush_hold_timed_out = log->flush_hold_timed_out;
    out.wire = rig.peer.received;
    out.state_at_settle = rig.state();
    held.reset();
    rig.stop();
    return out;
}

// The site's frame in `wire`, by MsgType and one field's value prefix.
struct FrameMatch {
    std::string_view type;
    std::string_view tag;
    std::string_view value;
};

// Runs the control (the frame is written) and the close cell (it is not, and nothing
// wrote Disconnected before close()'s flush returned, the arm's state then being
// `arm_state`).
void run_od26_site(Od26Case c, FrameMatch m, fsm_state arm_state) {
    c.close = false;
    auto const control = run_od26(c);
    ASSERT_TRUE(control.settled);
    EXPECT_EQ(wire_count(control.wire, m.type, m.tag, m.value), 1U)
        << "control: the site's frame is on the wire";

    c.close = true;
    auto const o = run_od26(c);
    ASSERT_TRUE(o.settled);
    EXPECT_TRUE(o.close_returned);
    EXPECT_FALSE(o.hold_timed_out) << "the held operation was released by its bound";
    EXPECT_FALSE(o.flush_hold_timed_out);
    EXPECT_EQ(wire_count(o.wire, m.type, m.tag, m.value), 0U)
        << "the site's frame was written after close() began";
    EXPECT_EQ(o.state_at_flush_end, std::optional{arm_state})
        << "a state was written before close()'s flush returned";
}

std::string stale_logon(plain_rig::Rig& rig, std::string_view extra = {}) {
    return plain_rig::message(rig.begin_string, "A", 1, "TW", "ISLD", "20000101-00:00:00.000",
                              std::string{"98=0\x01"
                                          "108=30\x01"} +
                                  std::string{extra});
}

std::string logon_with(plain_rig::Rig& rig, std::string_view extra) {
    return rig.msg("A", 1,
                   std::string{"98=0\x01"
                               "108=30\x01"} +
                       std::string{extra});
}

// refuse_logon_with_logout_, from each arm: a production-posture session refuses a peer
// whose Logon carries TestMessageIndicator(464)=Y.
TEST(Od26, CloseDuringTheAcceptorsPostureRefusalWritesNoLogout) {
    run_od26_site({.role = session_role::acceptor,
                   .configure = [](SessionConfig& cfg,
                                   plain_rig::Rig&) { cfg.posture = session_posture::production; },
                   .peer_logon = [](plain_rig::Rig& rig) { return logon_with(rig, "464=Y\x01"); },
                   .hold_store = 1},
                  {.type = "5", .tag = "58", .value = "TestMessageIndicator posture mismatch"},
                  fsm_state::NotConnected);
}

TEST(Od26, CloseDuringTheInitiatorsPostureRefusalWritesNoLogout) {
    run_od26_site({.role = session_role::initiator,
                   .configure = [](SessionConfig& cfg,
                                   plain_rig::Rig&) { cfg.posture = session_posture::production; },
                   .peer_logon = [](plain_rig::Rig& rig) { return logon_with(rig, "464=Y\x01"); },
                   .hold_store = 2},
                  {.type = "5", .tag = "58", .value = "TestMessageIndicator posture mismatch"},
                  fsm_state::LogonSent);
}

// The acceptor's SendingTime(52) Reject: a stale SendingTime on the Logon.
TEST(Od26, CloseDuringTheAcceptorsSendingTimeRejectWritesNoReject) {
    run_od26_site({.role = session_role::acceptor,
                   .peer_logon = [](plain_rig::Rig& rig) { return stale_logon(rig); },
                   .hold_store = 1},
                  {.type = "3", .tag = "371", .value = "52"}, fsm_state::NotConnected);
}

// The initiator's SendingTime(52) Logout: a stale SendingTime on the Logon-ack.
TEST(Od26, CloseDuringTheInitiatorsSendingTimeLogoutWritesNoLogout) {
    run_od26_site({.role = session_role::initiator,
                   .peer_logon = [](plain_rig::Rig& rig) { return stale_logon(rig); },
                   .hold_store = 2},
                  {.type = "5", .tag = "58", .value = "SendingTime(52)"}, fsm_state::LogonSent);
}

// The validation test dictionary loaded as FIX 4.4, with DefaultApplVerID(1137) declared
// on the Logon: a FIXT.1.1 session's dictionary, and the engine's one application
// version.
std::shared_ptr<const fixpp::dict::Dictionary> fix44_dictionary_with_1137() {
    std::string xml{fixpp::test_support::kValidationTestFix42Xml};
    auto replace_once = [&xml](std::string_view from, std::string_view to) {
        auto const pos = xml.find(from);
        if (pos == std::string::npos) {
            ADD_FAILURE() << "fix44_dictionary_with_1137: text not found: " << from;
            return;
        }
        xml.replace(pos, from.size(), to);
    };
    replace_once(R"(<fix major="4" minor="2">)", R"(<fix major="4" minor="4">)");
    replace_once(R"(<field number="108" name="HeartBtInt"    required="Y"/>)",
                 R"(<field number="108" name="HeartBtInt"    required="Y"/>)"
                 R"(<field number="1137" name="DefaultApplVerID" required="N"/>)");
    replace_once(R"(<field number="112" name="TestReqID"    type="STRING"/>)",
                 R"(<field number="112" name="TestReqID"    type="STRING"/>)"
                 R"(<field number="1137" name="DefaultApplVerID" type="STRING"/>)");
    constexpr std::size_t kBufSize = 128U * 1024U;
    // Destroyed in reverse order: the dictionary, then its resource, then the buffer.
    struct Owned {
        std::unique_ptr<std::array<std::byte, kBufSize>> buf;
        std::unique_ptr<std::pmr::monotonic_buffer_resource> mr;
        std::unique_ptr<const fixpp::dict::Dictionary> dict;
    };
    auto owned = std::make_shared<Owned>();
    owned->buf = std::make_unique<std::array<std::byte, kBufSize>>();
    owned->mr = std::make_unique<std::pmr::monotonic_buffer_resource>(owned->buf->data(),
                                                                      owned->buf->size());
    owned->dict = std::make_unique<const fixpp::dict::Dictionary>(
        fixpp::dict::XmlLoader{}.load_from_string(xml, owned->mr.get()));
    return std::shared_ptr<const fixpp::dict::Dictionary>{owned, owned->dict.get()};
}

// The acceptor's DefaultApplVerID(1137) Reject: a FIXT session, a Logon without 1137.
TEST(Od26, CloseDuringTheAcceptors1137RejectWritesNoReject) {
    run_od26_site({.role = session_role::acceptor,
                   .configure =
                       [](SessionConfig& cfg, plain_rig::Rig& rig) {
                           auto const dict = fix44_dictionary_with_1137();
                           rig.engine_dictionaries = {dict};
                           cfg.dictionary = dict;
                           cfg.begin_string = "FIXT.1.1";
                           cfg.default_appl_ver_id = fixpp::dict::application_version::v44;
                       },
                   .peer_logon =
                       [](plain_rig::Rig& rig) {
                           return plain_rig::message("FIXT.1.1", "A", 1, "TW", "ISLD",
                                                     rig.sending_time(),
                                                     "98=0\x01"
                                                     "108=30\x01");
                       },
                   .hold_store = 1},
                  {.type = "3", .tag = "371", .value = "1137"}, fsm_state::NotConnected);
}

// emit_session_reject_ on each arm's validate path: a Logon without HeartBtInt(108),
// which the validation test dictionary requires.
void validating(SessionConfig& cfg, plain_rig::Rig& /*rig*/) {
    cfg.dictionary = fixpp::test_support::make_validation_test_dictionary();
    cfg.validate_inbound_messages = true;
}
std::string logon_without_108(plain_rig::Rig& rig) { return rig.msg("A", 1, "98=0\x01"); }

TEST(Od26, CloseDuringTheAcceptorsValidationRejectWritesNoReject) {
    run_od26_site({.role = session_role::acceptor,
                   .configure = validating,
                   .peer_logon = logon_without_108,
                   .hold_store = 1},
                  {.type = "3", .tag = "371", .value = "108"}, fsm_state::NotConnected);
}

TEST(Od26, CloseDuringTheInitiatorsValidationRejectWritesNoReject) {
    run_od26_site({.role = session_role::initiator,
                   .configure = validating,
                   .peer_logon = logon_without_108,
                   .hold_store = 2},
                  {.type = "3", .tag = "371", .value = "108"}, fsm_state::LogonSent);
}

// The member, on a read failure: the acceptor's hydrating outbound read fails after
// close() began. Nothing writes Disconnected before close() does.
TEST(Od26, CloseDuringAFailingHydrateReadWritesNoState) {
    Od26Case c{.role = session_role::acceptor,
               .peer_logon = [](plain_rig::Rig& rig) { return rig.logon(); },
               .hold_first_outbound_read = true,
               .fail = true};
    auto const o = run_od26(c);
    ASSERT_TRUE(o.settled);
    EXPECT_FALSE(o.hold_timed_out);
    EXPECT_EQ(o.state_at_flush_end, std::optional{fsm_state::NotConnected})
        << "a state was written before close()'s flush returned";
}

// The member, on a write failure: the acceptor's inbound persist after Active fails
// after close() began.
TEST(Od26, CloseDuringAFailingInboundPersistWritesNoState) {
    Od26Case c{.role = session_role::acceptor,
               .peer_logon = [](plain_rig::Rig& rig) { return rig.logon(); },
               .hold_inbound_persist = true,
               .fail = true};
    auto const o = run_od26(c);
    ASSERT_TRUE(o.settled);
    EXPECT_FALSE(o.hold_timed_out);
    EXPECT_EQ(o.state_at_flush_end, std::optional{fsm_state::Active})
        << "a state was written before close()'s flush returned";
}

// The member, on the hydrate after both reads succeeded: the acceptor's hydrating
// outbound read is held until close() has returned, so close()'s drain of the seqnum
// mutex has run, and is then released with its value. SeqnumManager::hydrate fails on
// the drained mutex, and ensure_hydrated_ stops the arm through
// disconnect_unless_superseded_. close() wrote Disconnected itself, so a second write
// of the same state is visible only in the FSM visit history: nothing may be appended
// to it once close() has returned.
// Precondition: no teardown reset (reset_on_logout and reset_on_disconnect off). With
// one, close()'s store reset could queue behind the held read, and the hydrate would
// run before the drain.
TEST(Od26, CloseDrainingTheSeqnumMutexBeforeTheHydrateWritesNoState) {
    auto const app = std::make_shared<Od25App>();
    plain_rig::Rig rig{app};
    auto cfg = rig.cfg(session_role::acceptor);
    ASSERT_FALSE(cfg.reset_on_logout);
    ASSERT_FALSE(cfg.reset_on_disconnect);

    std::shared_ptr<Session> held;
    bool read_held = false;
    bool released = false;
    bool close_returned = false;
    std::vector<fsm_state> history_at_close;
    auto factory = std::make_shared<HookedStoreFactory>();
    factory->hooks.on_outbound_read = [&] {
        if (read_held) return false;
        read_held = true;
        held = rig.session();
        if (!held) return true;
        asio::any_io_executor ex = held->executor().underlying();
        asio::post(ex, [&, ex] {
            asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    app->close_began = true;
                    (void)co_await held->close(close_mode::graceful);
                    auto const h = held->fsm_visit_history();
                    history_at_close.assign(h.begin(), h.end());
                    close_returned = true;
                },
                asio::detached);
        });
        return true;
    };
    // Releases the held read, with its value, only once close() has returned.
    factory->hooks.release_when = [&] {
        released = released || close_returned;
        return close_returned;
    };
    auto const log = factory->log;
    cfg.store_factory = std::move(factory);

    bool const up = rig.start(cfg) && rig.connect_peer();
    if (up) rig.peer.send(rig.logon());
    bool const closed = up && rig.run_until([&] { return close_returned; });
    bool const resumed = closed && rig.run_until([&] { return released; });
    rig.settle();
    std::vector<fsm_state> history_after;
    if (held) {
        auto const h = held->fsm_visit_history();
        history_after.assign(h.begin(), h.end());
    }
    bool const logon_answered = !plain_rig::frames_of_type(rig.peer.received, "A").empty();
    auto const state_after = rig.state();
    bool const hold_timed_out = log->hold_timed_out;
    held.reset();
    rig.stop();

    ASSERT_TRUE(up && read_held) << "setup: the hydrating outbound read was held";
    ASSERT_TRUE(closed) << "close() returned";
    ASSERT_TRUE(resumed) << "the read was released after close() returned";
    // The release condition is close() having returned, so the read completes before
    // that only through its bound, which sets hold_timed_out.
    EXPECT_FALSE(hold_timed_out)
        << "the read completed through its bound, not after close() returned while it was held";
    EXPECT_EQ(history_after, history_at_close)
        << "the Logon arm wrote a state after close() had returned";
    EXPECT_FALSE(logon_answered) << "the Logon arm stopped: no Logon was sent";
    EXPECT_EQ(state_after, fsm_state::Disconnected);
}

}  // namespace
}  // namespace fixpp::session::test
