// QuickFIX-cpp golden-authoring generator for y (SecurityList) —
// 069-v44-all-families T011/T012 [US2] (contracts/coverage-and-completeness.md
// C4, family class A-025 reference data: NoRelatedSym group).
// OFFLINE TOOLING, not checked in to the main build (see gen/README.md).
//
// Required='Y' fields (dictionaries/FIX44.xml:1218-1224): SecurityReqID(320),
// SecurityResponseID(322), SecurityRequestResult(560) (FIX44::SecurityList
// ctor). SecListGrp/NoRelatedSym(146) is required='N' but populated — 1
// entry, Symbol(55) — per C4's table note.
#include <iostream>
#include <quickfix/fix44/SecurityList.h>

int main() {
    FIX44::SecurityList sl(
        FIX::SecurityReqID("SECREQ1"),
        FIX::SecurityResponseID("SECRESP1"),
        FIX::SecurityRequestResult(0));

    // NoRelatedSym (146, optional group — populated per C4's table note).
    {
        FIX44::SecurityList::NoRelatedSym entry;
        entry.set(FIX::Symbol("MSFT"));
        sl.addGroup(entry);
    }

    sl.getHeader().setField(FIX::BeginString("FIX.4.4"));
    sl.getHeader().setField(FIX::SenderCompID("S"));
    sl.getHeader().setField(FIX::TargetCompID("T"));
    sl.getHeader().setField(FIX::MsgSeqNum(1));
    sl.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = sl.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
