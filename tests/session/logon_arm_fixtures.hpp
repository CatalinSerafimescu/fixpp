// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/logon_arm_fixtures.hpp — test-only.
//
// A directly driven acceptor Session: no Engine. The cell's own coroutines call open(),
// on_inbound_frame() and close() on one io_context, which the test thread runs, and
// the session writes through its transport callback into `sent`. Its store is a
// RecordingStore (tests/support/recording_store.hpp), so a cell can hook, hold or fail
// one store call of a Logon arm.
//
// .specify/536-537-538-recovery-liveness.md §5, the rows of D-CAN-5 and "D-CAN-1, the
// hydration flag". Two forms, chosen by Options:
//
//   the 789 path        a volatile store seeded with stored application messages, and
//                       NextExpectedMsgSeqNum(789) enabled: the peer's Logon asks for a
//                       replay, so the NotConnected arm runs the resend walk
//                       (seed_outbound(), then logon("789=<n>|")).
//   a persistent store  a FileStore on a file I/O executor of its own: the NotConnected
//                       arm reads the store to hydrate before it checks the Logon's
//                       sequence number.
//
// The session's HeartBtInt is zero, so no liveness loop runs, and its clock is a
// mock_clock that nothing advances: no timer of the session can end a wait of a cell
// except close(graceful)'s grace wait, which is real time and is the configured
// logout_disconnect_timeout_ms. A cell that needs another configuration changes it
// through Options::tweak_cfg, and then states its own bounds.
#pragma once

#include <gtest/gtest.h>

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "support/calling_thread_readings.hpp"
#include "support/keep_running.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_store.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"

namespace fixpp::test_support::logon_arm {

class DirectAcceptor {
public:
    struct Options {
        bool file_store = false;  // a FileStore (persistent) in place of a MemoryStore
        bool enable_789 = false;
        // close(graceful)'s grace wait, in real time.
        std::uint32_t logout_disconnect_timeout_ms = 200;
        // What the store's factory reports as yields_persistent_store(), when set;
        // otherwise the inner factory's own answer.
        std::optional<bool> reports_persistent = std::nullopt;
        // The session's Application, in place of `app`.
        std::shared_ptr<fixpp::session::Application> application = nullptr;
        // Adjusts the session's configuration after the constructor has built it and
        // before the session is made. A transport callback it installs replaces the
        // one that fills `sent`.
        std::function<void(fixpp::session::SessionConfig&)> tweak_cfg = nullptr;
        // With file_store: the FileStore's file I/O executor, in place of the fixture's own
        // pool of one thread. It must outlive the fixture.
        std::optional<asio::any_io_executor> file_io_executor = std::nullopt;
    };

    // First, so destroyed last: the session's executor is on it.
    asio::io_context ioc;

private:
    // With a FileStore: keeps `ioc` from stopping between two pumps when an offload's work
    // is released on the pool thread (tests/support/keep_running.hpp). Released first in the
    // destructor, before the drain, which waits for the io_context to stop.
    std::optional<keep_running> keep_;
    // The FileStore's file I/O executor; it outlives the session and its store, and the
    // directory is removed after both.
    asio::thread_pool file_pool_{1};
    struct temp_dir {
        std::filesystem::path path = unique_temp_dir("b28_logon_arm");
        temp_dir() = default;
        ~temp_dir() { (void)try_remove_temp_dir(path); }
        temp_dir(temp_dir const&) = delete;
        temp_dir& operator=(temp_dir const&) = delete;
    } dir_;

public:
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::shared_ptr<plain_rig::CountingApp> app = std::make_shared<plain_rig::CountingApp>();
    fixpp::core::EngineConfig engine;
    fixpp::session::SessionConfig cfg;
    std::shared_ptr<StoreCallLog> log;
    std::vector<std::string> sent;  // each frame the session gave its transport callback
    std::unique_ptr<fixpp::session::Session> session;

    explicit DirectAcceptor(Options const& o) {
        using namespace std::chrono;
        clock = std::make_shared<fixpp::core::mock_clock>(
            system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
            ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        engine.application =
            o.application ? o.application : std::shared_ptr<fixpp::session::Application>{app};

        cfg.role = fixpp::session::session_role::acceptor;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = seconds{0};
        cfg.logout_disconnect_timeout_ms = o.logout_disconnect_timeout_ms;
        cfg.security_profile = make_minimal_security_profile();
        cfg.dictionary = make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.enable_next_expected_msg_seq_num = o.enable_789;
        cfg.transport_send = [this](std::span<const std::byte> f) {
            sent.emplace_back(reinterpret_cast<char const*>(f.data()), f.size());
        };
        std::shared_ptr<RecordingStoreFactory> factory;
        if (o.file_store) {
            keep_.emplace(keep_running_for(ioc));
            fixpp::session::FileStore::Config file_cfg;
            file_cfg.directory = dir_.path;
            file_cfg.sender_comp_id = cfg.sender_comp_id;
            file_cfg.target_comp_id = cfg.target_comp_id;
            file_cfg.file_io_executor = o.file_io_executor
                                            ? *o.file_io_executor
                                            : asio::any_io_executor{file_pool_.get_executor()};
            factory = std::make_shared<RecordingStoreFactory>(file_cfg);
        } else {
            factory = std::make_shared<RecordingStoreFactory>(
                std::make_shared<fixpp::session::MemoryStoreFactory>(
                    fixpp::session::MemoryStore::Config{
                        .policy = fixpp::session::capacity_policy::unbounded}));
        }
        log = factory->log;
        factory->reports_persistent = o.reports_persistent;
        cfg.store_factory = std::move(factory);
        if (o.tweak_cfg) o.tweak_cfg(cfg);
        session = std::make_unique<fixpp::session::Session>(engine, cfg);
    }

    DirectAcceptor(DirectAcceptor const&) = delete;
    DirectAcceptor& operator=(DirectAcceptor const&) = delete;

    // A destructor's body runs while every member is alive. A hold still in flight is
    // released, and whatever is still suspended on the io_context is resumed here, over
    // live storage. cancel_and_drain_or_report carries its own catch.
    ~DirectAcceptor() {
        keep_.reset();
        log->release_hold();
        cancel_and_drain_or_report(ioc, *clock, "logon_arm::DirectAcceptor");
    }

    template <class Fut>
    [[nodiscard]] static bool ready(Fut const& fut) {
        return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }
    template <class Pred>
    [[nodiscard]] bool run_until(Pred pred,
                                 std::chrono::steady_clock::duration budget = kPumpBudget) {
        return pump_until(ioc, std::move(pred), budget, kPumpSlice, nullptr);
    }

    // open(): an acceptor is NotConnected afterwards.
    [[nodiscard]] bool open() {
        auto fut = asio::co_spawn(ioc, session->open(), asio::use_future);
        if (!run_until([&] { return ready(fut); })) return false;
        return fut.get().has_value();
    }

    // The 789 path's seed: stores `count` application messages outbound, from 1, and
    // sets the session's next outbound number past them. For the volatile store only: a
    // persistent store's counters are read from the store when the Logon arrives.
    [[nodiscard]] bool seed_outbound(unsigned count) {
        auto* const store = fixpp::session::session_test_access::store(*session);
        if (store == nullptr) return false;
        auto fut = asio::co_spawn(
            ioc,
            [this, store, count]() -> asio::awaitable<bool> {
                for (unsigned seq = 1; seq <= count; ++seq) {
                    auto const bytes = plain_rig::to_bytes(stored_app(seq));
                    auto r = co_await store->store(seq, std::span<const std::byte>{bytes},
                                                   fixpp::session::direction_t::outbound);
                    if (!r) co_return false;
                }
                co_return true;
            },
            asio::use_future);
        if (!run_until([&] { return ready(fut); }) || !fut.get()) return false;
        fixpp::session::seqnum_manager_test_access::set_counters(
            fixpp::session::session_test_access::seqnum_mgr(*session), 1, count + 1);
        return true;
    }

    // SendingTime(52) for a frame stamped now on the mock clock.
    [[nodiscard]] std::string sending_time() const {
        std::array<char, 32> buf{};
        auto const r = fixpp::core::utc_time_to_fix_string(
            clock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        return r ? std::string{r->data(), r->size()} : std::string{};
    }
    // A frame from the peer ("TW") to the session ("ISLD"), stamped now.
    [[nodiscard]] std::vector<std::byte> msg(std::string_view msg_type, std::uint32_t seq,
                                             std::string_view extra = {}) const {
        return plain_rig::to_bytes(plain_rig::message(cfg.begin_string, msg_type, seq, "TW", "ISLD",
                                                      sending_time(), extra));
    }
    // The peer's Logon at MsgSeqNum(34) 1, with `extra` after its HeartBtInt.
    [[nodiscard]] std::vector<std::byte> logon(std::string_view extra = {}) const {
        return msg("A", 1,
                   "98=0\x01"
                   "108=0\x01" +
                       std::string{extra});
    }
    // An application message from the session at `seq`, as its store would hold it.
    [[nodiscard]] std::string stored_app(std::uint32_t seq) const {
        return plain_rig::message(cfg.begin_string, "D", seq, "ISLD", "TW", sending_time(),
                                  "11=ORD" + std::to_string(seq) + "\x01");
    }

    // Hands `frame` to on_inbound_frame from a coroutine of the cell's, bound to
    // `cancel`. `frame` and `out` must outlive the returned future's readiness.
    [[nodiscard]] std::future<void> feed(std::span<const std::byte> frame,
                                         CallingThreadReadings& out,
                                         asio::cancellation_signal& cancel,
                                         std::function<void()> after_call = {}) {
        return asio::co_spawn(
            ioc,
            await_and_read_thread([this, frame] { return session->on_inbound_frame(frame); }, out,
                                  std::move(after_call)),
            asio::bind_cancellation_slot(cancel.slot(), asio::use_future));
    }
    // The same, with no cancellation of the cell's, run until the call has completed.
    [[nodiscard]] bool feed_plain(std::span<const std::byte> frame, CallingThreadReadings& out) {
        asio::cancellation_signal none;
        auto fut = feed(frame, out, none);
        return run_until([&] { return ready(fut); }) && out.completed;
    }

    // The driver's close(), started without waiting for it. Called on the io_context's
    // thread from inside a handler, it runs to its first suspension before this returns.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> spawn_close(
        fixpp::session::close_mode mode) {
        return asio::co_spawn(ioc, session->close(mode), asio::use_future);
    }

    [[nodiscard]] std::vector<fixpp::session::fsm_state> ring() const {
        auto const r = session->fsm_visit_history();
        return {r.begin(), r.end()};
    }

    // How many frames of `msg_type` the session has written.
    [[nodiscard]] std::size_t sent_count(std::string_view msg_type) const {
        std::size_t n = 0;
        for (auto const& f : sent)
            n += static_cast<std::size_t>(plain_rig::field_value(f, "35") == msg_type);
        return n;
    }
};

// The states in `ring`, for a failure message.
[[nodiscard]] inline std::string ring_text(std::vector<fixpp::session::fsm_state> const& ring) {
    std::string t;
    for (auto const st : ring) t += std::to_string(static_cast<int>(st)) + " ";
    return t.empty() ? std::string{"(empty)"} : t;
}

}  // namespace fixpp::test_support::logon_arm
