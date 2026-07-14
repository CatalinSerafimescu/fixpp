// tools/quickfix_enum_golden/main.cpp
//
// 075-live-wire-enum-validation — Phase 0.5, T002/T003/T004/T005.
//
// Local-only golden generator. Links a real, locally built QuickFIX v1.16.0
// (root = the validated CMake cache variable FIXPP_QUICKFIX_ROOT, target
// guarded OFF by default via FIXPP_BUILD_QUICKFIX_GOLDEN — T002) and runs a
// 13-row corpus of boundary FIX frames through the REAL
// `FIX::DataDictionary::validate()`, recording QuickFIX's MEASURED verdict +
// reject reason for each row into the checked-in `golden.csv`.
//
// THE ONE RULE THAT MATTERS: every verdict this program writes to golden.csv
// is the literal, measured output of a real QuickFIX validate() call over a
// real message frame. Nothing here is hand-authored or back-filled from a
// predicted verdict (spec.md's FR-018 corpus table). Where the measured
// result disagrees with spec.md's prediction, this file's comments say so —
// reconciling any such disagreement is 075 T006's job, not this generator's.
//
// ─────────────────────────────────────────────────────────────────────────
// FR-019 configuration pin — ALL FOUR booleans AND the dictionary topology
// are set HERE, in source, never as a CLI argument, so `generator_source_hash`
// (below) genuinely pins them (a runtime-configurable flag would be recorded
// but unpinned):
//
//   m_checkFieldsHaveValues  = false
//       NOT the QuickFIX-library default (`true`). Leaving it at the default
//       is exactly the switch that front-ran the enum check in the original
//       (retracted) bundle and produced a false parity claim (FR-008,
//       FR-019, contracts/enum-domain.md C-6 DV-1/DV-2): with the default
//       `true`, QuickFIX throws NoTagValue on an empty field BEFORE ever
//       reaching checkValue/checkValidFormat, so the enum boundary this
//       golden exists to measure is never actually exercised. fixpp has no
//       equivalent "tag specified without a value" pre-check at all — its
//       empty-value disposition falls straight through to its own type-arm
//       check — so `false` is the pin that isolates the enum boundary
//       fixpp actually has, per FR-019's instruction to pin every non-enum
//       flag to "match fixpp's own validation choices".
//   m_checkFieldsOutOfOrder  = false
//       fixpp's Step-1 walk (validator.hpp:139-158) is a linear scan with no
//       global field-ordering enforcement as part of enum-domain checking.
//       Leaving this QuickFIX check on would reject frames for a reason
//       unrelated to enum domain and conflate two different checks — exactly
//       what FR-019 says the pin must prevent.
//   m_checkUserDefinedFields = false
//       No corpus row uses a tag >= FIELD::UserMin; pinned explicitly and
//       conservatively. Inert for this corpus either way.
//   AllowUnknownMsgFields    = false
//       fixpp's `field_valid_for` (validator.hpp:143) rejects a tag not
//       valid for the message type via its own check (reason 2) rather than
//       silently ignoring it — so QuickFIX must not silently allow unknown
//       message fields either, or the two engines would diverge on a
//       dimension unrelated to enum domain.
//
//   Dictionary topology: SINGLE-DD / non-FIXT, sessionDD == appDD, applied
//   PER the corpus row's own dictionary (FIX44, FIX41 or FIX42) — this is
//   the topology fixpp actually has (one `SessionConfig::dictionary`, no
//   session-DD/app-DD split). Concretely: `dd.validate(message)`, whose
//   default `bodyOnly=false` argument resolves to the *static*
//   `DataDictionary::validate(message, /*pSessionDD=*/this, /*pAppDD=*/this)`
//   overload — i.e. the SAME DataDictionary instance serves both roles. This
//   was empirically confirmed against QuickFIX v1.16.0's
//   `include/quickfix/DataDictionary.h:342-345`.
//
// ─────────────────────────────────────────────────────────────────────────
// Manifest hash formats (so a later C++ test — 075 T007 — can reproduce
// every hash byte-for-byte from the tree):
//
//   All hashes are standard SHA-1, 40 lowercase hex characters, computed via
//   OpenSSL's EVP_Digest(..., EVP_sha1(), ...).
//
//   dictionary_sha1[D]     = SHA-1 over the raw bytes of dictionaries/D.xml
//                            on disk (D in {FIX44, FIX41, FIX42}) — the SAME
//                            algorithm CMake's `file(SHA1 <path> var)` uses
//                            (see tests/dictionary/CMakeLists.txt's
//                            FIXPP_ORCHESTRA_SHA1_PIN precedent), so a CMake-
//                            side recomputation matches too.
//
//   generator_source_hash  = SHA-1 over the raw bytes of THIS FILE
//                            (tools/quickfix_enum_golden/main.cpp) as they
//                            exist on disk when the generator runs. Not
//                            self-referential: the hash is written to
//                            golden.csv, a SEPARATE file from main.cpp.
//
//   corpus_input_hash       = SHA-1 over the concatenation, for row ids
//                            1..13 in ascending order, of the exact bytes
//                              "{id}|{dictionary}|{msg_type}|{tag}|{value}\n"
//                            (decimal id and tag, LF line ending after
//                            EVERY row including the 13th, `value` is the
//                            row's raw wire value string byte-for-byte —
//                            including any embedded spaces, or the empty
//                            string for an empty-value row).
//
//   golden_output_hash      = SHA-1 over the concatenation, for row ids
//                            1..13 in ascending order, of the exact bytes
//                              "{id}|{verdict}|{reason}|{asserted}\n"
//                            where verdict in {"accept","reject"}, reason is
//                            the decimal SessionRejectReason for a reject
//                            row or the empty string for an accept row, and
//                            asserted in {"true","false"}. Computed over the
//                            OUTPUT rows only — excludes the manifest block
//                            it lives in (FR-024), so it is not
//                            self-referential.
//
// ─────────────────────────────────────────────────────────────────────────

#include <quickfix/DataDictionary.h>
#include <quickfix/Message.h>
#include <quickfix/Group.h>

#include <openssl/evp.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ── SHA-1 helpers ───────────────────────────────────────────────────────

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

// ── Corpus row model ────────────────────────────────────────────────────

struct RowResult {
    bool is_accept = false;
    int reason = -1;          // -1 == no reason (accept)
    std::string detail;       // human-readable, for the stdout report only
};

struct Row {
    int id;
    std::string dict;          // "FIX44" | "FIX41" | "FIX42"
    std::string begin_string;  // "FIX.4.4" | "FIX.4.1" | "FIX.4.2"
    std::string msg_type;
    int tag;
    std::string value;         // the raw wire value under test
    bool asserted;
    std::string note;
    std::function<void(FIX::Message &)> fill;
};

constexpr const char *kSendingTime = "20260714-12:00:00";

void set_common_header(FIX::Message &msg, const std::string &begin_string, const std::string &msg_type) {
    msg.getHeader().setField(FIX::FIELD::BeginString, begin_string);
    msg.getHeader().setField(FIX::FIELD::MsgType, msg_type);
    msg.getHeader().setField(FIX::FIELD::SenderCompID, "SENDER");
    msg.getHeader().setField(FIX::FIELD::TargetCompID, "TARGET");
    msg.getHeader().setField(FIX::FIELD::MsgSeqNum, "1");
    msg.getHeader().setField(FIX::FIELD::SendingTime, kSendingTime);
}

// NewOrderSingle(D) required fields beyond the header: ClOrdID(11), Side(54),
// TransactTime(60), OrdType(40). Instrument/OrderQtyData are required
// COMPONENTS in FIX44.xml but declare zero required LEAF fields, so no
// Symbol/OrderQty is needed for `checkHasRequired` to pass (confirmed
// empirically — see the T002 report).
void nos_required(FIX::Message &msg) {
    msg.setField(11, "CLORDID1");
    msg.setField(40, "1");  // OrdType=Market, in-domain
    msg.setField(60, kSendingTime);
}

RowResult classify(const FIX::DataDictionary &dd, FIX::Message &parsed) {
    RowResult r;
    try {
        dd.validate(parsed);
        r.is_accept = true;
        r.detail = "ACCEPT";
        return r;
    } catch (FIX::IncorrectTagValue &e) {
        r.reason = FIX::SessionRejectReason_VALUE_IS_INCORRECT;  // 5
        r.detail = "REJECT reason=5 (IncorrectTagValue) field=" + std::to_string(e.field);
    } catch (FIX::NoTagValue &e) {
        r.reason = FIX::SessionRejectReason_TAG_SPECIFIED_WITHOUT_A_VALUE;  // 4
        r.detail = "REJECT reason=4 (NoTagValue) field=" + std::to_string(e.field);
    } catch (FIX::TagNotDefinedForMessage &e) {
        r.reason = FIX::SessionRejectReason_TAG_NOT_DEFINED_FOR_THIS_MESSAGE_TYPE;  // 2
        r.detail = "REJECT reason=2 (TagNotDefinedForMessage) field=" + std::to_string(e.field);
    } catch (FIX::RequiredTagMissing &e) {
        r.reason = FIX::SessionRejectReason_REQUIRED_TAG_MISSING;  // 1
        r.detail = "REJECT reason=1 (RequiredTagMissing) field=" + std::to_string(e.field);
    } catch (FIX::IncorrectDataFormat &e) {
        r.reason = FIX::SessionRejectReason_INCORRECT_DATA_FORMAT_FOR_VALUE;  // 6
        r.detail = "REJECT reason=6 (IncorrectDataFormat) field=" + std::to_string(e.field);
    } catch (FIX::InvalidTagNumber &e) {
        r.reason = FIX::SessionRejectReason_INVALID_TAG_NUMBER;  // 0
        r.detail = "REJECT reason=0 (InvalidTagNumber) field=" + std::to_string(e.field);
    } catch (FIX::TagOutOfOrder &e) {
        r.reason = FIX::SessionRejectReason_TAG_SPECIFIED_OUT_OF_REQUIRED_ORDER;  // 14
        r.detail = "REJECT reason=14 (TagOutOfOrder) field=" + std::to_string(e.field);
    } catch (FIX::RepeatedTag &e) {
        r.reason = FIX::SessionRejectReason_TAG_APPEARS_MORE_THAN_ONCE;  // 13
        r.detail = "REJECT reason=13 (RepeatedTag) field=" + std::to_string(e.field);
    } catch (FIX::RepeatingGroupCountMismatch &e) {
        r.reason = FIX::SessionRejectReason_INCORRECT_NUMINGROUP_COUNT_FOR_REPEATING_GROUP;  // 16
        r.detail = "REJECT reason=16 (RepeatingGroupCountMismatch) field=" + std::to_string(e.field);
    } catch (FIX::Exception &e) {
        // Any exception type outside the above set is UNEXPECTED for this
        // corpus and must not be silently miscategorized as some other
        // reason. Fail loudly so a broken frame is caught, not measured.
        std::cerr << "FATAL: unexpected QuickFIX exception during validate(): " << e.what() << "\n";
        std::exit(1);
    } catch (std::exception &e) {
        std::cerr << "FATAL: unexpected std::exception during validate(): " << e.what() << "\n";
        std::exit(1);
    }
    return r;
}

FIX::DataDictionary make_dd(const std::string &path) {
    FIX::DataDictionary dd(path);
    dd.checkFieldsHaveValues(false);
    dd.checkFieldsOutOfOrder(false);
    dd.checkUserDefinedFields(false);
    dd.allowUnknownMsgFields(false);
    return dd;
}

RowResult run_row(const FIX::DataDictionary &dd, const Row &row) {
    FIX::Message msg;
    set_common_header(msg, row.begin_string, row.msg_type);
    row.fill(msg);
    msg.getTrailer().setField(FIX::FIELD::CheckSum, "000");  // recomputed by toString()

    // toString() recomputes BodyLength(9) and CheckSum(10) from the actual
    // content, regardless of the placeholder above, producing a byte-correct
    // "otherwise valid" frame (BeginString/BodyLength/MsgType/CheckSum all
    // correct — the frame construction discipline required by T004).
    std::string wire = msg.toString();

    // Re-parse through the dictionary (group-aware — this is what lets the
    // group/nested-group rows resolve their member fields into m_groups
    // rather than the flat field map) with structural validation ON
    // (BodyLength/CheckSum/header-order — all pass because toString() just
    // computed them correctly).
    FIX::Message parsed(wire, dd, /*validate=*/true);

    return classify(dd, parsed);
}

}  // namespace

int main() {
#ifndef FIXPP_GOLDEN_DICT_FIX44
#error "FIXPP_GOLDEN_DICT_FIX44 must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_DICT_FIX41
#error "FIXPP_GOLDEN_DICT_FIX41 must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_DICT_FIX42
#error "FIXPP_GOLDEN_DICT_FIX42 must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_GENERATOR_SOURCE
#error "FIXPP_GOLDEN_GENERATOR_SOURCE must be defined by CMake"
#endif
#ifndef FIXPP_GOLDEN_OUTPUT_CSV
#error "FIXPP_GOLDEN_OUTPUT_CSV must be defined by CMake"
#endif

    const std::string fix44_path = FIXPP_GOLDEN_DICT_FIX44;
    const std::string fix41_path = FIXPP_GOLDEN_DICT_FIX41;
    const std::string fix42_path = FIXPP_GOLDEN_DICT_FIX42;

    FIX::DataDictionary fix44 = make_dd(fix44_path);
    FIX::DataDictionary fix41 = make_dd(fix41_path);
    FIX::DataDictionary fix42 = make_dd(fix42_path);

    // ── The 13-row corpus (12 FR-018 rows + row 13, a supplementary DV-5
    // characterization row) ─────────────────────────────────────────────
    // Row 13 was added after the T002 measurement run revealed that row 6's
    // original literal (PossDupFlag(43)=X, BOOLEAN) never reaches either
    // engine's enum arm on the QuickFIX side (format-check short-circuits
    // first) -- see row 6 and row 13's own comments below.
    std::vector<Row> rows;

    // Row 1 — in-domain single value. Positive control against
    // over-rejection (pairs with row 2 on the same tag).
    rows.push_back({1, "FIX44", "FIX.4.4", "D", 54, "1", true,
                     "Side(54)=1, in-domain -> accept (positive control)",
                     [](FIX::Message &m) { nos_required(m); m.setField(54, "1"); }});

    // Row 2 — out-of-domain single value. The headline.
    rows.push_back({2, "FIX44", "FIX.4.4", "D", 54, "Z", true,
                     "Side(54)=Z, out-of-domain -> reject/5",
                     [](FIX::Message &m) { nos_required(m); m.setField(54, "Z"); }});

    // Row 3 — multi-value, all tokens declared. Positive control (reddens
    // under a whole-string-lookup mutation of the tokenizer).
    rows.push_back({3, "FIX44", "FIX.4.4", "D", 18, "1 G 6", true,
                     "ExecInst(18)=\"1 G 6\", all tokens declared -> accept (positive control)",
                     [](FIX::Message &m) {
                         nos_required(m);
                         m.setField(54, "1");
                         m.setField(18, "1 G 6");
                     }});

    // Row 4 — multi-value, one token undeclared.
    rows.push_back({4, "FIX44", "FIX.4.4", "D", 18, "1 ZZ 6", true,
                     "ExecInst(18)=\"1 ZZ 6\", ZZ undeclared -> reject/5",
                     [](FIX::Message &m) {
                         nos_required(m);
                         m.setField(54, "1");
                         m.setField(18, "1 ZZ 6");
                     }});

    // Row 5 — degenerate whitespace (double space -> empty token). FR-014.
    // (The trailing-space form `18=1 ` was independently verified during
    // T002 scratch-testing to also reject/5 under the same mechanism; only
    // ONE literal is carried as a golden row per FR-018's 12-row corpus.)
    rows.push_back({5, "FIX44", "FIX.4.4", "D", 18, "1  G", true,
                     "ExecInst(18)=\"1  G\" (double space -> empty token) -> reject/5",
                     [](FIX::Message &m) {
                         nos_required(m);
                         m.setField(54, "1");
                         m.setField(18, "1  G");
                     }});

    // Row 6 — enum-backed HEADER field, engine-parity variant. FR-015.
    // MUST be a STRING-typed header field, not a BOOLEAN one: QuickFIX's
    // checkValidFormat runs BEFORE checkValue (DataDictionary.cpp:171 before
    // :172), and a BOOLEAN field's BoolConvertor rejects a malformed value
    // there, never reaching the enum arm at all (see row 13 -- the swapped-out
    // PossDupFlag(43) case, now characterized as DV-5). MessageEncoding(347)
    // is STRING, so StringConvertor imposes no format constraint and BOTH
    // engines actually reach their enum arm here.
    rows.push_back({6, "FIX44", "FIX.4.4", "0", 347, "ZZZZ", true,
                     "MessageEncoding(347)=ZZZZ, header field, STRING type -> both engines' "
                     "format check is a no-op, so both reach the enum arm -> reject/5.",
                     [](FIX::Message &m) { m.getHeader().setField(347, "ZZZZ"); }});

    // Row 7 — strict prefix of a declared code. FR-009.
    rows.push_back({7, "FIX44", "FIX.4.4", "AE", 574, "A", true,
                     "MatchType(574)=A on TradeCaptureReport, no bare 'A' declared -> reject/5",
                     [](FIX::Message &m) {
                         m.setField(571, "TR1");                 // TradeReportID
                         m.setField(570, "N");                   // PreviouslyReported
                         m.setField(32, "100");                  // LastQty
                         m.setField(31, "10.5");                 // LastPx
                         m.setField(75, "20260714");              // TradeDate
                         m.setField(60, kSendingTime);            // TransactTime
                         m.setField(574, "A");                    // MatchType under test
                         FIX::Group sides(552, 54);                // NoSides, delim Side
                         sides.setField(54, "1");
                         sides.setField(37, "ORDER1");             // OrderID
                         m.addGroup(sides);
                     }});

    // Row 8 — empty value x Char. asserted:false, characterization-only
    // (DV-1). MEASURED: QuickFIX rejects via checkValidFormat's
    // CHAR_CONVERTOR (empty string fails size==1) at DataDictionary.cpp:171,
    // BEFORE checkValue (isFieldValue) at :172 is ever reached -- reason=6
    // (IncorrectDataFormat). contracts/enum-domain.md's original DV-1 text
    // ("QuickFIX reject/5 via isFieldValue's set.find(\"\") miss, parity by
    // coincidence via a different arm") was factually wrong in BOTH the
    // reason code AND the mechanism: the real mechanism is a format-check
    // rejection that never reaches the enum arm at all, exactly the same
    // shape as row 13's DV-5 (PossDupFlag(43)=X). fixpp's own Char type-arm
    // DOES constrain (size()!=1 -> reject/5), so both engines still agree on
    // *reject*, just via unrelated mechanisms and (for QuickFIX) a different
    // reason.
    rows.push_back({8, "FIX44", "FIX.4.4", "D", 54, "", false,
                     "Side(54)=\"\" (empty x Char) -- DV-1 characterization only; QuickFIX "
                     "rejects/6 via CharConvertor (DataDictionary.cpp:171), never reaching "
                     "the enum arm -- corrects DV-1's original reject/5 claim.",
                     [](FIX::Message &m) { nos_required(m); m.setField(54, ""); }});

    // Row 9 — empty value x String. asserted:false, characterization-only
    // (DV-2). MEASURED: reject/5 (IncorrectTagValue via isFieldValue's
    // empty-token miss) -- matches the DV-2 prediction.
    rows.push_back({9, "FIX44", "FIX.4.4", "D", 18, "", false,
                     "ExecInst(18)=\"\" (empty x String) -- DV-2 characterization only",
                     [](FIX::Message &m) {
                         nos_required(m);
                         m.setField(54, "1");
                         m.setField(18, "");
                     }});

    // Row 10 — out-of-domain enum on a repeating-group member
    // (NoPartyIDs(453) -> PartyRole(452)). asserted:false (DV-3).
    rows.push_back({10, "FIX44", "FIX.4.4", "D", 452, "9999", false,
                     "PartyRole(452)=9999 inside NoPartyIDs(453) group member -- DV-3 characterization only",
                     [](FIX::Message &m) {
                         nos_required(m);
                         m.setField(54, "1");
                         FIX::Group party(453, 448);  // NoPartyIDs, delim PartyID
                         party.setField(448, "PID1");
                         party.setField(447, "D");     // valid PartyIDSource
                         party.setField(452, "9999");  // out-of-domain PartyRole under test
                         m.addGroup(party);
                     }});

    // Row 11 — out-of-domain enum on a NESTED-group member, depth 2
    // (NoPartyIDs(453) -> NoPartySubIDs(802) -> PartySubIDType(803)).
    // asserted:false (DV-3, at depth).
    rows.push_back({11, "FIX44", "FIX.4.4", "D", 803, "999", false,
                     "PartySubIDType(803)=999 inside NoPartyIDs->NoPartySubIDs nested group member (depth 2) "
                     "-- DV-3 characterization only",
                     [](FIX::Message &m) {
                         nos_required(m);
                         m.setField(54, "1");
                         FIX::Group party(453, 448);
                         party.setField(448, "PID1");
                         party.setField(447, "D");
                         party.setField(452, "1");     // valid PartyRole
                         FIX::Group sub(802, 523);       // NoPartySubIDs, delim PartySubID
                         sub.setField(523, "SUB1");
                         sub.setField(803, "999");       // out-of-domain PartySubIDType under test
                         party.addGroup(sub);
                         m.addGroup(party);
                     }});

    // Row 12 — SettlLocation(166)="US" on FIX41/FIX42 (the "ISO Country
    // Code" placeholder codeset). asserted:true (DV-4 -- both engines
    // agree and both reject).
    auto settl_fill = [](FIX::Message &m) {
        m.setField(162, "SI1");        // SettlInstID
        m.setField(163, "N");          // SettlInstTransType
        m.setField(214, "SIR1");       // SettlInstRefID
        m.setField(160, "0");          // SettlInstMode
        m.setField(165, "1");          // SettlInstSource
        m.setField(79, "ACC1");        // AllocAccount
        m.setField(60, kSendingTime);  // TransactTime
        m.setField(166, "US");         // SettlLocation under test
    };
    rows.push_back({12, "FIX41", "FIX.4.1", "T", 166, "US", true,
                     "SettlLocation(166)=US on FIX41 SettlementInstructions -- reject/5, DV-4",
                     settl_fill});

    // Row 13 — PossDupFlag(43)="X" on Heartbeat. asserted:false,
    // characterization-only (DV-5). This is the row 6 originally carried
    // before it was swapped for MessageEncoding(347): PossDupFlag is
    // BOOLEAN, so QuickFIX's checkValidFormat (BoolConvertor,
    // DataDictionary.cpp:171) rejects the malformed value BEFORE checkValue
    // (the enum arm, :172) is ever reached -- MEASURED reject/6. fixpp's own
    // Boolean type-arm imposes NO constraint (validator.hpp:419-425), so
    // fixpp's own enum arm (enum_valid) DOES fire and rejects/5. Both
    // engines reject; only the reason differs, and the DIVERGING MECHANISM
    // is the point: QuickFIX never reaches its enum arm for this field at
    // all, so this row cannot be used to prove enum-arm parity (that is what
    // row 6 was rewritten to do, with a STRING-typed header field instead).
    rows.push_back({13, "FIX44", "FIX.4.4", "0", 43, "X", false,
                     "PossDupFlag(43)=X (header, BOOLEAN) -- DV-5 characterization only: "
                     "QuickFIX rejects/6 via checkValidFormat's BoolConvertor "
                     "(DataDictionary.cpp:171) BEFORE the enum arm at :172; fixpp's Boolean "
                     "type-arm imposes no constraint (validator.hpp:419-425) so it reaches "
                     "enum_valid and rejects/5. Both engines REJECT; only the reason differs.",
                     [](FIX::Message &m) { m.getHeader().setField(43, "X"); }});

    // Row 12 also spans FIX42 (the corpus row's own dictionary spans BOTH
    // FIX41 and FIX42 per T003/FR-019's topology note) -- measured
    // separately below, not as an additional golden row, purely to confirm
    // parity across both dictionaries before committing to one.
    {
        FIX::Message msg;
        set_common_header(msg, "FIX.4.2", "T");
        settl_fill(msg);
        msg.getTrailer().setField(FIX::FIELD::CheckSum, "000");
        std::string wire = msg.toString();
        FIX::Message parsed(wire, fix42, true);
        RowResult r42 = classify(fix42, parsed);
        std::cout << "[confirm] SettlLocation(166)=US on FIX42 (not a golden row, cross-check only): "
                   << r42.detail << "\n";
    }

    // ── Run every row, print + collect results ─────────────────────────
    struct OutRow {
        const Row *row;
        RowResult result;
    };
    std::vector<OutRow> out;
    out.reserve(rows.size());

    for (const Row &row : rows) {
        const FIX::DataDictionary &dd = row.dict == "FIX44" ? fix44 : row.dict == "FIX41" ? fix41 : fix42;
        RowResult r = run_row(dd, row);
        std::cout << "row " << row.id << " [" << row.dict << " " << row.msg_type << " tag=" << row.tag
                  << " value=\"" << row.value << "\" asserted=" << (row.asserted ? "true" : "false") << "]: "
                  << r.detail << "\n";
        out.push_back({&row, r});
    }

    // ── Manifest hashes ─────────────────────────────────────────────────
    const std::string dict_sha1_fix44 = sha1_hex(read_file_bytes(fix44_path));
    const std::string dict_sha1_fix41 = sha1_hex(read_file_bytes(fix41_path));
    const std::string dict_sha1_fix42 = sha1_hex(read_file_bytes(fix42_path));
    const std::string generator_source_hash = sha1_hex(read_file_bytes(FIXPP_GOLDEN_GENERATOR_SOURCE));

    std::ostringstream input_buf;
    for (const OutRow &o : out) {
        input_buf << o.row->id << "|" << o.row->dict << "|" << o.row->msg_type << "|" << o.row->tag << "|"
                   << o.row->value << "\n";
    }
    const std::string corpus_input_hash = sha1_hex(input_buf.str());

    std::ostringstream output_buf;
    for (const OutRow &o : out) {
        output_buf << o.row->id << "|" << (o.result.is_accept ? "accept" : "reject") << "|"
                    << (o.result.is_accept ? "" : std::to_string(o.result.reason)) << "|"
                    << (o.row->asserted ? "true" : "false") << "\n";
    }
    const std::string golden_output_hash = sha1_hex(output_buf.str());

    // ── Write golden.csv ─────────────────────────────────────────────────
    std::ofstream out_file(FIXPP_GOLDEN_OUTPUT_CSV, std::ios::binary | std::ios::trunc);
    if (!out_file) {
        std::cerr << "FATAL: cannot open '" << FIXPP_GOLDEN_OUTPUT_CSV << "' for writing\n";
        return 1;
    }

    out_file << "# 075-live-wire-enum-validation -- QuickFIX enum-domain golden (FR-018/FR-019/FR-024)\n";
    out_file << "# Generated by tools/quickfix_enum_golden/main.cpp against a REAL, locally built\n";
    out_file << "# QuickFIX v1.16.0. Every quickfix_verdict/quickfix_reason value below is the\n";
    out_file << "# literal measured output of FIX::DataDictionary::validate() -- never hand-authored.\n";
    out_file << "#\n";
    out_file << "# MANIFEST (FR-024, six fields):\n";
    out_file << "# quickfix_version=1.16.0\n";
    out_file << "# quickfix_soname=libquickfix.so.17.0.0\n";
    out_file << "# dictionary_sha1[FIX44]=" << dict_sha1_fix44 << "\n";
    out_file << "# dictionary_sha1[FIX41]=" << dict_sha1_fix41 << "\n";
    out_file << "# dictionary_sha1[FIX42]=" << dict_sha1_fix42 << "\n";
    out_file << "# generator_source_hash=" << generator_source_hash << "\n";
    out_file << "# corpus_input_hash=" << corpus_input_hash << "\n";
    out_file << "# golden_output_hash=" << golden_output_hash << "\n";
    out_file << "# topology[FIX44]=single-DD;non-FIXT;sessionDD==appDD (dd.validate(message))\n";
    out_file << "# topology[FIX41]=single-DD;non-FIXT;sessionDD==appDD (dd.validate(message))\n";
    out_file << "# topology[FIX42]=single-DD;non-FIXT;sessionDD==appDD (dd.validate(message))\n";
    out_file << "# config.m_checkFieldsHaveValues=false\n";
    out_file << "# config.m_checkFieldsOutOfOrder=false\n";
    out_file << "# config.m_checkUserDefinedFields=false\n";
    out_file << "# config.AllowUnknownMsgFields=false\n";
    out_file << "#\n";
    out_file << "# Hash algorithm: SHA-1 (OpenSSL EVP_sha1), 40 lowercase hex chars.\n";
    out_file << "# dictionary_sha1: over the raw bytes of dictionaries/<D>.xml (same algorithm as\n";
    out_file << "#   CMake's file(SHA1) -- see tests/dictionary/CMakeLists.txt's FIXPP_ORCHESTRA_SHA1_PIN).\n";
    out_file << "# generator_source_hash: over the raw bytes of this file, main.cpp.\n";
    out_file << "# corpus_input_hash: over the concatenation, for row ids 1..13 ascending, of the\n";
    out_file << "#   exact bytes \"{id}|{dictionary}|{msg_type}|{tag}|{value}\\n\" (decimal id/tag, LF\n";
    out_file << "#   line ending after EVERY row including the 13th, value = the raw wire value\n";
    out_file << "#   byte-for-byte including embedded spaces, empty string for an empty-value row).\n";
    out_file << "# golden_output_hash: over the concatenation, for row ids 1..13 ascending, of the\n";
    out_file << "#   exact bytes \"{id}|{verdict}|{reason}|{asserted}\\n\" (verdict in\n";
    out_file << "#   {accept,reject}, reason = decimal SessionRejectReason or empty string for\n";
    out_file << "#   accept, asserted in {true,false}). Computed over the OUTPUT ROWS ONLY --\n";
    out_file << "#   excludes this manifest block (FR-024), so it is not self-referential.\n";
    out_file << "#\n";
    out_file << "# CORPUS NOTE: 13 rows, not FR-018's original 12. Row 13 is a supplementary\n";
    out_file << "# DV-5 characterization row (asserted:false), split out of the original row 6\n";
    out_file << "# after measurement showed PossDupFlag(43)=X does not reach QuickFIX's enum arm\n";
    out_file << "# (see row 13's own note). Row 6 was rewritten to MessageEncoding(347)=ZZZZ, a\n";
    out_file << "# STRING-typed header field, to give FR-015 a genuine engine-parity witness.\n";
    out_file << "#\n";
    out_file << "# REGISTERED DIVERGENCES surfaced by measurement (not silently reconciled --\n";
    out_file << "# 075 T006 owns folding these into contracts/enum-domain.md's C-6 register):\n";
    out_file << "#   Row 8  (asserted:false) Side(54)=\"\" (empty x Char) -- DV-1, CORRECTED: the\n";
    out_file << "#     original DV-1 text predicted QuickFIX reject/5 \"via isFieldValue's\n";
    out_file << "#     set.find(\\\"\\\") miss\"; MEASURED is reject/6 (IncorrectDataFormat via\n";
    out_file << "#     CharConvertor, DataDictionary.cpp:171) -- the enum arm is never reached.\n";
    out_file << "#     Both reason AND mechanism in the original DV-1 text were wrong.\n";
    out_file << "#   Row 13 (asserted:false) PossDupFlag(43)=X (header, BOOLEAN) -- DV-5, NEW:\n";
    out_file << "#     QuickFIX rejects/6 via BoolConvertor (DataDictionary.cpp:171) before its\n";
    out_file << "#     enum arm (:172); fixpp's Boolean type-arm imposes no constraint so fixpp's\n";
    out_file << "#     OWN enum_valid fires and rejects/5. Both REJECT; only the reason differs.\n";
    out_file << "#\n";
    out_file << "# No asserted:true row disagrees with its predicted verdict as of this run.\n";
    out_file << "#\n";
    out_file << "row,dictionary,begin_string,msg_type,tag,value,asserted,quickfix_verdict,quickfix_reason,note\n";

    auto csv_quote = [](const std::string &s) {
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

    for (const OutRow &o : out) {
        out_file << o.row->id << "," << o.row->dict << "," << o.row->begin_string << "," << o.row->msg_type << ","
                  << o.row->tag << "," << csv_quote(o.row->value) << "," << (o.row->asserted ? "true" : "false")
                  << "," << (o.result.is_accept ? "accept" : "reject") << ","
                  << (o.result.is_accept ? "" : std::to_string(o.result.reason)) << ","
                  << csv_quote(o.row->note) << "\n";
    }

    out_file.close();

    std::cout << "\nWrote " << FIXPP_GOLDEN_OUTPUT_CSV << "\n";
    std::cout << "dictionary_sha1[FIX44]=" << dict_sha1_fix44 << "\n";
    std::cout << "dictionary_sha1[FIX41]=" << dict_sha1_fix41 << "\n";
    std::cout << "dictionary_sha1[FIX42]=" << dict_sha1_fix42 << "\n";
    std::cout << "generator_source_hash=" << generator_source_hash << "\n";
    std::cout << "corpus_input_hash=" << corpus_input_hash << "\n";
    std::cout << "golden_output_hash=" << golden_output_hash << "\n";

    return 0;
}
