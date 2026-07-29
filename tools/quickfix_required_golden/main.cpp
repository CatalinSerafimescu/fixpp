// tools/quickfix_required_golden/main.cpp
//
// 079-required-presence-scope — T018 (Contract 2, census-and-agreement.md).
//
// Local-only golden generator. Links a real, locally built QuickFIX v1.16.0
// (root = the validated CMake cache variable FIXPP_QUICKFIX_ROOT, target
// guarded OFF by default via FIXPP_BUILD_QUICKFIX_GOLDEN — mirrors 075 T002)
// and, for each of the 9 QuickFIX-schema dictionaries (FIX40/41/42/43/44/
// 50/50SP1/50SP2/FIXT11 — NO vlatest/Orchestra row: QuickFIX 1.16.0 cannot
// parse an Orchestra repository), records the LITERAL, MEASURED output of
// `FIX::DataDictionary::isRequiredField(msgType, tag)` for every message
// type declared in that dictionary into the checked-in `golden.csv`.
//
// THE ONE RULE THAT MATTERS (same rule as 075's generator): every required-
// tag entry this program writes to golden.csv is the literal, measured
// output of a real `isRequiredField()` call against a real, locally built
// QuickFIX DataDictionary. Nothing here is hand-authored or back-filled.
//
// ─────────────────────────────────────────────────────────────────────────
// SCOPE — body-only, NO header/trailer union (escalated, see phase report):
//
// `isRequiredField(msgType, tag)` has NO header/trailer-required surface at
// all. Verified directly against DataDictionary.cpp: header/trailer fields
// are read into `m_headerFields`/`m_trailerFields` (keyed by field number,
// independent of msgType — see `addHeaderField`/`addTrailerField`,
// DataDictionary.cpp:298-306/334-352) and NEVER into
// `m_requiredFields[msgType]` (populated only from the `<message>` body via
// `addRequiredField`, DataDictionary.cpp:396/511/560/570). So this golden
// carries ONLY the message-BODY component-AND required set (the exact
// surface Contract 2 exists to corroborate: "confirms the independent
// walker encodes the AND-rule faithfully").
//
// contracts/census-and-agreement.md's Contract 2 text ("Header/trailer
// fields appear as ordinary required fields in the per-message set") is
// CONTRADICTED by this reading of the real QuickFIX source — there is no
// live QuickFIX API surface that reports a header/trailer field as
// message-required, ordinary or otherwise. Per
// [[feedback_parity_corpus_row_needs_a_surface_the_reference_engine_has]],
// this generator does not fabricate agreement on a surface QuickFIX lacks:
// the parity test (T019) compares this golden against a BODY-ONLY oracle
// (`build_quickfix_oracle(path, /*include_header_trailer=*/false)`); the
// StandardHeader/Trailer carve-out itself stays pinned by Contract 1's
// census (T015-T017) only. Flagged to the orchestrator; does not block.
//
// ─────────────────────────────────────────────────────────────────────────
// Candidate tag universe: for each dict, a TRIVIAL (non-recursive) flat scan
// of the <fields> block finds the maximum declared field `number`; every
// (msgType, tag) pair for tag in [1, max_tag] is tested via
// `isRequiredField()`. This is NOT the recursive component-AND walker (no
// duplication of that logic here — this scan computes nothing about
// required-ness, group/component nesting, or the AND-rule; it exists solely
// to bound the enumeration, since DataDictionary exposes no public iterator
// over its own field/message-type sets). Message types are enumerated by
// the same kind of trivial flat scan of the <messages> block. Mathematically
// this over-approximates the "per-message field set" Contract 2's text
// mentions, but is equivalent: `isRequiredField(msgType, tag)` is false for
// any tag not applicable to that message regardless of how the candidate
// universe was bounded.
//
// ─────────────────────────────────────────────────────────────────────────
// 081-strict-validation-residuals T020 — per-group golden (Concern B,
// contracts/census-and-parity.md "Parity golden contract"): ALSO records, for
// every real group context QuickFIX itself discovers, the group's own DIRECT
// required-member set, measured via `FIX::DataDictionary::isGroup`/`getGroup`
// (DataDictionary.h:286/298) recursively descending into the returned
// sub-`DataDictionary`, then `isRequiredField(msgType, tag)` on THAT sub-DD.
// Non-circular: both context EXISTENCE (`isGroup`/`getGroup`) and
// required-ness (`isRequiredField` on the sub-DD) are real, measured
// QuickFIX API calls — no independent structural re-scan is needed for
// group discovery (unlike the message-type/max-tag candidate universe
// above, which bounds a `1..max_tag` probe range only). Because
// `addXMLGroup`/`addGroup` (DataDictionary.cpp:536-587) thread the ORIGINAL
// enclosing message's `msgtype` string unchanged through every nesting
// level, and register HEADER/TRAILER groups under the literal keys
// `"_header_"`/`"_trailer_"` (not the real msgType), calling
// `dd.isGroup(msg_type, tag)` with the real message type naturally scopes
// this walk to message-BODY groups only — the same body-only surface the
// message-level golden above corroborates (isRequiredField has no
// header/trailer surface at all).
//
// Written to a SEPARATE file, `golden_groups.csv` (NOT appended to
// golden.csv): the message-level golden's 3-column row shape
// ("dictionary,msg_type,required_tags") is consumed by an
// already-landed 3-field CSV parser (`required_scope_parity_test.cpp`);
// mixing in a 5-column group row would break that parser and the
// message-level parity test's byte-stability. Format:
//   "dictionary,msg_type,group_path,no_tag,required_tags"
// `group_path` = space-separated ancestor no_tags, OUTER-TO-INNER order
// (excludes this group's own no_tag; empty string for a depth-1 group),
// quoted like `required_tags`. Mirrors
// `tests/dictionary/required_scope_oracle.hpp`'s `GroupContextKey`
// convention (path excludes no_tag itself) so the parity test can build the
// exact same key type for both-directions context-set comparison.
// ─────────────────────────────────────────────────────────────────────────
// Manifest hash formats (mirrors 075's tools/quickfix_enum_golden/main.cpp):
//
//   All hashes are standard SHA-1, 40 lowercase hex characters, computed via
//   OpenSSL's EVP_Digest(..., EVP_sha1(), ...).
//
//   dictionary_sha1[D]      = SHA-1 over the raw bytes of dictionaries/D.xml
//                             on disk, for each of the 9 dicts.
//   generator_source_hash   = SHA-1 over the raw bytes of THIS FILE
//                             (tools/quickfix_required_golden/main.cpp).
//   candidate_universe_hash = SHA-1 over the concatenation, for the 9 dicts
//                             in the fixed order below, of the exact bytes
//                               "{dict}|{max_tag}|{msgtype1},{msgtype2},...\n"
//                             (msgtypes sorted ascending, comma-separated,
//                             no trailing comma) — pins the derived
//                             enumeration inputs (distinct from the raw
//                             dictionary bytes: catches a scan-logic change
//                             even when dictionary_sha1 is unchanged).
//   golden_output_hash      = SHA-1 over the concatenation, for the 9 dicts
//                             in the fixed order below and messages in
//                             ascending msgtype order within each dict, of
//                             the exact bytes
//                               "{dict}|{msg_type}|{tag1} {tag2} ...\n"
//                             (required tags ascending, space-separated, or
//                             empty for a message with zero body-level
//                             required tags). Computed over the OUTPUT ROWS
//                             ONLY — excludes the manifest block itself.
//
// ─────────────────────────────────────────────────────────────────────────

#include <quickfix/DataDictionary.h>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ── SHA-1 helpers (byte-identical algorithm to 075's generator) ────────────

std::string hex_encode(const unsigned char *data, unsigned int len) {
    static const char *kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

std::string sha1_hex(const std::string &bytes) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        std::cerr << "FATAL: EVP_MD_CTX_new failed\n";
        std::exit(1);
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, bytes.data(), bytes.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        std::cerr << "FATAL: OpenSSL SHA-1 computation failed\n";
        EVP_MD_CTX_free(ctx);
        std::exit(1);
    }
    EVP_MD_CTX_free(ctx);
    return hex_encode(digest, digest_len);
}

std::string read_file_bytes(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "FATAL: cannot open '" << path << "' to hash it\n";
        std::exit(1);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ── The 9 QuickFIX-schema dicts (Contract 2 scope — NO vlatest/Orchestra) ──

struct DictCase {
    std::string label;   // e.g. "FIX44" — also the golden's `dictionary` column
    std::string filename; // e.g. "FIX44.xml"
};

std::vector<DictCase> const kDicts{
    {"FIX40", "FIX40.xml"},
    {"FIX41", "FIX41.xml"},
    {"FIX42", "FIX42.xml"},
    {"FIX43", "FIX43.xml"},
    {"FIX44", "FIX44.xml"},
    {"FIX50", "FIX50.xml"},
    {"FIX50SP1", "FIX50SP1.xml"},
    {"FIX50SP2", "FIX50SP2.xml"},
    {"FIXT11", "FIXT11.xml"},
};

// Trivial (non-recursive) structural scan: message-type list + max declared
// field number. NOT the component-AND walker — computes nothing about
// required-ness.
struct DictScan {
    std::vector<std::string> msg_types;  // ascending
    std::uint32_t max_tag = 0;
};

// 081 T020 — one per-group golden row: (dict, msg_type, ancestor path
// outer-to-inner excluding this group's own no_tag, this group's own
// no_tag) -> its DIRECT required-member tags, all measured via real
// QuickFIX `isGroup`/`getGroup`/`isRequiredField` calls (see the T020
// header-comment banner above).
struct GroupRow {
    std::string dict;
    std::string msg_type;
    std::vector<std::uint16_t> path;
    std::uint16_t no_tag = 0;
    std::vector<std::uint16_t> required;  // ascending
};

// NOLINTNEXTLINE(misc-no-recursion) — recursive descent through real
// QuickFIX sub-DataDictionary objects (getGroup), depth bounded by the
// dictionary's own real nesting depth. Appends one GroupRow per real group
// context found, pre-order (tag-ascending at each level, depth-first) --
// this fixes `golden_groups_output_hash`'s row emission order.
void walk_groups(FIX::DataDictionary const &cur_dd, std::string const &dict_label, std::string const &msg_type,
                  std::uint32_t max_tag, std::vector<std::uint16_t> &path, std::vector<GroupRow> &rows_out) {
    for (std::uint32_t tag = 1; tag <= max_tag; ++tag) {
        if (!cur_dd.isGroup(msg_type, static_cast<int>(tag))) {
            continue;
        }
        int delim = 0;
        FIX::DataDictionary const *sub = nullptr;
        if (!cur_dd.getGroup(msg_type, static_cast<int>(tag), delim, sub) || sub == nullptr) {
            continue;  // isGroup already said true; defensive only, should not happen
        }

        std::vector<std::uint16_t> req;
        for (std::uint32_t t2 = 1; t2 <= max_tag; ++t2) {
            if (sub->isRequiredField(msg_type, static_cast<int>(t2))) {
                req.push_back(static_cast<std::uint16_t>(t2));
            }
        }

        GroupRow row;
        row.dict = dict_label;
        row.msg_type = msg_type;
        row.path = path;
        row.no_tag = static_cast<std::uint16_t>(tag);
        row.required = std::move(req);
        rows_out.push_back(std::move(row));

        path.push_back(static_cast<std::uint16_t>(tag));
        walk_groups(*sub, dict_label, msg_type, max_tag, path, rows_out);
        path.pop_back();
    }
}

DictScan scan_dict(std::filesystem::path const &xml_path) {
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        std::cerr << "FATAL: pugixml failed to load " << xml_path << "\n";
        std::exit(1);
    }
    auto const root = doc.child("fix");

    DictScan scan;
    for (auto const &f : root.child("fields").children("field")) {
        auto const num = f.attribute("number").as_uint();
        if (num > scan.max_tag) {
            scan.max_tag = num;
        }
    }
    std::set<std::string> msg_types_set;
    for (auto const &m : root.child("messages").children("message")) {
        msg_types_set.insert(std::string{m.attribute("msgtype").as_string("")});
    }
    scan.msg_types.assign(msg_types_set.begin(), msg_types_set.end());
    return scan;
}

}  // namespace

int main() {
#ifndef FIXPP_REQUIRED_GOLDEN_DICT_DIR
#error "FIXPP_REQUIRED_GOLDEN_DICT_DIR must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_GENERATOR_SOURCE
#error "FIXPP_GOLDEN_GENERATOR_SOURCE must be defined by CMake"
#endif
#ifndef FIXPP_REQUIRED_GOLDEN_OUTPUT_CSV
#error "FIXPP_REQUIRED_GOLDEN_OUTPUT_CSV must be defined by CMake"
#endif
#ifndef FIXPP_REQUIRED_GOLDEN_GROUPS_OUTPUT_CSV
#error "FIXPP_REQUIRED_GOLDEN_GROUPS_OUTPUT_CSV must be defined by CMake"
#endif

    std::filesystem::path const dict_dir = FIXPP_REQUIRED_GOLDEN_DICT_DIR;
    std::string const generator_source_hash = sha1_hex(read_file_bytes(FIXPP_GOLDEN_GENERATOR_SOURCE));

    struct DictOut {
        std::string label;
        std::string dict_sha1;
        std::map<std::string, std::vector<std::uint16_t>> required_by_msg;  // ascending tags
    };
    std::vector<DictOut> out;
    out.reserve(kDicts.size());

    // 081 T020: per-group golden rows, accumulated across all dicts/messages.
    std::vector<GroupRow> group_rows;

    std::ostringstream candidate_buf;
    std::ostringstream output_buf;

    for (auto const &dc : kDicts) {
        auto const xml_path = dict_dir / dc.filename;
        std::string const dict_sha1 = sha1_hex(read_file_bytes(xml_path.string()));
        auto const scan = scan_dict(xml_path);

        candidate_buf << dc.label << "|" << scan.max_tag << "|";
        for (std::size_t i = 0; i < scan.msg_types.size(); ++i) {
            if (i != 0) {
                candidate_buf << ",";
            }
            candidate_buf << scan.msg_types[i];
        }
        candidate_buf << "\n";

        // Real, measured QuickFIX load — the topology fixpp's own loader has
        // (single dict, no session/app split) applied per dict.
        FIX::DataDictionary dd(xml_path.string());

        DictOut d_out;
        d_out.label = dc.label;
        d_out.dict_sha1 = dict_sha1;

        for (auto const &msg_type : scan.msg_types) {
            std::vector<std::uint16_t> req;
            for (std::uint32_t tag = 1; tag <= scan.max_tag; ++tag) {
                if (dd.isRequiredField(msg_type, static_cast<int>(tag))) {
                    req.push_back(static_cast<std::uint16_t>(tag));
                }
            }
            d_out.required_by_msg.emplace(msg_type, req);

            output_buf << dc.label << "|" << msg_type << "|";
            for (std::size_t i = 0; i < req.size(); ++i) {
                if (i != 0) {
                    output_buf << " ";
                }
                output_buf << req[i];
            }
            output_buf << "\n";

            // 081 T020: per-group golden rows for this message, measured via
            // real QuickFIX isGroup/getGroup/isRequiredField (see walk_groups).
            std::vector<std::uint16_t> path;
            walk_groups(dd, dc.label, msg_type, scan.max_tag, path, group_rows);
        }

        std::cout << "[" << dc.label << "] " << scan.msg_types.size() << " message type(s), max_tag="
                  << scan.max_tag << ", dictionary_sha1=" << dict_sha1 << "\n";
        out.push_back(std::move(d_out));
    }

    std::string const candidate_universe_hash = sha1_hex(candidate_buf.str());
    std::string const golden_output_hash = sha1_hex(output_buf.str());

    // 081 T020: serialize group_rows (emitted in dict-then-message-then-
    // pre-order-depth-first order, per walk_groups' own iteration order) and
    // hash them the same way as golden_output_hash above.
    std::ostringstream group_output_buf;
    for (auto const &row : group_rows) {
        group_output_buf << row.dict << "|" << row.msg_type << "|";
        for (std::size_t i = 0; i < row.path.size(); ++i) {
            if (i != 0) {
                group_output_buf << " ";
            }
            group_output_buf << row.path[i];
        }
        group_output_buf << "|" << row.no_tag << "|";
        for (std::size_t i = 0; i < row.required.size(); ++i) {
            if (i != 0) {
                group_output_buf << " ";
            }
            group_output_buf << row.required[i];
        }
        group_output_buf << "\n";
    }
    std::string const golden_groups_output_hash = sha1_hex(group_output_buf.str());

    std::ofstream out_file(FIXPP_REQUIRED_GOLDEN_OUTPUT_CSV, std::ios::binary | std::ios::trunc);
    if (!out_file) {
        std::cerr << "FATAL: cannot open '" << FIXPP_REQUIRED_GOLDEN_OUTPUT_CSV << "' for writing\n";
        return 1;
    }

    out_file << "# 079-required-presence-scope -- QuickFIX required-set parity golden (Contract 2, "
                 "T018/T019, fixpp#201)\n";
    out_file << "# Generated by tools/quickfix_required_golden/main.cpp against a REAL, locally built\n";
    out_file << "# QuickFIX v1.16.0. Every required_tags value below is the literal measured output of\n";
    out_file << "# FIX::DataDictionary::isRequiredField(msgType, tag) -- never hand-authored.\n";
    out_file << "#\n";
    out_file << "# SCOPE: body-only (message-level component-AND set). isRequiredField() has NO\n";
    out_file << "# header/trailer-required surface (verified against DataDictionary.cpp -- see this\n";
    out_file << "# generator's main.cpp header comment); the StandardHeader/Trailer carve-out is\n";
    out_file << "# pinned separately by Contract 1's census (tests/dictionary/required_scope_census_test.cpp),\n";
    out_file << "# NOT by this golden. NO vlatest/Orchestra row (QuickFIX 1.16.0 cannot parse Orchestra).\n";
    out_file << "#\n";
    out_file << "# MANIFEST:\n";
    out_file << "# quickfix_version=1.16.0\n";
    out_file << "# quickfix_soname=libquickfix.so.17.0.0\n";
    for (auto const &d : out) {
        out_file << "# dictionary_sha1[" << d.label << "]=" << d.dict_sha1 << "\n";
    }
    out_file << "# generator_source_hash=" << generator_source_hash << "\n";
    out_file << "# candidate_universe_hash=" << candidate_universe_hash << "\n";
    out_file << "# golden_output_hash=" << golden_output_hash << "\n";
    out_file << "# golden_groups_output_hash=" << golden_groups_output_hash << " (081 T020 -- see the\n";
    out_file << "#   sibling golden_groups.csv, this dict/message/group-run's per-group golden)\n";
    out_file << "#\n";
    out_file << "# Hash algorithm: SHA-1 (OpenSSL EVP_sha1), 40 lowercase hex chars.\n";
    out_file << "# dictionary_sha1: over the raw bytes of dictionaries/<D>.xml.\n";
    out_file << "# generator_source_hash: over the raw bytes of this file, main.cpp.\n";
    out_file << "# candidate_universe_hash: over the concatenation, for the 9 dicts in the fixed\n";
    out_file << "#   kDicts order, of the exact bytes \"{dict}|{max_tag}|{msgtype1},{msgtype2},...\\n\"\n";
    out_file << "#   (msgtypes sorted ascending, comma-separated).\n";
    out_file << "# golden_output_hash: over the concatenation, for the 9 dicts in kDicts order and\n";
    out_file << "#   messages in ascending msgtype order, of the exact bytes\n";
    out_file << "#   \"{dict}|{msg_type}|{tag1} {tag2} ...\\n\" (required tags ascending, space-\n";
    out_file << "#   separated, empty for zero body-required tags). Computed over the OUTPUT ROWS\n";
    out_file << "#   ONLY -- excludes this manifest block, so it is not self-referential.\n";
    out_file << "#\n";
    out_file << "dictionary,msg_type,required_tags\n";

    auto csv_quote = [](std::string const &s) {
        std::string q = "\"";
        for (char c : s) {
            if (c == '"') {
                q += "\"\"";
            } else {
                q += c;
            }
        }
        q += "\"";
        return q;
    };

    for (auto const &d : out) {
        for (auto const &[msg_type, req] : d.required_by_msg) {
            std::ostringstream tags;
            for (std::size_t i = 0; i < req.size(); ++i) {
                if (i != 0) {
                    tags << " ";
                }
                tags << req[i];
            }
            out_file << d.label << "," << msg_type << "," << csv_quote(tags.str()) << "\n";
        }
    }

    out_file.close();

    // 081 T020: per-group golden -- SEPARATE file (see the header-comment
    // banner above for why it is not appended to golden.csv).
    std::ofstream group_file(FIXPP_REQUIRED_GOLDEN_GROUPS_OUTPUT_CSV, std::ios::binary | std::ios::trunc);
    if (!group_file) {
        std::cerr << "FATAL: cannot open '" << FIXPP_REQUIRED_GOLDEN_GROUPS_OUTPUT_CSV << "' for writing\n";
        return 1;
    }

    group_file << "# 081-strict-validation-residuals -- QuickFIX PER-GROUP required-member parity "
                  "golden (Concern B, T020, contracts/census-and-parity.md)\n";
    group_file << "# Generated by tools/quickfix_required_golden/main.cpp against a REAL, locally built\n";
    group_file << "# QuickFIX v1.16.0. Every row's (existence AND required_tags) is the literal measured\n";
    group_file << "# output of FIX::DataDictionary::isGroup/getGroup (DataDictionary.h:286/298) recursively\n";
    group_file << "# descended, then isRequiredField(msgType, tag) called on the returned sub-DataDictionary\n";
    group_file << "# -- never hand-authored, never derived from the independent oracle.\n";
    group_file << "#\n";
    group_file << "# SCOPE: body-only, same rationale as golden.csv (addXMLGroup/addGroup register\n";
    group_file << "# header/trailer groups under the literal keys \"_header_\"/\"_trailer_\", not the real\n";
    group_file << "# msgType, so probing isGroup(msg_type, tag) with the real message type naturally\n";
    group_file << "# excludes them). NO vlatest/Orchestra row (QuickFIX 1.16.0 cannot parse Orchestra).\n";
    group_file << "#\n";
    group_file << "# MANIFEST:\n";
    group_file << "# quickfix_version=1.16.0\n";
    group_file << "# quickfix_soname=libquickfix.so.17.0.0\n";
    for (auto const &d : out) {
        group_file << "# dictionary_sha1[" << d.label << "]=" << d.dict_sha1 << "\n";
    }
    group_file << "# generator_source_hash=" << generator_source_hash << "\n";
    group_file << "# candidate_universe_hash=" << candidate_universe_hash << "\n";
    group_file << "# golden_groups_output_hash=" << golden_groups_output_hash << "\n";
    group_file << "#\n";
    group_file << "# Hash algorithm: SHA-1 (OpenSSL EVP_sha1), 40 lowercase hex chars (see golden.csv's\n";
    group_file << "# own manifest comment for dictionary_sha1/generator_source_hash/candidate_universe_hash\n";
    group_file << "# definitions -- identical here).\n";
    group_file << "# golden_groups_output_hash: over the concatenation, in walk_groups' own emission\n";
    group_file << "# order (dict-then-message per kDicts/ascending-msgtype order, pre-order depth-first\n";
    group_file << "# within a message), of the exact bytes\n";
    group_file << "#   \"{dict}|{msg_type}|{path tag1 tag2 ...}|{no_tag}|{req tag1 tag2 ...}\\n\"\n";
    group_file << "# (path = ancestor no_tags outer-to-inner, space-separated, empty for a depth-1 group;\n";
    group_file << "# required tags ascending, space-separated). Computed over the OUTPUT ROWS ONLY.\n";
    group_file << "#\n";
    group_file << "dictionary,msg_type,group_path,no_tag,required_tags\n";

    auto csv_quote_path = [](std::vector<std::uint16_t> const &path) {
        std::ostringstream oss;
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (i != 0) {
                oss << " ";
            }
            oss << path[i];
        }
        return "\"" + oss.str() + "\"";
    };

    for (auto const &row : group_rows) {
        std::ostringstream tags;
        for (std::size_t i = 0; i < row.required.size(); ++i) {
            if (i != 0) {
                tags << " ";
            }
            tags << row.required[i];
        }
        group_file << row.dict << "," << row.msg_type << "," << csv_quote_path(row.path) << "," << row.no_tag
                    << "," << csv_quote(tags.str()) << "\n";
    }

    group_file.close();

    std::cout << "\nWrote " << FIXPP_REQUIRED_GOLDEN_OUTPUT_CSV << "\n";
    std::cout << "generator_source_hash=" << generator_source_hash << "\n";
    std::cout << "candidate_universe_hash=" << candidate_universe_hash << "\n";
    std::cout << "golden_output_hash=" << golden_output_hash << "\n";
    std::cout << "\nWrote " << FIXPP_REQUIRED_GOLDEN_GROUPS_OUTPUT_CSV << " (" << group_rows.size()
              << " group context row(s))\n";
    std::cout << "golden_groups_output_hash=" << golden_groups_output_hash << "\n";

    return 0;
}
