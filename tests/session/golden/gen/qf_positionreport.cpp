// QuickFIX-cpp golden-authoring generator for AP (PositionReport) —
// 069-v44-all-families T011/T012 [US2] (contracts/coverage-and-completeness.md
// C4, family class C-002 Position: NoPositions group).
// OFFLINE TOOLING, not checked in to the main build (see gen/README.md).
//
// Required='Y' fields (dictionaries/FIX44.xml:1815-1843): PosMaintRptID(721),
// PosReqResult(728), ClearingBusinessDate(715), Account(1), AccountType(581),
// SettlPrice(730), SettlPriceType(731), PriorSettlPrice(734) (constructor
// FIX44::PositionReport args, confirmed against the header). PositionQty
// (component, required='Y'; internal group NoPositions(702) itself
// required='N') is populated — 1 entry (PosType, LongQty) — per C4's table
// note.
#include <iostream>
#include <quickfix/fix44/PositionReport.h>

int main() {
    FIX44::PositionReport pr(
        FIX::PosMaintRptID("POSRPT1"),
        FIX::PosReqResult(0),
        FIX::ClearingBusinessDate("20240101"),
        FIX::Account("ACCT1"),
        FIX::AccountType(1),
        FIX::SettlPrice(100.5),
        FIX::SettlPriceType(1),
        FIX::PriorSettlPrice(99.75));

    // NoPositions (702, optional group — populated per C4's table note).
    {
        FIX44::PositionReport::NoPositions pos;
        pos.set(FIX::PosType("TQ"));
        pos.set(FIX::LongQty(100));
        pr.addGroup(pos);
    }

    pr.getHeader().setField(FIX::BeginString("FIX.4.4"));
    pr.getHeader().setField(FIX::SenderCompID("S"));
    pr.getHeader().setField(FIX::TargetCompID("T"));
    pr.getHeader().setField(FIX::MsgSeqNum(1));
    pr.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = pr.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
