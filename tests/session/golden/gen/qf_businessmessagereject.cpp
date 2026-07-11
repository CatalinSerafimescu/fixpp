// QuickFIX-cpp golden-authoring generator for j (BusinessMessageReject) —
// 069-v44-all-families T011/T012 [US2] (contracts/coverage-and-completeness.md
// C4, family class A-014 BusinessMessageReject: flat, ref-tag echo).
// OFFLINE TOOLING, not checked in to the main build (see gen/README.md).
//
// Required='Y' fields (dictionaries/FIX44.xml:956-964): RefMsgType(372),
// BusinessRejectReason(380).
#include <iostream>
#include <quickfix/fix44/BusinessMessageReject.h>

int main() {
    FIX44::BusinessMessageReject bmr(
        FIX::RefMsgType("D"),
        FIX::BusinessRejectReason(3));  // 3 = UnsupportedMessageType

    bmr.getHeader().setField(FIX::BeginString("FIX.4.4"));
    bmr.getHeader().setField(FIX::SenderCompID("S"));
    bmr.getHeader().setField(FIX::TargetCompID("T"));
    bmr.getHeader().setField(FIX::MsgSeqNum(1));
    bmr.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = bmr.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
