// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/enum_golden_parity_test.cpp
//
// 075-live-wire-enum-validation — T031 (SC-009/FR-018/FR-019). The SC-009
// PARITY GATE.
//
// Drives fixpp's PRODUCTION validation path (real shipped dictionaries ->
// Dictionary::as_table_view() -> dictionary_driven_validator::validate())
// over the exact 13-row corpus recorded in
// tools/quickfix_enum_golden/golden.csv (a REAL QuickFIX v1.16.0 measurement,
// FR-018/FR-019/FR-024) and compares fixpp's verdict.
//
// Standalone binary (LABELS "075;golden;parity") per [const §VII.8]: this is
// a data-driven exact gate consuming the checked-in golden + manifest, like
// its T007 sibling enum_golden_manifest_test.cpp. It links NO QuickFIX and
// never touches reference-engines/ — only the checked-in golden.csv and the
// checked-in dictionaries/*.xml, so it builds and runs identically in CI
// (which has no reference-engines/ tree) with FIXPP_BUILD_QUICKFIX_GOLDEN=OFF
// (the default).
//
// THE RULE (tasks.md T031):
//   - On every `asserted: true` row, fixpp's (verdict, reason, ref_tag_id)
//     MUST match the golden's (quickfix_verdict, quickfix_reason,
//     quickfix_ref_tag_id) exactly. Divergence there is a DEFECT.
//   - `asserted: false` rows are the DV-1..DV-5 divergence register
//     (contracts/enum-domain.md C-6) — deliberate, argued differences.
//     fixpp's outcome is RECORDED (printed) on those rows, never asserted
//     against the golden.
//
// Mutation (verified manually, see tasks.md T031 report): reverting
// table_view::enum_valid() -> `return true` must turn EVERY `asserted: true`
// *reject* row RED (rows 2, 4, 5, 6, 7, 12). Rows 1 and 3 are the paired
// positive controls against over-rejection (T001's standing per-row
// obligation) and must stay GREEN under that same mutation — a
// reject-asserting row that BOTH engines already accept under the Phase-1
// stub would be a defect in the corpus, not a row; there is none here,
// which is exactly what the mutation proves by process of elimination.
//
// Anchors:
//   spec: specs/075-live-wire-enum-validation/spec.md SC-009/FR-018/FR-019
//   tasks: specs/075-live-wire-enum-validation/tasks.md T031
//   contract: specs/075-live-wire-enum-validation/contracts/enum-domain.md C-6
//   golden: tools/quickfix_enum_golden/golden.csv

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory_resource>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/reject_reason_map.hpp>
#include <fixpp/wire/validator.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::wire_error_to_session_reject_reason;

// ── golden.csv parsing (deliberately independent of enum_golden_manifest_
// test.cpp's copy and of tools/quickfix_enum_golden/main.cpp's writer — no
// shared-bug risk between producer and either consumer). Manifest-integrity
// hashing is T007's job; this file only needs the row data. ─────────────────

struct GoldenRow {
    int id = 0;
    std::string dictionary;
    std::string begin_string;
    std::string msg_type;
    int tag = 0;
    std::string value;
    bool asserted = false;
    std::string verdict;              // "accept" | "reject" | "" (n/a)
    std::optional<int> reason;        // empty for accept
    std::optional<int> ref_tag_id;    // empty for accept
    std::string note;
};

std::vector<std::string> read_lines(std::string const& path) {
    std::vector<std::string> lines;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ADD_FAILURE() << "cannot open '" << path << "'";
        return lines;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> split_csv_line(std::string const& line) {
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

std::vector<GoldenRow> parse_golden_csv(std::string const& path) {
    std::vector<GoldenRow> rows;
    std::vector<std::string> lines = read_lines(path);
    std::vector<std::string> csv_lines;
    for (auto const& line : lines) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        csv_lines.push_back(line);
    }
    if (csv_lines.empty()) {
        ADD_FAILURE() << "golden.csv has no CSV body after stripping comments";
        return rows;
    }
    for (std::size_t i = 1; i < csv_lines.size(); ++i) {
        auto f = split_csv_line(csv_lines[i]);
        if (f.size() != 11) {
            ADD_FAILURE() << "golden.csv row " << i << " has " << f.size() << " fields, expected 11";
            continue;
        }
        GoldenRow row;
        row.id = std::stoi(f[0]);
        row.dictionary = f[1];
        row.begin_string = f[2];
        row.msg_type = f[3];
        row.tag = std::stoi(f[4]);
        row.value = f[5];
        row.asserted = (f[6] == "true");
        row.verdict = f[7];
        row.reason = f[8].empty() ? std::nullopt : std::optional<int>{std::stoi(f[8])};
        row.ref_tag_id = f[9].empty() ? std::nullopt : std::optional<int>{std::stoi(f[9])};
        row.note = f[10];
        rows.push_back(std::move(row));
    }
    return rows;
}

// ── frame-building helpers (mirrors validator_enum_domain_test.cpp) ─────────

std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

std::vector<std::byte> make_frame(std::string_view begin_string, std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=" + std::string{begin_string} + "\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

MessageView<access_mode::Index> parse_index(std::vector<std::byte> const& buf,
                                            std::array<std::byte, 4096>& stack_buf,
                                            std::pmr::monotonic_buffer_resource& arena_out) {
    new (&arena_out) std::pmr::monotonic_buffer_resource{stack_buf.data(), stack_buf.size(),
                                                         std::pmr::null_memory_resource()};
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed";
        return {};
    }
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena_out);
    if (!mv.has_value()) {
        ADD_FAILURE() << "parser.parse failed";
        return {};
    }
    return std::move(*mv);
}

// Loads a real shipped dictionary and returns its table_view — the same
// production path golden.csv's rows are meant to characterize
// (Dictionary::as_table_view(), dictionary.hpp:193-205; the enum-domain
// table owns copies of the code bytes, so this legally outlives the
// Dictionary going out of scope at the end of this function).
fixpp::dict::table_view load_shipped_table_view(char const* filename) {
    std::vector<std::byte> buf(8u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;
    auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    return dict.as_table_view();
}

constexpr std::string_view kSendingTime = "20260714-12:00:00";

// FIX44 standard header: BeginString/BodyLength/CheckSum are framer-owned;
// this covers the 5 remaining required header fields (35/49/56/34/52).
std::string fix_header(std::string_view msg_type) {
    std::string s = "35=";
    s += msg_type;
    s += "\x01";
    s += "49=SENDER\x01";
    s += "56=TARGET\x01";
    s += "34=1\x01";
    s += "52=";
    s += kSendingTime;
    s += "\x01";
    return s;
}

// NewOrderSingle(D) required fields beyond the header: ClOrdID(11),
// TransactTime(60), OrdType(40) [Side(54) is the field under test on rows
// 1/2/8; rows 3/4/5/9 also set it explicitly]. Instrument/OrderQtyData are
// required COMPONENTS but declare zero required LEAF fields (measured,
// mirrors validator_enum_domain_test.cpp's nos_prefix()).
std::string nos_required() {
    std::string s = "11=CLORDID1\x01";
    s += "40=1\x01";  // OrdType=Market, in-domain
    s += "60=";
    s += kSendingTime;
    s += "\x01";
    return s;
}

// ── per-row frame builders ───────────────────────────────────────────────

std::vector<std::byte> row1_frame() {  // Side(54)=1 -> accept
    return make_frame("FIX.4.4", fix_header("D") + nos_required() + "54=1\x01");
}
std::vector<std::byte> row2_frame() {  // Side(54)=Z -> reject/5/54
    return make_frame("FIX.4.4", fix_header("D") + nos_required() + "54=Z\x01");
}
std::vector<std::byte> row3_frame() {  // ExecInst(18)="1 G 6" -> accept
    return make_frame("FIX.4.4", fix_header("D") + nos_required() + "54=1\x01" + "18=1 G 6\x01");
}
std::vector<std::byte> row4_frame() {  // ExecInst(18)="1 ZZ 6" -> reject/5/18
    return make_frame("FIX.4.4", fix_header("D") + nos_required() + "54=1\x01" + "18=1 ZZ 6\x01");
}
std::vector<std::byte> row5_frame() {  // ExecInst(18)="1  G" (double-space) -> reject/5/18
    return make_frame("FIX.4.4", fix_header("D") + nos_required() + "54=1\x01" + "18=1  G\x01");
}
std::vector<std::byte> row6_frame() {  // MessageEncoding(347)=ZZZZ (header) -> reject/5/347
    return make_frame("FIX.4.4", fix_header("0") + "347=ZZZZ\x01");
}
std::vector<std::byte> row7_frame() {  // MatchType(574)=A on TradeCaptureReport -> reject/5/574
    std::string body = fix_header("AE");
    body += "571=TR1\x01";  // TradeReportID (required)
    body += "570=N\x01";    // PreviouslyReported (required, BOOLEAN, unconstrained by type)
    body += "32=100\x01";   // LastQty (required, Float)
    body += "31=10.5\x01";  // LastPx (required, Float)
    body += "75=20260714\x01";       // TradeDate (required, String-coarse)
    body += "60=";
    body += kSendingTime;
    body += "\x01";          // TransactTime (required)
    body += "574=A\x01";     // MatchType under test
    body += "552=1\x01";     // NoSides (required group, count=1)
    body += "54=1\x01";      // Side (required group delimiter, in-domain)
    body += "37=ORDER1\x01"; // OrderID (required group member)
    return make_frame("FIX.4.4", body);
}
std::vector<std::byte> row8_frame() {  // Side(54)="" (empty x Char) -- asserted:false, DV-1
    return make_frame("FIX.4.4", fix_header("D") + nos_required() + "54=\x01");
}
std::vector<std::byte> row9_frame() {  // ExecInst(18)="" (empty x String) -- asserted:false, DV-2
    return make_frame("FIX.4.4", fix_header("D") + nos_required() + "54=1\x01" + "18=\x01");
}
std::vector<std::byte> row10_frame() {  // PartyRole(452)=9999 group member -- asserted:false, DV-3
    std::string body = fix_header("D") + nos_required() + "54=1\x01";
    body += "453=1\x01";     // NoPartyIDs=1
    body += "448=PID1\x01";  // PartyID (delimiter)
    body += "447=D\x01";     // PartyIDSource -- valid
    body += "452=9999\x01";  // PartyRole -- out-of-domain (under test)
    return make_frame("FIX.4.4", body);
}
std::vector<std::byte> row11_frame() {  // PartySubIDType(803)=999 nested -- asserted:false, DV-3
    std::string body = fix_header("D") + nos_required() + "54=1\x01";
    body += "453=1\x01";
    body += "448=PID1\x01";
    body += "447=D\x01";
    body += "452=1\x01";     // PartyRole -- valid
    body += "802=1\x01";     // NoPartySubIDs=1 (nested group)
    body += "523=SUB1\x01";  // PartySubID (nested delimiter)
    body += "803=999\x01";   // PartySubIDType -- out-of-domain (under test)
    return make_frame("FIX.4.4", body);
}
std::vector<std::byte> row12_frame() {  // SettlLocation(166)=US on FIX41 SettlementInstructions
    // 79 (AllocAccount) and 162 (SettlInstID) are, per dictionaries/FIX41.xml,
    // declared type='CHAR' with no enum children -- single-char, Floor-1
    // accept regardless of content. Deliberately DIFFERENT literal values
    // from tools/quickfix_enum_golden/main.cpp's QuickFIX-side fill (which
    // uses multi-char "SI1"/"ACC1" -- QuickFIX's CharConvertor leniency
    // there is orthogonal to this row's assertion, which is about tag 166
    // only): fixpp's own Char type-arm requires exactly one byte, so a
    // multi-char value here would trip the TYPE arm before ever reaching the
    // enum check on 166, corrupting the isolation this row needs.
    std::string body = fix_header("T");
    body += "160=0\x01";   // SettlInstMode (required, CHAR, enum {0,1,2,3})
    body += "162=A\x01";   // SettlInstID (required, CHAR, no enum)
    body += "163=N\x01";   // SettlInstTransType (required, CHAR, enum {C,N,R})
    body += "165=1\x01";   // SettlInstSource (required, CHAR, enum {1,2})
    body += "79=B\x01";    // AllocAccount (required, CHAR, no enum)
    body += "60=";
    body += kSendingTime;
    body += "\x01";  // TransactTime (required)
    body += "166=US\x01";  // SettlLocation under test
    return make_frame("FIX.4.1", body);
}
std::vector<std::byte> row13_frame() {  // PossDupFlag(43)=X (header, BOOLEAN) -- asserted:false, DV-5
    return make_frame("FIX.4.4", fix_header("0") + "43=X\x01");
}

struct RowSpec {
    int id;
    char const* dict_file;
    std::vector<std::byte> (*build_frame)();
};

constexpr std::array<RowSpec, 13> kRowSpecs{{
    {1, "FIX44.xml", &row1_frame},
    {2, "FIX44.xml", &row2_frame},
    {3, "FIX44.xml", &row3_frame},
    {4, "FIX44.xml", &row4_frame},
    {5, "FIX44.xml", &row5_frame},
    {6, "FIX44.xml", &row6_frame},
    {7, "FIX44.xml", &row7_frame},
    {8, "FIX44.xml", &row8_frame},
    {9, "FIX44.xml", &row9_frame},
    {10, "FIX44.xml", &row10_frame},
    {11, "FIX44.xml", &row11_frame},
    {12, "FIX41.xml", &row12_frame},
    {13, "FIX44.xml", &row13_frame},
}};

struct FixppOutcome {
    std::string verdict;             // "accept" | "reject"
    std::optional<int> reason;
    std::optional<int> ref_tag_id;
};

FixppOutcome run_row(RowSpec const& spec) {
    auto tv = load_shipped_table_view(spec.dict_file);
    dictionary_driven_validator v{std::move(tv)};

    auto frame = spec.build_frame();
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(frame, stack, arena);

    std::array<std::byte, 2048> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);

    FixppOutcome out;
    if (result.has_value()) {
        out.verdict = "accept";
    } else {
        out.verdict = "reject";
        out.reason = wire_error_to_session_reject_reason(result.error());
        out.ref_tag_id = static_cast<int>(ref_tag);
    }
    return out;
}

}  // namespace

#ifndef FIXPP_GOLDEN_CSV_PATH
#error "FIXPP_GOLDEN_CSV_PATH must be defined by CMake"
#endif
#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be defined by CMake"
#endif

namespace {

// ⚠️ THE PARITY GATE (T031). Loads golden.csv (checked-in, no QuickFIX
// linked) and drives fixpp's production validate() over the exact same
// 13-row corpus. asserted:true rows are ASSERTED against the golden;
// asserted:false rows (the DV-1..DV-5 divergence register) are RECORDED via
// stdout only, matching contracts/enum-domain.md C-6.
TEST(EnumGoldenParity, MatchesQuickFixOnAssertedRowsAndRecordsDivergences) {
    auto rows = parse_golden_csv(FIXPP_GOLDEN_CSV_PATH);
    ASSERT_EQ(rows.size(), 13u) << "golden.csv row count drifted from the T031-pinned 13-row corpus";
    ASSERT_EQ(kRowSpecs.size(), rows.size());

    std::cout << "\n=== T031 SC-009 parity gate: fixpp vs QuickFIX v1.16.0 golden ===\n";

    for (auto const& row : rows) {
        SCOPED_TRACE(testing::Message() << "row " << row.id << " (" << row.note << ")");
        auto const it = std::find_if(kRowSpecs.begin(), kRowSpecs.end(),
                                     [&](RowSpec const& s) { return s.id == row.id; });
        ASSERT_NE(it, kRowSpecs.end()) << "no RowSpec for golden row id " << row.id;

        FixppOutcome const outcome = run_row(*it);

        std::string const golden_reason = row.reason ? std::to_string(*row.reason) : "-";
        std::string const golden_ref = row.ref_tag_id ? std::to_string(*row.ref_tag_id) : "-";
        std::string const fixpp_reason = outcome.reason ? std::to_string(*outcome.reason) : "-";
        std::string const fixpp_ref = outcome.ref_tag_id ? std::to_string(*outcome.ref_tag_id) : "-";

        std::cout << "row " << row.id << " [" << (row.asserted ? "asserted " : "recorded ")
                  << "] quickfix=(" << row.verdict << "," << golden_reason << "," << golden_ref
                  << ") fixpp=(" << outcome.verdict << "," << fixpp_reason << "," << fixpp_ref
                  << ")\n";

        if (!row.asserted) {
            // DV-1..DV-5 divergence register: RECORDED, never asserted.
            continue;
        }

        // ── asserted:true — divergence from the reference engine is a DEFECT.
        EXPECT_EQ(outcome.verdict, row.verdict) << "verdict mismatch on row " << row.id;
        EXPECT_EQ(outcome.reason, row.reason) << "reason mismatch on row " << row.id;
        EXPECT_EQ(outcome.ref_tag_id, row.ref_tag_id) << "ref_tag_id mismatch on row " << row.id;
    }
}

}  // namespace
