// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/fuzz/fuzz_092_support.hpp — shared by the two 092-garbled-frame-reject
// fuzz arms (specs/092-garbled-frame-reject/tasks.md T066, T067):
//   - fuzz_session_recovery_admin_parse.cpp (research.md R-2 §2): the header scan
//     against OffsetTable::build;
//   - fuzz_wire_validator.cpp (research.md R-7 "Fuzz"): the field iterator's fault
//     record against OffsetTable::build, and (T062a) validate() on a view whose
//     build failed (failed_build_prescan_counter below).
//
// Both arms compare a reader against OffsetTable::build under a dictionary that
// declares a dictionary-only Length+Data pair, and both skip ONLY the inputs whose
// build status is a resource failure (wire_offset_table_full, out_of_memory:
// data-model.md E-0 excludes those from the fault set). The skip counter exists so
// a run that skipped everything is visible: it is printed to stderr at process
// exit and, when FIXPP_FUZZ_092_COUNT_FILE names a file, appended to it as one
// line per process (so a `-fork` campaign, whose child output is not kept, can be
// summed across children).
//
// Anchors: research.md R-2, R-7; data-model.md E-0, E-1, E-4; [const §VII.7].
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <string_view>

namespace fixpp::fuzz092 {

// A dictionary declaring a Length+Data pair outside the standard table
// (CustomLen 5001, then CustomData 5002), so its hooks differ from
// dict_hooks::none().
inline constexpr std::string_view kPairDictXml =
    R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
    R"(<fields>)"
    R"(<field number='8' name='BeginString' type='STRING'/>)"
    R"(<field number='9' name='BodyLength' type='INT'/>)"
    R"(<field number='10' name='CheckSum' type='STRING'/>)"
    R"(<field number='35' name='MsgType' type='STRING'/>)"
    R"(<field number='5001' name='CustomLen' type='LENGTH'/>)"
    R"(<field number='5002' name='CustomData' type='DATA'/>)"
    R"(</fields>)"
    R"(<messages>)"
    R"(<message name='TestMsg' msgtype='T' msgcat='app'>)"
    R"(<field name='CustomLen' required='N'/>)"
    R"(<field name='CustomData' required='N'/>)"
    R"(</message>)"
    R"(</messages></fix>)";

// Loaded once per process; the arms read it and never modify it.
inline fixpp::dict::table_view const& pair_dict_table_view() {
    static std::pmr::monotonic_buffer_resource mr;
    static fixpp::dict::Dictionary const dict =
        fixpp::dict::XmlLoader{}.load_from_string(kPairDictXml, &mr);
    static fixpp::dict::table_view const tv = dict.as_table_view();
    return tv;
}

// The two build statuses that are not encoding faults (data-model E-0).
[[nodiscard]] inline bool is_resource_failure(
    fixpp::core::expected_t<void> const& status) noexcept {
    return !status.has_value() && (status.error() == fixpp::core::error::wire_offset_table_full ||
                                   status.error() == fixpp::core::error::out_of_memory);
}

// Per-process counters of one arm. `cases` counts (input, hook set) comparisons
// attempted; `skipped` counts those abandoned on a resource-failure build status.
struct arm_counter {
    char const* arm_name = "";
    std::uint64_t cases = 0;
    std::uint64_t skipped = 0;
};

// Registers the exit report once, on the first call. libFuzzer's normal exit
// (-runs / -max_total_time) goes through exit(), so atexit handlers run; a
// crashing process does not report.
inline arm_counter& counter(char const* arm_name) {
    static arm_counter c{.arm_name = arm_name};
    static bool const registered = [] {
        std::atexit([] {
            std::fprintf(stderr, "[092 fuzz arm %s] skipped %llu of %llu cases (resource status)\n",
                         c.arm_name, static_cast<unsigned long long>(c.skipped),
                         static_cast<unsigned long long>(c.cases));
            if (char const* path = std::getenv("FIXPP_FUZZ_092_COUNT_FILE")) {
                if (std::FILE* f = std::fopen(path, "a")) {
                    std::fprintf(f, "%s skipped=%llu cases=%llu\n", c.arm_name,
                                 static_cast<unsigned long long>(c.skipped),
                                 static_cast<unsigned long long>(c.cases));
                    std::fclose(f);
                }
            }
        });
        return true;
    }();
    (void)registered;
    return c;
}

// Per-process counters of fuzz_wire_validator's failed-build arm (tasks.md T062a:
// the validator's pre-scan of a view whose build failed). Every failed-build view
// is checked, none is skipped; the buckets show which of the pre-scan's two exits
// the run reached. `fell_through` counts views whose walk under the validator's
// hooks recorded no fault; `resource_status` is the subset of those whose build
// status is a resource failure.
struct prescan_counter {
    std::uint64_t entered = 0;
    std::uint64_t faulted_malformed_tag = 0;
    std::uint64_t faulted_length_data = 0;
    std::uint64_t fell_through = 0;
    std::uint64_t resource_status = 0;
};

// Reported at exit like counter() above, under its own arm name.
inline prescan_counter& failed_build_prescan_counter() {
    static prescan_counter c{};
    static bool const registered = [] {
        std::atexit([] {
            std::fprintf(stderr,
                         "[092 fuzz arm T062a validate pre-scan on a failed build] entered %llu, "
                         "faulted malformed_tag %llu, faulted length_data_mismatch %llu, "
                         "fell through %llu (resource status %llu)\n",
                         static_cast<unsigned long long>(c.entered),
                         static_cast<unsigned long long>(c.faulted_malformed_tag),
                         static_cast<unsigned long long>(c.faulted_length_data),
                         static_cast<unsigned long long>(c.fell_through),
                         static_cast<unsigned long long>(c.resource_status));
            if (char const* path = std::getenv("FIXPP_FUZZ_092_COUNT_FILE")) {
                if (std::FILE* f = std::fopen(path, "a")) {
                    std::fprintf(f,
                                 "T062a entered=%llu malformed_tag=%llu length_data=%llu "
                                 "fell_through=%llu resource=%llu\n",
                                 static_cast<unsigned long long>(c.entered),
                                 static_cast<unsigned long long>(c.faulted_malformed_tag),
                                 static_cast<unsigned long long>(c.faulted_length_data),
                                 static_cast<unsigned long long>(c.fell_through),
                                 static_cast<unsigned long long>(c.resource_status));
                    std::fclose(f);
                }
            }
        });
        return true;
    }();
    (void)registered;
    return c;
}

}  // namespace fixpp::fuzz092
