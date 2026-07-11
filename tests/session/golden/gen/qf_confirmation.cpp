// QuickFIX-cpp golden-authoring generator for AK (Confirmation) —
// 069-v44-all-families T011/T012 [US2] (contracts/coverage-and-completeness.md
// C4, family class P-005 post-trade confirmation).
// OFFLINE TOOLING, not checked in to the main build (see gen/README.md).
//
// Required='Y' fields (dictionaries/FIX44.xml:1646-1673, confirmed against
// the FIX44::Confirmation ctor): ConfirmID(664), ConfirmTransType(666),
// ConfirmType(773), ConfirmStatus(665), TransactTime(60), TradeDate(75),
// AllocQty(80), Side(54), AllocAccount(79), AvgPx(6), GrossTradeAmt(381),
// NetMoney(118). CpctyConfGrp/NoCapacities(862) is a REQUIRED group
// (dictionaries/FIX44.xml:2799 group required='Y') — 1 entry,
// OrderCapacity(528)+OrderCapacityQty(863), both required='Y' within the
// group.
#include <iostream>
#include <quickfix/fix44/Confirmation.h>

int main() {
    FIX44::Confirmation conf(
        FIX::ConfirmID("CONF1"),
        FIX::ConfirmTransType(0),
        FIX::ConfirmType(1),
        FIX::ConfirmStatus(1),
        FIX::TransactTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)),
        FIX::TradeDate("20240101"),
        FIX::AllocQty(100),
        FIX::Side(FIX::Side_BUY),
        FIX::AllocAccount("ACCT1"),
        FIX::AvgPx(50.25),
        FIX::GrossTradeAmt(5025),
        FIX::NetMoney(5025));

    // NoCapacities (862, required group): 1 entry.
    {
        FIX44::Confirmation::NoCapacities cap;
        cap.set(FIX::OrderCapacity('A'));
        cap.set(FIX::OrderCapacityQty(100));
        conf.addGroup(cap);
    }

    conf.getHeader().setField(FIX::BeginString("FIX.4.4"));
    conf.getHeader().setField(FIX::SenderCompID("S"));
    conf.getHeader().setField(FIX::TargetCompID("T"));
    conf.getHeader().setField(FIX::MsgSeqNum(1));
    conf.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = conf.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
