// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/recovery_liveness_capi_support.hpp — fixpp#536, fixpp#537, fixpp#538
// (.specify/536-537-538-recovery-liveness.md §6.2): what the C-ABI witnesses of the 1.12
// clause need on top of inbound_frame_dispositions_capi_support.hpp.
//
// That header's rig is one engine with one session whose CompIDs are fixed. Here the cell
// chooses the CompIDs: two sessions on one engine must differ in them, and two witnesses
// lengthen one to a boundary they find. A peer frame that carries other CompIDs than its
// session's ends that session at the CompID guard. So this header adds:
//   - Wire: the peer's frames for one session, by that session's CompIDs;
//   - CEngine: one C-ABI engine with one or more initiator sessions, each connecting to a
//     raw peer of its own;
//   - PeerLine: one such peer, with the time at which it read each frame;
//   - EstablishedWatch: fixpp_session_is_established, sampled while a peer waits;
//   - BlockedClose: fixpp_session_close on a second thread, because the call blocks;
//   - the finder of the longest SenderCompID at which a peer still reads the Logon.
//
// The engine side is driven through the public C ABI only. This header names no symbol
// the 1.12 clause adds, so the witnesses compile and run on a tree without the change.
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "fix/c_api/dict.h"
#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"
#include "fix/c_api/version.h"
#include "inbound_frame_dispositions_capi_support.hpp"

namespace fixpp::capi_test::rl {

using ifd::fix_fields;
using ifd::frame44;
using ifd::kStepBudget;
using ifd::order_fields;
using ifd::RawAcceptor;
using ifd::Recorder;
using ifd::utc_now_sending_time;

using steady = std::chrono::steady_clock;
using time_point = steady::time_point;

// How long one bounded peer read, or one pause between two samples, lasts while a cell
// waits for something else.
inline constexpr std::chrono::milliseconds kSlice{10};

inline std::chrono::milliseconds ms_between(time_point from, time_point to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from);
}

// ── Frames ──────────────────────────────────────────────────────────────────

// The value of `tag` in a frame, matched at a field boundary; empty when absent.
inline std::string field(std::string_view frame, int tag) {
    std::string const needle = "\x01" + std::to_string(tag) + "=";
    auto pos = frame.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    auto const end = frame.find('\x01', pos);
    return std::string{frame.substr(pos, end == std::string_view::npos ? frame.npos : end - pos)};
}

inline bool is_type(std::string_view frame, std::string_view msg_type) {
    return RawAcceptor::has_field(frame, "35=" + std::string{msg_type});
}

// A Heartbeat the liveness loop sent: one that answers no TestRequest. A Heartbeat with
// TestReqID(112) is the inbound arm's reply to a peer's TestRequest and says nothing
// about the loop.
inline bool is_loop_heartbeat(std::string_view frame) {
    return is_type(frame, "0") && field(frame, 112).empty();
}

// A frame for a failure message: SOH shown as '|'.
inline std::string printable(std::string_view frame) {
    std::string out{frame};
    std::ranges::replace(out, '\x01', '|');
    return out;
}

// The frames a peer writes to one session, by that session's CompIDs.
struct Wire {
    std::string engine_comp_id;
    std::string peer_comp_id;

    // The standard header fields after MsgType(35).
    [[nodiscard]] std::string header(std::uint32_t seq) const {
        return fix_fields({{34, std::to_string(seq)},
                           {49, peer_comp_id},
                           {52, utc_now_sending_time()},
                           {56, engine_comp_id}});
    }

    // The peer's Logon reply. With `reset`, it carries ResetSeqNumFlag(141)=Y, as the
    // Logon of a session that sets the flag does.
    [[nodiscard]] std::string logon_reply(std::uint32_t heartbeat_s, bool reset) const {
        std::string body =
            "35=A\x01" + header(1) + fix_fields({{98, "0"}, {108, std::to_string(heartbeat_s)}});
        if (reset) body += fix_fields({{141, "Y"}});
        return frame44(body);
    }

    // A NewOrderSingle carrying ClOrdID(11) = `cl_ord_id`.
    [[nodiscard]] std::string order(std::uint32_t seq, std::string_view cl_ord_id) const {
        return frame44("35=D\x01" + header(seq) + order_fields(cl_ord_id));
    }

    // The same with PossDupFlag(43)=Y and, when given, OrigSendingTime(122) =
    // `orig_sending_time`. A value taken from the clock before this call is not later
    // than the frame's SendingTime(52).
    [[nodiscard]] std::string poss_dup_order(
        std::uint32_t seq, std::string_view cl_ord_id,
        std::optional<std::string> const& orig_sending_time) const {
        std::string dup = fix_fields({{43, "Y"}});
        if (orig_sending_time) dup += fix_fields({{122, *orig_sending_time}});
        return frame44("35=D\x01" + header(seq) + dup + order_fields(cl_ord_id));
    }

    // A Heartbeat; with `test_req_id`, one that carries TestReqID(112).
    [[nodiscard]] std::string heartbeat(std::uint32_t seq,
                                        std::string_view test_req_id = {}) const {
        std::string body = "35=0\x01" + header(seq);
        if (!test_req_id.empty()) body += fix_fields({{112, std::string{test_req_id}}});
        return frame44(body);
    }

    [[nodiscard]] std::string test_request(std::uint32_t seq, std::string_view id) const {
        return frame44("35=1\x01" + header(seq) + fix_fields({{112, std::string{id}}}));
    }

    // A ResendRequest from `begin` through the current number (EndSeqNo(16) = 0).
    [[nodiscard]] std::string resend_request(std::uint32_t seq, std::uint32_t begin) const {
        return frame44("35=2\x01" + header(seq) +
                       fix_fields({{7, std::to_string(begin)}, {16, "0"}}));
    }

    // A SequenceReset in GapFill mode, as a resend answer carries it.
    [[nodiscard]] std::string gap_fill(std::uint32_t seq, std::uint32_t new_seq_no) const {
        std::string const orig = utc_now_sending_time();
        return frame44(
            "35=4\x01" + header(seq) +
            fix_fields({{43, "Y"}, {122, orig}, {123, "Y"}, {36, std::to_string(new_seq_no)}}));
    }

    // A SequenceReset in Reset mode: no GapFillFlag(123).
    [[nodiscard]] std::string sequence_reset(std::uint32_t seq, std::uint32_t new_seq_no) const {
        return frame44("35=4\x01" + header(seq) + fix_fields({{36, std::to_string(new_seq_no)}}));
    }

    [[nodiscard]] std::string logout(std::uint32_t seq) const {
        return frame44("35=5\x01" + header(seq));
    }
};

// ── The C-ABI engine ────────────────────────────────────────────────────────

// The bundled FIX 4.4 dictionary, loaded once for the engines of a cell.
struct Dictionary {
    fixpp_dict_t* handle = nullptr;

    Dictionary() {
        if (fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX44.xml", &handle) != FIXPP_ERR_OK) {
            handle = nullptr;
        }
    }
    Dictionary(Dictionary const&) = delete;
    Dictionary& operator=(Dictionary const&) = delete;
    ~Dictionary() {
        if (handle != nullptr) fixpp_dict_destroy(handle);
    }
};

// One initiator session: its CompIDs, the port of the peer it connects to, its
// heartbeat interval, and whether it resets the sequence numbers at its Logon, which
// puts ResetSeqNumFlag(141)=Y on that Logon.
struct SessionSpec {
    std::string sender_comp_id;
    std::string target_comp_id;
    std::uint16_t port = 0;
    std::uint32_t heartbeat_s = 0;
    bool reset_on_logon = true;
};

// One session of a CEngine: its handle, and what its callbacks saw.
struct CSession {
    fixpp_session_t* handle = nullptr;
    Recorder rec;

    // These calls act on the session the handle points to, so they are non-const
    // although they write no member.
    // NOLINTBEGIN(readability-make-member-function-const)
    [[nodiscard]] bool established() {
        bool est = false;
        return fixpp_session_is_established(handle, &est) == FIXPP_ERR_OK && est;
    }

    // Polls fixpp_session_is_established until true or the budget elapses.
    [[nodiscard]] bool wait_established(std::chrono::milliseconds budget = kStepBudget) {
        auto const until = steady::now() + budget;
        for (;;) {
            if (established()) return true;
            if (steady::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }

    fixpp_error_t send_order(std::string_view cl_ord_id) {
        std::string const p = ifd::order_payload(cl_ord_id);
        return fixpp_session_send(handle, reinterpret_cast<const uint8_t*>(p.data()), p.size());
    }
    // NOLINTEND(readability-make-member-function-const)
};

// One C-ABI engine on a real-time clock, over `dict`, with one initiator session per
// spec, in the order given. Each session is configured as ifd::CInitiator configures
// its initiator, FIX 4.4 on plain TCP with the recording callbacks, except that the
// spec says whether it resets at its Logon. Every session is opened before the engine starts, so
// the sessions share the engine's clock. A failed step is not reported here: start() returns false,
// and the cell reports it.
class CEngine {
public:
    CEngine(Dictionary const& dict, std::vector<SessionSpec> const& specs) {
        if (dict.handle == nullptr) return;
        fixpp_engine_config_t* ec = nullptr;
        if (fixpp_engine_config_create(&ec) != FIXPP_ERR_OK ||
            fixpp_engine_config_set_realtime_clock(ec) != FIXPP_ERR_OK ||
            fixpp_engine_create(ec, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR,
                                &engine_) != FIXPP_ERR_OK) {
            fixpp_engine_config_destroy(ec);
            engine_ = nullptr;
            return;
        }
        bool ok = true;
        for (auto const& spec : specs) {
            ok = ok && open(dict, spec);
        }
        opened_ = ok;
    }

    CEngine(CEngine const&) = delete;
    CEngine& operator=(CEngine const&) = delete;

    ~CEngine() {
        if (engine_ != nullptr) fixpp_engine_destroy(engine_);
    }

    // NOLINTNEXTLINE(readability-make-member-function-const)
    [[nodiscard]] bool start() { return opened_ && fixpp_engine_start(engine_) == FIXPP_ERR_OK; }

    [[nodiscard]] CSession& session(std::size_t i) { return sessions_.at(i); }

private:
    bool open(Dictionary const& dict, SessionSpec const& spec) {
        fixpp_session_config_t* sc = nullptr;
        if (fixpp_session_config_create(&sc) != FIXPP_ERR_OK) return false;
        bool const configured =
            fixpp_session_config_set_comp_ids(sc, spec.sender_comp_id.c_str(),
                                              spec.target_comp_id.c_str()) == FIXPP_ERR_OK &&
            fixpp_session_config_set_begin_string(sc, "FIX.4.4") == FIXPP_ERR_OK &&
            fixpp_session_config_set_role(sc, FIXPP_ROLE_INITIATOR) == FIXPP_ERR_OK &&
            fixpp_session_config_set_heartbeat_seconds(sc, spec.heartbeat_s) == FIXPP_ERR_OK &&
            fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr,
                                              nullptr) == FIXPP_ERR_OK &&
            fixpp_session_config_set_reset_on_logon(sc, spec.reset_on_logon) == FIXPP_ERR_OK &&
            fixpp_session_config_set_reset_seqnum_policy(
                sc, FIXPP_RESET_SEQNUM_BILATERAL_LENIENT) == FIXPP_ERR_OK &&
            fixpp_session_config_set_dictionary(sc, dict.handle) == FIXPP_ERR_OK &&
            fixpp_session_config_set_tcp_endpoint(sc, "127.0.0.1", spec.port) == FIXPP_ERR_OK;
        CSession& s = sessions_.emplace_back();
        if (!configured || fixpp_session_open(engine_, sc, &s.handle) != FIXPP_ERR_OK) {
            fixpp_session_config_destroy(sc);
            return false;
        }
        return fixpp_session_register_callback(s.handle, ifd::record_receive, &s.rec) ==
                   FIXPP_ERR_OK &&
               fixpp_session_register_send_callback(s.handle, ifd::record_to_app, &s.rec) ==
                   FIXPP_ERR_OK;
    }

    fixpp_engine_t* engine_ = nullptr;
    std::deque<CSession> sessions_;  // a CSession does not move: its callbacks hold its address
    bool opened_ = false;
};

// ── The peer of one session ─────────────────────────────────────────────────

// A frame a peer read, and when the read returned.
struct Seen {
    std::string frame;
    time_point at;
};

// The raw peer of one session: its frames, its socket, the MsgSeqNum of its next new
// frame, and every frame it read through read_next, in order.
struct PeerLine {
    Wire wire;
    RawAcceptor sock;
    std::uint32_t next_seq = 2;  // the Logon reply takes 1
    std::vector<Seen> seen;
    // When service() next writes this peer's keepalive Heartbeat; empty for a peer that
    // writes none.
    std::optional<time_point> keepalive_due;
    // A read through read_next failed with an error that is not end-of-stream.
    bool read_failed = false;

    explicit PeerLine(Wire w) : wire(std::move(w)) {}

    [[nodiscard]] SessionSpec spec(std::uint32_t heartbeat_s, bool reset_on_logon = true) const {
        return {wire.engine_comp_id, wire.peer_comp_id, sock.port(), heartbeat_s, reset_on_logon};
    }

    bool write(std::string const& frame) { return sock.write(frame); }

    // The next frame the session sent, or nullopt at `until`, EOF or an error. Those are
    // the three ways RawAcceptor::read_frame returns nothing, so a nullopt before the
    // bound that is not EOF is an error; one at the bound says nothing.
    std::optional<Seen> read_next(time_point until) {
        auto f = sock.read_frame(until);
        if (!f) {
            if (!sock.eof() && steady::now() < until) read_failed = true;
            return std::nullopt;
        }
        seen.push_back({std::move(*f), steady::now()});
        return seen.back();
    }

    // Reads frames until one satisfies `pred`, which is returned.
    template <class Pred>
    std::optional<Seen> read_until(Pred pred, time_point until) {
        for (;;) {
            auto s = read_next(until);
            if (!s) return std::nullopt;
            if (pred(s->frame)) return s;
        }
    }

    // The peer's reads have shown its connection is over: end-of-stream, or an error.
    [[nodiscard]] bool ended() const { return sock.eof() || read_failed; }

    // Reads until `until`; true if no read has shown the connection is over by then.
    [[nodiscard]] bool open_through(time_point until) {
        while (read_next(until)) {
        }
        return !ended();
    }
};

// Two times that bracket an event on the session's side, as its peer can bound it: `lo`
// before the peer's write that caused it, `hi` after the peer saw its effect.
struct Span {
    time_point lo;
    time_point hi;
};

// Accepts the session's connection, reads its Logon and answers it; `reset` is the
// session's reset_on_logon. The session became established between the returned times.
// The engine is started.
inline std::optional<Span> answer_logon(CSession& s, PeerLine& line, std::uint32_t heartbeat_s,
                                        bool reset = true) {
    if (!line.sock.accept() || !line.sock.read_logon()) return std::nullopt;
    Span at{steady::now(), {}};
    if (!line.write(line.wire.logon_reply(heartbeat_s, reset)) || !s.wait_established()) {
        return std::nullopt;
    }
    at.hi = steady::now();
    line.next_seq = 2;
    return at;
}

// A gap the peer opened: the first MsgSeqNum the session misses, and when it opened.
struct Gap {
    std::uint32_t begin = 0;
    Span at;
};

// Opens a gap: the peer withholds its next two numbers, writes an order with the number
// after them, and reads the ResendRequest that order draws, from the first withheld
// number through 0. The peer's next new frame is then above the gap.
inline std::optional<Gap> open_gap(PeerLine& line, std::string_view cl_ord_id) {
    constexpr std::uint32_t kWithheld = 2;
    Gap gap{line.next_seq, {steady::now(), {}}};
    if (!line.write(line.wire.order(gap.begin + kWithheld, cl_ord_id))) return std::nullopt;
    auto const rr = line.read_until(
        [&](std::string const& f) {
            return is_type(f, "2") && field(f, 7) == std::to_string(gap.begin) &&
                   field(f, 16) == "0";
        },
        steady::now() + kStepBudget);
    if (!rr) return std::nullopt;
    gap.at.hi = rr->at;
    line.next_seq = gap.begin + kWithheld + 1;
    return gap;
}

// Services `lines` until `done()` is true (returned as true) or `until` (false). Each
// peer reads in slices. A peer with keepalive_due set writes an in-sequence Heartbeat
// every `keepalive_period`, which keeps its session's liveness loop from asking for
// one. It never writes a TestRequest: the session would answer with a Heartbeat of its
// own, and a cell that counts the liveness loop's Heartbeats must not be offered that
// one.
template <class Done>
bool service(std::initializer_list<PeerLine*> lines, std::chrono::milliseconds keepalive_period,
             time_point until, Done done) {
    for (;;) {
        if (done()) return true;
        if (steady::now() >= until) return false;
        bool read_any = false;
        for (PeerLine* line : lines) {
            if (line->ended()) continue;
            if (line->keepalive_due && steady::now() >= *line->keepalive_due) {
                (void)line->write(line->wire.heartbeat(line->next_seq++));
                *line->keepalive_due += keepalive_period;
            }
            (void)line->read_next(std::min(until, steady::now() + kSlice));
            read_any = true;
        }
        if (!read_any) std::this_thread::sleep_for(kSlice);
    }
}

// The first frame `line` read after `after` that the liveness loop sent as a Heartbeat.
inline std::optional<Seen> first_loop_heartbeat_after(PeerLine const& line, time_point after) {
    for (auto const& s : line.seen) {
        if (s.at > after && is_loop_heartbeat(s.frame)) return s;
    }
    return std::nullopt;
}

// ── Observers through the C ABI ─────────────────────────────────────────────

// fixpp_session_is_established, sampled while a peer waits: when it first read false.
struct EstablishedWatch {
    CSession& session;
    std::optional<time_point> first_false;

    explicit EstablishedWatch(CSession& s) : session(s) {}

    void sample() {
        if (!first_false && !session.established()) first_false = steady::now();
    }

    // Samples in slices until it has read false, or until `until`.
    void watch_until(time_point until) {
        for (;;) {
            sample();
            if (first_false || steady::now() >= until) return;
            std::this_thread::sleep_for(kSlice);
        }
    }
};

// How a peer's connection stands. The far side closing it shows in one of two forms.
// A socket closed with nothing unread ends the stream, and the peer reads
// end-of-stream. A socket closed while bytes its peer wrote are still unread is reset:
// the peer's read then fails with an error, and so does its next write.
enum class ConnectionEnd { open, end_of_stream, reset };

inline std::string_view name_of(ConnectionEnd end) {
    switch (end) {
        case ConnectionEnd::end_of_stream:
            return "ended by end-of-stream";
        case ConnectionEnd::reset:
            return "ended by a reset (a read failed, then a write)";
        case ConnectionEnd::open:
            break;
    }
    return "open";
}

// How `line`'s connection stands now. It is `reset` only when a read failed before its
// bound without end-of-stream and one of two writes made here, a pause apart, fails too. A read
// that returned at its bound says nothing, and the connection counts as open. Call it once, when
// the cell has no more to write: where a read failed, it writes a few bytes that are no frame.
inline ConnectionEnd connection_end(PeerLine& line) {
    if (line.sock.eof()) return ConnectionEnd::end_of_stream;
    if (!line.read_failed) return ConnectionEnd::open;
    // The first write after the far side closed can still be accepted by the local
    // socket; one made a little later fails. A connection that is open accepts both.
    std::string const no_frame = "8=FIX.4.4\x01";
    if (!line.write(no_frame)) return ConnectionEnd::reset;
    std::this_thread::sleep_for(20 * kSlice);
    return line.write(no_frame) ? ConnectionEnd::open : ConnectionEnd::reset;
}

// How a session ended, as a C caller and its peer see it inside a window.
struct Ending {
    bool not_established = false;  // fixpp_session_is_established read false
    ConnectionEnd connection = ConnectionEnd::open;
};

// Samples fixpp_session_is_established and reads `line` until both have shown the
// session's end, or until `window` has passed.
inline Ending watch_ending(CSession& s, PeerLine& line, std::chrono::milliseconds window) {
    EstablishedWatch watch{s};
    auto const until = steady::now() + window;
    while (steady::now() < until && !(watch.first_false && line.ended())) {
        watch.sample();
        if (line.ended()) {
            std::this_thread::sleep_for(kSlice);
        } else {
            (void)line.read_next(std::min(until, steady::now() + kSlice));
        }
    }
    return {watch.first_false.has_value(), connection_end(line)};
}

// fixpp_session_close on a second thread: the call blocks its caller until the close
// has completed. Declare it after the CEngine, so that it joins before the engine is
// destroyed; the close is bounded by the session's own logout timeout.
class BlockedClose {
public:
    BlockedClose() = default;
    BlockedClose(BlockedClose const&) = delete;
    BlockedClose& operator=(BlockedClose const&) = delete;
    ~BlockedClose() {
        if (thread_.joinable()) thread_.join();
    }

    void start(CSession& s) {
        started_at_ = steady::now();
        thread_ = std::thread([this, handle = s.handle] {
            rc_ = fixpp_session_close(handle);
            returned_at_ = steady::now();
            done_.store(true, std::memory_order_release);
        });
    }

    // Just before the call was made.
    [[nodiscard]] time_point started_at() const { return started_at_; }
    [[nodiscard]] bool done() const { return done_.load(std::memory_order_acquire); }
    // Valid once done() is true.
    [[nodiscard]] fixpp_error_t rc() const { return rc_; }
    [[nodiscard]] time_point returned_at() const { return returned_at_; }

private:
    std::thread thread_;
    std::atomic<bool> done_{false};
    time_point started_at_;
    time_point returned_at_;
    fixpp_error_t rc_ = -1;
};

// ── The finder of the Logon boundary ────────────────────────────────────────

inline constexpr char const* kBoundaryPeerCompId = "ACC-RL";

// The Wire of a session whose SenderCompID has `sender_len` bytes.
inline Wire wire_with_sender_of(std::size_t sender_len) {
    return Wire{std::string(sender_len, 'S'), kBoundaryPeerCompId};
}

enum class LogonProbe { read, not_written, inconclusive };

// One session whose SenderCompID has `sender_len` bytes, started against a peer:
// `read` when the peer reads the session's Logon, `not_written` when the session closes
// the connection without one, `inconclusive` when neither happens within the budget.
inline LogonProbe probe_logon(Dictionary const& dict, std::size_t sender_len,
                              std::uint32_t heartbeat_s) {
    PeerLine line{wire_with_sender_of(sender_len)};
    CEngine engine{dict, {line.spec(heartbeat_s)}};
    if (!engine.start() || !line.sock.accept()) return LogonProbe::inconclusive;
    auto const until = steady::now() + kStepBudget;
    while (auto s = line.read_next(until)) {
        if (is_type(s->frame, "A")) return LogonProbe::read;
    }
    return line.sock.eof() ? LogonProbe::not_written : LogonProbe::inconclusive;
}

// The longest SenderCompID length at which a peer still reads the Logon of a session
// with this heartbeat interval, or nullopt when the search found none. A longer CompID
// makes a longer Logon, so the search doubles to the first length at which no Logon is
// written and then bisects; the result is checked on both sides before it is returned.
// The length depends on the whole Logon, so a cell finds it on its own configuration.
inline std::optional<std::size_t> longest_sender_comp_id_with_logon(Dictionary const& dict,
                                                                    std::uint32_t heartbeat_s) {
    constexpr std::size_t kSearchCap = 1U << 14U;
    bool inconclusive = false;
    auto written = [&](std::size_t len) {
        auto const p = probe_logon(dict, len, heartbeat_s);
        inconclusive = inconclusive || p == LogonProbe::inconclusive;
        return p == LogonProbe::read;
    };
    std::size_t lo = 1;
    if (!written(lo)) return std::nullopt;
    std::size_t hi = 2;
    while (hi <= kSearchCap && written(hi)) {
        lo = hi;
        hi *= 2;
    }
    if (hi > kSearchCap || inconclusive) return std::nullopt;
    while (hi - lo > 1) {
        auto const mid = lo + ((hi - lo) / 2);
        (written(mid) ? lo : hi) = mid;
    }
    if (!written(lo) || written(lo + 1) || inconclusive) return std::nullopt;
    return lo;
}

}  // namespace fixpp::capi_test::rl
