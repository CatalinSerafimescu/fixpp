// QuickFIX-cpp golden-authoring generator for N (ListStatus) —
// 069-v44-all-families T011/T012 [US2] (contracts/coverage-and-completeness.md
// C4, family class A-019 list-handling: NoOrders group).
// OFFLINE TOOLING, not checked in to the main build (see gen/README.md).
//
// Required='Y' fields (dictionaries/FIX44.xml:628-639, FIX44::ListStatus
// ctor): ListID(66), ListStatusType(429), NoRpts(82), ListOrderStatus(431),
// RptSeq(83), TotNoOrders(68). OrdListStatGrp/NoOrders(73) is a REQUIRED
// group (dictionaries/FIX44.xml:3138 group required='Y') — 1 entry,
// ClOrdID(11)+CumQty(14)+OrdStatus(39)+LeavesQty(151)+CxlQty(84), all
// required='Y' within the group.
#include <iostream>
#include <quickfix/fix44/ListStatus.h>

int main() {
    FIX44::ListStatus ls(
        FIX::ListID("LIST1"),
        FIX::ListStatusType(1),
        FIX::NoRpts(1),
        FIX::ListOrderStatus(1),
        FIX::RptSeq(1),
        FIX::TotNoOrders(1));

    // NoOrders (73, required group): 1 entry.
    {
        FIX44::ListStatus::NoOrders order;
        order.set(FIX::ClOrdID("ORD1"));
        order.set(FIX::CumQty(100));
        order.set(FIX::OrdStatus(FIX::OrdStatus_FILLED));
        order.set(FIX::LeavesQty(0));
        order.set(FIX::CxlQty(0));
        ls.addGroup(order);
    }

    ls.getHeader().setField(FIX::BeginString("FIX.4.4"));
    ls.getHeader().setField(FIX::SenderCompID("S"));
    ls.getHeader().setField(FIX::TargetCompID("T"));
    ls.getHeader().setField(FIX::MsgSeqNum(1));
    ls.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = ls.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
