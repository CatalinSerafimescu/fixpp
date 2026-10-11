// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_537_unbuildable_frames.cpp
//
// fixpp#537, D-537-7 (.specify/536-537-538-recovery-liveness.md §4.3 and the D-537-7 row
// of §5): a frame the session cannot build in its fixed frame buffer.
//
// The fixture is a directly driven Session on the callback transport, facing hand-built
// frames, at an ordinary HeartBtInt. The session's own Logon and its ResendRequest are
// built in buffers of one size and share their header fields, so which of the two still
// fits depends on the CompIDs and on the digits of the numbers each carries.
//
// Step 1, the boundary finder: the longest SenderCompID at which the session's own
// Logon is still written. It is found by building, per role, on every run. No length is
// written in this file: the lengths follow from the builders.
//
// Step 2: at that boundary the peer sends a Reset-mode SequenceReset, which raises
// NextNumIn to its NewSeqNo(36), and then a frame above it. The ResendRequest that
// frame draws carries NextNumIn as BeginSeqNo(7), so a NewSeqNo with enough digits
// makes it longer than the Logon was. The NewSeqNo is found by building too.

#include <gtest/gtest.h>

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

// A measurement of the code as it stands before D-537-7, and not a cell of the design:
// it asserts that a boundary exists at which the Logon is written and the ResendRequest
// is not, and what the session does there today. D-537-7's own cell replaces it.
TEST_P(UnbuildableFrames, Measurement_LogonWrittenAndResendRequestNotBuilt) {
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

    // Today: success, Active, and nothing written for a request the peer never got.
    ASSERT_TRUE(unbuilt->result.has_value());
    EXPECT_TRUE(unbuilt->result->has_value())
        << "error " << static_cast<int>(unbuilt->result->error());
    EXPECT_EQ(unbuilt->state, fsm_state::Active);
    EXPECT_EQ(unbuilt->frames_written, 0U);
}

// The same stimulus on a session whose SenderCompID is far from the boundary: every
// NewSeqNo draws a ResendRequest from it. So a missing ResendRequest in the measurement
// above comes from the frame's size and not from the stimulus.
TEST_P(UnbuildableFrames, Measurement_Control_ShortCompId_EveryNewSeqNoDrawsAResendRequest) {
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

INSTANTIATE_TEST_SUITE_P(BothRoles, UnbuildableFrames,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](const ::testing::TestParamInfo<session_role>& i) {
                             return i.param == session_role::initiator ? "Initiator" : "Acceptor";
                         });

}  // namespace fixpp::session::test
