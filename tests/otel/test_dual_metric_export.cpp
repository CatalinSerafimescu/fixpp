// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/otel/test_dual_metric_export.cpp
//
// TS-11: counter==3 readable via real GET http://localhost:9464/metrics AND
// received by a mock OTLP push exporter in one ForceFlush cycle (FR-017 / SC-005).
//
// Design:
//   • Build ONE sdk::metrics::MeterProvider with TWO readers:
//       1. PrometheusExporter (real civetweb server on :9464) — pull endpoint
//       2. MockOtlpPushExporter wrapped in PeriodicExportingMetricReader — push
//   • Create a uint64 counter named "fixpp.test.counter", Add(3).
//   • MeterProvider::ForceFlush() triggers ONE export cycle on the mock OTLP
//     reader; the Prometheus side is pulled via GET.
//   • Assert (a): GET :9464/metrics body contains the counter == 3 in text exposition.
//   • Assert (b): mock exporter captured a ResourceMetrics with the counter value == 3.
//
// Port robustness: if :9464 is already in use the test FAILs loudly (SDK throws
// or the HTTP connect returns ECONNREFUSED before any assertion).
// The HTTP GET has a short connect timeout (2 s) to avoid hanging.
//
// Anchor: .specify/2k-log-otel.md §4.10; contracts/otel-surface.md §Metric exporters.
// [const §XIII.3]: no opentelemetry::trace::Scope.

// Sockets for the raw HTTP GET (definition below). On Windows this MUST be the
// FIRST include in the TU: <winsock2.h> has to claim _WINSOCK2API_ before any
// header pulls in <windows.h> (which would include winsock v1), and the OTel
// headers below do exactly that. Placing it where the POSIX includes used to sit
// — after them — reproduces the same conflict from the other side.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// Self-contained linkage, mirroring tests/interop/support/counterparty_probe.hpp:
// auto-link winsock rather than making this target name ws2_32 in CMake.
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

#include <gtest/gtest.h>

#include <fixpp/otel/exporters.hpp>

// SDK — mock push exporter implementation needs these types.
#include <opentelemetry/sdk/metrics/push_metric_exporter.h>
#include <opentelemetry/sdk/metrics/export/metric_producer.h>
#include <opentelemetry/sdk/metrics/data/metric_data.h>
#include <opentelemetry/sdk/metrics/data/point_data.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/common/exporter_utils.h>

// SDK — MetricReader (for mock wrapping in PeriodicExportingMetricReader).
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>

// API — for creating a Meter and counter.
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/nostd/variant.h>

// SDK MeterProvider concrete type (needed for ForceFlush).
#include <opentelemetry/sdk/metrics/meter_provider.h>

// (Socket headers moved to the TOP of this file — on Windows <winsock2.h> must
// precede anything that pulls in <windows.h>, which the OTel headers above do.)

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sdk_metrics = opentelemetry::sdk::metrics;

// ── MockPushExporter ──────────────────────────────────────────────────────────
//
// A PushMetricExporter that records every Export() call.  Thread-safe.
// Used by TS-11 to capture the OTLP-side export without a real collector.

class MockPushExporter final : public sdk_metrics::PushMetricExporter {
public:
    MockPushExporter() = default;

    opentelemetry::sdk::common::ExportResult
    Export(const sdk_metrics::ResourceMetrics& data) noexcept override {
        std::lock_guard<std::mutex> lock(mu_);
        received_.push_back(data);
        return opentelemetry::sdk::common::ExportResult::kSuccess;
    }

    sdk_metrics::AggregationTemporality GetAggregationTemporality(
        sdk_metrics::InstrumentType) const noexcept override {
        return sdk_metrics::AggregationTemporality::kCumulative;
    }

    bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
    bool Shutdown(std::chrono::microseconds)   noexcept override { return true; }

    // Test accessor: returns a copy of all received batches.
    std::vector<sdk_metrics::ResourceMetrics> get_received() const {
        std::lock_guard<std::mutex> lock(mu_);
        return received_;
    }

private:
    mutable std::mutex mu_;
    std::vector<sdk_metrics::ResourceMetrics> received_;
};

// ── HTTP scrape helpers ────────────────────────────────────────────────────────

// Perform a raw HTTP/1.0 GET to http://127.0.0.1:<port><path> and return the
// full response body (after the blank-line header separator), or an empty string
// on error.  The connect has a 2-second timeout to fail loudly when the port
// is not listening, rather than hanging.
// Winsock and POSIX sockets differ in exactly five places for this helper: the
// handle type, the invalid-handle sentinel, the non-blocking toggle, the poll
// entry point, and getsockopt's buffer/length types. Isolating those five keeps
// http_get() below as ONE body — a duplicated per-platform copy is where two
// halves drift apart and only one of them keeps getting fixed.
#ifdef _WIN32
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;

// WSAStartup must run once per process before any winsock call. Function-local
// static init is thread-safe. Mirrors counterparty_probe.hpp's equivalent.
static void ensure_sockets_started() {
    static const bool started = [] {
        WSADATA wsa_data{};
        return ::WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();
    (void)started;
}
static void set_nonblocking(socket_t s, bool on) {
    u_long v = on ? 1u : 0u;
    ::ioctlsocket(s, FIONBIO, &v);
}
static int wait_writable(socket_t s, int timeout_ms) {
    WSAPOLLFD pfd{s, POLLWRNORM, 0};
    return ::WSAPoll(&pfd, 1, timeout_ms);
}
static int socket_error(socket_t s) {
    int err = 0;
    int len = static_cast<int>(sizeof(err));
    ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
    return err;
}
static void close_socket(socket_t s) { ::closesocket(s); }
#else
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;

static void ensure_sockets_started() {}
static void set_nonblocking(socket_t s, bool on) {
    const int flags = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
}
static int wait_writable(socket_t s, int timeout_ms) {
    pollfd pfd{s, POLLOUT, 0};
    return ::poll(&pfd, 1, timeout_ms);
}
static int socket_error(socket_t s) {
    int err = 0;
    socklen_t len = sizeof(err);
    ::getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
    return err;
}
static void close_socket(socket_t s) { ::close(s); }
#endif

static std::string http_get(uint16_t port, const char* path) {
    ensure_sockets_started();

    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalidSocket) return {};

    // Set non-blocking for the connect so we can apply a timeout.
    set_nonblocking(fd, true);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    ::connect(fd, reinterpret_cast<const sockaddr*>(&addr),
              static_cast<int>(sizeof(addr)));

    // Wait up to 2 s for the connection to succeed.
    if (wait_writable(fd, 2000) <= 0) { close_socket(fd); return {}; }

    // Check SO_ERROR for a connection failure (ECONNREFUSED etc.).
    if (socket_error(fd) != 0) { close_socket(fd); return {}; }

    // Restore blocking mode for send/recv.
    set_nonblocking(fd, false);

    // Send a minimal HTTP/1.0 request (no persistent connection).
    std::string req = std::string{"GET "} + path + " HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
    ::send(fd, req.data(), static_cast<int>(req.size()), 0);

    // Read the full response. recv() returns ssize_t on POSIX and int on
    // Windows, so let the type follow the platform rather than naming it.
    std::string resp;
    char buf[4096];
    for (;;) {
        const auto n = ::recv(fd, buf, static_cast<int>(sizeof(buf)), 0);
        if (n <= 0) break;
        resp.append(buf, static_cast<size_t>(n));
    }
    close_socket(fd);

    // Strip headers (everything up to and including the first blank line).
    const auto sep = resp.find("\r\n\r\n");
    if (sep == std::string::npos) return resp;
    return resp.substr(sep + 4);
}

// ── Helper: extract counter value from Prometheus text format ─────────────────
//
// Searches for lines matching  <metric_name>{...} <value>  or  <metric_name> <value>
// and returns the int64 value of the last matching line, or -1 if not found.
// We look for any line starting with the metric name (ignoring # comment lines).
static int64_t scrape_counter_value(const std::string& body,
                                    std::string_view   metric_name) {
    int64_t val = -1;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t eol = body.find('\n', pos);
        if (eol == std::string::npos) eol = body.size();
        std::string_view line{body.data() + pos, eol - pos};
        // Skip comment lines.
        if (!line.empty() && line[0] != '#') {
            if (line.substr(0, metric_name.size()) == metric_name) {
                // Find the last space-separated token = the value.
                size_t space = line.rfind(' ');
                if (space != std::string_view::npos) {
                    try {
                        val = std::stoll(std::string{line.substr(space + 1)});
                    } catch (...) {}
                }
            }
        }
        pos = eol + 1;
    }
    return val;
}

// ── Helper: sum all counter values in exported ResourceMetrics ────────────────
//
// Iterates over all ScopeMetrics → MetricData → PointDataAttributes looking
// for a metric named `metric_name` with a SumPointData int64 value.
// Returns the total sum, or -1 if the metric was not found.
static int64_t sum_counter_in_export(
    const std::vector<sdk_metrics::ResourceMetrics>& batches,
    std::string_view                                  metric_name)
{
    int64_t total = 0;
    bool found = false;

    for (const auto& rm : batches) {
        for (const auto& scope_m : rm.scope_metric_data_) {
            for (const auto& md : scope_m.metric_data_) {
                if (std::string_view{md.instrument_descriptor.name_} != metric_name)
                    continue;
                for (const auto& pda : md.point_data_attr_) {
                    if (const auto* sp =
                            opentelemetry::nostd::get_if<sdk_metrics::SumPointData>(
                                &pda.point_data)) {
                        if (const auto* iv =
                                opentelemetry::nostd::get_if<int64_t>(&sp->value_)) {
                            total += *iv;
                            found = true;
                        }
                    }
                }
            }
        }
    }
    return found ? total : -1;
}

// ── TS-11 test ────────────────────────────────────────────────────────────────

TEST(DualMetricExport, CounterVisibleOnBothExportersInOneCycle) {
    constexpr uint16_t kPort = 9464;
    constexpr const char* kMetricName = "fixpp_test_counter_total";

    // Build the mock OTLP push exporter and retain a raw pointer for
    // later assertion (the reader takes ownership).
    auto* raw_mock = new MockPushExporter{};
    auto mock_up = std::unique_ptr<MockPushExporter>{raw_mock};

    // Wrap the mock exporter in a PeriodicExportingMetricReader with a very
    // long interval so only ForceFlush() (not the background timer) triggers Export().
    sdk_metrics::PeriodicExportingMetricReaderOptions reader_opts;
    reader_opts.export_interval_millis = std::chrono::milliseconds{300'000};  // 5 min
    reader_opts.export_timeout_millis  = std::chrono::milliseconds{5'000};

    auto mock_reader_up =
        opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
            std::move(mock_up), reader_opts);
    auto mock_reader =
        std::shared_ptr<sdk_metrics::MetricReader>(mock_reader_up.release());

    // Build the dual-export MeterProvider:
    //   Reader 1: real Prometheus (civetweb embedded HTTP server on :9464)
    //   Reader 2: mock OTLP push reader
    fixpp::otel::PrometheusConfig prom_cfg;
    prom_cfg.host = "0.0.0.0";
    prom_cfg.port = kPort;

    auto mp = fixpp::otel::OtelDualExportBuilder{}
                .with_prometheus(prom_cfg)
                .with_otlp_reader(mock_reader)
                .build();

    // Obtain the SDK MeterProvider pointer for ForceFlush.
    auto* sdk_mp = dynamic_cast<opentelemetry::sdk::metrics::MeterProvider*>(mp.get());
    ASSERT_NE(sdk_mp, nullptr) << "OtelDualExportBuilder must return sdk MeterProvider";

    // Create a uint64 counter and add 3 (the FR-017 invariant value).
    auto meter   = mp->GetMeter("fixpp.test");
    auto counter = meter->CreateUInt64Counter("fixpp.test.counter",
                                              "TS-11 dual export counter");
    ASSERT_NE(counter.get(), nullptr) << "CreateUInt64Counter returned null";

    counter->Add(3);

    // Give the Prometheus server a brief moment to start accepting connections.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Trigger ONE export cycle on all readers (both Prometheus and mock OTLP).
    // For Prometheus (pull): we use a direct HTTP GET below.
    // For the mock OTLP push reader: ForceFlush() calls CollectAndExportOnce()
    // which calls mock_exporter.Export() synchronously.
    bool flush_ok = sdk_mp->ForceFlush(std::chrono::microseconds{10'000'000});
    EXPECT_TRUE(flush_ok) << "MeterProvider::ForceFlush() returned false";

    // ── ASSERTION (a): Prometheus scrape ─────────────────────────────────────
    //
    // GET http://127.0.0.1:9464/metrics must contain the counter == 3.
    // The OTel Prometheus exporter exposes the metric as
    //   fixpp_test_counter_total{...} 3
    // (SDK appends _total suffix for monotonic sum; attributes may appear).

    const std::string body = http_get(kPort, "/metrics");
    ASSERT_FALSE(body.empty())
        << "GET http://127.0.0.1:" << kPort << "/metrics returned empty body; "
        << "is the Prometheus exporter listening?";

    // Look for the counter (SDK appends _total for cumulative sums).
    int64_t prom_val = scrape_counter_value(body, kMetricName);
    if (prom_val < 0) {
        // Fallback: try without _total suffix (some SDK versions differ).
        prom_val = scrape_counter_value(body, "fixpp_test_counter");
    }
    EXPECT_EQ(prom_val, 3)
        << "Prometheus scrape did not find counter==3; body:\n" << body;

    // ── ASSERTION (b): mock OTLP capture ─────────────────────────────────────
    //
    // The mock exporter must have received at least one batch containing the
    // counter with value == 3.

    const auto batches = raw_mock->get_received();
    ASSERT_FALSE(batches.empty())
        << "MockPushExporter received no batches after ForceFlush; "
        << "PeriodicExportingMetricReader did not call Export()";

    int64_t otlp_val = sum_counter_in_export(batches, "fixpp.test.counter");
    EXPECT_EQ(otlp_val, 3)
        << "Mock OTLP exporter did not capture counter==3 in " << batches.size()
        << " batch(es)";

    // Shutdown cleanly (stops the civetweb server + mock reader thread).
    sdk_mp->Shutdown();
}
