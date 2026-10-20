// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/recovery_liveness_capi_witness_test.cpp — fixpp#536, fixpp#537, fixpp#538: the
// C-ABI witnesses of the 1.12 clause (.specify/536-537-538-recovery-liveness.md §6.2,
// owner ruling R-8). One cell per witness of that section's table; each cell's comment
// names its row by the row's first words.
//
// In every cell a C-ABI engine on a real clock faces raw TCP peers
// (recovery_liveness_capi_support.hpp). The cell asserts only what a C caller and the
// peer can see:
//   - fixpp_session_is_established, sampled while the peer waits;
//   - the result of fixpp_session_close, and when the call returned;
//   - the messages the receive callback was invoked for, in order;
//   - the frames the peer read, when it read them, and whether its connection ended.
//
// How a cell is laid out:
//   - a check that the trace was set up (the session established, the stimulus was
//     written, the session reacted to it) is an ASSERT whose message begins "setup:";
//   - the assertions of the row are EXPECTs. Their messages print what the cell saw, so
//     the output of a failing cell says what the tree under test did.
//
// Controls. A cell named Control_* runs a witness's body with another stimulus, one
// whose outcome the 1.12 clause does not change: it shows that the witness's observers
// can report the value the witness expects. The witness and its control differ in the
// arguments of one call.
//
// Times. A cell that samples inside a window runs at a heartbeat interval of two
// seconds, the value §6.2 gives, so that half an interval is a margin of one second on
// either side of a sample. Every bound is derived from the timer it competes with, and
// the derivation is at the constant or at the wait.
//
// The suites are named by issue, so that `--gtest_filter=Capi538Witness.*` selects the
// cells of one mechanism, and `--gtest_filter=*.Control_*` the controls.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/error.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "fix/c_api/session.h"
#include "recovery_liveness_capi_support.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test::rl;

namespace {

// The heartbeat interval of a cell that samples inside a window (§6.2).
constexpr std::uint32_t kWindowHeartBtInt = 2;
constexpr std::chrono::milliseconds kHb{kWindowHeartBtInt * 1000};

// The heartbeat interval of a cell whose trace needs no liveness traffic: longer than
// the cell runs, so the session's liveness loop writes nothing during it.
constexpr std::uint32_t kQuietHeartBtInt = 30;

// SessionConfig's default logout_disconnect_timeout_ms, which the C ABI has no setter
// for: how long fixpp_session_close waits for the peer's answer to its Logout.
constexpr std::chrono::milliseconds kLogoutTimeout{2000};

// The CompIDs of a cell with one session.
Wire one_session() { return Wire{"INI-RL", "ACC-RL"}; }

// A frame as its distinguishing fields, for a failure message.
std::string summary(std::string_view frame) {
    std::string out;
    for (int const tag : {35, 34, 43, 7, 16, 36, 45, 58, 112, 123, 371, 372, 373, 11}) {
        if (auto const v = field(frame, tag); !v.empty()) {
            out += std::to_string(tag) + "=" + v + " ";
        }
    }
    return out;
}

// The frames of `seen` as summaries, one per line, each with its time after `origin`.
std::string summaries(std::vector<Seen> const& seen, time_point origin) {
    if (seen.empty()) return " (none)";
    std::string out;
    for (auto const& s : seen) {
        out += "\n    " + std::to_string(ms_between(origin, s.at).count()) + " ms  " +
               summary(s.frame);
    }
    return out;
}

// What a cell saw, for a failure message: when fixpp_session_is_established first
// read false, whether the peer's connection ended, and what the peer read. Times are
// counted from `origin`, which `since` names.
std::string observed(PeerLine const& line, EstablishedWatch const& watch, time_point origin,
                     std::string_view since) {
    std::string out = "\n  times are in ms after " + std::string{since};
    out += "\n  fixpp_session_is_established: ";
    out += watch.first_false
               ? "first read false at " +
                     std::to_string(ms_between(origin, *watch.first_false).count()) + " ms"
               : std::string{"never read false while it was sampled"};
    out += "\n  the peer's connection: ";
    out += line.sock.eof() ? "ended by end-of-stream" : line.read_failed ? "a read failed" : "open";
    out += "\n  frames the peer read:" + summaries(line.seen, origin);
    return out;
}

// The origin of a cell with a gap: the peer's write of the frame that opened it.
constexpr std::string_view kSinceGap = "the peer wrote the frame that opened the gap";
// The origin of a cell whose peer went silent: its read of the session's TestRequest.
constexpr std::string_view kSinceTestRequest = "the peer read the session's TestRequest";

bool read_any(PeerLine const& line, std::string_view msg_type) {
    return std::ranges::any_of(line.seen,
                               [&](Seen const& s) { return is_type(s.frame, msg_type); });
}

// A Heartbeat of the peer's, by its MsgSeqNum and its TestReqID(112).
enum class Number { above_expected, in_sequence };
enum class TestReqId { the_requested, another };

// ── fixpp#537: the traffic of an unfilled gap ───────────────────────────────

// How often the peer writes into an open gap: twice per interval. Every such frame
// refreshes the session's inbound liveness, so the liveness loop, which asks for a
// Heartbeat after one interval without a frame, asks for none.
constexpr std::chrono::milliseconds kGapTrafficPeriod = kHb / 2;

// What the peer did and read while it wrote into an open gap.
struct GapRun {
    std::vector<std::uint32_t> written;  // MsgSeqNum of each frame written into the gap
    std::optional<Seen> logout;          // the Logout that ended the run, when one was read
};

// The peer writes `stimulus(seq)` into the gap every kGapTrafficPeriod, starting one
// period after it read the ResendRequest, each frame with its next MsgSeqNum, which is
// above the gap. The run ends only when the peer reads a Logout, or at its bound: a
// verdict that the peer read no Logout holds only if the peer went on writing and
// reading past the gap's deadline. That deadline is two intervals after
// the gap opened, and it is tested when a frame arrives; the first frame written at or
// after it is at most one period late, and the bound allows half an interval more for
// the session's answer to be read.
// With `answer_test_requests`, the peer answers a TestRequest with a Heartbeat that
// carries its TestReqID(112) and the next MsgSeqNum.
GapRun run_gap_traffic(PeerLine& line, EstablishedWatch& watch, Gap const& gap,
                       std::function<std::string(std::uint32_t)> const& stimulus,
                       bool answer_test_requests = false) {
    auto const until = gap.at.hi + (2 * kHb) + kGapTrafficPeriod + (kHb / 2);
    GapRun run;
    auto next_write = gap.at.hi + kGapTrafficPeriod;
    while (!line.ended()) {
        auto const now = steady::now();
        if (now >= until) break;
        if (now >= next_write) {
            std::uint32_t const seq = line.next_seq++;
            if (!line.write(stimulus(seq))) break;
            run.written.push_back(seq);
            next_write += kGapTrafficPeriod;
        }
        if (auto s = line.read_next(std::min(until, now + kSlice))) {
            if (is_type(s->frame, "5")) {
                run.logout = s;
                break;
            }
            if (answer_test_requests && is_type(s->frame, "1")) {
                (void)line.write(line.wire.heartbeat(line.next_seq++, field(s->frame, 112)));
            }
        }
        watch.sample();
    }
    return run;
}

// The ending of §4.6's condition (a), as the row "With a heartbeat interval set" states
// it for each of its witnesses: the peer reads a Logout with Text(58), no earlier than
// two intervals after it opened the gap; fixpp_session_is_established turns false; and
// the connection is still open.
void expect_deadline_logout(PeerLine& line, EstablishedWatch& watch, Gap const& gap,
                            GapRun const& run) {
    bool const logout_with_text = run.logout && !field(run.logout->frame, 58).empty();
    EXPECT_TRUE(logout_with_text)
        << "the peer reads a Logout with Text(58) once the gap has gone two intervals "
           "without an advance"
        << observed(line, watch, gap.at.lo, kSinceGap);
    if (run.logout) {
        // The peer wrote the frame that opened the gap at gap.at.lo, so the session
        // entered the gap no earlier than that.
        EXPECT_GE(ms_between(gap.at.lo, run.logout->at).count(), (2 * kHb).count())
            << "the Logout was read before two intervals had passed since the gap opened";
    }
    // The Logout is written before the state changes, so the peer can read it first.
    watch.watch_until(steady::now() + (kHb / 2));
    EXPECT_TRUE(watch.first_false.has_value()) << "fixpp_session_is_established turns false"
                                               << observed(line, watch, gap.at.lo, kSinceGap);
    EXPECT_TRUE(line.open_through(steady::now() + (kHb / 4)))
        << "the connection is still open after the deadline's Logout (fixpp#534 is where "
           "it closes)";
}

// One session at a two-second interval, established, with a gap its peer opened.
struct GapScene {
    Dictionary dict;
    PeerLine line;
    CEngine engine;
    std::optional<Gap> gap;

    explicit GapScene(Wire wire = one_session())
        : line{std::move(wire)}, engine{dict, {line.spec(kWindowHeartBtInt)}} {}

    [[nodiscard]] CSession& session() { return engine.session(0); }

    // Starts the engine, answers the Logon and opens the gap. Empty on success, else
    // the step that failed.
    [[nodiscard]] std::string open() {
        if (!engine.start()) return "the engine did not start";
        if (!answer_logon(session(), line, kWindowHeartBtInt)) {
            return "the session did not establish";
        }
        gap = open_gap(line, "OPENS-GAP");
        if (!gap) return "the frame above the expected number drew no ResendRequest from it";
        if (!session().established()) return "the session ended at the frame that opened the gap";
        return {};
    }
};

// ── Row: "A second too-high frame while a resend is outstanding is ignored" ──

// The peer withholds two numbers and writes a third: the gap. It then writes
// `frames_above` more frames above the expected number, and only then the replay of
// every number from the gap's first, each with PossDupFlag(43)=Y and
// OrigSendingTime(122), and a TestRequest in sequence. The answer to that TestRequest
// shows that every frame before it has been processed.
//
// fixpp_session_is_established stays true, sampled from the peer's writes to the
// TestRequest's answer; the peer reads no second ResendRequest; and after the replay
// the receive callback has each message once, in order.
void frames_above_then_replay_cell(std::uint32_t frames_above) {
    Dictionary dict;
    PeerLine line{one_session()};
    CEngine engine{dict, {line.spec(kQuietHeartBtInt)}};
    ASSERT_TRUE(engine.start()) << "setup: the engine did not start";
    CSession& s = engine.session(0);
    ASSERT_TRUE(answer_logon(s, line, kQuietHeartBtInt).has_value())
        << "setup: the session did not establish";

    auto const gap = open_gap(line, "M" + std::to_string(line.next_seq + 2));
    ASSERT_TRUE(gap.has_value())
        << "setup: the frame above the expected number drew no ResendRequest from it";
    ASSERT_TRUE(s.established()) << "setup: the session ended at the frame that opened the gap";
    std::size_t const read_before = line.seen.size();

    // The peer's next number is the one after the frame that opened the gap.
    std::uint32_t const last = line.next_seq - 1U + frames_above;
    bool written = true;
    for (std::uint32_t seq = line.next_seq; seq <= last; ++seq) {
        written = written && line.write(line.wire.order(seq, "M" + std::to_string(seq)));
    }
    auto const above_written = steady::now();
    std::vector<std::string> replayed;
    for (std::uint32_t seq = gap->begin; seq <= last; ++seq) {
        std::string const orig = utc_now_sending_time();
        replayed.push_back("M" + std::to_string(seq));
        written = written && line.write(line.wire.poss_dup_order(seq, replayed.back(), orig));
    }
    written = written && line.write(line.wire.test_request(last + 1U, "AFTER-REPLAY"));
    ASSERT_TRUE(written) << "setup: the peer could not write its frames";

    // The session answers a TestRequest at once; kStepBudget bounds the wait and
    // competes with no timer of the session, whose heartbeat interval is longer.
    EstablishedWatch watch{s};
    bool answered = false;
    auto const until = steady::now() + kStepBudget;
    while (!answered && !line.ended() && steady::now() < until) {
        watch.sample();
        if (auto f = line.read_next(std::min(until, steady::now() + kSlice))) {
            answered = is_type(f->frame, "0") && field(f->frame, 112) == "AFTER-REPLAY";
        }
    }
    watch.sample();

    EXPECT_FALSE(watch.first_false.has_value())
        << "fixpp_session_is_established stays true through the frames above the expected "
           "number and the replay"
        << observed(line, watch, above_written,
                    "the peer wrote its frames above the expected number");
    auto const second_requests =
        std::count_if(line.seen.begin() + static_cast<std::ptrdiff_t>(read_before), line.seen.end(),
                      [](Seen const& f) { return is_type(f.frame, "2"); });
    EXPECT_EQ(second_requests, 0) << "the peer reads no second ResendRequest";
    EXPECT_TRUE(answered) << "the session answers a TestRequest written after the replay";
    EXPECT_EQ(s.rec.received_snapshot(), replayed)
        << "after the replay the receive callback has each message once, in order";
}

TEST(Capi537Witness, SecondTooHighFrame_IsIgnored) { frames_above_then_replay_cell(1); }

// The same trace with no second frame above the expected number.
TEST(Capi537Witness, Control_OneTooHighFrame_ReplayIsDelivered) {
    frames_above_then_replay_cell(0);
}

// ── Row: "With a heartbeat interval set, an unfilled gap ends with a Logout" ──

// Witness (1): the peer keeps writing application frames above the expected number,
// and answers each TestRequest.
TEST(Capi537Witness, Deadline_TooHighApplicationFrames_Logout) {
    GapScene scene;
    ASSERT_EQ(scene.open(), "") << "setup";
    EstablishedWatch watch{scene.session()};
    auto const run = run_gap_traffic(
        scene.line, watch, *scene.gap,
        [&](std::uint32_t seq) { return scene.line.wire.order(seq, "ABOVE"); },
        /*answer_test_requests=*/true);
    ASSERT_GE(run.written.size(), 1U) << "setup: no second frame above the expected number";
    expect_deadline_logout(scene.line, watch, *scene.gap, run);
}

// The two other forms of witness (1): every frame the peer writes after opening the
// gap is `stimulus`. A session that reads none of them inside an interval asks for a
// Heartbeat, so a TestRequest read by the peer means its frames did not arrive as
// traffic.
void deadline_form_cell(std::function<std::string(PeerLine&, std::uint32_t)> const& stimulus) {
    GapScene scene;
    ASSERT_EQ(scene.open(), "") << "setup";
    EstablishedWatch watch{scene.session()};
    auto const run = run_gap_traffic(scene.line, watch, *scene.gap,
                                     [&](std::uint32_t seq) { return stimulus(scene.line, seq); });
    ASSERT_GE(run.written.size(), 1U) << "setup: no frame was written into the gap";
    ASSERT_FALSE(read_any(scene.line, "1"))
        << "setup: the session asked for a Heartbeat, so the peer's frames did not reach it "
           "inside every interval"
        << observed(scene.line, watch, scene.gap->at.lo, kSinceGap);
    expect_deadline_logout(scene.line, watch, *scene.gap, run);
}

// The Heartbeat form: a Heartbeat above the expected number.
TEST(Capi537Witness, Deadline_TooHighHeartbeats_Logout) {
    deadline_form_cell([](PeerLine& line, std::uint32_t seq) { return line.wire.heartbeat(seq); });
}

// The PossDup form: an application frame above the expected number with
// PossDupFlag(43)=Y and an OrigSendingTime(122) that is not later than its
// SendingTime(52).
TEST(Capi537Witness, Deadline_TooHighPossDupFrames_Logout) {
    deadline_form_cell([](PeerLine& line, std::uint32_t seq) {
        std::string const orig = utc_now_sending_time();
        return line.wire.poss_dup_order(seq, "DUP", orig);
    });
}

// The Rejects the peer read that name `ref_tag` in RefTagID(371).
std::size_t rejects_naming(PeerLine const& line, std::string_view ref_tag) {
    return static_cast<std::size_t>(std::ranges::count_if(line.seen, [&](Seen const& s) {
        return is_type(s.frame, "3") && field(s.frame, 371) == ref_tag;
    }));
}

// Witness (2): inside every interval the peer writes an application frame above the
// expected number with PossDupFlag(43)=Y and the OrigSendingTime(122) that
// `orig_sending_time` gives. It reads Rejects, then the Logout.
void deadline_after_rejected_poss_dup(std::optional<std::string> const& orig_sending_time) {
    GapScene scene;
    ASSERT_EQ(scene.open(), "") << "setup";
    EstablishedWatch watch{scene.session()};
    auto const run = run_gap_traffic(scene.line, watch, *scene.gap, [&](std::uint32_t seq) {
        return scene.line.wire.poss_dup_order(seq, "DUP", orig_sending_time);
    });
    ASSERT_GE(run.written.size(), 1U) << "setup: no PossDup frame above the expected number";
    EXPECT_GE(rejects_naming(scene.line, "122"), 1U)
        << "the peer reads Rejects naming OrigSendingTime(122) before the Logout"
        << observed(scene.line, watch, scene.gap->at.lo, kSinceGap);
    expect_deadline_logout(scene.line, watch, *scene.gap, run);
}

TEST(Capi537Witness, Deadline_PossDupWithoutOrigSendingTime_RejectsThenLogout) {
    deadline_after_rejected_poss_dup(std::nullopt);
}

// The second form of witness (2): a 122 that does not parse.
TEST(Capi537Witness, Deadline_PossDupWithUnparseableOrigSendingTime_RejectsThenLogout) {
    deadline_after_rejected_poss_dup(std::string{"NOT-A-TIME"});
}

// Witness (3): inside every interval the peer writes a Reset-mode SequenceReset whose
// own MsgSeqNum is above the expected number and whose NewSeqNo(36) is below it. It
// reads a Reject for each, and for the one that meets the deadline a Reject and then
// the Logout.
TEST(Capi537Witness, Deadline_ResetBelowExpected_RejectsThenLogout) {
    GapScene scene;
    ASSERT_EQ(scene.open(), "") << "setup";
    ASSERT_GT(scene.gap->begin, 1U) << "setup: no NewSeqNo below the expected number";
    EstablishedWatch watch{scene.session()};
    auto const run = run_gap_traffic(scene.line, watch, *scene.gap, [&](std::uint32_t seq) {
        return scene.line.wire.sequence_reset(seq, scene.gap->begin - 1U);
    });
    ASSERT_GE(run.written.size(), 1U) << "setup: no SequenceReset above the expected number";
    EXPECT_GE(rejects_naming(scene.line, "36"), 1U)
        << "the peer reads Rejects naming NewSeqNo(36) before the Logout"
        << observed(scene.line, watch, scene.gap->at.lo, kSinceGap);
    // The run ends at the Logout, so the Logout is the last frame read.
    bool reject_then_logout = false;
    if (run.logout && scene.line.seen.size() >= 2) {
        auto const& before = scene.line.seen[scene.line.seen.size() - 2].frame;
        reject_then_logout = is_type(before, "3") && field(before, 371) == "36" &&
                             field(before, 45) == std::to_string(run.written.back());
    }
    EXPECT_TRUE(reject_then_logout)
        << "the frame read just before the Logout is the Reject of the last SequenceReset "
           "the peer wrote";
    expect_deadline_logout(scene.line, watch, *scene.gap, run);
}

// ── Row: "With CompIDs that leave no room for the Logout's text" ─────────────

// A session of its own, whose SenderCompID is the longest at which the peer still
// reads the session's Logon, found on this cell's configuration. The Logout the
// deadline writes carries a text and so does not fit where the Logon just did; the
// ResendRequest for a small expected number does. The peer opens a gap and writes an
// application frame above the expected number inside every interval, which also keeps
// the session from sending a TestRequest.
//
// Sampled from the peer's second frame until half an interval before the deadline,
// fixpp_session_is_established stays true. After two intervals with no advance it
// turns false, the peer has read no Logout, and the connection is still open.
// The control is Deadline_TooHighApplicationFrames_Logout: the same trace at CompIDs
// that leave room, where the peer reads the Logout.
TEST(Capi537Witness, Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilently) {
    Dictionary dict;
    auto const boundary = longest_sender_comp_id_with_logon(dict, kWindowHeartBtInt);
    ASSERT_TRUE(boundary.has_value())
        << "setup: no SenderCompID length was found at which the peer reads the session's "
           "Logon and at one byte more it does not";

    GapScene scene{wire_with_sender_of(*boundary)};
    ASSERT_EQ(scene.open(), "") << "setup, at the boundary CompIDs";
    EstablishedWatch watch{scene.session()};
    auto const run = run_gap_traffic(scene.line, watch, *scene.gap, [&](std::uint32_t seq) {
        return scene.line.wire.order(seq, "ABOVE");
    });
    ASSERT_GE(run.written.size(), 1U) << "setup: no second frame above the expected number";

    // The deadline is two intervals after the session entered the gap, which is no
    // earlier than the peer's write at gap.at.lo.
    auto const before_the_bound = scene.gap->at.lo + (2 * kHb) - (kHb / 2);
    EXPECT_TRUE(!watch.first_false || *watch.first_false >= before_the_bound)
        << "fixpp_session_is_established is still true after the second frame above the "
           "expected number, sampled until half an interval before the deadline"
        << observed(scene.line, watch, scene.gap->at.lo, kSinceGap);
    EXPECT_TRUE(watch.first_false.has_value())
        << "fixpp_session_is_established turns false once the gap has gone two intervals "
           "without an advance"
        << observed(scene.line, watch, scene.gap->at.lo, kSinceGap);
    EXPECT_FALSE(read_any(scene.line, "5"))
        << "the peer reads no Logout" << observed(scene.line, watch, scene.gap->at.lo, kSinceGap);
    EXPECT_TRUE(scene.line.open_through(steady::now() + (kHb / 4)))
        << "the connection is still open after the silent ending (fixpp#534 is where it "
           "closes)";
}

// ── Row: "While a resend is outstanding, a too-high Heartbeat answers" ───────

// The peer opens a gap at E and goes silent. One interval later the liveness loop asks
// for a Heartbeat; the returned TestRequest is that one, read no later than a quarter
// interval after E + HB.
struct GapTestRequest {
    std::string failed;  // empty on success, else the step that failed
    std::string id;      // its TestReqID(112)
};
GapTestRequest read_gap_test_request(GapScene& scene) {
    if (auto failed = scene.open(); !failed.empty()) return {failed, {}};
    auto const& gap = *scene.gap;
    // The sample offsets below are counted on the peer's side; they hold for the
    // session's clock only while the two sides agree on E to within their margin.
    if (gap.at.hi - gap.at.lo >= kHb / 4) {
        return {"the gap opened too slowly for its time to be known to a quarter interval", {}};
    }
    auto const tr = scene.line.read_until([](std::string const& f) { return is_type(f, "1"); },
                                          gap.at.hi + kHb + (kHb / 4));
    if (!tr) return {"no TestRequest was read one interval after the gap opened", {}};
    if (tr->at < gap.at.lo + kHb)
        return {"the TestRequest came before one interval had passed", {}};
    std::string id = field(tr->frame, 112);
    if (id.empty()) return {"the TestRequest carries no TestReqID(112)", {}};
    return {{}, std::move(id)};
}

// The peer answers that TestRequest with a Heartbeat and writes nothing more. The
// Heartbeat's MsgSeqNum is above the expected number, or is the expected number; its
// TestReqID(112) is the TestRequest's, or another.
//
// With the TestRequest's id (witness (1)), the window is one interval wide: a session
// whose TestRequest went unanswered ends at its grace check, at E + 2 HB, and one
// whose answer was taken ends at the grace check of the next TestRequest, near
// E + 3 HB. fixpp_session_is_established is sampled from the answer until E + 2.5 HB,
// and stays true.
//
// With another id (witness (2)), the session ends at that frame, half an interval
// before the unanswered TestRequest's grace check would end it:
// fixpp_session_is_established is false when sampled at E + 1.5 HB, counted from the
// peer's write that opened the gap, which is not later than E.
void gap_heartbeat_cell(Number number, TestReqId id) {
    GapScene scene;
    auto const tr = read_gap_test_request(scene);
    ASSERT_EQ(tr.failed, "") << "setup";
    std::uint32_t const seq =
        number == Number::above_expected ? scene.line.next_seq++ : scene.gap->begin;
    std::string const answer_id = id == TestReqId::the_requested ? tr.id : tr.id + "-OTHER";
    ASSERT_TRUE(scene.line.write(scene.line.wire.heartbeat(seq, answer_id)))
        << "setup: the peer could not write its answer";

    EstablishedWatch watch{scene.session()};
    if (id == TestReqId::the_requested) {
        watch.watch_until(scene.gap->at.hi + (2 * kHb) + (kHb / 2));
        EXPECT_FALSE(watch.first_false.has_value())
            << "fixpp_session_is_established is true two and a half intervals after the gap "
               "opened, the peer's Heartbeat having answered the session's TestRequest"
            << observed(scene.line, watch, scene.gap->at.lo, kSinceGap);
    } else {
        std::this_thread::sleep_until(scene.gap->at.lo + kHb + (kHb / 2));
        watch.sample();
        EXPECT_TRUE(watch.first_false.has_value())
            << "fixpp_session_is_established is false one and a half intervals after the gap "
               "opened, the peer's Heartbeat having carried another TestReqID(112)"
            << observed(scene.line, watch, scene.gap->at.lo, kSinceGap);
    }
}

TEST(Capi537Witness, GapHeartbeat_AnswersTheTestRequest) {
    gap_heartbeat_cell(Number::above_expected, TestReqId::the_requested);
}

TEST(Capi537Witness, GapHeartbeat_OtherTestReqId_EndsTheSession) {
    gap_heartbeat_cell(Number::above_expected, TestReqId::another);
}

// The same answers in a Heartbeat that carries the expected number.
TEST(Capi537Witness, Control_GapHeartbeatInSequence_AnswersTheTestRequest) {
    gap_heartbeat_cell(Number::in_sequence, TestReqId::the_requested);
}

TEST(Capi537Witness, Control_GapHeartbeatInSequence_OtherTestReqId_EndsTheSession) {
    gap_heartbeat_cell(Number::in_sequence, TestReqId::another);
}

// ── Row: "A too-high Heartbeat that opens a gap answers fixpp's TestRequest" ──

// One session at a two-second interval whose peer goes silent after the Logon
// exchange. One interval later the liveness loop asks for a Heartbeat.
struct SilentPeerScene {
    Dictionary dict;
    PeerLine line{one_session()};
    CEngine engine{dict, {line.spec(kWindowHeartBtInt)}};
    time_point test_request_at;  // T: when the peer read the TestRequest
    std::string test_req_id;

    [[nodiscard]] CSession& session() { return engine.session(0); }

    // Starts the engine, answers the Logon and reads the TestRequest. Empty on
    // success, else the step that failed. The TestRequest is read no later than a
    // quarter interval after one interval from the establishment, so that offsets from
    // T hold for the session's clock to that margin.
    [[nodiscard]] std::string read_test_request() {
        if (!engine.start()) return "the engine did not start";
        auto const established = answer_logon(session(), line, kWindowHeartBtInt);
        if (!established) return "the session did not establish";
        auto const tr = line.read_until([](std::string const& f) { return is_type(f, "1"); },
                                        established->hi + kHb + (kHb / 4));
        if (!tr) return "no TestRequest was read one interval after the establishment";
        if (tr->at < established->lo + kHb) {
            return "the TestRequest came before one interval had passed";
        }
        test_request_at = tr->at;
        test_req_id = field(tr->frame, 112);
        return test_req_id.empty() ? "the TestRequest carries no TestReqID(112)" : "";
    }
};

// How far above the expected number the gap-opening Heartbeat is.
constexpr std::uint32_t kHeartbeatAbove = 3;

// The peer answers the TestRequest, read at T, with a Heartbeat. The Heartbeat's
// MsgSeqNum is above the expected number, so that it opens a gap, or is the expected
// number; its TestReqID(112) is the TestRequest's, or another.
//
// With the TestRequest's id (witness (1)), a Heartbeat that opens a gap draws a
// ResendRequest, which the peer reads; the peer fills the gap with a GapFill whose
// NewSeqNo(36) is the Heartbeat's number plus one, because the request runs to 0, and
// writes nothing more. The window is one interval wide: a session whose TestRequest
// went unanswered ends at its grace check, at T + HB, and one whose answer was taken
// ends at the grace check of the next TestRequest, near T + 2 HB.
// fixpp_session_is_established is sampled until T + 1.5 HB, and stays true.
//
// With another id (witness (2)), the peer reads no ResendRequest, and
// fixpp_session_is_established is false when sampled at T + 0.5 HB, half an interval
// before the unanswered TestRequest's grace check.
void silent_peer_heartbeat_cell(Number number, TestReqId id) {
    SilentPeerScene scene;
    ASSERT_EQ(scene.read_test_request(), "") << "setup";
    PeerLine& line = scene.line;
    std::uint32_t const expected = line.next_seq;
    std::uint32_t const seq =
        number == Number::above_expected ? expected + kHeartbeatAbove : expected;
    std::string const answer_id =
        id == TestReqId::the_requested ? scene.test_req_id : scene.test_req_id + "-OTHER";
    ASSERT_TRUE(line.write(line.wire.heartbeat(seq, answer_id)))
        << "setup: the peer could not write its answer";

    EstablishedWatch watch{scene.session()};
    if (id == TestReqId::the_requested) {
        if (number == Number::above_expected) {
            // A quarter interval: the peer's own steps stay well inside the window,
            // which ends at T + 1.5 HB.
            auto const rr = line.read_until(
                [&](std::string const& f) {
                    return is_type(f, "2") && field(f, 7) == std::to_string(expected);
                },
                steady::now() + (kHb / 4));
            ASSERT_TRUE(rr.has_value())
                << "setup: the Heartbeat above the expected number drew no ResendRequest";
            ASSERT_TRUE(line.write(line.wire.gap_fill(expected, seq + 1U)))
                << "setup: the peer could not write the GapFill";
        }
        watch.watch_until(scene.test_request_at + kHb + (kHb / 2));
        EXPECT_FALSE(watch.first_false.has_value())
            << "fixpp_session_is_established is true one and a half intervals after the "
               "TestRequest, which the peer's Heartbeat answered"
            << observed(line, watch, scene.test_request_at, kSinceTestRequest);
    } else {
        auto const sample_at = scene.test_request_at + (kHb / 2);
        auto const rr =
            line.read_until([](std::string const& f) { return is_type(f, "2"); }, sample_at);
        std::this_thread::sleep_until(sample_at);
        watch.sample();
        EXPECT_FALSE(rr.has_value())
            << "the peer reads no ResendRequest"
            << observed(line, watch, scene.test_request_at, kSinceTestRequest);
        EXPECT_TRUE(watch.first_false.has_value())
            << "fixpp_session_is_established is false half an interval after the "
               "TestRequest, the peer's Heartbeat having carried another TestReqID(112)"
            << observed(line, watch, scene.test_request_at, kSinceTestRequest);
    }
}

TEST(Capi537Witness, GapOpeningHeartbeat_AnswersTheTestRequest) {
    silent_peer_heartbeat_cell(Number::above_expected, TestReqId::the_requested);
}

TEST(Capi537Witness, GapOpeningHeartbeat_OtherTestReqId_NoResendRequest) {
    silent_peer_heartbeat_cell(Number::above_expected, TestReqId::another);
}

// The same answers in a Heartbeat that carries the expected number and opens no gap.
TEST(Capi537Witness, Control_HeartbeatInSequence_AnswersTheTestRequest) {
    silent_peer_heartbeat_cell(Number::in_sequence, TestReqId::the_requested);
}

TEST(Capi537Witness, Control_HeartbeatInSequence_OtherTestReqId_EndsTheSession) {
    silent_peer_heartbeat_cell(Number::in_sequence, TestReqId::another);
}

// ── Row: "A ResendRequest that cannot be built ends the session" ─────────────

// The heartbeat interval of that row's cell. Its digits are in D-537-7's inequality:
// the ResendRequest outgrows the Logon only when BeginSeqNo(7) has more digits than
// the Logon's own fields leave room for, so the Logon is kept short by an interval of
// one digit. If the cell reports that every NewSeqNo drew a ResendRequest, this
// configuration leaves no boundary and the Logon must be made shorter still.
constexpr std::uint32_t kUnbuildableHeartBtInt = 9;

// How long that cell watches a session for its end. It is shorter than one interval
// of that cell, so the liveness loop has asked for nothing, and its dead-peer ending
// cannot be what ends the session inside the window.
constexpr std::chrono::milliseconds kUnbuildableWindow{kUnbuildableHeartBtInt * 500};

// What a session at the boundary did with the frame above a Reset-mode
// SequenceReset's NewSeqNo.
struct AfterReset {
    bool drove = false;  // the reset raised the expected number to its NewSeqNo
    std::optional<std::string> begin_seq_no;  // BeginSeqNo(7) of the ResendRequest, if one was read
    bool asked_after = false;     // the peer wrote a TestRequest in sequence after the frame above
    bool answered_after = false;  // and the session answered it
    Ending ending;                // watched only when no ResendRequest was read
    std::string seen;             // the frames the peer read after the Logon exchange
};

// How long the peer waits, after the frame above, before it writes anything more. Its
// length decides only which form of ending the peer of a session that ends at that
// frame sees, never the cell's verdict: see after_reset.
constexpr std::chrono::milliseconds kAloneAfterTheFrameAbove = kStepBudget / 10;

// D-537-7's recipe on a session whose SenderCompID has `sender_len` bytes: the peer
// writes a Reset-mode SequenceReset to `new_seq_no`, and a TestRequest with that
// number, whose answer shows the expected number is now `new_seq_no`. It then writes a
// frame above the expected number, alone.
//   - A ResendRequest read after it: the request was built.
//   - The connection over after it: the session ended at that frame. The peer had
//     written nothing more, so nothing was unread at the close, and it reads
//     end-of-stream.
//   - Neither: the peer writes a TestRequest in sequence. Its answer shows the frame
//     above has been processed and drew nothing. A session that ends only now can
//     close with that TestRequest unread, and the peer's connection is then reset.
AfterReset after_reset(Dictionary const& dict, std::size_t sender_len, std::uint32_t new_seq_no) {
    AfterReset out;
    PeerLine line{wire_with_sender_of(sender_len)};
    CEngine engine{dict, {line.spec(kUnbuildableHeartBtInt)}};
    if (!engine.start()) return out;
    CSession& s = engine.session(0);
    if (!answer_logon(s, line, kUnbuildableHeartBtInt)) return out;
    auto const origin = steady::now();
    auto answer_to = [](std::string_view id) {
        return [id](std::string const& f) { return is_type(f, "0") && field(f, 112) == id; };
    };

    if (!line.write(line.wire.sequence_reset(line.next_seq, new_seq_no)) ||
        !line.write(line.wire.test_request(new_seq_no, "RAISED")) ||
        !line.read_until(answer_to("RAISED"), steady::now() + kStepBudget)) {
        out.seen = summaries(line.seen, origin);
        return out;
    }

    constexpr std::uint32_t kAbove = 5;
    if (!line.write(line.wire.order(new_seq_no + kAbove, "ABOVE"))) return out;
    out.drove = true;
    auto const alone_until = steady::now() + kAloneAfterTheFrameAbove;
    while (!out.begin_seq_no && !line.ended()) {
        auto const f = line.read_next(alone_until);
        if (!f) break;
        if (is_type(f->frame, "2")) out.begin_seq_no = field(f->frame, 7);
    }
    if (!out.begin_seq_no && !line.ended()) {
        out.asked_after = line.write(line.wire.test_request(new_seq_no + 1U, "AFTER"));
        auto const until = steady::now() + kStepBudget;
        while (auto f = line.read_next(until)) {
            if (is_type(f->frame, "2")) out.begin_seq_no = field(f->frame, 7);
            if (answer_to("AFTER")(f->frame)) {
                out.answered_after = true;
                break;
            }
        }
    }
    if (!out.begin_seq_no) out.ending = watch_ending(s, line, kUnbuildableWindow);
    out.seen = summaries(line.seen, origin);
    return out;
}

// One NewSeqNo(36) per digit count a sequence number can have, from two digits up,
// smallest first.
std::vector<std::uint32_t> new_seq_nos_by_digit_count() {
    std::vector<std::uint32_t> out;
    std::uint32_t v = 1;
    for (int digits = 2; digits <= std::numeric_limits<std::uint32_t>::digits10 + 1; ++digits) {
        v *= 10;
        out.push_back(v);
    }
    return out;
}

// A session of its own, whose SenderCompID is the longest at which the peer still
// reads the session's Logon, found on this cell's configuration. The NewSeqNo is found
// by building too: the smallest digit count at which the frame above draws no
// ResendRequest. A shorter NewSeqNo at the same CompIDs draws one whose BeginSeqNo(7)
// is the expected number, so the missing request comes from the frame's size and the
// search can report both outcomes.
//
// For the session that drew no ResendRequest: fixpp_session_is_established turns false
// and fixpp closes the connection. Either form of a close by the far side counts, and
// the failure message names the one that was seen (after_reset says which trace gives
// which).
TEST(Capi537Witness, UnbuildableResendRequest_EndsTheSession) {
    Dictionary dict;
    auto const boundary = longest_sender_comp_id_with_logon(dict, kUnbuildableHeartBtInt);
    ASSERT_TRUE(boundary.has_value())
        << "setup: no SenderCompID length was found at which the peer reads the session's "
           "Logon and at one byte more it does not";

    std::optional<AfterReset> unbuilt;
    bool built_at_some_digit_count = false;
    for (auto const new_seq_no : new_seq_nos_by_digit_count()) {
        auto const r = after_reset(dict, *boundary, new_seq_no);
        ASSERT_TRUE(r.drove) << "setup: the Reset-mode SequenceReset to " << new_seq_no
                             << " did not raise the expected number on a session at the "
                                "boundary; the peer read:"
                             << r.seen;
        if (r.begin_seq_no) {
            built_at_some_digit_count = true;
            ASSERT_EQ(*r.begin_seq_no, std::to_string(new_seq_no + 1U))
                << "setup: the ResendRequest does not start at the expected number";
            continue;
        }
        unbuilt = r;
        break;
    }
    ASSERT_TRUE(built_at_some_digit_count)
        << "setup: no NewSeqNo drew a ResendRequest at the boundary, so a missing one would "
           "not show that the frame could not be built";
    ASSERT_TRUE(unbuilt.has_value())
        << "setup: at the longest SenderCompID at which the peer reads the Logon, every "
           "NewSeqNo drew a ResendRequest: this configuration has no boundary at which the "
           "ResendRequest cannot be built";

    // The peer read no ResendRequest: that is how the session was selected.
    EXPECT_TRUE(unbuilt->ending.not_established)
        << "fixpp_session_is_established turns false when the ResendRequest cannot be built"
        << "\n  a TestRequest in sequence after the frame above was "
        << (!unbuilt->asked_after     ? "not written"
            : unbuilt->answered_after ? "answered"
                                      : "not answered")
        << "\n  frames the peer read, by time in ms after the Logon exchange:" << unbuilt->seen;
    auto const connection = unbuilt->ending.connection;
    EXPECT_TRUE(connection == ConnectionEnd::end_of_stream || connection == ConnectionEnd::reset)
        << "fixpp closes the connection; the peer's connection is " << name_of(connection);
}

// The same observers on a session that ends in the same way for another reason. The
// peer raises the expected number to the largest sequence number with a Reset-mode
// SequenceReset and then writes a Heartbeat with that number, in sequence. No next
// number exists, so SeqnumManager::check_inbound refuses the frame;
// Session::on_inbound_active_out_of_sequence_ records Disconnected and returns the
// error for a frame the read pump delivered, and run_read_pump then closes the session
// and its transport. An unbuildable ResendRequest is designed to end that way
// (D-537-7): its leaf records Disconnected and returns an error to the same pump.
// The peer writes nothing after the Heartbeat, so nothing is unread at the close.
TEST(Capi537Witness, Control_SequenceNumberOverflow_EndsTheSessionAndClosesTheConnection) {
    Dictionary dict;
    PeerLine line{one_session()};
    CEngine engine{dict, {line.spec(kUnbuildableHeartBtInt)}};
    ASSERT_TRUE(engine.start()) << "setup: the engine did not start";
    CSession& s = engine.session(0);
    ASSERT_TRUE(answer_logon(s, line, kUnbuildableHeartBtInt).has_value())
        << "setup: the session did not establish";

    constexpr std::uint32_t kLargest = std::numeric_limits<std::uint32_t>::max();
    ASSERT_TRUE(line.write(line.wire.sequence_reset(line.next_seq, kLargest)))
        << "setup: the peer could not write the SequenceReset";
    ASSERT_TRUE(line.write(line.wire.heartbeat(kLargest)))
        << "setup: the peer could not write the Heartbeat";

    auto const ending = watch_ending(s, line, kUnbuildableWindow);
    EXPECT_TRUE(ending.not_established) << "fixpp_session_is_established turns false";
    EXPECT_TRUE(ending.connection == ConnectionEnd::end_of_stream ||
                ending.connection == ConnectionEnd::reset)
        << "fixpp closes the connection; the peer's connection is " << name_of(ending.connection);
}

// ── fixpp#536: two sessions on one engine ───────────────────────────────────

// How often a peer whose session must stay up writes its keepalive Heartbeat: twice
// per interval, so the session's liveness loop asks for none.
constexpr std::chrono::milliseconds kKeepalive = kHb / 2;

// Two C sessions, A and B, on one engine and so on one clock, each facing a raw peer
// of its own. The member order is the teardown order in reverse: a blocked close is
// joined before the engine is destroyed, and the engine before the peers go.
struct TwoSessions {
    Dictionary dict;
    PeerLine a{Wire{"INI-A", "ACC-A"}};
    PeerLine b{Wire{"INI-B", "ACC-B"}};
    CEngine engine;
    BlockedClose close;

    explicit TwoSessions(std::uint32_t heartbeat_s)
        : engine{dict, {a.spec(heartbeat_s), b.spec(heartbeat_s)}}, heartbeat_s_{heartbeat_s} {}

    [[nodiscard]] CSession& sa() { return engine.session(0); }
    [[nodiscard]] CSession& sb() { return engine.session(1); }

    [[nodiscard]] bool establish() {
        return engine.start() && answer_logon(sa(), a, heartbeat_s_).has_value() &&
               answer_logon(sb(), b, heartbeat_s_).has_value();
    }

    // Both peers write keepalive Heartbeats from now on.
    void keep_both_alive() {
        a.keepalive_due = steady::now() + kKeepalive;
        b.keepalive_due = a.keepalive_due;
    }

private:
    std::uint32_t heartbeat_s_;
};

// The Heartbeats `line`'s peer read from its session's liveness loop, by time after
// `origin`, for a failure message.
std::string loop_heartbeats(PeerLine const& line, time_point origin) {
    std::string out;
    for (auto const& s : line.seen) {
        if (is_loop_heartbeat(s.frame)) {
            out += " " + std::to_string(ms_between(origin, s.at).count());
        }
    }
    return out.empty() ? " (none)" : out + " (ms)";
}

// How one of the two sessions is closed while the other's Heartbeats are counted. The
// letters are those of §6.2's rows.
enum class OtherClose {
    none,            // no session is closed; B's Heartbeats are counted
    by_application,  // fixpp_session_close(A), whose Logout A's peer does not answer
    answered,        // fixpp_session_close(A), whose Logout A's peer answers
    by_engine,       // B's peer drops its connection, and the engine closes B
};

// Two sessions whose peers write keepalive Heartbeats. Once the peer of the session
// that survives has read a Heartbeat from that session's liveness loop, the other
// session is closed in the way `how` names. The survivor's peer, still serviced with
// keepalives, then reads a Heartbeat from the survivor's liveness loop more than one
// interval after the close ended.
//
// One loop sends both frame types (run_liveness_loop), so a Heartbeat read shows the
// loop is alive, and a loop that is alive sends one every interval: one falls in the
// interval that starts one interval after the close, and the bound allows half an
// interval more. A Heartbeat read that late was sent after the close: one sent before
// it is read as soon as the peer is serviced.
void other_sessions_close_cell(OtherClose how) {
    TwoSessions t{kWindowHeartBtInt};
    ASSERT_TRUE(t.establish()) << "setup: the two sessions did not establish";
    bool const b_closes = how == OtherClose::by_engine;
    PeerLine& closing = b_closes ? t.b : t.a;
    PeerLine& surviving = b_closes ? t.a : t.b;
    CSession& closing_session = b_closes ? t.sb() : t.sa();
    CSession& survivor = b_closes ? t.sa() : t.sb();
    t.keep_both_alive();
    // The first Heartbeat of a liveness loop is due one interval after the session's
    // last outbound frame; the bound allows half an interval more.
    auto const from = steady::now();
    ASSERT_TRUE(service({&t.a, &t.b}, kKeepalive, from + kHb + (kHb / 2),
                        [&] { return first_loop_heartbeat_after(surviving, from).has_value(); }))
        << "setup: the survivor's peer read no Heartbeat from its liveness loop before the "
           "other session's close";

    // When the close ended, as the test thread can bound it from above.
    time_point closed = steady::now();
    if (how == OtherClose::by_application || how == OtherClose::answered) {
        closing.keepalive_due.reset();
        t.close.start(closing_session);
        if (how == OtherClose::answered) {
            ASSERT_TRUE(service({&t.a, &t.b}, kKeepalive,
                                t.close.started_at() + (kLogoutTimeout / 4),
                                [&] { return read_any(closing, "5"); }))
                << "setup: A's peer did not read A's Logout";
            ASSERT_TRUE(closing.write(closing.wire.logout(closing.next_seq++)))
                << "setup: A's peer could not write its answer";
        }
        // The close is bounded by the session's logout timeout.
        ASSERT_TRUE(service({&t.a, &t.b}, kKeepalive,
                            t.close.started_at() + kLogoutTimeout + kStepBudget,
                            [&] { return t.close.done(); }))
            << "setup: fixpp_session_close(A) did not return";
        ASSERT_EQ(t.close.rc(), FIXPP_ERR_OK) << "setup: fixpp_session_close(A)";
        auto const took = ms_between(t.close.started_at(), t.close.returned_at());
        if (how == OtherClose::answered) {
            ASSERT_LT(took.count(), (kLogoutTimeout / 2).count())
                << "setup: fixpp_session_close(A) did not end on the peer's answer: it ran "
                   "towards its logout timeout";
        } else {
            ASSERT_GE(took.count(), (kLogoutTimeout / 2).count())
                << "setup: fixpp_session_close(A) did not wait for the answer its peer "
                   "withheld";
        }
        closed = t.close.returned_at();
    } else if (how == OtherClose::by_engine) {
        // fixpp_session_is_established(B) reading false is when the engine's close of B
        // began.
        closing.sock.disconnect();
        auto const dropped = steady::now();
        EstablishedWatch closing_watch{closing_session};
        ASSERT_TRUE(service({&surviving}, kKeepalive, dropped + kStepBudget, [&] {
            closing_watch.sample();
            return closing_watch.first_false.has_value();
        })) << "setup: the engine did not close B after its peer dropped the connection";
        closed = *closing_watch.first_false;
    }

    auto const later = closed + kHb;
    (void)service({&surviving}, kKeepalive, later + kHb + (kHb / 2),
                  [&] { return first_loop_heartbeat_after(surviving, later).has_value(); });
    ASSERT_TRUE(survivor.established())
        << "setup: the surviving session is no longer established, so its missing "
           "Heartbeats would not show that its liveness loop was stopped";
    EXPECT_TRUE(first_loop_heartbeat_after(surviving, later).has_value())
        << "the surviving session's peer reads a Heartbeat from that session's liveness loop "
           "more than one interval after the other session's close ended"
        << "\n  Heartbeats it read from the liveness loop, by time after that close ended:"
        << loop_heartbeats(surviving, closed);
}

// ── Row: "A session's close, whether the application called it or the engine ran it" ──

// Witness (1), the application's close: A is closed, and B's Heartbeats are counted.
TEST(Capi536Witness, ApplicationClose_OtherSessionKeepsItsHeartbeats) {
    other_sessions_close_cell(OtherClose::by_application);
}

// Witness (2), the engine's close: B is closed, and A's Heartbeats are counted.
TEST(Capi536Witness, EngineClose_OtherSessionKeepsItsHeartbeats) {
    other_sessions_close_cell(OtherClose::by_engine);
}

// ── Row: "The peer's answer to a close's Logout on one session" ──────────────

TEST(Capi536Witness, AnsweredLogout_OtherSessionKeepsItsHeartbeats) {
    other_sessions_close_cell(OtherClose::answered);
}

// The same count with no close.
TEST(Capi536Witness, Control_NoClose_OtherSessionKeepsItsHeartbeats) {
    other_sessions_close_cell(OtherClose::none);
}

// ── Row: "Another session's close no longer ends this session's logout wait early" ──

// How much earlier than the logout timeout fixpp_session_close may return. The wait is
// armed after the call starts and the time is counted from before the call, so the
// margin covers only the granularity of the two clocks.
constexpr std::chrono::milliseconds kLogoutMargin{100};

// A's peer does not answer A's Logout. With `b_drops`, half the logout timeout into
// the blocked fixpp_session_close(A), B's peer drops its connection, and the engine
// closes B. fixpp_session_close(A) returns, and A's peer reads end-of-stream, no
// earlier than the logout timeout less kLogoutMargin after the call started.
void logout_wait_cell(bool b_drops) {
    TwoSessions t{kQuietHeartBtInt};
    ASSERT_TRUE(t.establish()) << "setup: the two sessions did not establish";

    t.close.start(t.sa());
    auto const started = t.close.started_at();
    ASSERT_TRUE(service({&t.a}, 0ms, started + (kLogoutTimeout / 4), [&] {
        return read_any(t.a, "5");
    })) << "setup: A's peer did not read A's Logout";
    (void)service({&t.a}, 0ms, started + (kLogoutTimeout / 2), [] { return false; });
    ASSERT_FALSE(t.close.done())
        << "setup: fixpp_session_close(A) had returned half its logout timeout in";
    ASSERT_FALSE(t.a.ended()) << "setup: A's connection had ended half its logout timeout in";

    std::string after_the_drop;
    if (b_drops) {
        t.b.sock.disconnect();
        auto const dropped = steady::now();
        EstablishedWatch b_watch{t.sb()};
        ASSERT_TRUE(service({&t.a}, 0ms, dropped + (kLogoutTimeout / 4), [&] {
            b_watch.sample();
            return b_watch.first_false.has_value();
        })) << "setup: the engine did not close B while A's logout wait was still running";
        after_the_drop = "; B's peer dropped its connection " +
                         std::to_string(ms_between(started, dropped).count()) +
                         " ms after the call started";
    }

    // The close is bounded by the session's logout timeout.
    (void)service({&t.a}, 0ms, started + kLogoutTimeout + kStepBudget,
                  [&] { return t.close.done() && t.a.sock.ended_at().has_value(); });
    ASSERT_TRUE(t.close.done()) << "setup: fixpp_session_close(A) did not return";
    EXPECT_EQ(t.close.rc(), FIXPP_ERR_OK) << "fixpp_session_close(A)";
    EXPECT_GE(ms_between(started, t.close.returned_at()).count(),
              (kLogoutTimeout - kLogoutMargin).count())
        << "fixpp_session_close(A) returns no earlier than its logout timeout after the call "
           "started (times in ms)"
        << after_the_drop;
    // The engine's close shows to the peer as end-of-stream or as a reset, as the
    // Capi537Witness cells accept; the property read here is when the connection ended.
    auto const ended = t.a.sock.ended_at();
    std::error_code const end_code = t.a.sock.end_code();
    std::string const route =
        !ended ? std::string{"open"}
        : end_code == asio::error::eof
            ? std::string{"end-of-stream"}
            : "a read error (" + end_code.message() + ", " + std::to_string(end_code.value()) + ")";
    ::testing::Test::RecordProperty("a_connection_end", route);
    ASSERT_TRUE(ended.has_value())
        << "A's connection ends (end-of-stream or a read error); A's peer read: " << route;
    EXPECT_GE(ms_between(started, *ended).count(), (kLogoutTimeout - kLogoutMargin).count())
        << "A's connection ends (" << route
        << ") no earlier than the logout timeout after the call started (times in ms)"
        << after_the_drop;
}

TEST(Capi536Witness, OtherSessionsClose_DoesNotEndTheLogoutWait) { logout_wait_cell(true); }

// The same wait while session B stays up.
TEST(Capi536Witness, Control_NoOtherClose_LogoutWaitRunsToItsTimeout) { logout_wait_cell(false); }

// ── Row: "After a 141=Y exchange that leaves the first outbound number at 2" ──

// A C initiator; with `reset_exchange`, it sets ResetSeqNumFlag on its Logon and the
// peer acknowledges with 141=Y. One fixpp_session_send: the peer reads the message
// under MsgSeqNum 2, the first outbound number after the Logon. The peer then asks for
// that number again and reads the message with PossDupFlag(43)=Y, not a GapFill.
void resend_after_logon_cell(bool reset_exchange) {
    Dictionary dict;
    PeerLine line{one_session()};
    CEngine engine{dict, {line.spec(kQuietHeartBtInt, reset_exchange)}};
    ASSERT_TRUE(engine.start()) << "setup: the engine did not start";
    CSession& s = engine.session(0);
    ASSERT_TRUE(answer_logon(s, line, kQuietHeartBtInt, reset_exchange).has_value())
        << "setup: the session did not establish";

    ASSERT_EQ(s.send_order("SENT"), FIXPP_ERR_OK) << "setup: fixpp_session_send";
    auto const sent = line.read_until([](std::string const& f) { return is_type(f, "D"); },
                                      steady::now() + kStepBudget);
    ASSERT_TRUE(sent.has_value()) << "setup: the peer did not read the message";
    ASSERT_EQ(field(sent->frame, 34), "2")
        << "setup: the Logon exchange did not leave the first outbound number at 2";
    ASSERT_EQ(field(sent->frame, 11), "SENT");
    ASSERT_EQ(field(sent->frame, 43), "") << "setup: the original carries no PossDupFlag(43)";

    ASSERT_TRUE(line.write(line.wire.resend_request(line.next_seq++, 2)))
        << "setup: the peer could not write its ResendRequest";
    auto const answer = line.read_next(steady::now() + kStepBudget);
    ASSERT_TRUE(answer.has_value()) << "setup: the session did not answer the ResendRequest";

    bool const replayed = is_type(answer->frame, "D") && field(answer->frame, 34) == "2" &&
                          field(answer->frame, 43) == "Y" && field(answer->frame, 11) == "SENT";
    EXPECT_TRUE(replayed)
        << "the peer reads the message again, under its own MsgSeqNum, with PossDupFlag(43)=Y; "
           "it read: "
        << printable(answer->frame);
    EXPECT_FALSE(is_type(answer->frame, "4") && field(answer->frame, 123) == "Y")
        << "the answer to the ResendRequest is not a GapFill";
}

TEST(Capi538Witness, SentAfterResetLogon_IsReplayed) { resend_after_logon_cell(true); }

// The same exchange on a session that does not reset at its Logon.
TEST(Capi538Witness, Control_NoResetLogon_SentMessageIsReplayed) { resend_after_logon_cell(false); }

}  // namespace
