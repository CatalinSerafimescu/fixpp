// QuickFIX-cpp golden-authoring generator for 8 (ExecutionReport) — 061-slim T014/T018.
// OFFLINE TOOLING, not checked in.
#include <iostream>
#include <quickfix/fix44/ExecutionReport.h>

int main() {
    FIX44::ExecutionReport er(
        FIX::OrderID("ORD-XCH-001"),
        FIX::ExecID("EXEC-001"),
        FIX::ExecType(FIX::ExecType_TRADE),
        FIX::OrdStatus(FIX::OrdStatus_FILLED),
        FIX::Side(FIX::Side_BUY),
        FIX::LeavesQty(0),
        FIX::CumQty(100),
        FIX::AvgPx(190.5));
    er.set(FIX::Symbol("MSFT"));

    er.getHeader().setField(FIX::BeginString("FIX.4.4"));
    er.getHeader().setField(FIX::SenderCompID("S"));
    er.getHeader().setField(FIX::TargetCompID("T"));
    er.getHeader().setField(FIX::MsgSeqNum(1));
    er.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = er.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
