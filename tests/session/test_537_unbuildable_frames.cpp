// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_537_unbuildable_frames.cpp
//
// fixpp#537, D-537-7 and D-537-5 (.specify/536-537-538-recovery-liveness.md §4.3; the rows
// "D-537-7" and "D-537-5, a Logout that cannot be built" of §5): a frame the session cannot
// build in its fixed frame buffer.
//
// The fixture is a directly driven Session on the callback transport, facing hand-built
// frames, at an ordinary HeartBtInt. The session's own Logon, its ResendRequest and its
// Logout are built in buffers of one size and share their header fields, so which of them
// still fits depends on the CompIDs and on what each carries beyond the header.
//
// Step 1, the boundary finder: the longest SenderCompID at which the session's own
// Logon is still written. It is found by building, per role, on every run. No length is
// written in this file: the lengths follow from the builders.
//
// D-537-7. At that boundary the peer sends a Reset-mode SequenceReset, which raises
// NextNumIn to its NewSeqNo(36), and then a frame above it. The ResendRequest that frame
// draws carries NextNumIn as BeginSeqNo(7), so a NewSeqNo with enough digits makes it
// longer than the Logon was. The NewSeqNo is found by building too. A ResendRequest that
// cannot be built ends the session: the peer never got the request, so a session that
// stayed up would wait for a replay nobody was asked for.
//
// D-537-5. At the same boundary with NextNumIn left small, the ResendRequest is built, so
// a gap opens, and the Logout that the deadline of an unfilled gap sends is not: it
// carries a Text(58) longer than what the Logon carries beyond the header. The session
// then ends at the bound with nothing written (ruling R-12). A control session far from
// the boundary writes that Logout under the same traffic.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

constexpr std::string_view kSt52 = "20240101-00:00:00.000";
constexpr std::string_view kPeerCompId = "TW";
constexpr auto kHeartBtInt = 30s;

// A frame from the peer to the session whose SenderCompID is `session_comp_id`.
std::vector<std::byte> make_frame(std::string_view session_comp_id, std::string_view msg_type,
                                  std::uint32_t seq, std::string_view extra = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(kPeerCompId) + "\x01";
    body += "52=" + std::string(kSt52) + "\x01";
    body += "56=" + std::string(session_comp_id) + "\x01";
    body += std::string(extra);
    std::string full =
        "8=FIX.4.2\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char const c : full) cs += c;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> out;
    out.reserve(full.size());
    for (char const c : full) out.push_back(static_cast<std::byte>(c));
    return out;
}

// Value of tag `tag` in a SOH-delimited frame, matched at a field boundary.
std::string field(std::string_view frame, std::uint32_t tag) {
    const std::string needle = "\x01" + std::to_string(tag) + "=";
    auto pos = frame.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    auto const end = frame.find('\x01', pos);
    return std::string(frame.substr(pos, end == std::string_view::npos ? frame.npos : end - pos));
}

// Every frame the session wrote, in order.
struct Capture {
    std::vector<std::string> frames;
    [[nodiscard]] std::vector<std::string> of_type(std::string_view msg_type) const {
        std::vector<std::string> out;
        for (auto const& f : frames) {
            if (field(f, 35) == msg_type) out.push_back(f);
        }
        return out;
    }
};

// What the session did with the frame above a Reset-mode SequenceReset's NewSeqNo.
struct AfterReset {
    bool drove = false;  // every step before the frame above completed as expected
    std::optional<fixpp::core::expected_t<void>> result;  // on_inbound_frame, for that frame
    std::size_t resend_requests = 0;                      // 35=2 frames written for it
    std::size_t frames_written = 0;                       // any frame written for it
    std::string begin_seq_no;  // BeginSeqNo(7) of the first ResendRequest, when one was written
    fsm_state state = fsm_state::NotConnected;
};

// What a session did with frames above NextNumIn, from the frame that opened the gap to the
// frame at the bound.
struct ToTheBound {
    std::string setup_miss;        // not empty: the gap was not opened as the cell needs
    std::string before_the_bound;  // not empty: a frame before the bound was not ignored
    std::string logon;             // the session's own Logon, as written
    std::string stamp;             // SendingTime(52) of the session's ResendRequest
    seqnum_t next_out = 0;         // the session's next outbound number at the bound
    std::optional<fixpp::core::expected_t<void>> result;  // on_inbound_frame, at the bound
    std::vector<std::string> written;                     // every frame written for that frame
    fsm_state state = fsm_state::NotConnected;
};

// The spacing of the peer's frames before the bound, inside every HeartBtInt, and the
// bound itself, spelled from the note and not read from the session.
constexpr auto kStep = 15s;
constexpr auto kGapBound = 60s;
static_assert(kStep < kHeartBtInt && kGapBound == 2 * kHeartBtInt);

class UnbuildableFrames : public ::testing::TestWithParam<session_role> {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    void SetUp() override {
        auto const utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                          ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // One session whose SenderCompID has `comp_id_len` bytes, driven through its Logon
    // exchange. Closed terminally when it goes out of scope, so a loop over lengths
    // leaves no liveness loop parked on the clock.
    class Driven {
    public:
        Driven(UnbuildableFrames& fx, std::size_t comp_id_len)
            : fx_(fx), comp_id(comp_id_len, 'S') {
            SessionConfig cfg;
            cfg.sender_comp_id = comp_id;
            cfg.target_comp_id = std::string{kPeerCompId};
            cfg.begin_string = "FIX.4.2";
            cfg.heartbeat_interval = kHeartBtInt;
            cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
            cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
            cfg.executor_override = fx.ioc.get_executor();
            cfg.role = fx.GetParam();
            cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
            cfg.transport_send = [cap = cap](std::span<const std::byte> f) {
                cap->frames.emplace_back(reinterpret_cast<const char*>(f.data()), f.size());
            };
            session = std::make_unique<Session>(fx.engine, cfg);
            opened = fx.run(session->open(), "UnbuildableFrames::open").has_value();
            const std::string logon_body =
                "98=0\x01"
                "108=" +
                std::to_string(kHeartBtInt.count()) + "\x01";
            if (opened) (void)feed("A", 1, logon_body);
        }
        ~Driven() {
            // A close that does not complete is drained before the Session is destroyed.
            // cancel_and_drain_or_report carries its own catch; the outer one covers the
            // pump and a report that throws under --gtest_throw_on_failure.
            try {
                if (opened && !fx_.run_quiet(session->close(close_mode::terminal))) {
                    fixpp::test_support::cancel_and_drain_or_report(fx_.ioc, *fx_.clock,
                                                                    "UnbuildableFrames::~Driven");
                }
            } catch (...) {  // NOLINT(bugprone-empty-catch): nothing to report to from here
            }
        }
        Driven(Driven const&) = delete;
        Driven& operator=(Driven const&) = delete;

        // on_inbound_frame's result for one peer frame, or nullopt when it did not
        // complete within the pump's budget (reported as a failure by run()).
        std::optional<fixpp::core::expected_t<void>> feed(std::string_view msg_type,
                                                          std::uint32_t seq,
                                                          std::string_view extra = {}) {
            auto const f = make_frame(comp_id, msg_type, seq, extra);
            return fx_.run(session->on_inbound_frame(std::span<const std::byte>{f}),
                           "UnbuildableFrames::feed");
        }

        // The session wrote its own Logon and reached Active.
        [[nodiscard]] bool logon_written() const {
            return !cap->of_type("A").empty() && session->state() == fsm_state::Active;
        }

    private:
        UnbuildableFrames& fx_;

    public:
        std::string comp_id;
        std::shared_ptr<Capture> cap = std::make_shared<Capture>();
        std::unique_ptr<Session> session;
        bool opened = false;
    };

    // The coroutine's result, or nullopt with one reported failure when it did not
    // complete within the pump's budget.
    template <class Coro>
    std::optional<fixpp::core::expected_t<void>> run(Coro coro, const char* site) {
        auto fut = asio::co_spawn(ioc, std::move(coro), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(ioc, fut, site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << site;
            return std::nullopt;
        }
        return fut.get();
    }
    // As run(), without the report: for a destructor.
    template <class Coro>
    bool run_quiet(Coro coro) {
        auto fut = asio::co_spawn(ioc, std::move(coro), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(ioc, fut)) return false;
        (void)fut.get();
        return true;
    }

    // Moves the clock forward by `d`, then runs every handler that became ready.
    void pass(std::chrono::nanoseconds d) {
        // Staging (ci/mock-clock-staging-sweep.sh): KIND D for the liveness loop of the
        // Driven that is alive: each of its sleeps is armed to an instant computed from
        // the last inbound or outbound instant, which a frame fed or written before this
        // advance stamped, so a sleep armed late names an instant already passed. KIND A
        // for the frame fed next, which the session reads the clock for on this thread.
        clock->advance(d);
        ioc.restart();
        while (ioc.poll() > 0) ioc.restart();
        ioc.restart();
    }

    // The "D-537-5, a Logout that cannot be built" row on a session whose SenderCompID
    // has `comp_id_len` bytes. A TestRequest above NextNumIn opens the gap at E. The peer
    // then sends one more at once, one at each kStep, and one a millisecond before
    // E + kGapBound: an inbound frame inside every HeartBtInt, so the liveness loop sends
    // no TestRequest. The last frame is at E + kGapBound. The peer's frames carry the
    // clock's seed as SendingTime(52), which stays inside the SendingTime threshold for a
    // clock moved by kGapBound.
    [[nodiscard]] ToTheBound gap_to_the_bound(std::size_t comp_id_len) {
        ToTheBound out;
        Driven d{*this, comp_id_len};
        if (!d.logon_written()) {
            out.setup_miss = "the session did not reach Active";
            return out;
        }
        out.logon = d.cap->of_type("A").front();
        auto const next_in = [&] {
            return session_test_access::seqnum_mgr(*d.session).next_inbound_unsafe();
        };
        auto const n = next_in();
        std::uint32_t seq = n + 5;
        d.cap->frames.clear();
        (void)d.feed("1", seq, "112=PEER-TR\x01");
        auto const requests = d.cap->of_type("2");
        if (requests.size() != 1U || field(requests.front(), 7) != std::to_string(n) ||
            !session_test_access::resend_outstanding(*d.session)) {
            out.setup_miss =
                "with NextNumIn = " + std::to_string(n) + " the ResendRequest was not built";
            return out;
        }
        out.stamp = field(requests.front(), 52);

        // Before the bound: ignored. The frame fed at once, with the clock not moved, is
        // also checked for "nothing written"; later the liveness loop writes Heartbeats.
        auto const ignored = [&](std::optional<fixpp::core::expected_t<void>> const& r) {
            return r.has_value() && r->has_value() && d.session->state() == fsm_state::Active &&
                   d.cap->of_type("5").empty() && d.cap->of_type("1").empty() && next_in() == n &&
                   session_test_access::resend_outstanding(*d.session);
        };
        d.cap->frames.clear();
        if (!ignored(d.feed("1", ++seq, "112=PEER-TR\x01")) || !d.cap->frames.empty()) {
            out.before_the_bound =
                d.session->state() == fsm_state::Active
                    ? "a second frame above NextNumIn, before the bound, was not ignored"
                    : "a second frame above NextNumIn, before the bound, ended the session";
            return out;
        }
        const std::array<std::chrono::milliseconds, 4> steps{kStep, kStep, kStep,
                                                             kGapBound - 3 * kStep - 1ms};
        for (auto const step : steps) {
            pass(step);
            if (!ignored(d.feed("1", ++seq, "112=PEER-TR\x01"))) {
                out.before_the_bound = "a frame above NextNumIn before the bound was not ignored";
                return out;
            }
        }

        pass(1ms);
        out.next_out = session_test_access::seqnum_mgr(*d.session).peek_outbound();
        d.cap->frames.clear();
        out.result = d.feed("1", ++seq, "112=PEER-TR\x01");
        out.written = d.cap->frames;
        out.state = d.session->state();
        return out;
    }

    [[nodiscard]] bool logon_written_at(std::size_t comp_id_len) {
        return Driven{*this, comp_id_len}.logon_written();
    }

    // One NewSeqNo(36) per digit count a sequence number can have, from two digits up,
    // smallest first.
    [[nodiscard]] static std::vector<std::uint32_t> new_seq_nos_by_digit_count() {
        std::vector<std::uint32_t> out;
        std::uint32_t v = 1;
        for (int digits = 2; digits <= std::numeric_limits<std::uint32_t>::digits10 + 1; ++digits) {
            v *= 10;
            out.push_back(v);
        }
        return out;
    }

    // Step 2 of the D-537-7 row, on a session whose SenderCompID has `comp_id_len` bytes:
    // the peer's Reset-mode SequenceReset to `new_seq_no`, then a TestRequest above it.
    [[nodiscard]] AfterReset after_reset(std::size_t comp_id_len, std::uint32_t new_seq_no) {
        AfterReset out;
        Driven d{*this, comp_id_len};
        if (!d.logon_written()) return out;
        auto const reset = d.feed("4", 2, "36=" + std::to_string(new_seq_no) + "\x01");
        if (!reset || !reset->has_value() ||
            session_test_access::seqnum_mgr(*d.session).next_inbound_unsafe() != new_seq_no) {
            return out;
        }
        out.drove = true;
        d.cap->frames.clear();
        out.result = d.feed("1", new_seq_no + 5, "112=PEER-TR\x01");
        auto const requests = d.cap->of_type("2");
        out.resend_requests = requests.size();
        if (!requests.empty()) out.begin_seq_no = field(requests.front(), 7);
        out.frames_written = d.cap->frames.size();
        out.state = d.session->state();
        return out;
    }

    // Step 1 of the D-537-7 row: the longest SenderCompID length at which the session's
    // own Logon is still written, or nullopt when the search found none. A longer CompID
    // makes a longer Logon, so the search doubles to the first length that is not
    // written and then bisects; the result is checked on both sides before it is
    // returned.
    [[nodiscard]] std::optional<std::size_t> longest_comp_id_with_logon() {
        constexpr std::size_t kSearchCap = 1U << 14U;
        std::size_t lo = 1;
        if (!logon_written_at(lo)) return std::nullopt;
        std::size_t hi = 2;
        while (hi <= kSearchCap && logon_written_at(hi)) {
            lo = hi;
            hi *= 2;
        }
        if (hi > kSearchCap) return std::nullopt;
        while (hi - lo > 1) {
            auto const mid = lo + (hi - lo) / 2;
            (logon_written_at(mid) ? lo : hi) = mid;
        }
        if (!logon_written_at(lo) || logon_written_at(lo + 1)) return std::nullopt;
        return lo;
    }
};

}  // namespace

// D-537-7: at the longest SenderCompID that still lets the Logon be written, a NewSeqNo(36)
// with enough digits leaves no room for the ResendRequest. The frame that would draw it
// returns dispatch_aborted, the session is Disconnected, and nothing is written.
TEST_P(UnbuildableFrames, ResendRequestThatCannotBeBuilt_EndsTheSession) {
    auto const boundary = longest_comp_id_with_logon();
    ASSERT_TRUE(boundary.has_value())
        << "no SenderCompID length was found at which the session's own Logon is written "
           "and one byte more is not";

    // NewSeqNo(36) by digit count, smallest first.
    std::optional<AfterReset> unbuilt;
    bool built_at_some_digit_count = false;
    for (auto const new_seq_no : new_seq_nos_by_digit_count()) {
        auto const r = after_reset(*boundary, new_seq_no);
        ASSERT_TRUE(r.drove) << "the Reset-mode SequenceReset to " << new_seq_no
                             << " did not raise NextNumIn on a session at the boundary";
        if (r.resend_requests != 0) {
            built_at_some_digit_count = true;
            continue;
        }
        unbuilt = r;
        break;
    }

    // The search can report both outcomes: a short NewSeqNo still draws a ResendRequest.
    EXPECT_TRUE(built_at_some_digit_count)
        << "no NewSeqNo drew a ResendRequest at the boundary, so a missing one below would "
           "not show that the frame could not be built";
    ASSERT_TRUE(unbuilt.has_value())
        << "at the longest SenderCompID that still lets the Logon be written, every "
           "NewSeqNo drew a ResendRequest: no boundary exists at which the Logon is written "
           "and the ResendRequest cannot be built";

    ASSERT_TRUE(unbuilt->result.has_value());
    ASSERT_FALSE(unbuilt->result->has_value())
        << "on_inbound_frame returned success for a ResendRequest the session could not "
           "build; the session's state is "
        << static_cast<int>(unbuilt->state) << " (Active is " << static_cast<int>(fsm_state::Active)
        << ")";
    EXPECT_EQ(unbuilt->result->error(), fixpp::core::error::dispatch_aborted);
    EXPECT_EQ(unbuilt->state, fsm_state::Disconnected);
    EXPECT_EQ(unbuilt->frames_written, 0U);
}

// Control of the cell above: the same stimulus on a session whose SenderCompID is far
// from the boundary draws a ResendRequest for every NewSeqNo. So a missing ResendRequest
// there comes from the frame's size and not from the stimulus.
TEST_P(UnbuildableFrames, Control_ShortCompId_EveryNewSeqNoDrawsAResendRequest) {
    for (auto const new_seq_no : new_seq_nos_by_digit_count()) {
        auto const r = after_reset(/*comp_id_len=*/1, new_seq_no);
        ASSERT_TRUE(r.drove) << "the Reset-mode SequenceReset to " << new_seq_no
                             << " did not raise NextNumIn";
        ASSERT_TRUE(r.result.has_value());
        EXPECT_TRUE(r.result->has_value());
        EXPECT_EQ(r.resend_requests, 1U) << "NewSeqNo " << new_seq_no;
        EXPECT_EQ(r.begin_seq_no, std::to_string(new_seq_no));
        EXPECT_EQ(r.state, fsm_state::Active);
    }
}

// D-537-5, a Logout that cannot be built. At the boundary, with NextNumIn left small, the
// ResendRequest is built, so a gap opens, and a frame above NextNumIn before the bound is
// ignored. At the bound the deadline's Logout cannot be built: nothing is written, the
// session is Disconnected, and the frame returns success.
TEST_P(UnbuildableFrames, AtTheBoundary_LogoutThatCannotBeBuilt_EndsTheSessionWithNothingWritten) {
    auto const boundary = longest_comp_id_with_logon();
    ASSERT_TRUE(boundary.has_value())
        << "no SenderCompID length was found at which the session's own Logon is written "
           "and one byte more is not";

    // The control: far from the boundary, the same traffic draws the deadline's Logout.
    // Its Text(58) is the text a session with the same NextNumIn sends at the bound.
    auto const control = gap_to_the_bound(/*comp_id_len=*/1);
    ASSERT_TRUE(control.setup_miss.empty()) << "short SenderCompID: " << control.setup_miss;
    ASSERT_TRUE(control.before_the_bound.empty())
        << "short SenderCompID: " << control.before_the_bound;
    ASSERT_EQ(control.written.size(), 1U)
        << "short SenderCompID: the frame at the bound drew no single frame";
    ASSERT_EQ(field(control.written.front(), 35), "5");
    auto const text = field(control.written.front(), 58);
    ASSERT_FALSE(text.empty()) << "the Logout at the bound carries no Text(58)";
    EXPECT_EQ(control.state, fsm_state::Disconnected);

    auto const at = gap_to_the_bound(*boundary);
    ASSERT_TRUE(at.setup_miss.empty()) << "at the boundary: " << at.setup_miss;
    ASSERT_TRUE(at.before_the_bound.empty()) << at.before_the_bound;

    // By building: the Logout with that text does not fit the session's frame buffer. The
    // buffer's size is not written here. It is at most one byte more than the Logon the
    // session wrote: a Logon one SenderCompID byte longer was not written, and that Logon
    // is at most two bytes longer, one for the CompID and one where BodyLength(9) gains a
    // digit.
    std::vector<std::byte> buf(at.logon.size() + 1U);
    auto const logout = fixpp::session::build_logout(std::span<std::byte>{buf}, at.next_out,
                                                     std::string(*boundary, 'S'), kPeerCompId, text,
                                                     "FIX.4.2", at.stamp);
    ASSERT_FALSE(logout.has_value())
        << "at the boundary the Logout with its Text(58) fits a buffer of " << buf.size()
        << " bytes, so the session can build it and the cell does not reach the branch";

    ASSERT_TRUE(at.result.has_value());
    EXPECT_TRUE(at.result->has_value())
        << "on_inbound_frame returned error " << static_cast<int>(at.result->error())
        << " for the frame at the bound";
    EXPECT_EQ(at.state, fsm_state::Disconnected)
        << "the session's state after the frame at the bound is " << static_cast<int>(at.state)
        << " (Active is " << static_cast<int>(fsm_state::Active) << ")";
    EXPECT_TRUE(at.written.empty())
        << "the session wrote " << at.written.size() << " frame(s) for the frame at the bound";
}

INSTANTIATE_TEST_SUITE_P(BothRoles, UnbuildableFrames,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](const ::testing::TestParamInfo<session_role>& i) {
                             return i.param == session_role::initiator ? "Initiator" : "Acceptor";
                         });

}  // namespace fixpp::session::test
