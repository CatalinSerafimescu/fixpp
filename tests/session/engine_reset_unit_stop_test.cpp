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
#include <future>
#include <memory>
#include <optional>
#include <span>
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
    return std::string{p.role == session_role::acceptor ? "Acceptor" : "Initiator"} + "_" +
           store + (p.teardown ? "_TeardownReset" : "_NoTeardownReset");
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
    auto fio_until_unit = [&] { return p.store != StoreKind::file_held && !unit_in_flight(); };

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
        ASSERT_TRUE(run_until([&] { return lock_held; }, [] { return false; }))
            << "the cell's lock";
    }

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
    // The FileStore row: a store() the cell issues on the session's strand takes the
    // writer lock and parks in its file I/O, which does not run until the flag is set.
    bool competitor_began = false;
    bool competitor_done = false;
    if (p.store == StoreKind::file_held) {
        auto* s = session();
        ASSERT_NE(s, nullptr);
        auto* store = session_test_access::store(*s);
        ASSERT_NE(store, nullptr);
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
        ASSERT_TRUE(competitor_began);
        ASSERT_FALSE(competitor_done) << "the competing store() did not park in its file I/O";
    }
    rig.peer.send(rig.msg("A", 1, logon_141));
    ASSERT_TRUE(run_until(unit_in_flight, fio_until_unit)) << "the unit's store operation";
    ASSERT_NE(session(), nullptr);
    int const to_admin_at_unit = app->to_admin;

    auto stop_fut = asio::co_spawn(rig.ioc, rig.engine->stop(), asio::use_future);
    bool const stopped = run_until(
        [&] { return stop_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
        [&] { return !file_backed || stop_step1_ran(); });
    ASSERT_TRUE(stopped) << "Engine::stop() did not complete";
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
                out.push_back({store, teardown, role});
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
    return plain_rig::message(rig.begin_string, "D", seq, "ISLD", "TW", rig.sending_time(),
                              extra);
}

// A MemoryStore with `frames` stored outbound, in order from 1. MemoryStore's store()
// takes only its next number and advances the counter past it.
std::shared_ptr<MemoryStore> seeded_store(std::vector<std::pair<seqnum_t, std::string>> frames) {
    auto inner = std::make_shared<MemoryStore>(
        MemoryStore::Config{.policy = capacity_policy::unbounded});
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
    rig.peer.send(rig.msg("A", 1, "98=0\x01" "108=30\x01" + c.logon_extra));
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
    std::vector<fsm_state> const after_close(o.ring.begin() + static_cast<std::ptrdiff_t>(o.ring_at_close),
                                             o.ring.end());
    EXPECT_EQ(after_close, kCloseWrites) << "states written after close() began; ring=" << ring_text(o.ring);
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
    std::vector<fsm_state> const after_close(o.ring.begin() + static_cast<std::ptrdiff_t>(o.ring_at_close),
                                             o.ring.end());
    EXPECT_EQ(after_close, kCloseWrites) << "states written after close() began; ring=" << ring_text(o.ring);
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

    ASSERT_TRUE(rig.start(cfg));
    ASSERT_TRUE(rig.run_until([&] {
        return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
               rig.state() == fsm_state::LogonSent;
    })) << "the initiator's Logon";
    rig.peer.send(rig.msg("A", 1, "98=0\x01" "108=30\x01" "141=Y\x01" "789=1\x01"));
    ASSERT_TRUE(rig.run_until([&] { return read_held; })) << "the replay's read";
    std::size_t const wire_at_hold = rig.peer.received.size();

    auto stop_fut = asio::co_spawn(rig.ioc, rig.engine->stop(), asio::use_future);
    bool const stopped = rig.run_until(
        [&] { return stop_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; });
    ASSERT_TRUE(stopped) << "Engine::stop() did not complete";
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

}  // namespace
}  // namespace fixpp::session::test
