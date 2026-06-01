// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/golden_diff.cpp
//
// Golden FIX byte-stream transcript parsing and normalization helpers for the
// interop harness. Standard-library only; no production fixpp dependencies.

#include "golden_diff.hpp"

#include <algorithm>
#include <utility>

namespace fixpp::interop {
namespace {

constexpr std::byte soh{0x01};

struct Field {
    int tag = 0;
    std::vector<std::byte> value;
};

struct ParsedFrame {
    std::vector<Field> fields;
    std::string error;
};

DiffResult mismatch(char dir, std::size_t frame_index, std::string tag_or_structure)
{
    return {
        DiffStatus::mismatch,
        "mismatch:" + std::string(1, dir) + ":" + std::to_string(frame_index) + ":" + tag_or_structure
    };
}

bool is_digit(std::byte value)
{
    const int digit = std::to_integer<int>(value);
    return digit >= '0' && digit <= '9';
}

int parse_tag(std::span<const std::byte> bytes, bool& ok)
{
    ok = !bytes.empty();
    int tag = 0;

    for (const std::byte value : bytes) {
        if (!is_digit(value)) {
            ok = false;
            return 0;
        }

        tag = (tag * 10) + (std::to_integer<int>(value) - '0');
    }

    return tag;
}

ParsedFrame split_fields(std::span<const std::byte> bytes)
{
    ParsedFrame parsed;
    std::size_t field_begin = 0;

    if (bytes.empty()) {
        parsed.error = "missing-field";
        return parsed;
    }

    while (field_begin < bytes.size()) {
        const auto field_end_it = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(field_begin),
                                           bytes.end(),
                                           soh);
        if (field_end_it == bytes.end()) {
            parsed.error = "missing-field";
            return parsed;
        }

        const auto field_end = static_cast<std::size_t>(field_end_it - bytes.begin());
        if (field_end == field_begin) {
            parsed.error = "extra-field";
            return parsed;
        }

        const auto field_begin_it = bytes.begin() + static_cast<std::ptrdiff_t>(field_begin);
        const auto equal_it = std::find(field_begin_it,
                                        field_end_it,
                                        static_cast<std::byte>(static_cast<unsigned char>('=')));
        if (equal_it == field_end_it || equal_it == field_begin_it) {
            parsed.error = "missing-field";
            return parsed;
        }

        bool tag_ok = false;
        const int tag = parse_tag(std::span<const std::byte>{field_begin_it, equal_it}, tag_ok);
        if (!tag_ok) {
            parsed.error = "missing-field";
            return parsed;
        }

        parsed.fields.push_back(Field{
            tag,
            std::vector<std::byte>{equal_it + 1, field_end_it}
        });
        field_begin = field_end + 1;
    }

    return parsed;
}

std::vector<Field> normalized_fields(std::span<const std::byte> bytes,
                                     const std::set<int>& excluded_tags,
                                     std::string& error)
{
    ParsedFrame parsed = split_fields(bytes);
    if (!parsed.error.empty()) {
        error = parsed.error;
        return {};
    }

    std::vector<Field> normalized;
    for (auto& field : parsed.fields) {
        if (!excluded_tags.contains(field.tag)) {
            normalized.push_back(std::move(field));
        }
    }

    return normalized;
}

bool values_equal(const std::vector<std::byte>& lhs, const std::vector<std::byte>& rhs)
{
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

std::vector<std::byte> decode_frame_bytes(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index + 3 < text.size()
            && text[index] == '\\'
            && text[index + 1] == 'x'
            && text[index + 2] == '0'
            && text[index + 3] == '1') {
            bytes.push_back(soh);
            index += 3;
            continue;
        }

        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(text[index])));
    }

    return bytes;
}

bool is_valid_dir(char dir)
{
    return dir == '>' || dir == '<';
}

}  // namespace

std::vector<GoldenFrame> parse_golden(std::string_view text)
{
    std::vector<GoldenFrame> frames;
    std::size_t line_begin = 0;

    while (line_begin <= text.size()) {
        std::size_t line_end = text.find('\n', line_begin);
        if (line_end == std::string_view::npos) {
            line_end = text.size();
        }

        std::string_view line = text.substr(line_begin, line_end - line_begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!line.empty() && line.size() >= 2 && is_valid_dir(line[0]) && line[1] == ' ') {
            frames.push_back(GoldenFrame{
                line[0],
                decode_frame_bytes(line.substr(2))
            });
        } else if (!line.empty()) {
            frames.push_back(GoldenFrame{
                '?',
                decode_frame_bytes(line)
            });
        }

        if (line_end == text.size()) {
            break;
        }
        line_begin = line_end + 1;
    }

    return frames;
}

DiffResult diff_transcripts(std::span<const GoldenFrame> expected,
                            std::span<const GoldenFrame> actual,
                            const std::set<int>& excluded_tags)
{
    if (expected.size() != actual.size()) {
        const std::size_t frame_index = std::min(expected.size(), actual.size());
        char dir = '>';
        if (frame_index < expected.size()) {
            dir = expected[frame_index].dir;
        } else if (frame_index < actual.size()) {
            dir = actual[frame_index].dir;
        }
        return mismatch(is_valid_dir(dir) ? dir : '?', frame_index, "count");
    }

    for (std::size_t frame_index = 0; frame_index < expected.size(); ++frame_index) {
        const GoldenFrame& expected_frame = expected[frame_index];
        const GoldenFrame& actual_frame = actual[frame_index];

        if (!is_valid_dir(expected_frame.dir)) {
            return mismatch('?', frame_index, "direction");
        }
        if (!is_valid_dir(actual_frame.dir)) {
            return mismatch(expected_frame.dir, frame_index, "direction");
        }
        if (expected_frame.dir != actual_frame.dir) {
            return mismatch(expected_frame.dir, frame_index, "direction");
        }

        std::string expected_error;
        std::string actual_error;
        const std::vector<Field> expected_fields = normalized_fields(expected_frame.bytes,
                                                                     excluded_tags,
                                                                     expected_error);
        const std::vector<Field> actual_fields = normalized_fields(actual_frame.bytes,
                                                                   excluded_tags,
                                                                   actual_error);
        if (!expected_error.empty()) {
            return mismatch(expected_frame.dir, frame_index, expected_error);
        }
        if (!actual_error.empty()) {
            return mismatch(expected_frame.dir, frame_index, actual_error);
        }

        const std::size_t common_count = std::min(expected_fields.size(), actual_fields.size());
        for (std::size_t field_index = 0; field_index < common_count; ++field_index) {
            const Field& expected_field = expected_fields[field_index];
            const Field& actual_field = actual_fields[field_index];
            if (expected_field.tag != actual_field.tag) {
                return mismatch(expected_frame.dir, frame_index, std::to_string(expected_field.tag));
            }
            if (!values_equal(expected_field.value, actual_field.value)) {
                return mismatch(expected_frame.dir, frame_index, std::to_string(expected_field.tag));
            }
        }

        if (expected_fields.size() < actual_fields.size()) {
            return mismatch(expected_frame.dir, frame_index, "extra-field");
        }
        if (expected_fields.size() > actual_fields.size()) {
            return mismatch(expected_frame.dir, frame_index, "missing-field");
        }
    }

    return {};
}

}  // namespace fixpp::interop
