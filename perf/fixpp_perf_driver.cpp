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
// Implemented workloads: wl-03-admin-idle-heartbeat, wl-04-nos-er-small,
// wl-05-nos-er-medium-groups (closed-loop). wl-01/07/08 are scoped follow-ons
// (benchmark-readiness.md step 1). Gated, non-shipped (FIXPP_BUILD_INTEROP_PERF).
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
        << "  --transport plain (default) = 043 insecure_plain_tcp (apples-to-apples\n"
        << "    with QF tls:off rows); tls = mtls_ca (TLS-overhead row). TLS needs\n"
        << "    FIXPP_TLS_FIXTURE_DIR (compiled-in default = tests/tls/fixtures);\n"
        << "    plaintext needs no certs.\n";
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
        else if (a == "-h" || a == "--help") usage(argv[0], 0);
        else { std::cerr << "unknown arg: " << a << "\n"; usage(argv[0], 2); }
    }
    if (o.out.empty()) { std::cerr << "--out is required\n"; usage(argv[0], 2); }
    if (o.transport != "plain" && o.transport != "tls") {
        std::cerr << "--transport must be plain|tls, got: " << o.transport << "\n";
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

// NewOrderSingle(35=D), small / limit, no groups.
std::vector<std::byte> make_nos_payload() {
    return to_bytes(
        "35=D\x01"
        "11=ORD-PERF\x01"
        "54=1\x01"
        "55=AAPL\x01"
        "38=100\x01"
        "40=2\x01"
        "44=100.00\x01"
        "60=20240101-00:00:00.000\x01");
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

// ExecutionReport(35=8), fully-filled echo for the NOS above.
std::vector<std::byte> make_er_payload() {
    return to_bytes(
        "35=8\x01"
        "37=EXEC-PERF\x01"
        "11=ORD-PERF\x01"
        "17=E1\x01"
        "150=F\x01"
        "39=2\x01"
        "55=AAPL\x01"
        "54=1\x01"
        "151=0\x01"
        "14=100\x01"
        "6=100.00\x01");
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
            {
                std::lock_guard<std::mutex> lk(hb_mtx);
                if (last_hb_ns != 0)
                    hb_intervals_us.push_back(static_cast<double>(now_ns - last_hb_ns) / 1000.0);
                last_hb_ns = now_ns;
            }
            hb_cv.notify_one();
        }
        return {};
    }

    fixpp::core::expected_t<void> fromApp(const MessageView<access_mode::Index>& msg,
                                          const SessionId& /*id*/) override {
        const std::string_view mt = msg.msg_type();
        if (mt == "D" && engine_ptr != nullptr) {
            // Acceptor leg: echo an ExecutionReport back to the counterparty.
            // Post to the engine executor, then co_spawn the send there (mirrors
            // the re-entrant-send pattern in test_business_messages_roundtrip.cpp).
            auto& eng = *engine_ptr;
            auto sid = acceptor_id;
            auto pl = er_payload;
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
            {
                std::lock_guard<std::mutex> lk(mtx);
                last_er_recv_ns = now_ns;
                ++er_received;
            }
            cv.notify_one();
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
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> wg;
    std::thread t1;
    std::thread t2;
    bool established = false;

    // Returns false ONLY when the TLS fixtures are absent (caller emits skip).
    // Otherwise true; check `established` for whether both sessions reached
    // Active. The caller MUST call teardown() on any non-skip path.
    bool bring_up(const Options& o, int heartbeat_secs, const char* dir) {
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
                ioc.run_for(50ms);
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

// ── closed-loop NOS→ER (wl-04 plain / wl-05 grouped) ─────────────────────────

int run_closed_loop(const Options& o) {
    const char* dir = fixture_dir();
    if (want_tls(o) && (dir == nullptr || dir[0] == '\0')) {
        std::cerr << "skip:tls-fixtures-absent (FIXPP_TLS_FIXTURE_DIR unset)\n";
        return 3;
    }

    LoopbackHarness h;
    if (!h.bring_up(o, 30, dir)) {
        std::cerr << "skip:tls-fixtures-absent (cert source build failed in " << dir << ")\n";
        return 3;
    }
    if (!h.established) {
        std::cerr << "sessions did not reach Active within 10s\n";
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

}  // namespace

int main(int argc, char** argv) {
    Options o = parse_args(argc, argv);
    const bool heartbeat = (o.workload == "wl-03-admin-idle-heartbeat");
    // Resolve workload-appropriate defaults when counts were not passed. wl-03
    // heartbeats arrive ~1/s, so its defaults are small (a 20000-sample run
    // would take ~5.5 h); the closed loop is fast so it gets large defaults.
    if (o.warmup_messages == 0) o.warmup_messages = heartbeat ? 3 : 2000;
    if (o.measured_messages == 0) o.measured_messages = heartbeat ? 15 : 20000;

    if (o.workload == "wl-04-nos-er-small" || o.workload == "wl-05-nos-er-medium-groups") {
        return run_closed_loop(o);
    }
    if (heartbeat) {
        return run_idle_heartbeat(o);
    }
    std::cerr << "workload not implemented: " << o.workload
              << " (have: wl-03-admin-idle-heartbeat, wl-04-nos-er-small, "
                 "wl-05-nos-er-medium-groups)\n";
    return 2;
}
