// tests/fuzz/fuzz_orchestra_loader.cpp
// 074-orchestra-native-reader — libFuzzer entry point for
// OrchestraLoader::load_from_string. Mirrors fuzz_dict_xml_loader.cpp
// (Seam #8 of `[2c §9]`) per `[const §VII.7]` ("new parser-touching code
// without a fuzz harness is a Gate B blocker") applied to the new native
// Orchestra reader.
//
// Invariants the fuzzer asserts (any violation is a crash / sanitizer report):
//   - No std::terminate (e.g., from a noexcept constructor that nonetheless
//     allocates).
//   - No std::bad_alloc escape — PMR OOM translates to dict::xml_oom_error
//     (074 reuses the trap_throw_or_throw<xml_oom_error> path, same as
//     XmlLoader).
//   - No exception escapes the loader other than the four documented types
//     (orchestra_parse_error, unknown_version_error,
//     group_delimiter_collision_error, xml_oom_error — contracts/
//     orchestra_loader.md).
//   - No ASan / UBSan / TSan report from pugixml or the loader's PMR routing.
//
// Build: requires Clang + libFuzzer sanitizer instrumentation; built only
// under FIXPP_BUILD_FUZZ=ON. Corpus on disk:
// tests/fuzz/corpus/orchestra_loader/.
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <string_view>

#include "fixpp/dict/error.hpp"
#include "fixpp/dict/orchestra_loader.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Reinterpret fuzzer bytes as a std::string_view; this is well-defined
    // for the lifetime of the call (libFuzzer guarantees `data` is valid
    // through the call).
    auto xml = std::string_view{reinterpret_cast<const char*>(data), size};

    // PMR-aware: bound the output arena so the fuzzer can exercise the
    // OOM-translation path without exhausting host memory. A 1 MiB
    // monotonic_buffer_resource is generous for parsed dictionary metadata
    // but small enough that hostile inputs trigger trap_throw_or_throw.
    std::byte scratch[1 << 20];
    std::pmr::monotonic_buffer_resource arena{scratch, sizeof scratch,
                                              std::pmr::null_memory_resource()};

    fixpp::dict::OrchestraLoader loader{};

    // The loader's documented exception set is orchestra_parse_error,
    // unknown_version_error, group_delimiter_collision_error, xml_oom_error.
    // Any other exception type escaping is an invariant violation (rethrown
    // so libFuzzer logs the crash). std::bad_alloc explicitly must NOT
    // escape.
    try {
        auto dict = loader.load_from_string(xml, &arena);
        // Touch the result to keep the optimizer honest. The accessors are
        // noexcept so they cannot themselves perturb fuzzer state.
        (void)dict.which_session_version();
    } catch (const fixpp::dict::orchestra_parse_error&) {
        // Expected on malformed/unknown-datatype/wrong-root/dangling-ref
        // negative paths.
    } catch (const fixpp::dict::group_delimiter_collision_error&) {
        // Expected: a nested group's delimiter equals its parent's.
    } catch (const fixpp::dict::unknown_version_error&) {
        // Expected: <fixr:repository version=...> is not FIX Latest.
    } catch (const fixpp::dict::xml_oom_error&) {
        // Expected — bounded-arena exhaustion.
    } catch (const std::bad_alloc&) {
        // Invariant violation: a std::bad_alloc that is NOT xml_oom_error
        // escaped the loader. Rethrow so libFuzzer logs the crash and
        // records the input.
        throw;
    } catch (const std::exception&) {
        // Any other std::exception escape is an invariant violation; rethrow.
        throw;
    }

    return 0;
}
