// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/golden_diff.hpp
//
// Golden FIX byte-stream transcript parsing and normalization helpers for the
// interop harness. Standard-library only; no production fixpp dependencies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fixpp::interop {

struct GoldenFrame {
    char dir = '>';
    std::vector<std::byte> bytes;
};

enum class DiffStatus : std::uint8_t { match, mismatch };

struct DiffResult {
    DiffStatus status = DiffStatus::match;
    std::string detail;

    explicit operator bool() const
    {
        return status == DiffStatus::match;
    }
};

inline const std::set<int>& default_normalization_tags()
{
    static const std::set<int> tags{9, 10, 34, 52, 60, 112, 122};
    return tags;
}

std::vector<GoldenFrame> parse_golden(std::string_view text);

DiffResult diff_transcripts(std::span<const GoldenFrame> expected,
                            std::span<const GoldenFrame> actual,
                            const std::set<int>& excluded_tags = default_normalization_tags());

}  // namespace fixpp::interop
