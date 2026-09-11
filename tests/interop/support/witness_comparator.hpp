// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/witness_comparator.hpp — 089 T018.
//
// The SHARED comparator (data-model.md §4 "Witness"): reads the two readback
// JSONL streams a run produces (contracts/readback-jsonl.md § Transport —
// "fixpp-readback.jsonl" + "counterparty-readback.jsonl", separate files,
// paired on `(seq_num, direction, occurrence)`) and emits one witness row per
// `sent` record, per FR-006's exact-set-equality rule.
//
// Scope, stated rather than implied:
//   - Implements FR-006's THREE named classes (value_mismatch / missing /
//     spurious) and FR-016c ("no readback record ⇒ fail, never pass and
//     never skip").
//   - Does NOT implement FR-005's fourth class, `occurrence_count_mismatch`
//     (spec.md FR-005): that requires the conversation census's DECLARED
//     occurrence values as an operand, which is out of T018's scope. A
//     `readback` record with no matching `sent` record (the mirror case) is
//     therefore not surfaced as its own witness row here — it is FR-005
//     territory, owed by whichever task threads the census through.
//   - Does NOT validate `hello`/`terminal` presence or ordering (C-1/C-10);
//     that is the promotion command's job (tasks.md T024,
//     promote_interop_evidence.py), not the comparator's.
//   - `combo_id`, `authoritative` and `kind` (data-model.md §4's W-1 row) are
//     NOT derivable from the two streams — they are per-run identity the
//     harness/shim already holds. Callers MUST supply them via
//     `WitnessIdentity`; an empty one is a caller bug, not a defaulted value
//     (witness-evidence.md: a witness row missing `kind`/`authoritative` is
//     "silently dropped from the population it should have joined").
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "readback_jsonl.hpp"  // FieldEntry (reused verbatim: same grammar, contract § Records)

namespace fixpp::interop::readback {

// One parsed `sent` or `readback` record. `hello`/`terminal` lines are
// skipped by parse_stream() (see file-header scope note).
struct ParsedRecord {
    enum class Kind : std::uint8_t { Sent, Readback };
    Kind kind = Kind::Sent;
    std::string msg_type;
    long long seq_num = 0;
    std::string direction;
    long long occurrence = 0;
    std::string script_step_id;  // `sent` only; empty on `readback`
    // Every field entry's value DECODED TO RAW BYTES already — a `value`
    // entry is JSON-unescaped, a `value_b64` entry is base64-decoded, so a
    // sent `value` and a readback `value_b64` over the SAME raw bytes
    // compare equal without any special-casing at the caller.
    std::vector<FieldEntry> fields;
};

// Parses one readback-JSONL file (contracts/readback-jsonl.md § Transport).
// Malformed lines and non-sent/readback record kinds are silently skipped —
// this is a comparator input reader, not a schema validator (see file-header
// scope note on C-1/C-10).
[[nodiscard]] std::vector<ParsedRecord> parse_stream(std::string const& path);

struct Mismatch {
    std::string path;   // field path per contract § Canonical form; empty for
                         // the whole-record "no readback" case (FR-016c)
    std::string cls;     // "value_mismatch" | "missing" | "spurious" (FR-006)
    std::string sent_value;
    std::string readback_value;
};

// Identity fields data-model.md §4 requires on every witness row (W-1) that
// the two streams cannot supply. Caller-stamped, never derived.
struct WitnessIdentity {
    std::string run_id;
    std::string combo_id;
    std::string cell_id;
    std::string config;
    std::string arm;
    std::string kind;  // "conformance" | "validator-positive-control"
    bool authoritative = false;
};

struct WitnessRow {
    std::string witness_id;  // derived: cell_id + ":" + script_step_id + ":"
                              // + direction + ":" + occurrence (data-model.md
                              // §4: "stable and derivable from the
                              // conversation script" — script_step_id is that
                              // derivation's input)
    std::string run_id;
    bool authoritative = false;
    std::string combo_id;
    std::string cell_id;
    std::string config;
    std::string arm;
    std::string kind;
    std::string script_step_id;
    std::string msg_type;
    std::string direction;
    long long occurrence = 0;
    std::string verdict;  // "pass" | "fail" — this comparator never emits
                           // "skip" (data-model.md §4: "skip may not be
                           // produced by a missing record")
    std::vector<Mismatch> mismatch;
};

// data-model.md §4: "Value comparison is on decoded values, not rendered
// text — decimals compare numerically." Whether a field is a decimal is a
// property of its FIX TYPE, not its spelling — a raw `fields` entry (§2/§3)
// carries no type, so the comparator needs an external tag→"is this a
// decimal type" oracle. `true` iff the tag's dictionary type collapses to
// `fixpp::dict::field_type::Float` (PRICE, QTY, AMT, FLOAT, PRICEOFFSET,
// PERCENTAGE — `fixpp::dict::field_type_from_data_type`'s own collapse,
// include/fixpp/dict/field_type.hpp). Every other type — including every
// STRING-collapsed one — compares EXACTLY, byte for byte; there is no
// spelling-based fallback.
using DecimalTagResolver = std::function<bool(std::uint16_t tag)>;

// Builds a DecimalTagResolver backed by fixpp's own FIX 4.4 dictionary,
// loaded once from `dict_xml_path` (the gtests' copy of the path the harness
// sets via `FIXPP_FIX44_DICT_XML`, data-model.md §1 / R-4a). Throws whatever
// `fixpp::dict::XmlLoader::load` throws on a malformed/missing file
// (construction-time exception, `[arch §5.3]` — this is test-support setup,
// never the parse/fromApp hot path).
[[nodiscard]] DecimalTagResolver make_fix44_decimal_resolver(std::string const& dict_xml_path);

// The shared comparator. `stream_a`/`stream_b` are the two parsed streams of
// one run, in EITHER order — pairing is by key, not by which file a record
// came from (contracts/readback-jsonl.md § Transport: "the comparator reads
// both streams and pairs ... the same key regardless of which file each came
// from"). One witness row per `sent` record pooled from both streams.
// `is_decimal_tag` resolves whether a given field's tag is decimal-typed
// (see DecimalTagResolver above) — required, not defaulted: a comparator
// with no dictionary behind it has no basis for guessing.
[[nodiscard]] std::vector<WitnessRow> compare_streams(std::vector<ParsedRecord> const& stream_a,
                                                       std::vector<ParsedRecord> const& stream_b,
                                                       WitnessIdentity const& identity,
                                                       DecimalTagResolver const& is_decimal_tag);

// The comparator→promotion hand-off (data-model.md §4, pinned `a7923892`):
// `rows` — the FULL result of one compare_streams() call, not a filtered
// subset — written one JSON object per line to `path` (the run directory's
// `witnesses.jsonl`, beside `fixpp-readback.jsonl` /
// `counterparty-readback.jsonl`), TRUNCATE mode (same rule as
// readback_jsonl.hpp::Stream — a stale row surviving a re-run is worse than
// a missing one). Every W-1 field, in data-model.md §4's table order;
// canonical JSON form per contracts/readback-jsonl.md § "Escaping and
// encoding" (json_escape, no insignificant whitespace, bare decimal
// integers) — the same rules the streams use, because the promotion
// command's reader (`promote_interop_evidence.py::_read_records`) is a
// PLAIN `json.loads` per line, not this repo's hand-rolled grammar; it only
// needs valid, well-formed JSON, and reusing the streams' rendering rules
// keeps one canonical-form story instead of two. Returns false (no file
// written, or a short/partial write) on an unopenable path; true otherwise.
[[nodiscard]] bool write_witness_rows(std::string const& path, std::vector<WitnessRow> const& rows);

// The round-trip counterpart of write_witness_rows(). Every §4 field on the
// FIELD LIST BELOW is REQUIRED on every line — an absent one throws
// `std::runtime_error` naming the missing field(s) by name, rather than
// silently defaulting it. This is the direct fix for the gap
// witness-evidence.md itself names: "a witness row missing `kind`/
// `authoritative`... is silently dropped from the population it should have
// joined" (W-1) — a reader that defaults a missing field reproduces exactly
// that silent drop one layer down, at parse time instead of at the gate.
// Required: witness_id, run_id, authoritative, combo_id, cell_id, config,
// arm, kind, script_step_id, msg_type, direction, occurrence, verdict,
// mismatch (the last always present, possibly `[]`). A malformed/absent
// FILE (as opposed to a malformed ROW) returns an empty vector, matching
// parse_stream()'s convention — "no witnesses" is a legitimate state
// (data-model.md §4's own "an absent file is no witnesses").
[[nodiscard]] std::vector<WitnessRow> parse_witness_rows(std::string const& path);

}  // namespace fixpp::interop::readback
