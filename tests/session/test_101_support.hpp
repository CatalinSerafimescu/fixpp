#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_support.hpp
//
// 101-inbound-reject-fidelity, tasks.md T005: the rig the Session witnesses of tasks.md
// `## Witnesses` share. A real Session per cell, acceptor or initiator, validating or not, on
// FIX 4.2, FIX 4.4 or the FIXT shape of 099's `T` (FIXT11 transport, FIX50SP2 and FIX Latest
// registered). Raw frames go through on_inbound_frame with BodyLength(9) and CheckSum(10)
// computed. What the session sends is captured through cfg.transport_send and decoded; the
// callbacks are counted; NextNumIn and the next outbound number are read through
// session_test_access; the ending is a value.
//
// The dictionaries, the frame header and the route-set adoption are 099's
// (tests/session/test_099_fixt_support.hpp): this header reuses them and copies none.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/log/logger.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/log_capture.hpp"
#include "support/session_test_access.hpp"
#include "test_099_fixt_support.hpp"

namespace fixpp::session::test101 {

namespace t99 = fixpp::session::test099;
namespace dd = fixpp::dict;
using fixpp::core::expected_t;
using t99::Arm;
using t99::DictPtr;

// ── QuickFIX C++'s SessionRejectReason texts ────────────────────────────────────────────
//
// Copied from the reference engine's src/C++/Values.h (`SessionRejectReason_*_TEXT`), at the
// commit the evidence file records for tasks.md T004. Never retyped from memory: re-derive with
// `grep -n "SessionRejectReason_.*_TEXT" reference-engines/quickfix-cpp/src/C++/Values.h`.
inline constexpr std::string_view kTextInvalidTagNumber = "Invalid tag number";      // 0
inline constexpr std::string_view kTextRequiredTagMissing = "Required tag missing";  // 1
inline constexpr std::string_view kTextTagNotDefined =
    "Tag not defined for this message type";                                               // 2
inline constexpr std::string_view kTextTagWithoutValue = "Tag specified without a value";  // 4
inline constexpr std::string_view kTextValueIncorrect =
    "Value is incorrect (out of range) for this tag";                                        // 5
inline constexpr std::string_view kTextIncorrectFormat = "Incorrect data format for value";  // 6
inline constexpr std::string_view kTextInvalidMsgType = "Invalid MsgType";                   // 11
inline constexpr std::string_view kTextTagMoreThanOnce = "Tag appears more than once";       // 13
inline constexpr std::string_view kTextOutOfOrder = "Tag specified out of required order";   // 14

// ── Session kinds ───────────────────────────────────────────────────────────────────────

enum class Kind { fix42, fix44, fixt };

inline const char* kind_name(Kind k) {
    switch (k) {
        case Kind::fix42:
            return "FIX42";
        case Kind::fix44:
            return "FIX44";
        case Kind::fixt:
            return "FIXT";
    }
    return "?";
}

inline std::string begin_of(Kind k) {
    switch (k) {
        case Kind::fix42:
            return "FIX.4.2";
        case Kind::fix44:
            return "FIX.4.4";
        case Kind::fixt:
            return t99::kFT();
    }
    return {};
}

// What one cell's session is. `fixture` replaces the kind's default dictionaries (a FIXT cell
// with its own S or registry); `tweak` runs last on the SessionConfig.
struct Spec {
    Kind kind = Kind::fix44;
    bool validate = true;
    session_role role = session_role::acceptor;
    bool no_application = false;
    std::optional<t99::Fixture> fixture;
    std::function<void(SessionConfig&)> tweak;

    [[nodiscard]] Spec validating(bool on) const {
        Spec s = *this;
        s.validate = on;
        return s;
    }
    [[nodiscard]] Spec off() const { return validating(false); }
    [[nodiscard]] Spec as(session_role r) const {
        Spec s = *this;
        s.role = r;
        return s;
    }
    [[nodiscard]] Spec initiator() const { return as(session_role::initiator); }
    [[nodiscard]] Spec without_application() const {
        Spec s = *this;
        s.no_application = true;
        return s;
    }
    [[nodiscard]] Spec with(t99::Fixture f) const {
        Spec s = *this;
        s.fixture = std::move(f);
        return s;
    }
    [[nodiscard]] Spec with(std::function<void(SessionConfig&)> t) const {
        Spec s = *this;
        s.tweak = std::move(t);
        return s;
    }
};

// The session keeps what it sends in a MemoryStore, so a ResendRequest replays it: a bounded
// store whose frames may be as long as the outbound ceiling, inside the factory's memory guard.
inline void with_memory_store(SessionConfig& c) {
    MemoryStore::Config store;
    store.inbound_capacity = 16;
    store.outbound_capacity = 16;
    store.max_frame_bytes = session_test_access::send_frame_bytes();
    c.store_factory = std::make_shared<MemoryStoreFactory>(store);
}

// A validating acceptor of kind `k`; the members above vary it.
inline Spec spec(Kind k) {
    Spec s;
    s.kind = k;
    return s;
}

// ── The application: every callback counted, fromApp's answer chosen by the cell ────────

class App101 final : public Application {
public:
    using View = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
    int from_app = 0;
    int from_admin = 0;
    int to_admin = 0;
    int to_app = 0;
    bool reject_from_app = false;  // fromApp answers app_do_not_send: a BusinessMessageReject
    std::vector<std::string> to_admin_372;  // RefMsgType(372) of each toAdmin view, or "<absent>"
    std::vector<std::string> to_app_372;
    std::function<void()> to_admin_probe;  // run at each toAdmin, when a cell sets it

    expected_t<void> fromApp(const View&, const SessionId&) override {
        ++from_app;
        if (reject_from_app) {
            return std::unexpected(fixpp::core::error::app_do_not_send);
        }
        return {};
    }
    expected_t<void> fromAdmin(const View&, const SessionId&) override {
        ++from_admin;
        return {};
    }
    void toAdmin(const View& v, const SessionId&) override {
        ++to_admin;
        to_admin_372.push_back(t99::field_text(v, 372));
        if (to_admin_probe) {
            to_admin_probe();
        }
    }
    expected_t<void> toApp(const View& v, const SessionId&) override {
        ++to_app;
        to_app_372.push_back(t99::field_text(v, 372));
        return {};
    }
};

// ── Decoding what the session sent ──────────────────────────────────────────────────────

// One outbound frame's fields in order, each split at its first '='. The session's Reject and
// BusinessMessageReject carry no Data field, so splitting at SOH reads them whole.
struct Decoded {
    std::string raw;
    std::vector<std::pair<int, std::string>> fields;

    [[nodiscard]] bool has(int tag) const {
        return std::ranges::any_of(fields, [tag](auto const& f) { return f.first == tag; });
    }
    // The first value of `tag`, or "<absent>".
    [[nodiscard]] std::string get(int tag) const {
        for (auto const& f : fields) {
            if (f.first == tag) {
                return f.second;
            }
        }
        return "<absent>";
    }
};

inline Decoded decode(const std::string& frame) {
    Decoded d;
    d.raw = frame;
    std::size_t i = 0;
    while (i < frame.size()) {
        const std::size_t soh = frame.find('\x01', i);
        const std::size_t end = soh == std::string::npos ? frame.size() : soh;
        const std::string_view f{frame.data() + i, end - i};
        const std::size_t eq = f.find('=');
        int tag = -1;
        if (eq != std::string_view::npos && eq > 0) {
            tag = 0;
            for (char c : f.substr(0, eq)) {
                tag = (c >= '0' && c <= '9') ? tag * 10 + (c - '0') : -1;
                if (tag < 0) {
                    break;
                }
            }
        }
        d.fields.emplace_back(tag,
                              std::string(eq == std::string_view::npos ? f : f.substr(eq + 1)));
        i = end + 1;
    }
    return d;
}

// The tag=value set of a frame, without the fields that differ by session or by run
// (BodyLength, CheckSum, SendingTime): SC-002's comparison, which does not compare order.
inline std::vector<std::string> field_set(const Decoded& d) {
    std::vector<std::string> out;
    for (auto const& [tag, value] : d.fields) {
        if (tag == 9 || tag == 10 || tag == 52) {
            continue;
        }
        out.push_back(std::to_string(tag) + "=" + value);
    }
    std::ranges::sort(out);
    return out;
}

// The same set read from a QuickFIX test definition's expected line, written `|`-separated as
// the evidence file's T004 record holds it.
inline std::vector<std::string> field_set_of_def_line(std::string_view line) {
    std::string frame;
    for (char c : line) {
        frame += c == '|' ? '\x01' : c;
    }
    return field_set(decode(frame));
}

// ── The ending as a value (research D6 "Terms") ─────────────────────────────────────────

struct Ending101 {
    fsm_state before = fsm_state::NotConnected;
    fsm_state after = fsm_state::NotConnected;
    unsigned next_in_before = 0;
    unsigned next_in_after = 0;
    unsigned next_out_before = 0;
    unsigned next_out_after = 0;
    int from_app = 0;  // each a count over this frame
    int from_admin = 0;
    int to_admin = 0;
    int to_app = 0;
    std::string err;
    std::vector<Decoded> out;

    [[nodiscard]] std::vector<const Decoded*> of_type(std::string_view msg_type) const {
        std::vector<const Decoded*> v;
        for (auto const& d : out) {
            if (d.get(35) == msg_type) {
                v.push_back(&d);
            }
        }
        return v;
    }
};

// One frame in the oracle's fields: 35, 34, 45, 58, 371, 372, 373.
inline std::string frame_summary(const Decoded& d) {
    std::string s = "{35=" + d.get(35) + " 34=" + d.get(34);
    for (int tag : {45, 58, 371, 372, 373}) {
        s += " " + std::to_string(tag) + "=" + d.get(tag);
    }
    return s + "}";
}

// Every member of the ending, so two endings compare equal exactly when the oracle says they are.
inline std::string describe(const Ending101& e) {
    std::string s =
        std::string(t99::state_name(e.before)) + "->" + t99::state_name(e.after) + " | in " +
        std::to_string(e.next_in_before) + "->" + std::to_string(e.next_in_after) + " | out# " +
        std::to_string(e.next_out_before) + "->" + std::to_string(e.next_out_after) +
        " | fromApp=" + std::to_string(e.from_app) + " fromAdmin=" + std::to_string(e.from_admin) +
        " toAdmin=" + std::to_string(e.to_admin) + " toApp=" + std::to_string(e.to_app) +
        " | sent:";
    if (e.out.empty()) {
        s += " none";
    }
    for (auto const& d : e.out) {
        s += " " + frame_summary(d);
    }
    if (!e.err.empty()) {
        s += " | " + e.err;
    }
    return s;
}

// ── The rig ─────────────────────────────────────────────────────────────────────────────

class Rig101 {
public:
    Spec spec;
    std::shared_ptr<App101> app = std::make_shared<App101>();
    std::unique_ptr<t99::Rig> r;

    explicit Rig101(Spec s) : spec(std::move(s)) {
        t99::Fixture fx = spec.fixture ? *spec.fixture : default_fixture(spec.kind);
        t99::RigOptions opts;
        opts.validate = spec.validate;
        opts.role = spec.role;
        opts.private_build = spec.kind != Kind::fixt;
        const Kind kind = spec.kind;
        auto user_tweak = spec.tweak;
        opts.tweak = [kind, user_tweak](SessionConfig& cfg) {
            if (kind != Kind::fixt) {
                cfg.begin_string = begin_of(kind);
                cfg.default_appl_ver_id.reset();
            }
            if (user_tweak) {
                user_tweak(cfg);
            }
        };
        r = std::make_unique<t99::Rig>(std::move(fx), std::move(opts));
        r->engine.application = spec.no_application ? nullptr : app;
    }
    Rig101(const Rig101&) = delete;
    Rig101& operator=(const Rig101&) = delete;
    Rig101(Rig101&&) = delete;
    Rig101& operator=(Rig101&&) = delete;
    ~Rig101() = default;

    static t99::Fixture default_fixture(Kind k) {
        if (k == Kind::fixt) {
            return t99::shape_fixture(t99::Shape::T);
        }
        t99::Fixture f;  // no registry: a FIX 4.x session routes nothing
        f.session_dict = k == Kind::fix42 ? t99::fix42() : t99::fix44();
        return f;
    }

    [[nodiscard]] Session& session() const { return *r->sess; }
    [[nodiscard]] std::string begin() const { return r->cfg.begin_string; }

    // The peer's header for MsgSeqNum `seq`: its SenderCompID is the session's target.
    [[nodiscard]] t99::Ctx ctx(unsigned seq) const {
        return t99::Ctx{.snd = r->cfg.target_comp_id, .tgt = r->cfg.sender_comp_id, .seq = seq};
    }

    // A frame from the peer: `35=<msg_type>`, then `before_34`, then 34/49/52/56, then `body`.
    [[nodiscard]] std::string frame(const std::string& msg_type, unsigned seq,
                                    const std::string& body = {},
                                    const std::string& before_34 = {}) const {
        return t99::wire(begin(),
                         "35=" + msg_type + "\x01" + before_34 + t99::hdr(ctx(seq)) + body);
    }

    // The peer's Logon at `seq`: FIXT's carries 1137, as 099's establish() does.
    [[nodiscard]] std::string peer_logon(unsigned seq, const std::string& extra = {}) const {
        std::string body = t99::kLogonBody();
        if (spec.kind == Kind::fixt) {
            body += std::string("1137=") + r->fixture.peer_1137 + "\x01";
        }
        return frame("A", seq, body + extra);
    }

    [[nodiscard]] unsigned next_in() const { return r->next_in(); }
    [[nodiscard]] unsigned next_out() const {
        return static_cast<unsigned>(session_test_access::seqnum_mgr(*r->sess).peek_outbound());
    }

    // Opens and takes the session to `arm`; on failure returns the setup message. The checks
    // that make a reading mean what the cell says: the arm's state, and a validator exactly when
    // the cell validates.
    [[nodiscard]] std::optional<std::string> start(Arm arm) {
        if (auto f = r->open()) {
            return f;
        }
        if (arm == Arm::Active || arm == Arm::LogoutSent) {
            std::string err;
            r->feed(peer_logon(1), &err);
            if (r->sess->state() != fsm_state::Active) {
                return std::string("CANNOT-ESTABLISH state=") + t99::state_name(r->sess->state()) +
                       " " + err;
            }
        }
        if (arm == Arm::LogoutSent) {
            if (auto f = r->enter_logout_sent()) {
                return f;
            }
        }
        const fsm_state want = arm == Arm::NotConnected ? fsm_state::NotConnected
                               : arm == Arm::LogonSent  ? fsm_state::LogonSent
                               : arm == Arm::Active     ? fsm_state::Active
                                                        : fsm_state::LogoutSent;
        if (r->sess->state() != want) {
            return std::string("WRONG-ARM state=") + t99::state_name(r->sess->state());
        }
        if (session_test_access::has_validator(*r->sess) != spec.validate) {
            return std::string("VALIDATOR-MISMATCH validate=") + (spec.validate ? "on" : "off");
        }
        return std::nullopt;
    }

    // The SendingTime the session stamps: the mock clock does not move unless a cell moves it, so
    // every frame it sent carries the same value. Empty until it has sent one.
    [[nodiscard]] std::string stamped_sending_time() const { return sending_time_; }

    // Feeds one frame; the ending holds only what this frame drew.
    [[nodiscard]] Ending101 deliver(const std::string& frame) {
        Ending101 e;
        r->out.clear();
        const int app0 = app->from_app;
        const int adm0 = app->from_admin;
        const int to_adm0 = app->to_admin;
        const int to_app0 = app->to_app;
        e.before = r->sess->state();
        e.next_in_before = next_in();
        e.next_out_before = next_out();
        r->feed(frame, &e.err);
        e.after = r->sess->state();
        e.next_in_after = next_in();
        e.next_out_after = next_out();
        e.from_app = app->from_app - app0;
        e.from_admin = app->from_admin - adm0;
        e.to_admin = app->to_admin - to_adm0;
        e.to_app = app->to_app - to_app0;
        for (auto const& f : r->out) {
            e.out.push_back(decode(f));
        }
        return e;
    }

    // Records the SendingTime of what the session has sent so far (its Logon, on an arm that
    // established); call after start().
    void note_sending_time(const Ending101* from = nullptr) {
        if (from != nullptr && !from->out.empty()) {
            sending_time_ = from->out.front().get(52);
            return;
        }
        for (auto const& f : r->out) {
            sending_time_ = decode(f).get(52);
            return;
        }
    }

private:
    std::string sending_time_;
};

// A cell's session, started: a setup failure is reported once and makes the cell fail, never
// read as the cell's own result.
inline std::unique_ptr<Rig101> started(Spec spec, Arm arm) {
    auto rig = std::make_unique<Rig101>(std::move(spec));
    if (auto f = rig->start(arm)) {
        ADD_FAILURE() << "setup: " << *f;
        return nullptr;
    }
    rig->note_sending_time();
    return rig;
}

// ── research D6's oracle ────────────────────────────────────────────────────────────────

// The frame with every 35 after the first removed, BodyLength and CheckSum recomputed. For a
// frame holding no Data field: its fields are split at SOH.
inline std::string strip_later_35_and_reframe(const std::string& frame) {
    const Decoded d = decode(frame);
    std::string begin;
    std::string body;
    bool seen_35 = false;
    for (auto const& [tag, value] : d.fields) {
        if (tag == 8) {
            begin = value;
            continue;
        }
        if (tag == 9 || tag == 10) {
            continue;
        }
        if (tag == 35) {
            if (seen_35) {
                continue;
            }
            seen_35 = true;
        }
        body += std::to_string(tag) + "=" + value + "\x01";
    }
    return t99::wire(begin, body);
}

// The ending of `make`'s frame on a fresh session of `spec` taken to `arm`.
inline Ending101 ending_of(const Spec& spec, Arm arm,
                           const std::function<std::string(const Rig101&)>& make,
                           bool strip = false) {
    auto rig = started(spec, arm);
    if (!rig) {
        Ending101 e;
        e.err = "setup failed";
        return e;
    }
    const std::string f = make(*rig);
    return rig->deliver(strip ? strip_later_35_and_reframe(f) : f);
}

// ── Thresholds, derived at test time (research D7, D9) ──────────────────────────────────

// The longest n in [0, hi] for which `fits(n)` holds, `fits` holding for every n up to some
// bound and for none past it; nullopt when it holds for none.
template <class F>
std::optional<std::size_t> longest_fitting(F&& fits, std::size_t hi) {
    if (!fits(0)) {
        return std::nullopt;
    }
    std::size_t lo = 0;  // fits(lo)
    std::size_t up = hi + 1;
    while (up - lo > 1) {
        const std::size_t mid = lo + (up - lo) / 2;
        if (fits(mid)) {
            lo = mid;
        } else {
            up = mid;
        }
    }
    return lo;
}

// What a session Reject's build takes from the session and the inbound frame.
struct RejectInputs {
    std::string sender;
    std::string target;
    std::string begin;
    std::string sending_time;
    unsigned seq = 0;      // the 34 the session will stamp
    unsigned ref_seq = 0;  // the 45 it will echo
    int ref_tag = 0;
    int reason = 0;
    std::string text;  // Text(58), empty for none
};

inline bool reject_fits(const RejectInputs& in, std::string_view ref_msg_type,
                        std::size_t buffer_bytes) {
    std::vector<std::byte> buf(buffer_bytes);
    return build_reject_with_text(std::span<std::byte>{buf.data(), buf.size()}, in.seq, in.sender,
                                  in.target, in.ref_seq, in.ref_tag, ref_msg_type, in.reason,
                                  in.begin, in.sending_time, in.text)
        .has_value();
}

inline bool bmr_fits(const RejectInputs& in, std::string_view ref_msg_type,
                     std::size_t buffer_bytes) {
    std::vector<std::byte> buf(buffer_bytes);
    return build_business_message_reject(std::span<std::byte>{buf.data(), buf.size()}, in.seq,
                                         in.sender, in.target, in.ref_seq, ref_msg_type, 0,
                                         in.begin, in.sending_time)
        .has_value();
}

// The longest MsgType (of 'U' bytes) whose Reject, or whose BusinessMessageReject, builds into
// the ceiling Session::kSendFrameBytes.
inline std::optional<std::size_t> longest_reject_msg_type(const RejectInputs& in) {
    const std::size_t ceiling = session_test_access::send_frame_bytes();
    return longest_fitting(
        [&](std::size_t n) { return reject_fits(in, std::string(n == 0 ? 1 : n, 'U'), ceiling); },
        ceiling);
}
inline std::optional<std::size_t> longest_bmr_msg_type(const RejectInputs& in) {
    const std::size_t ceiling = session_test_access::send_frame_bytes();
    return longest_fitting(
        [&](std::size_t n) { return bmr_fits(in, std::string(n == 0 ? 1 : n, 'U'), ceiling); },
        ceiling);
}

// A MsgType of `n` bytes no shipped dictionary defines: 'U' then digits.
inline std::string long_msg_type(std::size_t n) {
    std::string s = "U";
    while (s.size() < n) {
        s += static_cast<char>('0' + (s.size() % 10));
    }
    return s.substr(0, n);
}

// ── The outbound-ceiling record (research D7, "Outbound-ceiling ending") ─────────────────
//
// Spelled here independently of the format registry. One format per message type not sent; its
// integer arguments are the inbound MsgType's length and the count of such messages suppressed
// since the previous record, in that order. The record never carries the MsgType's bytes.
inline constexpr char kCeilingRecordReject[] =
    "inbound reply over the outbound ceiling not sent: msg_type=3 ref_msg_type_len={} "
    "suppressed={}";
inline constexpr char kCeilingRecordBmr[] =
    "inbound reply over the outbound ceiling not sent: msg_type=j ref_msg_type_len={} "
    "suppressed={}";

struct CeilingRecord {
    std::uint64_t ref_msg_type_len = 0;
    std::uint64_t suppressed = 0;
};

// The ceiling records of one format the logger captured. Shuts the logger down: read once, at
// the end of a cell.
inline std::vector<CeilingRecord> ceiling_records(fixpp::test_support::LogCapture& log,
                                                  std::uint32_t format_id) {
    std::vector<CeilingRecord> out;
    for (auto const& rec : log.records_of(format_id)) {
        CeilingRecord c;
        if (rec.arg_count >= 1 && rec.args[0].kind == fixpp::log::ArgValue::Kind::u64) {
            c.ref_msg_type_len = rec.args[0].u64;
        }
        if (rec.arg_count >= 2 && rec.args[1].kind == fixpp::log::ArgValue::Kind::u64) {
            c.suppressed = rec.args[1].u64;
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace fixpp::session::test101
