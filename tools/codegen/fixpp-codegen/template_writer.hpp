// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/template_writer.hpp
//
// 003-dictionary-codegen — T013. Deterministic, locale-independent
// string-templating layer. Underpins every emitter and the US5 determinism
// guarantee (NFR-003-7 / AC-T1): LF-only line endings, no locale-dependent
// formatting, sorted/bytewise-stable emission (the same invariant 002 uses
// for Dictionary storage — research.md D-6 / D-4). Build-only host tool
// ([const §III.5]); never linked into the user-facing library.
#pragma once
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace fixpp::codegen {

// Append-only byte buffer with locale-free helpers. Determinism rules:
//  * only '\n' is ever written (never '\r'); callers must not embed '\r';
//  * integers via std::to_chars (locale-independent);
//  * the buffer is never sorted post-hoc — callers feed already-sorted input
//    (Dictionary::messages() is bytewise-sorted per 002 D-6), so emission is
//    byte-stable across runs and machines without a post-emit sort (A4).
class TemplateWriter {
public:
    TemplateWriter& raw(std::string_view s) {
        buf_.append(s);
        return *this;
    }

    TemplateWriter& line(std::string_view s = {}) {
        buf_.append(s);
        buf_.push_back('\n');
        return *this;
    }

    TemplateWriter& num(std::uint64_t v) {
        char tmp[20];
        auto [p, ec] = std::to_chars(tmp, tmp + sizeof tmp, v);
        buf_.append(tmp, p);
        return *this;
    }

    [[nodiscard]] std::string_view str() const noexcept { return buf_; }
    [[nodiscard]] std::string&& take() noexcept { return std::move(buf_); }

private:
    std::string buf_;
};

}  // namespace fixpp::codegen
