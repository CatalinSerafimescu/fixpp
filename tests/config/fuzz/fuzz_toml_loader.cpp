// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/fuzz/fuzz_toml_loader.cpp
//
// T037 — [const §VII.7] MUST — libFuzzer harness for load_toml_config().
// 044-toml-session-config (FR-012 / validation rule 9).
//
// Central guarantee: EVERY byte sequence either yields a ConfigBundle or a
// vector<LoadDiagnostic> — the noexcept boundary NEVER terminates or triggers
// UB, regardless of input.  Any escape → std::terminate → libFuzzer crash
// report (which is exactly the behaviour we want to catch).
//
// Design:
//   - Static io_context (never run()) provides a valid executor for
//     system_clock_source / TLS-factory construction.  Function-local static
//     keeps it reachable at exit so LSan stays quiet.
//   - Per-process temp file path (PID-qualified) is re-used across calls for
//     speed; each call overwrites it with the fuzzer's bytes.  The ofstream
//     scope closes before load_toml_config() is called, ensuring the file is
//     fully flushed before the loader reads it.
//   - -max_len limits tomlplusplus allocation on deeply-nested hostile inputs;
//     see smoke command in CMakeLists.txt comment.
//   - No try/catch: load_toml_config is noexcept; any throw → terminate →
//     libFuzzer records the crash.  Catching here would mask the invariant
//     violation we are testing for.
//
// Seed corpus: tests/config/fuzz/corpus/toml_config_loader/
// Full ≥10-min campaign at Gate B (CI), per tasks.md T037.

#include <cstddef>
#include <cstdint>
#include <cstdio>             // ::getpid
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <string>

#include <asio/io_context.hpp>

#include <fixpp/config/toml_config_loader.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Static executor: a single io_context whose executor is reused across
    // all fuzzer iterations.  Never called run() — we only need a valid
    // executor for the loader's object construction (system_clock_source,
    // TLS TransportFactory).
    static asio::io_context ioc;
    static auto exec = ioc.get_executor();

    // Per-process temp file: avoid cross-run clobbering under parallel -jobs.
    static const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() /
        ("fuzz_toml_" + std::to_string(static_cast<long>(::getpid())) + ".toml");

    // Write input bytes to the temp file.  The ofstream is destroyed (flushed
    // and closed) before load_toml_config is called.
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(data),
                static_cast<std::streamsize>(size));
        // f closes here — dtor flushes before load_toml_config reads the file.
    }

    fixpp::config::LoadOptions opts{exec, std::pmr::get_default_resource()};

    // load_toml_config is noexcept: every input MUST return without throwing.
    // Touch the result minimally so the optimizer cannot elide the call.
    auto r = fixpp::config::load_toml_config(tmp, opts);
    if (r) {
        (void)r->sessions.size();
    } else {
        (void)r.error().size();
    }

    return 0;
}
