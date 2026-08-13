// tools/quickfix_v42_exemplar_golden/gen_v42_mass_quote.cpp
// 082-structural-group-detection T044 [US4] — US4 AC1 / SC-007.
//
// Authors tests/session/golden/v42_mass_quote.fix: a FIX 4.2 MassQuote(i) body with
// a populated NoQuoteSets(296) -> NoQuoteEntries(295) nesting, serialised by a REAL
// QuickFIX-cpp so the golden is an INDEPENDENT oracle rather than a transcript of
// fixpp's own emitter.
//
// DELIBERATELY NOT WIRED INTO CMake — same rationale as tools/quickfix_{enum,
// required}_golden's OFF-by-default guards: reference-engines/ lives outside the
// submodule's git boundary and is NEVER present in CI, so a wired target could only
// fail there. The source is checked in so the golden is reproducible rather than
// folklore. Compile + run locally:
//
//   R=<repo>/../../../reference-engines/quickfix-cpp
//   g++ -std=c++17 -I"$R/include" tools/quickfix_v42_exemplar_golden/gen_v42_mass_quote.cpp \
//       -L"$R/lib" -lquickfix -o /tmp/gen_v42_mass_quote
//   LD_LIBRARY_PATH="$R/lib" /tmp/gen_v42_mass_quote > tests/session/golden/v42_mass_quote.fix
//
// Engine: QuickFIX-cpp v1.16.0, libquickfix.so.17 (project_reference_engines_setup).
// Authored 2026-08-12; output verified byte-identical to the checked-in golden.
//
// WHY 311 IS PRESENT AND THE FIX44 SIBLING LACKS IT: FIX 4.2 marks
// UnderlyingSymbol(311) required inside NoQuoteSets, FIX 4.4 does not. QuickFIX's own
// FIX42 message_order(302,311,312,...) places it second, and fixpp's independently
// derived G_296_2Args required set is {302, 311, 304} — two independent sources
// agreeing, which is the whole point of using a reference engine here.
#include <quickfix/fix42/MassQuote.h>
#include <quickfix/Message.h>
#include <iostream>
#include <string>

int main() {
    FIX42::MassQuote m;
    m.set(FIX::QuoteID("QID-100"));

    FIX42::MassQuote::NoQuoteSets set1;
    set1.set(FIX::QuoteSetID("QS1"));
    set1.set(FIX::UnderlyingSymbol("AAPL"));   // FIX42 requires 311 (FIX44 does not)
    set1.set(FIX::TotQuoteEntries(1));

    FIX42::MassQuote::NoQuoteSets::NoQuoteEntries e1;
    e1.set(FIX::QuoteEntryID("QE1"));
    e1.set(FIX::BidPx(10.5));
    e1.set(FIX::OfferPx(10.75));
    set1.addGroup(e1);

    m.addGroup(set1);

    std::string s = m.toString();
    // Strip session header (8,9) and trailer (10) -> body-only, per PROVENANCE.md.
    std::string body;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find('\001', i);
        if (j == std::string::npos) break;
        std::string f = s.substr(i, j - i);
        size_t eq = f.find('=');
        std::string tag = f.substr(0, eq);
        if (tag != "8" && tag != "9" && tag != "10") { body += f; body += '\001'; }
        i = j + 1;
    }
    // Print with \x01 escaped, prefixed for parse_golden.
    std::cout << "> ";
    for (char c : body) { if (c == '\001') std::cout << "\\x01"; else std::cout << c; }
    std::cout << "\n";
    return 0;
}
