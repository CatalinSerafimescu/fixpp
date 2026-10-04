// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/engine_establishment_timeout_test.cpp
//
// 093-inbound-frame-dispositions, User Story 2: establishment cannot hang (FR-006;
// contract C-4; data-model E-6, E-7). Phase (b) of the establishment deadline: a
// Session that has not yet reached Active is closed at T = logon_timeout_ms after the
// connection began, on EngineConfig::clock, with session_event_establishment_timeout
// and a log record. Phase (a), the acceptor's first-frame read, is in
// engine_firstframe_test.cpp.
//
// The cells drive a plaintext Engine on a mock engine clock (plain_engine_rig.hpp),
// or, for the readable-across-T cells, an initiator whose transport is a scripted
// double (tests/support/transport_double.hpp). "At T, not before" means: still open,
// with no timeout event, after the clock is advanced to T - 1 ms and the io_context
// drained; closed after it is advanced to T. Every observation is taken before
// Engine::stop(), and none is fatal until stop() has returned.
//
// Mock-clock advances in this file move the deadline's clock. The deadline is an
// absolute instant fixed when the connection began, and Clock::sleep_until fires at
// once on a deadline already passed, so an advance that lands before the pump has
// armed its race is not lost (KIND D, ci/mock-clock-staging-sweep.sh); the pump's
// loop-head test reads the same clock.
#include <gtest/gtest.h>

#include <algorithm>
#include <asio/any_io_executor.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "plain_engine_rig.hpp"
#include "support/session_test_access.hpp"
#include "support/transport_double.hpp"

namespace {

namespace pr = fixpp::test_support::plain_rig;
using fixpp::session::fsm_state;
using fixpp::session::Session;
using fixpp::session::session_role;
using fixpp::session::SessionConfig;
using namespace std::chrono_literals;

// The two 093 symbols these cells name, in one place each.
void set_logon_timeout(SessionConfig& c, std::chrono::milliseconds t) {
    c.logon_timeout_ms = static_cast<std::uint32_t>(t.count());
}

std::size_t timeout_events(Session const& s) {
    std::size_t n = 0;
    for (auto const& ev : s.recent_events()) {
        if (std::holds_alternative<fixpp::session::session_event_establishment_timeout>(ev)) ++n;
    }
    return n;
}

std::uint32_t next_inbound(Session& s) {
    return fixpp::session::session_test_access::seqnum_mgr(s).next_inbound_unsafe();
}

// What a cell reads from the rig at one instant.
struct Snapshot {
    bool has_session = false;
    fsm_state state = fsm_state::NotConnected;
    bool read_ended = false;  // the peer's read ended: the engine closed the connection
    std::size_t timeouts = 0;
};

Snapshot snapshot(pr::Rig& rig) {
    Snapshot o;
    o.read_ended = rig.peer.read_ended;
    if (auto const s = rig.session()) {
        o.has_session = true;
        o.state = s->state();
        o.timeouts = timeout_events(*s);
    }
    return o;
}

// ── Q-16: the pre-Active contexts of phase (b) ──────────────────────────────
//
// The initiator after its Logon; the acceptor after a matching first frame that leaves
// it pre-Active: a refused Logon (no HeartBtInt(108)) or a non-Logon frame. Both
// acceptor contexts leave the session in Disconnected with its transport open.
enum class Ctx { initiator_after_logon, acceptor_after_refused_logon, acceptor_after_non_logon };

char const* name_of(Ctx c) {
    switch (c) {
        case Ctx::initiator_after_logon:
            return "initiator after its Logon";
        case Ctx::acceptor_after_refused_logon:
            return "acceptor after a refused Logon";
        case Ctx::acceptor_after_non_logon:
            return "acceptor after a non-Logon first frame";
    }
    return "?";
}

fsm_state pre_active_state(Ctx c) {
    return c == Ctx::initiator_after_logon ? fsm_state::LogonSent : fsm_state::Disconnected;
}

// Starts the rig in context `c` with T = `t` and waits for the context's state.
bool reach(pr::Rig& rig, Ctx c, std::chrono::milliseconds t) {
    auto cfg =
        rig.cfg(c == Ctx::initiator_after_logon ? session_role::initiator : session_role::acceptor);
    set_logon_timeout(cfg, t);
    if (!rig.start(std::move(cfg))) return false;
    if (c == Ctx::initiator_after_logon) {
        return rig.run_until([&] {
            return !pr::frames_of_type(rig.peer.received, "A").empty() &&
                   rig.state() == fsm_state::LogonSent;
        });
    }
    if (!rig.connect_peer()) return false;
    if (c == Ctx::acceptor_after_refused_logon) {
        rig.peer.send(rig.msg("A", 1, "98=0\x01"));
    } else {
        rig.peer.send(rig.heartbeat(1));
    }
    return rig.run_until([&] { return rig.state() == fsm_state::Disconnected; });
}

// Bytes no Framer frames: no "8=FIX" anywhere.
std::string garbage() { return std::string(200, 'G') + "\x01" + std::string(100, 'q'); }

// One phase (b) cell: reach `c` with T, optionally stream garbage, then observe at
// T - 1 ms and at T.
void run_q16(Ctx c, bool send_garbage) {
    SCOPED_TRACE(name_of(c));
    SCOPED_TRACE(send_garbage ? "garbage-only peer" : "silent peer");
    constexpr auto kT = 2000ms;
    pr::Rig rig;
    bool const up = reach(rig, c, kT);
    bool delivered = true;
    if (up && send_garbage) delivered = rig.deliver(garbage());

    Snapshot before;
    Snapshot after;
    bool closed = false;
    if (up && delivered) {
        // KIND D (ci/mock-clock-staging-sweep.sh): the deadline is a stored anchor
        // predating the advance; nothing must fire at T - 1 ms.
        rig.clock->advance(kT - 1ms);
        rig.settle();
        before = snapshot(rig);
        // KIND D (ci/mock-clock-staging-sweep.sh): the deadline is a stored anchor
        // predating the advance, so a late arm fires at once.
        rig.clock->advance(1ms);
        closed = rig.run_until([&] {
            auto const o = snapshot(rig);
            return o.read_ended && o.timeouts == 1U;
        });
        after = snapshot(rig);
    }
    rig.stop();

    ASSERT_TRUE(up) << "the session did not reach its pre-Active context";
    ASSERT_TRUE(delivered) << "the peer's garbage was not written";
    EXPECT_TRUE(before.has_session);
    EXPECT_EQ(before.state, pre_active_state(c)) << "pre-Active at T - 1 ms";
    EXPECT_FALSE(before.read_ended) << "closed before T";
    EXPECT_EQ(before.timeouts, 0U) << "a timeout event before T";
    EXPECT_TRUE(closed) << "not closed at T";
    EXPECT_TRUE(after.read_ended) << "the transport is closed at T";
    EXPECT_EQ(after.timeouts, 1U) << "one session_event_establishment_timeout";
    EXPECT_EQ(after.state, fsm_state::Disconnected) << "the FSM ends in Disconnected";
}

TEST(EstablishmentTimeoutQ16, InitiatorAfterLogon_GarbageOnlyPeer_ClosedAtTNotBefore) {
    run_q16(Ctx::initiator_after_logon, true);
}
TEST(EstablishmentTimeoutQ16, InitiatorAfterLogon_SilentPeer_ClosedAtTNotBefore) {
    run_q16(Ctx::initiator_after_logon, false);
}
TEST(EstablishmentTimeoutQ16, AcceptorAfterRefusedLogon_GarbageOnlyPeer_ClosedAtTNotBefore) {
    run_q16(Ctx::acceptor_after_refused_logon, true);
}
TEST(EstablishmentTimeoutQ16, AcceptorAfterRefusedLogon_SilentPeer_ClosedAtTNotBefore) {
    run_q16(Ctx::acceptor_after_refused_logon, false);
}
TEST(EstablishmentTimeoutQ16, AcceptorAfterNonLogonFrame_GarbageOnlyPeer_ClosedAtTNotBefore) {
    run_q16(Ctx::acceptor_after_non_logon, true);
}
TEST(EstablishmentTimeoutQ16, AcceptorAfterNonLogonFrame_SilentPeer_ClosedAtTNotBefore) {
    run_q16(Ctx::acceptor_after_non_logon, false);
}

// ── Q-16: the timeout's log record (data-model E-12) ────────────────────────

// The record's format, spelled out here rather than taken from the session, so a change
// to the record fails this cell.
constexpr char kTimeoutRecordFormat[] = "session not established within logon_timeout_ms={}";

class CaptureSink final : public fixpp::log::Sink {
public:
    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const& rec) noexcept override {
        std::scoped_lock lk{mu_};
        records_.push_back(rec);
    }
    void flush(std::chrono::milliseconds /*deadline*/) noexcept override {}
    void close() noexcept override {}
    [[nodiscard]] std::vector<fixpp::log::Record> records() const {
        std::scoped_lock lk{mu_};
        return records_;
    }

private:
    mutable std::mutex mu_;
    std::vector<fixpp::log::Record> records_;
};

TEST(EstablishmentTimeoutQ16, TheTimeoutIsLoggedOnceWithTAndTheSessionTraceContext) {
    auto owned = std::make_unique<CaptureSink>();
    CaptureSink* const sink = owned.get();
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks(std::pmr::get_default_resource());
    sinks.push_back(std::move(owned));
    auto const logger =
        std::make_shared<fixpp::log::Logger>(fixpp::log::LoggerConfig{}, std::move(sinks));
    fixpp::otel::trace_context tc{};
    tc.trace_id.fill(std::byte{0x6B});
    tc.span_id.fill(std::byte{0x2D});

    constexpr auto kT = 1500ms;
    pr::Rig rig;
    auto cfg = rig.cfg(session_role::initiator);
    set_logon_timeout(cfg, kT);
    cfg.logger_override = logger;
    cfg.initial_trace_context = tc;
    bool const up = rig.start(std::move(cfg)) &&
                    rig.run_until([&] { return rig.state() == fsm_state::LogonSent; });
    bool closed = false;
    if (up) {
        // KIND D (ci/mock-clock-staging-sweep.sh): the deadline is a stored anchor
        // predating the advance, so a late arm fires at once.
        rig.clock->advance(kT);
        closed = rig.run_until([&] { return rig.peer.read_ended; });
    }
    rig.stop();
    (void)logger->shutdown();
    std::vector<fixpp::log::Record> records;
    for (auto const& r : sink->records()) {
        if (r.format_id == FIXPP_FORMAT_ID(kTimeoutRecordFormat)) records.push_back(r);
    }

    ASSERT_TRUE(up);
    EXPECT_TRUE(closed);
    ASSERT_EQ(records.size(), 1U) << "one timeout record";
    auto const& r = records[0];
    EXPECT_EQ(r.level, fixpp::log::Level::warn);
    EXPECT_EQ(r.category, fixpp::log::cat::session);
    ASSERT_EQ(r.arg_count, 1U);
    EXPECT_EQ(r.args[0].u64, 1500U) << "the record carries T";
    EXPECT_EQ(r.trace_id, (reinterpret_cast<std::array<std::uint8_t, 16> const&>(tc.trace_id)))
        << "the record carries the session's trace_id";
}

// ── Q-16: readable across T (the scripted double) ───────────────────────────
//
// An engine-managed initiator whose transport is ScriptedReadTransport: every read
// completes at initiation, so in the pump's race the read arm is ready first. Each read
// is issued at clock t and moves the clock to t + 1 s before serving its chunk, so the
// read issued at T - 1 s serves its chunk at T. With T = 3 s: reads issued at 0, 1 and
// 2 s are served; the chunk served at T is not delivered; the next loop head, at T,
// closes the connection. The script is twice that long and ends in EOF, so a pump that
// ignores the deadline reads past T and stops at EOF, without the event.
constexpr auto kScriptT = 3000ms;
constexpr std::size_t kScriptChunks = 6;

struct ScriptRun {
    bool up = false;
    bool ended = false;
    std::vector<fixpp::core::steady_time_point> issued;  // the clock at each read's issue
    fixpp::core::steady_time_point deadline{};
    std::size_t reads_served = 0;
    bool closed = false;
    fsm_state state = fsm_state::NotConnected;
    std::size_t timeouts = 0;
    std::uint64_t garbled = 0;
};

ScriptRun run_script(std::vector<std::string> const& chunks_text) {
    ScriptRun out;
    pr::Rig rig;
    auto stream = std::make_shared<fixpp::session::test::ScriptedStream>();
    for (auto const& c : chunks_text) stream->chunks.push_back(pr::to_bytes(c));
    stream->on_read = [&out, clock = rig.clock](std::size_t) {
        out.issued.push_back(clock->steady_now());
        // KIND D (ci/mock-clock-staging-sweep.sh): the bytes of this read arrive one
        // second after it was issued; the deadline is a stored anchor predating it.
        clock->advance(1s);
    };
    out.deadline = rig.clock->steady_now() + kScriptT;
    auto cfg = rig.cfg(session_role::initiator);
    set_logon_timeout(cfg, kScriptT);
    cfg.transport_factory_override =
        std::make_shared<fixpp::session::test::ScriptedReadTransportFactory>(stream);
    out.up = rig.start(std::move(cfg));
    if (out.up) {
        out.ended = rig.run_until([&] { return stream->closed; });
        rig.settle();
    }
    out.reads_served = stream->reads_served;
    out.closed = stream->closed;
    if (auto const s = rig.session()) {
        out.state = s->state();
        out.timeouts = timeout_events(*s);
        out.garbled = s->garbled_frame_count();
    }
    rig.stop();
    return out;
}

void expect_closed_at_the_first_loop_head_after_t(ScriptRun const& r) {
    ASSERT_TRUE(r.up);
    EXPECT_TRUE(r.ended) << "the transport was never closed";
    EXPECT_TRUE(std::none_of(r.issued.begin(), r.issued.end(), [&](auto t) {
        return t >= r.deadline;
    })) << "a read was issued at or after T";
    EXPECT_EQ(r.reads_served, 3U) << "the reads issued before T, and no other";
    EXPECT_EQ(r.timeouts, 1U) << "closed by the deadline, not by the script's EOF";
    EXPECT_EQ(r.state, fsm_state::Disconnected);
}

TEST(EstablishmentTimeoutQ16, ReadableAcrossT_GarbageStream_ClosedAtTheFirstLoopHeadAfterT) {
    std::vector<std::string> chunks(kScriptChunks, garbage());
    auto const r = run_script(chunks);
    expect_closed_at_the_first_loop_head_after_t(r);
}

TEST(EstablishmentTimeoutQ16, ReadableAcrossT_ValidNonLogonFrames_ClosedAtTheFirstLoopHeadAfterT) {
    // Heartbeats from the acceptor ("TW"), 34=1 on. The first takes the initiator from
    // LogonSent to Disconnected with its transport open; the rest are read and ignored.
    std::vector<std::string> chunks;
    for (std::uint32_t i = 1; i <= kScriptChunks; ++i) {
        chunks.push_back(pr::message("FIX.4.2", "0", i, "TW", "ISLD", "20240101-00:00:00.000"));
    }
    auto const r = run_script(chunks);
    expect_closed_at_the_first_loop_head_after_t(r);
}

TEST(EstablishmentTimeoutQ16, ReadableAcrossT_NoFrameIsDeliveredOnceTheDeadlineHasPassed) {
    // Frames whose third field is not MsgType(35): framed, then disregarded on delivery
    // in LogonSent (contract C-2 step 1), which counts each one. The count is therefore
    // the number of frames delivered: the two chunks served before T, not the one served
    // at T.
    std::vector<std::string> chunks;
    for (std::uint32_t i = 1; i <= kScriptChunks; ++i) {
        std::string const body = "34=" + std::to_string(i) +
                                 "\x01"
                                 "35=0\x01"
                                 "49=TW\x01"
                                 "52=20240101-00:00:00.000\x01"
                                 "56=ISLD\x01";
        chunks.push_back(pr::frame("FIX.4.2", body));
    }
    auto const r = run_script(chunks);
    expect_closed_at_the_first_loop_head_after_t(r);
    EXPECT_EQ(r.garbled, 2U) << "only the frames served before T were delivered";
}

// ── Q-16: the initial_bytes drain tests the deadline before each delivery ────
//
// The acceptor's first-frame read returns the first frame and the frames coalesced
// after it; those reach the pump as initial_bytes, and the pump drains them before its
// first read. The store factory below advances the engine clock while open() mints the
// store, which runs after the first-frame read and before the pump, so with an advance
// past T the drain starts after T. Every frame is one whose third field is not
// MsgType(35): in NotConnected it is disregarded on delivery and counted (contract C-2
// step 1), so the count is the number of frames delivered. The accept loop delivers the
// first frame itself, before the pump; the drain delivers the rest.

// A MemoryStoreFactory (unbounded, so it reserves no slab against the engine's store
// memory cap) that advances the engine's mock clock by `by` each time it mints.
class ClockAdvancingStoreFactory final : public fixpp::session::MessageStoreFactory {
public:
    ClockAdvancingStoreFactory(std::shared_ptr<fixpp::core::mock_clock> clock,
                               std::chrono::nanoseconds by)
        : clock_{std::move(clock)}, by_{by} {}

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return false; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view sender, std::string_view target, std::pmr::memory_resource* mr,
        std::size_t max_store_memory_bytes,
        asio::any_io_executor file_io_executor) noexcept override {
        // KIND A (ci/mock-clock-staging-sweep.sh): a time stamp; what consumes it is the
        // pump's synchronous loop-head read of steady_now(), not a waiter.
        clock_->advance(by_);
        return inner_.make(sender, target, mr, max_store_memory_bytes, std::move(file_io_executor));
    }

private:
    std::shared_ptr<fixpp::core::mock_clock> clock_;
    std::chrono::nanoseconds by_;
    fixpp::session::MemoryStoreFactory inner_{unbounded()};

    static fixpp::session::MemoryStore::Config unbounded() noexcept {
        fixpp::session::MemoryStore::Config c;
        c.policy = fixpp::session::capacity_policy::unbounded;
        return c;
    }
};

// A frame from the peer whose third field is MsgSeqNum(34), not MsgType(35).
std::string misordered(std::uint32_t seq) {
    std::string const body = "34=" + std::to_string(seq) +
                             "\x01"
                             "35=0\x01"
                             "49=TW\x01"
                             "52=20240101-00:00:00.000\x01"
                             "56=ISLD\x01";
    return pr::frame("FIX.4.2", body);
}

struct DrainRun {
    bool up = false;
    bool settled = false;
    std::uint64_t delivered = 0;
    std::size_t timeouts = 0;
    bool read_ended = false;
};

// Three frames in one write, so the first-frame read takes the first and hands the other
// two to the pump as initial_bytes; open() advances the clock by `advance_in_open`.
DrainRun run_initial_drain(std::chrono::nanoseconds advance_in_open, bool expect_timeout) {
    DrainRun out;
    pr::Rig rig;
    auto cfg = rig.cfg();
    set_logon_timeout(cfg, 2000ms);
    cfg.store_factory = std::make_shared<ClockAdvancingStoreFactory>(rig.clock, advance_in_open);
    out.up = rig.start(std::move(cfg)) && rig.connect_peer();
    if (out.up) {
        rig.peer.send(misordered(1) + misordered(2) + misordered(3));
        out.settled = rig.run_until([&] {
            auto const s = rig.session();
            if (!s) return false;
            return expect_timeout ? (rig.peer.read_ended && timeout_events(*s) == 1U)
                                  : s->garbled_frame_count() == 3U;
        });
        rig.settle();
        if (auto const s = rig.session()) {
            out.delivered = s->garbled_frame_count();
            out.timeouts = timeout_events(*s);
        }
        out.read_ended = rig.peer.read_ended;
    }
    rig.stop();
    return out;
}

TEST(EstablishmentTimeoutQ16,
     InitialBytesDrain_NoCoalescedFrameIsDeliveredOnceTheDeadlineHasPassed) {
    auto const r = run_initial_drain(3000ms, /*expect_timeout=*/true);
    ASSERT_TRUE(r.up);
    EXPECT_TRUE(r.settled) << "not closed by the deadline";
    EXPECT_EQ(r.delivered, 1U) << "only the first frame, which the accept loop delivers; the "
                                  "coalesced frames the drain holds are not delivered after T";
    EXPECT_EQ(r.timeouts, 1U);
    EXPECT_TRUE(r.read_ended);
}

// The control: with no time passing in open(), the drain delivers both coalesced frames,
// so the cell above sees the frames reach the drain when they may be delivered.
TEST(EstablishmentTimeoutQ16, InitialBytesDrain_CoalescedFramesAreDeliveredBeforeT) {
    auto const r = run_initial_drain(0ms, /*expect_timeout=*/false);
    ASSERT_TRUE(r.up);
    EXPECT_TRUE(r.settled);
    EXPECT_EQ(r.delivered, 3U) << "the first frame and the two coalesced after it";
    EXPECT_EQ(r.timeouts, 0U);
    EXPECT_FALSE(r.read_ended);
}

// ── Q-35: the default T ─────────────────────────────────────────────────────

TEST(EstablishmentTimeoutQ35, DefaultIsTenSecondsAndASilentPeerIsClosedAtTenSeconds) {
    EXPECT_EQ(SessionConfig{}.logon_timeout_ms, 10000U);

    pr::Rig rig;
    bool const up = rig.start(rig.cfg(session_role::initiator)) &&
                    rig.run_until([&] { return rig.state() == fsm_state::LogonSent; });
    Snapshot before;
    bool closed = false;
    if (up) {
        // KIND D (ci/mock-clock-staging-sweep.sh): a stored anchor; nothing fires yet.
        rig.clock->advance(9999ms);
        rig.settle();
        before = snapshot(rig);
        // KIND D (ci/mock-clock-staging-sweep.sh): a stored anchor; a late arm fires.
        rig.clock->advance(1ms);
        closed = rig.run_until([&] {
            auto const o = snapshot(rig);
            return o.read_ended && o.timeouts == 1U;
        });
    }
    rig.stop();

    ASSERT_TRUE(up);
    EXPECT_FALSE(before.read_ended) << "closed before 10 s";
    EXPECT_EQ(before.timeouts, 0U);
    EXPECT_TRUE(closed) << "not closed at 10 s";
}

// ── Q-16's TOML arm (data-model E-7; tasks.md T093) ─────────────────────────
//
// A TOML document sets logon_timeout_ms, and the SessionConfig load_toml_config
// returns for it drives the initiator. It is completed only with what the loader
// leaves to the host (include/fixpp/config/config_bundle.hpp: the executor, and the
// session's dictionary, taken from the bundle's [dictionary]) and with the rig's
// plumbing (the endpoint it listens on and the initial transport_send). Still open
// at T - 1 ms; closed at T.
TEST(EstablishmentTimeoutQ16, TomlLoadedSession_ClosedAtTheLoadedTNotBefore) {
    constexpr auto kT = 1500ms;
    auto const path =
        std::filesystem::temp_directory_path() /
        ("fixpp_093_logon_timeout_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml");
    {
        std::ofstream out{path};
        out << "[clock]\nkind = \"system\"\n"
               "[store]\nkind = \"memory\"\n"
               "[dictionary]\nkind = \"path\"\npath = \"" FIXPP_DICT_DATA_DIR
               "/FIX42.xml\"\n"
               "[[session]]\n"
               "sender_comp_id = \"ISLD\"\n"
               "target_comp_id = \"TW\"\n"
               "begin_string = \"FIX.4.2\"\n"
               "role = \"initiator\"\n"
               "heartbeat_interval = \"30s\"\n"
               "reset_seqnum_policy = \"bilateral_lenient\"\n"
               "logon_timeout_ms = "
            << kT.count()
            << "\n"
               "[session.transport]\nkind = \"plaintext\"\nhost = \"127.0.0.1\"\nport = 1\n"
               "[session.security_profile]\nkind = \"insecure_plain_tcp\"\n";
    }
    pr::Rig rig;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = rig.ioc.get_executor();
    auto loaded = fixpp::config::load_toml_config(path, opts);
    std::error_code rm_ec;
    std::filesystem::remove(path, rm_ec);
    std::string diagnostics;
    if (!loaded) {
        for (auto const& d : loaded.error())
            diagnostics += " [" + d.key_path + ": " + d.message + "]";
    }
    ASSERT_TRUE(loaded.has_value()) << "the TOML document did not load:" << diagnostics;
    ASSERT_EQ(loaded->sessions.size(), 1U);
    ASSERT_EQ(loaded->engine.dictionaries.size(), 1U);
    SessionConfig cfg = loaded->sessions[0].config;
    EXPECT_EQ(cfg.logon_timeout_ms, static_cast<std::uint32_t>(kT.count()));
    cfg.executor_override = rig.ioc.get_executor();
    cfg.dictionary = loaded->engine.dictionaries.front();
    cfg.transport_send = [](std::span<const std::byte>) {};

    bool const up = rig.start(std::move(cfg)) && rig.run_until([&] {
        return !pr::frames_of_type(rig.peer.received, "A").empty() &&
               rig.state() == fsm_state::LogonSent;
    });
    Snapshot before;
    Snapshot after;
    bool closed = false;
    if (up) {
        // KIND D (ci/mock-clock-staging-sweep.sh): the deadline is a stored anchor
        // predating the advance; nothing must fire at T - 1 ms.
        rig.clock->advance(kT - 1ms);
        rig.settle();
        before = snapshot(rig);
        // KIND D (ci/mock-clock-staging-sweep.sh): the deadline is a stored anchor
        // predating the advance, so a late arm fires at once.
        rig.clock->advance(1ms);
        closed = rig.run_until([&] {
            auto const o = snapshot(rig);
            return o.read_ended && o.timeouts == 1U;
        });
        after = snapshot(rig);
    }
    auto const reached = rig.state();
    std::size_t const peer_bytes = rig.peer.received.size();
    rig.stop();

    ASSERT_TRUE(up) << "the TOML-loaded initiator did not reach LogonSent: state "
                    << static_cast<int>(reached) << ", " << peer_bytes << " bytes at the peer";
    EXPECT_FALSE(before.read_ended) << "closed before T";
    EXPECT_EQ(before.timeouts, 0U);
    EXPECT_TRUE(closed) << "not closed at T";
    EXPECT_EQ(after.timeouts, 1U);
}

// ── Q-18: the deadline's clock ──────────────────────────────────────────────

// A clock-wide cancel_sleeps() (any session's Logout reply calls one) during phase (b)
// does not end the wait early.
TEST(EstablishmentTimeoutQ18, AClockWideCancelSleepsDoesNotEndTheWaitEarly) {
    constexpr auto kT = 2000ms;
    pr::Rig rig;
    bool const up = reach(rig, Ctx::initiator_after_logon, kT);
    Snapshot swept;
    Snapshot before;
    bool closed = false;
    if (up) {
        // KIND D (ci/mock-clock-staging-sweep.sh): a stored anchor; nothing fires yet.
        rig.clock->advance(1000ms);
        rig.settle();
        rig.clock->cancel_sleeps();
        rig.settle();
        swept = snapshot(rig);
        // KIND D (ci/mock-clock-staging-sweep.sh): a stored anchor; nothing fires yet.
        rig.clock->advance(kT - 1000ms - 1ms);
        rig.settle();
        before = snapshot(rig);
        // KIND D (ci/mock-clock-staging-sweep.sh): a stored anchor; a late arm fires.
        rig.clock->advance(1ms);
        closed = rig.run_until([&] {
            auto const o = snapshot(rig);
            return o.read_ended && o.timeouts == 1U;
        });
    }
    rig.stop();

    ASSERT_TRUE(up);
    EXPECT_FALSE(swept.read_ended) << "the sweep closed the connection";
    EXPECT_EQ(swept.timeouts, 0U) << "the sweep recorded a timeout";
    EXPECT_EQ(swept.state, fsm_state::LogonSent);
    EXPECT_FALSE(before.read_ended) << "closed before T after a sweep";
    EXPECT_TRUE(closed) << "the sweep cost the session its deadline";
}

// The deadline runs on EngineConfig::clock and ignores SessionConfig::clock_override.
TEST(EstablishmentTimeoutQ18, TheDeadlineIgnoresTheClockOverride) {
    constexpr auto kT = 2000ms;
    pr::Rig rig;
    auto override_clock = std::make_shared<fixpp::core::mock_clock>(
        rig.clock->now(), rig.clock->steady_now(), rig.ioc.get_executor());
    auto cfg = rig.cfg(session_role::initiator);
    set_logon_timeout(cfg, kT);
    cfg.clock_override = override_clock;
    bool const up = rig.start(std::move(cfg)) &&
                    rig.run_until([&] { return rig.state() == fsm_state::LogonSent; });
    Snapshot after_override;
    bool closed = false;
    if (up) {
        // KIND C (ci/mock-clock-staging-sweep.sh): advancing the override must fire
        // nothing the deadline depends on; that is the oracle.
        override_clock->advance(kT + 3000ms);
        rig.settle();
        after_override = snapshot(rig);
        // KIND D (ci/mock-clock-staging-sweep.sh): a stored anchor; a late arm fires.
        rig.clock->advance(kT);
        closed = rig.run_until([&] {
            auto const o = snapshot(rig);
            return o.read_ended && o.timeouts == 1U;
        });
    }
    rig.stop();

    ASSERT_TRUE(up);
    EXPECT_FALSE(after_override.read_ended) << "the override's time expired the deadline";
    EXPECT_EQ(after_override.timeouts, 0U);
    EXPECT_EQ(after_override.state, fsm_state::LogonSent);
    EXPECT_TRUE(closed) << "the engine clock's time did not expire the deadline";
}

// A clock-wide sweep during phase (b) neither cancels the pump's blocked read nor makes
// it issue another (the deadline race re-arms in place, contract C-4 Sleep cancellation).
// A cancelled read can lose bytes already taken off the socket, which "the wait does not
// end early" cannot see. On the scripted double, whose read waits once its script is
// exhausted and counts every initiation and every cancelled wait.
TEST(EstablishmentTimeoutQ18, AClockWideSweepNeitherCancelsNorReissuesTheBlockedRead) {
    pr::Rig rig;
    auto stream = std::make_shared<fixpp::session::test::ScriptedStream>();
    stream->hold_open = true;
    auto cfg = rig.cfg(session_role::initiator);
    set_logon_timeout(cfg, 5000ms);
    cfg.transport_factory_override =
        std::make_shared<fixpp::session::test::ScriptedReadTransportFactory>(stream);
    bool const up = rig.start(std::move(cfg)) && rig.run_until([&] {
        return rig.state() == fsm_state::LogonSent && stream->waiting != nullptr;
    });
    std::size_t initiated_before = 0;
    std::size_t initiated_after = 0;
    std::size_t cancelled_after = 0;
    bool waiting_after = false;
    bool active = false;
    if (up) {
        initiated_before = stream->reads_initiated;
        rig.clock->cancel_sleeps();
        rig.settle();
        initiated_after = stream->reads_initiated;
        cancelled_after = stream->reads_cancelled;
        waiting_after = stream->waiting != nullptr;
        stream->push(pr::to_bytes(rig.logon()));
        active = rig.run_until([&] { return rig.state() == fsm_state::Active; });
    }
    rig.stop();

    ASSERT_TRUE(up) << "the initiator's pump did not block on a read in LogonSent";
    EXPECT_EQ(initiated_before, 1U);
    EXPECT_EQ(cancelled_after, 0U) << "the sweep cancelled the blocked read";
    EXPECT_EQ(initiated_after, 1U) << "the sweep made the pump issue another read";
    EXPECT_TRUE(waiting_after) << "the read is still blocked after the sweep";
    EXPECT_TRUE(active) << "the peer's Logon after the sweep was not delivered";
}

// ── Q-36: disarmed at the first Active, with no application attached ────────

void run_q36(session_role role) {
    SCOPED_TRACE(role == session_role::initiator ? "initiator" : "acceptor");
    constexpr auto kT = 5000ms;  // below the rig's HeartBtInt (30 s): liveness does not act
    pr::Rig rig;                 // no Application
    auto cfg = rig.cfg(role);
    set_logon_timeout(cfg, kT);
    bool const up = rig.start(std::move(cfg)) && rig.to_active();
    Snapshot idle;
    bool processed = false;
    Snapshot last;
    if (up) {
        // KIND C (ci/mock-clock-staging-sweep.sh): past T, nothing may fire; that is
        // the oracle.
        rig.clock->advance(kT + 1000ms);
        rig.settle();
        idle = snapshot(rig);
        processed = rig.deliver(rig.heartbeat(2)) && rig.run_until([&] {
            auto const s = rig.session();
            return s && next_inbound(*s) == 3U;
        });
        last = snapshot(rig);
    }
    rig.stop();

    ASSERT_TRUE(up) << "the session did not reach Active before T";
    EXPECT_EQ(idle.state, fsm_state::Active) << "closed past T while Active";
    EXPECT_FALSE(idle.read_ended) << "the transport was closed past T";
    EXPECT_EQ(idle.timeouts, 0U);
    EXPECT_TRUE(processed) << "the pump stopped reading past T";
    EXPECT_EQ(last.state, fsm_state::Active);
}

TEST(EstablishmentTimeoutQ36, Acceptor_ActiveBeforeTWithNoApplication_StaysActivePastT) {
    run_q36(session_role::acceptor);
}
TEST(EstablishmentTimeoutQ36, Initiator_ActiveBeforeTWithNoApplication_StaysActivePastT) {
    run_q36(session_role::initiator);
}

}  // namespace
