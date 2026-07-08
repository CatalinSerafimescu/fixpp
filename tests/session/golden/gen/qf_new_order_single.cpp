// QuickFIX-cpp golden-authoring generator for D (NewOrderSingle) — 061-slim T013/T018.
// OFFLINE TOOLING, not checked in.
#include <iostream>
#include <quickfix/fix44/NewOrderSingle.h>

int main() {
    FIX44::NewOrderSingle nos(
        FIX::ClOrdID("ORD-001"),
        FIX::Side(FIX::Side_BUY),
        FIX::TransactTime(FIX::UtcTimeStamp(10, 0, 0, 1, 1, 2024)),
        FIX::OrdType(FIX::OrdType_LIMIT));
    nos.set(FIX::Symbol("MSFT"));
    nos.set(FIX::OrderQty(100));
    nos.set(FIX::Price(190.5));

    nos.getHeader().setField(FIX::BeginString("FIX.4.4"));
    nos.getHeader().setField(FIX::SenderCompID("S"));
    nos.getHeader().setField(FIX::TargetCompID("T"));
    nos.getHeader().setField(FIX::MsgSeqNum(1));
    nos.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = nos.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
