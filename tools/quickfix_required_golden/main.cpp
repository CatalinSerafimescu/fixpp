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

    std::filesystem::path const dict_dir = FIXPP_REQUIRED_GOLDEN_DICT_DIR;
    std::string const generator_source_hash = sha1_hex(read_file_bytes(FIXPP_GOLDEN_GENERATOR_SOURCE));

    struct DictOut {
        std::string label;
        std::string dict_sha1;
        std::map<std::string, std::vector<std::uint16_t>> required_by_msg;  // ascending tags
    };
    std::vector<DictOut> out;
    out.reserve(kDicts.size());

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
        }

        std::cout << "[" << dc.label << "] " << scan.msg_types.size() << " message type(s), max_tag="
                  << scan.max_tag << ", dictionary_sha1=" << dict_sha1 << "\n";
        out.push_back(std::move(d_out));
    }

    std::string const candidate_universe_hash = sha1_hex(candidate_buf.str());
    std::string const golden_output_hash = sha1_hex(output_buf.str());

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

    std::cout << "\nWrote " << FIXPP_REQUIRED_GOLDEN_OUTPUT_CSV << "\n";
    std::cout << "generator_source_hash=" << generator_source_hash << "\n";
    std::cout << "candidate_universe_hash=" << candidate_universe_hash << "\n";
    std::cout << "golden_output_hash=" << golden_output_hash << "\n";

    return 0;
}
