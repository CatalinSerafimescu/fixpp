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
// HONESTY CAVEAT (recorded into run-config.yaml + stdout.log): fixpp is TLS-only
// (asio_tls_transport; the plaintext sibling 043 is opt-in and not wired here),
// so this run is OVER TLS while the QF `tls:off` rows are plaintext. It is an
// INDICATIVE, non-isolated-host (WSL2) reading, NOT an apples-to-apples engine
// comparison. See benchmark-readiness.md blockers #3 (plaintext) and #4 (host).
//
// Slice 1 implements wl-04-nos-er-small only (the closed-loop NOS→ER baseline);
// other workloads are scoped follow-ons (benchmark-readiness.md step 1 list:
// wl-01/03/05/07/08). This is a gated, non-shipped target (FIXPP_BUILD_INTEROP_PERF).
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
    int warmup_messages = 2000;
    int measured_messages = 20000;
    std::filesystem::path out;  // results directory (created if absent)
};

[[noreturn]] void usage(const char* argv0, int code) {
    std::cerr
        << "usage: " << argv0 << " --workload <id> --out <dir>\n"
        << "           [--warmup-msgs N] [--measured-msgs N] [--begin-string FIX.4.4]\n"
        << "  Only wl-04-nos-er-small is implemented in slice 1.\n"
        << "  Requires FIXPP_TLS_FIXTURE_DIR (compiled-in default = tests/tls/fixtures).\n";
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
        else if (a == "--out") o.out = next();
        else if (a == "--warmup-msgs") o.warmup_messages = std::stoi(next());
        else if (a == "--measured-msgs") o.measured_messages = std::stoi(next());
        else if (a == "--begin-string") o.begin_string = next();
        else if (a == "-h" || a == "--help") usage(argv[0], 0);
        else { std::cerr << "unknown arg: " << a << "\n"; usage(argv[0], 2); }
    }
    if (o.out.empty()) { std::cerr << "--out is required\n"; usage(argv[0], 2); }
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

    void onLogon(const SessionId& /*id*/) override {
        logon_count.fetch_add(1, std::memory_order_acq_rel);
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

void write_run_config_yaml(const std::filesystem::path& dir, const Options& o) {
    std::ofstream f(dir / "run-config.yaml");
    f << "workload_id: " << o.workload << "\n"
      << "engine: " << o.engine << "\n"
      << "begin_string: " << o.begin_string << "\n"
      << "persistence_mode: memory-store\n"
      << "tls: on            # fixpp is TLS-only; QF tls:off rows are plaintext\n"
      << "topology: homogeneous-self-pairing (fixpp-init <-> fixpp-acc, loopback)\n"
      << "security_profile: mtls_ca\n"
      << "outstanding_depth: 1\n"
      << "mps_note: depth-1 closed loop, so messages_per_second ~= 1/RTT "
      << "(response-rate bounded, not peak throughput); cv-signalled wait, "
      << "symmetric with the QFcpp runner\n"
      << "warmup_messages: " << o.warmup_messages << "\n"
      << "measured_messages: " << o.measured_messages << "\n"
      << "host_class: WSL2 (non-isolated, indicative only)\n"
      << "comparison_caveat: >\n"
      << "  Indicative directional read only. fixpp runs over TLS while the QF\n"
      << "  tls:off rows are plaintext; WSL2 is not core-isolated. NOT an\n"
      << "  apples-to-apples engine comparison (see benchmark-readiness.md #3/#4).\n";
}

// ── wl-04 closed-loop NOS→ER ─────────────────────────────────────────────────

int run_wl04(const Options& o) {
    const char* dir = fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        std::cerr << "skip:tls-fixtures-absent (FIXPP_TLS_FIXTURE_DIR unset)\n";
        return 3;
    }

    asio::io_context ioc;
    const std::uint16_t port = reserve_free_port(ioc);

    auto factory = make_tls_factory(dir);
    if (factory == nullptr) {
        std::cerr << "skip:tls-fixtures-absent (cert source build failed in " << dir << ")\n";
        return 3;
    }

    auto app = std::make_shared<PerfApp>();

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = app;
    ecfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    ecfg.default_store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>();

    fixpp::session::Engine engine{ioc.get_executor(), std::move(ecfg)};

    auto make_cfg = [&](const char* sender, const char* target,
                        fixpp::session::session_role role, const char* peer_compid) {
        fixpp::session::SessionConfig c;
        c.sender_comp_id = sender;
        c.target_comp_id = target;
        c.begin_string = o.begin_string;
        c.role = role;
        c.executor_override = ioc.get_executor();
        c.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
        c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer_compid);
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        c.transport_factory_override = factory;
        c.heartbeat_interval = std::chrono::seconds{30};
        c.logout_disconnect_timeout_ms = 2000;
        c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
        c.transport_send = [](std::span<const std::byte>) {};
        return c;
    };

    auto acc_cfg =
        make_cfg("FIXPP_ACC", "FIXPP_INIT", fixpp::session::session_role::acceptor, "FIXPP_INIT");
    auto ini_cfg =
        make_cfg("FIXPP_INIT", "FIXPP_ACC", fixpp::session::session_role::initiator, "FIXPP_ACC");
    const auto acc_id = SessionId::from_config(acc_cfg);
    const auto ini_id = SessionId::from_config(ini_cfg);

    if (!engine.register_session(std::move(acc_cfg)).has_value() ||
        !engine.register_session(std::move(ini_cfg)).has_value()) {
        std::cerr << "register_session failed\n";
        return 1;
    }
    if (!engine.start().has_value()) {
        std::cerr << "engine.start() failed\n";
        return 1;
    }

    app->engine_ptr = &engine;
    app->acceptor_id = acc_id;
    app->exec = ioc.get_executor();

    // ── Establishment: single-thread drive until both sessions reach Active ──
    {
        auto deadline = Clock::now() + 10s;
        while (app->logon_count.load(std::memory_order_acquire) < 2 && Clock::now() < deadline) {
            ioc.run_for(50ms);
            ioc.restart();
        }
        if (app->logon_count.load(std::memory_order_acquire) < 2) {
            std::cerr << "sessions did not reach Active within 10s\n";
            return 1;
        }
    }

    // ── Worker threads own the io_context from here (no more restart()) ──
    auto wg = asio::make_work_guard(ioc);
    std::thread t1{[&ioc] { ioc.run(); }};
    std::thread t2{[&ioc] { ioc.run(); }};

    const auto nos = make_nos_payload();

    hdr_histogram* hist = nullptr;
    hdr_init(1, 60'000'000, 3, &hist);  // 1 µs .. 60 s, 3 sig figs

    // One closed-loop round trip: fire the NOS onto the worker pool, then block
    // on the cv until fromApp signals the matching ER (symmetric with QFcpp's
    // send-then-cv.wait_for). Returns RTT in ns, or -1 on timeout. Holding the
    // lock across co_spawn is safe: the send coroutine never touches `mtx`, and
    // fromApp's notify can't be lost (cv.wait_for releases the lock atomically).
    auto round_trip = [&]() -> long long {
        std::unique_lock<std::mutex> lk(app->mtx);
        const std::uint64_t prev = app->er_received;
        const auto t0 = Clock::now();
        asio::co_spawn(ioc.get_executor(),
                       engine.send(ini_id, std::span<const std::byte>(nos)), asio::detached);
        const bool ok = app->cv.wait_for(lk, 5s, [&] { return app->er_received > prev; });
        if (!ok) return -1;
        const long long recv_ns = app->last_er_recv_ns;
        const long long t0_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t0.time_since_epoch()).count();
        return recv_ns - t0_ns;
    };

    // ── Warmup ──
    const auto warmup_start = Clock::now();
    for (int i = 0; i < o.warmup_messages; ++i) {
        if (round_trip() < 0) {
            std::cerr << "warmup stalled at msg " << i << "\n";
            wg.reset(); t1.join(); t2.join();
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

    // ── Teardown: drain the engine BEFORE releasing the guard / joining ──
    {
        auto stop_fut = asio::co_spawn(ioc.get_executor(), engine.stop(), asio::use_future);
        auto sdl = Clock::now() + 10s;
        while (stop_fut.wait_for(0ms) != std::future_status::ready && Clock::now() < sdl)
            std::this_thread::sleep_for(1ms);
        if (stop_fut.wait_for(0ms) == std::future_status::ready) stop_fut.get();
    }
    wg.reset();
    t1.join();
    t2.join();

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
    write_run_config_yaml(o.out, o);

    // latency.hgrm — standard HdrHistogram percentile log (readable by hdr tools).
    if (FILE* hf = std::fopen((o.out / "latency.hgrm").string().c_str(), "w")) {
        hdr_percentiles_print(hist, hf, 5, 1.0, CLASSIC);
        std::fclose(hf);
    }

    std::ofstream log(o.out / "stdout.log");
    log << "fixpp perf driver — " << o.workload << " (engine=" << o.engine << ")\n"
        << "topology: homogeneous self-pairing (fixpp-init <-> fixpp-acc), loopback TLS\n"
        << "persistence_mode=memory-store\n"
        << "INDICATIVE ONLY: over-TLS vs QF tls:off plaintext; WSL2 non-isolated host.\n"
        << "begin_string=" << o.begin_string << " security_profile=mtls_ca depth=1\n"
        << "warmup_messages=" << o.warmup_messages << " (" << warmup_seconds << " s)\n"
        << "measured_messages=" << recorded << " (" << run_seconds << " s)\n"
        << "messages_per_second=" << r.messages_per_second << "\n"
        << "latency_us p50=" << r.p50_us << " p99=" << r.p99_us << " p999=" << r.p999_us << "\n"
        << "peak_rss_mb=" << r.peak_rss_mb << "\n"
        << "sample_request=35=D 11=ORD-PERF 55=AAPL 54=1 38=100 44=100.00\n"
        << "sample_response=35=8 37=EXEC-PERF 150=F 39=2 14=100\n";

    std::cout << "[fixpp-perf] " << o.workload << " mps=" << r.messages_per_second
              << " p50=" << r.p50_us << "us p99=" << r.p99_us << "us p999=" << r.p999_us
              << "us recorded=" << recorded << " → " << o.out << "\n";

    hdr_close(hist);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Options o = parse_args(argc, argv);
    if (o.workload != "wl-04-nos-er-small") {
        std::cerr << "workload not implemented in slice 1: " << o.workload
                  << " (only wl-04-nos-er-small)\n";
        return 2;
    }
    return run_wl04(o);
}
