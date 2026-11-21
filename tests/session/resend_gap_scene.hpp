// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/resend_gap_scene.hpp — test-only.
//
// 105-transport-liveness (the R-2 wait, contract C-2) supersedes, for every Logout on C-2's
// "Which Logouts" list, the "Logout, then Disconnected at once" ending this file pinned: the
// session stays Active until the peer's Logout or the bound (logout_disconnect_timeout_ms from
// the Logout's write start), and ends there; the cells read it through
// tests/support/r2_wait_reading.hpp or their fixture's form of it.
//
// fixpp#537 (.specify/536-537-538-recovery-liveness.md §4 and the #537 table of §5): one
// directly driven Session, on either role, that a cell brings to Active and then feeds the
// peer's frames by hand. No Engine. The cell's own coroutines call open() and
// on_inbound_frame() on one io_context, which the test thread runs, and the session writes
// through its transport callback into `sent`.
//
// The clock is a mock_clock. Every frame a builder makes is stamped from it, so a cell
// that moves the clock still passes the SendingTime(52) guard. HeartBtInt is kHeartBtInt
// unless Options says otherwise, so the liveness loop runs: a cell that moves the clock
// states the traffic that keeps the loop from ending the session before the cell's own
// subject does.
//
// Nothing here waits in real time for a mock-clock effect. The mock clock's wake-ups are
// posted handlers, the store (with Options::with_store; otherwise the session has none) is a
// MemoryStore and the transport is a callback, so after a delivery or a clock move every
// handler the session owes is ready, and drain() runs them all. A cell that closes
// gracefully, uses another store or runs an Engine does not have that property and must not
// use pass().
#pragma once

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/r2_wait_reading.hpp"
#include "support/recording_store.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"
#include "support/validation_test_dictionary.hpp"

namespace fixpp::test_support::resend_gap {

// HeartBtInt of a Scene, and the bound of D-537-5 as the note states it: twice that. The
// bound is spelled here from the note, not read from the session.
inline constexpr std::chrono::seconds kHeartBtInt{30};
inline constexpr std::chrono::seconds kGapBound{60};

// Side(54) and TransactTime(60) of a NewOrderSingle.
inline constexpr std::string_view kSideAndTime =
    "54=1\x01"
    "60=20240101-00:00:00.000\x01";

// The value of the first `<SOH>tag=` field of `frame`, or "".
[[nodiscard]] inline std::string field(std::string_view frame, std::uint32_t tag) {
    const std::string needle = "\x01" + std::to_string(tag) + "=";
    auto pos = frame.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    const auto end = frame.find('\x01', pos);
    return std::string{frame.substr(pos, end == std::string_view::npos ? end : end - pos)};
}

[[nodiscard]] inline std::string_view state_name(fixpp::session::fsm_state s) {
    switch (s) {
        case fixpp::session::fsm_state::NotConnected:
            return "NotConnected";
        case fixpp::session::fsm_state::LogonSent:
            return "LogonSent";
        case fixpp::session::fsm_state::LogonReceived:
            return "LogonReceived";
        case fixpp::session::fsm_state::Active:
            return "Active";
        case fixpp::session::fsm_state::LogoutSent:
            return "LogoutSent";
        case fixpp::session::fsm_state::Disconnected:
            return "Disconnected";
    }
    return "?";
}

// Records what the session hands its application.
class GapApp : public fixpp::session::Application {
public:
    std::vector<std::string> delivered;       // ClOrdID(11) of each fromApp call, in order
    std::vector<std::string> to_admin_types;  // MsgType(35) of each toAdmin call, in order

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& /*id*/) override {
        const auto fv = msg.get(11);
        delivered.emplace_back(fv ? std::string{fv->as_string()} : std::string{"<no 11>"});
        return {};
    }
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& /*id*/) override {
        to_admin_types.emplace_back(msg.msg_type());
    }

    [[nodiscard]] int to_admin_count(std::string_view msg_type) const {
        int n = 0;
        for (auto const& t : to_admin_types) n += static_cast<int>(t == msg_type);
        return n;
    }
};

// An application whose fromAdmin rejects a SequenceReset(35=4) and accepts every other
// administrative message.
class ResetRejectingApp final : public GapApp {
public:
    int resets_rejected = 0;

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& /*id*/) override {
        if (msg.msg_type() != "4") return {};
        ++resets_rejected;
        return std::unexpected(fixpp::core::error::app_do_not_send);
    }
};

// An application whose toAdmin throws on a Logout(35=5) and returns on every other frame.
class LogoutThrowingApp final : public GapApp {
public:
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& id) override {
        GapApp::toAdmin(msg, id);
        if (msg.msg_type() == "5") throw std::runtime_error("LogoutThrowingApp: toAdmin");
    }
};

struct Options {
    fixpp::session::session_role role = fixpp::session::session_role::initiator;
    // The session's HeartBtInt, and the HeartBtInt(108) of the peer's Logon. Zero turns the
    // liveness loop off.
    std::chrono::seconds heartbeat = kHeartBtInt;
    // False: the Session is given no clock (EngineConfig::clock is null). The Scene keeps
    // its mock clock for the builders' SendingTime(52) and for its own teardown.
    bool with_clock = true;
    // The validate gate, on the validation test dictionary (validation_test_dictionary.hpp).
    // PossDupFlag(43) is not a header field of that dictionary, so a PossDup frame fails
    // validation in such a Scene; read its XML before sending another message type.
    bool validate = false;
    bool redeliver_poss_dup = false;
    bool validate_sequence_numbers = true;
    // Null: a GapApp.
    std::shared_ptr<GapApp> app = nullptr;
    // True: the session stores what it sends in an unbounded MemoryStore, so a ResendRequest
    // is answered by replays. False: no store, and the walk answers with one GapFill.
    bool with_store = false;
    // When set, SessionConfig::logout_disconnect_timeout_ms (the too-high Logout's bound T,
    // research D4.4); otherwise SessionConfig's default.
    std::optional<std::uint32_t> logout_timeout_ms = std::nullopt;
    // True (with_store ignored): the unbounded MemoryStore behind a RecordingStore, whose log
    // is Scene::log, so a cell can hold one store call. While a hold is in flight the
    // io_context always has a handler ready: such a cell feeds with feed_spawn() and pumps
    // with pump_until() or pump_for(), never with feed(), pass() or drain().
    bool recording_store = false;
    // With recording_store: the store reports itself persistent, so the session persists
    // each inbound advance (persist_inbound_advance_) and a cell can hold that store call.
    bool recording_store_persistent = false;
};

// The peer is "TW" and the session "ISLD", FIX.4.2.
class Scene {
public:
    // First, so destroyed last: the session's executor is on it.
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> mock;
    std::shared_ptr<GapApp> app;
    fixpp::core::EngineConfig engine;
    fixpp::session::SessionConfig cfg;
    std::vector<std::string> sent;  // each frame the session gave its transport callback
    std::unique_ptr<fixpp::session::Session> session;
    std::shared_ptr<fixpp::test_support::StoreCallLog> log;  // Options::recording_store only

    explicit Scene(Options const& o = {}) : app{o.app ? o.app : std::make_shared<GapApp>()} {
        using namespace std::chrono;
        mock = std::make_shared<fixpp::core::mock_clock>(
            system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
            ioc.get_executor());
        if (o.with_clock) engine.clock = mock;
        engine.executor = ioc.get_executor();
        engine.application = app;

        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = o.heartbeat;
        cfg.role = o.role;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = o.validate ? fixpp::test_support::make_validation_test_dictionary()
                                    : fixpp::test_support::make_minimal_dictionary();
        cfg.validate_inbound_messages = o.validate;
        cfg.redeliver_poss_dup = o.redeliver_poss_dup;
        cfg.validate_sequence_numbers = o.validate_sequence_numbers;
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        if (o.logout_timeout_ms) cfg.logout_disconnect_timeout_ms = *o.logout_timeout_ms;
        if (o.recording_store) {
            auto const f = std::make_shared<fixpp::test_support::RecordingStoreFactory>(
                std::make_shared<fixpp::session::MemoryStoreFactory>(
                    fixpp::session::MemoryStore::Config{
                        .policy = fixpp::session::capacity_policy::unbounded}));
            if (o.recording_store_persistent) f->reports_persistent = true;
            log = f->log;
            cfg.store_factory = f;
        } else if (o.with_store) {
            cfg.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(
                fixpp::session::MemoryStore::Config{
                    .policy = fixpp::session::capacity_policy::unbounded});
        }
        cfg.transport_send = [this](std::span<const std::byte> f) {
            sent.emplace_back(reinterpret_cast<const char*>(f.data()), f.size());
        };
        heartbeat_ = o.heartbeat;
        session = std::make_unique<fixpp::session::Session>(engine, cfg);
    }

    Scene(Scene const&) = delete;
    Scene& operator=(Scene const&) = delete;

    // A destructor's body runs while every member is alive. The session is closed if a
    // cell left it open, and whatever is still suspended on the io_context is resumed
    // here, over live storage. cancel_and_drain_or_report carries its own catch.
    ~Scene() {
        try {
            asio::co_spawn(fixpp::test_support::session_executor_of(*session, ioc),
                           session->close(fixpp::session::close_mode::terminal), asio::detached);
        } catch (...) {  // NOLINT(bugprone-empty-catch) — the drain below still runs.
        }
        fixpp::test_support::cancel_and_drain_or_report(ioc, *mock, "resend_gap::Scene");
    }

    // ── Running the io_context ────────────────────────────────────────────────────────

    // Runs every ready handler until none is left. No wall-clock bound: see the file
    // comment for why every handler the session owes is ready.
    void drain() {
        ioc.restart();
        while (ioc.poll() > 0) ioc.restart();
        ioc.restart();
    }

    // Moves the mock clock forward by `d`, then runs every handler that became ready.
    void pass(std::chrono::nanoseconds d) {
        // Staging (ci/mock-clock-staging-sweep.sh): KIND D for the one sleeper a Scene
        // has, the liveness loop. Each of its two sleeps is armed to an instant computed
        // from a stored anchor (the last inbound or outbound instant), and every delivery
        // and every earlier call of this function ended in drain(), so the anchor predates
        // this advance; a sleep armed late names an instant already passed. KIND A for
        // the other reader: the next delivered frame reads the clock on this thread.
        mock->advance(d);
        drain();
    }

    // 105-transport-liveness, the R-2 wait (contract C-2; tests/support/r2_wait_reading.hpp):
    // the state after an ending Logout, at the bound - 1 ms and at the bound, read with the
    // mock clock still at the instant the ending's write started.
    [[nodiscard]] fixpp::test_support::r2_wait_reading read_r2_wait() {
        auto const bound = std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms};
        fixpp::test_support::r2_wait_reading r{.after_ending = session->state(),
                                               .before_bound = session->state(),
                                               .at_bound = session->state()};
        pass(bound - std::chrono::milliseconds{1});
        r.before_bound = session->state();
        pass(std::chrono::milliseconds{1});
        r.at_bound = session->state();
        return r;
    }

    // ── The peer's frames ─────────────────────────────────────────────────────────────

    // SendingTime(52) for a frame stamped `offset` away from now on the mock clock.
    [[nodiscard]] std::string sending_time(std::chrono::seconds offset = {}) const {
        std::array<char, 32> buf{};
        const auto r = fixpp::core::utc_time_to_fix_string(
            mock->now() + offset, fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        return r ? std::string{r->data(), r->size()} : std::string{};
    }

    // 35, 34, 49, 52, 56 in that order, then `extra`.
    [[nodiscard]] static std::vector<std::byte> raw(std::string_view msg_type, std::uint32_t seq,
                                                    std::string_view sender,
                                                    std::string_view sending_time,
                                                    std::string_view extra = {}) {
        std::string body;
        body += "35=" + std::string(msg_type) + "\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body += "49=" + std::string(sender) + "\x01";
        body += "52=" + std::string(sending_time) + "\x01";
        body += "56=ISLD\x01";
        body += std::string(extra);
        std::string full =
            "8=FIX.4.2\x01"
            "9=" +
            std::to_string(body.size()) + "\x01" + body;
        unsigned int cs = 0;
        for (unsigned char const c : full) cs += c;
        std::array<char, 8> csbuf{};
        std::snprintf(csbuf.data(), csbuf.size(), "%03u", cs & 0xFFU);
        full += "10=" + std::string(csbuf.data()) + "\x01";
        std::vector<std::byte> out;
        out.reserve(full.size());
        for (char const c : full) out.push_back(static_cast<std::byte>(c));
        return out;
    }

    // A frame from the peer, stamped now.
    [[nodiscard]] std::vector<std::byte> frame(std::string_view msg_type, std::uint32_t seq,
                                               std::string_view extra = {}) const {
        return raw(msg_type, seq, "TW", sending_time(), extra);
    }

    [[nodiscard]] std::vector<std::byte> logon(std::uint32_t seq) const {
        return frame("A", seq,
                     "98=0\x01"
                     "108=" +
                         std::to_string(heartbeat_.count()) + "\x01");
    }
    // A Heartbeat; with TestReqID(112) when `test_req_id` is not empty.
    [[nodiscard]] std::vector<std::byte> heartbeat(std::uint32_t seq,
                                                   std::string_view test_req_id = {}) const {
        return frame(
            "0", seq,
            test_req_id.empty() ? std::string{} : "112=" + std::string(test_req_id) + "\x01");
    }
    [[nodiscard]] std::vector<std::byte> test_request(std::uint32_t seq) const {
        return frame("1", seq, "112=PEER-TR\x01");
    }
    [[nodiscard]] std::vector<std::byte> resend_request(std::uint32_t seq) const {
        return frame("2", seq,
                     "7=1\x01"
                     "16=0\x01");
    }
    [[nodiscard]] std::vector<std::byte> reject(std::uint32_t seq) const {
        return frame("3", seq, "45=1\x01");
    }
    [[nodiscard]] std::vector<std::byte> logout(std::uint32_t seq) const { return frame("5", seq); }

    // The fields of a NewOrderSingle the validation test dictionary requires, with
    // ClOrdID(11) = "ORD<seq>".
    [[nodiscard]] static std::string order_fields(std::uint32_t seq) {
        return "11=ORD" + std::to_string(seq) + "\x01" + std::string{kSideAndTime};
    }
    // An application message: a NewOrderSingle that passes the validate gate.
    [[nodiscard]] std::vector<std::byte> order(std::uint32_t seq) const {
        return frame("D", seq, order_fields(seq));
    }
    // A NewOrderSingle without its required ClOrdID(11): the validate gate Rejects it.
    [[nodiscard]] std::vector<std::byte> invalid_order(std::uint32_t seq) const {
        return frame("D", seq, kSideAndTime);
    }
    // The same invalid NewOrderSingle from a SenderCompID(49) that is not the peer's.
    [[nodiscard]] std::vector<std::byte> invalid_order_from_other_comp_id(std::uint32_t seq) const {
        return raw("D", seq, "OTHER", sending_time(), kSideAndTime);
    }
    // A valid NewOrderSingle from a SenderCompID(49) that is not the peer's.
    [[nodiscard]] std::vector<std::byte> order_from_other_comp_id(std::uint32_t seq) const {
        return raw("D", seq, "OTHER", sending_time(), order_fields(seq));
    }
    // A valid NewOrderSingle whose SendingTime(52) is ten minutes behind the clock, which
    // is outside the default threshold.
    [[nodiscard]] std::vector<std::byte> order_with_stale_sending_time(std::uint32_t seq) const {
        return raw("D", seq, "TW", sending_time(std::chrono::seconds{-600}), order_fields(seq));
    }

    // What a PossDup frame carries as OrigSendingTime(122).
    enum class orig_time : std::uint8_t { valid, absent, unparseable };
    // A PossDup NewOrderSingle. A valid 122 equals the frame's 52.
    [[nodiscard]] std::vector<std::byte> poss_dup_order(std::uint32_t seq,
                                                        orig_time orig = orig_time::valid) const {
        std::string extra = "43=Y\x01";
        if (orig == orig_time::valid) extra += "122=" + sending_time() + "\x01";
        if (orig == orig_time::unparseable) extra += "122=NOT-A-TIME\x01";
        return frame("D", seq, extra + order_fields(seq));
    }

    // A SequenceReset in GapFill mode: 34 = `begin`, NewSeqNo(36) = `new_seq_no`.
    [[nodiscard]] std::vector<std::byte> gap_fill(std::uint32_t begin, std::uint32_t new_seq_no,
                                                  bool poss_dup = true) const {
        std::string extra;
        if (poss_dup)
            extra +=
                "43=Y\x01"
                "122=" +
                sending_time() + "\x01";
        extra +=
            "123=Y\x01"
            "36=" +
            std::to_string(new_seq_no) + "\x01";
        return frame("4", begin, extra);
    }
    // A SequenceReset in Reset mode; without NewSeqNo(36) when `new_seq_no` is zero.
    [[nodiscard]] std::vector<std::byte> sequence_reset(std::uint32_t seq,
                                                        std::uint32_t new_seq_no) const {
        return frame("4", seq,
                     new_seq_no == 0 ? std::string{} : "36=" + std::to_string(new_seq_no) + "\x01");
    }

    // ── Driving the session ───────────────────────────────────────────────────────────

    template <class Fut>
    [[nodiscard]] static bool ready(Fut const& fut) {
        return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }

    // Delivers one frame and returns on_inbound_frame's result, with every handler the
    // delivery made ready already run. The bytes are kept until the Scene dies, so a
    // delivery that a pump missed is still resumed over live storage by the destructor.
    fixpp::core::expected_t<void> feed(std::vector<std::byte> f) {
        fed_.push_back(std::move(f));
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*session, ioc),
                                  session->on_inbound_frame(fed_.back()), asio::use_future);
        if (!fixpp::test_support::pump_until(
                ioc, [&] { return ready(fut); }, fixpp::test_support::kPumpBudget,
                fixpp::test_support::kPumpSlice, nullptr)) {
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "resend_gap::Scene::feed";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        drain();
        return fut.get();
    }

    // Starts the delivery of one frame and returns its future without running anything.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> feed_spawn(std::vector<std::byte> f) {
        fed_.push_back(std::move(f));
        return asio::co_spawn(fixpp::test_support::session_executor_of(*session, ioc),
                              session->on_inbound_frame(fed_.back()), asio::use_future);
    }
    // Runs handlers until `pred` holds or `budget` has passed; true if it held.
    template <class Pred>
    [[nodiscard]] bool pump_until(
        Pred pred, std::chrono::steady_clock::duration budget = fixpp::test_support::kPumpBudget) {
        return fixpp::test_support::pump_until(ioc, std::move(pred), budget,
                                               fixpp::test_support::kPumpSlice, nullptr);
    }
    // Runs handlers for `d` of real time.
    void pump_for(std::chrono::steady_clock::duration d) {
        (void)pump_until([] { return false; }, d);
    }

    // open(), then the peer's Logon at 1: the session is Active, NextNumIn is 2, and its
    // liveness loop, when it has one, is parked in its first sleep.
    [[nodiscard]] bool open_to_active() {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*session, ioc),
                                  session->open(), asio::use_future);
        if (!fixpp::test_support::pump_until(
                ioc, [&] { return ready(fut); }, fixpp::test_support::kPumpBudget,
                fixpp::test_support::kPumpSlice, nullptr) ||
            !fut.get().has_value()) {
            return false;
        }
        (void)feed(logon(1));
        return session->state() == fixpp::session::fsm_state::Active;
    }

    // ── Readings ──────────────────────────────────────────────────────────────────────

    [[nodiscard]] fixpp::session::fsm_state state() const { return session->state(); }
    [[nodiscard]] fixpp::session::seqnum_t next_in() const {
        return fixpp::session::session_test_access::seqnum_mgr(*session).next_inbound_unsafe();
    }
    [[nodiscard]] bool resend_outstanding() const {
        return fixpp::session::session_test_access::resend_outstanding(*session);
    }
    [[nodiscard]] bool test_request_pending() const {
        return fixpp::session::session_test_access::test_request_pending(*session);
    }

    // The frames of this MsgType among those written at or after index `from` of `sent`.
    [[nodiscard]] std::vector<std::string> of_type(std::string_view msg_type,
                                                   std::size_t from = 0) const {
        std::vector<std::string> out;
        for (std::size_t i = from; i < sent.size(); ++i) {
            if (field(sent[i], 35) == msg_type) out.push_back(sent[i]);
        }
        return out;
    }
    [[nodiscard]] std::size_t count(std::string_view msg_type, std::size_t from = 0) const {
        return of_type(msg_type, from).size();
    }
    // "35=<t>/34=<n>" of each frame written at or after index `from`, for a failure message.
    [[nodiscard]] std::string written(std::size_t from = 0) const {
        std::string out;
        for (std::size_t i = from; i < sent.size(); ++i) {
            out += (out.empty() ? "" : " ") + ("35=" + field(sent[i], 35)) +
                   "/34=" + field(sent[i], 34);
        }
        return out.empty() ? std::string{"nothing"} : out;
    }
    // TestReqID(112) of the last TestRequest the session wrote, or "".
    [[nodiscard]] std::string last_test_req_id() const {
        const auto trs = of_type("1");
        return trs.empty() ? std::string{} : field(trs.back(), 112);
    }

private:
    std::chrono::seconds heartbeat_{};
    std::deque<std::vector<std::byte>> fed_;
};

// How far above NextNumIn the frame that opens a cell's gap is. Wide, so that a cell's
// in-sequence frames do not reach the recorded end unless the cell means them to.
inline constexpr std::uint32_t kGapWidth = 50;
// NextNumIn when a gap opened, and the size of `sent` after its ResendRequest.
struct Gap {
    fixpp::session::seqnum_t n = 0;
    std::size_t mark = 0;
};

// Brings `s` to Active and opens a gap with a NewOrderSingle at N + `width`. The gap is
// open when one ResendRequest with BeginSeqNo(7) = N was written and a resend is
// outstanding; n is 0 after a failed gate.
[[nodiscard]] inline Gap open_gap(Scene& s, std::uint32_t width = kGapWidth) {
    if (!s.open_to_active()) {
        ADD_FAILURE() << "setup: the session did not reach Active";
        return {};
    }
    const fixpp::session::seqnum_t n = s.next_in();
    const auto before = s.sent.size();
    (void)s.feed(s.order(n + width));
    const auto rr = s.of_type("2", before);
    if (rr.size() != 1U || field(rr.front(), 7) != std::to_string(n) || !s.resend_outstanding() ||
        s.state() != fixpp::session::fsm_state::Active) {
        ADD_FAILURE() << "setup: no gap was opened; the session wrote " << s.written(before);
        return {};
    }
    return Gap{.n = n, .mark = s.sent.size()};
}

// The ending of an unfilled gap whose peer keeps sending (note §4.6, condition (a)), read
// after the frame at or after the bound: one Logout written since `mark`, with a Text(58);
// on_inbound_frame returned success; then, by 105-transport-liveness's R-2 wait (contract
// C-2), which supersedes the Disconnected at the Logout: Active through the wait, Disconnected
// at its bound (Scene::read_r2_wait moves the mock clock to that bound).
[[nodiscard]] inline ::testing::AssertionResult deadline_logout_written(
    Scene& s, std::size_t mark, fixpp::core::expected_t<void> const& r) {
    const auto logouts = s.of_type("5", mark);
    if (logouts.size() != 1U) {
        return ::testing::AssertionFailure()
               << "the frame at the bound drew " << logouts.size()
               << " Logout(s), not the deadline's one: the session is " << state_name(s.state())
               << " and wrote " << s.written(mark);
    }
    if (field(logouts.front(), 58).empty()) {
        return ::testing::AssertionFailure()
               << "the Logout at the bound carries no Text(58); the session wrote "
               << s.written(mark);
    }
    if (!r) {
        return ::testing::AssertionFailure()
               << "on_inbound_frame returned error " << static_cast<int>(r.error())
               << " for the frame that drew the deadline's Logout";
    }
    auto const w = s.read_r2_wait();
    if (w.after_ending != fixpp::session::fsm_state::Active ||
        w.before_bound != fixpp::session::fsm_state::Active ||
        w.at_bound != fixpp::session::fsm_state::Disconnected) {
        return ::testing::AssertionFailure()
               << "the deadline's Logout's wait (R-2): after it " << state_name(w.after_ending)
               << ", at the bound - 1 ms " << state_name(w.before_bound) << ", at the bound "
               << state_name(w.at_bound);
    }
    return ::testing::AssertionSuccess();
}

[[nodiscard]] inline const char* role_name(fixpp::session::session_role r) {
    return r == fixpp::session::session_role::initiator ? "Initiator" : "Acceptor";
}

}  // namespace fixpp::test_support::resend_gap
