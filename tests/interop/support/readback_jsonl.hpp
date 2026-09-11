// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/readback_jsonl.hpp — 089 T018.
//
// fixpp's own half of the readback JSONL stream: the WITNESS PRODUCER
// (specs/089-quickfix-interop-conversation/data-model.md §4). fixpp is the
// THIRD of three independent emitters this record format binds
// (contracts/readback-jsonl.md § "THE THREE EMITTERS" — the QuickFIX-cpp
// counterparty, the QuickFIX-J counterparty, and fixpp), and C-7 requires
// fixpp's `sent`/`readback`/`terminal` rendering to be BYTE-IDENTICAL to the
// two counterparties' (data-model.md §2/§3/§12 carry no per-side variance).
// fixpp's own `hello` is the ONE record that legitimately differs — it
// follows data-model.md §1a, not §1 — see Stream::hello() below.
//
// Written independently from the contract text, NOT copied from either
// counterparty implementation
// (phase-9-harness/quickfix-cpp/counterparty/readback_jsonl.hpp,
// phase-9-harness/quickfixj/.../ReadbackJsonl.java, both read-only from this
// submodule). FR-025's whole premise is that fixpp's emitter CAN silently
// diverge from the QuickFIX-cpp counterparty's despite sharing std::string
// (spec.md FR-025) — a byte-for-byte copy would make that guard vacuous by
// construction. Byte-identity for the shared record kinds is instead
// EARNED and MEASURED against the committed cross-language fixture by
// emit_fixpp_fixture.cpp in this directory (see that file's header for why
// it runs manually rather than under ctest).
//
// Standard-library only; no production fixpp dependency (mirrors
// golden_diff.hpp's own rule) and no JSON library, because none of the three
// emitters is allowed one (contracts/readback-jsonl.md § "Escaping and
// encoding" — Java has no Jackson/Gson/JSON-B, the C++ counterparty links
// only quickfix+OpenSSL+Threads, and fixpp's dependency graph carries no JSON
// library either).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace fixpp::interop::readback {

// ── escaping / encoding (contract § Escaping and encoding) ─────────────────

// Exactly the escapes the contract permits: `\"`, `\\`, and `\u00xx` (LOWER
// hex) for every byte < 0x20. `/` and every other byte >= 0x20 is emitted
// literally — `\/` and `\uXXXX` for non-ASCII are FORBIDDEN; byte
// compatibility (C-7) admits exactly one spelling.
inline std::string json_escape(std::string const& raw)
{
    static const char* kHexLower = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() + 8);
    for (unsigned char const byte : raw) {
        if (byte == '"') {
            out += "\\\"";
        } else if (byte == '\\') {
            out += "\\\\";
        } else if (byte < 0x20) {
            out += "\\u00";
            out += kHexLower[(byte >> 4) & 0x0F];
            out += kHexLower[byte & 0x0F];
        } else {
            out += static_cast<char>(byte);
        }
    }
    return out;
}

// Strict UTF-8 validator — the classifier that routes a value to `value` or
// to `value_b64`. Must reject overlong forms, surrogates and codepoints
// beyond U+10FFFF: a lax validator would let this emitter call a byte
// sequence valid that a counterparty calls invalid, and the two would then
// disagree about WHICH KEY to emit for identical bytes.
inline bool is_valid_utf8(std::string const& raw)
{
    auto const* p = reinterpret_cast<unsigned char const*>(raw.data());
    auto const* const end = p + raw.size();
    while (p < end) {
        unsigned char const lead = *p;
        if (lead < 0x80) {
            ++p;
            continue;
        }
        std::size_t extra = 0;
        std::uint32_t code = 0;
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            code = lead & 0x1FU;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            code = lead & 0x0FU;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            code = lead & 0x07U;
        } else {
            return false;  // continuation byte as lead, or 0xF8..0xFF
        }
        if (static_cast<std::size_t>(end - p) < extra + 1) {
            return false;
        }
        for (std::size_t i = 1; i <= extra; ++i) {
            unsigned char const cont = p[i];
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
            code = (code << 6) | (cont & 0x3FU);
        }
        if (extra == 1 && code < 0x80U) return false;           // overlong
        if (extra == 2 && code < 0x800U) return false;          // overlong
        if (extra == 3 && code < 0x10000U) return false;        // overlong
        if (code > 0x10FFFFU) return false;                     // out of range
        if (code >= 0xD800U && code <= 0xDFFFU) return false;   // surrogate
        p += extra + 1;
    }
    return true;
}

// RFC 4648 standard alphabet, `=` padding, no line breaks.
inline std::string base64_encode(std::string const& raw)
{
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((raw.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 2 < raw.size(); i += 3) {
        auto const a = static_cast<unsigned char>(raw[i]);
        auto const b = static_cast<unsigned char>(raw[i + 1]);
        auto const c = static_cast<unsigned char>(raw[i + 2]);
        out += kAlphabet[a >> 2];
        out += kAlphabet[((a & 0x03) << 4) | (b >> 4)];
        out += kAlphabet[((b & 0x0F) << 2) | (c >> 6)];
        out += kAlphabet[c & 0x3F];
    }
    if (i + 1 == raw.size()) {
        auto const a = static_cast<unsigned char>(raw[i]);
        out += kAlphabet[a >> 2];
        out += kAlphabet[(a & 0x03) << 4];
        out += "==";
    } else if (i + 2 == raw.size()) {
        auto const a = static_cast<unsigned char>(raw[i]);
        auto const b = static_cast<unsigned char>(raw[i + 1]);
        out += kAlphabet[a >> 2];
        out += kAlphabet[((a & 0x03) << 4) | (b >> 4)];
        out += kAlphabet[(b & 0x0F) << 2];
        out += '=';
    }
    return out;
}

// The contract's canonical rendering for a TYPED value (data-model.md §2,
// "Typed entry"): numeric FIX types take the shortest decimal spelling with
// no trailing fractional zeros; UTCTIMESTAMP is pinned to millisecond
// precision. This is what keeps the three emitters byte-identical (C-7); it
// is not formatting.
inline std::string canonical_typed_value(std::string const& fix_type, std::string const& raw)
{
    bool const numeric = fix_type == "PRICE" || fix_type == "QTY" || fix_type == "AMT"
                       || fix_type == "FLOAT" || fix_type == "PRICEOFFSET"
                       || fix_type == "PERCENTAGE";
    if (numeric && raw.find('.') != std::string::npos) {
        std::string trimmed = raw;
        while (!trimmed.empty() && trimmed.back() == '0') {
            trimmed.pop_back();
        }
        if (!trimmed.empty() && trimmed.back() == '.') {
            trimmed.pop_back();
        }
        return trimmed.empty() ? "0" : trimmed;
    }
    if (fix_type == "UTCTIMESTAMP") {
        std::size_t const dot = raw.find('.');
        if (dot == std::string::npos) {
            return raw + ".000";
        }
        std::string frac = raw.substr(dot + 1);
        frac.resize(3, '0');
        return raw.substr(0, dot) + "." + frac;
    }
    return raw;
}

// ── field entries and their canonical order ─────────────────────────────────

struct FieldEntry {
    std::string path;   // "40" | "453[0].448" | "453[1].802[0].523"
    std::string value;  // RAW BYTES as parsed; classification (value / value_b64)
                         // happens at write time
};

struct TypedEntry {
    std::string path;
    std::string fix_type;  // resolved from the dictionary loaded for this cell
    std::string value;     // canonical_typed_value()-rendered
};

// Parse a path into its integer tuple: "453[1].802[0].523" -> {453,1,802,0,523}.
// Sorting by this tuple — element-wise numerically, a shorter tuple sorting
// before a longer one sharing its prefix — is what removes the ENGINE'S WALK
// ORDER from the record (contract § "Canonical form"). Group instance order
// survives automatically: the instance index is INSIDE the path.
inline std::vector<long long> path_tuple(std::string const& path)
{
    std::vector<long long> parts;
    long long current = 0;
    bool in_number = false;
    for (char const ch : path) {
        if (ch >= '0' && ch <= '9') {
            current = current * 10 + (ch - '0');
            in_number = true;
        } else {
            if (in_number) {
                parts.push_back(current);
            }
            current = 0;
            in_number = false;
        }
    }
    if (in_number) {
        parts.push_back(current);
    }
    return parts;
}

inline bool path_less(std::string const& lhs, std::string const& rhs)
{
    std::vector<long long> const a = path_tuple(lhs);
    std::vector<long long> const b = path_tuple(rhs);
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

// ── the stream writer ───────────────────────────────────────────────────────

// One writer per PROCESS, one file per writer — TRUNCATE, never append
// (contract § Transport: an appended file accumulates stale records across
// runs, and a stale record is worse than a missing one). Flushed per line so
// a killed process still yields the records it had emitted.
class Stream {
public:
    explicit Stream(std::string const& path) : out_(path, std::ios::binary | std::ios::trunc) {}

    [[nodiscard]] bool ok() const
    {
        return static_cast<bool>(out_);
    }

    // data-model.md §1a — fixpp's own hello, NOT §1's. fixpp carries "the
    // fields of §1 that apply to it (type, run_id, cell_id, config,
    // script_digest, arm) plus the two that exist only here" (has_validator,
    // dictionary_digest); that parenthetical IS the key order used below.
    // fixpp deliberately OMITS engine / engine_version / readback_protocol /
    // dictionary_enabled / counterparty_digest / typed_accessor_arm — those
    // describe a COUNTERPARTY build, and fixpp is not one. This is the one
    // record kind where fixpp's bytes are NOT expected to match either
    // counterparty's hello (see emit_fixpp_fixture.cpp's header).
    void hello(std::string const& run_id, std::string const& cell_id, std::string const& config,
               std::string const& script_digest, std::string const& arm, bool has_validator,
               std::string const& dictionary_digest)
    {
        std::string line = "{\"type\":\"hello\"";
        line += ",\"run_id\":\"" + json_escape(run_id) + "\"";
        line += ",\"cell_id\":\"" + json_escape(cell_id) + "\"";
        line += ",\"config\":\"" + json_escape(config) + "\"";
        line += ",\"script_digest\":\"" + json_escape(script_digest) + "\"";
        line += ",\"arm\":\"" + json_escape(arm) + "\"";
        line += ",\"has_validator\":";
        line += has_validator ? "true" : "false";
        line += ",\"dictionary_digest\":\"" + json_escape(dictionary_digest) + "\"";
        line += "}";
        write_line(line);
    }

    // data-model.md §3 / contract § Records. `fields` MUST come from BUILDER
    // INPUTS (C-8), never re-read from the serialized frame.
    void sent(std::string const& msg_type, long long seq_num, std::string const& direction,
              long long occurrence, std::string const& script_step_id,
              std::vector<FieldEntry> fields)
    {
        std::string line = "{\"type\":\"sent\"";
        line += ",\"msg_type\":\"" + json_escape(msg_type) + "\"";
        line += ",\"seq_num\":" + std::to_string(seq_num);
        line += ",\"direction\":\"" + json_escape(direction) + "\"";
        line += ",\"occurrence\":" + std::to_string(occurrence);
        line += ",\"script_step_id\":\"" + json_escape(script_step_id) + "\"";
        line += ",\"fields\":" + render_fields(std::move(fields));
        line += "}";
        write_line(line);
        ++sent_count_;
    }

    // data-model.md §2. Carries NO script_step_id — nothing puts a step
    // identifier on the wire; it is inherited from the paired `sent` record
    // after correlation (comparator's job, not this writer's).
    void readback(std::string const& msg_type, long long seq_num, std::string const& direction,
                  long long occurrence, bool poss_dup, std::vector<FieldEntry> fields,
                  std::vector<TypedEntry> typed_reads)
    {
        std::string line = "{\"type\":\"readback\"";
        line += ",\"msg_type\":\"" + json_escape(msg_type) + "\"";
        line += ",\"seq_num\":" + std::to_string(seq_num);
        line += ",\"direction\":\"" + json_escape(direction) + "\"";
        line += ",\"occurrence\":" + std::to_string(occurrence);
        line += ",\"poss_dup\":";
        line += poss_dup ? "true" : "false";
        line += ",\"fields\":" + render_fields(std::move(fields));
        line += ",\"typed_reads\":" + render_typed(std::move(typed_reads));
        line += "}";
        write_line(line);
        ++readback_count_;
    }

    // data-model.md §12 — written LAST, whatever the outcome, exactly once
    // (the first disposition is the true one; a graceful-then-forced double
    // call must not overwrite it).
    void terminal(std::string const& terminal_state, std::string const& run_id,
                  std::string const& cell_id, std::string const& config,
                  std::string const& script_digest)
    {
        if (terminal_written_) {
            return;
        }
        terminal_written_ = true;
        std::string line = "{\"type\":\"terminal\"";
        line += ",\"run_id\":\"" + json_escape(run_id) + "\"";
        line += ",\"cell_id\":\"" + json_escape(cell_id) + "\"";
        line += ",\"config\":\"" + json_escape(config) + "\"";
        line += ",\"script_digest\":\"" + json_escape(script_digest) + "\"";
        line += ",\"terminal_state\":\"" + json_escape(terminal_state) + "\"";
        line += ",\"sent_count\":" + std::to_string(sent_count_);
        line += ",\"readback_count\":" + std::to_string(readback_count_);
        line += "}";
        write_line(line);
    }

private:
    // Exactly one of `value` / `value_b64` per entry; the choice is made on
    // the RAW BYTES (contract § "C-11 — the live-path charset arm", item 1 —
    // classifying on a decoded string rather than the re-encoded bytes is
    // one of the two ways this rule goes vacuous).
    static std::string render_value(std::string const& raw)
    {
        if (is_valid_utf8(raw)) {
            return "\"value\":\"" + json_escape(raw) + "\"";
        }
        return "\"value_b64\":\"" + base64_encode(raw) + "\"";
    }

    static std::string render_fields(std::vector<FieldEntry> fields)
    {
        std::sort(fields.begin(), fields.end(), [](FieldEntry const& a, FieldEntry const& b) {
            return path_less(a.path, b.path);
        });
        std::string out = "[";
        bool first = true;
        for (FieldEntry const& entry : fields) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += "{\"path\":\"" + json_escape(entry.path) + "\"," + render_value(entry.value) + "}";
        }
        out += "]";
        return out;
    }

    static std::string render_typed(std::vector<TypedEntry> entries)
    {
        std::sort(entries.begin(), entries.end(), [](TypedEntry const& a, TypedEntry const& b) {
            return path_less(a.path, b.path);
        });
        std::string out = "[";
        bool first = true;
        for (TypedEntry const& entry : entries) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += "{\"path\":\"" + json_escape(entry.path) + "\"";
            out += ",\"fix_type\":\"" + json_escape(entry.fix_type) + "\"";
            out += "," + render_value(entry.value) + "}";
        }
        out += "]";
        return out;
    }

    void write_line(std::string const& line)
    {
        std::lock_guard<std::mutex> const guard(mutex_);
        out_ << line << '\n';
        out_.flush();
    }

    std::ofstream out_;
    std::mutex mutex_;
    long long sent_count_ = 0;
    long long readback_count_ = 0;
    bool terminal_written_ = false;
};

}  // namespace fixpp::interop::readback
