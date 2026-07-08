// QuickFIX-cpp golden-authoring generator for E (NewOrderList) — 061-slim T017.
// OFFLINE TOOLING, not checked in.
#include <iostream>
#include <quickfix/fix44/NewOrderList.h>
#include <quickfix/Session.h>

int main() {
    FIX44::NewOrderList list;
    list.set(FIX::ListID("LIST1"));
    list.set(FIX::BidType(3));
    list.set(FIX::TotNoOrders(2));

    // Order 1: carries the nested 453->802 party chain.
    {
        FIX44::NewOrderList::NoOrders order;
        order.set(FIX::ClOrdID("ORD1"));
        order.set(FIX::ListSeqNo(1));
        order.set(FIX::Side(FIX::Side_BUY));
        order.set(FIX::Symbol("MSFT"));
        order.set(FIX::OrderQty(150.75));

        FIX44::NewOrderList::NoOrders::NoPartyIDs party;
        party.set(FIX::PartyID("PARTY1"));
        party.set(FIX::PartyIDSource('D'));
        party.set(FIX::PartyRole(1));

        FIX44::NewOrderList::NoOrders::NoPartyIDs::NoPartySubIDs sub;
        sub.set(FIX::PartySubID("SUB1"));
        sub.set(FIX::PartySubIDType(1));
        party.addGroup(sub);

        order.addGroup(party);
        list.addGroup(order);
    }

    // Order 2: count-of-zero (present-but-empty) NoPartyIDs.
    {
        FIX44::NewOrderList::NoOrders order;
        order.set(FIX::ClOrdID("ORD2"));
        order.set(FIX::ListSeqNo(2));
        order.set(FIX::Side(FIX::Side_SELL));
        order.set(FIX::Symbol("IBM"));
        order.set(FIX::OrderQty(50));
        order.setField(FIX::NoPartyIDs(0));
        list.addGroup(order);
    }

    list.getHeader().setField(FIX::BeginString("FIX.4.4"));
    list.getHeader().setField(FIX::SenderCompID("S"));
    list.getHeader().setField(FIX::TargetCompID("T"));
    list.getHeader().setField(FIX::MsgSeqNum(1));
    list.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = list.toString();
    // Print with SOH shown as '|' for readability.
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
