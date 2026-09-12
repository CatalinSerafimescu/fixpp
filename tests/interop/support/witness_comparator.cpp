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
//
// make_fix44_decimal_resolver() is the one place this TU DOES depend on
// production fixpp (fixpp::dict) — deliberately: data-model.md §4's "decimals
// compare numerically" rule is keyed on a field's FIX TYPE, and the type
// vocabulary is not this test-support layer's to invent (gate-b fix round,
// see values_equal() below).
#include "witness_comparator.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <unordered_map>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>

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

    // Parses a witness row's `mismatch` array:
    // `[{"path":"...","cls":"...","sent_value":"...","readback_value":"..."}, ...]`.
    bool parse_mismatch_array(std::vector<Mismatch>& out);

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

bool LineReader::parse_mismatch_array(std::vector<Mismatch>& out)
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
        Mismatch entry;
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
            } else if (key == "cls") {
                if (!parse_string(entry.cls)) return false;
            } else if (key == "sent_value") {
                if (!parse_string(entry.sent_value)) return false;
            } else if (key == "readback_value") {
                if (!parse_string(entry.readback_value)) return false;
            } else {
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
        out.push_back(std::move(entry));
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
        bool has_msg_type = false;
        bool has_seq_num = false;
        bool has_direction = false;
        bool has_occurrence = false;
        bool has_script_step_id = false;
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
                if (!r.parse_string(rec.msg_type)) {
                    malformed = true;
                    break;
                }
                has_msg_type = true;
            } else if (key == "seq_num") {
                if (!r.parse_number(rec.seq_num)) {
                    malformed = true;
                    break;
                }
                has_seq_num = true;
            } else if (key == "direction") {
                if (!r.parse_string(rec.direction)) {
                    malformed = true;
                    break;
                }
                has_direction = true;
            } else if (key == "occurrence") {
                if (!r.parse_number(rec.occurrence)) {
                    malformed = true;
                    break;
                }
                has_occurrence = true;
            } else if (key == "script_step_id") {
                if (!r.parse_string(rec.script_step_id)) {
                    malformed = true;
                    break;
                }
                has_script_step_id = true;
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
        if (type == "sent" || type == "readback") {
            // Fail-closed (gate-b fix round, FQ-4 part 0): seq_num and
            // occurrence default to 0 and direction defaults to "" (the
            // struct's own in-class initializers), and 0/"" are LEGITIMATE
            // values -- occurrence 0 is the common case in the committed
            // artifact -- so presence is tracked by a seen-flag PER FIELD,
            // never inferred from the value. A record that is syntactically
            // well-formed JSON but simply OMITS a required correlation
            // field is corrupted evidence, not a malformed line, and must
            // not be silently admitted at key (0, "", 0) -- two such
            // records would silently COLLIDE there (see compare_streams'
            // duplicate-record rejection), which is why this check runs
            // BEFORE that one and names the real cause first.
            std::vector<std::string> missing;
            if (!has_msg_type) missing.emplace_back("msg_type");
            if (!has_seq_num) missing.emplace_back("seq_num");
            if (!has_direction) missing.emplace_back("direction");
            if (!has_occurrence) missing.emplace_back("occurrence");
            if (type == "sent" && !has_script_step_id) missing.emplace_back("script_step_id");
            if (!missing.empty()) {
                std::string msg =
                    "parse_stream: " + type + " record missing required correlation field(s):";
                for (std::string const& field : missing) {
                    msg += " " + field;
                }
                msg += " -- line: " + line;
                throw std::runtime_error(msg);
            }
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

// data-model.md §4: "Value comparison is on decoded values, not rendered
// text — decimals compare numerically." Whether a field is a DECIMAL is a
// property of its FIX TYPE (gate-b fix round), never its spelling — a
// spelling-keyed heuristic ("both sides contain '.'") is wrong in BOTH
// directions: a STRING field whose value legitimately contains '.'
// (ClOrdID "ORD.10" vs "ORD.1", Text "1.50" vs "1.5") collapses to a false
// PASS, and a decimal field with no fractional part on one side (PRICE
// "190" vs "190.0") reports a false value_mismatch. The caller resolves the
// type via DecimalTagResolver (witness_comparator.hpp); this function only
// does the comparison once told which rule applies.
//
// EXACT decimal comparison, never float: sign, integer part with leading
// zeros stripped (at least one digit kept), fraction with trailing zeros
// stripped. "0100" and "100" compare equal; "190.5" and "190.50000000000001"
// do NOT — there is no rounding anywhere in this path.
std::string canonical_decimal(std::string const& raw)
{
    if (raw.empty()) {
        return raw;
    }
    std::size_t i = 0;
    bool negative = false;
    if (raw[i] == '+') {
        ++i;
    } else if (raw[i] == '-') {
        negative = true;
        ++i;
    }
    std::size_t const dot = raw.find('.', i);
    std::string int_part = (dot == std::string::npos) ? raw.substr(i) : raw.substr(i, dot - i);
    std::string frac_part = (dot == std::string::npos) ? std::string{} : raw.substr(dot + 1);

    std::size_t const first_nonzero = int_part.find_first_not_of('0');
    int_part = (first_nonzero == std::string::npos) ? "0" : int_part.substr(first_nonzero);
    if (int_part.empty()) {
        int_part = "0";
    }
    while (!frac_part.empty() && frac_part.back() == '0') {
        frac_part.pop_back();
    }

    bool const is_zero = int_part == "0" && frac_part.empty();
    std::string out;
    if (negative && !is_zero) {
        out += '-';
    }
    out += int_part;
    if (!frac_part.empty()) {
        out += '.';
        out += frac_part;
    }
    return out;
}

// Raw-byte equality first (also what makes a sent `value` and a readback
// `value_b64` over the SAME bytes compare equal, since both are already
// decoded to raw bytes by parse_field_array above). `is_decimal` gates the
// ONLY fallback: canonical exact-decimal comparison. Every non-decimal
// (STRING-collapsed and otherwise) field compares byte for byte, full stop —
// no spelling-based heuristic of any kind.
bool values_equal(std::string const& a, std::string const& b, bool is_decimal)
{
    if (a == b) {
        return true;
    }
    if (is_decimal) {
        return canonical_decimal(a) == canonical_decimal(b);
    }
    return false;
}

// Path grammar (contract § Canonical form): "40" | "453[0].448" |
// "453[1].802[0].523" — the LEAF tag (the field this entry's value actually
// belongs to) is always the plain numeric segment after the last '.', with
// no bracket suffix. Falls back to 0 (never decimal-typed by construction:
// `field_type::String` is field_type_of()'s own miss-path default) on a
// malformed path rather than throwing — this is a best-effort comparator,
// not a validator.
std::uint16_t leaf_tag(std::string const& path)
{
    std::size_t const last_dot = path.find_last_of('.');
    std::string const seg = (last_dot == std::string::npos) ? path : path.substr(last_dot + 1);
    try {
        return static_cast<std::uint16_t>(std::stoul(seg));
    } catch (...) {
        return 0;
    }
}

}  // namespace

DecimalTagResolver make_fix44_decimal_resolver(std::string const& dict_xml_path)
{
    // Dictionary is move-only; table_view is built once from it here (not
    // per comparison — [const §XV.1], config-time cost, not per-message).
    // Both are kept alive for the resolver's lifetime via shared_ptr so
    // std::function stays copyable.
    auto dict = std::make_shared<fixpp::dict::Dictionary>(
        fixpp::dict::XmlLoader{}.load(dict_xml_path, std::pmr::get_default_resource()));
    auto table = std::make_shared<fixpp::dict::table_view>(dict->as_table_view());
    return [dict, table](std::uint16_t tag) noexcept {
        return table->field_type_of(tag) == fixpp::dict::field_type::Float;
    };
}

std::vector<WitnessRow> compare_streams(std::vector<ParsedRecord> const& stream_a,
                                         std::vector<ParsedRecord> const& stream_b,
                                         WitnessIdentity const& identity,
                                         DecimalTagResolver const& is_decimal_tag)
{
    // Pool `readback` records from BOTH streams — a run's two processes each
    // write their OWN file, and the receiver of a given direction is never
    // the same process as the sender (contracts/readback-jsonl.md §
    // Transport), so there is no risk of double-counting one message twice
    // under the same key.
    //
    // FQ-4 part 2 (gate-b fix round): `emplace`, not `operator[]` — the OLD
    // code silently overwrote on a key collision, so a wrong record followed
    // by a correct duplicate DISAPPEARED from the verdict. On a collision the
    // FIRST record stays resident (emplace's own rule) but the key is
    // remembered as duplicated so the consuming `sent` below fails loudly
    // instead of comparing against whichever one happened to win.
    std::unordered_map<Key, ParsedRecord const*, KeyHash> readback_by_key;
    std::unordered_map<Key, std::string, KeyHash> readback_duplicate_reason;
    for (auto const* stream : {&stream_a, &stream_b}) {
        for (ParsedRecord const& rec : *stream) {
            if (rec.kind == ParsedRecord::Kind::Readback) {
                Key const key{rec.seq_num, rec.direction, rec.occurrence};
                auto const [it, inserted] = readback_by_key.emplace(key, &rec);
                if (!inserted) {
                    readback_duplicate_reason[key] = "duplicate readback record at (seq_num="
                        + std::to_string(rec.seq_num) + ", direction=" + rec.direction
                        + ", occurrence=" + std::to_string(rec.occurrence)
                        + "): resident msg_type=" + it->second->msg_type
                        + ", duplicate msg_type=" + rec.msg_type;
                }
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

            // FQ-4 part 2: a collision at this key means there is no single
            // correct readback record to compare against -- fail loudly
            // rather than silently comparing against whichever one won the
            // emplace race above.
            if (auto const dup_it = readback_duplicate_reason.find(
                    Key{sent.seq_num, sent.direction, sent.occurrence});
                dup_it != readback_duplicate_reason.end()) {
                row.verdict = "fail";
                row.mismatch.push_back(Mismatch{.path = "",
                                                 .cls = "duplicate_record",
                                                 .sent_value = sent.msg_type,
                                                 .readback_value = dup_it->second});
                rows.push_back(std::move(row));
                continue;
            }

            ParsedRecord const& readback = *it->second;

            // FR-018 spurious-hit (spec.md's FR-016c empty-intent-vs-empty-readback row): "Emit a message whose
            // declared intent set is empty -- the comparator must reject
            // rather than pass on ∅ == ∅." Distinct from the "readback not
            // found" branch above (T042/FR-016c): here a readback record
            // DOES exist, at the correct key, and ALSO declares zero
            // fields -- the per-field loops below would then find zero
            // mismatches and wrongly report "pass", comparing nothing
            // against nothing. Checked here, BEFORE those loops, and
            // narrowed to BOTH sides empty: a sent record with zero
            // declared fields against a readback that reports real fields
            // is not this case -- it is already correctly caught below as
            // one `spurious` mismatch per reported field.
            if (sent.fields.empty() && readback.fields.empty()) {
                row.verdict = "fail";
                row.mismatch.push_back(Mismatch{.path = "",
                                                 .cls = "missing",
                                                 .sent_value = "<empty intent vs empty readback>",
                                                 .readback_value = ""});
                rows.push_back(std::move(row));
                continue;
            }

            std::vector<Mismatch> mismatches;

            // FQ-4 part 1: `Key` (above) intentionally omits msg_type -- an
            // EXPLICIT check here, rather than widening the key, because
            // widening would turn a real msg_type defect into a "missing
            // readback" diagnostic instead of naming the invariant
            // data-model.md §3 states ("msg_type — must equal the paired
            // readback's").
            if (sent.msg_type != readback.msg_type) {
                mismatches.push_back(Mismatch{.path = "35",
                                               .cls = "msg_type",
                                               .sent_value = sent.msg_type,
                                               .readback_value = readback.msg_type});
            }

            // FQ-4 part 3: readback-side duplicate paths (data-model.md §2:
            // "two entries may not share a `path` within one record").
            // `emplace` keeps the FIRST value; every collision beyond the
            // first is reported explicitly rather than silently overwritten
            // (the OLD code's `operator[]` let a wrong field followed by a
            // correct duplicate disappear from the verdict).
            std::unordered_map<std::string, std::string> readback_fields;
            for (FieldEntry const& fe : readback.fields) {
                auto const [rf_it, rf_inserted] = readback_fields.emplace(fe.path, fe.value);
                if (!rf_inserted) {
                    mismatches.push_back(Mismatch{.path = fe.path,
                                                   .cls = "duplicate_path",
                                                   .sent_value = "",
                                                   .readback_value = fe.value});
                }
            }
            // Same treatment for the `sent` side -- the field loop below
            // never built a map before, so a duplicate `sent` path was
            // invisible to every check, not merely to a last-wins hazard.
            {
                std::unordered_map<std::string, std::string> seen;
                for (FieldEntry const& fe : sent.fields) {
                    auto const [s_it, s_inserted] = seen.emplace(fe.path, fe.value);
                    if (!s_inserted) {
                        mismatches.push_back(Mismatch{.path = fe.path,
                                                       .cls = "duplicate_path",
                                                       .sent_value = fe.value,
                                                       .readback_value = ""});
                    }
                }
            }

            for (FieldEntry const& fe : sent.fields) {
                auto const rb_it = readback_fields.find(fe.path);
                if (rb_it == readback_fields.end()) {
                    mismatches.push_back(Mismatch{
                        .path = fe.path, .cls = "missing", .sent_value = fe.value, .readback_value = ""});
                } else if (!values_equal(fe.value, rb_it->second, is_decimal_tag(leaf_tag(fe.path)))) {
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

namespace {

// Canonical form matches readback_jsonl.hpp's Stream: json_escape, no
// insignificant whitespace, bare decimal integers, fixed key order
// (path, cls, sent_value, readback_value — the same order Mismatch's members
// are declared in witness_comparator.hpp).
std::string render_mismatch_array(std::vector<Mismatch> const& mismatches)
{
    std::string out = "[";
    bool first = true;
    for (Mismatch const& m : mismatches) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += "{\"path\":\"" + json_escape(m.path) + "\"";
        out += ",\"cls\":\"" + json_escape(m.cls) + "\"";
        out += ",\"sent_value\":\"" + json_escape(m.sent_value) + "\"";
        out += ",\"readback_value\":\"" + json_escape(m.readback_value) + "\"";
        out += "}";
    }
    out += "]";
    return out;
}

}  // namespace

bool write_witness_rows(std::string const& path, std::vector<WitnessRow> const& rows)
{
    // TRUNCATE, same rule as readback_jsonl.hpp::Stream — a witnesses.jsonl
    // surviving from an earlier run at this path would satisfy promotion's
    // reader while describing a DIFFERENT run (data-model.md §4's own "an
    // absent file is no witnesses, never a skip" only holds if a stale file
    // cannot masquerade as a fresh one).
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    for (WitnessRow const& row : rows) {
        std::string line = "{\"witness_id\":\"" + json_escape(row.witness_id) + "\"";
        line += ",\"run_id\":\"" + json_escape(row.run_id) + "\"";
        line += ",\"authoritative\":";
        line += row.authoritative ? "true" : "false";
        line += ",\"combo_id\":\"" + json_escape(row.combo_id) + "\"";
        line += ",\"cell_id\":\"" + json_escape(row.cell_id) + "\"";
        line += ",\"config\":\"" + json_escape(row.config) + "\"";
        line += ",\"arm\":\"" + json_escape(row.arm) + "\"";
        line += ",\"kind\":\"" + json_escape(row.kind) + "\"";
        line += ",\"script_step_id\":\"" + json_escape(row.script_step_id) + "\"";
        line += ",\"msg_type\":\"" + json_escape(row.msg_type) + "\"";
        line += ",\"direction\":\"" + json_escape(row.direction) + "\"";
        line += ",\"occurrence\":" + std::to_string(row.occurrence);
        line += ",\"verdict\":\"" + json_escape(row.verdict) + "\"";
        line += ",\"mismatch\":" + render_mismatch_array(row.mismatch);
        line += "}";
        out << line << '\n';
    }
    out.flush();
    return static_cast<bool>(out);
}

std::vector<WitnessRow> parse_witness_rows(std::string const& path)
{
    std::vector<WitnessRow> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return out;  // absent file -> no witnesses (data-model.md §4), not an error
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        LineReader r(line);
        if (!r.consume('{')) {
            throw std::runtime_error("witness row is not a JSON object: " + line);
        }
        WitnessRow row;
        bool has_witness_id = false;
        bool has_run_id = false;
        bool has_authoritative = false;
        bool has_combo_id = false;
        bool has_cell_id = false;
        bool has_config = false;
        bool has_arm = false;
        bool has_kind = false;
        bool has_script_step_id = false;
        bool has_msg_type = false;
        bool has_direction = false;
        bool has_occurrence = false;
        bool has_verdict = false;
        bool has_mismatch = false;
        while (true) {
            std::string key;
            if (!r.parse_string(key)) {
                throw std::runtime_error("witness row: malformed key: " + line);
            }
            if (!r.consume(':')) {
                throw std::runtime_error("witness row: malformed ':' after key '" + key + "': " + line);
            }
            if (key == "witness_id") {
                r.parse_string(row.witness_id);
                has_witness_id = true;
            } else if (key == "run_id") {
                r.parse_string(row.run_id);
                has_run_id = true;
            } else if (key == "authoritative") {
                r.parse_bool(row.authoritative);
                has_authoritative = true;
            } else if (key == "combo_id") {
                r.parse_string(row.combo_id);
                has_combo_id = true;
            } else if (key == "cell_id") {
                r.parse_string(row.cell_id);
                has_cell_id = true;
            } else if (key == "config") {
                r.parse_string(row.config);
                has_config = true;
            } else if (key == "arm") {
                r.parse_string(row.arm);
                has_arm = true;
            } else if (key == "kind") {
                r.parse_string(row.kind);
                has_kind = true;
            } else if (key == "script_step_id") {
                r.parse_string(row.script_step_id);
                has_script_step_id = true;
            } else if (key == "msg_type") {
                r.parse_string(row.msg_type);
                has_msg_type = true;
            } else if (key == "direction") {
                r.parse_string(row.direction);
                has_direction = true;
            } else if (key == "occurrence") {
                r.parse_number(row.occurrence);
                has_occurrence = true;
            } else if (key == "verdict") {
                r.parse_string(row.verdict);
                has_verdict = true;
            } else if (key == "mismatch") {
                r.parse_mismatch_array(row.mismatch);
                has_mismatch = true;
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
        if (!r.consume('}')) {
            throw std::runtime_error("witness row: unterminated object: " + line);
        }

        // §4 / W-1: a row missing a required field is NOT silently
        // defaulted -- that reproduces, at parse time, exactly the "silently
        // dropped from the population it should have joined" hazard
        // witness-evidence.md names for a missing `kind`/`authoritative`.
        // Name every missing field, not just the first, so one bad row does
        // not hide a second defect behind it.
        std::vector<std::string> missing;
        if (!has_witness_id) missing.emplace_back("witness_id");
        if (!has_run_id) missing.emplace_back("run_id");
        if (!has_authoritative) missing.emplace_back("authoritative");
        if (!has_combo_id) missing.emplace_back("combo_id");
        if (!has_cell_id) missing.emplace_back("cell_id");
        if (!has_config) missing.emplace_back("config");
        if (!has_arm) missing.emplace_back("arm");
        if (!has_kind) missing.emplace_back("kind");
        if (!has_script_step_id) missing.emplace_back("script_step_id");
        if (!has_msg_type) missing.emplace_back("msg_type");
        if (!has_direction) missing.emplace_back("direction");
        if (!has_occurrence) missing.emplace_back("occurrence");
        if (!has_verdict) missing.emplace_back("verdict");
        if (!has_mismatch) missing.emplace_back("mismatch");
        if (!missing.empty()) {
            std::string msg = "witness row missing required field(s):";
            for (std::string const& field : missing) {
                msg += " " + field;
            }
            msg += " -- line: " + line;
            throw std::runtime_error(msg);
        }

        out.push_back(std::move(row));
    }
    return out;
}

}  // namespace fixpp::interop::readback
