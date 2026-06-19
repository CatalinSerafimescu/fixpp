// SPDX-License-Identifier: AGPL-3.0-or-later
//
// perf/fixpp_perf_driver.cpp — Phase 9 benchmark driver for fixpp (the fixpp
// side of the cross-engine perf rig; QuickFIX-cpp / QuickFIX-J runners live in
// the parent ../phase-9-harness/{quickfix-cpp,quickfixj}/).
//
// Anchors: phases/phase-9/benchmark-plan.md §8 ("minimal initiator and acceptor
// benchmark apps with pluggable store mode and scripted app callbacks") +
// phases/phase-9/benchmark-readiness.md sequencing step 1 (MemoryStore-only,
// directional smoke, over TLS). Emits the same artifact set as the QF runners
// (phase-9-harness/results/SCHEMA.md): summary.json, latency.hgrm,
// run-config.yaml, stdout.log.
//
// Topology — HOMOGENEOUS SELF-PAIRING (benchmark-plan.md "Measurement topology"):
// one Engine drives an initiator session AND an acceptor session of fixpp's own
// kind over a real loopback socket. The driver timestamps send → matching
// response at depth 1 (closed loop). This measures fixpp-vs-fixpp; the mx-*
// cross-engine comparison is assembled offline from the three engines' self-
// paired numbers.
//
// TRANSPORT (--transport, default "plain"): the 043 plaintext sibling
// (insecure_plain_tcp) is now wired, so the DEFAULT run is plaintext-both-sides —
// apples-to-apples with the QF `tls:off` rows (blocker #3 cleared). `--transport
// tls` (mtls_ca) is retained for the TLS-overhead row (same workload, plain vs
// tls diff). The only remaining honesty caveat is the host: WSL2 is not core-
// isolated, so ABSOLUTE numbers are INDICATIVE, not publishable (blocker #4).
// All caveats are recorded into run-config.yaml + stdout.log per run.
//
// Implemented workloads: wl-01-logon-logout-fix44, wl-03-admin-idle-heartbeat,
// wl-04-nos-er-small, wl-05-nos-er-medium-groups (closed-loop),
// wl-07-burst-single-session (depth sweep), wl-08-many-sessions (32 self-paired
// pairs on one engine). Gated, non-shipped (FIXPP_BUILD_INTEROP_PERF).
//
// Threading discipline (load-bearing — see project memory):
//   * Establishment is driven single-threaded (run_for + restart) BEFORE any
//     worker thread touches the io_context.
//   * The measured loop runs worker threads on the io_context; the main thread
//     NEVER calls ioc.restart() while workers are inside ioc.run() (asio UB —
//     the BIO_ctrl SEGV). It sleep-polls atomics instead.
//   * A work_guard keeps run() alive across lulls; wg.reset() drains at teardown.
//   * Engine::stop() is co_awaited (drains all in-flight callbacks) BEFORE the
//     Engine / store are destroyed.

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <fixpp/wire/parser.hpp>

#include "support/minimal_dictionary.hpp"  // tests/support/ (via tests/ include dir)

#include <hdr/hdr_histogram.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

// ── CLI ──────────────────────────────────────────────────────────────────────

struct Options {
    std::string workload = "wl-04-nos-er-small";
    std::string engine = "fixpp";
    std::string begin_string = "FIX.4.4";
    // Transport mode. "plain" (043 insecure_plain_tcp) is the DEFAULT so the
    // fixpp rows are apples-to-apples with the QF `tls:off` rows; "tls" (mtls_ca)
    // is retained for the TLS-overhead row (same workload, plain vs tls diff).
    std::string transport = "plain";
    // Process topology. "loopback" (default) = self-paired (init+acc in ONE
    // process over a real loopback socket — the original, back-compat path).
    // "acceptor" / "initiator" = the 2-PROCESS rig: each role in its own process
    // (production-faithful; removes the self-pairing shared-pool/futex-wakeup
    // artifact). The initiator owns all RTT timing; the acceptor only serves.
    std::string role = "loopback";
    std::string host = "127.0.0.1";  // initiator dials this; acceptor binds 127.0.0.1
    int port = 0;                    // explicit shared port (required for acceptor/initiator)
    int warmup_messages = 0;    // 0 = unset → workload default resolved in main()
    int measured_messages = 0;  // 0 = unset → workload default resolved in main()
    std::filesystem::path out;  // results directory (created if absent)
};

// Single source of truth for the transport branch.
bool want_tls(const Options& o) { return o.transport == "tls"; }

[[noreturn]] void usage(const char* argv0, int code) {
    std::cerr
        << "usage: " << argv0 << " --workload <id> --out <dir>\n"
        << "           [--transport plain|tls] [--warmup-msgs N] [--measured-msgs N]\n"
        << "           [--begin-string FIX.4.4]\n"
        << "           [--role loopback|acceptor|initiator] [--host IP] [--port N]\n"
        << "  --transport plain (default) = 043 insecure_plain_tcp (apples-to-apples\n"
        << "    with QF tls:off rows); tls = mtls_ca (TLS-overhead row). TLS needs\n"
        << "    FIXPP_TLS_FIXTURE_DIR (compiled-in default = tests/tls/fixtures);\n"
        << "    plaintext needs no certs.\n"
        << "  --role loopback (default) = self-paired one-process rig. --role\n"
        << "    acceptor/initiator = 2-process rig (separate processes); both then\n"
        << "    REQUIRE --port (the shared loopback port); the initiator dials --host\n"
        << "    (default 127.0.0.1). Acceptor runs until SIGTERM, printing READY once\n"
        << "    started; initiator runs the workload, writes the bundle, exits.\n";
    std::exit(code);
}

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) usage(argv[0], 2);
            return argv[++i];
        };
        if (a == "--workload") o.workload = next();
        else if (a == "--transport") o.transport = next();
        else if (a == "--out") o.out = next();
        else if (a == "--warmup-msgs") o.warmup_messages = std::stoi(next());
        else if (a == "--measured-msgs") o.measured_messages = std::stoi(next());
        else if (a == "--begin-string") o.begin_string = next();
        else if (a == "--role") o.role = next();
        else if (a == "--host") o.host = next();
        else if (a == "--port") o.port = std::stoi(next());
        else if (a == "-h" || a == "--help") usage(argv[0], 0);
        else { std::cerr << "unknown arg: " << a << "\n"; usage(argv[0], 2); }
    }
    // The acceptor process needs no --out (it owns no result bundle); the
    // initiator and the self-paired loopback both do.
    if (o.out.empty() && o.role != "acceptor") {
        std::cerr << "--out is required\n";
        usage(argv[0], 2);
    }
    if (o.transport != "plain" && o.transport != "tls") {
        std::cerr << "--transport must be plain|tls, got: " << o.transport << "\n";
        usage(argv[0], 2);
    }
    if (o.role != "loopback" && o.role != "acceptor" && o.role != "initiator") {
        std::cerr << "--role must be loopback|acceptor|initiator, got: " << o.role << "\n";
        usage(argv[0], 2);
    }
    if ((o.role == "acceptor" || o.role == "initiator") && (o.port <= 0 || o.port > 65535)) {
        std::cerr << "--role " << o.role << " requires a valid --port (1..65535)\n";
        usage(argv[0], 2);
    }
    return o;
}

// ── TLS / port helpers (mirror tests/session/test_business_messages_roundtrip.cpp) ──

const char* fixture_dir() {
    const char* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");  // NOLINT(concurrency-mt-unsafe)
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (env && env[0] != '\0') ? env : kDir;
}

std::uint16_t reserve_free_port(asio::io_context& ioc) {
    asio::ip::tcp::acceptor a{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    a.open(ep.protocol());
    a.bind(ep);
    std::uint16_t port = a.local_endpoint().port();
    a.close();
    return port;
}

std::shared_ptr<fixpp::transport::TransportFactory> make_tls_factory(const char* dir) {
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(dir) + "/ca.pem";
    auto cs_r =
        fixpp::tls::file_cert_source::make_file_cert_source(cs_cfg, std::pmr::new_delete_resource());
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

// Build the plaintext (043 insecure_plain_tcp) SecurityProfile. The selection of
// SecurityProfile::kind::insecure_plain_tcp fires the [[deprecated]] operator-
// friction diagnostic by design; this perf driver legitimately selects it (it IS
// the plaintext benchmark), so the suppression is LOCALIZED to this one helper —
// the rest of the file keeps deprecation warnings active. Idiom mirrors the 043
// tests' file-wide pragma (test_session_plaintext_roundtrip.cpp), portable to gcc.
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
fixpp::session::SecurityProfile plain_profile() {
    return fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
}
#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

// Build a raw app-message body (the session frames it: prepends 8=/9=/34=/49=/
// 52=/56=, appends 10=). Mirrors the roundtrip test's kPayload shape.
std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> b;
    b.reserve(s.size());
    for (char c : s) b.push_back(static_cast<std::byte>(c));
    return b;
}

// Read a tag's raw value as a string_view from an inbound Index-mode MessageView
// (correlation: 112=TestReqID on Heartbeat, 11=ClOrdID on ER). Empty if absent.
std::string_view field_sv(const MessageView<access_mode::Index>& msg, std::uint16_t tag) {
    auto fv = msg.get(tag);
    if (!fv) return {};
    auto b = fv->bytes();
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

// NewOrderSingle(35=D), small / limit, no groups. ClOrdID(11) is parameterized so
// wl-07 can carry a per-order correlation id (bench-nos-burst-<seq>); the depth-1
// wl-04 path keeps the fixed "ORD-PERF".
std::vector<std::byte> make_nos_payload(std::string_view clordid = "ORD-PERF") {
    std::string s = "35=D\x01" "11=";
    s += clordid;
    s += "\x01"
         "54=1\x01"
         "55=AAPL\x01"
         "38=100\x01"
         "40=2\x01"
         "44=100.00\x01"
         "60=20240101-00:00:00.000\x01";
    return to_bytes(s);
}

// NewOrderSingle(35=D) with a flat NoPartyIDs(453) repeating group (medium /
// wl-05). Group shape matches the QFcpp runner's add_party_groups() exactly
// (quickfix-cpp/src/benchmark_runner.cpp) for cross-engine comparability:
// two entries — {448=CLIENT-ALPHA,447=D,452=1}, {448=DESK-BRAVO,447=D,452=3}.
std::vector<std::byte> make_nos_grouped_payload() {
    return to_bytes(
        "35=D\x01"
        "11=ORD-PERF\x01"
        "54=1\x01"
        "55=AAPL\x01"
        "38=100\x01"
        "40=2\x01"
        "44=100.00\x01"
        "60=20240101-00:00:00.000\x01"
        "453=2\x01"
        "448=CLIENT-ALPHA\x01"
        "447=D\x01"
        "452=1\x01"
        "448=DESK-BRAVO\x01"
        "447=D\x01"
        "452=3\x01");
}

// ExecutionReport(35=8), fully-filled echo for the NOS above. ClOrdID(11) is
// parameterized: wl-07 echoes the inbound NOS's ClOrdID so the initiator can
// correlate each ER to its order; depth-1 keeps the fixed "ORD-PERF".
std::vector<std::byte> make_er_payload(std::string_view clordid = "ORD-PERF") {
    std::string s = "35=8\x01" "37=EXEC-PERF\x01" "11=";
    s += clordid;
    s += "\x01"
         "17=E1\x01"
         "150=F\x01"
         "39=2\x01"
         "55=AAPL\x01"
         "54=1\x01"
         "151=0\x01"
         "14=100\x01"
         "6=100.00\x01";
    return to_bytes(s);
}

// ── Application: acceptor replies ER on inbound NOS; initiator records RTT ─────
//
// One Application instance serves BOTH sessions (engine has a single receiver).
// Inbound is discriminated by MsgType: the acceptor sees "D" (reply with "8"),
// the initiator sees "8" (record the receive timestamp). At depth 1 there is
// exactly one outstanding request, so no correlation key is needed.
struct PerfApp : public Application {
    fixpp::session::Engine* engine_ptr = nullptr;
    SessionId acceptor_id;
    SessionId initiator_id;
    asio::any_io_executor exec;
    std::vector<std::byte> er_payload = make_er_payload();

    std::atomic<int> logon_count{0};

    // Response signalling. The initiator's response landing is signalled via a
    // condition_variable (immediate wakeup) — NOT sleep-polling — so the wait
    // mechanism is SYMMETRIC with the QuickFIX-cpp runner's cv_.wait_for path
    // (quickfix-cpp/src/benchmark_runner.cpp). A sleep-poll here would bias the
    // fixpp throughput number down by pure harness mechanism vs QF (the
    // accidental unfairness benchmark-readiness.md exists to prevent).
    std::mutex mtx;
    std::condition_variable cv;
    std::uint64_t er_received = 0;     // guarded by mtx
    long long last_er_recv_ns = 0;     // guarded by mtx; steady_clock ns of last ER

    // Idle-liveness (wl-03): inter-heartbeat intervals observed on the INITIATOR
    // leg. Same cv-signalled discipline as the ER path (mirrors the QFcpp
    // heartbeat_intervals_us_ deque + wait_for_heartbeat_interval).
    std::mutex hb_mtx;
    std::condition_variable hb_cv;
    std::deque<double> hb_intervals_us;  // guarded by hb_mtx
    long long last_hb_ns = 0;            // guarded by hb_mtx

    // wl-07 OPEN QUESTION #1 spike: an app-initiated TestRequest (35=1, 112=<id>)
    // sent via engine.send() must round-trip as a peer auto-reply Heartbeat(35=0)
    // echoing 112. When spike_tr_id is set, the initiator's fromAdmin flips
    // spike_seen on the matching Heartbeat. Guarded by hb_mtx (reuses hb_cv).
    std::string spike_tr_id;             // guarded by hb_mtx; empty = inactive
    bool spike_seen = false;             // guarded by hb_mtx

    // wl-07 burst (depth>1): correlation_id → RTT µs (mirrors QFcpp responses_ /
    // sent_). When burst_active, fromApp(8)/fromAdmin(0) parse the correlation tag
    // (11=ClOrdID on the ER, 112=TestReqID on the Heartbeat), compute recv-sent,
    // insert into burst_responses_us, and notify_all. Guarded by burst_mtx.
    std::mutex burst_mtx;
    std::condition_variable burst_cv;
    std::unordered_map<std::string, long long> burst_sent_ns;    // id → send steady ns
    std::unordered_map<std::string, double> burst_responses_us;  // id → RTT µs
    // Keeps each in-flight send body alive until its response correlates: the
    // detached send coroutine borrows a span into the body, so a stack-local body
    // would be freed before the worker runs the send (UAF → no valid frame on the
    // wire). unordered_map node addresses are stable across rehash, so the span
    // stays valid. Erased in wait_for() once the reply arrives (strictly after the
    // send completed).
    std::unordered_map<std::string, std::vector<std::byte>> burst_bodies;
    // Cross-thread gate (main sets it; workers read it in fromApp/fromAdmin).
    // MUST be atomic: a plain bool has no happens-before with the worker threads
    // started in bring_up, so they would never observe the flip → correlation
    // paths silently skipped (single-threaded-harness-masks-races class).
    std::atomic<bool> burst_active{false};

    // wl-08 many-sessions: when set, the acceptor-leg ER reply is routed to the
    // SESSION THAT RECEIVED the NOS (the `id` arg of fromApp) rather than the
    // single hardcoded `acceptor_id`. With 32 self-paired pairs on one engine,
    // each acceptor session must reply on its OWN leg so the ER reaches the
    // matching initiator. For single-pair workloads the receiving session of a
    // "D" IS acceptor_id, so this flag changes nothing there (default false).
    // Atomic for the same reason as burst_active (cross-thread happens-before).
    std::atomic<bool> many_active{false};

    void onLogon(const SessionId& /*id*/) override {
        logon_count.fetch_add(1, std::memory_order_acq_rel);
    }

    fixpp::core::expected_t<void> fromAdmin(const MessageView<access_mode::Index>& msg,
                                            const SessionId& id) override {
        // SCHEMA: wl-03 records inter-heartbeat intervals "on the initiator
        // side". One Application serves both sessions → filter by initiator_id.
        if (msg.msg_type() == "0" && id == initiator_id) {
            const long long now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         Clock::now().time_since_epoch())
                                         .count();
            const std::string_view tr = field_sv(msg, 112);  // TestReqID echo (spike)
            {
                std::lock_guard<std::mutex> lk(hb_mtx);
                if (last_hb_ns != 0)
                    hb_intervals_us.push_back(static_cast<double>(now_ns - last_hb_ns) / 1000.0);
                last_hb_ns = now_ns;
                if (!spike_tr_id.empty() && tr == spike_tr_id) spike_seen = true;
            }
            hb_cv.notify_all();

            // wl-07 burst: a Heartbeat echoing 112 answers a driver-issued
            // TestRequest (the 10% admin leg) — correlate it.
            if (burst_active && !tr.empty()) {
                const std::string id{tr};
                std::lock_guard<std::mutex> lk(burst_mtx);
                if (auto it = burst_sent_ns.find(id); it != burst_sent_ns.end())
                    burst_responses_us[id] = static_cast<double>(now_ns - it->second) / 1000.0;
                burst_cv.notify_all();
            }
        }
        return {};
    }

    fixpp::core::expected_t<void> fromApp(const MessageView<access_mode::Index>& msg,
                                          const SessionId& id) override {
        const std::string_view mt = msg.msg_type();
        if (mt == "D" && engine_ptr != nullptr) {
            // Acceptor leg: echo an ExecutionReport back to the counterparty.
            // Post to the engine executor, then co_spawn the send there (mirrors
            // the re-entrant-send pattern in test_business_messages_roundtrip.cpp).
            // In burst mode the ER must echo the inbound NOS's ClOrdID(11) so the
            // initiator can correlate each ER (read synchronously here while msg is
            // valid; make_er_payload copies the id into the owned payload).
            auto& eng = *engine_ptr;
            // wl-08: reply on the receiving acceptor session (`id`); otherwise the
            // single-pair hardcoded acceptor_id (equivalent for one pair).
            auto sid = many_active.load(std::memory_order_acquire) ? id : acceptor_id;
            auto pl = burst_active ? make_er_payload(field_sv(msg, 11)) : er_payload;
            auto ex = exec;
            asio::post(ex, [&eng, sid, pl = std::move(pl), ex]() mutable {
                asio::co_spawn(ex, eng.send(sid, std::span<const std::byte>(pl)), asio::detached);
            });
        } else if (mt == "8") {
            // Initiator leg: the response landed — stamp (on this worker thread,
            // immune to main-thread poll granularity) + signal the waiter.
            const long long now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         Clock::now().time_since_epoch())
                                         .count();
            if (burst_active) {
                // Correlate the ER to its order via ClOrdID(11).
                const std::string id{field_sv(msg, 11)};
                std::lock_guard<std::mutex> lk(burst_mtx);
                if (auto it = burst_sent_ns.find(id); it != burst_sent_ns.end())
                    burst_responses_us[id] = static_cast<double>(now_ns - it->second) / 1000.0;
                burst_cv.notify_all();
            } else {
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    last_er_recv_ns = now_ns;
                    ++er_received;
                }
                cv.notify_one();
            }
        }
        return {};
    }
};

// ── Result emission (phase-9-harness/results/SCHEMA.md) ───────────────────────

struct RunResult {
    double messages_per_second = 0.0;
    double p50_us = 0.0;
    double p99_us = 0.0;
    double p999_us = 0.0;
    double peak_rss_mb = 0.0;
    double run_seconds = 0.0;
    double warmup_seconds = 0.0;
    int measured_messages = 0;
    int warmup_messages = 0;
};

double peak_rss_mb() {
    std::ifstream st("/proc/self/status");
    std::string key;
    while (st >> key) {
        if (key == "VmHWM:") {
            long kb = 0;
            st >> kb;
            return static_cast<double>(kb) / 1024.0;
        }
        std::getline(st, key);
    }
    return 0.0;
}

void write_summary_json(const std::filesystem::path& dir, const Options& o, const RunResult& r) {
    std::ofstream f(dir / "summary.json");
    f << "{\n"
      << "  \"workload_id\": \"" << o.workload << "\",\n"
      << "  \"engine\": \"" << o.engine << "\",\n"
      << "  \"messages_per_second\": " << r.messages_per_second << ",\n"
      << "  \"latency_p50_us\": " << r.p50_us << ",\n"
      << "  \"latency_p99_us\": " << r.p99_us << ",\n"
      << "  \"latency_p999_us\": " << r.p999_us << ",\n"
      << "  \"peak_rss_mb\": " << r.peak_rss_mb << ",\n"
      << "  \"total_allocations\": null,\n"
      << "  \"run_seconds\": " << r.run_seconds << ",\n"
      << "  \"warmup_seconds\": " << r.warmup_seconds << "\n"
      << "}\n";
}

// `mode_lines` carries the workload-specific keys (e.g. outstanding_depth /
// heartbeat_interval_s + mps_note), inserted after the common block.
void write_run_config_yaml(const std::filesystem::path& dir, const Options& o,
                           std::string_view mode_lines) {
    const bool tls = want_tls(o);
    std::ofstream f(dir / "run-config.yaml");
    f << "workload_id: " << o.workload << "\n"
      << "engine: " << o.engine << "\n"
      << "begin_string: " << o.begin_string << "\n"
      << "persistence_mode: memory-store\n"
      << "tls: " << (tls ? "on             # mtls_ca; vs QF tls:off (TLS-overhead row)"
                         : "off            # plaintext both sides; matches QF tls:off rows")
      << "\n"
      << "topology: homogeneous-self-pairing (fixpp-init <-> fixpp-acc, loopback)\n"
      << "security_profile: " << (tls ? "mtls_ca" : "insecure_plain_tcp") << "\n"
      << mode_lines
      << "warmup_messages: " << o.warmup_messages << "\n"
      << "measured_messages: " << o.measured_messages << "\n"
      << "host_class: WSL2 (non-isolated, indicative only)\n"
      << "comparison_caveat: >\n";
    if (tls) {
        f << "  TLS-overhead row. fixpp runs over mTLS while the QF tls:off rows are\n"
          << "  plaintext; WSL2 is not core-isolated. Use the paired --transport plain\n"
          << "  run for the apples-to-apples engine delta (see benchmark-readiness.md).\n";
    } else {
        f << "  Plaintext both sides (043 insecure_plain_tcp), apples-to-apples with\n"
          << "  the QF tls:off rows. Remaining caveat: WSL2 is not core-isolated, so\n"
          << "  absolute numbers are indicative, not publishable (benchmark-readiness.md #4).\n";
    }
}

// Transport-dependent stdout.log header fragments (keep both run paths in sync).
const char* topology_line(const Options& o) {
    return want_tls(o)
        ? "topology: homogeneous self-pairing (fixpp-init <-> fixpp-acc), loopback mTLS\n"
        : "topology: homogeneous self-pairing (fixpp-init <-> fixpp-acc), loopback plaintext\n";
}
const char* indicative_line(const Options& o) {
    return want_tls(o)
        ? "INDICATIVE: fixpp over mTLS vs QF tls:off plaintext (TLS-overhead row); WSL2 non-isolated.\n"
        : "PLAINTEXT both sides — apples-to-apples vs QF tls:off; WSL2 non-isolated (indicative absolute).\n";
}
const char* sec_profile_str(const Options& o) { return want_tls(o) ? "mtls_ca" : "insecure_plain_tcp"; }

// Write the HdrHistogram percentile log (standard .hgrm; readable by hdr tools).
void write_hgrm(const std::filesystem::path& dir, hdr_histogram* hist) {
    if (FILE* hf = std::fopen((dir / "latency.hgrm").string().c_str(), "w")) {
        hdr_percentiles_print(hist, hf, 5, 1.0, CLASSIC);
        std::fclose(hf);
    }
}

// ── LoopbackHarness — engine + fixpp-init/acc sessions over loopback TLS ──────
//
// Brings up one Engine driving an initiator + acceptor session of fixpp's own
// kind over a real loopback mTLS socket, drives both to Active single-threaded,
// then hands the io_context to 2 worker threads. Shared by every self-paired
// workload (wl-03/04/05/07/08). Threading discipline (project memory): NO
// ioc.restart() once workers run; work_guard keeps run() alive; Engine::stop()
// is co_awaited (drains callbacks) before the workers join.
struct LoopbackHarness {
    asio::io_context ioc;
    std::shared_ptr<PerfApp> app = std::make_shared<PerfApp>();
    std::shared_ptr<fixpp::core::system_clock_source> clock_src;
    std::shared_ptr<fixpp::transport::TransportFactory> factory;
    std::optional<fixpp::session::Engine> engine;  // Engine is non-movable → emplace in place
    SessionId ini_id;
    SessionId acc_id;
    std::vector<SessionId> ini_ids;  // wl-08: the N initiator legs (one per pair)
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> wg;
    std::thread t1;
    std::thread t2;
    bool established = false;

    // Returns false ONLY when the TLS fixtures are absent (caller emits skip).
    // Otherwise true; check `established` for whether both sessions reached
    // Active. The caller MUST call teardown() on any non-skip path.
    bool bring_up(const Options& o, int heartbeat_secs, const char* dir,
                  std::chrono::microseconds est_quantum = std::chrono::milliseconds(50)) {
        const bool tls = want_tls(o);
        if (tls) {
            factory = make_tls_factory(dir);
            if (factory == nullptr) return false;
        }
        // Plaintext (043): no TLS factory — the engine auto-derives the plaintext
        // factory from the insecure_plain_tcp profile (FR-003a); no certs needed.
        clock_src = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

        fixpp::core::EngineConfig ecfg;
        ecfg.executor = ioc.get_executor();
        ecfg.application = app;
        ecfg.clock = clock_src;
        ecfg.default_store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>();
        engine.emplace(ioc.get_executor(), std::move(ecfg));

        const std::uint16_t port = reserve_free_port(ioc);
        auto make_cfg = [&](const char* sender, const char* target,
                            fixpp::session::session_role role, const char* peer) {
            fixpp::session::SessionConfig c;
            c.sender_comp_id = sender;
            c.target_comp_id = target;
            c.begin_string = o.begin_string;
            c.role = role;
            c.executor_override = ioc.get_executor();
            if (tls) {
                c.security_profile = fixpp::session::SecurityProfile{
                    fixpp::session::SecurityProfile::kind::mtls_ca};
                c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer);
                c.transport_factory_override = factory;
            } else {
                // Plaintext: insecure_plain_tcp + NO cert binding (the authz arm
                // is suppressed when live_peer_id_ == nullopt — 043 D-10) + NO
                // factory override (engine auto-derives the plaintext factory).
                c.security_profile = plain_profile();
            }
            c.dictionary = fixpp::test_support::make_minimal_dictionary();
            c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
            c.heartbeat_interval = std::chrono::seconds{heartbeat_secs};
            c.logout_disconnect_timeout_ms = 2000;
            c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
            c.transport_send = [](std::span<const std::byte>) {};
            return c;
        };
        auto acc_cfg = make_cfg("FIXPP_ACC", "FIXPP_INIT",
                                fixpp::session::session_role::acceptor, "FIXPP_INIT");
        auto ini_cfg = make_cfg("FIXPP_INIT", "FIXPP_ACC",
                                fixpp::session::session_role::initiator, "FIXPP_ACC");
        acc_id = SessionId::from_config(acc_cfg);
        ini_id = SessionId::from_config(ini_cfg);

        app->engine_ptr = &*engine;
        app->acceptor_id = acc_id;
        app->initiator_id = ini_id;
        app->exec = ioc.get_executor();

        const bool reg_start_ok =
            engine->register_session(std::move(acc_cfg)).has_value() &&
            engine->register_session(std::move(ini_cfg)).has_value() &&
            engine->start().has_value();

        if (reg_start_ok) {
            // Establishment — single-threaded drive (NO worker threads yet; an
            // ioc.restart() while workers run ioc.run() is asio UB).
            auto deadline = Clock::now() + 10s;
            while (app->logon_count.load(std::memory_order_acquire) < 2 &&
                   Clock::now() < deadline) {
                ioc.run_for(est_quantum);
                ioc.restart();
            }
            established = app->logon_count.load(std::memory_order_acquire) >= 2;
        }

        // Hand the io_context to worker threads (uniform teardown via stop()).
        wg.emplace(asio::make_work_guard(ioc));
        t1 = std::thread{[this] { ioc.run(); }};
        t2 = std::thread{[this] { ioc.run(); }};
        return true;
    }

    // 2-PROCESS rig: bring up exactly ONE role in THIS process. Registers a single
    // session against the EXPLICIT shared --port (the peer process owns the other
    // role) and runs workers from the start — single-role has no establishment/
    // worker conflict, so the production work_guard + poll pattern applies and the
    // run_for/restart dance (and its restart-while-workers asio UB) is avoided
    // entirely. The initiator polls until its session reaches Active (logon
    // delivered); the acceptor serves reactively (PerfApp.fromApp echoes the ER on
    // the receiving leg via many_active). Acceptor readiness is gated LAUNCHER-side
    // by an `ss` LISTEN probe — the accept loop binds asynchronously inside the
    // start()-spawned run_accept_loop coroutine, so start() returning ≠ listening.
    bool bring_up_role(const Options& o, int heartbeat_secs, const char* dir) {
        const bool tls = want_tls(o);
        if (tls) {
            factory = make_tls_factory(dir);
            if (factory == nullptr) return false;
        }
        clock_src = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

        fixpp::core::EngineConfig ecfg;
        ecfg.executor = ioc.get_executor();
        ecfg.application = app;
        ecfg.clock = clock_src;
        ecfg.default_store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>();
        engine.emplace(ioc.get_executor(), std::move(ecfg));

        const std::uint16_t port = static_cast<std::uint16_t>(o.port);
        const bool is_acc = (o.role == "acceptor");
        auto make_cfg = [&](const char* sender, const char* target,
                            fixpp::session::session_role srole, const char* peer,
                            const std::string& endpoint_host) {
            fixpp::session::SessionConfig c;
            c.sender_comp_id = sender;
            c.target_comp_id = target;
            c.begin_string = o.begin_string;
            c.role = srole;
            c.executor_override = ioc.get_executor();
            if (tls) {
                c.security_profile = fixpp::session::SecurityProfile{
                    fixpp::session::SecurityProfile::kind::mtls_ca};
                c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer);
                c.transport_factory_override = factory;
            } else {
                c.security_profile = plain_profile();
            }
            c.dictionary = fixpp::test_support::make_minimal_dictionary();
            c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
            c.heartbeat_interval = std::chrono::seconds{heartbeat_secs};
            c.logout_disconnect_timeout_ms = 2000;
            c.reconnect_endpoint = fixpp::transport::Endpoint{endpoint_host, port};
            c.transport_send = [](std::span<const std::byte>) {};
            return c;
        };

        app->engine_ptr = &*engine;
        app->exec = ioc.get_executor();

        bool reg_ok = false;
        if (is_acc) {
            // Acceptor binds 127.0.0.1:port (reconnect_endpoint is repurposed as the
            // bind endpoint for an acceptor session — same as the self-paired path).
            auto acc_cfg = make_cfg("FIXPP_ACC", "FIXPP_INIT",
                                    fixpp::session::session_role::acceptor, "FIXPP_INIT",
                                    "127.0.0.1");
            acc_id = SessionId::from_config(acc_cfg);
            app->acceptor_id = acc_id;
            // Route each ER reply to the receiving acceptor leg (`id`) — the
            // single-leg id IS acceptor_id here, but this keeps fromApp uniform.
            app->many_active.store(true, std::memory_order_release);
            reg_ok = engine->register_session(std::move(acc_cfg)).has_value();
        } else {
            auto ini_cfg = make_cfg("FIXPP_INIT", "FIXPP_ACC",
                                    fixpp::session::session_role::initiator, "FIXPP_ACC",
                                    o.host);
            ini_id = SessionId::from_config(ini_cfg);
            app->initiator_id = ini_id;
            reg_ok = engine->register_session(std::move(ini_cfg)).has_value();
        }
        const bool start_ok = reg_ok && engine->start().has_value();

        // Workers from the start (NO ioc.restart() — see method comment).
        wg.emplace(asio::make_work_guard(ioc));
        t1 = std::thread{[this] { ioc.run(); }};
        t2 = std::thread{[this] { ioc.run(); }};

        if (is_acc) {
            established = start_ok;  // serves reactively; nothing to wait on here
        } else {
            // Poll until the initiator session reaches Active (logon delivered).
            auto deadline = Clock::now() + 15s;
            while (start_ok && app->logon_count.load(std::memory_order_acquire) < 1 &&
                   Clock::now() < deadline) {
                std::this_thread::sleep_for(2ms);
            }
            established = start_ok && app->logon_count.load(std::memory_order_acquire) >= 1;
        }
        return true;
    }

    // wl-08: N self-paired pairs on ONE engine/io_context (fixpp's shared-executor
    // strand model — the "scheduler/strand vs thread-per-session" comparison vs
    // QF's per-session runtimes). Each pair gets its own port + UNIQUE CompIDs
    // (FIXPP_INIT<i>/FIXPP_ACC<i>) so the 2N SessionIds don't collide. Same
    // threading discipline as bring_up (single-thread establishment → workers →
    // stop()-drain); establishment waits for 2N logons with a wider deadline.
    bool bring_up_many(const Options& o, int n_pairs, int heartbeat_secs, const char* dir) {
        const bool tls = want_tls(o);
        if (tls) {
            factory = make_tls_factory(dir);
            if (factory == nullptr) return false;
        }
        clock_src = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

        fixpp::core::EngineConfig ecfg;
        ecfg.executor = ioc.get_executor();
        ecfg.application = app;
        ecfg.clock = clock_src;
        ecfg.default_store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>();
        engine.emplace(ioc.get_executor(), std::move(ecfg));

        auto make_cfg = [&](const std::string& sender, const std::string& target,
                            fixpp::session::session_role role, const std::string& peer,
                            std::uint16_t port) {
            fixpp::session::SessionConfig c;
            c.sender_comp_id = sender;
            c.target_comp_id = target;
            c.begin_string = o.begin_string;
            c.role = role;
            c.executor_override = ioc.get_executor();
            if (tls) {
                c.security_profile = fixpp::session::SecurityProfile{
                    fixpp::session::SecurityProfile::kind::mtls_ca};
                c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer);
                c.transport_factory_override = factory;
            } else {
                c.security_profile = plain_profile();
            }
            c.dictionary = fixpp::test_support::make_minimal_dictionary();
            c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
            c.heartbeat_interval = std::chrono::seconds{heartbeat_secs};
            c.logout_disconnect_timeout_ms = 2000;
            c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
            c.transport_send = [](std::span<const std::byte>) {};
            return c;
        };

        bool reg_ok = true;
        for (int i = 0; i < n_pairs && reg_ok; ++i) {
            const std::uint16_t port = reserve_free_port(ioc);
            const std::string init = "FIXPP_INIT" + std::to_string(i);
            const std::string acc = "FIXPP_ACC" + std::to_string(i);
            auto acc_cfg = make_cfg(acc, init, fixpp::session::session_role::acceptor, init, port);
            auto ini_cfg = make_cfg(init, acc, fixpp::session::session_role::initiator, acc, port);
            ini_ids.push_back(SessionId::from_config(ini_cfg));  // compute BEFORE move
            reg_ok = engine->register_session(std::move(acc_cfg)).has_value() &&
                     engine->register_session(std::move(ini_cfg)).has_value();
        }

        app->engine_ptr = &*engine;
        app->exec = ioc.get_executor();

        if (reg_ok && engine->start().has_value()) {
            const int want = 2 * n_pairs;  // 2N logons (acc + ini per pair)
            auto deadline = Clock::now() + 30s;  // 64-session plaintext bring-up
            while (app->logon_count.load(std::memory_order_acquire) < want &&
                   Clock::now() < deadline) {
                ioc.run_for(std::chrono::milliseconds(50));
                ioc.restart();
            }
            established = app->logon_count.load(std::memory_order_acquire) >= want;
        }

        wg.emplace(asio::make_work_guard(ioc));
        t1 = std::thread{[this] { ioc.run(); }};
        t2 = std::thread{[this] { ioc.run(); }};
        return true;
    }

    void teardown() {
        if (engine.has_value()) {
            auto stop_fut = asio::co_spawn(ioc.get_executor(), engine->stop(), asio::use_future);
            auto sdl = Clock::now() + 10s;
            while (stop_fut.wait_for(0ms) != std::future_status::ready && Clock::now() < sdl)
                std::this_thread::sleep_for(1ms);
            if (stop_fut.wait_for(0ms) == std::future_status::ready) stop_fut.get();
        }
        if (wg.has_value()) wg->reset();
        if (t1.joinable()) t1.join();
        if (t2.joinable()) t2.join();
    }
};

// ── 2-process acceptor: serve replies until SIGTERM ──────────────────────────
//
// The acceptor process owns no result bundle and no timing. It stands up a single
// acceptor session and serves ER/Heartbeat replies reactively through PerfApp,
// running until the launcher signals it (SIGTERM, sent after the initiator
// finishes). The signal handler is async-signal-safe — it sets ONLY a volatile
// sig_atomic_t flag; the main loop performs the engine.stop()-drain teardown().
volatile std::sig_atomic_t g_acc_stop = 0;
extern "C" void acc_signal_handler(int) { g_acc_stop = 1; }

int run_acceptor(const Options& o) {
    const char* dir = fixture_dir();
    if (want_tls(o) && (dir == nullptr || dir[0] == '\0')) {
        std::cerr << "skip:tls-fixtures-absent (FIXPP_TLS_FIXTURE_DIR unset)\n";
        return 3;
    }
    LoopbackHarness h;
    if (!h.bring_up_role(o, 30, dir)) {
        std::cerr << "skip:tls-fixtures-absent (cert source build failed in " << dir << ")\n";
        return 3;
    }
    if (!h.established) {
        std::cerr << "acceptor failed to register/start on port " << o.port << "\n";
        h.teardown();
        return 1;
    }
    std::signal(SIGTERM, acc_signal_handler);
    std::signal(SIGINT, acc_signal_handler);
    // The launcher gates true readiness with an `ss` LISTEN probe (the listener
    // binds asynchronously inside the start()-spawned accept-loop coroutine); this
    // line just confirms register+start succeeded and is a human breadcrumb.
    std::cout << "READY role=acceptor port=" << o.port << " transport=" << o.transport
              << "\n" << std::flush;
    while (g_acc_stop == 0) std::this_thread::sleep_for(20ms);
    h.teardown();
    std::cout << "[fixpp-perf] acceptor stopped (port " << o.port << ")\n";
    return 0;
}

// ── closed-loop NOS→ER (wl-04 plain / wl-05 grouped) ─────────────────────────

int run_closed_loop(const Options& o) {
    const char* dir = fixture_dir();
    if (want_tls(o) && (dir == nullptr || dir[0] == '\0')) {
        std::cerr << "skip:tls-fixtures-absent (FIXPP_TLS_FIXTURE_DIR unset)\n";
        return 3;
    }

    LoopbackHarness h;
    // 2-process initiator registers only its own session and dials the shared port;
    // loopback (default) self-pairs both roles in one process. Same measured loop.
    const bool brought_up =
        (o.role == "initiator") ? h.bring_up_role(o, 30, dir) : h.bring_up(o, 30, dir);
    if (!brought_up) {
        std::cerr << "skip:tls-fixtures-absent (cert source build failed in " << dir << ")\n";
        return 3;
    }
    if (!h.established) {
        std::cerr << "session(s) did not reach Active in time (role=" << o.role << ")\n";
        h.teardown();
        return 1;
    }

    const auto nos =
        (o.workload == "wl-05-nos-er-medium-groups") ? make_nos_grouped_payload() : make_nos_payload();

    hdr_histogram* hist = nullptr;
    hdr_init(1, 60'000'000, 3, &hist);  // 1 µs .. 60 s, 3 sig figs

    // One closed-loop round trip: fire the NOS onto the worker pool, then block
    // on the cv until fromApp signals the matching ER (symmetric with QFcpp's
    // send-then-cv.wait_for). Returns RTT in ns, or -1 on timeout. Holding the
    // lock across co_spawn is safe: the send coroutine never touches `mtx`, and
    // fromApp's notify can't be lost (cv.wait_for releases the lock atomically).
    auto round_trip = [&]() -> long long {
        std::unique_lock<std::mutex> lk(h.app->mtx);
        const std::uint64_t prev = h.app->er_received;
        const auto t0 = Clock::now();
        asio::co_spawn(h.ioc.get_executor(),
                       h.engine->send(h.ini_id, std::span<const std::byte>(nos)), asio::detached);
        const bool ok = h.app->cv.wait_for(lk, 5s, [&] { return h.app->er_received > prev; });
        if (!ok) return -1;
        const long long recv_ns = h.app->last_er_recv_ns;
        const long long t0_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t0.time_since_epoch()).count();
        return recv_ns - t0_ns;
    };

    // ── Warmup ──
    const auto warmup_start = Clock::now();
    for (int i = 0; i < o.warmup_messages; ++i) {
        if (round_trip() < 0) {
            std::cerr << "warmup stalled at msg " << i << "\n";
            h.teardown();
            hdr_close(hist);
            return 1;
        }
    }
    const double warmup_seconds =
        std::chrono::duration<double>(Clock::now() - warmup_start).count();

    // ── Measured window ──
    const auto run_start = Clock::now();
    int recorded = 0;
    for (int i = 0; i < o.measured_messages; ++i) {
        long long rtt_ns = round_trip();
        if (rtt_ns < 0) break;
        if (rtt_ns < 1) rtt_ns = 1;
        hdr_record_value(hist, std::max<long long>(1, rtt_ns / 1000));  // ns → µs
        ++recorded;
    }
    const double run_seconds = std::chrono::duration<double>(Clock::now() - run_start).count();

    h.teardown();

    RunResult r;
    r.measured_messages = recorded;
    r.warmup_messages = o.warmup_messages;
    r.run_seconds = run_seconds;
    r.warmup_seconds = warmup_seconds;
    r.messages_per_second = (run_seconds > 0.0) ? recorded / run_seconds : 0.0;
    r.p50_us = hdr_value_at_percentile(hist, 50.0);
    r.p99_us = hdr_value_at_percentile(hist, 99.0);
    r.p999_us = hdr_value_at_percentile(hist, 99.9);
    r.peak_rss_mb = peak_rss_mb();

    std::filesystem::create_directories(o.out);
    write_summary_json(o.out, o, r);
    write_run_config_yaml(o.out, o,
        "outstanding_depth: 1\n"
        "mps_note: depth-1 closed loop, so messages_per_second ~= 1/RTT "
        "(response-rate bounded, not peak throughput); cv-signalled wait, "
        "symmetric with the QFcpp runner\n");
    write_hgrm(o.out, hist);

    std::ofstream log(o.out / "stdout.log");
    log << "fixpp perf driver — " << o.workload << " (engine=" << o.engine << ")\n"
        << topology_line(o)
        << "persistence_mode=memory-store\n"
        << indicative_line(o)
        << "begin_string=" << o.begin_string << " security_profile=" << sec_profile_str(o)
        << " depth=1\n"
        << "warmup_messages=" << o.warmup_messages << " (" << warmup_seconds << " s)\n"
        << "measured_messages=" << recorded << " (" << run_seconds << " s)\n"
        << "messages_per_second=" << r.messages_per_second << "\n"
        << "latency_us p50=" << r.p50_us << " p99=" << r.p99_us << " p999=" << r.p999_us << "\n"
        << "peak_rss_mb=" << r.peak_rss_mb << "\n"
        << "sample_request=35=D 11=ORD-PERF 55=AAPL 54=1 38=100 44=100.00"
        << (o.workload == "wl-05-nos-er-medium-groups"
                ? " 453=2[448=CLIENT-ALPHA,447=D,452=1|448=DESK-BRAVO,447=D,452=3]"
                : "")
        << "\n"
        << "sample_response=35=8 37=EXEC-PERF 150=F 39=2 14=100\n";

    std::cout << "[fixpp-perf] " << o.workload << " mps=" << r.messages_per_second
              << " p50=" << r.p50_us << "us p99=" << r.p99_us << "us p999=" << r.p999_us
              << "us recorded=" << recorded << " → " << o.out << "\n";

    hdr_close(hist);
    return 0;
}

// ── idle heartbeat (wl-03) ───────────────────────────────────────────────────
//
// HeartBtInt=1s; the initiator observes the acceptor's Heartbeats via fromAdmin.
// Latency = inter-heartbeat interval (µs); mps = observed heartbeat cadence over
// the window. Mirrors the QFcpp run_idle_heartbeat_benchmark (1s HeartBtInt,
// wait_for_heartbeat_interval). NOTE: each sample takes ~1s of wall-clock, so the
// counts are small by default (see main()'s workload-default resolution).
int run_idle_heartbeat(const Options& o) {
    const char* dir = fixture_dir();
    if (want_tls(o) && (dir == nullptr || dir[0] == '\0')) {
        std::cerr << "skip:tls-fixtures-absent (FIXPP_TLS_FIXTURE_DIR unset)\n";
        return 3;
    }

    LoopbackHarness h;
    if (!h.bring_up(o, 1, dir)) {  // 1s heartbeat interval
        std::cerr << "skip:tls-fixtures-absent (cert source build failed in " << dir << ")\n";
        return 3;
    }
    if (!h.established) {
        std::cerr << "sessions did not reach Active within 10s\n";
        h.teardown();
        return 1;
    }

    hdr_histogram* hist = nullptr;
    hdr_init(1, 60'000'000, 3, &hist);  // 1 µs .. 60 s, 3 sig figs

    // Pop the next inter-heartbeat interval (µs), cv-signalled. -1 on timeout.
    auto next_interval = [&]() -> double {
        std::unique_lock<std::mutex> lk(h.app->hb_mtx);
        const bool ok =
            h.app->hb_cv.wait_for(lk, 5s, [&] { return !h.app->hb_intervals_us.empty(); });
        if (!ok) return -1.0;
        const double v = h.app->hb_intervals_us.front();
        h.app->hb_intervals_us.pop_front();
        return v;
    };

    const auto warmup_start = Clock::now();
    for (int i = 0; i < o.warmup_messages; ++i) {
        if (next_interval() < 0) {
            std::cerr << "no heartbeat within 5s at warmup " << i << "\n";
            h.teardown();
            hdr_close(hist);
            return 1;
        }
    }
    const double warmup_seconds =
        std::chrono::duration<double>(Clock::now() - warmup_start).count();

    const auto run_start = Clock::now();
    int recorded = 0;
    for (int i = 0; i < o.measured_messages; ++i) {
        const double iv_us = next_interval();
        if (iv_us < 0) break;
        hdr_record_value(hist, std::max<long long>(1, static_cast<long long>(iv_us)));
        ++recorded;
    }
    const double run_seconds = std::chrono::duration<double>(Clock::now() - run_start).count();

    h.teardown();

    RunResult r;
    r.measured_messages = recorded;
    r.warmup_messages = o.warmup_messages;
    r.run_seconds = run_seconds;
    r.warmup_seconds = warmup_seconds;
    r.messages_per_second = (run_seconds > 0.0) ? recorded / run_seconds : 0.0;
    r.p50_us = hdr_value_at_percentile(hist, 50.0);
    r.p99_us = hdr_value_at_percentile(hist, 99.0);
    r.p999_us = hdr_value_at_percentile(hist, 99.9);
    r.peak_rss_mb = peak_rss_mb();

    std::filesystem::create_directories(o.out);
    write_summary_json(o.out, o, r);
    write_run_config_yaml(o.out, o,
        "heartbeat_interval_s: 1\n"
        "mps_note: idle-liveness; latency = inter-heartbeat interval (initiator "
        "side); messages_per_second = observed heartbeat cadence (~1/HeartBtInt)\n");
    write_hgrm(o.out, hist);

    std::ofstream log(o.out / "stdout.log");
    log << "fixpp perf driver — " << o.workload << " (engine=" << o.engine << ")\n"
        << topology_line(o)
        << "persistence_mode=memory-store\n"
        << indicative_line(o)
        << "begin_string=" << o.begin_string << " security_profile=" << sec_profile_str(o)
        << " heartbeat_interval_s=1\n"
        << "warmup_heartbeats=" << o.warmup_messages << " (" << warmup_seconds << " s)\n"
        << "measured_heartbeats=" << recorded << " (" << run_seconds << " s)\n"
        << "heartbeat_cadence_per_s=" << r.messages_per_second << "\n"
        << "inter_heartbeat_us p50=" << r.p50_us << " p99=" << r.p99_us << " p999=" << r.p999_us
        << "\n"
        << "peak_rss_mb=" << r.peak_rss_mb << "\n"
        << "sample=35=0 Heartbeat (initiator-observed inter-arrival)\n";

    std::cout << "[fixpp-perf] " << o.workload << " cadence/s=" << r.messages_per_second
              << " interval_us p50=" << r.p50_us << " p99=" << r.p99_us << " p999=" << r.p999_us
              << " recorded=" << recorded << " → " << o.out << "\n";

    hdr_close(hist);
    return 0;
}

// ── wl-01 logon/logout — session-establishment latency (connection churn) ────
//
// Mirrors QFcpp run_session_open_benchmark: each cycle constructs a FRESH
// self-paired engine (acceptor+initiator on a new loopback port), times
// construct→both-Active (Logon), then tears it down (untimed, between cycles).
// latency = establishment µs; mps = cycles/s.
//
// FAIRNESS NOTE: fixpp drives establishment SINGLE-THREADED (run_for+restart — a
// cv can't be used, nothing pumps the io_context while waiting), so the latency
// is poll-quantized. We use a TIGHT 200µs quantum (vs the 50ms default for the
// message workloads where establishment is one-time + untimed) so the number
// reflects real establishment, not poll granularity. QFcpp's wait_for_logon is
// cv-precise, so fixpp's wl-01 is ~200µs-granular — INDICATIVE establishment-rate.
int run_logon_logout(const Options& o) {
    const char* dir = fixture_dir();
    if (want_tls(o) && (dir == nullptr || dir[0] == '\0')) {
        std::cerr << "skip:tls-fixtures-absent (FIXPP_TLS_FIXTURE_DIR unset)\n";
        return 3;
    }
    constexpr auto kEstQuantum = std::chrono::microseconds(200);

    hdr_histogram* hist = nullptr;
    hdr_init(1, 60'000'000, 3, &hist);

    // One cycle: construct + establish (timed), then teardown (untimed). Returns
    // establishment µs, or -1 if the pair did not reach Active.
    auto one_cycle = [&]() -> double {
        const auto t0 = Clock::now();
        LoopbackHarness h;
        const bool ok = h.bring_up(o, 30, dir, kEstQuantum);
        const bool est = ok && h.established;
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        h.teardown();
        return est ? us : -1.0;
    };

    const auto warmup_start = Clock::now();
    for (int i = 0; i < o.warmup_messages; ++i) {
        if (one_cycle() < 0) {
            std::cerr << "warmup cycle " << i << " failed to establish (tls fixtures?)\n";
            hdr_close(hist);
            return 1;
        }
    }
    const double warmup_seconds = std::chrono::duration<double>(Clock::now() - warmup_start).count();

    int recorded = 0;
    const auto run_start = Clock::now();
    for (int i = 0; i < o.measured_messages; ++i) {
        const double us = one_cycle();
        if (us < 0) break;
        hdr_record_value(hist, std::max<long long>(1, static_cast<long long>(us)));
        ++recorded;
    }
    const double run_seconds = std::chrono::duration<double>(Clock::now() - run_start).count();

    RunResult r;
    r.measured_messages = recorded;
    r.warmup_messages = o.warmup_messages;
    r.run_seconds = run_seconds;
    r.warmup_seconds = warmup_seconds;
    r.messages_per_second = (run_seconds > 0.0) ? recorded / run_seconds : 0.0;
    r.p50_us = hdr_value_at_percentile(hist, 50.0);
    r.p99_us = hdr_value_at_percentile(hist, 99.0);
    r.p999_us = hdr_value_at_percentile(hist, 99.9);
    r.peak_rss_mb = peak_rss_mb();

    std::filesystem::create_directories(o.out);
    write_summary_json(o.out, o, r);
    write_run_config_yaml(o.out, o,
        "cycle: construct+connect+logon (establishment); teardown untimed\n"
        "establishment_poll_quantum_us: 200 (single-threaded drive; fixpp wl-01 is "
        "~200us-granular vs QFcpp cv-precise — INDICATIVE establishment-rate)\n"
        "mps_note: latency = per-cycle establishment us; messages_per_second = cycles/s\n");
    write_hgrm(o.out, hist);

    std::ofstream log(o.out / "stdout.log");
    log << "fixpp perf driver — " << o.workload << " (engine=" << o.engine << ")\n"
        << topology_line(o)
        << "persistence_mode=memory-store\n"
        << indicative_line(o)
        << "begin_string=" << o.begin_string << " security_profile=" << sec_profile_str(o)
        << " cycle=logon/logout establishment_poll_us=200\n"
        << "warmup_cycles=" << o.warmup_messages << " (" << warmup_seconds << " s)\n"
        << "measured_cycles=" << recorded << " (" << run_seconds << " s)\n"
        << "cycles_per_second=" << r.messages_per_second << "\n"
        << "establishment_us p50=" << r.p50_us << " p99=" << r.p99_us << " p999=" << r.p999_us
        << "\n"
        << "peak_rss_mb=" << r.peak_rss_mb << "\n";

    std::cout << "[fixpp-perf] " << o.workload << " cycles/s=" << r.messages_per_second
              << " establishment_us p50=" << r.p50_us << " p99=" << r.p99_us << " p999="
              << r.p999_us << " recorded=" << recorded << " → " << o.out << "\n";

    hdr_close(hist);
    return 0;
}

// ── wl-07 burst-single-session (depth sweep 1/8/64/256) ──────────────────────
//
// Peak-throughput / burst-tolerance on ONE session-pair under increasing pipeline
// depth. Structurally mirrors QFcpp run_burst_single_session_benchmark
// (quickfix-cpp/src/benchmark_runner.cpp) byte-for-byte where it makes a
// methodology choice (fairness mandate, wl-07-burst-design.md §Fairness):
//   * kDepths = {1,8,64,256}; both warmup + measured iterate all four.
//   * Sliding-window maintain-depth: send-without-wait; when pending >= depth,
//     FIFO-block on the OLDEST and record its RTT; drain the tail at depth end.
//   * 90/10 mix by GLOBAL monotonic seq: seq%10==0 → TestRequest(35=1, expect
//     Heartbeat echoing 112); else → NewOrderSingle(35=D, expect ER echoing 11).
//   * Pooled histogram across all four depths (summary.json p50/p99/p999 + mps
//     are over the pool); per-depth percentiles go to stdout.log only (shape
//     sanity — p99 MUST rise with depth, else the window isn't pipelining).
//   * NO coordinated-omission correction (symmetric with QFcpp).
int run_burst(const Options& o) {
    const char* dir = fixture_dir();
    if (want_tls(o) && (dir == nullptr || dir[0] == '\0')) {
        std::cerr << "skip:tls-fixtures-absent (FIXPP_TLS_FIXTURE_DIR unset)\n";
        return 3;
    }
    LoopbackHarness h;
    if (!h.bring_up(o, 30, dir)) {  // 30s heartbeat → liveness never fires mid-burst
        std::cerr << "skip:tls-fixtures-absent (cert source build failed in " << dir << ")\n";
        return 3;
    }
    if (!h.established) {
        std::cerr << "sessions did not reach Active within 10s\n";
        h.teardown();
        return 1;
    }
    h.app->burst_active = true;

    static constexpr int kDepths[] = {1, 8, 64, 256};
    constexpr std::size_t kND = std::size(kDepths);
    hdr_histogram* hist = nullptr;
    hdr_init(1, 60'000'000, 3, &hist);
    hdr_histogram* per_depth[kND] = {};
    for (auto& p : per_depth) hdr_init(1, 60'000'000, 3, &p);

    auto now_ns = [] {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   Clock::now().time_since_epoch())
            .count();
    };

    // Send one burst message (90/10 by seq%10), stamp the send time under
    // burst_mtx, fire onto the worker pool. Returns the correlation id.
    auto send_one = [&](long long seq) -> std::string {
        std::string id;
        std::vector<std::byte> body;
        if (seq % 10 == 0) {  // 10% admin: TestRequest → Heartbeat echoing 112
            id = "bench-admin-" + std::to_string(seq);
            body = to_bytes("35=1\x01" "112=" + id + "\x01");
        } else {              // 90% app: NewOrderSingle → ER echoing 11
            id = "bench-nos-burst-" + std::to_string(seq);
            body = make_nos_payload(id);
        }
        std::span<const std::byte> body_span;
        {
            std::lock_guard<std::mutex> lk(h.app->burst_mtx);
            h.app->burst_sent_ns[id] = now_ns();
            auto& stored = (h.app->burst_bodies[id] = std::move(body));  // keepalive
            body_span = std::span<const std::byte>(stored);
        }
        asio::co_spawn(h.ioc.get_executor(),
                       h.engine->send(h.ini_id, body_span), asio::detached);
        return id;
    };

    // FIFO wait for a specific id's RTT (µs); -1 on 5s timeout. Erases on success.
    auto wait_for = [&](const std::string& id) -> double {
        std::unique_lock<std::mutex> lk(h.app->burst_mtx);
        const bool ok = h.app->burst_cv.wait_for(lk, 5s, [&] {
            return h.app->burst_responses_us.find(id) != h.app->burst_responses_us.end();
        });
        if (!ok) return -1.0;
        const double v = h.app->burst_responses_us[id];
        h.app->burst_responses_us.erase(id);
        h.app->burst_sent_ns.erase(id);
        h.app->burst_bodies.erase(id);  // send long done — safe to free the keepalive
        return v;
    };

    bool stalled = false;
    long long seq = 0;

    // ── Warmup: all depths, global monotonic seq, results discarded ──
    const auto warmup_start = Clock::now();
    for (std::size_t di = 0; di < kND && !stalled; ++di) {
        const int depth = kDepths[di];
        std::deque<std::string> pending;
        for (int i = 0; i < o.warmup_messages && !stalled; ++i) {
            pending.push_back(send_one(seq++));
            if (static_cast<int>(pending.size()) >= depth) {
                if (wait_for(pending.front()) < 0) stalled = true;
                pending.pop_front();
            }
        }
        while (!pending.empty() && !stalled) {
            if (wait_for(pending.front()) < 0) stalled = true;
            pending.pop_front();
        }
    }
    const double warmup_seconds =
        std::chrono::duration<double>(Clock::now() - warmup_start).count();

    // ── Measured: all depths, pooled + per-depth histograms ──
    int recorded = 0;
    const auto run_start = Clock::now();
    for (std::size_t di = 0; di < kND && !stalled; ++di) {
        const int depth = kDepths[di];
        std::deque<std::string> pending;
        auto record = [&](double rtt) {
            const auto v = std::max<long long>(1, static_cast<long long>(rtt));
            hdr_record_value(hist, v);
            hdr_record_value(per_depth[di], v);
            ++recorded;
        };
        for (int i = 0; i < o.measured_messages && !stalled; ++i) {
            pending.push_back(send_one(seq++));
            if (static_cast<int>(pending.size()) >= depth) {
                const double rtt = wait_for(pending.front());
                pending.pop_front();
                if (rtt < 0) { stalled = true; break; }
                record(rtt);
            }
        }
        while (!pending.empty() && !stalled) {
            const double rtt = wait_for(pending.front());
            pending.pop_front();
            if (rtt < 0) { stalled = true; break; }
            record(rtt);
        }
    }
    const double run_seconds = std::chrono::duration<double>(Clock::now() - run_start).count();

    h.teardown();

    if (stalled)
        std::cerr << "warning: burst stalled (a response did not arrive within 5s); recorded "
                  << recorded << " before stall\n";

    RunResult r;
    r.measured_messages = recorded;
    r.warmup_messages = o.warmup_messages * static_cast<int>(kND);
    r.run_seconds = run_seconds;
    r.warmup_seconds = warmup_seconds;
    r.messages_per_second = (run_seconds > 0.0) ? recorded / run_seconds : 0.0;
    r.p50_us = hdr_value_at_percentile(hist, 50.0);
    r.p99_us = hdr_value_at_percentile(hist, 99.0);
    r.p999_us = hdr_value_at_percentile(hist, 99.9);
    r.peak_rss_mb = peak_rss_mb();

    std::filesystem::create_directories(o.out);
    write_summary_json(o.out, o, r);
    write_run_config_yaml(o.out, o,
        "burst_queue_depths: \"1,8,64,256\"\n"
        "burst_mix: \"90/10 NOS/TestRequest (seq%10==0 -> TestRequest)\"\n"
        "coordinated_omission: not-corrected (symmetric with QFcpp; depth-N closed-loop)\n"
        "mps_note: pooled across all four depths; messages_per_second = pooled sample "
        "count / measured wall-time; p50/p99/p999 over the pooled set (per-depth in stdout.log)\n");
    write_hgrm(o.out, hist);

    std::ofstream log(o.out / "stdout.log");
    log << "fixpp perf driver — " << o.workload << " (engine=" << o.engine << ")\n"
        << topology_line(o)
        << "persistence_mode=memory-store\n"
        << indicative_line(o)
        << "begin_string=" << o.begin_string << " security_profile=" << sec_profile_str(o)
        << " burst_queue_depths=1,8,64,256\n"
        << "burst_mix=90/10 NOS/TestRequest; coordinated_omission=not-corrected\n"
        << "warmup_messages=" << r.warmup_messages << " (" << warmup_seconds << " s)\n"
        << "measured_messages=" << recorded << " (pooled; " << run_seconds << " s)\n"
        << "messages_per_second=" << r.messages_per_second << "\n"
        << "latency_us(pooled) p50=" << r.p50_us << " p99=" << r.p99_us << " p999=" << r.p999_us
        << "\n";
    log << "per_depth (shape sanity — p99 should rise with depth):\n";
    for (std::size_t di = 0; di < kND; ++di) {
        log << "  depth=" << kDepths[di]
            << " p50=" << hdr_value_at_percentile(per_depth[di], 50.0)
            << " p99=" << hdr_value_at_percentile(per_depth[di], 99.0)
            << " p999=" << hdr_value_at_percentile(per_depth[di], 99.9)
            << " count=" << per_depth[di]->total_count << "\n";
    }
    log << "peak_rss_mb=" << r.peak_rss_mb << "\n"
        << "sample_app=35=D 11=bench-nos-burst-<seq> -> 35=8 (ER echoes 11)\n"
        << "sample_admin=35=1 112=bench-admin-<seq> -> 35=0 (Heartbeat echoes 112)\n"
        << (stalled ? "STATUS=STALLED (incomplete)\n" : "STATUS=OK\n");

    std::cout << "[fixpp-perf] " << o.workload << " (depths 1/8/64/256 pooled) mps="
              << r.messages_per_second << " p50=" << r.p50_us << "us p99=" << r.p99_us
              << "us p999=" << r.p999_us << "us recorded=" << recorded
              << (stalled ? " [STALLED]" : "") << "\n";
    std::cout << "[fixpp-perf]   per-depth p99(us):";
    for (std::size_t di = 0; di < kND; ++di)
        std::cout << " d" << kDepths[di] << "=" << hdr_value_at_percentile(per_depth[di], 99.0);
    std::cout << " → " << o.out << "\n";

    hdr_close(hist);
    for (auto& p : per_depth) hdr_close(p);
    return stalled ? 1 : 0;
}

// ── wl-08 many-sessions (32 self-paired pairs on one engine) ─────────────────
//
// Mirrors QFcpp run_many_sessions_benchmark (benchmark_runner.cpp:1707): kSessions
// self-paired pairs, depth-1 SERIAL round-robin (one message in flight globally),
// pooled latencies. fixpp runs ALL pairs on ONE engine/shared executor (its strand
// model) — the wl-08 "scheduler/strand vs thread-per-session" comparison point.
// Reuses the burst ClOrdID→RTT correlation (globally-unique ids are session-
// agnostic) + many_active id-routing for the per-pair ER reply.
int run_many_sessions(const Options& o) {
    constexpr int kSessions = 32;
    const char* dir = fixture_dir();
    if (want_tls(o) && (dir == nullptr || dir[0] == '\0')) {
        std::cerr << "skip:tls-fixtures-absent (FIXPP_TLS_FIXTURE_DIR unset)\n";
        return 3;
    }
    LoopbackHarness h;
    if (!h.bring_up_many(o, kSessions, 30, dir)) {
        std::cerr << "skip:tls-fixtures-absent (cert source build failed in " << dir << ")\n";
        return 3;
    }
    if (!h.established) {
        std::cerr << "sessions did not reach Active within 30s ("
                  << h.app->logon_count.load() << "/" << (2 * kSessions) << " logons)\n";
        h.teardown();
        return 1;
    }
    h.app->burst_active = true;  // reuse ClOrdID→RTT correlation
    h.app->many_active = true;   // route the ER reply to the receiving acceptor leg

    auto now_ns = [] {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   Clock::now().time_since_epoch())
            .count();
    };
    auto send_one = [&](int s, long long seq) -> std::string {
        std::string id =
            "bench-nos-mx08-s" + std::to_string(s) + "-" + std::to_string(seq);
        std::vector<std::byte> body = make_nos_payload(id);
        std::span<const std::byte> body_span;
        {
            std::lock_guard<std::mutex> lk(h.app->burst_mtx);
            h.app->burst_sent_ns[id] = now_ns();
            auto& stored = (h.app->burst_bodies[id] = std::move(body));  // keepalive
            body_span = std::span<const std::byte>(stored);
        }
        asio::co_spawn(h.ioc.get_executor(),
                       h.engine->send(h.ini_ids[static_cast<std::size_t>(s)], body_span),
                       asio::detached);
        return id;
    };
    auto wait_for = [&](const std::string& id) -> double {
        std::unique_lock<std::mutex> lk(h.app->burst_mtx);
        const bool ok = h.app->burst_cv.wait_for(lk, 5s, [&] {
            return h.app->burst_responses_us.find(id) != h.app->burst_responses_us.end();
        });
        if (!ok) return -1.0;
        const double v = h.app->burst_responses_us[id];
        h.app->burst_responses_us.erase(id);
        h.app->burst_sent_ns.erase(id);
        h.app->burst_bodies.erase(id);  // send long done — safe to free
        return v;
    };

    hdr_histogram* hist = nullptr;
    hdr_init(1, 60'000'000, 3, &hist);
    bool stalled = false;
    long long seq = 0;

    // Warmup: round-robin across all sessions, discarded.
    const auto warmup_start = Clock::now();
    for (int i = 0; i < o.warmup_messages && !stalled; ++i)
        for (int s = 0; s < kSessions && !stalled; ++s)
            if (wait_for(send_one(s, seq++)) < 0) stalled = true;
    const double warmup_seconds =
        std::chrono::duration<double>(Clock::now() - warmup_start).count();

    // Measured: round-robin across all sessions, pooled.
    int recorded = 0;
    const auto run_start = Clock::now();
    for (int i = 0; i < o.measured_messages && !stalled; ++i) {
        for (int s = 0; s < kSessions && !stalled; ++s) {
            const double rtt = wait_for(send_one(s, seq++));
            if (rtt < 0) { stalled = true; break; }
            hdr_record_value(hist, static_cast<int64_t>(rtt));
            ++recorded;
        }
    }
    const double run_seconds = std::chrono::duration<double>(Clock::now() - run_start).count();

    h.teardown();

    if (stalled)
        std::cerr << "warning: mx08 stalled (a response did not arrive within 5s); recorded "
                  << recorded << " before stall\n";

    RunResult r;
    r.measured_messages = recorded;
    r.warmup_messages = o.warmup_messages * kSessions;
    r.run_seconds = run_seconds;
    r.warmup_seconds = warmup_seconds;
    r.messages_per_second = (run_seconds > 0.0) ? recorded / run_seconds : 0.0;
    r.p50_us = hdr_value_at_percentile(hist, 50.0);
    r.p99_us = hdr_value_at_percentile(hist, 99.0);
    r.p999_us = hdr_value_at_percentile(hist, 99.9);
    r.peak_rss_mb = peak_rss_mb();

    std::filesystem::create_directories(o.out);
    write_summary_json(o.out, o, r);
    write_run_config_yaml(o.out, o,
        "session_count: 32\n"
        "topology_note: \"32 self-paired pairs on ONE engine (shared-executor strand "
        "model); depth-1 serial round-robin, one message in flight (symmetric with "
        "QFcpp run_many_sessions_benchmark)\"\n"
        "coordinated_omission: not-corrected (symmetric with QFcpp)\n");
    write_hgrm(o.out, hist);

    std::ofstream log(o.out / "stdout.log");
    log << "fixpp perf driver — " << o.workload << " (engine=" << o.engine << ")\n"
        << topology_line(o)
        << "persistence_mode=memory-store\n"
        << indicative_line(o)
        << "begin_string=" << o.begin_string << " security_profile=" << sec_profile_str(o)
        << " session_count=32\n"
        << "warmup_messages=" << r.warmup_messages << " (" << warmup_seconds << " s)\n"
        << "measured_messages=" << recorded << " (pooled across 32 sessions; " << run_seconds
        << " s)\n"
        << "messages_per_second=" << r.messages_per_second << "\n"
        << "latency_us(pooled) p50=" << r.p50_us << " p99=" << r.p99_us << " p999=" << r.p999_us
        << "\n"
        << "peak_rss_mb=" << r.peak_rss_mb << "\n"
        << "sample_app=35=D 11=bench-nos-mx08-s<sess>-<seq> -> 35=8 (ER echoes 11)\n"
        << (stalled ? "STATUS=STALLED (incomplete)\n" : "STATUS=OK\n");

    std::cout << "[fixpp-perf] " << o.workload << " (32 sessions, depth-1 round-robin) mps="
              << r.messages_per_second << " p50=" << r.p50_us << "us p99=" << r.p99_us
              << "us p999=" << r.p999_us << "us recorded=" << recorded
              << (stalled ? " [STALLED]" : "") << " → " << o.out << "\n";
    hdr_close(hist);
    return stalled ? 1 : 0;
}

// ── wl-07 OPEN QUESTION #1 spike (GREEN gate before any wl-07 loop code) ──────
//
// Proves an app-initiated TestRequest(35=1, 112=<id>) sent through the opaque
// engine.send() path: (a) is accepted by send_impl (no admin-msgtype guard —
// confirmed by source-read), (b) is routed by the peer as admin → auto-reply
// Heartbeat(35=0) echoing 112 (the FR-006 path PR #136 PRESERVED), and (c) does
// NOT tear the initiator session down (the mismatch→Disconnected arm is guarded
// on a non-empty pending_test_req_id_, which an app-initiated TR leaves empty —
// session.cpp:3303). The survival check is the seal against a false-GREEN where
// the acceptor emits the Heartbeat but the initiator session is dying.
int run_wl07_spike(const Options& o) {
    const char* dir = fixture_dir();
    if (want_tls(o) && (dir == nullptr || dir[0] == '\0')) {
        std::cerr << "skip:tls-fixtures-absent\n";
        return 3;
    }
    LoopbackHarness h;
    if (!h.bring_up(o, 30, dir)) {  // 30s heartbeat → no liveness HB in the 5s window
        std::cerr << "skip:tls-fixtures-absent (cert source build failed)\n";
        return 3;
    }
    if (!h.established) {
        std::cerr << "sessions did not reach Active within 10s\n";
        h.teardown();
        return 1;
    }

    const std::string spike_id = "PERF-SPIKE-001";
    {
        std::lock_guard<std::mutex> lk(h.app->hb_mtx);
        h.app->spike_tr_id = spike_id;
    }
    // Send one app-initiated TestRequest(35=1) from the initiator. Capture the
    // send result to distinguish a send-side reject from a no-echo.
    const auto tr_body = to_bytes("35=1\x01" "112=" + spike_id + "\x01");
    auto send_fut = asio::co_spawn(h.ioc.get_executor(),
                                   h.engine->send(h.ini_id, std::span<const std::byte>(tr_body)),
                                   asio::use_future);

    bool seen = false;
    {
        std::unique_lock<std::mutex> lk(h.app->hb_mtx);
        seen = h.app->hb_cv.wait_for(lk, 5s, [&] { return h.app->spike_seen; });
    }
    const auto send_r = send_fut.get();  // expected_t<void> — workers run it to completion

    // Survival check: a normal NOS→ER round trip must still complete.
    bool survived = false;
    if (seen) {
        const auto nos = make_nos_payload();
        std::unique_lock<std::mutex> lk(h.app->mtx);
        const std::uint64_t prev = h.app->er_received;
        asio::co_spawn(h.ioc.get_executor(),
                       h.engine->send(h.ini_id, std::span<const std::byte>(nos)), asio::detached);
        survived = h.app->cv.wait_for(lk, 5s, [&] { return h.app->er_received > prev; });
    }

    h.teardown();

    const bool green = seen && survived;
    std::cout << "[fixpp-perf] wl-07-spike:"
              << " send_accepted=" << (send_r.has_value() ? "yes" : "NO(rejected)")
              << " testrequest_echo=" << (seen ? "GREEN" : "RED")
              << " session_survived=" << (survived ? "GREEN" : (seen ? "RED" : "n/a"))
              << " → "
              << (green ? "PASS — opaque send() of 35=1 works; wl-07 UNBLOCKED"
                        : "FAIL — escalate per wl-07-burst-design.md OPEN QUESTION #1")
              << "\n";
    return green ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options o = parse_args(argc, argv);

    // 2-process acceptor: dispatched BEFORE workload resolution — one acceptor
    // process serves whichever workload the peer initiator drives (reactive ER /
    // Heartbeat replies), so it needs no per-workload branch.
    if (o.role == "acceptor") {
        return run_acceptor(o);
    }
    // 2-process initiator: only the closed-loop workloads are wired so far. Fail
    // loudly rather than silently self-pairing (bring_up) under an initiator role.
    if (o.role == "initiator" &&
        !(o.workload == "wl-04-nos-er-small" || o.workload == "wl-05-nos-er-medium-groups")) {
        std::cerr << "--role initiator is not yet wired for workload " << o.workload
                  << " (2-process slice currently supports wl-04/wl-05)\n";
        return 2;
    }

    const bool heartbeat = (o.workload == "wl-03-admin-idle-heartbeat");
    const bool burst = (o.workload == "wl-07-burst-single-session");
    const bool logon_logout = (o.workload == "wl-01-logon-logout-fix44");
    const bool many = (o.workload == "wl-08-many-sessions");
    // Resolve workload-appropriate defaults when counts were not passed. wl-03
    // heartbeats arrive ~1/s, so its defaults are small (a 20000-sample run
    // would take ~5.5 h); the closed loop is fast so it gets large defaults.
    // wl-07 counts are PER-DEPTH and run ×4 depths (warmup×4, measured×4 pooled),
    // so moderate per-depth counts keep the total bounded (depth 256 is heavy).
    // wl-08 counts are PER-SESSION and run ×32 sessions (warmup×32, measured×32
    // pooled), so modest per-session counts keep the total bounded.
    if (o.warmup_messages == 0)
        o.warmup_messages =
            heartbeat ? 3 : (burst ? 500 : (logon_logout ? 50 : (many ? 20 : 2000)));
    if (o.measured_messages == 0)
        o.measured_messages =
            heartbeat ? 15 : (burst ? 5000 : (logon_logout ? 500 : (many ? 200 : 20000)));

    if (logon_logout) {
        return run_logon_logout(o);
    }
    if (many) {
        return run_many_sessions(o);
    }
    if (o.workload == "wl-04-nos-er-small" || o.workload == "wl-05-nos-er-medium-groups") {
        return run_closed_loop(o);
    }
    if (heartbeat) {
        return run_idle_heartbeat(o);
    }
    if (burst) {
        return run_burst(o);
    }
    if (o.workload == "wl-07-spike") {
        return run_wl07_spike(o);
    }
    std::cerr << "workload not implemented: " << o.workload
              << " (have: wl-01-logon-logout-fix44, wl-03-admin-idle-heartbeat, "
                 "wl-04-nos-er-small, wl-05-nos-er-medium-groups, "
                 "wl-07-burst-single-session, wl-07-spike, wl-08-many-sessions)\n";
    return 2;
}
