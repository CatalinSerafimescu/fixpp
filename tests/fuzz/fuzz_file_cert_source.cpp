// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/fuzz/fuzz_file_cert_source.cpp
// T030 — [2g §9 seam #6] libFuzzer harness for file_cert_source.
//
// Feeds random PEM/DER inputs to file_cert_source construction via the
// make_file_cert_source factory (NOT the throwing constructor — the fuzzer
// must never trigger an unhandled exception per [const §IX.4]).
//
// Invariants:
//   - No crash / UAF / UB on adversarial inputs (ASan + UBSan).
//   - No unhandled exception escapes LLVMFuzzerTestOneInput.
//   - Any rejection is a typed error (tls_cert_load_failed or tls_cert_parse_failed).
//   - Factory always returns expected_t (never throws), confirmed by the
//     static_assert noexcept test in T026.
//
// Corpus: seeded from tests/tls/fixtures/ PEM files. The CMake target registers
// them via -seed_inputs= flag in the corpus discovery convention.
//
// Campaign note: A full ≥1-hour campaign is the /speckit-verify (T053)
// responsibility. For T034 verification a 60-second smoke run suffices.

#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory_resource>
#include <string>

// Write fuzz input to a temp file so file_cert_source can open it by path
// (the interface requires file paths, not in-memory byte spans).
static std::string write_tmp_pem(const uint8_t* data, size_t size) {
    // Use a static per-run temp filename to avoid unbounded file creation.
    // libFuzzer runs sequentially per process — no reentrancy concern.
    static const std::string kTmp =
        (std::filesystem::temp_directory_path() / "fixpp_fuzz_fcs_input.pem").string();

    FILE* f = fopen(kTmp.c_str(), "wb");
    if (!f) return {};
    fwrite(data, 1, size, f);
    fclose(f);
    return kTmp;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Bound size to avoid excessive I/O overhead.
    if (size == 0 || size > 64 * 1024) return 0;

    std::string tmp_path = write_tmp_pem(data, size);
    if (tmp_path.empty()) return 0;

    // Exercise 1: Feed as leaf cert (PEM or DER — auto-detected by content).
    {
        fixpp::tls::file_cert_source::Config cfg;
        cfg.leaf_path        = tmp_path;
        cfg.private_key_path = tmp_path;  // also fuzz the key parse

        // Factory must not throw; result is always expected_t.
        auto result = fixpp::tls::file_cert_source::make_file_cert_source(
            cfg, nullptr);
        // Consume the result to avoid unused-variable optimisation.
        if (result.has_value()) {
            // Try load_trust_anchors (synchronous, non-awaitable).
            auto trust = (*result)->load_trust_anchors();
            (void)trust;
        }
        // On error: typed fixpp error — no UB possible.
    }

    // Exercise 2: Feed as CA bundle.
    {
        fixpp::tls::file_cert_source::Config cfg;
        cfg.ca_bundle_path = tmp_path;

        auto result = fixpp::tls::file_cert_source::make_file_cert_source(
            cfg, nullptr);
        (void)result;
    }

    // Exercise 3: Tiny PMR arena — ensure exhaustion doesn't throw.
    {
        std::array<std::byte, 64> arena_buf{};
        std::pmr::monotonic_buffer_resource arena{
            arena_buf.data(), arena_buf.size(),
            std::pmr::null_memory_resource()};

        fixpp::tls::file_cert_source::Config cfg;
        cfg.leaf_path        = tmp_path;
        cfg.private_key_path = tmp_path;

        auto result = fixpp::tls::file_cert_source::make_file_cert_source(
            cfg, &arena);
        (void)result;
    }

    return 0;
}
