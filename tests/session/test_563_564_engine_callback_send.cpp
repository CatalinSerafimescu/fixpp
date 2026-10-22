// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_563_564_engine_callback_send.cpp
//
// fixpp#563 and fixpp#564 (batch B41).
//
// Subject: an Engine::send started from inside an Application callback, with no
// asio::post between the callback and the co_spawn. Engine::send reaches a
// published session through the engine's snapshot and one co_spawn onto that
// session's strand; an id the snapshot does not hold goes through the engine's
// control strand first (`.specify/563-564-callback-send.md` §2.3). A co_spawn's
// first step is a dispatch. Whether the send's toApp then runs while the
// calling callback is still on the stack, or only after it returned, is what
// each cell classifies. tests/session/test_563_564_callback_send.cpp holds the
// Session::send cells.
//
// Cells (suite EngineCallbackSend), each to be run alone with --gtest_filter:
//   SequentialEngineSends     control. Message A, then message B once A's send
//                             completed, both on the initiator.
//   EngineSendFromToApp       toApp for A, on the initiator, starts
//                             engine.send(B) on the initiator, spawned on the
//                             io_context's executor.
//   EngineSendFromToAppOnSessionExecutor
//                             the same, spawned on the executor the initiator
//                             session reports (Engine::lookup, then executor()).
//   DeferredEngineSendCancelled_Terminal, DeferredEngineSendCancelled_Total
//                             EngineSendFromToAppOnSessionExecutor with the
//                             co_spawn's completion token bound to a
//                             cancellation slot the cell owns; toApp emits on
//                             that slot before it returns, while the send waits
//                             to start. Terminal: the send returns
//                             dispatch_aborted, B reaches no toApp and takes no
//                             MsgSeqNum, and the initiator stays Active. Total:
//                             the chain's filter drops the emission and the
//                             send completes; a control for the first.
//   EngineSendFromFromApp     the acceptor's fromApp for A starts
//                             engine.send(REPLY) on the acceptor, spawned on
//                             the io_context's executor.
//   EngineSendFromFromAppOnSessionExecutor
//                             the same, spawned on the acceptor session's
//                             executor.
//   MtEngineSendFromFromApp_Quiet
//                             EngineSendFromFromApp's shape on an io_context
//                             run by three threads, repeated: the initiator
//                             sends one message at a time and waits for its
//                             reply; every acceptor fromApp starts
//                             engine.send(REPLY-k) directly. Requires every
//                             reply nested, and prints how many replies were
//                             nested, deferred, never ran.
//   MtEngineSendFromFromApp_BusyControlStrand
//                             the same while two foreign threads keep calling
//                             Engine::send for a SessionId that is not
//                             registered. Such a call runs on the engine's
//                             control strand up to its registry lookup and
//                             touches no session.
//   EngineSendFromOnLogon_Initiator, EngineSendFromOnLogon_Acceptor
//                             onLogon of the session in that role starts
//                             engine.send on that session. Requires: the
//                             session is published and Active inside onLogon,
//                             the send's toApp runs inside onLogon, and the
//                             send succeeds
//                             (`.specify/563-564-callback-send.md` §6, OD-4).
//   StopNotedOnTheSessionRefusesEngineSend
//                             the session's engine-stop flag is set on its
//                             strand, as Engine::stop()'s first step sets it,
//                             and then engine.send is called for that session:
//                             the send is refused and nothing is stamped or
//                             retained (the same note, §6, OD-3).
//   StopNotNotedOnTheSessionAdmitsEngineSend
//                             its positive control: the same setup without the
//                             flag.
//   MissPathSendsQueuedWhileStopRuns
//                             Engine::send calls for an unregistered id and for
//                             a registered acceptor nobody connects to, spawned
//                             from the test thread around the start of stop(),
//                             on three io threads; then the Engine is destroyed.
//   TwoEngines_FromAppStartsClose_Terminal, TwoEngines_FromAppStartsClose_Graceful,
//   TwoEngines_ToAppStartsClose_Terminal, TwoEngines_ToAppStartsClose_Graceful
//                             two Engines, an acceptor's and an initiator's,
//                             over plain loopback TCP. The initiator's fromApp
//                             for a message the acceptor sent, or its toApp for
//                             a message of its own Engine::send, starts a close
//                             of the initiator's session. The cell holds no
//                             handle to that session outside the callback, so
//                             the session is freed by its owners: the role
//                             loop, then stop().
//
// Classification, printed by every cell and attached to every failure:
//   nested     the second send's toApp ran while the callback that started it
//              was still on the stack;
//   deferred   it ran only after that callback had returned;
//   never ran  it did not run within the cell's bound.
// "never ran" fails a cell, except the one whose send is cancelled before it
// starts. The two cells that start the send in fromApp (EngineSendFromFromApp,
// EngineSendFromFromAppOnSessionExecutor) require "nested", on a build with
// asserts too: fromApp is not an outbound callback, so the send's toApp may run
// inside it. The two that start it in toApp (EngineSendFromToApp,
// EngineSendFromToAppOnSessionExecutor) require "deferred": toApp is an
// outbound callback, so the send starts after it returned
// (`.specify/563-564-callback-send.md` §2.1), and B is stamped and delivered
// after A.
//
// What a cell asserts, per session that sent: every frame toApp saw carries its
// own MsgSeqNum(34), the values are consecutive from 2 (each session's Logon
// is number 1), the session's store holds each frame under the number it was stamped
// with, and the manager's and the store's next outbound number agree. Per
// session that received: fromApp saw the peer's messages with consecutive
// MsgSeqNums in arrival order. Both sends report success and both sessions stay
// Active.
//
// On a build with asserts, a toApp entered inside toApp or toAdmin of the same
// session stops at the session's debug guard before the second toApp's body
// runs (`.specify/563-564-callback-send.md` §2.2), so a cell in which that
// happens cannot print its classification. The lines the hook writes to stderr
// around the co_spawn bracket that case: an abort between "starting" and
// "returned" happened while the outer callback was on the stack.
//
// Observation limits: the bytes on the socket are not tapped. What was stamped
// is read from the frame toApp is handed, which is the buffer the send path
// stores and transmits; what was retained is read from the sender's store; what
// arrived is what the peer session delivered to fromApp, so a frame the peer
// session refused shows only as a missing delivery, an admin callback and a
// state change.
//
// The multi-threaded cells classify per reply with two marks. The acceptor's
// fromApp for message k marks its own thread with k around the co_spawn
// (thread_local) and moves an atomic phase for k from "not started" to "in
// fromApp" before the co_spawn and to "the co_spawn returned" after it. The
// acceptor's toApp for REPLY-k is nested when its thread carries the mark k,
// deferred when the phase says the co_spawn had returned, and overlapped when
// the phase says fromApp(k) was still inside the co_spawn on another thread,
// which the session's strand is meant to exclude. Per-reply records are written on one
// session's strand each and read after the io threads are joined; what another
// thread reads while they run is atomic or under a mutex. These cells assert
// that every reply is nested and what must hold in every interleaving (each
// reply sent once, consecutive MsgSeqNums on each side, both sessions Active),
// and print the rest: the tally, and per session whether two consecutive
// callbacks ran on different io threads. A build with asserts also writes one
// stderr line per reply, so an abort shows how far the cell got.
//
// Fixture: tests/session/test_application_engine_send.cpp's, one Engine holding
// an acceptor and an initiator over loopback mTLS on one io_context run by the
// test thread, plus a bounded MemoryStore on each session. The Mt cells run
// that io_context on three threads. No fatal assertion sits between
// Engine::start() and a completed stop(): a cell collects into `Readings`,
// stops the engine, and asserts afterwards.
//
// The TwoEngines_ cells have a fixture of their own, run_two_engines_cell: one
// Engine per session, each with its own Application and mock clock, on one
// io_context run by the test thread, over plain TCP
// (SecurityProfile::kind::insecure_plain_tcp), with a heartbeat interval that no
// clock advance reaches and a logout timeout that no run of a cell reaches. They
// keep the rule on fatal assertions: both Engines are stopped before a cell
// asserts.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <fixpp/wire/parser.hpp>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "session/session_engine_access.hpp"
#include "support/engine_test_access.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

namespace fixpp::session::test {

// printf stays: std::println needs <print>, which libstdc++ 13 (the linux-gcc-release
// toolchain) does not ship.
// NOLINTBEGIN(modernize-use-std-print)

namespace {

using View = MessageView<access_mode::Index>;

constexpr std::string_view kIdA = "B41-A";
constexpr std::string_view kIdB = "B41-B";
constexpr std::string_view kIdReply = "B41-REPLY";
constexpr std::string_view kPayloadA =
    "35=D\x01"
    "11=B41-A\x01"
    "54=1\x01"
    "55=AAA\x01";
constexpr std::string_view kPayloadB =
    "35=D\x01"
    "11=B41-B\x01"
    "54=1\x01"
    "55=BBB\x01";
constexpr std::string_view kPayloadReply =
    "35=D\x01"
    "11=B41-REPLY\x01"
    "54=2\x01"
    "55=AAA\x01";

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (char c : s) v.push_back(static_cast<std::byte>(c));
    return v;
}

std::string text_of(std::span<const std::byte> b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

// Value of tag `tag` in a SOH-delimited frame, matched at a field boundary.
std::string field(std::string_view frame, std::uint32_t tag) {
    const std::string needle = "\x01" + std::to_string(tag) + "=";
    auto pos = frame.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    const auto end = frame.find('\x01', pos);
    return std::string(
        frame.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos));
}

std::string value(const View& mv, std::uint16_t tag) {
    const auto fv = mv.get(tag);
    return fv ? std::string(fv->as_string()) : std::string{};
}

seqnum_t to_seq(std::string_view v) {
    seqnum_t out = 0;
    const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    if (ec != std::errc{} || ptr != v.data() + v.size()) return 0;
    return out;
}

// ── TLS fixture, as tests/session/test_application_engine_send.cpp builds it ──

const char* get_fixture_dir() {
    const char* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");  // NOLINT(concurrency-mt-unsafe)
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (env && env[0] != '\0') ? env : kDir;
}

uint16_t reserve_free_port(asio::io_context& ioc) {
    asio::ip::tcp::acceptor a{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    a.open(ep.protocol());
    a.bind(ep);
    uint16_t port = a.local_endpoint().port();
    a.close();
    return port;
}

std::shared_ptr<fixpp::transport::TransportFactory> make_tls_factory(const char* fixture_dir) {
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    if (!cs_r.has_value()) return nullptr;
    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};
    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    if (!fac_r.has_value()) return nullptr;
    return std::shared_ptr<fixpp::transport::TransportFactory>{std::move(*fac_r)};
}

SessionConfig make_session_cfg(asio::io_context& ioc,
                               std::shared_ptr<fixpp::transport::TransportFactory> fac,
                               const char* sender, const char* target, session_role role,
                               const char* peer_compid, uint16_t port,
                               std::size_t store_outbound_capacity = 16) {
    SessionConfig c;
    c.sender_comp_id = sender;
    c.target_comp_id = target;
    c.begin_string = "FIX.4.2";
    c.role = role;
    c.executor_override = ioc.get_executor();
    c.security_profile = SecurityProfile{SecurityProfile::kind::mtls_ca};
    c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer_compid);
    c.dictionary = fixpp::test_support::make_minimal_dictionary();
    c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
    c.transport_factory_override = std::move(fac);
    c.heartbeat_interval = std::chrono::seconds{30};
    c.logout_disconnect_timeout_ms = 500;
    c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
    c.transport_send = [](std::span<const std::byte>) {};
    MemoryStore::Config sc;
    sc.inbound_capacity = 16;
    sc.outbound_capacity = store_outbound_capacity;
    sc.max_frame_bytes = 4096;
    c.store_factory = std::make_shared<MemoryStoreFactory>(sc);
    return c;
}

std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::io_context& ioc) {
    using namespace std::chrono;
    auto utc = system_clock::time_point{} + seconds{1704067200};
    auto stp = fixpp::core::steady_time_point{} + seconds{0};
    return std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
}

// ── What a cell records ───────────────────────────────────────────────────────

enum class Who { initiator, acceptor };

const char* name(Who w) { return w == Who::initiator ? "INI" : "ACC"; }

// One toApp call: the frame as stamped, and what was true when it ran.
struct ToAppRecord {
    Who who;
    std::string id;        // ClOrdID(11)
    std::string seq;       // MsgSeqNum(34) as stamped
    bool in_callback;      // a callback that started a send was on the stack
    bool first_done;       // the first send had completed
    std::string next_out;  // that session's manager, next outbound
};

struct FromAppRecord {
    Who who;
    std::string id;
    std::string seq;
};

struct AdminRecord {
    Who who;
    const char* callback;
    std::string msg_type;
    std::string seq;
    std::string text;  // Text(58)
};

struct SendOutcome {
    bool done = false;
    bool threw = false;
    std::optional<error> err;
};

std::string describe(const SendOutcome& o) {
    if (!o.done) return "not completed";
    if (o.threw) return "threw";
    if (o.err) return "error " + std::to_string(static_cast<int>(*o.err));
    return "ok";
}

class CollectVisitor final : public retrieve_visitor {
public:
    std::vector<std::string> frames;

    asio::awaitable<expected_t<visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> frame) noexcept override {
        frames.push_back(text_of(frame));
        co_return visit_result::cont;
    }
};

// One session, read on its own strand once both sends completed.
struct SessionSnapshot {
    bool done = false;
    bool threw = false;
    std::string state = "not read";
    std::string mgr_next_out = "not read";
    std::string mgr_next_in = "not read";
    std::string store_next_out = "not read";
    // Outbound MsgSeqNum -> ClOrdID(11) of each frame the store returned for it,
    // or the retrieve error.
    std::map<seqnum_t, std::string> retained;
};

constexpr seqnum_t kFirstAppSeq = 2;
constexpr seqnum_t kLastReadSeq = 4;

asio::awaitable<void> read_session(Session& s, SessionSnapshot& out) {
    out.state = std::to_string(static_cast<int>(s.state()));
    out.mgr_next_out = std::to_string(session_test_access::seqnum_mgr(s).next_outbound_unsafe());
    out.mgr_next_in = std::to_string(session_test_access::seqnum_mgr(s).next_inbound_unsafe());
    MessageStore* store = session_test_access::store(s);
    if (store == nullptr) {
        out.store_next_out = "no store";
        co_return;
    }
    const auto n = co_await store->next_seqnum(direction_t::outbound, false);
    out.store_next_out =
        n ? std::to_string(*n) : "error " + std::to_string(static_cast<int>(n.error()));
    for (seqnum_t q = kFirstAppSeq; q <= kLastReadSeq; ++q) {
        CollectVisitor v;
        const auto r = co_await store->retrieve(q, q, direction_t::outbound, v);
        std::string got;
        for (const auto& f : v.frames) got += (got.empty() ? "" : ",") + field(f, 11);
        if (!r) got += "(error " + std::to_string(static_cast<int>(r.error())) + ")";
        out.retained[q] = got;
    }
}

class RecordingApp final : public Application {
public:
    std::function<void(const View&, const SessionId&)> on_from_app;
    std::function<void(const View&, const SessionId&)> on_to_app;
    std::function<void(const char*, const View&, const SessionId&)> on_admin;
    std::function<void(const SessionId&)> on_logon;

    expected_t<void> fromApp(const View& mv, const SessionId& id) override {
        if (on_from_app) on_from_app(mv, id);
        return {};
    }
    expected_t<void> toApp(const View& mv, const SessionId& id) override {
        if (on_to_app) on_to_app(mv, id);
        return {};
    }
    expected_t<void> fromAdmin(const View& mv, const SessionId& id) override {
        if (on_admin) on_admin("fromAdmin", mv, id);
        return {};
    }
    void toAdmin(const View& mv, const SessionId& id) override {
        if (on_admin) on_admin("toAdmin", mv, id);
    }
    void onLogon(const SessionId& id) override {
        if (on_logon) on_logon(id);
    }
};

enum class Shape { sequential, from_to_app, from_from_app };
enum class SpawnOn { io_context, session_executor };

struct Readings {
    bool skipped = false;
    bool setup_ok = false;
    bool stopped = false;
    bool started_second = false;  // the callback under test reached its co_spawn
    SendOutcome first;
    SendOutcome second;
    std::string after_spawn = "not recorded";  // read in the callback, after co_spawn returned
    // The cells that emit a cancellation, read in the callback between the co_spawn's
    // return and the emission: the co_spawn installed a handler in the cell's slot, the
    // second send had not completed, and its message had reached no toApp.
    bool emitted = false;
    bool slot_has_a_handler = false;
    bool second_pending_at_emission = false;
    bool second_to_app_ran_before_emission = false;
    std::vector<ToAppRecord> to_app;
    std::vector<FromAppRecord> from_app;
    std::vector<AdminRecord> admin;
    SessionSnapshot ini;
    SessionSnapshot acc;
    std::string ini_state_end = "not read";
    std::string acc_state_end = "not read";
};

std::string describe(const SessionSnapshot& s) {
    std::string out = "state=" + s.state + " manager NextNumOut=" + s.mgr_next_out +
                      " NextNumIn=" + s.mgr_next_in + " store NextNumOut=" + s.store_next_out +
                      " retained{";
    for (const auto& [seq, ids] : s.retained) {
        out += " 34=" + std::to_string(seq) + ":[" + ids + "]";
    }
    out += " }";
    if (!s.done) out += " (snapshot not completed)";
    if (s.threw) out += " (snapshot threw)";
    return out;
}

const ToAppRecord* to_app_of(const Readings& o, std::string_view id) {
    const auto it =
        std::ranges::find_if(o.to_app, [&](const ToAppRecord& r) { return r.id == id; });
    return it == o.to_app.end() ? nullptr : &*it;
}

std::string classify(const Readings& o, Shape shape) {
    if (shape == Shape::sequential) return "sequential (control)";
    const ToAppRecord* second = to_app_of(o, shape == Shape::from_to_app ? kIdB : kIdReply);
    if (second == nullptr) return "never ran";
    return second->in_callback ? "nested" : "deferred";
}

std::string context(const Readings& o, Shape shape) {
    std::string out = "Classification: " + classify(o, shape) + ". toApp calls, in order:";
    for (const auto& r : o.to_app) {
        out += std::string(" [") + name(r.who) + " 11=" + r.id + " 34=" + r.seq +
               (r.in_callback ? " inside-the-starting-callback" : " outside-it") +
               (r.first_done ? " first-send-completed" : " first-send-pending") +
               " NextNumOut=" + r.next_out + "]";
    }
    out += "; fromApp deliveries, in order:";
    for (const auto& r : o.from_app) {
        out += std::string(" [") + name(r.who) + " 11=" + r.id + " 34=" + r.seq + "]";
    }
    out += "; admin callbacks after setup, in order:";
    for (const auto& r : o.admin) {
        out += std::string(" [") + name(r.who) + " " + r.callback + " 35=" + r.msg_type +
               " 34=" + r.seq + (r.text.empty() ? "" : " 58=" + r.text) + "]";
    }
    out +=
        "; first send: " + describe(o.first) + "; second send: " + describe(o.second) +
        "; NextNumOut read in the starting callback after its co_spawn returned: " + o.after_spawn +
        "; when both sends had completed: INI " + describe(o.ini) + "; ACC " + describe(o.acc) +
        "; after the delivery window: INI state=" + o.ini_state_end +
        " ACC state=" + o.acc_state_end + ".";
    return out;
}

// Runs one cell to a stopped engine and returns what it saw. Non-fatal only.
// With `emit`, the second send's co_spawn is bound to a cancellation slot of this
// function, and the callback that started it emits `*emit` on that slot before it
// returns.
Readings run_cell(Shape shape, SpawnOn spawn_on,
                  std::optional<asio::cancellation_type> emit = std::nullopt) {
    Readings o;
    const char* fd = get_fixture_dir();
    if (!fd || fd[0] == '\0') {
        o.skipped = true;
        return o;
    }

    asio::io_context ioc;
    auto app = std::make_shared<RecordingApp>();
    auto fac = make_tls_factory(fd);
    if (!fac) {
        ADD_FAILURE() << "setup: TLS factory failed";
        return o;
    }

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = app;
    ecfg.clock = make_mock_clock(ioc);
    Engine engine{ioc.get_executor(), std::move(ecfg)};

    const uint16_t port = reserve_free_port(ioc);
    auto acc_cfg = make_session_cfg(ioc, fac, "ACCEPTOR", "INITIATOR", session_role::acceptor,
                                    "INITIATOR", port);
    auto ini_cfg = make_session_cfg(ioc, fac, "INITIATOR", "ACCEPTOR", session_role::initiator,
                                    "ACCEPTOR", port);
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    const SessionId ini_id = SessionId::from_config(ini_cfg);

    // The payloads and ids outlive every send: Engine::send reads its span and its
    // SessionId reference when its body first runs, which may be after the callback
    // that started it returned.
    const auto payload_a = bytes_of(kPayloadA);
    const auto payload_b = bytes_of(kPayloadB);
    const auto payload_reply = bytes_of(kPayloadReply);

    // Session handles, held from Active until just before stop(): a session that
    // disconnects during the cell can then still be read. A build with asserts
    // counts these handles, so they are released before the engine is destroyed.
    std::shared_ptr<Session> ini_h;
    std::shared_ptr<Session> acc_h;

    const auto stop_engine = [&] {
        ini_h.reset();
        acc_h.reset();
        auto sf = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, sf, 3s, "EngineCallbackSend/stop")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *engine.clock(),
                                                            "EngineCallbackSend/stop");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "EngineCallbackSend/stop";
            return;
        }
        sf.get();
        o.stopped = engine.stopped();
    };

    const bool registered = engine.register_session(std::move(acc_cfg)).has_value() &&
                            engine.register_session(std::move(ini_cfg)).has_value();
    if (!registered) {
        ADD_FAILURE() << "setup: register_session failed";
        return o;
    }
    if (!engine.start().has_value()) {
        ADD_FAILURE() << "setup: engine.start() failed";
        return o;
    }
    const auto active = [&](const SessionId& id) {
        const auto h = engine.lookup(id);
        return h && h->state() == fsm_state::Active;
    };
    if (!fixpp::test_support::pump_until(
            ioc, [&] { return active(acc_id) && active(ini_id); }, 5s,
            fixpp::test_support::kPumpSlice, "EngineCallbackSend/active")) {
        ADD_FAILURE() << "setup: the sessions did not reach Active";
        stop_engine();
        return o;
    }
    ini_h = engine.lookup(ini_id);
    acc_h = engine.lookup(acc_id);
    if (!ini_h || !acc_h) {
        ADD_FAILURE() << "setup: an Active session is not published";
        stop_engine();
        return o;
    }
    o.setup_ok = true;

    const auto who_of = [&](const SessionId& id) {
        return id == ini_id ? Who::initiator : Who::acceptor;
    };
    const auto session_of = [&](Who w) -> Session& {
        return w == Who::initiator ? *ini_h : *acc_h;
    };
    const auto next_out = [&](Who w) {
        return std::to_string(
            session_test_access::seqnum_mgr(session_of(w)).next_outbound_unsafe());
    };

    // The session whose callback starts the second send, and which that send is on.
    const Who starter = shape == Shape::from_from_app ? Who::acceptor : Who::initiator;
    const SessionId& second_id = starter == Who::initiator ? ini_id : acc_id;
    const auto& second_payload = shape == Shape::from_from_app ? payload_reply : payload_b;
    // The starting session, named by a handle initialised from the engine, so that
    // tools/audit_co_spawn_named_closure.py can follow `exec`'s session branch to
    // `ioc`. It must be the session session_of(starter) returns. The handle is
    // released once `exec` holds a copy of its executor.
    auto starter_h = engine.lookup(second_id);
    if (!starter_h || starter_h.get() != &session_of(starter)) {
        ADD_FAILURE() << "setup: the starting session's lookup is not session_of(starter)";
        stop_engine();
        return o;
    }
    const asio::any_io_executor exec =
        spawn_on == SpawnOn::session_executor
            ? asio::any_io_executor{starter_h->executor().underlying()}
            : asio::any_io_executor{ioc.get_executor()};
    starter_h.reset();

    // Both sessions are read on their own strands from the completion handler of
    // whichever send completes last, so the read is queued before the test thread
    // regains control.
    bool snapshots_started = false;
    const auto start_snapshots = [&] {
        if (snapshots_started) return;
        snapshots_started = true;
        const auto read = [&](Session& s, SessionSnapshot& snap) {
            asio::co_spawn(s.executor().underlying(), read_session(s, snap),
                           [&snap](std::exception_ptr ep) {
                               snap.threw = static_cast<bool>(ep);
                               snap.done = true;
                           });
        };
        read(*ini_h, o.ini);
        read(*acc_h, o.acc);
    };
    const auto record_into = [&](SendOutcome& out) {
        return [&o, &out, &start_snapshots](std::exception_ptr ep, expected_t<void> r) {
            out.done = true;
            out.threw = static_cast<bool>(ep);
            if (!ep && !r) out.err = r.error();
            if (o.first.done && o.second.done) start_snapshots();
        };
    };

    bool in_callback = false;
    asio::cancellation_signal cancel;
    // The completion token of a send the cell can cancel. A closure call produces it,
    // so that the call is a direct argument of co_spawn: both co_spawn census
    // instruments (tools/audit_co_spawn_named_closure.py and
    // tools/reconcile_co_spawn_census.py) must list the site.
    const auto record_into_with_slot = [&](SendOutcome& out) {
        return asio::bind_cancellation_slot(cancel.slot(), record_into(out));
    };
    // Starts the second send from inside the calling callback: no post, the
    // co_spawn is issued synchronously in the callback's body.
    const auto start_second = [&](const char* from) {
        o.started_second = true;
        in_callback = true;
        std::fprintf(stderr, "[ B41 ] %s: starting engine.send for the second message\n", from);
        if (emit) {
            asio::co_spawn(exec, engine.send(second_id, std::span<const std::byte>(second_payload)),
                           record_into_with_slot(o.second));
            o.slot_has_a_handler = cancel.slot().has_handler();
            o.second_pending_at_emission = !o.second.done;
            o.second_to_app_ran_before_emission = to_app_of(o, kIdB) != nullptr;
            cancel.emit(*emit);
            o.emitted = true;
        } else {
            asio::co_spawn(exec, engine.send(second_id, std::span<const std::byte>(second_payload)),
                           record_into(o.second));
        }
        std::fprintf(stderr, "[ B41 ] %s: co_spawn returned\n", from);
        in_callback = false;
        o.after_spawn = next_out(starter) +
                        (o.second.done ? " (second send completed)" : " (second send pending)");
    };

    app->on_to_app = [&](const View& mv, const SessionId& id) {
        const Who w = who_of(id);
        // Read the view before anything else runs on this stack.
        std::string id11 = value(mv, 11);
        o.to_app.push_back({.who = w,
                            .id = id11,
                            .seq = value(mv, 34),
                            .in_callback = in_callback,
                            .first_done = o.first.done,
                            .next_out = next_out(w)});
        if (shape == Shape::from_to_app && w == Who::initiator && id11 == kIdA &&
            !o.started_second) {
            start_second("toApp(A)");
        }
    };
    app->on_from_app = [&](const View& mv, const SessionId& id) {
        const Who w = who_of(id);
        std::string id11 = value(mv, 11);
        o.from_app.push_back({.who = w, .id = id11, .seq = value(mv, 34)});
        if (shape == Shape::from_from_app && w == Who::acceptor && id11 == kIdA &&
            !o.started_second) {
            start_second("fromApp(A)");
        }
    };
    app->on_admin = [&](const char* callback, const View& mv, const SessionId& id) {
        o.admin.push_back({.who = who_of(id),
                           .callback = callback,
                           .msg_type = value(mv, 35),
                           .seq = value(mv, 34),
                           .text = value(mv, 58)});
    };

    asio::co_spawn(ioc, engine.send(ini_id, std::span<const std::byte>(payload_a)),
                   record_into(o.first));
    if (shape == Shape::sequential) {
        if (fixpp::test_support::pump_until(
                ioc, [&] { return o.first.done; }, 5s, fixpp::test_support::kPumpSlice,
                "EngineCallbackSend/first")) {
            o.started_second = true;
            asio::co_spawn(ioc, engine.send(second_id, std::span<const std::byte>(second_payload)),
                           record_into(o.second));
        }
    }
    // A miss here is reported by the caller from `o`; the cell goes on to read
    // the sessions and stop the engine either way.
    (void)fixpp::test_support::pump_until(
        ioc, [&] { return o.first.done && o.second.done; }, 5s, fixpp::test_support::kPumpSlice,
        "EngineCallbackSend/sends");
    start_snapshots();
    (void)fixpp::test_support::pump_until(
        ioc, [&] { return o.ini.done && o.acc.done; }, 2s, fixpp::test_support::kPumpSlice,
        "EngineCallbackSend/snapshots");

    // Delivery window: until every message sent has reached the other side's
    // fromApp, or a session left Active, or the bound. In a cell that emits a
    // cancellation, a second send that returned an error is not waited for.
    const std::size_t expected_deliveries = emit && o.second.err ? 1 : 2;
    (void)fixpp::test_support::pump_until(
        ioc,
        [&] {
            return o.from_app.size() >= expected_deliveries ||
                   ini_h->state() != fsm_state::Active || acc_h->state() != fsm_state::Active;
        },
        2s, fixpp::test_support::kPumpSlice, "EngineCallbackSend/deliveries");
    o.ini_state_end = std::to_string(static_cast<int>(ini_h->state()));
    o.acc_state_end = std::to_string(static_cast<int>(acc_h->state()));

    app->on_to_app = nullptr;
    app->on_from_app = nullptr;
    app->on_admin = nullptr;
    stop_engine();
    return o;
}

// Per session that sent: the frames toApp saw carry consecutive MsgSeqNums from
// 2 in toApp order, the store holds each under its number, and the manager and
// the store agree on the next one.
void expect_sent(const Readings& o, Who w, const std::vector<std::string_view>& ids,
                 const std::string& ctx) {
    const SessionSnapshot& snap = w == Who::initiator ? o.ini : o.acc;
    std::vector<seqnum_t> stamped;
    for (const auto id : ids) {
        const auto n = std::ranges::count_if(
            o.to_app, [&](const ToAppRecord& r) { return r.who == w && r.id == id; });
        EXPECT_EQ(n, 1) << name(w) << ": message " << id << " must reach toApp once. " << ctx;
        const ToAppRecord* r = to_app_of(o, id);
        if (r == nullptr) continue;
        const seqnum_t seq = to_seq(r->seq);
        stamped.push_back(seq);
        const auto it = snap.retained.find(seq);
        const std::string held = it == snap.retained.end() ? "(not read)" : it->second;
        EXPECT_EQ(held, std::string(id))
            << name(w) << ": the store's frame under outbound 34=" << seq
            << " must be the frame stamped with it, 11=" << id << ". " << ctx;
    }
    std::ranges::sort(stamped);
    std::vector<seqnum_t> want;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
        want.push_back(kFirstAppSeq + static_cast<seqnum_t>(i));
    }
    EXPECT_EQ(stamped, want) << name(w)
                             << ": the frames must carry distinct, consecutive MsgSeqNum(34) "
                                "values from 2. "
                             << ctx;
    const std::string next = std::to_string(kFirstAppSeq + ids.size());
    EXPECT_EQ(snap.mgr_next_out, next)
        << name(w) << ": the manager's NextNumOut must have advanced once per send. " << ctx;
    EXPECT_EQ(snap.store_next_out, snap.mgr_next_out)
        << name(w) << ": the store's and the manager's NextNumOut must agree. " << ctx;
}

// Per session that received: fromApp saw exactly these messages, in this
// order, with consecutive MsgSeqNums from 2.
void expect_received(const Readings& o, Who w, const std::vector<std::string_view>& ids,
                     const std::string& ctx) {
    std::vector<std::string> got;
    for (const auto& r : o.from_app) {
        if (r.who == w) got.push_back("11=" + r.id + " 34=" + r.seq);
    }
    std::vector<std::string> want;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
        want.push_back("11=" + std::string(ids[i]) +
                       " 34=" + std::to_string(kFirstAppSeq + static_cast<seqnum_t>(i)));
    }
    EXPECT_EQ(got, want) << name(w) << ": fromApp deliveries, in arrival order. " << ctx;
}

// The cells that start engine.send(B) in toApp(A): A is stamped 2 and B 3, and the
// acceptor receives them in that order.
void expect_a_then_b(const Readings& o, const std::string& ctx) {
    std::vector<std::string> stamped;
    for (const auto& r : o.to_app) {
        if (r.who == Who::initiator) stamped.push_back("11=" + r.id + " 34=" + r.seq);
    }
    EXPECT_EQ(stamped, (std::vector<std::string>{"11=" + std::string(kIdA) + " 34=2",
                                                 "11=" + std::string(kIdB) + " 34=3"}))
        << "INI: toApp must see A under MsgSeqNum 2 and then B under 3. " << ctx;
    expect_received(o, Who::acceptor, {kIdA, kIdB}, ctx);
}

void run_and_check(Shape shape, SpawnOn spawn_on) {
    const Readings o = run_cell(shape, spawn_on);
    if (o.skipped) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    ASSERT_TRUE(o.setup_ok) << "setup did not complete";
    const std::string ctx = context(o, shape);
    const std::string cls = classify(o, shape);
    ::testing::Test::RecordProperty("classification", cls);
    std::printf("[ MEASURED ] %s\n", ctx.c_str());
    std::fflush(stdout);

    EXPECT_TRUE(o.stopped) << "the engine must stop. " << ctx;
    ASSERT_TRUE(o.started_second) << "PATH NOT DRIVEN: the second send was never started. " << ctx;
    ASSERT_NE(cls, "never ran") << "PATH NOT DRIVEN: the second send's toApp never ran. " << ctx;
    if (shape == Shape::from_from_app) {
        EXPECT_EQ(cls, "nested") << "an Engine::send started in fromApp must run nested. " << ctx;
    }
    if (shape == Shape::from_to_app) {
        EXPECT_EQ(cls, "deferred") << "an Engine::send started in toApp must be deferred. " << ctx;
    }

    EXPECT_EQ(describe(o.first), "ok") << ctx;
    EXPECT_EQ(describe(o.second), "ok") << ctx;
    if (shape == Shape::from_from_app) {
        expect_sent(o, Who::initiator, {kIdA}, ctx);
        expect_sent(o, Who::acceptor, {kIdReply}, ctx);
        expect_received(o, Who::acceptor, {kIdA}, ctx);
        expect_received(o, Who::initiator, {kIdReply}, ctx);
        EXPECT_EQ(o.acc.mgr_next_in, "3")
            << "ACC: the inbound message that triggered the reply must have been consumed. " << ctx;
    } else {
        expect_sent(o, Who::initiator, {kIdA, kIdB}, ctx);
        // The peer accepts only ascending MsgSeqNums, so the order it delivered
        // the two messages in is the order of their numbers; which message carries
        // which number is not asserted.
        std::vector<std::string> seqs;
        std::vector<std::string> ids;
        for (const auto& r : o.from_app) {
            if (r.who != Who::acceptor) continue;
            seqs.push_back(r.seq);
            ids.push_back(r.id);
        }
        std::ranges::sort(ids);
        EXPECT_EQ(seqs, (std::vector<std::string>{"2", "3"}))
            << "ACC: fromApp must deliver two messages with MsgSeqNum 2 then 3. " << ctx;
        EXPECT_EQ(ids, (std::vector<std::string>{std::string(kIdA), std::string(kIdB)}))
            << "ACC: fromApp must deliver each message once. " << ctx;
        if (shape == Shape::from_to_app) expect_a_then_b(o, ctx);
    }
    const std::string active = std::to_string(static_cast<int>(fsm_state::Active));
    EXPECT_EQ(o.ini_state_end, active) << "INI must stay Active. " << ctx;
    EXPECT_EQ(o.acc_state_end, active) << "ACC must stay Active. " << ctx;
}

// EngineSendFromToAppOnSessionExecutor, with `emission` emitted from toApp(A) on the
// slot the second send's co_spawn is bound to. An Engine::send started in toApp
// reaches Session::send inside toApp, where it waits for the callback to return, so
// the emission is made while it waits.
void run_and_check_cancelled(asio::cancellation_type emission) {
    const Readings o = run_cell(Shape::from_to_app, SpawnOn::session_executor, emission);
    if (o.skipped) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    ASSERT_TRUE(o.setup_ok) << "setup did not complete";
    const std::string ctx = context(o, Shape::from_to_app);
    const std::string cls = classify(o, Shape::from_to_app);
    ::testing::Test::RecordProperty("classification", cls);
    std::printf("[ MEASURED ] %s\n", ctx.c_str());
    std::fflush(stdout);

    EXPECT_TRUE(o.stopped) << "the engine must stop. " << ctx;
    // Witness: the emission was made on a slot the chain listens on, while the send
    // had neither completed nor reached its toApp.
    ASSERT_TRUE(o.emitted) << "PATH NOT DRIVEN: toApp(A) never emitted. " << ctx;
    ASSERT_TRUE(o.slot_has_a_handler)
        << "PATH NOT DRIVEN: the co_spawn installed no handler in the cell's slot. " << ctx;
    ASSERT_TRUE(o.second_pending_at_emission)
        << "PATH NOT DRIVEN: the second send had completed before the emission. " << ctx;
    ASSERT_FALSE(o.second_to_app_ran_before_emission)
        << "PATH NOT DRIVEN: B had reached toApp before the emission, so its send was not "
           "waiting to start. "
        << ctx;

    const std::string active = std::to_string(static_cast<int>(fsm_state::Active));
    EXPECT_EQ(describe(o.first), "ok") << ctx;
    EXPECT_EQ(o.ini_state_end, active) << "INI must stay Active. " << ctx;
    EXPECT_EQ(o.acc_state_end, active) << "ACC must stay Active. " << ctx;
    if (emission == asio::cancellation_type::terminal) {
        // The send is refused before it starts: it has consumed nothing.
        EXPECT_FALSE(o.second.threw) << ctx;
        EXPECT_EQ(o.second.err, std::optional<error>{error::dispatch_aborted}) << ctx;
        EXPECT_EQ(cls, "never ran")
            << "a send cancelled before it starts must not reach toApp. " << ctx;
        expect_sent(o, Who::initiator, {kIdA}, ctx);
        for (const auto& [seq, ids] : o.ini.retained) {
            EXPECT_EQ(ids.find(kIdB), std::string::npos)
                << "INI: nothing of the cancelled send may be stored, but outbound 34=" << seq
                << " holds it. " << ctx;
        }
        expect_received(o, Who::acceptor, {kIdA}, ctx);
        return;
    }
    // Engine::send's outer frame is a chain with the terminal-only filter, which
    // drops the emission.
    EXPECT_EQ(cls, "deferred") << ctx;
    EXPECT_EQ(describe(o.second), "ok") << ctx;
    expect_sent(o, Who::initiator, {kIdA, kIdB}, ctx);
    expect_a_then_b(o, ctx);
}

// ── Two Engines over loopback TCP: a close started in a callback ─────────────
//
// `.specify/563-564-callback-send.md` §3, "New cells", the row of two Engines over
// loopback TCP; §2.5 "Not a memory-safety defect, as far as reading goes".

// The logout timeout of these cells. Their clocks are mock clocks that nothing
// advances, so no heartbeat interval elapses and no wait on the session's clock
// ends; close() also arms a timer on real time for this duration, and the value is
// one that no run of a cell reaches.
constexpr std::uint32_t kOutOfReachMs = 60000;

// SecurityProfile::kind::insecure_plain_tcp is [[deprecated]] at every selection
// site; these cells select it on purpose, for a plain loopback TCP transport.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)  // NOLINT(readability-use-concise-preprocessor-directives): C++23 form
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
SessionConfig make_plain_session_cfg(asio::io_context& ioc, const char* sender, const char* target,
                                     session_role role, uint16_t port) {
    SessionConfig c;
    c.sender_comp_id = sender;
    c.target_comp_id = target;
    c.begin_string = "FIX.4.2";
    c.role = role;
    c.executor_override = ioc.get_executor();
    c.security_profile = SecurityProfile{SecurityProfile::kind::insecure_plain_tcp};
    c.dictionary = fixpp::test_support::make_minimal_dictionary();
    c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
    c.heartbeat_interval = std::chrono::seconds{30};
    c.logout_disconnect_timeout_ms = kOutOfReachMs;
    c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
    c.transport_send = [](std::span<const std::byte>) {};
    MemoryStore::Config sc;
    sc.inbound_capacity = 16;
    sc.outbound_capacity = 16;
    sc.max_frame_bytes = 4096;
    c.store_factory = std::make_shared<MemoryStoreFactory>(sc);
    return c;
}
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)  // NOLINT(readability-use-concise-preprocessor-directives): C++23 form
#pragma warning(pop)
#endif

// The Application of one Engine of a cell with two.
class OneEngineApp final : public Application {
public:
    std::function<void(const View&)> on_from_app;
    std::function<void(const View&)> on_to_app;
    std::function<void(const char*, const View&)> on_admin;
    std::function<void()> on_logout;

    expected_t<void> fromApp(const View& mv, const SessionId& /*id*/) override {
        if (on_from_app) on_from_app(mv);
        return {};
    }
    expected_t<void> toApp(const View& mv, const SessionId& /*id*/) override {
        if (on_to_app) on_to_app(mv);
        return {};
    }
    expected_t<void> fromAdmin(const View& mv, const SessionId& /*id*/) override {
        if (on_admin) on_admin("fromAdmin", mv);
        return {};
    }
    void toAdmin(const View& mv, const SessionId& /*id*/) override {
        if (on_admin) on_admin("toAdmin", mv);
    }
    void onLogout(const SessionId& /*id*/) override {
        if (on_logout) on_logout();
    }
};

// A session's FSM state and lifecycle, read together.
std::string where_is(const Session& s) {
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores): read when the state is no enumerator
    const char* state = "unknown";
    switch (s.state()) {
        case fsm_state::NotConnected:
            state = "NotConnected";
            break;
        case fsm_state::LogonSent:
            state = "LogonSent";
            break;
        case fsm_state::LogonReceived:
            state = "LogonReceived";
            break;
        case fsm_state::Active:
            state = "Active";
            break;
        case fsm_state::LogoutSent:
            state = "LogoutSent";
            break;
        case fsm_state::Disconnected:
            state = "Disconnected";
            break;
    }
    const char* lifecycle = "unknown";
    switch (session_test_access::lifecycle_value(s)) {
        case 0:
            lifecycle = "never_opened";
            break;
        case 1:
            lifecycle = "open";
            break;
        case 2:
            lifecycle = "closing";
            break;
        case 3:
            lifecycle = "closed_drained";
            break;
        default:
            break;
    }
    return std::string(state) + " " + lifecycle;
}

enum class CloseStartedIn { from_app, to_app };

struct TwoEngineReadings {
    bool setup_ok = false;
    std::string setup_failure;
    // The initiator's callback under test reached its co_spawn of close().
    bool started = false;
    // The initiator's session on entry to that callback, and after the co_spawn
    // returned.
    std::string entered = "not recorded";
    std::string inside = "not recorded";
    bool inside_close_done = false;
    int inside_on_logout_calls = 0;
    // The Engine::send that carries message A, and the close the callback started.
    SendOutcome send;
    SendOutcome close;
    // Each side's callbacks and completions from the start of the send on, in the
    // order that side saw them. The two lists are not ordered against each other:
    // a socket lies between them.
    std::vector<std::string> ini_events;
    std::vector<std::string> acc_events;
    int ini_on_logout_calls = 0;
    int acc_on_logout_calls = 0;
    int acc_from_app_calls = 0;
    // The initiator's role loop unpublished its transport.
    bool role_loop_ended = false;
    bool completed = false;
    std::string pending_at_the_bound;
    std::string ini_at_the_end = "not read";
    bool ini_stopped = false;
    bool acc_stopped = false;
    bool ini_published_after_stop = true;
};

std::string list_of(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& e : v) out += " [" + e + "]";
    return out.empty() ? " (none)" : out;
}

// Two Engines on one io_context, an acceptor and an initiator, each with its own
// Application and mock clock, one session each, over plain loopback TCP. Message A
// is sent by the initiator (to_app) or by the acceptor (from_app), and the
// initiator's callback for it starts a close of the initiator's session. The cell
// holds no handle to that session outside the callback, so the session's owners
// are the Engine's: its role loop, and its registry until stop(). Non-fatal only:
// both Engines are stopped before this returns.
TwoEngineReadings run_two_engines_cell(CloseStartedIn started_in, close_mode mode) {
    TwoEngineReadings o;
    asio::io_context ioc;
    auto ini_app = std::make_shared<OneEngineApp>();
    auto acc_app = std::make_shared<OneEngineApp>();
    const auto engine_cfg = [&](const std::shared_ptr<OneEngineApp>& app) {
        fixpp::core::EngineConfig c;
        c.executor = ioc.get_executor();
        c.application = app;
        c.clock = make_mock_clock(ioc);
        return c;
    };
    Engine acc_engine{ioc.get_executor(), engine_cfg(acc_app)};
    Engine ini_engine{ioc.get_executor(), engine_cfg(ini_app)};
    bool acc_started = false;
    bool ini_started = false;

    const auto stop = [&](Engine& engine, bool& stopped, const char* site) {
        auto sf = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, sf, 3s, site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *engine.clock(), site);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
            return;
        }
        sf.get();
        stopped = engine.stopped();
    };
    const auto stop_both = [&] {
        if (ini_started) stop(ini_engine, o.ini_stopped, "EngineCallbackSend/two_engines_stop_ini");
        if (acc_started) stop(acc_engine, o.acc_stopped, "EngineCallbackSend/two_engines_stop_acc");
    };

    // The acceptor's Engine first: the initiator connects once, to a port that is
    // already listening.
    auto acc_cfg = make_plain_session_cfg(ioc, "ACCEPTOR", "INITIATOR", session_role::acceptor, 0);
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    if (!acc_engine.register_session(std::move(acc_cfg)).has_value() ||
        !acc_engine.start().has_value()) {
        o.setup_failure = "the acceptor's Engine did not register or start";
        return o;
    }
    acc_started = true;
    uint16_t port = 0;
    if (!fixpp::test_support::pump_until(
            ioc,
            [&] {
                port = acc_engine.acceptor_bound_endpoint(acc_id).port;
                return port != 0;
            },
            5s, fixpp::test_support::kPumpSlice, "EngineCallbackSend/two_engines_bind")) {
        o.setup_failure = "the acceptor's listener was not bound";
        stop_both();
        return o;
    }
    auto ini_cfg =
        make_plain_session_cfg(ioc, "INITIATOR", "ACCEPTOR", session_role::initiator, port);
    const SessionId ini_id = SessionId::from_config(ini_cfg);
    if (!ini_engine.register_session(std::move(ini_cfg)).has_value() ||
        !ini_engine.start().has_value()) {
        o.setup_failure = "the initiator's Engine did not register or start";
        stop_both();
        return o;
    }
    ini_started = true;
    const auto active = [&](const Engine& engine, const SessionId& id) {
        const auto h = engine.lookup(id);
        return h && h->state() == fsm_state::Active;
    };
    if (!fixpp::test_support::pump_until(
            ioc, [&] { return active(acc_engine, acc_id) && active(ini_engine, ini_id); }, 5s,
            fixpp::test_support::kPumpSlice, "EngineCallbackSend/two_engines_active")) {
        o.setup_failure = "the sessions did not reach Active";
        stop_both();
        return o;
    }
    o.setup_ok = true;

    const auto payload_a = bytes_of(kPayloadA);
    // The initiator's session, looked up for the length of one statement.
    const auto ini_now = [&] {
        const auto h = ini_engine.lookup(ini_id);
        return h ? where_is(*h) : std::string("not published");
    };
    const auto note_ini = [&](const std::string& what) {
        o.ini_events.push_back(what + " (" + ini_now() + ")");
    };
    const auto completes = [&](SendOutcome& out, const char* what) {
        return [&, what](std::exception_ptr ep, expected_t<void> r) {
            out.done = true;
            out.threw = static_cast<bool>(ep);
            if (!ep && !r) out.err = r.error();
            note_ini(std::string(what) + " completes: " + describe(out));
        };
    };
    const auto start_close = [&](const char* from) {
        o.started = true;
        // The handle lives until this closure returns.
        const auto h = ini_engine.lookup(ini_id);
        if (!h) return;
        o.entered = where_is(*h);
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] %s: starting Session::close\n", from);
        asio::co_spawn(h->executor().underlying(), h->close(mode), completes(o.close, "the close"));
        std::fprintf(stderr, "[ B41 ] %s: co_spawn returned\n", from);
        o.inside = where_is(*h);
        o.inside_close_done = o.close.done;
        o.inside_on_logout_calls = o.ini_on_logout_calls;
    };

    ini_app->on_to_app = [&](const View& mv) {
        const std::string id11 = value(mv, 11);
        note_ini("toApp 11=" + id11);
        if (started_in == CloseStartedIn::to_app && id11 == kIdA && !o.started) {
            start_close("toApp(A)");
        }
    };
    ini_app->on_from_app = [&](const View& mv) {
        const std::string id11 = value(mv, 11);
        note_ini("fromApp 11=" + id11);
        if (started_in == CloseStartedIn::from_app && id11 == kIdA && !o.started) {
            start_close("fromApp(A)");
        }
    };
    ini_app->on_admin = [&](const char* callback, const View& mv) {
        note_ini(std::string(callback) + " 35=" + value(mv, 35));
    };
    ini_app->on_logout = [&] {
        ++o.ini_on_logout_calls;
        note_ini("onLogout");
    };
    acc_app->on_from_app = [&](const View& mv) {
        ++o.acc_from_app_calls;
        o.acc_events.push_back("fromApp 11=" + value(mv, 11));
    };
    acc_app->on_to_app = [&](const View& mv) {
        o.acc_events.push_back("toApp 11=" + value(mv, 11));
    };
    acc_app->on_admin = [&](const char* callback, const View& mv) {
        o.acc_events.push_back(std::string(callback) + " 35=" + value(mv, 35));
    };
    acc_app->on_logout = [&] {
        ++o.acc_on_logout_calls;
        o.acc_events.emplace_back("onLogout");
    };

    if (started_in == CloseStartedIn::to_app) {
        asio::co_spawn(ioc, ini_engine.send(ini_id, std::span<const std::byte>(payload_a)),
                       completes(o.send, "the send"));
    } else {
        asio::co_spawn(ioc, acc_engine.send(acc_id, std::span<const std::byte>(payload_a)),
                       completes(o.send, "the acceptor's send"));
    }

    // Until both operations completed, the initiator's role loop unpublished its
    // transport, and the acceptor saw its session leave Active: from then on
    // nothing more of this connection reaches the acceptor. Bounded: what is
    // missing at the bound is reported by the caller from `o`.
    const auto role_loop_ended = [&] {
        return engine_test_access::live_transport_published(ini_engine, ini_id) ==
               std::optional<bool>{false};
    };
    o.completed = fixpp::test_support::pump_until(
        ioc,
        [&] {
            return o.send.done && o.close.done && role_loop_ended() && o.acc_on_logout_calls > 0;
        },
        10s, fixpp::test_support::kPumpSlice, "EngineCallbackSend/two_engines_close");
    o.role_loop_ended = role_loop_ended();
    if (!o.completed) {
        o.pending_at_the_bound =
            std::string(o.send.done ? "" : "the send had not completed; ") +
            (o.close.done ? "" : "the close had not completed; ") +
            (o.role_loop_ended ? "" : "the initiator's role loop had not ended; ") +
            (o.acc_on_logout_calls > 0 ? "" : "the acceptor had not seen its session disconnect; ");
    }
    o.ini_at_the_end = ini_now();

    ini_app->on_to_app = nullptr;
    ini_app->on_from_app = nullptr;
    ini_app->on_admin = nullptr;
    ini_app->on_logout = nullptr;
    acc_app->on_from_app = nullptr;
    acc_app->on_to_app = nullptr;
    acc_app->on_admin = nullptr;
    acc_app->on_logout = nullptr;
    stop_both();
    o.ini_published_after_stop = ini_engine.lookup(ini_id) != nullptr;
    return o;
}

void run_and_check_two_engines(CloseStartedIn started_in, close_mode mode) {
    const TwoEngineReadings o = run_two_engines_cell(started_in, mode);
    ASSERT_TRUE(o.setup_ok) << "setup: " << o.setup_failure;
    const char* callback = started_in == CloseStartedIn::to_app ? "toApp" : "fromApp";
    const std::string reading =
        std::string("The initiator's ") + callback + " starts a " +
        (mode == close_mode::terminal ? "terminal" : "graceful") +
        " close. On entry: " + o.entered + "; after the start: " + o.inside + ", the close " +
        (o.inside_close_done ? "completed" : "not completed") + ", onLogout calls " +
        std::to_string(o.inside_on_logout_calls) + "; the send: " + describe(o.send) +
        "; the close: " + describe(o.close) +
        "; the initiator's side, in order:" + list_of(o.ini_events) +
        "; the acceptor's side, in order:" + list_of(o.acc_events) +
        "; the initiator's role loop " + (o.role_loop_ended ? "ended" : "had not ended") +
        "; the initiator's session at the end: " + o.ini_at_the_end +
        "; Engines stopped: initiator " + (o.ini_stopped ? "yes" : "no") + ", acceptor " +
        (o.acc_stopped ? "yes" : "no") + "; the initiator's session after stop(): " +
        (o.ini_published_after_stop ? "still published" : "not published") + ".";
    std::printf("[ MEASURED ] %s\n", reading.c_str());
    std::fflush(stdout);

    ASSERT_TRUE(o.started) << "PATH NOT DRIVEN: the initiator's " << callback
                           << " did not start its close. " << reading;
    EXPECT_EQ(o.inside, "Active open")
        << "inside the callback after the start, the session is Active and the lifecycle is "
           "open. "
        << reading;
    EXPECT_EQ(o.inside, o.entered)
        << "inside the callback after the start, the session is as the callback entered it. "
        << reading;
    EXPECT_FALSE(o.inside_close_done)
        << "inside the callback after the start, the close it started has not completed. "
        << reading;
    EXPECT_EQ(o.inside_on_logout_calls, 0)
        << "inside the callback after the start, onLogout has not run. " << reading;
    EXPECT_TRUE(o.completed) << "every awaited event must happen within the pump's bound; "
                             << o.pending_at_the_bound << reading;

    EXPECT_EQ(describe(o.close), "ok") << "the close's result is ok. " << reading;
    EXPECT_EQ(o.ini_at_the_end, "Disconnected closed_drained")
        << "the initiator's session ends closed. " << reading;
    EXPECT_EQ(o.ini_on_logout_calls, 1) << "the initiator's onLogout runs once. " << reading;
    EXPECT_TRUE(o.role_loop_ended) << "the session's role loop ends. " << reading;
    EXPECT_TRUE(o.ini_stopped) << "the initiator's Engine stops cleanly afterwards. " << reading;
    EXPECT_TRUE(o.acc_stopped) << "the acceptor's Engine stops cleanly afterwards. " << reading;
    EXPECT_FALSE(o.ini_published_after_stop)
        << "after stop() the Engine no longer holds the session. " << reading;
    EXPECT_GE(o.acc_on_logout_calls, 1)
        << "the acceptor sees its session leave Active. " << reading;

    const auto in_acc = [&](std::string_view what) {
        return static_cast<std::size_t>(std::ranges::find(o.acc_events, what) -
                                        o.acc_events.begin());
    };
    const auto ini_logouts = std::ranges::count_if(
        o.ini_events, [](const std::string& e) { return e.starts_with("toAdmin 35=5"); });
    const std::string outer = "fromApp 11=" + std::string(kIdA);
    if (mode == close_mode::graceful) {
        EXPECT_EQ(ini_logouts, 1) << "a graceful close sends one Logout. " << reading;
        EXPECT_LT(in_acc("fromAdmin 35=5"), o.acc_events.size())
            << "the acceptor receives the Logout. " << reading;
    } else {
        EXPECT_EQ(ini_logouts, 0) << "a terminal close sends no Logout. " << reading;
        EXPECT_EQ(in_acc("fromAdmin 35=5"), o.acc_events.size())
            << "the acceptor receives no Logout. " << reading;
    }
    if (started_in == CloseStartedIn::from_app) {
        EXPECT_EQ(describe(o.send), "ok") << "the acceptor's send returns ok. " << reading;
        return;
    }
    if (mode == close_mode::graceful) {
        EXPECT_EQ(describe(o.send), "ok") << "the send returns ok. " << reading;
        EXPECT_EQ(o.acc_from_app_calls, 1)
            << "the acceptor receives the outer frame once. " << reading;
        EXPECT_LT(in_acc(outer), in_acc("fromAdmin 35=5"))
            << "the acceptor receives the outer frame and then the Logout. " << reading;
    } else {
        // The acceptor delivers every frame it read before it acts on the end of
        // the stream, so once its session has left Active a frame that was written
        // would have reached fromApp.
        EXPECT_EQ(o.acc_from_app_calls, 0)
            << "the acceptor does not receive the outer frame. " << reading;
        EXPECT_EQ(o.send.err, std::optional<error>{error::dispatch_aborted})
            << "the send returns dispatch_aborted. " << reading;
    }
}

// ── Multi-threaded cells ──────────────────────────────────────────────────────

constexpr std::size_t kMtReplies = 2000;
constexpr std::size_t kMtIoThreads = 3;
constexpr std::string_view kMtPrefixA = "B41-A-";
constexpr std::string_view kMtPrefixReply = "B41-R-";

// The reply whose starting fromApp is running on this thread, or -1.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): a per-thread marker
thread_local long tl_reply_being_started = -1;

std::vector<std::byte> mt_payload(std::string_view prefix, std::size_t k, char side) {
    std::string p =
        "35=D\x01"
        "11=";
    p += prefix;
    p += std::to_string(k);
    p += "\x01"
         "54=";
    p += side;
    p += "\x01"
         "55=AAA\x01";
    return bytes_of(p);
}

// The k in "<prefix><k>", or -1.
long mt_index(std::string_view id, std::string_view prefix) {
    if (!id.starts_with(prefix)) return -1;
    id.remove_prefix(prefix.size());
    long k = -1;
    const auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), k);
    if (ec != std::errc{} || ptr != id.data() + id.size()) return -1;
    return k;
}

enum class MtClass : std::uint8_t {
    never_ran = 0,
    nested = 1,      // on the thread, and inside, the fromApp that started it
    deferred = 2,    // after that fromApp's co_spawn had returned
    overlapped = 3,  // on another thread while that fromApp was inside the co_spawn
    unstarted = 4,   // before any fromApp started it
};

// Where the acceptor's fromApp for message k is.
enum class MtPhase : std::uint8_t { not_started = 0, in_from_app = 1, co_spawn_returned = 2 };

struct MtDelivery {
    long k;
    seqnum_t seq;
};

struct MtReadings {
    bool skipped = false;
    bool setup_ok = false;
    bool stopped = false;
    std::size_t requested = 0;
    std::size_t driven = 0;  // messages whose reply arrived back at the initiator
    std::string stall;       // empty, or why the driver stopped early

    // Written on the acceptor's strand; read after the io threads are joined.
    std::vector<MtClass> classification;
    std::vector<std::uint32_t> reply_to_app_calls;
    std::vector<seqnum_t> reply_seq;
    std::vector<MtDelivery> acc_deliveries;
    // Written on the initiator's strand; read after the io threads are joined.
    std::vector<std::uint32_t> a_to_app_calls;
    std::vector<seqnum_t> a_seq;
    std::vector<MtDelivery> ini_deliveries;
    // The io thread of each callback of a session, in that session's order. Each list
    // is written on its session's strand and read after the io threads are joined.
    std::vector<std::thread::id> acc_callback_threads;
    std::vector<std::thread::id> ini_callback_threads;

    // Per reply, written by the acceptor's fromApp and read by its toApp.
    std::unique_ptr<std::atomic<MtPhase>[]> phase;

    // Written by completion handlers and load threads, on any thread.
    std::atomic<std::uint64_t> reply_sends_ok{0};
    std::atomic<std::uint64_t> reply_sends_failed{0};
    std::atomic<int> first_reply_send_error{0};
    std::atomic<std::uint64_t> load_sends{0};
    std::atomic<std::uint64_t> load_sends_not_unregistered{0};

    std::mutex admin_mu;
    std::vector<std::string> admin;  // admin callbacks after setup, any session

    SessionSnapshot ini;
    SessionSnapshot acc;
    std::string ini_state_end = "not read";
    std::string acc_state_end = "not read";

    explicit MtReadings(std::size_t n)
        : requested(n),
          classification(n, MtClass::never_ran),
          reply_to_app_calls(n, 0),
          reply_seq(n, 0),
          a_to_app_calls(n, 0),
          a_seq(n, 0),
          phase(std::make_unique<std::atomic<MtPhase>[]>(n)) {
        for (std::size_t k = 0; k < n; ++k) phase[k].store(MtPhase::not_started);
        acc_deliveries.reserve(n);
        ini_deliveries.reserve(n);
        acc_callback_threads.reserve(4 * n);
        ini_callback_threads.reserve(4 * n);
    }
};

// How one session's callbacks were spread over the io threads.
std::string thread_use(const std::vector<std::thread::id>& ids) {
    std::vector<std::thread::id> seen;
    std::size_t hand_offs = 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (std::ranges::find(seen, ids[i]) == seen.end()) seen.push_back(ids[i]);
        if (i > 0 && ids[i] != ids[i - 1]) ++hand_offs;
    }
    return "callbacks=" + std::to_string(ids.size()) +
           " io threads=" + std::to_string(seen.size()) +
           " consecutive callbacks on different threads=" + std::to_string(hand_offs);
}

struct MtTally {
    std::size_t nested = 0;
    std::size_t deferred = 0;
    std::size_t overlapped = 0;
    std::size_t unstarted = 0;
    std::size_t never_ran = 0;
    long first_nested = -1;
    long first_deferred = -1;
};

MtTally tally(const MtReadings& o) {
    MtTally t;
    for (std::size_t k = 0; k < o.requested; ++k) {
        switch (o.classification[k]) {
            case MtClass::nested:
                if (t.nested++ == 0) t.first_nested = static_cast<long>(k);
                break;
            case MtClass::deferred:
                if (t.deferred++ == 0) t.first_deferred = static_cast<long>(k);
                break;
            case MtClass::overlapped:
                ++t.overlapped;
                break;
            case MtClass::unstarted:
                ++t.unstarted;
                break;
            case MtClass::never_ran:
                ++t.never_ran;
                break;
        }
    }
    return t;
}

std::string mt_context(MtReadings& o, bool busy) {
    const MtTally t = tally(o);
    std::string out =
        "Replies requested=" + std::to_string(o.requested) +
        ", driven=" + std::to_string(o.driven) + ": nested=" + std::to_string(t.nested) +
        " deferred=" + std::to_string(t.deferred) + " overlapped=" + std::to_string(t.overlapped) +
        " before-its-fromApp=" + std::to_string(t.unstarted) +
        " never ran=" + std::to_string(t.never_ran) + "; first nested at reply " +
        std::to_string(t.first_nested) + ", first deferred at reply " +
        std::to_string(t.first_deferred) + "; io threads=" + std::to_string(kMtIoThreads) +
        "; control-strand load: " +
        (busy ? std::to_string(o.load_sends.load()) +
                    " Engine::send calls for an unregistered SessionId completed, " +
                    std::to_string(o.load_sends_not_unregistered.load()) +
                    " of them with a result other than session_invalid_argument"
              : std::string("none")) +
        "; reply sends ok=" + std::to_string(o.reply_sends_ok.load()) +
        " failed=" + std::to_string(o.reply_sends_failed.load()) + " (first error " +
        std::to_string(o.first_reply_send_error.load()) + ")" + "; deliveries: ACC " +
        std::to_string(o.acc_deliveries.size()) + ", INI " +
        std::to_string(o.ini_deliveries.size()) +
        "; driver: " + (o.stall.empty() ? std::string("ran to the end") : o.stall) +
        "; at the end: INI " + describe(o.ini) + "; ACC " + describe(o.acc) +
        "; INI state=" + o.ini_state_end + " ACC state=" + o.acc_state_end +
        "; admin callbacks after setup:";
    const std::scoped_lock lk(o.admin_mu);
    for (const auto& a : o.admin) out += " [" + a + "]";
    out += ".";
    return out;
}

// Runs one multi-threaded cell to a stopped engine and joined threads.
void run_mt_cell(bool busy, MtReadings& o) {
    const char* fd = get_fixture_dir();
    if (!fd || fd[0] == '\0') {
        o.skipped = true;
        return;
    }
    const std::size_t n = o.requested;

    asio::io_context ioc;
    auto app = std::make_shared<RecordingApp>();
    auto fac = make_tls_factory(fd);
    if (!fac) {
        ADD_FAILURE() << "setup: TLS factory failed";
        return;
    }

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = app;
    ecfg.clock = make_mock_clock(ioc);
    Engine engine{ioc.get_executor(), std::move(ecfg)};

    const uint16_t port = reserve_free_port(ioc);
    const std::size_t capacity = n + 64;
    auto acc_cfg = make_session_cfg(ioc, fac, "ACCEPTOR", "INITIATOR", session_role::acceptor,
                                    "INITIATOR", port, capacity);
    auto ini_cfg = make_session_cfg(ioc, fac, "INITIATOR", "ACCEPTOR", session_role::initiator,
                                    "ACCEPTOR", port, capacity);
    const SessionId acc_id = SessionId::from_config(acc_cfg);
    const SessionId ini_id = SessionId::from_config(ini_cfg);
    // A SessionId no session is registered under, for the control-strand load.
    SessionConfig nobody_cfg;
    nobody_cfg.sender_comp_id = "NOBODY";
    nobody_cfg.target_comp_id = "NOWHERE";
    nobody_cfg.begin_string = "FIX.4.2";
    const SessionId nobody_id = SessionId::from_config(nobody_cfg);

    // Every payload outlives every send: Engine::send reads its span when its body
    // first runs, which may be after the callback that started it returned.
    std::vector<std::vector<std::byte>> a_payloads;
    std::vector<std::vector<std::byte>> reply_payloads;
    a_payloads.reserve(n);
    reply_payloads.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        a_payloads.push_back(mt_payload(kMtPrefixA, k, '1'));
        reply_payloads.push_back(mt_payload(kMtPrefixReply, k, '2'));
    }

    // Setup runs on the test thread alone; the io threads start once both sessions
    // are Active.
    const bool registered = engine.register_session(std::move(acc_cfg)).has_value() &&
                            engine.register_session(std::move(ini_cfg)).has_value();
    if (!registered || !engine.start().has_value()) {
        ADD_FAILURE() << "setup: register_session or engine.start() failed";
        return;
    }
    std::shared_ptr<Session> ini_h;
    std::shared_ptr<Session> acc_h;
    const auto active = [&](const SessionId& id) {
        const auto h = engine.lookup(id);
        return h && h->state() == fsm_state::Active;
    };
    const bool is_active = fixpp::test_support::pump_until(
        ioc, [&] { return active(acc_id) && active(ini_id); }, 5s, fixpp::test_support::kPumpSlice,
        "EngineCallbackSend/mt-active");
    if (is_active) {
        ini_h = engine.lookup(ini_id);
        acc_h = engine.lookup(acc_id);
    }
    o.setup_ok = is_active && ini_h && acc_h;

    std::atomic<bool> recording{o.setup_ok};
    std::mutex mu;
    std::condition_variable cv;
    std::size_t replies_back = 0;  // under mu
    const asio::any_io_executor exec{ioc.get_executor()};

    const auto on_reply_done = [&o](std::exception_ptr ep, expected_t<void> r) {
        if (!ep && r) {
            o.reply_sends_ok.fetch_add(1);
            return;
        }
        int expected_zero = 0;
        o.first_reply_send_error.compare_exchange_strong(expected_zero,
                                                         ep ? -1 : static_cast<int>(r.error()));
        o.reply_sends_failed.fetch_add(1);
    };

    // Installed before the io threads start and never reassigned while they run.
    const auto note_thread = [&](const SessionId& id) {
        (id == acc_id ? o.acc_callback_threads : o.ini_callback_threads)
            .push_back(std::this_thread::get_id());
    };
    app->on_from_app = [&](const View& mv, const SessionId& id) {
        if (!recording.load()) return;
        note_thread(id);
        const std::string id11 = value(mv, 11);
        const seqnum_t seq = to_seq(value(mv, 34));
        if (id == acc_id) {
            const long k = mt_index(id11, kMtPrefixA);
            o.acc_deliveries.push_back({.k = k, .seq = seq});
            if (k < 0 || std::cmp_greater_equal(k, n)) return;
#ifndef NDEBUG
            std::fprintf(stderr, "[ B41 ] reply %ld: starting\n", k);
#endif
            // The second send, started inside this callback: no post.
            o.phase[static_cast<std::size_t>(k)].store(MtPhase::in_from_app);
            tl_reply_being_started = k;
            asio::co_spawn(exec,
                           engine.send(acc_id, std::span<const std::byte>(
                                                   reply_payloads[static_cast<std::size_t>(k)])),
                           on_reply_done);
            tl_reply_being_started = -1;
            o.phase[static_cast<std::size_t>(k)].store(MtPhase::co_spawn_returned);
#ifndef NDEBUG
            std::fprintf(stderr, "[ B41 ] reply %ld: co_spawn returned\n", k);
#endif
        } else {
            const long k = mt_index(id11, kMtPrefixReply);
            o.ini_deliveries.push_back({.k = k, .seq = seq});
            {
                const std::scoped_lock lk(mu);
                ++replies_back;
            }
            cv.notify_all();
        }
    };
    app->on_to_app = [&](const View& mv, const SessionId& id) {
        if (!recording.load()) return;
        note_thread(id);
        const std::string id11 = value(mv, 11);
        const seqnum_t seq = to_seq(value(mv, 34));
        if (id == acc_id) {
            const long k = mt_index(id11, kMtPrefixReply);
            if (k < 0 || std::cmp_greater_equal(k, n)) return;
            const auto i = static_cast<std::size_t>(k);
            ++o.reply_to_app_calls[i];
            o.reply_seq[i] = seq;
            const MtPhase phase = o.phase[i].load();
            const MtClass cls = tl_reply_being_started == k           ? MtClass::nested
                                : phase == MtPhase::co_spawn_returned ? MtClass::deferred
                                : phase == MtPhase::in_from_app       ? MtClass::overlapped
                                                                      : MtClass::unstarted;
            o.classification[i] = cls;
#ifndef NDEBUG
            std::fprintf(stderr, "[ B41 ] reply %ld: %s\n", k,
                         cls == MtClass::nested       ? "nested"
                         : cls == MtClass::deferred   ? "deferred"
                         : cls == MtClass::overlapped ? "overlapped"
                                                      : "before its fromApp");
#endif
        } else {
            const long k = mt_index(id11, kMtPrefixA);
            if (k < 0 || std::cmp_greater_equal(k, n)) return;
            ++o.a_to_app_calls[static_cast<std::size_t>(k)];
            o.a_seq[static_cast<std::size_t>(k)] = seq;
        }
    };
    app->on_admin = [&](const char* callback, const View& mv, const SessionId& id) {
        if (!recording.load()) return;
        note_thread(id);
        std::string line = std::string(id == ini_id ? "INI " : "ACC ") + callback +
                           " 35=" + value(mv, 35) + " 34=" + value(mv, 34);
        const std::string text = value(mv, 58);
        if (!text.empty()) line += " 58=" + text;
        const std::scoped_lock lk(o.admin_mu);
        o.admin.push_back(std::move(line));
    };

    auto work = asio::make_work_guard(ioc);
    std::vector<std::thread> io_threads;
    for (std::size_t i = 0; i < kMtIoThreads; ++i) {
        // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
        io_threads.emplace_back([&ioc] { ioc.run(); });
    }

    // The control-strand load: foreign threads, each keeping a few Engine::send calls
    // for the unregistered id in flight.
    std::atomic<bool> stop_load{false};
    std::vector<std::thread> load_threads;
    const auto load_payload = mt_payload("B41-LOAD-", 0, '1');
    if (busy && o.setup_ok) {
        for (int t = 0; t < 2; ++t) {
            load_threads.emplace_back([&] {
                constexpr std::size_t kInFlight = 8;
                while (!stop_load.load()) {
                    std::array<std::future<expected_t<void>>, kInFlight> futs;
                    for (auto& f : futs) {
                        f = asio::co_spawn(
                            ioc, engine.send(nobody_id, std::span<const std::byte>(load_payload)),
                            asio::use_future);
                    }
                    for (auto& f : futs) {
                        if (f.wait_for(5s) != std::future_status::ready) {
                            stop_load.store(true);
                            o.load_sends_not_unregistered.fetch_add(1);
                            continue;
                        }
                        const auto r = f.get();
                        o.load_sends.fetch_add(1);
                        if (r || r.error() != error::session_invalid_argument) {
                            o.load_sends_not_unregistered.fetch_add(1);
                        }
                    }
                }
            });
        }
    }

    // The driver: one message at a time, the next only once its reply is back, so
    // the driver's own sends are not in flight when the next fromApp runs.
    if (o.setup_ok) {
        for (std::size_t k = 0; k < n; ++k) {
            auto fut =
                asio::co_spawn(ioc, engine.send(ini_id, std::span<const std::byte>(a_payloads[k])),
                               asio::use_future);
            if (fut.wait_for(5s) != std::future_status::ready) {
                o.stall =
                    "the initiator's send of message " + std::to_string(k) + " did not complete";
                break;
            }
            if (const auto r = fut.get(); !r) {
                o.stall = "the initiator's send of message " + std::to_string(k) +
                          " returned error " + std::to_string(static_cast<int>(r.error()));
                break;
            }
            std::unique_lock lk(mu);
            if (!cv.wait_for(lk, 5s, [&] { return replies_back > k; })) {
                o.stall = "reply " + std::to_string(k) + " did not arrive back at the initiator";
                break;
            }
            ++o.driven;
        }
    }

    stop_load.store(true);
    for (auto& t : load_threads) t.join();

    // Both sessions, each read on its own strand.
    if (o.setup_ok) {
        const auto read = [&](Session& s, SessionSnapshot& snap) {
            auto fut =
                asio::co_spawn(s.executor().underlying(), read_session(s, snap), asio::use_future);
            if (fut.wait_for(5s) != std::future_status::ready) return;
            try {
                fut.get();
                snap.done = true;
            } catch (...) {
                snap.threw = true;
            }
        };
        read(*ini_h, o.ini);
        read(*acc_h, o.acc);
        auto states = asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                // Each state is read on its session's strand.
                o.ini_state_end = co_await asio::co_spawn(
                    ini_h->executor().underlying(),
                    [&]() -> asio::awaitable<std::string> {
                        co_return std::to_string(static_cast<int>(ini_h->state()));
                    },
                    asio::use_awaitable);
                o.acc_state_end = co_await asio::co_spawn(
                    acc_h->executor().underlying(),
                    [&]() -> asio::awaitable<std::string> {
                        co_return std::to_string(static_cast<int>(acc_h->state()));
                    },
                    asio::use_awaitable);
            },
            asio::use_future);
        (void)states.wait_for(5s);
    }
    recording.store(false);

    ini_h.reset();
    acc_h.reset();
    {
        auto sf = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        if (sf.wait_for(10s) == std::future_status::ready) {
            sf.get();
            o.stopped = engine.stopped();
        } else {
            ADD_FAILURE() << "engine.stop() did not complete";
        }
    }
    work.reset();
    ioc.stop();
    for (auto& t : io_threads) t.join();
}

void run_and_check_mt(bool busy) {
    MtReadings o{kMtReplies};
    run_mt_cell(busy, o);
    if (o.skipped) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    ASSERT_TRUE(o.setup_ok) << "setup did not complete";
    const std::string ctx = mt_context(o, busy);
    const MtTally t = tally(o);
    ::testing::Test::RecordProperty("nested", static_cast<int>(t.nested));
    ::testing::Test::RecordProperty("deferred", static_cast<int>(t.deferred));
    ::testing::Test::RecordProperty("overlapped", static_cast<int>(t.overlapped));
    ::testing::Test::RecordProperty("never_ran", static_cast<int>(t.never_ran));
    std::printf("[ MEASURED ] %s\n", ctx.c_str());
    // Printed and not asserted: which thread runs a strand's next handler is the
    // scheduler's choice.
    std::printf("[ THREADS ] ACC %s; INI %s.\n", thread_use(o.acc_callback_threads).c_str(),
                thread_use(o.ini_callback_threads).c_str());
    std::fflush(stdout);

    EXPECT_EQ(t.deferred, 0U) << "no reply may be deferred. " << ctx;
    EXPECT_EQ(t.nested, o.requested) << "every reply must be nested. " << ctx;

    EXPECT_TRUE(o.stopped) << "the engine must stop. " << ctx;
    if (busy) {
        EXPECT_GT(o.load_sends.load(), 0U) << "the control-strand load must have run. " << ctx;
        EXPECT_EQ(o.load_sends_not_unregistered.load(), 0U)
            << "every load call must have ended at the control strand's registry lookup. " << ctx;
    }

    // Every reply sent once.
    EXPECT_EQ(o.driven, o.requested) << "every message must have been answered. " << ctx;
    EXPECT_EQ(t.never_ran, 0U) << "PATH NOT DRIVEN for some replies: toApp never ran. " << ctx;
    EXPECT_EQ(t.overlapped + t.unstarted, 0U)
        << "a reply's toApp must not run on another thread while its fromApp is inside the "
           "co_spawn, nor before that fromApp. "
        << ctx;
    const auto once = [](const std::vector<std::uint32_t>& calls) {
        return std::ranges::all_of(calls, [](std::uint32_t c) { return c == 1; });
    };
    EXPECT_TRUE(once(o.reply_to_app_calls)) << "each reply must reach toApp once. " << ctx;
    EXPECT_TRUE(once(o.a_to_app_calls)) << "each message must reach toApp once. " << ctx;
    EXPECT_EQ(o.reply_sends_ok.load(), o.requested) << "each reply's send must succeed. " << ctx;

    // MsgSeqNums on each side: distinct and consecutive, as stamped and as delivered.
    std::vector<seqnum_t> want;
    for (std::size_t i = 0; i < o.requested; ++i) {
        // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
        want.push_back(kFirstAppSeq + static_cast<seqnum_t>(i));
    }
    const auto sorted = [](std::vector<seqnum_t> v) {
        std::ranges::sort(v);
        return v;
    };
    EXPECT_EQ(sorted(o.reply_seq), want)
        << "ACC: the replies must carry distinct, consecutive MsgSeqNum(34) values from 2. " << ctx;
    EXPECT_EQ(sorted(o.a_seq), want)
        << "INI: the messages must carry distinct, consecutive MsgSeqNum(34) values from 2. "
        << ctx;
    const auto arrival = [](const std::vector<MtDelivery>& d) {
        std::vector<seqnum_t> v;
        // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
        for (const auto& x : d) v.push_back(x.seq);
        return v;
    };
    EXPECT_EQ(arrival(o.acc_deliveries), want)
        << "ACC: fromApp must deliver consecutive MsgSeqNums in arrival order. " << ctx;
    EXPECT_EQ(arrival(o.ini_deliveries), want)
        << "INI: fromApp must deliver consecutive MsgSeqNums in arrival order. " << ctx;

    const std::string active = std::to_string(static_cast<int>(fsm_state::Active));
    EXPECT_EQ(o.ini_state_end, active) << "INI must stay Active. " << ctx;
    EXPECT_EQ(o.acc_state_end, active) << "ACC must stay Active. " << ctx;
}

// ── Engine::send through the published snapshot ──────────────────────────────
//
// `.specify/563-564-callback-send.md` §3, "New cells": NC-1, NC-10, NC-18; §2.3 and
// §2.4 for the path they are on.

constexpr std::string_view kIdLogon = "B41-LOGON";
constexpr std::string_view kPayloadLogon =
    "35=D\x01"
    "11=B41-LOGON\x01"
    "54=1\x01"
    "55=AAA\x01";

// One Engine holding an acceptor and an initiator over plain loopback TCP, on one
// io_context that the test thread runs, with a mock clock that nothing advances. A
// cell installs its hooks on `app` before start() and calls stop() before it asserts.
struct PlainPair {
    asio::io_context ioc;
    std::shared_ptr<RecordingApp> app = std::make_shared<RecordingApp>();
    Engine engine;
    SessionId acc_id;
    SessionId ini_id;
    bool registered = false;
    bool started = false;
    bool stopped = false;

    PlainPair() : engine{ioc.get_executor(), engine_cfg()} {
        const uint16_t port = reserve_free_port(ioc);
        auto acc_cfg =
            make_plain_session_cfg(ioc, "ACCEPTOR", "INITIATOR", session_role::acceptor, port);
        auto ini_cfg =
            make_plain_session_cfg(ioc, "INITIATOR", "ACCEPTOR", session_role::initiator, port);
        acc_id = SessionId::from_config(acc_cfg);
        ini_id = SessionId::from_config(ini_cfg);
        registered = engine.register_session(std::move(acc_cfg)).has_value() &&
                     engine.register_session(std::move(ini_cfg)).has_value();
    }

    fixpp::core::EngineConfig engine_cfg() {
        fixpp::core::EngineConfig c;
        c.executor = ioc.get_executor();
        c.application = app;
        c.clock = make_mock_clock(ioc);
        return c;
    }

    bool start() {
        started = registered && engine.start().has_value();
        return started;
    }

    bool active(const SessionId& id) const {
        const auto handle = engine.lookup(id);
        return handle && handle->state() == fsm_state::Active;
    }

    void stop() {
        if (!started) return;
        auto sf = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, sf, 3s,
                                                        "EngineCallbackSend/plain_pair_stop")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *engine.clock(),
                                                            "EngineCallbackSend/plain_pair_stop");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "EngineCallbackSend/plain_pair_stop";
            return;
        }
        sf.get();
        stopped = engine.stopped();
    }
};

// The completion handler of a send whose outcome a cell reads.
auto completes_into(SendOutcome& out) {
    return [&out](std::exception_ptr ep, expected_t<void> r) {
        out.done = true;
        out.threw = static_cast<bool>(ep);
        if (!ep && !r) out.err = r.error();
    };
}

std::string list_of_deliveries(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& e : v) out += " [" + e + "]";
    return out.empty() ? " (none)" : out;
}

// ── NC-1: Engine::send from onLogon, per role ────────────────────────────────

struct OnLogonReadings {
    bool setup_ok = false;
    bool stopped = false;
    // onLogon of the session under test reached its co_spawn of engine.send.
    bool started = false;
    // That session as Engine::lookup returns it inside onLogon.
    std::string inside = "not recorded";
    int to_app_calls = 0;
    bool to_app_inside_on_logon = false;
    std::string to_app_seq = "not stamped";
    SendOutcome send;
    // What the peer session's fromApp was handed: "<ClOrdID> 34=<MsgSeqNum>".
    std::vector<std::string> peer_deliveries;
    std::string missing;  // empty, or what the cell's bound expired waiting for
};

// onLogon of the session in `role` starts engine.send on that session, with no post.
// Non-fatal only: the engine is stopped before this returns.
OnLogonReadings run_on_logon_cell(session_role role) {
    OnLogonReadings o;
    PlainPair p;
    const SessionId& me = role == session_role::initiator ? p.ini_id : p.acc_id;
    // The payload outlives the send.
    const auto payload = bytes_of(kPayloadLogon);
    bool in_on_logon = false;
    p.app->on_logon = [&](const SessionId& id) {
        if (id != me || o.started) return;
        o.started = true;
        {
            const auto handle = p.engine.lookup(me);
            o.inside = handle ? where_is(*handle) : std::string("not published");
        }
        in_on_logon = true;
        asio::co_spawn(p.ioc, p.engine.send(me, std::span<const std::byte>(payload)),
                       completes_into(o.send));
        in_on_logon = false;
    };
    p.app->on_to_app = [&](const View& mv, const SessionId& id) {
        if (id != me || value(mv, 11) != kIdLogon) return;
        ++o.to_app_calls;
        o.to_app_inside_on_logon = in_on_logon;
        o.to_app_seq = value(mv, 34);
    };
    p.app->on_from_app = [&](const View& mv, const SessionId& id) {
        if (id != me) o.peer_deliveries.push_back(value(mv, 11) + " 34=" + value(mv, 34));
    };
    if (!p.start()) {
        ADD_FAILURE() << "setup: register_session or engine.start() failed";
        return o;
    }
    o.setup_ok = true;
    if (!fixpp::test_support::pump_until(
            p.ioc, [&] { return o.send.done && !o.peer_deliveries.empty(); }, 5s,
            fixpp::test_support::kPumpSlice, "EngineCallbackSend/on_logon")) {
        o.missing = !o.started     ? "onLogon of the session under test"
                    : !o.send.done ? "the completion of the send"
                                   : "the peer's fromApp for the message";
    }
    p.app->on_logon = nullptr;
    p.app->on_to_app = nullptr;
    p.app->on_from_app = nullptr;
    p.stop();
    o.stopped = p.stopped;
    return o;
}

void run_and_check_on_logon(session_role role) {
    const OnLogonReadings o = run_on_logon_cell(role);
    ASSERT_TRUE(o.setup_ok) << "setup did not complete";
    const std::string ctx =
        std::string("onLogon ") + (o.started ? "ran" : "did not run") +
        "; the session inside onLogon: " + o.inside +
        "; toApp calls for the message: " + std::to_string(o.to_app_calls) +
        (o.to_app_inside_on_logon ? " (inside onLogon)" : " (not inside onLogon)") +
        ", 34=" + o.to_app_seq + "; the send: " + describe(o.send) +
        "; the peer's fromApp:" + list_of_deliveries(o.peer_deliveries) + ".";
    std::printf("[ MEASURED ] %s\n", ctx.c_str());
    std::fflush(stdout);

    EXPECT_TRUE(o.stopped) << "the engine must stop. " << ctx;
    EXPECT_TRUE(o.missing.empty())
        << "the cell's bound expired while it waited for " << o.missing << ". " << ctx;
    ASSERT_TRUE(o.started) << "PATH NOT DRIVEN: onLogon did not run for the session. " << ctx;
    EXPECT_EQ(o.inside, "Active open")
        << "inside onLogon the session must be published and Active. " << ctx;
    EXPECT_EQ(o.to_app_calls, 1) << "the message must reach toApp once. " << ctx;
    EXPECT_TRUE(o.to_app_inside_on_logon)
        << "the send's toApp must run inside onLogon (nested). " << ctx;
    EXPECT_EQ(describe(o.send), "ok") << "the send must succeed. " << ctx;
    EXPECT_EQ(o.peer_deliveries, std::vector<std::string>{"B41-LOGON 34=2"})
        << "the peer must receive the message once, after the Logon. " << ctx;
}

// ── NC-10: the stop check of the path to a published session ─────────────────

struct StopNotedReadings {
    bool setup_ok = false;
    bool stopped = false;
    bool flag_set = false;
    SessionSnapshot before;  // the initiator, before the send
    SessionSnapshot after;   // the initiator, once the send had returned
    SendOutcome send;
    int to_app_calls = 0;
    std::vector<std::string> peer_deliveries;
    std::string missing;  // empty, or what the cell's bound expired waiting for
};

// Message A sent by Engine::send on the initiator of an Active pair. With `note_stop`
// the initiator's engine-stop flag is set first, on the strand Engine::stop()'s first
// step sets it on, through the same call.
//
// This is a construction. The order it stands for is a send that passed its admission
// re-check, with stop()'s first step reaching the session's strand before the send's
// body starts there; no test hook sits between that re-check and the send's spawn, so
// the real interleaving is not driven.
StopNotedReadings run_stop_noted_cell(bool note_stop) {
    StopNotedReadings o;
    PlainPair p;
    const auto payload = bytes_of(kPayloadA);
    p.app->on_to_app = [&](const View& mv, const SessionId& id) {
        if (id == p.ini_id && value(mv, 11) == kIdA) ++o.to_app_calls;
    };
    p.app->on_from_app = [&](const View& mv, const SessionId& id) {
        if (id == p.acc_id) o.peer_deliveries.push_back(value(mv, 11) + " 34=" + value(mv, 34));
    };
    if (!p.start()) {
        ADD_FAILURE() << "setup: register_session or engine.start() failed";
        return o;
    }
    // Held from Active until just before stop(); a build with asserts counts it.
    std::shared_ptr<Session> ini_h;
    bool ok = fixpp::test_support::pump_until(
        p.ioc, [&] { return p.active(p.acc_id) && p.active(p.ini_id); }, 5s,
        fixpp::test_support::kPumpSlice, "EngineCallbackSend/stop_noted_active");
    if (ok) ini_h = p.engine.lookup(p.ini_id);
    o.setup_ok = ok && ini_h;
    if (!o.setup_ok) o.missing = "both sessions Active and published";
    ok = o.setup_ok;

    const auto read = [&](SessionSnapshot& snap) {
        asio::co_spawn(ini_h->executor().underlying(), read_session(*ini_h, snap),
                       [&snap](std::exception_ptr ep) {
                           snap.threw = static_cast<bool>(ep);
                           snap.done = true;
                       });
    };
    if (ok && note_stop) {
        // The registry is read here while nothing runs the io_context.
        const auto strand = engine_test_access::session_strand(p.engine, p.ini_id);
        bool noted = false;
        if (strand) {
            asio::co_spawn(
                *strand,
                [&]() -> asio::awaitable<void> {
                    session_engine_access::note_engine_stop(*ini_h);
                    co_return;
                },
                [&](std::exception_ptr ep) {
                    o.flag_set = !ep;
                    noted = true;
                });
        }
        ok = strand && fixpp::test_support::pump_until(
                           p.ioc, [&] { return noted; }, 5s, fixpp::test_support::kPumpSlice,
                           "EngineCallbackSend/stop_noted_flag");
        if (!ok) o.missing = "the engine-stop flag set on the session's strand";
    }
    if (ok) {
        read(o.before);
        ok = fixpp::test_support::pump_until(
            p.ioc, [&] { return o.before.done; }, 5s, fixpp::test_support::kPumpSlice,
            "EngineCallbackSend/stop_noted_before");
        if (!ok) o.missing = "the initiator's numbers before the send";
    }
    if (ok) {
        asio::co_spawn(p.ioc, p.engine.send(p.ini_id, std::span<const std::byte>(payload)),
                       completes_into(o.send));
        ok = fixpp::test_support::pump_until(
            p.ioc, [&] { return o.send.done; }, 5s, fixpp::test_support::kPumpSlice,
            "EngineCallbackSend/stop_noted_send");
        if (!ok) o.missing = "the completion of the send";
    }
    // The control waits for the peer's delivery, which is what "transmitted" means
    // there. The arm with the flag reads the sender's side only: a refused send leaves
    // nothing to wait for.
    if (ok && !note_stop) {
        ok = fixpp::test_support::pump_until(
            p.ioc, [&] { return !o.peer_deliveries.empty(); }, 5s, fixpp::test_support::kPumpSlice,
            "EngineCallbackSend/stop_noted_delivery");
        if (!ok) o.missing = "the peer's fromApp for the message";
    }
    if (ok) {
        read(o.after);
        ok = fixpp::test_support::pump_until(
            p.ioc, [&] { return o.after.done; }, 5s, fixpp::test_support::kPumpSlice,
            "EngineCallbackSend/stop_noted_after");
        if (!ok) o.missing = "the initiator's numbers after the send";
    }
    p.app->on_to_app = nullptr;
    p.app->on_from_app = nullptr;
    ini_h.reset();
    p.stop();
    o.stopped = p.stopped;
    return o;
}

void run_and_check_stop_noted(bool note_stop) {
    const StopNotedReadings o = run_stop_noted_cell(note_stop);
    ASSERT_TRUE(o.setup_ok) << "setup did not complete: " << o.missing;
    const std::string ctx = std::string("The engine-stop flag ") +
                            (o.flag_set ? "set" : "not set") + "; the send: " + describe(o.send) +
                            "; toApp calls for the message: " + std::to_string(o.to_app_calls) +
                            "; the peer's fromApp:" + list_of_deliveries(o.peer_deliveries) +
                            "; INI before the send: " + describe(o.before) +
                            "; INI after it: " + describe(o.after) + ".";
    std::printf("[ MEASURED ] %s\n", ctx.c_str());
    std::fflush(stdout);

    EXPECT_TRUE(o.stopped) << "the engine must stop. " << ctx;
    EXPECT_TRUE(o.missing.empty())
        << "the cell's bound expired while it waited for " << o.missing << ". " << ctx;
    const std::string active = std::to_string(static_cast<int>(fsm_state::Active));
    const std::string first_app_seq = std::to_string(kFirstAppSeq);
    EXPECT_EQ(o.before.state, active) << "INI must be Active before the send. " << ctx;
    EXPECT_EQ(o.before.mgr_next_out, first_app_seq)
        << "INI's next outbound number before the send must be the first after its Logon. " << ctx;
    // Active in both arms: what refuses the send is the flag, not the Active check.
    EXPECT_EQ(o.after.state, active) << "INI must stay Active. " << ctx;
    const auto held = [&](const SessionSnapshot& snap) {
        const auto it = snap.retained.find(kFirstAppSeq);
        return it == snap.retained.end() ? std::string("(not read)") : it->second;
    };
    if (note_stop) {
        EXPECT_TRUE(o.flag_set) << "PATH NOT DRIVEN: the flag was not set. " << ctx;
        EXPECT_EQ(o.send.err, std::optional<error>{error::session_invalid_state_for_send})
            << "the send must return session_invalid_state_for_send. " << ctx;
        EXPECT_EQ(o.to_app_calls, 0) << "the message must reach no toApp. " << ctx;
        EXPECT_EQ(o.after.mgr_next_out, o.before.mgr_next_out)
            << "the manager's NextNumOut must not move. " << ctx;
        EXPECT_EQ(o.after.store_next_out, o.before.store_next_out)
            << "the store's NextNumOut must not move. " << ctx;
        EXPECT_EQ(held(o.after), held(o.before))
            << "the store must hold nothing new under the number the message would take. " << ctx;
        EXPECT_TRUE(o.peer_deliveries.empty()) << "the peer must receive nothing. " << ctx;
    } else {
        EXPECT_EQ(describe(o.send), "ok") << "the send must succeed. " << ctx;
        EXPECT_EQ(o.to_app_calls, 1) << "the message must reach toApp once. " << ctx;
        const std::string next = std::to_string(kFirstAppSeq + 1);
        EXPECT_EQ(o.after.mgr_next_out, next)
            << "the manager's NextNumOut must have advanced once. " << ctx;
        EXPECT_EQ(o.after.store_next_out, next)
            << "the store's NextNumOut must have advanced once. " << ctx;
        EXPECT_EQ(held(o.after), std::string(kIdA))
            << "the store must hold the message under its number. " << ctx;
        EXPECT_EQ(o.peer_deliveries, std::vector<std::string>{"B41-A 34=2"})
            << "the peer must receive the message once. " << ctx;
    }
}

// ── NC-18: the path of an id that is not in the snapshot, while stop() runs ──

constexpr int kMissSendsPerId = 32;

struct MissReadings {
    bool setup_ok = false;
    // Engine::lookup of the registered acceptor returned null just before the sends.
    bool unpublished_before = false;
    bool stop_done = false;
    bool stopped = false;
    // Per id: sends whose future was ready once the io threads were joined, and their
    // results by kind.
    struct PerId {
        int ready = 0;
        int ok = 0;
        int invalid_argument = 0;
        int invalid_state = 0;
        int other = 0;
        int threw = 0;
    };
    PerId unregistered;
    PerId registered;
};

std::string describe(const MissReadings::PerId& r) {
    return "ready=" + std::to_string(r.ready) + " of " + std::to_string(kMissSendsPerId) +
           ": session_invalid_argument=" + std::to_string(r.invalid_argument) +
           " session_invalid_state_for_send=" + std::to_string(r.invalid_state) +
           " ok=" + std::to_string(r.ok) + " other=" + std::to_string(r.other) +
           " threw=" + std::to_string(r.threw);
}

// The twin of EngineSessionStrand.V15_SendCounterEnrolledBeforeControlHop_NoUAF on the
// path that keeps the control-strand hop, with V-15's numbers: three io threads, and
// per id as many sends as V-15 starts. One Engine with one acceptor that nobody
// connects to, so its id is registered and never published. Sends for it and for an
// unregistered id are started around the start of stop(), then the Engine is destroyed
// while the io_context is still alive. Which sends are on the control strand's queue
// when stop() drains is up to the scheduler: the cell claims nothing about any single
// send.
MissReadings run_miss_path_cell() {
    MissReadings o;
    asio::io_context ioc;
    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = std::make_shared<RecordingApp>();
    ecfg.clock = make_mock_clock(ioc);
    auto engine = std::make_unique<Engine>(ioc.get_executor(), std::move(ecfg));

    auto acc_cfg = make_plain_session_cfg(ioc, "ACCEPTOR", "INITIATOR", session_role::acceptor,
                                          reserve_free_port(ioc));
    const SessionId registered_id = SessionId::from_config(acc_cfg);
    SessionConfig nobody_cfg;
    nobody_cfg.sender_comp_id = "NOBODY";
    nobody_cfg.target_comp_id = "NOWHERE";
    nobody_cfg.begin_string = "FIX.4.2";
    const SessionId unregistered_id = SessionId::from_config(nobody_cfg);
    if (!engine->register_session(std::move(acc_cfg)).has_value() || !engine->start().has_value()) {
        ADD_FAILURE() << "setup: register_session or engine.start() failed";
        return o;
    }
    o.setup_ok = true;

    auto work = asio::make_work_guard(ioc);
    std::vector<std::thread> io_threads;
    for (std::size_t i = 0; i < kMtIoThreads; ++i) {
        // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
        io_threads.emplace_back([&ioc] { ioc.run(); });
    }

    const auto payload = bytes_of(kPayloadA);
    o.unpublished_before = engine->lookup(registered_id) == nullptr;
    std::vector<std::future<expected_t<void>>> unregistered;
    std::vector<std::future<expected_t<void>>> registered;
    unregistered.reserve(kMissSendsPerId);
    registered.reserve(kMissSendsPerId);
    const auto start_sends = [&](int count) {
        for (int i = 0; i < count; ++i) {
            unregistered.push_back(asio::co_spawn(
                ioc, engine->send(unregistered_id, std::span<const std::byte>(payload)),
                asio::use_future));
            registered.push_back(asio::co_spawn(
                ioc, engine->send(registered_id, std::span<const std::byte>(payload)),
                asio::use_future));
        }
    };
    // Half the sends before stop() is started and half after, so that sends are on
    // their way on both sides of stop()'s write of its flag.
    start_sends(kMissSendsPerId / 2);
    auto stop_fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);
    start_sends(kMissSendsPerId - (kMissSendsPerId / 2));
    o.stop_done = stop_fut.wait_for(12s) == std::future_status::ready;

    // Once stop() has completed the io threads run out of work and return, having run
    // every send's completion handler. If it has not, they would stay: the io_context
    // is stopped so that the cell reports instead of hanging.
    work.reset();
    if (!o.stop_done) ioc.stop();
    for (auto& t : io_threads) t.join();

    if (o.stop_done) {
        stop_fut.get();
        o.stopped = engine->stopped();
    }
    const auto collect = [](std::vector<std::future<expected_t<void>>>& futs,
                            MissReadings::PerId& out) {
        for (auto& f : futs) {
            if (f.wait_for(0s) != std::future_status::ready) continue;
            ++out.ready;
            try {
                const auto r = f.get();
                if (r) {
                    ++out.ok;
                } else if (r.error() == error::session_invalid_argument) {
                    ++out.invalid_argument;
                } else if (r.error() == error::session_invalid_state_for_send) {
                    ++out.invalid_state;
                } else {
                    ++out.other;
                }
            } catch (...) {
                ++out.threw;
            }
        }
    };
    collect(unregistered, o.unregistered);
    collect(registered, o.registered);

    // A send that is not ready here still holds the Engine: destroying it would be the
    // fault the cell exists to find, reported as a crash instead of a failure.
    if (o.stop_done && o.unregistered.ready == kMissSendsPerId &&
        o.registered.ready == kMissSendsPerId) {
        engine.reset();
    } else {
        ADD_FAILURE() << "stop() or a send did not complete: the Engine is leaked, not destroyed";
        (void)engine.release();
    }
    return o;
}

void run_and_check_miss_path() {
    const MissReadings o = run_miss_path_cell();
    ASSERT_TRUE(o.setup_ok) << "setup did not complete";
    const std::string ctx = "Unregistered id: " + describe(o.unregistered) +
                            "; registered, unpublished id: " + describe(o.registered) +
                            "; io threads=" + std::to_string(kMtIoThreads) + ".";
    std::printf("[ MEASURED ] %s\n", ctx.c_str());
    std::fflush(stdout);

    EXPECT_TRUE(o.unpublished_before)
        << "PATH NOT DRIVEN: the registered acceptor was published before the sends. " << ctx;
    EXPECT_TRUE(o.stop_done) << "stop() must complete. " << ctx;
    EXPECT_TRUE(o.stopped) << "the engine must be stopped. " << ctx;
    EXPECT_EQ(o.unregistered.ready, kMissSendsPerId)
        << "every send for the unregistered id must have completed. " << ctx;
    EXPECT_EQ(o.registered.ready, kMissSendsPerId)
        << "every send for the registered id must have completed. " << ctx;
    // Each send: session_invalid_argument or session_invalid_state_for_send. An id
    // that is registered never yields the first.
    EXPECT_EQ(o.unregistered.ok + o.unregistered.other + o.unregistered.threw, 0)
        << "a send for the unregistered id returned something else. " << ctx;
    EXPECT_EQ(
        o.registered.ok + o.registered.other + o.registered.threw + o.registered.invalid_argument,
        0)
        << "a send for the registered, unpublished id returned something other than "
           "session_invalid_state_for_send. "
        << ctx;
}

}  // namespace

TEST(EngineCallbackSend, SequentialEngineSends) {
    run_and_check(Shape::sequential, SpawnOn::io_context);
}

TEST(EngineCallbackSend, EngineSendFromToApp) {
    run_and_check(Shape::from_to_app, SpawnOn::io_context);
}

TEST(EngineCallbackSend, EngineSendFromToAppOnSessionExecutor) {
    run_and_check(Shape::from_to_app, SpawnOn::session_executor);
}

TEST(EngineCallbackSend, DeferredEngineSendCancelled_Terminal) {
    run_and_check_cancelled(asio::cancellation_type::terminal);
}

TEST(EngineCallbackSend, DeferredEngineSendCancelled_Total) {
    run_and_check_cancelled(asio::cancellation_type::total);
}

TEST(EngineCallbackSend, EngineSendFromFromApp) {
    run_and_check(Shape::from_from_app, SpawnOn::io_context);
}

TEST(EngineCallbackSend, EngineSendFromFromAppOnSessionExecutor) {
    run_and_check(Shape::from_from_app, SpawnOn::session_executor);
}

// fromApp starts a terminal close of its own session. On a build with a memory checker
// this arm is R-3's measurement: `.specify/563-564-callback-send.md` §2.5, and the row of
// NC-13 in §3, "New cells".
TEST(EngineCallbackSend, TwoEngines_FromAppStartsClose_Terminal) {
    run_and_check_two_engines(CloseStartedIn::from_app, close_mode::terminal);
}

TEST(EngineCallbackSend, TwoEngines_FromAppStartsClose_Graceful) {
    run_and_check_two_engines(CloseStartedIn::from_app, close_mode::graceful);
}

// toApp of an Engine::send starts a terminal close: the close runs while the send's frame is
// in its store call, and the frame is not transmitted.
TEST(EngineCallbackSend, TwoEngines_ToAppStartsClose_Terminal) {
    run_and_check_two_engines(CloseStartedIn::to_app, close_mode::terminal);
}

// toApp of an Engine::send starts a graceful close: the frame goes out, then the Logout.
// It is the positive control of the terminal arm's "does not receive the outer frame".
TEST(EngineCallbackSend, TwoEngines_ToAppStartsClose_Graceful) {
    run_and_check_two_engines(CloseStartedIn::to_app, close_mode::graceful);
}

TEST(EngineCallbackSend, MtEngineSendFromFromApp_Quiet) { run_and_check_mt(false); }

TEST(EngineCallbackSend, MtEngineSendFromFromApp_BusyControlStrand) { run_and_check_mt(true); }

TEST(EngineCallbackSend, EngineSendFromOnLogon_Initiator) {
    run_and_check_on_logon(session_role::initiator);
}

TEST(EngineCallbackSend, EngineSendFromOnLogon_Acceptor) {
    run_and_check_on_logon(session_role::acceptor);
}

TEST(EngineCallbackSend, StopNotedOnTheSessionRefusesEngineSend) { run_and_check_stop_noted(true); }

// The positive control of the cell above: the same setup without the flag.
TEST(EngineCallbackSend, StopNotNotedOnTheSessionAdmitsEngineSend) {
    run_and_check_stop_noted(false);
}

TEST(EngineCallbackSend, MissPathSendsQueuedWhileStopRuns) { run_and_check_miss_path(); }

// NOLINTEND(modernize-use-std-print)

}  // namespace fixpp::session::test
