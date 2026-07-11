// QuickFIX-cpp golden-authoring generator for AE (TradeCaptureReport) —
// 069-v44-all-families T011/T012 [US2] (contracts/coverage-and-completeness.md
// C4, family class P-008 post-trade: the REQUIRED group-heavy/nested case,
// NoSides / NoLegs both populated; NoSides carries a nested NoPartyIDs entry).
// OFFLINE TOOLING, not checked in to the main build (see gen/README.md).
//
// Required='Y' fields (dictionaries/FIX44.xml:1435-1494): TradeReportID(571),
// PreviouslyReported(570), Instrument(component, no individually-required
// sub-field — Symbol(55) seeded for a sane instrument identity), LastQty(32),
// LastPx(31), TradeDate(75), TransactTime(60), TrdCapRptSideGrp/NoSides(552)
// (component required — entries require Side(54)+OrderID(37)).
// NoLegs(555, TrdInstrmtLegGrp) is required='N' but populated (1 entry,
// LegSymbol(600)) to exercise the group-heavy/nested exemplar per C4's table
// note. NoPartyIDs(453) nested inside the NoSides entry demonstrates real
// group-in-group nesting (Side/OrderID's group carries a party sub-group).
#include <iostream>
#include <quickfix/fix44/TradeCaptureReport.h>

int main() {
    FIX44::TradeCaptureReport tcr(
        FIX::TradeReportID("TCR001"),
        FIX::PreviouslyReported(false),
        FIX::LastQty(100),
        FIX::LastPx(50.25),
        FIX::TradeDate("20240101"),
        FIX::TransactTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));
    tcr.set(FIX::Symbol("MSFT"));

    // NoSides (552, required): 1 entry, Side + OrderID + nested NoPartyIDs.
    {
        FIX44::TradeCaptureReport::NoSides side;
        side.set(FIX::Side(FIX::Side_BUY));
        side.set(FIX::OrderID("ORDER1"));

        FIX44::TradeCaptureReport::NoSides::NoPartyIDs party;
        party.set(FIX::PartyID("PARTY1"));
        party.set(FIX::PartyIDSource('D'));
        party.set(FIX::PartyRole(1));
        side.addGroup(party);

        tcr.addGroup(side);
    }

    // NoLegs (555, optional — populated for the group-heavy/nested exemplar).
    {
        FIX44::TradeCaptureReport::NoLegs leg;
        leg.set(FIX::LegSymbol("IBM"));
        tcr.addGroup(leg);
    }

    tcr.getHeader().setField(FIX::BeginString("FIX.4.4"));
    tcr.getHeader().setField(FIX::SenderCompID("S"));
    tcr.getHeader().setField(FIX::TargetCompID("T"));
    tcr.getHeader().setField(FIX::MsgSeqNum(1));
    tcr.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = tcr.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
