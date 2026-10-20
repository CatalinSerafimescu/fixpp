// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_598_store_offload_engine.cpp
//
// fixpp#598 (.specify/536-537-538-recovery-liveness.md §11, D-598-1, D-598-3; §5, the table
// "The store's offload (§11)"; §11.5): the session scenes under an Engine, on the plain
// transport. The cells on a bare FileStore and on a directly driven Session are in
// test_598_store_offload.cpp, whose header describes the window "taken and not started" and
// the release event; this file uses the same executor
// (tests/support/recording_file_executor.hpp) on the Scene of held_file_store_scene.hpp, with
// no held send.
//
// The executor is armed from inside the Logout's toAdmin, after close(graceful)'s flush has
// been through the file executor, so the Logout's write job is job 1; for an application's
// send it is armed before the send.
//
// The release, under an Engine. The cancellation reaches the store call through further
// handlers on the session's executor (the io_context the test thread runs), so the count of
// jobs is final only when those have run. The cell posts a marker to that executor and runs
// it until the marker has run, once per hop, and never runs it until it is idle: in the
// escalation input the terminal close posts itself again for as long as the graceful one
// runs (fixpp#593). The hops, re-derived from `git grep -n 'close_grace' --
// src/session/session.cpp` and asio's experimental/impl/parallel_group.hpp:
//   the grace timer input: close_grace's completion ends its arm of `||`; that arm's
//     completion reaches the parallel group, which emits the cancellation on the slot of the
//     logout phase's arm; that arm's co_spawn cancellation handler dispatches onto the
//     session's executor;
//   the escalation input: before those, the terminal close's own start, which cancels
//     close_grace through close_grace_wake_;
//   an application's send: the cell's emit, and the frames of Engine::send and Session::send
//     between the slot and the store call.
// A marker beyond the hops costs nothing. The markers are enough only if every cell here but
// the controls fails at the count or at the read-back under the mutant M1, offload_to's
// co_await with asio::use_awaitable alone (src/session/file_store.cpp), applied to the
// current tree. Re-run that mutant after any change to the hops above: to Session::close,
// Session::run_logout_phase1, Session::send or Engine::send.
//
// The grace timer input waits for close's own grace timer, which runs on real time; it is the
// one wait on a clock in these cells. From inside the Logout's toAdmin the cell reads the
// steady clock and arms a timer of its own on the session's executor at that reading plus
// the session's logout_disconnect_timeout_ms plus one millisecond. close set its deadline
// before it started the race, so before the toAdmin: the cell's deadline is strictly later.
// Both timers complete through the session's executor, so the cell's handler runs after
// close_grace's, and the markers follow it. The session's own clock is a mock clock and no
// cell here moves it.
//
// A cell asserts nothing while its engine runs, and stops the engine on every path
// (~Engine requires a completed stop). No mutant for the controls on one thread: there the
// cancellation's job runs after the write's.
//
// FS-RECORD lines on stdout carry each cell's readings, printed before any assertion.

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "session/held_file_store_scene.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_file_executor.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace fe = fixpp::test_support::file_executor;
namespace plain_rig = fixpp::test_support::plain_rig;
using fixpp::core::error;
using fixpp::session::close_mode;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_test_access;
using fixpp::test_support::HeldFileStoreScene;
using fixpp::test_support::kPumpBudget;
using steady = std::chrono::steady_clock;

// logout_disconnect_timeout_ms of the scene's session. Every wait of a cell is derived from
// the value the session was configured with, not from this constant.
constexpr std::uint32_t kEngineLogoutTimeoutMs = 1000;
constexpr int kMarkerHops = 6;

enum class FileExec : std::uint8_t {
    two_threads_write_delayed,   // the delaying executor on the Scene's pool of two threads
    one_thread_held_by_the_cell  // the Scene's pool of one thread, held by a job of the cell
};

enum class SessionInput : std::uint8_t { grace_timer, escalation, app_send };

struct LogoutToAdminApp final : fixpp::session::Application {
    std::function<void()> on_logout;
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& mv,
                 const fixpp::session::SessionId& /*id*/) override {
        if (on_logout && mv.msg_type() == "5") on_logout();
    }
};

std::string error_text(std::optional<error> const& e) {
    return e ? "error " + std::to_string(static_cast<int>(*e)) : std::string{"success"};
}

std::string result_text(std::optional<fixpp::core::expected_t<void>> const& r) {
    if (!r) return "did not complete";
    return r->has_value() ? std::string{"success"} : error_text(r->error());
}

// The MsgType(35) of each frame retrieve visits.
class TypeVisitor final : public fixpp::session::retrieve_visitor {
public:
    std::string types;
    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> frame) noexcept override {
        std::string_view const text{reinterpret_cast<char const*>(frame.data()), frame.size()};
        types += (types.empty() ? "" : "+") + plain_rig::field_value(text, "35");
        co_return fixpp::session::visit_result::cont;
    }
};

struct StoreAt {
    bool read = false;
    std::optional<error> retrieve_error;
    std::string types;  // MsgType(35) of the frames at the number; empty: none
    std::optional<seqnum_t> next_out;
    [[nodiscard]] std::string text() const {
        return (retrieve_error ? error_text(retrieve_error) : "35=" + types) +
               ", next_out=" + (next_out ? std::to_string(*next_out) : std::string{"?"});
    }
};

asio::awaitable<StoreAt> read_store_at(fixpp::session::MessageStore* store, seqnum_t seq) {
    StoreAt out;
    TypeVisitor visitor;
    auto const r = co_await store->retrieve(seq, seq, direction_t::outbound, visitor);
    if (!r) out.retrieve_error = r.error();
    out.types = visitor.types;
    if (auto const n = co_await store->next_seqnum(direction_t::outbound, false)) {
        out.next_out = *n;
    }
    out.read = true;
    co_return out;
}

struct EngineOutcome {
    std::string setup_miss;
    bool armed = false;
    bool reached = false;
    bool before_deadline = true;  // the grace timer input: event 1 came before close's deadline
    bool cell_timer_seen = true;  // the grace timer input: the cell's own timer ran
    bool emitted = true;          // an application's send: the cell's emit ran
    int markers = 0;
    bool job1_began_at_count = false;
    int count = 0;
    bool others_ended = false;
    bool graceful_ready_before = false;
    bool terminal_ready_before = false;
    bool send_ready_before = false;
    bool completed_after = false;
    std::optional<fixpp::core::expected_t<void>> graceful;
    std::optional<fixpp::core::expected_t<void>> terminal;
    std::optional<fixpp::core::expected_t<void>> send;
    seqnum_t seq = 0;  // the Logout's number, or the application message's
    StoreAt store;
    seqnum_t session_next_out = 0;
    fsm_state state = fsm_state::NotConnected;  // read before the engine stops
    bool peer_read_ended = false;
    std::size_t peer_frames_at_seq = 0;  // of the scene's type at `seq`, to end-of-stream
    int bodies = 0;
    int bodies_after_let_go = 0;
    bool timed_out = false;
    bool stop_completed = false;
    std::string events;

    [[nodiscard]] bool exercised(SessionInput in) const {
        bool const common = setup_miss.empty() && armed && reached && emitted &&
                            markers == kMarkerHops && !job1_began_at_count && others_ended &&
                            !timed_out && completed_after;
        bool const timer_ok =
            in != SessionInput::grace_timer || (before_deadline && cell_timer_seen);
        return common && timer_ok;
    }
    [[nodiscard]] std::string text() const {
        return "setup_miss='" + setup_miss + "' armed=" + std::to_string(armed) +
               " reached=" + std::to_string(reached) +
               " before_deadline=" + std::to_string(before_deadline) +
               " cell_timer_seen=" + std::to_string(cell_timer_seen) +
               " emitted=" + std::to_string(emitted) + " markers=" + std::to_string(markers) +
               " job1_began_at_count=" + std::to_string(job1_began_at_count) +
               " count=" + std::to_string(count) + " others_ended=" + std::to_string(others_ended) +
               " timed_out=" + std::to_string(timed_out) +
               " completed_after=" + std::to_string(completed_after) +
               "\nthe executor's record:\n" + events;
    }
};

EngineOutcome run_engine_session(SessionInput in, FileExec kind, char const* cell) {
    using std::chrono::milliseconds;
    EngineOutcome o;
    bool const two = kind == FileExec::two_threads_write_delayed;
    auto const log = std::make_shared<fe::JobLog>();
    auto const app = std::make_shared<LogoutToAdminApp>();
    auto const sig = std::make_shared<asio::cancellation_signal>();
    HeldFileStoreScene scene{two ? 2U : 1U, app};
    // After the Scene: it replaces the Scene's offload probe for this scope, and is removed
    // before the Scene joins its pool.
    fe::scoped_body_probe const probe{*log};
    // After the Scene: a held thread is let go before the Scene's pool is joined.
    fe::let_go_on_exit const let_go{log};
    auto& rig = scene.rig;
    std::optional<asio::thread_pool::executor_type> pool_ex;
    scene.wrap_file_executor = [&pool_ex, log, two](asio::thread_pool::executor_type ex) {
        pool_ex = ex;
        return asio::any_io_executor{fe::DelayingExecutor{ex, log, two}};
    };
    scene.tweak_cfg = [](fixpp::session::SessionConfig& c) {
        c.logout_disconnect_timeout_ms = kEngineLogoutTimeoutMs;
    };
    std::shared_ptr<fixpp::session::Session> session;
    std::future<fixpp::core::expected_t<void>> graceful_fut;
    std::future<fixpp::core::expected_t<void>> terminal_fut;
    std::future<fixpp::core::expected_t<void>> send_fut;
    std::size_t rx_mark = 0;
    bool cell_timer_ran = false;
    steady::time_point reading{};
    std::optional<asio::steady_timer> cell_timer;

    bool const ran = [&] {
        if (!scene.setup(0)) return false;
        session = rig.session();
        if (!session || !pool_ex) {
            scene.setup_miss = "the session is not published";
            return false;
        }
        auto const strand = session->executor().underlying();
        cell_timer.emplace(strand);
        auto const timeout = milliseconds{kEngineLogoutTimeoutMs};
        o.seq = session_test_access::seqnum_mgr(*session).peek_outbound();
        rx_mark = rig.peer.received.size();

        if (in != SessionInput::app_send) {
            app->on_logout = [&] {
                if (o.armed) return;
                o.armed = true;
                reading = steady::now();
                log->arm();
                if (!two) log->hold_a_thread(*pool_ex);
                if (in == SessionInput::grace_timer) {
                    cell_timer->expires_at(reading + timeout + milliseconds{1});
                    cell_timer->async_wait([&](std::error_code const& ec) {
                        if (!ec) cell_timer_ran = true;
                    });
                }
            };
            graceful_fut =
                asio::co_spawn(strand, session->close(close_mode::graceful), asio::use_future);
        } else {
            log->arm();
            o.armed = true;
            if (!two) {
                log->hold_a_thread(*pool_ex);
                if (!log->wait_holders(1)) return true;
            }
            send_fut = scene.send_app(asio::bind_cancellation_slot(sig->slot(), asio::use_future));
        }

        // Event 1.
        o.reached = rig.run_until([&] { return two ? log->held() : log->handed() >= 1; }) &&
                    (two || log->wait_holders(1));
        if (o.reached && in == SessionInput::grace_timer) {
            o.before_deadline = steady::now() < reading + timeout;
        }
        if (!o.reached) return true;
        // Event 2.
        if (in == SessionInput::grace_timer) {
            o.cell_timer_seen =
                rig.run_until([&] { return cell_timer_ran; }, timeout + kPumpBudget);
        } else if (in == SessionInput::escalation) {
            terminal_fut =
                asio::co_spawn(strand, session->close(close_mode::terminal), asio::use_future);
        } else {
            o.emitted = false;
            asio::post(strand, [&] {
                sig->emit(asio::cancellation_type::terminal);
                o.emitted = true;
            });
            (void)rig.run_until([&] { return o.emitted; });
        }
        // The hops, one marker each.
        for (int i = 0; i < kMarkerHops; ++i) {
            bool seen = false;
            asio::post(strand, [&seen] { seen = true; });
            if (rig.run_until([&seen] { return seen; })) ++o.markers;
        }
        // Event 3.
        o.job1_began_at_count = log->job_began(1);
        o.count = log->handed();
        o.others_ended = two ? log->wait_others_ended() : true;
        o.graceful_ready_before = graceful_fut.valid() && HeldFileStoreScene::ready(graceful_fut);
        o.terminal_ready_before = terminal_fut.valid() && HeldFileStoreScene::ready(terminal_fut);
        o.send_ready_before = send_fut.valid() && HeldFileStoreScene::ready(send_fut);
        return true;
    }();

    log->let_go();
    if (ran) {
        o.completed_after = rig.run_until([&] {
            return (!graceful_fut.valid() || HeldFileStoreScene::ready(graceful_fut)) &&
                   (!terminal_fut.valid() || HeldFileStoreScene::ready(terminal_fut)) &&
                   (!send_fut.valid() || HeldFileStoreScene::ready(send_fut));
        });
        app->on_logout = nullptr;
        if (graceful_fut.valid() && HeldFileStoreScene::ready(graceful_fut)) {
            o.graceful = graceful_fut.get();
        }
        if (terminal_fut.valid() && HeldFileStoreScene::ready(terminal_fut)) {
            o.terminal = terminal_fut.get();
        }
        if (send_fut.valid() && HeldFileStoreScene::ready(send_fut)) o.send = send_fut.get();
        o.bodies = log->bodies();
        o.bodies_after_let_go = log->bodies_after_let_go();
        o.timed_out = log->held_timed_out();
        o.events = log->events();
        if (auto* const store = scene.log->store; store != nullptr) {
            auto read_fut = asio::co_spawn(rig.ioc, read_store_at(store, o.seq), asio::use_future);
            if (rig.run_until([&] { return HeldFileStoreScene::ready(read_fut); })) {
                o.store = read_fut.get();
            }
        }
        if (session) {
            o.session_next_out = session_test_access::seqnum_mgr(*session).peek_outbound();
            o.state = session->state();
        }
    }
    app->on_logout = nullptr;
    session.reset();
    scene.finish();
    o.peer_read_ended = rig.run_until([&] { return rig.peer.read_ended; });
    std::string const type = in == SessionInput::app_send ? "D" : "5";
    for (auto const& f : scene.peer_frames_from(rx_mark)) {
        if (plain_rig::field_value(f, "35") == type &&
            plain_rig::field_value(f, "34") == std::to_string(o.seq)) {
            ++o.peer_frames_at_seq;
        }
    }
    o.setup_miss = scene.setup_miss;
    o.stop_completed = scene.stop_completed;
    std::cout << "FS-RECORD " << cell << ": count=" << o.count << " markers=" << o.markers
              << " seq=" << o.seq << " graceful_before/after=" << o.graceful_ready_before << "/"
              << result_text(o.graceful) << " terminal_before/after=" << o.terminal_ready_before
              << "/" << result_text(o.terminal) << " send_before/after=" << o.send_ready_before
              << "/" << result_text(o.send) << " store_at_seq=[" << o.store.text()
              << "] session_next_out=" << o.session_next_out
              << " state=" << static_cast<int>(o.state)
              << " peer_frames_at_seq=" << o.peer_frames_at_seq
              << " peer_read_ended=" << o.peer_read_ended << " bodies=" << o.bodies << "/"
              << o.bodies_after_let_go << " stop_completed=" << o.stop_completed << "\n";
    return o;
}

void expect_engine_gate(EngineOutcome const& o, SessionInput in) {
    ASSERT_TRUE(o.exercised(in)) << "the path was not exercised: " << o.text();
    ASSERT_TRUE(o.stop_completed) << "Engine::stop did not complete";
}

void expect_engine_logout(EngineOutcome const& o, SessionInput in) {
    EXPECT_FALSE(o.graceful_ready_before) << "close(graceful) returned before the job was let go";
    if (in == SessionInput::escalation) {
        EXPECT_FALSE(o.terminal_ready_before)
            << "close(terminal) returned before the job was let go";
    }
    EXPECT_TRUE(o.store.read && !o.store.retrieve_error && o.store.types == "5")
        << "the Logout does not read back from the store: " << o.store.text();
    EXPECT_EQ(o.store.next_out, std::optional<seqnum_t>{o.seq + 1})
        << "the store's next outbound number";
    EXPECT_EQ(o.session_next_out, o.seq + 1) << "the session's next outbound number";
    // fixpp#584: the transmit step after a store call that completed with a cancellation
    // still recorded. That issue's batch flips these two assertions.
    EXPECT_TRUE(o.peer_read_ended && o.peer_frames_at_seq == 0)
        << "the peer read the Logout, or its read did not end: peer_frames_at_seq="
        << o.peer_frames_at_seq << " peer_read_ended=" << o.peer_read_ended;
    EXPECT_EQ(o.state, fsm_state::Disconnected) << "the session's state after the closes";
}

void expect_engine_send(EngineOutcome const& o) {
    EXPECT_FALSE(o.send_ready_before) << "Engine::send returned before the job was let go";
    EXPECT_TRUE(o.store.read && !o.store.retrieve_error && o.store.types == "D")
        << "the frame does not read back from the store: " << o.store.text();
    EXPECT_EQ(o.store.next_out, std::optional<seqnum_t>{o.seq + 1})
        << "the store's next outbound number";
    EXPECT_EQ(o.session_next_out, o.seq + 1) << "the session's next outbound number";
    // fixpp#584: the transmit step after a store call that completed with a cancellation
    // still recorded. That issue's batch flips these three assertions.
    EXPECT_TRUE(o.send.has_value() && !o.send->has_value() &&
                o.send->error() == error::dispatch_aborted)
        << "Engine::send " << result_text(o.send);
    EXPECT_TRUE(o.peer_read_ended && o.peer_frames_at_seq == 0)
        << "the peer read the frame, or its read did not end: peer_frames_at_seq="
        << o.peer_frames_at_seq << " peer_read_ended=" << o.peer_read_ended;
    EXPECT_EQ(o.state, fsm_state::Disconnected) << "the session's state after the send";
}

void expect_body_once_after_let_go(EngineOutcome const& o) {
    EXPECT_EQ(o.bodies, 1) << "the store's body did not begin exactly once";
    EXPECT_EQ(o.bodies_after_let_go, 1) << "the store's body did not begin after the release";
}

// ── D-598-1, D-598-3: the graceful Logout and an application's send (TFS.12, TFS.13) ─

TEST(OffloadEngineSession, GracefulLogout_ByTheGraceTimer_IsWrittenAndNotSent) {
    auto const o =
        run_engine_session(SessionInput::grace_timer, FileExec::two_threads_write_delayed,
                           "TFS.12 plain, grace timer");
    expect_engine_gate(o, SessionInput::grace_timer);
    expect_engine_logout(o, SessionInput::grace_timer);
    expect_body_once_after_let_go(o);
    EXPECT_EQ(o.count, 1) << "a job beyond the write's own was handed to the file executor";
}

TEST(OffloadEngineSession, GracefulLogout_ByAnEscalatingTerminalClose_IsWrittenAndNotSent) {
    auto const o = run_engine_session(SessionInput::escalation, FileExec::two_threads_write_delayed,
                                      "TFS.12 plain, escalation");
    expect_engine_gate(o, SessionInput::escalation);
    expect_engine_logout(o, SessionInput::escalation);
    expect_body_once_after_let_go(o);
    EXPECT_EQ(o.count, 1) << "a job beyond the write's own was handed to the file executor";
}

TEST(OffloadEngineSession, ApplicationSend_IsStoredAndNotSent) {
    auto const o = run_engine_session(SessionInput::app_send, FileExec::two_threads_write_delayed,
                                      "TFS.13 plain");
    expect_engine_gate(o, SessionInput::app_send);
    expect_engine_send(o);
    expect_body_once_after_let_go(o);
    EXPECT_EQ(o.count, 1) << "a job beyond the write's own was handed to the file executor";
}

// ── D-598-1, D-598-3, control: the session scenes on one thread (TFS.14) ─────────────

TEST(OffloadEngineSession, Control_OneThread_GracefulLogout_ByTheGraceTimer) {
    auto const o =
        run_engine_session(SessionInput::grace_timer, FileExec::one_thread_held_by_the_cell,
                           "TFS.14 plain, grace timer");
    expect_engine_gate(o, SessionInput::grace_timer);
    expect_engine_logout(o, SessionInput::grace_timer);
    expect_body_once_after_let_go(o);
}

TEST(OffloadEngineSession, Control_OneThread_GracefulLogout_ByAnEscalatingTerminalClose) {
    auto const o =
        run_engine_session(SessionInput::escalation, FileExec::one_thread_held_by_the_cell,
                           "TFS.14 plain, escalation");
    expect_engine_gate(o, SessionInput::escalation);
    expect_engine_logout(o, SessionInput::escalation);
    expect_body_once_after_let_go(o);
}

TEST(OffloadEngineSession, Control_OneThread_ApplicationSend) {
    auto const o = run_engine_session(SessionInput::app_send, FileExec::one_thread_held_by_the_cell,
                                      "TFS.14 plain, application send");
    expect_engine_gate(o, SessionInput::app_send);
    expect_engine_send(o);
    expect_body_once_after_let_go(o);
}

}  // namespace
