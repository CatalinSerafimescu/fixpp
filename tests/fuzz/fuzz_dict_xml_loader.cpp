// tests/fuzz/fuzz_dict_xml_loader.cpp
// Seam #8 of `[2c §9]` — libFuzzer entry point for XmlLoader::load_from_string.
//
// Shipped in 002-dictionary-xml-loader per `[const §VII.7]` ("new parser-touching
// code without a fuzz harness is a Gate B blocker"). Added in Gate A round 1
// per Opus root cause #3 (the earlier /plan draft deferred this harness; the
// deferral was a constitution-level override masquerading as a /plan cut).
//
// Invariants the fuzzer asserts (any violation is a crash / sanitizer report):
//   - No std::terminate (e.g., from a noexcept constructor that nonetheless
//     allocates).
//   - No std::bad_alloc escape — PMR OOM translates to dict::xml_oom_error per
//     AC-L9 / AC-P2.
//   - No exception escapes the loader other than the three documented types
//     (xml_parse_error, unknown_version_error, xml_oom_error).
//   - No ASan / UBSan / TSan report from pugixml or the loader's PMR routing.
//
// Build: requires Clang + libFuzzer sanitizer instrumentation; built under the
// asan / ubsan presets per quickstart.md §7b. Corpus on disk:
// tests/fuzz/corpus/dict_xml_loader/ (auto-created on first crash).
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <string_view>

#include "fixpp/dict/error.hpp"
#include "fixpp/dict/xml_loader.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Reinterpret fuzzer bytes as a std::string_view; this is well-defined
    // for the lifetime of the call (libFuzzer guarantees `data` is valid
    // through the call).
    auto xml = std::string_view{reinterpret_cast<const char*>(data), size};

    // PMR-aware: bound the output arena so the fuzzer can exercise the
    // AC-L9 OOM-translation path without exhausting host memory. A 1 MiB
    // monotonic_buffer_resource is generous for parsed dictionary metadata
    // but small enough that hostile inputs trigger trap_throw_or_throw.
    std::byte scratch[1 << 20];
    std::pmr::monotonic_buffer_resource arena{scratch, sizeof scratch,
                                              std::pmr::null_memory_resource()};

    fixpp::dict::XmlLoader loader{};

    // The loader's documented exception set is xml_parse_error,
    // unknown_version_error, xml_oom_error. Any other exception type
    // escaping is an invariant violation (rethrown so libFuzzer logs the
    // crash). std::bad_alloc explicitly must NOT escape — AC-P2.
    try {
        auto dict = loader.load_from_string(xml, &arena);
        // Touch the result to keep the optimizer honest. The accessors are
        // noexcept so they cannot themselves perturb fuzzer state.
        (void)dict.which_session_version();
    } catch (const fixpp::dict::xml_parse_error&) {
        // Expected on AC-L2..L8 negative paths, and -- 083 T039 / FR-006 --
        // on a group whose delimiter cannot be resolved from its own
        // declaration under the fail-closed default. NO harness widening is
        // required: XmlLoader throws the BASE type and this catch already
        // covers it (verified against T004's recorded pre-change set).
    } catch (const fixpp::dict::unknown_version_error&) {
        // Expected on AC-L4 negative paths.
    } catch (const fixpp::dict::xml_oom_error&) {
        // Expected on AC-L9 — bounded-arena exhaustion.
    } catch (const std::bad_alloc&) {
        // AC-P2 invariant violation: a std::bad_alloc that is NOT
        // xml_oom_error escaped the loader. Rethrow so libFuzzer logs the
        // crash and records the input.
        throw;
    } catch (const std::exception&) {
        // Any other std::exception escape is an invariant violation; rethrow.
        throw;
    }

    return 0;
}
