// tests/packaging/real_client/shim/hdr/hdr_histogram.h — 084 T038/T039.
//
// Witness-local stand-in for HdrHistogram_c.
//
// ── WHY A SHIM ───────────────────────────────────────────────────────────────
// perf/CMakeLists.txt obtains HdrHistogram_c via
// FetchContent_Declare(GIT_REPOSITORY https://github.com/...). This witness asks
// exactly one question — "does a REAL client build and run against the INSTALLED
// fixpp package?" — and cloning an unrelated third-party repository to answer it
// would add a network failure mode that has nothing to do with packaging, and
// would make the test unrunnable offline. HdrHistogram is the client's own
// dependency, not fixpp's; nothing about it is under test here.
//
// ── SCOPE ────────────────────────────────────────────────────────────────────
// Exactly the surface fixpp_perf_driver.cpp uses, and nothing else:
//   hdr_init, hdr_record_value, hdr_value_at_percentile,
//   hdr_percentiles_print, hdr_close, and the hdr_histogram::total_count field.
//
// Percentiles are computed for real rather than stubbed to 0. That is a few
// lines more, and it stops the witness writing FABRICATED latency numbers into a
// results bundle that is byte-shaped exactly like a genuine perf bundle — a file
// that could later be mistaken for a measurement.
//
// ⚠️ NOT a general HdrHistogram replacement, and must never be used by perf/:
// no value-range clamping, no significant-figure bucketing, unbounded memory,
// and O(n log n) per percentile query. The emitted .hgrm is deliberately NOT in
// HdrHistogram's format and says so on its first line.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

// The driver calls hdr_percentiles_print(..., CLASSIC) with the enumerator
// unqualified, as HdrHistogram_c declares it.
enum hdr_percentile_format { CLASSIC = 0, CSV = 1 };

struct hdr_histogram {
    std::vector<std::int64_t> values;
    std::int64_t total_count = 0;  // read directly by the driver
};

inline int hdr_init(std::int64_t /*lowest_trackable*/, std::int64_t /*highest_trackable*/,
                    int /*significant_figures*/, hdr_histogram** result) {
    if (result == nullptr) return -1;
    // new/delete rather than a smart pointer: hdr_init/hdr_close are a C
    // ownership pair and the driver — which is not ours to change — stores the
    // raw hdr_histogram* and calls hdr_close itself.
    *result = new hdr_histogram{};  // NOLINT(cppcoreguidelines-owning-memory)
    return 0;
}

inline bool hdr_record_value(hdr_histogram* h, std::int64_t value) {
    if (h == nullptr) return false;
    h->values.push_back(value);
    ++h->total_count;
    return true;
}

inline std::int64_t hdr_value_at_percentile(const hdr_histogram* h, double percentile) {
    if (h == nullptr || h->values.empty()) return 0;
    std::vector<std::int64_t> sorted(h->values);
    std::sort(sorted.begin(), sorted.end());
    // HdrHistogram's rank convention: ceil(p/100 * N), clamped to [1, N].
    const auto n = static_cast<double>(sorted.size());
    auto rank = static_cast<std::size_t>(((percentile / 100.0) * n) + 0.5);
    rank = std::max<std::size_t>(rank, 1);
    rank = std::min<std::size_t>(rank, sorted.size());
    return sorted[rank - 1];
}

inline int hdr_percentiles_print(hdr_histogram* h, std::FILE* out, std::int32_t /*ticks*/,
                                 double /*value_scale*/, int /*format*/) {
    if (h == nullptr || out == nullptr) return -1;
    std::fprintf(out, "# fixpp 084 packaging real-client witness — SHIM histogram.\n");
    std::fprintf(out, "# This is NOT an HdrHistogram .hgrm and is NOT a measurement.\n");
    std::fprintf(out, "# total_count=%lld\n", static_cast<long long>(h->total_count));
    for (const double p : {50.0, 90.0, 99.0, 99.9, 100.0}) {
        std::fprintf(out, "%12.6f %16lld\n", p,
                     static_cast<long long>(hdr_value_at_percentile(h, p)));
    }
    return 0;
}

inline void hdr_close(hdr_histogram* h) {
    delete h;  // NOLINT(cppcoreguidelines-owning-memory) — pairs with hdr_init
}
