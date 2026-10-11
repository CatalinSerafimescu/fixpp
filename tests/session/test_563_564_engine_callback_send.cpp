// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_563_564_engine_callback_send.cpp
//
// fixpp#563 and fixpp#564 (batch B41) — measurement only, no fix.
//
// Subject: an Engine::send started from inside an Application callback, with no
// asio::post between the callback and the co_spawn. Engine::send hops to the
// engine's control strand and from there to the session's strand; each hop is a
// co_spawn, whose first step is a dispatch. Whether the send's toApp then runs
// while the calling callback is still on the stack, or only after it returned,
// is what each cell classifies. tests/session/test_563_564_callback_send.cpp
// holds the Session::send cells.
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
//   EngineSendFromFromApp     the acceptor's fromApp for A starts
//                             engine.send(REPLY) on the acceptor, spawned on
//                             the io_context's executor.
//   EngineSendFromFromAppOnSessionExecutor
//                             the same, spawned on the acceptor session's
//                             executor.
//
// Classification, printed by every cell and attached to every failure:
//   nested     the second send's toApp ran while the callback that started it
//              was still on the stack;
//   deferred   it ran only after that callback had returned;
//   never ran  it did not run within the cell's bound.
// "deferred" does not fail a cell and "never ran" does. The sequence-number
// assertions run for either of the first two.
//
// What a cell asserts, per session that sent: every frame toApp saw carries its
// own MsgSeqNum(34), the values are consecutive from 2 (each session's Logon
// is number 1), the session's store holds each frame under the number it was stamped
// with, and the manager's and the store's next outbound number agree. Per
// session that received: fromApp saw the peer's messages with consecutive
// MsgSeqNums in arrival order. Both sends report success and both sessions stay
// Active.
//
// On a build with asserts a nested entry is expected to stop in
// callback_dispatch_scope before the second toApp's body runs, so the cell
// cannot print its classification. The lines the hook writes to stderr around
// the co_spawn bracket that case: an abort between "starting" and "returned"
// happened while the outer callback was on the stack.
//
// Observation limits: the bytes on the socket are not tapped. What was stamped
// is read from the frame toApp is handed, which is the buffer the send path
// stores and transmits; what was retained is read from the sender's store; what
// arrived is what the peer session delivered to fromApp, so a frame the peer
// session refused shows only as a missing delivery, an admin callback and a
// state change.
//
// Fixture: tests/session/test_application_engine_send.cpp's, one Engine holding
// an acceptor and an initiator over loopback mTLS on one io_context run by the
// test thread, plus a bounded MemoryStore on each session. A multi-threaded
// io_context is not covered. No fatal assertion sits between Engine::start()
// and a completed stop(): a cell collects into `Readings`, stops the engine,
// and asserts afterwards.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
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
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

namespace fixpp::session::test {

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
    return std::string(frame.substr(pos, end == std::string_view::npos ? frame.npos : end - pos));
}

std::string value(const View& mv, std::uint32_t tag) {
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
                               const char* peer_compid, uint16_t port) {
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
    sc.outbound_capacity = 16;
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
Readings run_cell(Shape shape, SpawnOn spawn_on) {
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
    const asio::any_io_executor exec =
        spawn_on == SpawnOn::session_executor
            ? asio::any_io_executor{session_of(starter).executor().underlying()}
            : asio::any_io_executor{ioc.get_executor()};

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
    // Starts the second send from inside the calling callback: no post, the
    // co_spawn is issued synchronously in the callback's body.
    const auto start_second = [&](const char* from) {
        o.started_second = true;
        in_callback = true;
        std::fprintf(stderr, "[ B41 ] %s: starting engine.send for the second message\n", from);
        asio::co_spawn(exec, engine.send(second_id, std::span<const std::byte>(second_payload)),
                       record_into(o.second));
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
    // fromApp, or a session left Active, or the bound.
    const std::size_t expected_deliveries = 2;
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
        want.push_back("11=" + std::string(ids[i]) +
                       " 34=" + std::to_string(kFirstAppSeq + static_cast<seqnum_t>(i)));
    }
    EXPECT_EQ(got, want) << name(w) << ": fromApp deliveries, in arrival order. " << ctx;
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
    }
    const std::string active = std::to_string(static_cast<int>(fsm_state::Active));
    EXPECT_EQ(o.ini_state_end, active) << "INI must stay Active. " << ctx;
    EXPECT_EQ(o.acc_state_end, active) << "ACC must stay Active. " << ctx;
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

TEST(EngineCallbackSend, EngineSendFromFromApp) {
    run_and_check(Shape::from_from_app, SpawnOn::io_context);
}

TEST(EngineCallbackSend, EngineSendFromFromAppOnSessionExecutor) {
    run_and_check(Shape::from_from_app, SpawnOn::session_executor);
}

}  // namespace fixpp::session::test
