// tests/wire/enum_golden_manifest_test.cpp
//
// 075-live-wire-enum-validation — T007. CI manifest gate for the QuickFIX
// enum-domain golden (`tools/quickfix_enum_golden/golden.csv`, FR-024).
//
// This binary links NO QuickFIX and runs WITHOUT `reference-engines/`
// present — it only hashes checked-in files (dictionaries + the generator's
// own main.cpp) and re-parses the checked-in golden.csv, then compares the
// recomputed hashes against the manifest block golden.csv embeds. It is a
// pure tree-consistency gate.
//
// What this gate catches, exactly (narrowed at Gate A round 2, O2-3 — do not
// overstate it):
//   (i)  TREE DRIFT — a dictionary / corpus-input / generator-source change
//        that golden.csv was not regenerated against (any of the five
//        input-side hashes goes stale).
//   (ii) A CARELESS HAND-EDIT of the checked-in verdict/reason/ref_tag_id/
//        asserted columns — this is what `golden_output_hash` exists to
//        catch. The other five manifest fields (three dictionary_sha1, the
//        generator hash, the corpus input hash) ALL stay valid under such an
//        edit — none of them are computed over the verdict/RefTagID columns
//        — so without a hash over them specifically, this gate would be
//        decorative. 075 T005a extended `golden_output_hash` to also cover
//        `quickfix_ref_tag_id` (it originally covered only
//        verdict/reason/asserted): a hand-edited RefTagID was invisible to
//        every manifest field until this extension.
//
// What this gate CANNOT catch: drift against a NEWER QuickFIX release.
// `quickfix_version`/`quickfix_soname` are recorded in the manifest but are
// UNVERIFIABLE here — this CI binary has no QuickFIX by the design's own
// premise (FR-024 item 2). Detecting QuickFIX-version drift is the
// regeneration-and-diff target's job (T009), bound to the release-interop
// checklist, not this test's.

#include <gtest/gtest.h>

#include <openssl/evp.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ── SHA-1 helper (independent re-implementation from the generator's, same
// algorithm — OpenSSL EVP_sha1 — so this is a genuine cross-check, not a
// shared-bug risk masquerading as independent verification). ────────────────
std::string sha1_hex(const std::string &bytes) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EXPECT_NE(ctx, nullptr);
    if (!ctx) {
        return {};
    }
    int ok = EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
    ok = ok && EVP_DigestUpdate(ctx, bytes.data(), bytes.size());
    ok = ok && EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);
    EXPECT_TRUE(ok);
    if (!ok) {
        return {};
    }
    static const char *kHex = "0123456789abcdef";
    std::string out;
    out.reserve(digest_len * 2);
    for (unsigned int i = 0; i < digest_len; ++i) {
        out.push_back(kHex[(digest[i] >> 4) & 0xF]);
        out.push_back(kHex[digest[i] & 0xF]);
    }
    return out;
}

std::string read_file_bytes(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ADD_FAILURE() << "cannot open '" << path << "'";
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> read_lines(const std::string &path) {
    std::vector<std::string> lines;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ADD_FAILURE() << "cannot open '" << path << "'";
        return lines;
    }
    std::string line;
    while (std::getline(f, line)) {
        // Tolerate a stray CR if the file was ever touched on a CRLF
        // checkout (git core.autocrlf=false is expected, but do not let the
        // gate itself become the platform trap — [[feedback_byte_exact...]]
        // is about the HASHED input files, not this parser).
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

// Minimal CSV field splitter matching the generator's own quoting scheme
// (RFC4180-style: a field wrapped in double quotes, internal quotes doubled).
std::vector<std::string> split_csv_line(const std::string &line) {
    std::vector<std::string> fields;
    std::string cur;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                cur += c;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
    }
    fields.push_back(cur);
    return fields;
}

struct GoldenRow {
    std::string id;
    std::string dictionary;
    std::string begin_string;
    std::string msg_type;
    std::string tag;
    std::string value;
    std::string asserted;
    std::string verdict;
    std::string reason;
    std::string ref_tag_id;  // 075 T005a
    std::string note;
};

struct ParsedGolden {
    std::map<std::string, std::string> manifest;  // key -> value, from "# key=value" lines
    std::vector<GoldenRow> rows;
};

// Parses the manifest comment block ("# key=value" lines) and the CSV body
// of golden.csv. This is intentionally independent of
// tools/quickfix_enum_golden/main.cpp's own parsing (there is none there —
// main.cpp only WRITES the file) so there is no shared-bug risk between
// producer and consumer.
ParsedGolden parse_golden_csv(const std::string &path) {
    ParsedGolden result;
    std::vector<std::string> lines = read_lines(path);

    std::vector<std::string> csv_lines;
    for (const std::string &line : lines) {
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            // "# key=value" manifest line. Prose comment lines (using ":"
            // rather than "=", or with no "=" at all) are harmlessly
            // skipped; a stray "=" inside prose (e.g. "PossDupFlag(43)=X"
            // in the free-text notes) produces a spurious map entry under a
            // key nobody queries, which is inert.
            std::string body = line.substr(1);
            std::size_t start = body.find_first_not_of(' ');
            if (start == std::string::npos) {
                continue;
            }
            body = body.substr(start);
            std::size_t eq = body.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            result.manifest[body.substr(0, eq)] = body.substr(eq + 1);
            continue;
        }
        csv_lines.push_back(line);
    }

    // First non-comment line is the CSV header; skip it.
    if (csv_lines.empty()) {
        ADD_FAILURE() << "golden.csv has no CSV body after stripping manifest comments";
        return result;
    }
    for (std::size_t i = 1; i < csv_lines.size(); ++i) {
        std::vector<std::string> f = split_csv_line(csv_lines[i]);
        if (f.size() != 11) {
            ADD_FAILURE() << "golden.csv row " << i << " has " << f.size() << " fields, expected 11: '"
                           << csv_lines[i] << "'";
            continue;
        }
        GoldenRow row;
        row.id = f[0];
        row.dictionary = f[1];
        row.begin_string = f[2];
        row.msg_type = f[3];
        row.tag = f[4];
        row.value = f[5];
        row.asserted = f[6];
        row.verdict = f[7];
        row.reason = f[8];
        row.ref_tag_id = f[9];
        row.note = f[10];
        result.rows.push_back(std::move(row));
    }
    return result;
}

// Recomputes corpus_input_hash per the format documented in
// tools/quickfix_enum_golden/main.cpp: for row ids ascending, the exact
// bytes "{id}|{dictionary}|{msg_type}|{tag}|{value}\n", concatenated.
std::string recompute_corpus_input_hash(const std::vector<GoldenRow> &rows) {
    std::ostringstream buf;
    for (const GoldenRow &r : rows) {
        buf << r.id << "|" << r.dictionary << "|" << r.msg_type << "|" << r.tag << "|" << r.value << "\n";
    }
    return sha1_hex(buf.str());
}

// Recomputes golden_output_hash per the same documented format: for row ids
// ascending, the exact bytes "{id}|{verdict}|{reason}|{ref_tag_id}|{asserted}\n".
// THIS IS THE LOAD-BEARING HASH (FR-024): it is the only one of the six
// manifest fields computed over the verdict/reason/ref_tag_id/asserted
// columns, so it is the only one that can catch a hand-edited verdict OR a
// hand-edited RefTagID (075 T005a) — every other field stays byte-identical
// under such an edit.
std::string recompute_golden_output_hash(const std::vector<GoldenRow> &rows) {
    std::ostringstream buf;
    for (const GoldenRow &r : rows) {
        buf << r.id << "|" << r.verdict << "|" << r.reason << "|" << r.ref_tag_id << "|" << r.asserted << "\n";
    }
    return sha1_hex(buf.str());
}

std::string manifest_value(const ParsedGolden &g, const std::string &key) {
    auto it = g.manifest.find(key);
    if (it == g.manifest.end()) {
        ADD_FAILURE() << "manifest key '" << key << "' not found in golden.csv's comment block";
        return {};
    }
    return it->second;
}

}  // namespace

#ifndef FIXPP_GOLDEN_CSV_PATH
#error "FIXPP_GOLDEN_CSV_PATH must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_MAIN_CPP_PATH
#error "FIXPP_GOLDEN_MAIN_CPP_PATH must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_DICT_FIX44_PATH
#error "FIXPP_GOLDEN_DICT_FIX44_PATH must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_DICT_FIX41_PATH
#error "FIXPP_GOLDEN_DICT_FIX41_PATH must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_DICT_FIX42_PATH
#error "FIXPP_GOLDEN_DICT_FIX42_PATH must be defined by CMake"
#endif

namespace {

class EnumGoldenManifestTest : public ::testing::Test {
   protected:
    void SetUp() override { golden_ = parse_golden_csv(FIXPP_GOLDEN_CSV_PATH); }

    ParsedGolden golden_;
};

TEST_F(EnumGoldenManifestTest, CorpusHasThirteenRows) {
    // Sanity precondition for every other test here: T006's rebase (row 6
    // swapped to MessageEncoding(347), row 13 added as the DV-5
    // characterization row) makes this 13, not FR-018's original 12.
    ASSERT_EQ(golden_.rows.size(), 13u);
}

TEST_F(EnumGoldenManifestTest, DictionarySha1Fix44Matches) {
    std::string recomputed = sha1_hex(read_file_bytes(FIXPP_GOLDEN_DICT_FIX44_PATH));
    EXPECT_EQ(recomputed, manifest_value(golden_, "dictionary_sha1[FIX44]"))
        << "dictionaries/FIX44.xml changed without regenerating golden.csv (tree drift)";
}

TEST_F(EnumGoldenManifestTest, DictionarySha1Fix41Matches) {
    std::string recomputed = sha1_hex(read_file_bytes(FIXPP_GOLDEN_DICT_FIX41_PATH));
    EXPECT_EQ(recomputed, manifest_value(golden_, "dictionary_sha1[FIX41]"))
        << "dictionaries/FIX41.xml changed without regenerating golden.csv (tree drift)";
}

TEST_F(EnumGoldenManifestTest, DictionarySha1Fix42Matches) {
    std::string recomputed = sha1_hex(read_file_bytes(FIXPP_GOLDEN_DICT_FIX42_PATH));
    EXPECT_EQ(recomputed, manifest_value(golden_, "dictionary_sha1[FIX42]"))
        << "dictionaries/FIX42.xml changed without regenerating golden.csv (tree drift)";
}

TEST_F(EnumGoldenManifestTest, GeneratorSourceHashMatches) {
    std::string recomputed = sha1_hex(read_file_bytes(FIXPP_GOLDEN_MAIN_CPP_PATH));
    EXPECT_EQ(recomputed, manifest_value(golden_, "generator_source_hash"))
        << "tools/quickfix_enum_golden/main.cpp changed (config pin or corpus) without regenerating "
           "golden.csv (tree drift)";
}

TEST_F(EnumGoldenManifestTest, CorpusInputHashMatches) {
    std::string recomputed = recompute_corpus_input_hash(golden_.rows);
    EXPECT_EQ(recomputed, manifest_value(golden_, "corpus_input_hash"))
        << "a row's dictionary/msg_type/tag/value changed without regenerating golden.csv (tree drift)";
}

// ⚠️ THE LOAD-BEARING CHECK (FR-024). Every OTHER hash in this file stays
// valid if someone hand-edits a quickfix_verdict/quickfix_reason/
// quickfix_ref_tag_id/asserted column in the checked-in CSV — this is the
// only one that is computed over those columns, so it is the only one that
// can catch that class of edit. Proven RED under a verdict/reason mutation
// at T008 (quickstart S-7 step 5), and RE-PROVEN RED under a
// RefTagID-ONLY mutation at T005a (verdict and reason left untouched) —
// before T005a's hash extension, that specific edit was invisible to every
// field in this file.
TEST_F(EnumGoldenManifestTest, GoldenOutputHashMatches) {
    std::string recomputed = recompute_golden_output_hash(golden_.rows);
    EXPECT_EQ(recomputed, manifest_value(golden_, "golden_output_hash"))
        << "a checked-in quickfix_verdict/quickfix_reason/quickfix_ref_tag_id/asserted value was "
           "hand-edited (or the corpus changed) without regenerating golden.csv from a real QuickFIX run";
}

}  // namespace
