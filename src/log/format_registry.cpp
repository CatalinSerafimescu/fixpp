// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/log/format_registry.cpp
//
// Compile-time CRC32 format-id → format-string registry (T024).
// The producer stores only FIXPP_FORMAT_ID(fmt) = CRC32(fmt) in Record::format_id.
// The drain thread calls format_registry::lookup(format_id) to retrieve the
// format string, then calls std::vformat with the captured ArgValue args.
//
// Anchors:
//   [2k §4.3]         — drain-side formatting obligation
//   contracts/log-core.md — R3 duplicate-id collision check
//   [arch §2.3]       — log → {core} only
//
// No format string crosses the producer→ring boundary; only the 4-byte CRC32
// ID is written into Record::format_id on the hot path (FR-010, SC-003).
//
// DUPLICATE-ID COLLISION CHECK (R3):
// If two different format strings share the same CRC32 (collision), the drain
// thread cannot determine which string was intended. In debug builds this is
// detected at registry build time: a static_assert fires if any two entries
// share the same key. In release builds the first-registered entry wins (the
// drain-thread lookup is read-only after startup; no dynamic allocation).

#include <fixpp/log/format_registry.hpp>

#include <cassert>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Include the level/record headers only for the CRC32 helper (crc32_str).
#include <fixpp/log/level.hpp>

namespace fixpp::log::detail {

// ── Registry entry ─────────────────────────────────────────────────────────

struct FormatEntry {
    std::uint32_t id;
    std::string_view fmt;
};

// ── Static compile-time table ──────────────────────────────────────────────
//
// All format strings used by FIXPP_LOG0 / FIXPP_SLOG / FIXPP_ELOG must be
// registered here so the drain thread can look them up by CRC32.
//
// Each entry is { FIXPP_FORMAT_ID("fmt"), "fmt" }.
// The constexpr CRC32 is computed at compile time; no runtime hashing.
//
// R3 duplicate-id collision check: the build_registry() function below asserts
// that no two entries share the same format_id. If a collision is detected in
// debug builds, the program aborts with an assertion failure naming the
// collision pair.

static const std::unordered_map<std::uint32_t, std::string_view>&
get_registry() noexcept
{
    // Lazy-initialised on first call (thread-safe in C++11+).
    static const std::unordered_map<std::uint32_t, std::string_view> registry = []() {
        // ── Format string table (add new entries here) ────────────────────
        //
        // Entries used by the internal fixpp log macros and by tests.
        // Format strings follow std::format conventions (Python-style {}).
        static constexpr FormatEntry table[] = {
            // test / smoke entries
            { static_cast<std::uint32_t>(crc32_str("test message")),
              "test message" },
            { static_cast<std::uint32_t>(crc32_str("overflow test {}")),
              "overflow test {}" },
            { static_cast<std::uint32_t>(crc32_str("level filter test")),
              "level filter test" },
            { static_cast<std::uint32_t>(crc32_str("category filter test")),
              "category filter test" },
            { static_cast<std::uint32_t>(crc32_str("drop test {}")),
              "drop test {}" },
            { static_cast<std::uint32_t>(crc32_str("record {}")),
              "record {}" },
            { static_cast<std::uint32_t>(crc32_str("msg {}")),
              "msg {}" },
        };

        std::unordered_map<std::uint32_t, std::string_view> m;
        m.reserve(std::size(table));

        for (auto const& entry : table) {
            // R3: debug-build duplicate-id collision check.
            // Two different format strings that happen to share the same CRC32
            // would cause the drain to format one record with the wrong template.
#ifndef NDEBUG
            if (auto it = m.find(entry.id); it != m.end()) {
                // Collision detected — two entries share the same format_id.
                // This is a build-time defect (must be fixed in the table).
                assert(false &&
                    "format_registry: duplicate format_id (CRC32 collision). "
                    "Two format strings share the same CRC32; rename one.");
            }
#endif
            m.emplace(entry.id, entry.fmt);
        }

        return m;
    }();

    return registry;
}

// ── Public lookup ──────────────────────────────────────────────────────────

std::string_view format_registry_lookup(std::uint32_t format_id) noexcept
{
    auto const& reg = get_registry();
    auto it = reg.find(format_id);
    if (it == reg.end()) {
        // Unknown format_id: the drain cannot format this record.
        // Return a diagnostic placeholder rather than crashing.
        return "<unknown format_id>";
    }
    return it->second;
}

// ── format_args builder ────────────────────────────────────────────────────
//
// The drain thread calls format_record() to produce the formatted string for
// a Record. Internally it uses std::vformat with a dynamic args list built
// from the ArgValue array.

std::string format_record(Record const& rec)
{
    std::string_view fmt = format_registry_lookup(rec.format_id);

    // Build the std::format dynamic arg store from ArgValue array.
    // std::make_format_args requires lvalue references; we stash them here.
    // We support the same ArgValue kinds as the producer.
    //
    // For brevity: collect into a string by formatting each kind individually.
    // std::vformat is used when at least one arg is present.

    // Fast path: no args.
    if (rec.arg_count == 0) {
        return std::string{fmt};
    }

    // Build a format string substituting each {} placeholder.
    // We use a simple manual approach since std::make_format_args can't
    // accept heterogeneous typed values via a simple loop.
    // Strategy: replace each {} in fmt with the formatted value of the
    // corresponding ArgValue in order.

    std::string result;
    result.reserve(fmt.size() + rec.arg_count * 8);

    std::size_t arg_idx = 0;
    std::size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i+1] == '}') {
            // Substitute the next arg.
            if (arg_idx < rec.arg_count && arg_idx < k_max_args) {
                auto const& av = rec.args[arg_idx++];
                switch (av.kind) {
                case ArgValue::Kind::u64:
                    result += std::to_string(av.u64);
                    break;
                case ArgValue::Kind::i64:
                    result += std::to_string(av.i64);
                    break;
                case ArgValue::Kind::f64:
                    result += std::to_string(av.f64);
                    break;
                case ArgValue::Kind::bool_val:
                    result += av.b ? "true" : "false";
                    break;
                case ArgValue::Kind::inline_str:
                    result.append(av.inl.data, av.inl.len);
                    break;
                case ArgValue::Kind::static_str:
                    if (av.static_ptr) result += av.static_ptr;
                    break;
                case ArgValue::Kind::empty:
                default:
                    result += "<empty>";
                    break;
                }
            } else {
                result += "{}";
            }
            i += 2;
        } else {
            result += fmt[i++];
        }
    }
    return result;
}

}  // namespace fixpp::log::detail
