// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/witness_comparator.cpp — 089 T018.
//
// The reader here is a minimal hand-rolled decoder for THIS repo's own
// readback-JSONL grammar (contracts/readback-jsonl.md), not a general JSON
// library — fixpp's dependency graph carries none, and the comparator has no
// reason to pull one in for a schema it fully controls on both ends
// (readback_jsonl.hpp's writer, here, is the only producer this consumer
// needs to trust — plus the two counterparty writers, which the contract
// binds to the identical grammar).
#include "witness_comparator.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace fixpp::interop::readback {
namespace {

// ---- minimal JSON-line reader (this schema only) --------------------------

class LineReader {
public:
    explicit LineReader(std::string const& text) : s_(text) {}

    void skip_ws()
    {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t')) {
            ++i_;
        }
    }

    [[nodiscard]] bool eof() const
    {
        return i_ >= s_.size();
    }

    [[nodiscard]] char peek() const
    {
        return i_ < s_.size() ? s_[i_] : '\0';
    }

    bool consume(char c)
    {
        skip_ws();
        if (i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }

    // Decodes a JSON string literal (the cursor must be AT the opening `"`).
    // Handles `\"`, `\\`, `\/`, `\uXXXX` (BMP; surrogate pairs combined when
    // present) — a superset of what readback_jsonl.hpp's writer ever emits
    // (it only ever writes `\"`, `\\`, `\u00xx`), for robustness against the
    // two counterparty writers, which implement the same contract clause
    // independently.
    // Not [[nodiscard]]: callers that only care about a value's presence
    // (skip_value, the generic-key loop in parse_stream()) legitimately
    // discard the decoded text.
    bool parse_string(std::string& out)
    {
        skip_ws();
        if (i_ >= s_.size() || s_[i_] != '"') {
            return false;
        }
        ++i_;
        out.clear();
        while (i_ < s_.size() && s_[i_] != '"') {
            char const c = s_[i_];
            if (c == '\\' && i_ + 1 < s_.size()) {
                char const esc = s_[i_ + 1];
                if (esc == '"') {
                    out += '"';
                    i_ += 2;
                } else if (esc == '\\') {
                    out += '\\';
                    i_ += 2;
                } else if (esc == '/') {
                    out += '/';
                    i_ += 2;
                } else if (esc == 'u' && i_ + 5 < s_.size()) {
                    std::uint32_t code = 0;
                    for (int k = 0; k < 4; ++k) {
                        code = code * 16 + hex_digit(s_[i_ + 2 + k]);
                    }
                    i_ += 6;
                    // Combine a UTF-16 surrogate pair when present.
                    if (code >= 0xD800 && code <= 0xDBFF && i_ + 5 < s_.size() && s_[i_] == '\\'
                        && s_[i_ + 1] == 'u') {
                        std::uint32_t low = 0;
                        for (int k = 0; k < 4; ++k) {
                            low = low * 16 + hex_digit(s_[i_ + 2 + k]);
                        }
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                            i_ += 6;
                        }
                    }
                    append_utf8(out, code);
                } else {
                    // Unknown escape — pass through literally rather than
                    // corrupting the stream.
                    out += esc;
                    i_ += 2;
                }
            } else {
                out += c;
                ++i_;
            }
        }
        if (i_ >= s_.size()) {
            return false;  // unterminated string
        }
        ++i_;  // closing quote
        return true;
    }

    bool parse_number(long long& out)
    {
        skip_ws();
        std::size_t const start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) {
            ++i_;
        }
        while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') {
            ++i_;
        }
        if (i_ == start) {
            return false;
        }
        out = std::stoll(s_.substr(start, i_ - start));
        return true;
    }

    bool parse_bool(bool& out)
    {
        skip_ws();
        if (s_.compare(i_, 4, "true") == 0) {
            out = true;
            i_ += 4;
            return true;
        }
        if (s_.compare(i_, 5, "false") == 0) {
            out = false;
            i_ += 5;
            return true;
        }
        return false;
    }

    // Skips one JSON value of any shape, for keys the caller does not need.
    void skip_value()
    {
        skip_ws();
        if (eof()) {
            return;
        }
        char const c = peek();
        if (c == '"') {
            std::string discard;
            parse_string(discard);
        } else if (c == '{') {
            ++i_;
            skip_ws();
            if (peek() == '}') {
                ++i_;
                return;
            }
            while (true) {
                std::string key;
                parse_string(key);
                consume(':');
                skip_value();
                skip_ws();
                if (peek() == ',') {
                    ++i_;
                    continue;
                }
                consume('}');
                break;
            }
        } else if (c == '[') {
            ++i_;
            skip_ws();
            if (peek() == ']') {
                ++i_;
                return;
            }
            while (true) {
                skip_value();
                skip_ws();
                if (peek() == ',') {
                    ++i_;
                    continue;
                }
                consume(']');
                break;
            }
        } else if (c == 't' || c == 'f') {
            bool discard = false;
            parse_bool(discard);
        } else {
            long long discard = 0;
            parse_number(discard);
        }
    }

    // Parses a `fields`-shaped array: `[{"path":"...","value":"..."}, ...]`
    // or with `"value_b64"` instead of `"value"`. Decodes every entry to RAW
    // BYTES (readback_jsonl.hpp::base64_decode below) so the caller never has
    // to special-case which key was present.
    bool parse_field_array(std::vector<FieldEntry>& out);

private:
    static std::uint32_t hex_digit(char c)
    {
        if (c >= '0' && c <= '9') return static_cast<std::uint32_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint32_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<std::uint32_t>(c - 'A' + 10);
        return 0;
    }

    static void append_utf8(std::string& out, std::uint32_t code)
    {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::string s_;
    std::size_t i_ = 0;
};

// RFC 4648 standard alphabet decode — the inverse of readback_jsonl.hpp's
// base64_encode(). Returns the decoded raw bytes; malformed input decodes as
// far as it can (this is a test-support reader, not a hardened boundary).
std::string base64_decode(std::string const& in)
{
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve((in.size() / 4) * 3);
    int buf = 0;
    int bits = 0;
    for (char const c : in) {
        if (c == '=') {
            break;
        }
        int const v = decode_char(c);
        if (v < 0) {
            continue;
        }
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xFF);
        }
    }
    return out;
}

bool LineReader::parse_field_array(std::vector<FieldEntry>& out)
{
    skip_ws();
    if (!consume('[')) {
        return false;
    }
    skip_ws();
    if (peek() == ']') {
        ++i_;
        return true;
    }
    while (true) {
        if (!consume('{')) {
            return false;
        }
        FieldEntry entry;
        bool have_value = false;
        while (true) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            if (!consume(':')) {
                return false;
            }
            if (key == "path") {
                if (!parse_string(entry.path)) return false;
            } else if (key == "value") {
                if (!parse_string(entry.value)) return false;
                have_value = true;
            } else if (key == "value_b64") {
                std::string b64;
                if (!parse_string(b64)) return false;
                entry.value = base64_decode(b64);
                have_value = true;
            } else {
                // "fix_type" on a typed entry, or any future key — not this
                // struct's concern.
                skip_value();
            }
            skip_ws();
            if (peek() == ',') {
                ++i_;
                continue;
            }
            break;
        }
        if (!consume('}')) {
            return false;
        }
        if (have_value) {
            out.push_back(std::move(entry));
        }
        skip_ws();
        if (peek() == ',') {
            ++i_;
            continue;
        }
        break;
    }
    return consume(']');
}

}  // namespace

std::vector<ParsedRecord> parse_stream(std::string const& path)
{
    std::vector<ParsedRecord> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return out;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        LineReader r(line);
        if (!r.consume('{')) {
            continue;
        }
        std::string type;
        ParsedRecord rec;
        bool got_type = false;
        bool malformed = false;
        while (true) {
            std::string key;
            if (!r.parse_string(key)) {
                malformed = true;
                break;
            }
            if (!r.consume(':')) {
                malformed = true;
                break;
            }
            if (key == "type") {
                if (!r.parse_string(type)) {
                    malformed = true;
                    break;
                }
                got_type = true;
            } else if (key == "msg_type") {
                r.parse_string(rec.msg_type);
            } else if (key == "seq_num") {
                r.parse_number(rec.seq_num);
            } else if (key == "direction") {
                r.parse_string(rec.direction);
            } else if (key == "occurrence") {
                r.parse_number(rec.occurrence);
            } else if (key == "script_step_id") {
                r.parse_string(rec.script_step_id);
            } else if (key == "fields") {
                r.parse_field_array(rec.fields);
            } else {
                r.skip_value();
            }
            r.skip_ws();
            if (r.peek() == ',') {
                r.consume(',');
                continue;
            }
            break;
        }
        if (malformed || !got_type) {
            continue;
        }
        if (type == "sent") {
            rec.kind = ParsedRecord::Kind::Sent;
            out.push_back(std::move(rec));
        } else if (type == "readback") {
            rec.kind = ParsedRecord::Kind::Readback;
            out.push_back(std::move(rec));
        }
        // "hello" / "terminal" — intentionally skipped (file-header scope note).
    }
    return out;
}

namespace {

struct Key {
    long long seq_num;
    std::string direction;
    long long occurrence;

    bool operator==(Key const& other) const
    {
        return seq_num == other.seq_num && direction == other.direction
            && occurrence == other.occurrence;
    }
};

struct KeyHash {
    std::size_t operator()(Key const& k) const
    {
        return std::hash<long long>{}(k.seq_num) ^ (std::hash<std::string>{}(k.direction) << 1)
            ^ (std::hash<long long>{}(k.occurrence) << 2);
    }
};

// §4: "Value comparison is on decoded values, not rendered text — decimals
// compare numerically." Raw-byte equality first (also what makes a sent
// `value` and a readback `value_b64` over the SAME bytes compare equal,
// since both are already decoded to raw bytes by parse_field_array above);
// only on inequality, and only when BOTH sides look like a decimal spelling
// (contain '.'), fall back to a trailing-zero-insensitive comparison. An
// integer-looking mismatch (e.g. a leading-zero'd Account "0100" vs "100")
// therefore never collapses — it stays a real value_mismatch.
std::string normalize_decimal_spelling(std::string const& raw)
{
    if (raw.find('.') == std::string::npos) {
        return raw;
    }
    std::string trimmed = raw;
    while (!trimmed.empty() && trimmed.back() == '0') {
        trimmed.pop_back();
    }
    if (!trimmed.empty() && trimmed.back() == '.') {
        trimmed.pop_back();
    }
    return trimmed.empty() ? "0" : trimmed;
}

bool values_equal(std::string const& a, std::string const& b)
{
    if (a == b) {
        return true;
    }
    if (a.find('.') != std::string::npos && b.find('.') != std::string::npos) {
        return normalize_decimal_spelling(a) == normalize_decimal_spelling(b);
    }
    return false;
}

}  // namespace

std::vector<WitnessRow> compare_streams(std::vector<ParsedRecord> const& stream_a,
                                         std::vector<ParsedRecord> const& stream_b,
                                         WitnessIdentity const& identity)
{
    // Pool `readback` records from BOTH streams — a run's two processes each
    // write their OWN file, and the receiver of a given direction is never
    // the same process as the sender (contracts/readback-jsonl.md §
    // Transport), so there is no risk of double-counting one message twice
    // under the same key.
    std::unordered_map<Key, ParsedRecord const*, KeyHash> readback_by_key;
    for (auto const* stream : {&stream_a, &stream_b}) {
        for (ParsedRecord const& rec : *stream) {
            if (rec.kind == ParsedRecord::Kind::Readback) {
                readback_by_key[Key{rec.seq_num, rec.direction, rec.occurrence}] = &rec;
            }
        }
    }

    std::vector<WitnessRow> rows;
    for (auto const* stream : {&stream_a, &stream_b}) {
        for (ParsedRecord const& sent : *stream) {
            if (sent.kind != ParsedRecord::Kind::Sent) {
                continue;
            }

            WitnessRow row;
            row.witness_id = identity.cell_id + ":" + sent.script_step_id + ":" + sent.direction
                + ":" + std::to_string(sent.occurrence);
            row.run_id = identity.run_id;
            row.authoritative = identity.authoritative;
            row.combo_id = identity.combo_id;
            row.cell_id = identity.cell_id;
            row.config = identity.config;
            row.arm = identity.arm;
            row.kind = identity.kind;
            row.script_step_id = sent.script_step_id;
            row.msg_type = sent.msg_type;
            row.direction = sent.direction;
            row.occurrence = sent.occurrence;

            auto const it = readback_by_key.find(Key{sent.seq_num, sent.direction, sent.occurrence});
            if (it == readback_by_key.end()) {
                // FR-016c: "No readback record ⇒ fail, never pass and never
                // skip." An empty field set must not compare equal to an
                // empty intent by accident — so this is checked BEFORE any
                // field comparison, not modeled as "readback.fields == {}".
                row.verdict = "fail";
                row.mismatch.push_back(
                    Mismatch{.path = "", .cls = "missing", .sent_value = "<record>", .readback_value = ""});
                rows.push_back(std::move(row));
                continue;
            }

            ParsedRecord const& readback = *it->second;
            std::unordered_map<std::string, std::string> readback_fields;
            for (FieldEntry const& fe : readback.fields) {
                readback_fields[fe.path] = fe.value;
            }

            std::vector<Mismatch> mismatches;
            for (FieldEntry const& fe : sent.fields) {
                auto const rb_it = readback_fields.find(fe.path);
                if (rb_it == readback_fields.end()) {
                    mismatches.push_back(Mismatch{
                        .path = fe.path, .cls = "missing", .sent_value = fe.value, .readback_value = ""});
                } else if (!values_equal(fe.value, rb_it->second)) {
                    mismatches.push_back(Mismatch{.path = fe.path,
                                                   .cls = "value_mismatch",
                                                   .sent_value = fe.value,
                                                   .readback_value = rb_it->second});
                }
            }
            for (FieldEntry const& fe : readback.fields) {
                bool const declared = std::any_of(sent.fields.begin(), sent.fields.end(),
                                                   [&](FieldEntry const& s) { return s.path == fe.path; });
                if (!declared) {
                    mismatches.push_back(Mismatch{
                        .path = fe.path, .cls = "spurious", .sent_value = "", .readback_value = fe.value});
                }
            }

            row.verdict = mismatches.empty() ? "pass" : "fail";
            row.mismatch = std::move(mismatches);
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

}  // namespace fixpp::interop::readback
