// QuickFIX-cpp golden-authoring generator for 9 (OrderCancelReject) — 061-slim T017/T018.
// OFFLINE TOOLING, not checked in.
#include <iostream>
#include <quickfix/fix44/OrderCancelReject.h>

int main() {
    FIX44::OrderCancelReject ocr(
        FIX::OrderID("ORDER1"),
        FIX::ClOrdID("CLORD2"),
        FIX::OrigClOrdID("CLORD1"),
        FIX::OrdStatus(FIX::OrdStatus_REJECTED),
        FIX::CxlRejResponseTo(FIX::CxlRejResponseTo_ORDER_CANCEL_REQUEST));
    ocr.set(FIX::CxlRejReason(0));

    ocr.getHeader().setField(FIX::BeginString("FIX.4.4"));
    ocr.getHeader().setField(FIX::SenderCompID("S"));
    ocr.getHeader().setField(FIX::TargetCompID("T"));
    ocr.getHeader().setField(FIX::MsgSeqNum(1));
    ocr.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = ocr.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
