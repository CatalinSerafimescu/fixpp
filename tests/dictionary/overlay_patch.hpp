#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/overlay_patch.hpp — the 098 overlay witnesses' line-based patch
// helper (research R-10, Vendored-base patches), shared by W-1/W-2
// (overlay_equivalence_test.cpp) and W-5 (tests/session/
// test_098_dialect_overlay_inbound.cpp). Test-only, header-only, with no gtest and
// no library dependency, so either binary can include it.
//
// The helper is line-based, not XML-aware: a hunk's context block must match
// exactly once in the unpatched base, its `-`/`+` pairs must have one of the two
// allowed rewrite shapes, and its inserted run lands immediately before the block's
// last line. Lines compare with one trailing CR stripped; the output is written
// with LF.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace overlay_test {

// Lines of a text, one trailing CR stripped from each (research R-10, line endings).
[[nodiscard]] inline std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t const nl = text.find('\n', pos);
        std::size_t const end = nl == std::string_view::npos ? text.size() : nl;
        std::string line{text.substr(pos, end - pos)};
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        out.push_back(std::move(line));
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
    }
    return out;
}

[[nodiscard]] inline std::string join_lines(std::vector<std::string> const& lines) {
    std::string out;
    for (auto const& l : lines) {
        out += l;
        out += '\n';
    }
    return out;
}

// The indices of every line equal to `anchor`.
[[nodiscard]] inline std::vector<std::size_t> find_line(std::vector<std::string> const& lines,
                                                        std::string_view anchor) {
    std::vector<std::size_t> hits;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] == anchor) {
            hits.push_back(i);
        }
    }
    return hits;
}

struct hunk {
    std::string label;
    std::vector<std::string> context;                           // as in the base
    std::vector<std::pair<std::size_t, std::string>> rewrites;  // context index → new line
    std::vector<std::string> inserted;                          // before the last context line
};

struct parsed_patch {
    std::vector<hunk> hunks;
    std::string error;  // empty when the patch is well-formed
};

// The Required promotion shape (research R-10, Two in-place rewrites): a reference
// line whose only change is its `required` value set to Y, or `required` added as
// required='Y' in the line's own quote style right after one of its attributes.
inline bool is_promotion(std::string const& old_line, std::string const& new_line) {
    std::size_t lead = old_line.find_first_not_of(' ');
    if (lead == std::string::npos) {
        return false;
    }
    std::string_view const body = std::string_view{old_line}.substr(lead);
    if (!body.starts_with("<field ") && !body.starts_with("<component ") &&
        !body.starts_with("<group ")) {
        return false;
    }
    for (char const q : {'\'', '"'}) {
        std::string const key = std::string{" required="} + q;
        std::size_t const pos = old_line.find(key);
        if (pos == std::string::npos) {
            continue;
        }
        std::size_t const vb = pos + key.size();
        std::size_t const ve = old_line.find(q, vb);
        if (ve == std::string::npos || old_line.substr(vb, ve - vb) == "Y") {
            return false;
        }
        return new_line == old_line.substr(0, vb) + "Y" + old_line.substr(ve);
    }
    std::size_t const q_at = old_line.find_first_of("'\"");
    if (q_at == std::string::npos) {
        return false;
    }
    char const q = old_line[q_at];
    std::string const added = std::string{" required="} + q + "Y" + q;
    for (std::size_t p = 1; p <= old_line.size(); ++p) {
        if (old_line[p - 1] == q &&
            new_line == old_line.substr(0, p) + added + old_line.substr(p)) {
            return true;
        }
    }
    return false;
}

// The self-closing opener shape: `<x …/>` becomes `<x …>`.
inline bool is_opener(std::string const& old_line, std::string const& new_line) {
    if (!old_line.ends_with("/>") || new_line.ends_with("/>") || !new_line.ends_with(">")) {
        return false;
    }
    std::string head = old_line.substr(0, old_line.size() - 2);
    while (!head.empty() && head.back() == ' ') {
        head.pop_back();
    }
    return !head.empty() && new_line == head + ">";
}

inline parsed_patch parse_patch(std::string_view text) {
    parsed_patch out;
    auto const lines = lines_of(text);
    hunk* h = nullptr;
    bool after_insert = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string const& l = lines[i];
        if (l.starts_with("@@")) {
            out.hunks.push_back({});
            h = &out.hunks.back();
            h->label = l.size() > 3 ? l.substr(3) : std::string{};
            after_insert = false;
            continue;
        }
        if (h == nullptr || l.empty()) {
            out.error = "line " + std::to_string(i + 1) + " is outside a hunk or empty";
            return out;
        }
        std::string const rest = l.substr(1);
        if (l[0] == ' ') {
            if (after_insert && !h->inserted.empty()) {
                // Only the hunk's last row may follow the inserted run.
                if (i + 1 < lines.size() && !lines[i + 1].starts_with("@@")) {
                    out.error = "hunk '" + h->label +
                                "': a context line after the inserted run "
                                "is not the hunk's last line";
                    return out;
                }
            }
            h->context.push_back(rest);
        } else if (l[0] == '-') {
            if (after_insert || i + 1 >= lines.size() || lines[i + 1].empty() ||
                lines[i + 1][0] != '+') {
                out.error = "hunk '" + h->label + "': a '-' line not followed at once by its '+'";
                return out;
            }
            std::string const repl = lines[i + 1].substr(1);
            if (!is_promotion(rest, repl) && !is_opener(rest, repl)) {
                out.error = "hunk '" + h->label + "': the rewrite '" + rest + "' -> '" + repl +
                            "' is neither a Required promotion nor a self-closing opener";
                return out;
            }
            h->context.push_back(rest);
            h->rewrites.emplace_back(h->context.size() - 1, repl);
            ++i;
        } else if (l[0] == '+') {
            after_insert = true;
            h->inserted.push_back(rest);
        } else {
            out.error = "line " + std::to_string(i + 1) + " has no ' ', '-', '+' or '@@' prefix";
            return out;
        }
    }
    for (auto const& hk : out.hunks) {
        if (hk.context.empty()) {
            out.error = "hunk '" + hk.label + "' has no context";
            return out;
        }
    }
    return out;
}

struct applied {
    std::string text;
    std::string error;  // empty when every hunk applied
};

inline applied apply_patch(std::string_view base, std::vector<hunk> const& hunks) {
    std::vector<std::string> const lines = lines_of(base);
    // Each hunk is located in the unpatched base; hunks are independent, so their
    // context blocks may overlap. Refused, because the result would depend on hunk
    // order: two hunks rewriting one line, a line one hunk rewrites that another
    // hunk's context names, and two hunks inserting at one position.
    struct located {
        hunk const* h;
        std::size_t at;
    };
    std::vector<located> found;
    for (auto const& h : hunks) {
        std::vector<std::size_t> at;
        for (std::size_t i = 0; i + h.context.size() <= lines.size(); ++i) {
            bool same = true;
            for (std::size_t k = 0; k < h.context.size() && same; ++k) {
                same = lines[i + k] == h.context[k];
            }
            if (same) {
                at.push_back(i);
            }
        }
        if (at.size() != 1) {
            return {.text = {},
                    .error = "hunk '" + h.label + "': its context block matches " +
                             std::to_string(at.size()) + " places, not exactly one"};
        }
        found.push_back({.h = &h, .at = at.front()});
    }
    std::vector<std::optional<std::string>> rewrite(lines.size());
    std::vector<hunk const*> rewriter(lines.size(), nullptr);
    std::vector<std::vector<std::string>> before(lines.size());
    std::vector<hunk const*> inserter(lines.size(), nullptr);
    for (auto const& [h, at] : found) {
        for (auto const& [k, repl] : h->rewrites) {
            if (rewriter[at + k] != nullptr) {
                return {.text = {},
                        .error = "hunks '" + rewriter[at + k]->label + "' and '" + h->label +
                                 "' rewrite one line"};
            }
            rewriter[at + k] = h;
            rewrite[at + k] = repl;
        }
        if (!h->inserted.empty()) {
            std::size_t const pos = at + h->context.size() - 1;
            if (inserter[pos] != nullptr) {
                return {.text = {},
                        .error = "hunks '" + inserter[pos]->label + "' and '" + h->label +
                                 "' insert at one position"};
            }
            inserter[pos] = h;
            before[pos] = h->inserted;
        }
    }
    for (auto const& [h, at] : found) {
        for (std::size_t k = 0; k < h->context.size(); ++k) {
            hunk const* r = rewriter[at + k];
            if (r != nullptr && r != h) {
                return {.text = {},
                        .error = "hunk '" + r->label + "' rewrites a line the context of hunk '" +
                                 h->label + "' names"};
            }
        }
    }
    std::vector<std::string> out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out.insert(out.end(), before[i].begin(), before[i].end());
        out.push_back(rewrite[i].value_or(lines[i]));
    }
    return {.text = join_lines(out), .error = {}};
}

}  // namespace overlay_test
