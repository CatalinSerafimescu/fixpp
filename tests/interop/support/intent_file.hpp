// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/intent_file.hpp — 089 T053a (FR-008d).
//
// fixpp's reader for the shim-rendered per-run intent file (FR-008d (a),
// `phase-9-harness/tools/run_interop_cell.py::render_intent_file`): one line
// per field, `step_id` TAB `originator` TAB `msg_type` TAB `path` TAB
// `value` LF. fixpp's cell builds every fixpp-originated message's fields
// from this file (T052, not yet wired) instead of a hardcoded literal.
//
// Read in BINARY, never as text: a `value` column may carry a raw non-UTF-8
// byte (the script's B-05 EncodedText(355) 0xff, C-11's live-path charset
// arm) — this reader never decodes/re-encodes a value, every field stays a
// raw-bytes std::string end to end.
//
// A message with NO declared fields (e.g. A-GAPFILL's ResendRequest, whose
// fields are all `runtime_generated`) still renders exactly ONE line, with
// an empty `path` and an empty `value` — this reader treats that line as
// "the message exists, with zero fields", never as malformed.
//
// Written INDEPENDENTLY from the two counterparties' own readers
// (phase-9-harness/quickfix-cpp/counterparty/intent_file.hpp,
// phase-9-harness/quickfixj/.../IntentFile.java, both read-only from this
// submodule) — the reader-agreement check
// (phase-9-harness/interop-readback-fixture/intent_file_check.sh) is what
// proves the three independent implementations agree on one rendered file,
// mirroring C-7's three-way byte-identity check for the readback/sent
// stream.
//
// Standard-library only — same rule as readback_jsonl.hpp (no production
// fixpp dependency, no third-party parser).
#pragma once

#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fixpp::interop::intent {

struct FieldEntry {
    std::string path;
    std::string value;  // raw bytes, as parsed — never re-encoded
};

struct Message {
    std::string step_id;
    std::string originator;  // "fixpp" | "peer"
    std::string msg_type;
    std::vector<FieldEntry> fields;  // empty ⇒ a zero-field message
};

// Parses the WHOLE file's bytes into an ordered list of Messages. Consecutive
// lines sharing (step_id, originator, msg_type) form ONE message — the
// renderer always emits one message's field lines contiguously, in the
// script's declared field order, so grouping is purely "does this line's key
// match the line immediately before it", never a cross-file scan/sort.
//
// Rejects (throws std::runtime_error, naming the 1-based line number) a line
// with the wrong column count or an unknown `originator` — never silently
// skips it, so a dropped/corrupted line in the middle cannot pass unnoticed.
inline std::vector<Message> parse_intent_bytes(std::string const& raw)
{
    std::vector<Message> messages;
    std::size_t start = 0;
    long line_no = 0;
    while (start < raw.size()) {
        std::size_t const nl = raw.find('\n', start);
        std::string const line = (nl == std::string::npos) ? raw.substr(start)
                                                             : raw.substr(start, nl - start);
        start = (nl == std::string::npos) ? raw.size() : nl + 1;
        ++line_no;
        if (nl == std::string::npos && line.empty()) {
            break;  // no content after the final LF — not a phantom line
        }

        std::vector<std::string> cols;
        std::size_t col_start = 0;
        while (true) {
            std::size_t const tab = line.find('\t', col_start);
            if (tab == std::string::npos) {
                cols.push_back(line.substr(col_start));
                break;
            }
            cols.push_back(line.substr(col_start, tab - col_start));
            col_start = tab + 1;
        }
        if (cols.size() != 5) {
            throw std::runtime_error(
                "intent_file.hpp: line " + std::to_string(line_no) + " has " +
                std::to_string(cols.size()) + " columns, expected 5 "
                "(step_id\\toriginator\\tmsg_type\\tpath\\tvalue)");
        }
        std::string const& step_id = cols[0];
        std::string const& originator = cols[1];
        std::string const& msg_type = cols[2];
        std::string const& path = cols[3];
        std::string const& value = cols[4];

        if (originator != "fixpp" && originator != "peer") {
            throw std::runtime_error(
                "intent_file.hpp: line " + std::to_string(line_no) +
                " has unknown originator '" + originator + "' (want 'fixpp' or 'peer')");
        }

        bool const new_message = messages.empty() ||
            messages.back().step_id != step_id ||
            messages.back().originator != originator ||
            messages.back().msg_type != msg_type;
        if (new_message) {
            messages.push_back(Message{step_id, originator, msg_type, {}});
        }
        // An empty path is the zero-field marker (§ file header) — no field
        // entry is added, but the message itself was already started above.
        if (!path.empty()) {
            messages.back().fields.push_back(FieldEntry{path, value});
        }
    }
    return messages;
}

inline std::vector<Message> parse_intent_file(std::string const& file_path)
{
    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("intent_file.hpp: cannot open " + file_path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_intent_bytes(ss.str());
}

}  // namespace fixpp::interop::intent
