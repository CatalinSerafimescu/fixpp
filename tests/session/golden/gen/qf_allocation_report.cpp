// QuickFIX-cpp golden-authoring generator for AS (AllocationReport) — 061-slim T017/T018.
// OFFLINE TOOLING, not checked in.
#include <iostream>
#include <quickfix/fix44/AllocationReport.h>

int main() {
    FIX44::AllocationReport ar(
        FIX::AllocReportID("ALLOCRPT1"),
        FIX::AllocTransType(FIX::AllocTransType_NEW),
        FIX::AllocReportType(9),  // ACCEPT (FIX::AllocReportType_ACCEPT)
        FIX::AllocStatus(FIX::AllocStatus_ACCEPTED),
        FIX::AllocNoOrdersType(0),
        FIX::Side(FIX::Side_BUY),
        FIX::Quantity(1000),
        FIX::AvgPx(25.5),
        FIX::TradeDate("20240101"));
    ar.set(FIX::Symbol("MSFT"));

    FIX44::AllocationReport::NoPartyIDs party;
    party.set(FIX::PartyID("PARTY1"));
    party.set(FIX::PartyIDSource('D'));
    party.set(FIX::PartyRole(1));

    FIX44::AllocationReport::NoPartyIDs::NoPartySubIDs sub;
    sub.set(FIX::PartySubID("SUB1"));
    sub.set(FIX::PartySubIDType(1));
    party.addGroup(sub);

    ar.addGroup(party);

    ar.getHeader().setField(FIX::BeginString("FIX.4.4"));
    ar.getHeader().setField(FIX::SenderCompID("S"));
    ar.getHeader().setField(FIX::TargetCompID("T"));
    ar.getHeader().setField(FIX::MsgSeqNum(1));
    ar.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = ar.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
